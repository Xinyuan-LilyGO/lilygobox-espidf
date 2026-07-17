/*
 * @Description: RF 聊天热缓存、会话摘要与 LittleFS 日志仓库实现
 * @Author: LILYGO_L
 * @Date: 2026-07-17 00:00:00
 * @LastEditTime: 2026-07-17 17:22:18
 * @License: GPL 3.0
 */
#include "app/rf_chat_repository.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include "app/storage/littlefs_storage.h"
#include "base/logger.h"
#include "esp_heap_caps.h"
#include "esp_random.h"

namespace lilygo_box::app {
namespace {

// PSRAM 分配失败时使用的内部 RAM 消息容量。
constexpr size_t kFallbackGlobalCapacity = 32;
// 日志达到容量上限后单次压缩保留的记录数量。
constexpr size_t kCompactionTarget =
    kRfChatStorageCapacity - kRfChatStorageCapacity / 8;
// RF 聊天记录在 LittleFS 中的分层目录名称。
constexpr char kApplicationDirectory[] = "lilygobox";
constexpr char kDataDirectory[] = "data";
constexpr char kRfDirectory[] = "rf";
constexpr char kChatDirectory[] = "chat";
constexpr char kChatLogFile[] = "messages.rfchat";
constexpr char kChatTempFile[] = "messages.tmp";

// 固定长度聊天记录的格式标识和版本。
constexpr uint32_t kRecordMagic = 0x52464348;
constexpr uint16_t kRecordVersion = 1;
// 固定长度聊天记录中各字段的字节偏移。
constexpr size_t kMagicOffset = 0;
constexpr size_t kVersionOffset = 4;
constexpr size_t kRecordSizeOffset = 6;
constexpr size_t kSequenceOffset = 8;
constexpr size_t kProfileIdOffset = 16;
constexpr size_t kTextLengthOffset = 20;
constexpr size_t kMessageTypeOffset = 22;
constexpr size_t kDeliveryOffset = 23;
constexpr size_t kRssiOffset = 24;
constexpr size_t kSnrOffset = 25;
constexpr size_t kTimeOffset = 28;
constexpr size_t kTextOffset = kTimeOffset + kRfChatTimeCapacity;
constexpr size_t kChecksumOffset = kTextOffset + kRfChatTextCapacity;
constexpr size_t kDiskRecordSize = kChecksumOffset + sizeof(uint32_t);

// 固定长度二进制聊天记录缓冲区。
using DiskRecord = std::array<uint8_t, kDiskRecordSize>;
static_assert(kDiskRecordSize <= UINT16_MAX,
    "RF chat disk record size exceeds its file format field");

struct ChatLogReadBuffer {
  // 当前聊天日志文件路径。
  char path[224] = {};
  // 从 LittleFS 读取的单条固定长度记录。
  DiskRecord record = {};
  // 完成格式校验和解码后的聊天消息。
  RfChatMessage message;
};

/**
 * @brief 将字符串安全复制到固定容量缓冲区
 * @param destination 目标缓冲区
 * @param destination_size 目标缓冲区容量
 * @param source 源字符串，允许为空
 */
void CopyBoundedString(
    char* destination, size_t destination_size, const char* source) {
  if (destination == nullptr || destination_size == 0) {
    return;
  }
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  const size_t length = std::min(std::strlen(source), destination_size - 1);
  std::memcpy(destination, source, length);
  destination[length] = '\0';
}

/**
 * @brief 在固定容量路径末尾安全追加一个目录或文件名
 * @param path 待追加的零结尾路径缓冲区
 * @param path_size 路径缓冲区容量
 * @param component 待追加的目录或文件名
 * @return 路径有效且追加后未发生截断时返回 true
 */
bool AppendPathComponent(
    char* path, size_t path_size, const char* component) {
  if (path == nullptr || path_size == 0 || component == nullptr) {
    return false;
  }
  size_t path_length = 0;
  while (path_length < path_size && path[path_length] != '\0') {
    ++path_length;
  }
  if (path_length == path_size) {
    return false;
  }
  const size_t component_length = std::strlen(component);
  const bool add_separator = path_length > 0 && path[path_length - 1] != '/' &&
                             component_length > 0 && component[0] != '/';
  const size_t remaining = path_size - path_length;
  const size_t separator_length = add_separator ? 1 : 0;
  if (separator_length >= remaining ||
      component_length >= remaining - separator_length) {
    return false;
  }
  if (add_separator) {
    path[path_length++] = '/';
  }
  std::memcpy(path + path_length, component, component_length);
  path[path_length + component_length] = '\0';
  return true;
}

/**
 * @brief 按小端字节序写入 16 位无符号整数
 * @param output 两字节输出缓冲区
 * @param value 待写入数值
 */
void StoreUint16(uint8_t* output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
}

/**
 * @brief 按小端字节序写入 32 位无符号整数
 * @param output 四字节输出缓冲区
 * @param value 待写入数值
 */
void StoreUint32(uint8_t* output, uint32_t value) {
  for (size_t index = 0; index < sizeof(value); ++index) {
    output[index] = static_cast<uint8_t>(value >> (index * 8));
  }
}

/**
 * @brief 按小端字节序写入 64 位无符号整数
 * @param output 八字节输出缓冲区
 * @param value 待写入数值
 */
void StoreUint64(uint8_t* output, uint64_t value) {
  for (size_t index = 0; index < sizeof(value); ++index) {
    output[index] = static_cast<uint8_t>(value >> (index * 8));
  }
}

/**
 * @brief 按小端字节序读取 16 位无符号整数
 * @param input 两字节输入缓冲区
 * @return 解码后的数值
 */
uint16_t LoadUint16(const uint8_t* input) {
  return static_cast<uint16_t>(input[0]) | static_cast<uint16_t>(input[1]) << 8;
}

/**
 * @brief 按小端字节序读取 32 位无符号整数
 * @param input 四字节输入缓冲区
 * @return 解码后的数值
 */
uint32_t LoadUint32(const uint8_t* input) {
  uint32_t value = 0;
  for (size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<uint32_t>(input[index]) << (index * 8);
  }
  return value;
}

/**
 * @brief 按小端字节序读取 64 位无符号整数
 * @param input 八字节输入缓冲区
 * @return 解码后的数值
 */
uint64_t LoadUint64(const uint8_t* input) {
  uint64_t value = 0;
  for (size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<uint64_t>(input[index]) << (index * 8);
  }
  return value;
}

/**
 * @brief 计算磁盘记录使用的 FNV-1a 校验值
 * @param data 待校验数据
 * @param size 数据长度
 * @return 32 位校验值
 */
uint32_t CalculateChecksum(const uint8_t* data, size_t size) {
  constexpr uint32_t kFnvOffsetBasis = 2166136261U;
  constexpr uint32_t kFnvPrime = 16777619U;
  uint32_t checksum = kFnvOffsetBasis;
  for (size_t index = 0; index < size; ++index) {
    checksum ^= data[index];
    checksum *= kFnvPrime;
  }
  return checksum;
}

/**
 * @brief 判断消息是否已经得到最终发送结果
 * @param delivery 消息发送状态
 * @return 消息无需继续等待射频结果时返回 true
 */
bool IsFinalDelivery(RfChatDeliveryState delivery) {
  return delivery != RfChatDeliveryState::kSending;
}

/**
 * @brief 创建目录或确认同名路径已经是目录
 * @param path 待创建目录路径
 * @return 目录可用时返回 true
 */
bool CreateDirectory(const char* path) {
  if (path == nullptr) {
    return false;
  }
  if (mkdir(path, 0775) == 0) {
    return true;
  }
  if (errno != EEXIST) {
    return false;
  }
  struct stat information = {};
  return stat(path, &information) == 0 && S_ISDIR(information.st_mode);
}

/**
 * @brief 判断 RF 配置 ID 是否包含在指定数组中
 * @param profile_ids RF 配置 ID 数组
 * @param profile_count RF 配置数量
 * @param profile_id 待检查配置 ID
 * @return 数组包含指定配置时返回 true
 */
bool ContainsProfileId(
    const uint32_t* profile_ids, size_t profile_count, uint32_t profile_id) {
  return profile_ids != nullptr &&
         std::find(profile_ids, profile_ids + profile_count, profile_id) !=
             profile_ids + profile_count;
}

/**
 * @brief 将聊天消息编码为固定长度磁盘记录
 * @param message 待编码消息
 * @param record 磁盘记录输出
 * @return 消息字段有效并完成编码时返回 true
 */
bool EncodeRecord(const RfChatMessage& message, DiskRecord* record) {
  if (record == nullptr || message.profile_id == 0 || message.sequence == 0) {
    return false;
  }
  record->fill(0);
  const size_t text_length =
      std::min(std::strlen(message.text), kRfChatTextCapacity - 1);
  StoreUint32(record->data() + kMagicOffset, kRecordMagic);
  StoreUint16(record->data() + kVersionOffset, kRecordVersion);
  StoreUint16(record->data() + kRecordSizeOffset,
      static_cast<uint16_t>(kDiskRecordSize));
  StoreUint64(record->data() + kSequenceOffset, message.sequence);
  StoreUint32(record->data() + kProfileIdOffset, message.profile_id);
  StoreUint16(
      record->data() + kTextLengthOffset, static_cast<uint16_t>(text_length));
  (*record)[kMessageTypeOffset] = static_cast<uint8_t>(message.type);
  (*record)[kDeliveryOffset] = static_cast<uint8_t>(message.delivery);
  (*record)[kRssiOffset] = static_cast<uint8_t>(message.rssi_dbm);
  (*record)[kSnrOffset] = static_cast<uint8_t>(message.snr_db);
  std::memcpy(record->data() + kTimeOffset, message.time, kRfChatTimeCapacity);
  std::memcpy(record->data() + kTextOffset, message.text, text_length);
  StoreUint32(record->data() + kChecksumOffset,
      CalculateChecksum(record->data(), kChecksumOffset));
  return true;
}

/**
 * @brief 校验并解码固定长度磁盘记录
 * @param record 待解码磁盘记录
 * @param message 消息输出
 * @return 记录格式和校验值均有效时返回 true
 */
bool DecodeRecord(const DiskRecord& record, RfChatMessage* message) {
  if (message == nullptr ||
      LoadUint32(record.data() + kMagicOffset) != kRecordMagic ||
      LoadUint16(record.data() + kVersionOffset) != kRecordVersion ||
      LoadUint16(record.data() + kRecordSizeOffset) != kDiskRecordSize ||
      LoadUint32(record.data() + kChecksumOffset) !=
          CalculateChecksum(record.data(), kChecksumOffset)) {
    return false;
  }
  const size_t text_length = LoadUint16(record.data() + kTextLengthOffset);
  const auto type = static_cast<RfChatMessageType>(record[kMessageTypeOffset]);
  auto delivery = static_cast<RfChatDeliveryState>(record[kDeliveryOffset]);
  if (text_length >= kRfChatTextCapacity || type > RfChatMessageType::kSystem ||
      delivery > RfChatDeliveryState::kFailed) {
    return false;
  }
  if (delivery == RfChatDeliveryState::kSending) {
    delivery = RfChatDeliveryState::kFailed;
  }
  *message = RfChatMessage{};
  message->sequence = LoadUint64(record.data() + kSequenceOffset);
  message->profile_id = LoadUint32(record.data() + kProfileIdOffset);
  message->type = type;
  message->delivery = delivery;
  message->rssi_dbm = static_cast<int8_t>(record[kRssiOffset]);
  message->snr_db = static_cast<int8_t>(record[kSnrOffset]);
  std::memcpy(message->time, record.data() + kTimeOffset, kRfChatTimeCapacity);
  message->time[kRfChatTimeCapacity - 1] = '\0';
  std::memcpy(message->text, record.data() + kTextOffset, text_length);
  message->text[text_length] = '\0';
  return message->sequence != 0 && message->profile_id != 0;
}

}  // namespace

struct RfChatRepository::Entry {
  // 缓存中保存的完整聊天消息。
  RfChatMessage message;
  // 消息在当前运行会话中的显示顺序。
  uint64_t order = 0;
  // 当前条目是否保存有效消息。
  bool used = false;
  // 当前消息是否需要写入 LittleFS。
  bool dirty = false;
  // 当前消息是否已经进入等待写入队列。
  bool queued = false;
};

struct RfChatRepository::ProfileState {
  // 状态所属 RF 配置的稳定 ID。
  uint32_t profile_id = 0;
  // 尚未进入聊天页面查看的消息数量。
  uint16_t unread_count = 0;
  // 当前 RF 配置最近一次访问的顺序。
  uint64_t last_access_order = 0;
  // RF 主页面显示的最新消息摘要。
  char latest_message[kRfChatSummaryCapacity] = {};
  // 最新消息的本地时间文本。
  char latest_time[kRfChatTimeCapacity] = {};
  // 当前配置是否已经从 LittleFS 补载历史记录。
  bool history_loaded = false;
};

class RfChatRepository::ScopedLock final {
 public:
  explicit ScopedLock(const RfChatRepository* repository)
      : repository_(repository),
        locked_(repository_ != nullptr && repository_->Lock()) {}

  ~ScopedLock() {
    if (locked_) {
      repository_->Unlock();
    }
  }

  bool locked() const { return locked_; }

 private:
  const RfChatRepository* repository_ = nullptr;
  bool locked_ = false;
};

RfChatRepository::~RfChatRepository() {
  heap_caps_free(entries_);
  heap_caps_free(profile_states_);
}

bool RfChatRepository::Lock() const {
  if (mutex_ == nullptr) {
    mutex_ = xSemaphoreCreateRecursiveMutexStatic(&mutex_buffer_);
  }
  return mutex_ != nullptr &&
         xSemaphoreTakeRecursive(mutex_, portMAX_DELAY) == pdTRUE;
}

void RfChatRepository::Unlock() const {
  if (mutex_ != nullptr) {
    xSemaphoreGiveRecursive(mutex_);
  }
}

bool RfChatRepository::InitializeCache() {
  if (entries_ != nullptr && profile_states_ != nullptr) {
    return true;
  }
  entries_ = static_cast<Entry*>(heap_caps_calloc(kRfChatGlobalCapacity,
      sizeof(Entry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (entries_ != nullptr) {
    capacity_ = kRfChatGlobalCapacity;
  } else {
    entries_ = static_cast<Entry*>(heap_caps_calloc(kFallbackGlobalCapacity,
        sizeof(Entry), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    capacity_ = entries_ == nullptr ? 0 : kFallbackGlobalCapacity;
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "RF chat PSRAM allocation failed, capacity=%u\n",
        static_cast<unsigned>(capacity_));
  }
  profile_states_ =
      static_cast<ProfileState*>(heap_caps_calloc(kRfProfileCapacity,
          sizeof(ProfileState), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (entries_ == nullptr || profile_states_ == nullptr) {
    heap_caps_free(entries_);
    heap_caps_free(profile_states_);
    entries_ = nullptr;
    profile_states_ = nullptr;
    capacity_ = 0;
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "RF chat cache allocation failed\n");
    return false;
  }
  const uint32_t session_id = std::max(esp_random(), uint32_t{1});
  next_sequence_ = (static_cast<uint64_t>(session_id) << 32) | 1;
  return true;
}

bool RfChatRepository::Initialize() {
  ScopedLock lock(this);
  return lock.locked() && InitializeCache();
}

RfChatRepository::ProfileState* RfChatRepository::FindProfileState(
    uint32_t profile_id, bool create) {
  if (profile_id == 0 || profile_states_ == nullptr) {
    return nullptr;
  }
  ProfileState* empty = nullptr;
  for (size_t index = 0; index < kRfProfileCapacity; ++index) {
    ProfileState& state = profile_states_[index];
    if (state.profile_id == profile_id) {
      return &state;
    }
    if (empty == nullptr && state.profile_id == 0) {
      empty = &state;
    }
  }
  if (create && empty != nullptr) {
    *empty = ProfileState{};
    empty->profile_id = profile_id;
    return empty;
  }
  return nullptr;
}

const RfChatRepository::ProfileState* RfChatRepository::FindProfileState(
    uint32_t profile_id) const {
  if (profile_id == 0 || profile_states_ == nullptr) {
    return nullptr;
  }
  for (size_t index = 0; index < kRfProfileCapacity; ++index) {
    if (profile_states_[index].profile_id == profile_id) {
      return &profile_states_[index];
    }
  }
  return nullptr;
}

RfChatRepository::Entry* RfChatRepository::FindEntry(uint64_t sequence) {
  if (entries_ == nullptr || sequence == 0) {
    return nullptr;
  }
  for (size_t index = 0; index < capacity_; ++index) {
    if (entries_[index].used && entries_[index].message.sequence == sequence) {
      return &entries_[index];
    }
  }
  return nullptr;
}

RfChatRepository::Entry* RfChatRepository::SelectInsertionEntry(
    uint32_t profile_id) {
  if (entries_ == nullptr || capacity_ == 0 || profile_id == 0) {
    return nullptr;
  }
  size_t profile_count = 0;
  Entry* oldest_profile_entry = nullptr;
  Entry* oldest_global_entry = nullptr;
  Entry* least_recent_other_entry = nullptr;
  uint64_t least_recent_other_order = UINT64_MAX;
  const size_t profile_capacity =
      std::min(kRfChatActiveProfileCapacity, capacity_);
  for (size_t index = 0; index < capacity_; ++index) {
    Entry& entry = entries_[index];
    if (!entry.used) {
      continue;
    }
    if (entry.message.profile_id == profile_id) {
      ++profile_count;
    }
    // 未完成发送或尚未落盘的消息必须保留，避免状态或记录丢失。
    if (!IsFinalDelivery(entry.message.delivery) || entry.dirty) {
      continue;
    }
    if (oldest_global_entry == nullptr ||
        entry.order < oldest_global_entry->order) {
      oldest_global_entry = &entry;
    }
    if (entry.message.profile_id == profile_id) {
      if (oldest_profile_entry == nullptr ||
          entry.order < oldest_profile_entry->order) {
        oldest_profile_entry = &entry;
      }
    } else {
      const ProfileState* state = FindProfileState(entry.message.profile_id);
      const uint64_t access_order =
          state == nullptr ? 0 : state->last_access_order;
      if (least_recent_other_entry == nullptr ||
          access_order < least_recent_other_order ||
          (access_order == least_recent_other_order &&
              entry.order < least_recent_other_entry->order)) {
        least_recent_other_entry = &entry;
        least_recent_other_order = access_order;
      }
    }
  }
  if (profile_count >= profile_capacity && oldest_profile_entry != nullptr) {
    return oldest_profile_entry;
  }
  for (size_t offset = 0; offset < capacity_; ++offset) {
    const size_t index = (insertion_cursor_ + offset) % capacity_;
    if (!entries_[index].used) {
      insertion_cursor_ = (index + 1) % capacity_;
      return &entries_[index];
    }
  }
  return least_recent_other_entry == nullptr ? oldest_global_entry
                                             : least_recent_other_entry;
}

void RfChatRepository::QueueEntry(Entry* entry) {
  if (entry == nullptr || !entry->used || !entry->dirty || entry->queued ||
      !IsFinalDelivery(entry->message.delivery) ||
      pending_count_ >= kRfChatPendingCapacity) {
    return;
  }
  const size_t tail = (pending_head_ + pending_count_) % kRfChatPendingCapacity;
  pending_sequences_[tail] = entry->message.sequence;
  entry->queued = true;
  ++pending_count_;
}

void RfChatRepository::RefillPendingQueue() {
  while (pending_count_ < kRfChatPendingCapacity) {
    Entry* oldest = nullptr;
    for (size_t index = 0; index < capacity_; ++index) {
      Entry& entry = entries_[index];
      if (!entry.used || !entry.dirty || entry.queued ||
          !IsFinalDelivery(entry.message.delivery)) {
        continue;
      }
      if (oldest == nullptr || entry.order < oldest->order) {
        oldest = &entry;
      }
    }
    if (oldest == nullptr) {
      return;
    }
    QueueEntry(oldest);
  }
}

void RfChatRepository::RebuildPendingQueue() {
  pending_head_ = 0;
  pending_count_ = 0;
  std::fill_n(pending_sequences_, kRfChatPendingCapacity, uint64_t{0});
  for (size_t index = 0; index < capacity_; ++index) {
    entries_[index].queued = false;
  }
  RefillPendingQueue();
}

uint64_t RfChatRepository::Append(RfChatMessage message) {
  ScopedLock lock(this);
  if (!lock.locked() || !InitializeCache() || message.profile_id == 0) {
    return 0;
  }
  TouchProfile(message.profile_id);
  Entry* entry = SelectInsertionEntry(message.profile_id);
  if (entry == nullptr) {
    return 0;
  }
  const uint32_t replaced_profile_id =
      entry->used ? entry->message.profile_id : 0;
  message.sequence = next_sequence_++;
  entry->message = message;
  entry->order = next_entry_order_++;
  entry->used = true;
  entry->dirty = true;
  entry->queued = false;
  QueueEntry(entry);
  if (replaced_profile_id != 0 && replaced_profile_id != message.profile_id) {
    RefreshProfileSummary(replaced_profile_id);
  }
  RefreshProfileSummary(message.profile_id);
  return message.sequence;
}

bool RfChatRepository::UpdateDelivery(
    uint64_t sequence, RfChatDeliveryState delivery) {
  ScopedLock lock(this);
  if (!lock.locked()) {
    return false;
  }
  Entry* entry = FindEntry(sequence);
  if (entry == nullptr) {
    return false;
  }
  entry->message.delivery = delivery;
  entry->dirty = true;
  QueueEntry(entry);
  return true;
}

void RfChatRepository::FailPending(uint32_t profile_id) {
  ScopedLock lock(this);
  if (!lock.locked()) {
    return;
  }
  for (size_t index = 0; index < capacity_; ++index) {
    Entry& entry = entries_[index];
    if (entry.used && entry.message.profile_id == profile_id &&
        entry.message.delivery == RfChatDeliveryState::kSending) {
      entry.message.delivery = RfChatDeliveryState::kFailed;
      entry.dirty = true;
      QueueEntry(&entry);
    }
  }
}

size_t RfChatRepository::GetRecent(uint32_t profile_id,
    const RfChatMessage** messages, size_t capacity) const {
  ScopedLock lock(this);
  if (!lock.locked() || entries_ == nullptr || messages == nullptr ||
      capacity == 0) {
    return 0;
  }
  std::array<const Entry*, kRfChatActiveProfileCapacity> matching = {};
  size_t matching_count = 0;
  for (size_t index = 0; index < capacity_; ++index) {
    if (entries_[index].used &&
        entries_[index].message.profile_id == profile_id) {
      if (matching_count < matching.size()) {
        matching[matching_count++] = &entries_[index];
      }
    }
  }
  std::sort(matching.begin(), matching.begin() + matching_count,
      [](const Entry* lhs, const Entry* rhs) {
        return lhs->order < rhs->order;
      });
  const size_t output_count = std::min(matching_count, capacity);
  const size_t first = matching_count - output_count;
  for (size_t index = 0; index < output_count; ++index) {
    messages[index] = matching[first + index] == nullptr
                          ? nullptr
                          : &matching[first + index]->message;
  }
  return output_count;
}

size_t RfChatRepository::GetCachedMessageCount() const {
  ScopedLock lock(this);
  if (!lock.locked() || entries_ == nullptr) {
    return 0;
  }
  return static_cast<size_t>(std::count_if(
      entries_, entries_ + capacity_, [](const Entry& entry) {
        return entry.used;
      }));
}

bool RfChatRepository::GetOldestPending(
    uint32_t profile_id, RfChatMessage* message) const {
  ScopedLock lock(this);
  if (!lock.locked() || profile_id == 0 || message == nullptr ||
      entries_ == nullptr) {
    return false;
  }
  const Entry* oldest = nullptr;
  for (size_t index = 0; index < capacity_; ++index) {
    const Entry& entry = entries_[index];
    if (!entry.used || entry.message.profile_id != profile_id ||
        entry.message.delivery != RfChatDeliveryState::kSending) {
      continue;
    }
    if (oldest == nullptr || entry.order < oldest->order) {
      oldest = &entry;
    }
  }
  if (oldest == nullptr) {
    return false;
  }
  *message = oldest->message;
  return true;
}

void RfChatRepository::RefreshProfileSummary(uint32_t profile_id) {
  ProfileState* state = FindProfileState(profile_id, true);
  if (state == nullptr) {
    return;
  }
  const Entry* latest = nullptr;
  for (size_t index = 0; index < capacity_; ++index) {
    const Entry& entry = entries_[index];
    if (entry.used && entry.message.profile_id == profile_id &&
        (latest == nullptr || entry.order > latest->order)) {
      latest = &entry;
    }
  }
  if (latest == nullptr) {
    return;
  }
  CopyBoundedString(state->latest_message, sizeof(state->latest_message),
      latest->message.text);
  CopyBoundedString(
      state->latest_time, sizeof(state->latest_time), latest->message.time);
}

bool RfChatRepository::GetProfileSummary(
    uint32_t profile_id, RfChatProfileSummary* summary) const {
  ScopedLock lock(this);
  if (!lock.locked() || summary == nullptr) {
    return false;
  }
  *summary = RfChatProfileSummary{};
  const ProfileState* state = FindProfileState(profile_id);
  if (state == nullptr) {
    return false;
  }
  CopyBoundedString(summary->latest_message, sizeof(summary->latest_message),
      state->latest_message);
  CopyBoundedString(
      summary->latest_time, sizeof(summary->latest_time), state->latest_time);
  summary->unread_count = state->unread_count;
  return true;
}

void RfChatRepository::TouchProfile(uint32_t profile_id) {
  ScopedLock lock(this);
  if (!lock.locked()) {
    return;
  }
  ProfileState* state = FindProfileState(profile_id, true);
  if (state != nullptr) {
    state->last_access_order = next_access_order_++;
  }
}

void RfChatRepository::IncrementUnread(uint32_t profile_id) {
  ScopedLock lock(this);
  if (!lock.locked()) {
    return;
  }
  ProfileState* state = FindProfileState(profile_id, true);
  if (state != nullptr && state->unread_count < UINT16_MAX) {
    ++state->unread_count;
  }
}

void RfChatRepository::MarkRead(uint32_t profile_id) {
  ScopedLock lock(this);
  if (!lock.locked()) {
    return;
  }
  ProfileState* state = FindProfileState(profile_id, false);
  if (state != nullptr && state->unread_count != 0) {
    state->unread_count = 0;
  }
}

void RfChatRepository::RemoveProfile(uint32_t profile_id) {
  ScopedLock lock(this);
  if (!lock.locked() || profile_id == 0) {
    return;
  }
  for (size_t index = 0; index < capacity_; ++index) {
    if (entries_[index].used &&
        entries_[index].message.profile_id == profile_id) {
      entries_[index] = Entry{};
    }
  }
  ProfileState* state = FindProfileState(profile_id, false);
  if (state != nullptr) {
    *state = ProfileState{};
  }
  const bool already_pending =
      std::find(pending_delete_profile_ids_,
          pending_delete_profile_ids_ + pending_delete_count_,
          profile_id) != pending_delete_profile_ids_ + pending_delete_count_;
  if (!already_pending && pending_delete_count_ < kRfProfileCapacity) {
    pending_delete_profile_ids_[pending_delete_count_++] = profile_id;
  }
  RebuildPendingQueue();
}

bool RfChatRepository::GetStorageDirectory(
    char* output, size_t output_size) const {
  if (output == nullptr || output_size == 0) {
    return false;
  }
  const char* base_path = LittleFsStorageBasePath();
  if (base_path == nullptr || base_path[0] == '\0') {
    output[0] = '\0';
    return false;
  }
  output[0] = '\0';
  return AppendPathComponent(output, output_size, base_path) &&
         AppendPathComponent(output, output_size, kApplicationDirectory) &&
         AppendPathComponent(output, output_size, kDataDirectory) &&
         AppendPathComponent(output, output_size, kRfDirectory) &&
         AppendPathComponent(output, output_size, kChatDirectory);
}

bool RfChatRepository::BuildLogPath(char* output, size_t output_size) const {
  char directory[160] = {};
  if (!GetStorageDirectory(directory, sizeof(directory))) {
    return false;
  }
  output[0] = '\0';
  return AppendPathComponent(output, output_size, directory) &&
         AppendPathComponent(output, output_size, kChatLogFile);
}

bool RfChatRepository::EnsureStorageDirectory() {
  if (!IsLittleFsStorageMounted()) {
    return false;
  }
  const char* base_path = LittleFsStorageBasePath();
  if (base_path == nullptr || base_path[0] == '\0') {
    return false;
  }
  char path[160] = {};
  const char* directories[] = {
      kApplicationDirectory, kDataDirectory, kRfDirectory, kChatDirectory};
  if (!AppendPathComponent(path, sizeof(path), base_path)) {
    return false;
  }
  for (const char* directory : directories) {
    if (!AppendPathComponent(path, sizeof(path), directory) ||
        !CreateDirectory(path)) {
      return false;
    }
  }
  return true;
}

bool RfChatRepository::LoadLog(
    const uint32_t* profile_ids, size_t profile_count) {
  auto buffer = std::unique_ptr<ChatLogReadBuffer>(
      new (std::nothrow) ChatLogReadBuffer{});
  if (buffer == nullptr ||
      !BuildLogPath(buffer->path, sizeof(buffer->path))) {
    return false;
  }
  FILE* file = std::fopen(buffer->path, "rb");
  if (file == nullptr) {
    if (errno == ENOENT) {
      stored_record_count_ = 0;
      log_scanned_ = true;
      return true;
    }
    return false;
  }
  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return false;
  }
  const long file_size = std::ftell(file);
  if (file_size < 0) {
    std::fclose(file);
    return false;
  }
  const size_t record_count = static_cast<size_t>(file_size) / kDiskRecordSize;
  if (std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    return false;
  }
  bool result = true;
  size_t valid_record_count = 0;
  for (size_t index = 0; index < record_count; ++index) {
    if (std::fread(buffer->record.data(), 1, buffer->record.size(), file) !=
        buffer->record.size()) {
      result = false;
      break;
    }
    if (!DecodeRecord(buffer->record, &buffer->message)) {
      continue;
    }
    ++valid_record_count;
    if (!ContainsProfileId(
            profile_ids, profile_count, buffer->message.profile_id)) {
      continue;
    }
    if (FindEntry(buffer->message.sequence) != nullptr) {
      continue;
    }
    Entry* entry = SelectInsertionEntry(buffer->message.profile_id);
    if (entry == nullptr) {
      result = false;
      break;
    }
    const uint32_t replaced_profile_id =
        entry->used ? entry->message.profile_id : 0;
    entry->message = buffer->message;
    entry->order = next_entry_order_++;
    entry->used = true;
    entry->dirty = false;
    entry->queued = false;
    if (replaced_profile_id != 0 &&
        replaced_profile_id != buffer->message.profile_id) {
      RefreshProfileSummary(replaced_profile_id);
    }
  }
  std::fclose(file);
  if (!result) {
    return false;
  }
  stored_record_count_ = valid_record_count;
  log_scanned_ = true;
  compaction_pending_ = compaction_pending_ ||
                        valid_record_count > kRfChatStorageCapacity ||
                        valid_record_count != record_count ||
                        static_cast<size_t>(file_size) % kDiskRecordSize != 0;
  for (size_t index = 0; index < profile_count; ++index) {
    RefreshProfileSummary(profile_ids[index]);
  }
  return result;
}

bool RfChatRepository::LoadProfiles(
    const uint32_t* profile_ids, size_t profile_count) {
  ScopedLock lock(this);
  if (!lock.locked() || !InitializeCache()) {
    return false;
  }
  if (profile_count > kRfProfileCapacity ||
      (profile_count > 0 && profile_ids == nullptr)) {
    return false;
  }
  if (profile_count == 0) {
    if (log_scanned_) {
      return true;
    }
    return EnsureStorageDirectory() && LoadLog(nullptr, 0);
  }
  uint32_t unloaded_profile_ids[kRfProfileCapacity] = {};
  size_t unloaded_profile_count = 0;
  for (size_t index = 0; index < profile_count; ++index) {
    ProfileState* state = FindProfileState(profile_ids[index], true);
    if (state != nullptr && !state->history_loaded) {
      unloaded_profile_ids[unloaded_profile_count++] = profile_ids[index];
    }
  }
  if (unloaded_profile_count == 0) {
    return true;
  }
  if (!EnsureStorageDirectory()) {
    return false;
  }
  if (!LoadLog(unloaded_profile_ids, unloaded_profile_count)) {
    return false;
  }
  for (size_t index = 0; index < unloaded_profile_count; ++index) {
    ProfileState* state = FindProfileState(unloaded_profile_ids[index], false);
    if (state != nullptr) {
      state->history_loaded = true;
    }
  }
  return true;
}

bool RfChatRepository::PersistEntry(Entry* entry) {
  if (entry == nullptr || !entry->used || !entry->dirty ||
      !IsFinalDelivery(entry->message.delivery)) {
    return true;
  }
  char path[224] = {};
  DiskRecord record = {};
  if (!BuildLogPath(path, sizeof(path)) ||
      !EncodeRecord(entry->message, &record)) {
    return false;
  }
  if (!log_scanned_ && !LoadLog(nullptr, 0)) {
    return false;
  }
  if (stored_record_count_ >= kRfChatStorageCapacity &&
      !CompactLog(kCompactionTarget)) {
    return false;
  }
  FILE* file = std::fopen(path, "rb+");
  if (file == nullptr && errno == ENOENT) {
    file = std::fopen(path, "wb+");
  }
  if (file == nullptr) {
    return false;
  }
  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return false;
  }
  const long file_size = std::ftell(file);
  if (file_size < 0) {
    std::fclose(file);
    return false;
  }
  const long aligned_size =
      file_size - file_size % static_cast<long>(kDiskRecordSize);
  if (aligned_size != file_size &&
      ftruncate(fileno(file), static_cast<off_t>(aligned_size)) != 0) {
    std::fclose(file);
    return false;
  }
  if (aligned_size >= static_cast<long>(kDiskRecordSize)) {
    DiskRecord last_record = {};
    RfChatMessage last_message;
    if (std::fseek(file, aligned_size - static_cast<long>(kDiskRecordSize),
            SEEK_SET) != 0 ||
        std::fread(last_record.data(), 1, last_record.size(), file) !=
            last_record.size()) {
      std::fclose(file);
      return false;
    }
    if (DecodeRecord(last_record, &last_message) &&
        last_message.sequence == entry->message.sequence &&
        last_message.profile_id == entry->message.profile_id) {
      std::fclose(file);
      entry->dirty = false;
      return true;
    }
  }
  if (std::fseek(file, aligned_size, SEEK_SET) != 0) {
    std::fclose(file);
    return false;
  }
  const bool written =
      std::fwrite(record.data(), 1, record.size(), file) == record.size();
  const bool flushed = written && std::fflush(file) == 0;
  const bool closed = std::fclose(file) == 0;
  if (!flushed || !closed) {
    return false;
  }
  entry->dirty = false;
  ++stored_record_count_;
  return true;
}

bool RfChatRepository::CompactLog(size_t keep_records) {
  char path[224] = {};
  char temporary_path[224] = {};
  if (!BuildLogPath(path, sizeof(path))) {
    return false;
  }
  char directory[160] = {};
  if (!GetStorageDirectory(directory, sizeof(directory))) {
    return false;
  }
  if (!AppendPathComponent(
          temporary_path, sizeof(temporary_path), directory) ||
      !AppendPathComponent(
          temporary_path, sizeof(temporary_path), kChatTempFile)) {
    return false;
  }

  FILE* source = std::fopen(path, "rb");
  if (source == nullptr) {
    if (errno != ENOENT) {
      return false;
    }
    std::remove(temporary_path);
    stored_record_count_ = 0;
    log_scanned_ = true;
    pending_delete_count_ = 0;
    compaction_pending_ = false;
    return true;
  }

  size_t valid_record_count = 0;
  DiskRecord record = {};
  while (std::fread(record.data(), 1, record.size(), source) == record.size()) {
    RfChatMessage message;
    if (DecodeRecord(record, &message) &&
        !ContainsProfileId(pending_delete_profile_ids_, pending_delete_count_,
            message.profile_id)) {
      ++valid_record_count;
    }
  }
  if (std::ferror(source) != 0 || std::fseek(source, 0, SEEK_SET) != 0) {
    std::fclose(source);
    return false;
  }

  FILE* destination = std::fopen(temporary_path, "wb");
  if (destination == nullptr) {
    std::fclose(source);
    return false;
  }
  size_t records_to_skip =
      valid_record_count > keep_records ? valid_record_count - keep_records : 0;
  size_t written_record_count = 0;
  bool result = true;
  while (std::fread(record.data(), 1, record.size(), source) == record.size()) {
    RfChatMessage message;
    if (!DecodeRecord(record, &message) ||
        ContainsProfileId(pending_delete_profile_ids_, pending_delete_count_,
            message.profile_id)) {
      continue;
    }
    if (records_to_skip > 0) {
      --records_to_skip;
      continue;
    }
    if (std::fwrite(record.data(), 1, record.size(), destination) !=
        record.size()) {
      result = false;
      break;
    }
    ++written_record_count;
  }
  result = result && std::ferror(source) == 0 && std::fflush(destination) == 0;
  const bool source_closed = std::fclose(source) == 0;
  const bool destination_closed = std::fclose(destination) == 0;
  result = result && source_closed && destination_closed;
  if (!result || std::rename(temporary_path, path) != 0) {
    std::remove(temporary_path);
    return false;
  }

  stored_record_count_ = written_record_count;
  log_scanned_ = true;
  pending_delete_count_ = 0;
  compaction_pending_ = false;
  return true;
}

bool RfChatRepository::DeletePendingProfiles() {
  if (pending_delete_count_ == 0) {
    return true;
  }
  return CompactLog(kRfChatStorageCapacity);
}

bool RfChatRepository::FlushPending(size_t maximum_records) {
  ScopedLock lock(this);
  if (!lock.locked()) {
    return false;
  }
  RefillPendingQueue();
  if (pending_count_ == 0 && pending_delete_count_ == 0 &&
      !compaction_pending_) {
    return true;
  }
  if (!EnsureStorageDirectory()) {
    return false;
  }
  if (!DeletePendingProfiles()) {
    return false;
  }
  if (compaction_pending_ &&
      !CompactLog(stored_record_count_ > kRfChatStorageCapacity
                      ? kCompactionTarget
                      : kRfChatStorageCapacity)) {
    return false;
  }
  size_t written = 0;
  while (written < maximum_records) {
    if (pending_count_ == 0) {
      RefillPendingQueue();
      if (pending_count_ == 0) {
        break;
      }
    }
    const uint64_t sequence = pending_sequences_[pending_head_];
    Entry* entry = FindEntry(sequence);
    if (entry != nullptr && !PersistEntry(entry)) {
      return false;
    }
    if (entry != nullptr) {
      entry->queued = false;
    }
    pending_sequences_[pending_head_] = 0;
    pending_head_ = (pending_head_ + 1) % kRfChatPendingCapacity;
    --pending_count_;
    ++written;
  }
  RefillPendingQueue();
  return true;
}

bool RfChatRepository::HasPendingWrites() const {
  ScopedLock lock(this);
  if (!lock.locked()) {
    return true;
  }
  if (pending_delete_count_ != 0 || compaction_pending_) {
    return true;
  }
  for (size_t index = 0; index < capacity_; ++index) {
    if (entries_[index].used && entries_[index].dirty &&
        IsFinalDelivery(entries_[index].message.delivery)) {
      return true;
    }
  }
  return false;
}

RfChatRepository& GetRfChatRepository() {
  static RfChatRepository repository;
  return repository;
}

}  // namespace lilygo_box::app

/*
 * @Description: RF 聊天热缓存、会话摘要与 LittleFS 日志仓库
 * @Author: LILYGO_L
 * @Date: 2026-07-17 00:00:00
 * @LastEditTime: 2026-07-17 17:22:18
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "app/storage/rf_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace lilygo_box::app {

// 单条聊天文本缓冲区容量，包含字符串结尾字符。
inline constexpr size_t kRfChatTextCapacity = 511;
// 消息时间文本缓冲区容量，包含字符串结尾字符。
inline constexpr size_t kRfChatTimeCapacity = 16;
// RF 主页面最新消息摘要缓冲区容量。
inline constexpr size_t kRfChatSummaryCapacity = 96;
// 单个 RF 配置在热缓存中最多保留的消息数量。
inline constexpr size_t kRfChatActiveProfileCapacity = 64;
// 聊天页面单次渲染的最近消息数量。
inline constexpr size_t kRfChatPageCapacity = 32;
// PSRAM 热缓存最多保留的消息总数。
inline constexpr size_t kRfChatGlobalCapacity = 256;
// LittleFS 聊天日志最多保留的消息总数。
inline constexpr size_t kRfChatStorageCapacity = 1024;
// 等待写入 LittleFS 的消息队列容量。
inline constexpr size_t kRfChatPendingCapacity = 16;

enum class RfChatDeliveryState : uint8_t {
  // 从对端接收的消息。
  kReceived,
  // 正在等待射频发送结果的消息。
  kSending,
  // 已成功发送的消息。
  kSent,
  // 发送失败的消息。
  kFailed,
};

enum class RfChatMessageType : uint8_t {
  // 用户发送或接收的普通消息。
  kUser,
  // RF 配置变更等系统提示。
  kSystem,
};

struct RfChatMessage {
  // 跨缓存和磁盘记录使用的消息唯一序号。
  uint64_t sequence = 0;
  // 消息所属 RF 配置的稳定 ID。
  uint32_t profile_id = 0;
  // 聊天页面显示的消息正文。
  char text[kRfChatTextCapacity] = {};
  // 消息产生时的本地时间文本。
  char time[kRfChatTimeCapacity] = {};
  // 消息内容类型。
  RfChatMessageType type = RfChatMessageType::kUser;
  // 消息接收或发送状态。
  RfChatDeliveryState delivery = RfChatDeliveryState::kReceived;
  // 接收信号强度，发送消息和系统消息保持为 0。
  int8_t rssi_dbm = 0;
  // 接收信噪比，发送消息和系统消息保持为 0。
  int8_t snr_db = 0;
};

struct RfChatProfileSummary {
  // 当前 RF 配置的最新消息摘要。
  char latest_message[kRfChatSummaryCapacity] = {};
  // 最新消息的本地时间文本。
  char latest_time[kRfChatTimeCapacity] = {};
  // 当前 RF 配置尚未查看的消息数量。
  uint16_t unread_count = 0;
};

class RfChatRepository final {
 public:
  RfChatRepository() = default;
  ~RfChatRepository();

  RfChatRepository(const RfChatRepository&) = delete;
  RfChatRepository& operator=(const RfChatRepository&) = delete;

  /**
   * @brief 初始化 RF 聊天热缓存
   * @return 热缓存可用时返回 true
   */
  bool Initialize();

  /**
   * @brief 补载指定 RF 配置最近的 LittleFS 聊天记录
   * @param profile_ids RF 配置 ID 数组
   * @param profile_count RF 配置数量
   * @return 所有可读取记录均处理完成时返回 true
   */
  bool LoadProfiles(const uint32_t* profile_ids, size_t profile_count);

  /**
   * @brief 追加消息并按单配置和全局容量执行淘汰
   * @param message 待追加消息
   * @return 分配给消息的稳定序号，失败返回 0
   */
  uint64_t Append(RfChatMessage message);

  /**
   * @brief 更新指定消息的发送状态
   * @param sequence 消息序号
   * @param delivery 新发送状态
   * @return 找到并更新消息时返回 true
   */
  bool UpdateDelivery(uint64_t sequence, RfChatDeliveryState delivery);

  /**
   * @brief 将指定配置的全部等待发送消息标记为失败
   * @param profile_id RF 配置 ID
   */
  void FailPending(uint32_t profile_id);

  /**
   * @brief 获取指定配置最近消息的只读视图
   * @param profile_id RF 配置 ID
   * @param messages 消息指针输出数组
   * @param capacity 输出数组容量
   * @return 写入输出数组的消息数量
   */
  size_t GetRecent(uint32_t profile_id, const RfChatMessage** messages,
      size_t capacity) const;

  /**
   * @brief 获取当前 RAM 热缓存中的聊天消息总数
   * @return 当前缓存中有效消息的数量
   */
  size_t GetCachedMessageCount() const;

  /**
   * @brief 获取指定配置最早一条等待射频结果的消息
   * @param profile_id RF 配置 ID
   * @param message 消息副本输出
   * @return 存在等待消息时返回 true
   */
  bool GetOldestPending(uint32_t profile_id, RfChatMessage* message) const;

  /**
   * @brief 获取指定配置的最新消息摘要与未读数量
   * @param profile_id RF 配置 ID
   * @param summary 摘要输出
   * @return 配置存在聊天状态时返回 true
   */
  bool GetProfileSummary(
      uint32_t profile_id, RfChatProfileSummary* summary) const;

  /**
   * @brief 将指定配置标记为最近访问并提高其热缓存保留优先级
   * @param profile_id RF 配置 ID
   */
  void TouchProfile(uint32_t profile_id);

  /**
   * @brief 增加指定配置的未读数量
   * @param profile_id RF 配置 ID
   */
  void IncrementUnread(uint32_t profile_id);

  /**
   * @brief 清空指定配置的未读数量
   * @param profile_id RF 配置 ID
   */
  void MarkRead(uint32_t profile_id);

  /**
   * @brief 删除指定 RF 配置的 RAM 与 LittleFS 聊天记录
   * @param profile_id RF 配置 ID
   */
  void RemoveProfile(uint32_t profile_id);

  /**
   * @brief 将待保存消息追加到 LittleFS 日志
   * @param maximum_records 本次最多写入的消息数
   * @return 当前没有待写消息或写入成功时返回 true
   */
  bool FlushPending(size_t maximum_records);

  /**
   * @brief 查询是否存在等待写入或整理的聊天记录
   * @return 存在未持久化数据时返回 true
   */
  bool HasPendingWrites() const;

  /**
   * @brief 生成当前 RF 聊天数据目录
   * @param output 路径输出缓冲区
   * @param output_size 输出缓冲区大小
   * @return 路径有效且未截断时返回 true
   */
  bool GetStorageDirectory(char* output, size_t output_size) const;

 private:
  struct Entry;
  struct ProfileState;
  class ScopedLock;

  /**
   * @brief 取得 RF 聊天仓库递归互斥锁
   * @return 成功取得互斥锁时返回 true
   */
  bool Lock() const;

  /**
   * @brief 释放 RF 聊天仓库递归互斥锁
   */
  void Unlock() const;

  /**
   * @brief 初始化 PSRAM 消息缓存和内部配置状态
   * @return 所需缓存均分配成功时返回 true
   */
  bool InitializeCache();

  /**
   * @brief 创建 RF 聊天数据目录
   * @return LittleFS 和完整目录均可用时返回 true
   */
  bool EnsureStorageDirectory();

  /**
   * @brief 从 LittleFS 日志补载指定 RF 配置的最近聊天记录
   * @param profile_ids RF 配置 ID 数组
   * @param profile_count RF 配置数量
   * @return 文件不存在或可读记录均处理完成时返回 true
   */
  bool LoadLog(const uint32_t* profile_ids, size_t profile_count);

  /**
   * @brief 将一条已完成发送状态的缓存消息写入 LittleFS
   * @param entry 待持久化的缓存条目
   * @return 无需写入或完整写入成功时返回 true
   */
  bool PersistEntry(Entry* entry);

  /**
   * @brief 从聊天日志移除等待清理的 RF 配置记录
   * @return 所有等待记录均删除完成时返回 true
   */
  bool DeletePendingProfiles();

  /**
   * @brief 按唯一序号查找缓存消息
   * @param sequence 消息唯一序号
   * @return 找到时返回缓存条目，否则返回 nullptr
   */
  Entry* FindEntry(uint64_t sequence);

  /**
   * @brief 按单配置容量和最近访问顺序选择写入位置
   * @param profile_id 待写消息所属 RF 配置 ID
   * @return 可写缓存条目，缓存不可用时返回 nullptr
   */
  Entry* SelectInsertionEntry(uint32_t profile_id);

  /**
   * @brief 查找或创建指定 RF 配置的运行时状态
   * @param profile_id RF 配置 ID
   * @param create 未找到时是否创建状态
   * @return 找到或创建的状态，否则返回 nullptr
   */
  ProfileState* FindProfileState(uint32_t profile_id, bool create);

  /**
   * @brief 查找指定 RF 配置的只读运行时状态
   * @param profile_id RF 配置 ID
   * @return 找到时返回状态，否则返回 nullptr
   */
  const ProfileState* FindProfileState(uint32_t profile_id) const;

  /**
   * @brief 将可持久化消息加入等待写入队列
   * @param entry 待排队的缓存条目
   */
  void QueueEntry(Entry* entry);

  /**
   * @brief 使用尚未排队的脏消息补满等待写入队列
   */
  void RefillPendingQueue();

  /**
   * @brief 根据当前缓存中的脏消息重建等待写入队列
   */
  void RebuildPendingQueue();

  /**
   * @brief 使用缓存中的最新消息刷新 RF 配置摘要
   * @param profile_id RF 配置 ID
   */
  void RefreshProfileSummary(uint32_t profile_id);

  /**
   * @brief 生成 RF 聊天日志文件路径
   * @param output 路径输出缓冲区
   * @param output_size 输出缓冲区大小
   * @return 路径有效且未截断时返回 true
   */
  bool BuildLogPath(char* output, size_t output_size) const;

  /**
   * @brief 重写聊天日志并按配置删除和容量上限执行压缩
   * @param keep_records 最多保留的最新有效记录数
   * @return 日志压缩或无需压缩时返回 true
   */
  bool CompactLog(size_t keep_records);

  // 固定容量的消息热缓存，优先分配在 PSRAM。
  Entry* entries_ = nullptr;
  // RF 配置的摘要、未读数和最近访问状态。
  ProfileState* profile_states_ = nullptr;
  // entries_ 的实际容量，PSRAM 失败时使用回退容量。
  size_t capacity_ = 0;
  // 搜索空闲缓存条目时使用的循环起点。
  size_t insertion_cursor_ = 0;
  // 当前运行会话中分配下一条消息唯一序号的计数器。
  uint64_t next_sequence_ = 1;
  // 维护消息显示先后关系的运行时顺序计数器。
  uint64_t next_entry_order_ = 1;
  // 维护 RF 配置最近访问顺序的计数器。
  uint64_t next_access_order_ = 1;
  // 等待写入 LittleFS 的消息序号环形队列。
  uint64_t pending_sequences_[kRfChatPendingCapacity] = {};
  // 环形队列首条消息的位置。
  size_t pending_head_ = 0;
  // 环形队列当前有效消息数量。
  size_t pending_count_ = 0;
  // 等待从聊天日志删除的 RF 配置 ID。
  uint32_t pending_delete_profile_ids_[kRfProfileCapacity] = {};
  // 等待删除的 RF 配置数量。
  size_t pending_delete_count_ = 0;
  // 当前聊天日志中已经确认的有效记录数量。
  size_t stored_record_count_ = 0;
  // 当前运行周期是否已经扫描聊天日志并统计记录数量。
  bool log_scanned_ = false;
  // 日志是否需要在下次安全刷新时执行容量整理。
  bool compaction_pending_ = false;
  // 仓库递归互斥锁使用的静态控制块。
  mutable StaticSemaphore_t mutex_buffer_ = {};
  // 保护 RAM 缓存和 LittleFS 刷新状态的递归互斥锁。
  mutable SemaphoreHandle_t mutex_ = nullptr;
};

/**
 * @brief 获取应用进程内共享的 RF 聊天仓库
 * @return RF 聊天仓库引用
 */
RfChatRepository& GetRfChatRepository();

}  // namespace lilygo_box::app

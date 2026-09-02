/*
 * @Description: NVS 缓存与持久化内部协调接口
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-18 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "nvs.h"

namespace lilygo_box::app {

inline constexpr char kApplicationNvsPartitionName[] = "app_nvs";

enum class StorageDomain : uint8_t {
  kDisplay,
  kFirstBoot,
  kPowerState,
  kHaptic,
  kMusicSources,
  kRadioProfiles,
  kSound,
  kWifiPreferences,
  kWifiSavedNetworks,
  kOtg,
  kInputMethod,
  kKeyboardExpansion,
  kBattery,
  kCount,
};

/**
 * @brief 确保存储缓存锁和 I/O 锁已经初始化
 * @return 存储协调器可用时返回 true
 */
bool EnsureStorageCoordinatorInitialized();

/**
 * @brief 确保 LilyGoBox 独立应用 NVS 分区已经初始化
 * @return app_nvs 分区可用时返回 true
 */
bool EnsureApplicationNvsInitialized();

enum class StorageStageResult : uint8_t {
  kClean,
  kStaged,
  kFailed,
};

/**
 * @brief 统一保护全部 NVS RAM 缓存与存储冻结状态的作用域锁
 */
class StorageCacheLock final {
 public:
  StorageCacheLock();
  ~StorageCacheLock();

  StorageCacheLock(const StorageCacheLock&) = delete;
  StorageCacheLock& operator=(const StorageCacheLock&) = delete;

  /**
   * @brief 判断缓存锁是否成功取得
   * @return 已取得返回 true
   */
  bool IsLocked() const { return locked_; }

 private:
  // 当前作用域是否已成功持有统一缓存互斥锁。
  bool locked_ = false;
};

/**
 * @brief 更新指定存储域的脏状态，调用方必须持有 StorageCacheLock
 * @param domain 存储域
 * @param dirty 是否存在待落盘修改
 */
void SetStorageDomainDirtyLocked(StorageDomain domain, bool dirty);

/**
 * @brief 查询指定存储域的脏状态，调用方必须持有 StorageCacheLock
 * @param domain 存储域
 * @return 存在待落盘修改返回 true
 */
bool IsStorageDomainDirtyLocked(StorageDomain domain);

/**
 * @brief 查询终止流程是否已冻结缓存更新，调用方必须持有缓存锁
 * @return 已冻结返回 true
 */
bool AreStorageUpdatesFrozenLocked();

/**
 * @brief 从 LilyGoBox 独立应用 NVS 分区打开命名空间
 * @param namespace_name NVS 命名空间名称
 * @param open_mode 只读或读写打开模式
 * @param handle 成功时接收 NVS 句柄
 * @return ESP-IDF NVS 打开结果
 */
esp_err_t OpenApplicationNvs(const char* namespace_name,
    nvs_open_mode_t open_mode, nvs_handle_t* handle);

/**
 * @brief 尝试一次立即提交全部待写入的 NVS 配置域
 * @return 没有待写数据或全部提交成功返回 true，否则返回 false
 *
 * 调用方不得持有 StorageCacheLock。提交失败时脏数据仍保留在 RAM，
 * 后续设置操作或关机前最终落盘会再次尝试提交。
 */
bool FlushPendingNvsStorage();

/**
 * @brief 请求后台任务尽快写入待处理的 LittleFS 数据
 * @param urgent 队列接近容量时是否跳过合并等待
 */
void RequestLittleFsStorageFlush(bool urgent);

/**
 * @brief 保存当前值、已落盘值和事务快照的 NVS 配置缓存
 *
 * UpdateAndPersist() 先比较 RAM 当前值与新值，仅在存在变化或需要重试时
 * 发起 NVS 事务；BeginFlush() 固定本次实际写入的快照；FinishFlush()
 * 只在提交成功后推进已落盘快照。写入失败会保留强制重试状态。
 */
template <typename T>
class NvsStorageCache final {
 public:
  using EqualFunction = bool (*)(const T&, const T&);

  NvsStorageCache(StorageDomain domain, EqualFunction equal)
      : domain_(domain), equal_(equal) {}

  NvsStorageCache(const NvsStorageCache&) = delete;
  NvsStorageCache& operator=(const NvsStorageCache&) = delete;

  bool Initialize(const T& value) {
    StorageCacheLock lock;
    if (!lock.IsLocked() || equal_ == nullptr) {
      return false;
    }
    current_ = value;
    persisted_ = value;
    staged_ = value;
    initialized_ = true;
    retry_required_ = false;
    flush_pending_ = false;
    SetStorageDomainDirtyLocked(domain_, false);
    return true;
  }

  bool Read(T* value) const {
    if (value == nullptr) {
      return false;
    }
    StorageCacheLock lock;
    if (!lock.IsLocked() || !initialized_) {
      return false;
    }
    *value = current_;
    return true;
  }

  /**
   * @brief 接收新配置，并在需要时立即提交全部 NVS 脏缓存
   * @param value 新配置快照
   * @return 无变化或 NVS 提交成功返回 true，否则返回 false
   */
  bool UpdateAndPersist(const T& value) {
    bool flush_required = false;
    {
      StorageCacheLock lock;
      if (!lock.IsLocked() || !initialized_ || equal_ == nullptr ||
          AreStorageUpdatesFrozenLocked()) {
        return false;
      }
      if (!equal_(current_, value)) {
        current_ = value;
      }
      UpdateDirtyStateLocked();
      flush_required = IsStorageDomainDirtyLocked(domain_);
    }
    return !flush_required || FlushPendingNvsStorage();
  }

  bool BeginFlush(const T** value) {
    if (value == nullptr) {
      return false;
    }
    StorageCacheLock lock;
    if (!lock.IsLocked() || !initialized_ || flush_pending_ ||
        !IsStorageDomainDirtyLocked(domain_)) {
      return false;
    }
    staged_ = current_;
    flush_pending_ = true;
    *value = &staged_;
    return true;
  }

  void FinishFlush(bool committed) {
    StorageCacheLock lock;
    if (!lock.IsLocked() || !initialized_ || !flush_pending_) {
      return;
    }
    if (committed) {
      persisted_ = staged_;
      retry_required_ = false;
    } else {
      retry_required_ = true;
    }
    flush_pending_ = false;
    UpdateDirtyStateLocked();
  }

 private:
  void UpdateDirtyStateLocked() {
    const bool dirty = flush_pending_ || retry_required_ ||
        !equal_(current_, persisted_);
    SetStorageDomainDirtyLocked(domain_, dirty);
  }

  // 当前缓存所属的独立 NVS 配置域。
  StorageDomain domain_;
  // 判断两个配置快照在业务语义上是否相等。
  EqualFunction equal_ = nullptr;
  // 运行期读取和更新的最新 RAM 值。
  T current_ = {};
  // 最后一次确认已经成功提交到 NVS 的值。
  T persisted_ = {};
  // 当前 NVS 事务实际写入的不可变快照。
  T staged_ = {};
  // 缓存是否已经从启动默认值或 NVS 完成初始化。
  bool initialized_ = false;
  // 上次写入失败后是否必须重试，避免误清除脏状态。
  bool retry_required_ = false;
  // 当前配置域是否已经加入正在执行的 NVS 事务。
  bool flush_pending_ = false;
};

/**
 * @brief 将显示偏好脏快照暂存到当前 NVS 事务
 * @param handle 已打开的共享 NVS 句柄
 * @return 无修改、暂存成功或暂存失败
 */
StorageStageResult StageDisplayStorage(nvs_handle_t handle);

/**
 * @brief 根据 NVS 事务提交结果结束显示偏好快照
 * @param committed 事务是否提交成功
 */
void FinishDisplayStorage(bool committed);

/**
 * @brief 将首次开机状态脏快照暂存到当前 NVS 事务
 * @param handle 已打开的共享 NVS 句柄
 * @return 无修改、暂存成功或暂存失败
 */
StorageStageResult StageFirstBootStorage(nvs_handle_t handle);

/**
 * @brief 根据 NVS 事务提交结果结束首次开机状态快照
 * @param committed 事务是否提交成功
 */
void FinishFirstBootStorage(bool committed);

/**
 * @brief 将系统电源状态脏快照暂存到当前 NVS 事务
 * @param handle 已打开的共享 NVS 句柄
 * @return 无修改、暂存成功或暂存失败
 */
StorageStageResult StagePowerStateStorage(nvs_handle_t handle);

/**
 * @brief 根据统一事务结果结束系统电源状态快照
 * @param committed 事务是否提交成功
 */
void FinishPowerStateStorage(bool committed);

/**
 * @brief 将 OTG 偏好脏快照暂存到当前 NVS 事务
 * @param handle 已打开的共享 NVS 句柄
 * @return 无修改、暂存成功或暂存失败
 */
StorageStageResult StageOtgStorage(nvs_handle_t handle);

/**
 * @brief 根据 NVS 事务提交结果结束 OTG 偏好快照
 * @param committed 事务是否提交成功
 */
void FinishOtgStorage(bool committed);

/**
 * @brief 将输入法偏好脏快照暂存到当前 NVS 事务
 * @param handle 已打开的共享 NVS 句柄
 * @return 无修改、暂存成功或暂存失败
 */
StorageStageResult StageInputMethodStorage(nvs_handle_t handle);

/**
 * @brief 根据 NVS 事务提交结果结束输入法偏好快照
 * @param committed 事务是否提交成功
 */
void FinishInputMethodStorage(bool committed);

/**
 * @brief 将键盘扩展偏好脏快照暂存到当前 NVS 事务
 * @param handle 已打开的共享 NVS 句柄
 * @return 无修改、暂存成功或暂存失败
 */
StorageStageResult StageKeyboardExpansionStorage(nvs_handle_t handle);

/**
 * @brief 根据 NVS 事务提交结果结束键盘扩展偏好快照
 * @param committed 事务是否提交成功
 */
void FinishKeyboardExpansionStorage(bool committed);

/**
 * @brief 将电池容量偏好脏快照暂存到当前 NVS 事务
 * @param handle 已打开的共享 NVS 句柄
 * @return 无修改、暂存成功或暂存失败
 */
StorageStageResult StageBatteryStorage(nvs_handle_t handle);

/**
 * @brief 根据 NVS 事务提交结果结束电池容量偏好快照
 * @param committed 事务是否提交成功
 */
void FinishBatteryStorage(bool committed);

/**
 * @brief 将振动偏好脏快照暂存到当前 NVS 事务
 * @param handle 已打开的共享 NVS 句柄
 * @return 无修改、暂存成功或暂存失败
 */
StorageStageResult StageHapticStorage(nvs_handle_t handle);

/**
 * @brief 根据 NVS 事务提交结果结束振动偏好快照
 * @param committed 事务是否提交成功
 */
void FinishHapticStorage(bool committed);

/**
 * @brief 将音乐源偏好脏快照暂存到当前 NVS 事务
 * @param handle 已打开的共享 NVS 句柄
 * @return 无修改、暂存成功或暂存失败
 */
StorageStageResult StageMusicStorage(nvs_handle_t handle);

/**
 * @brief 根据 NVS 事务提交结果结束音乐源偏好快照
 * @param committed 事务是否提交成功
 */
void FinishMusicStorage(bool committed);

/**
 * @brief 将 Radio 配置脏快照暂存到当前 NVS 事务
 * @param handle 已打开的共享 NVS 句柄
 * @return 无修改、暂存成功或暂存失败
 */
StorageStageResult StageRadioStorage(nvs_handle_t handle);

/**
 * @brief 根据 NVS 事务提交结果结束 Radio 配置快照
 * @param committed 事务是否提交成功
 */
void FinishRadioStorage(bool committed);

/**
 * @brief 将声音偏好脏快照暂存到当前 NVS 事务
 * @param handle 已打开的共享 NVS 句柄
 * @return 无修改、暂存成功或暂存失败
 */
StorageStageResult StageSoundStorage(nvs_handle_t handle);

/**
 * @brief 根据 NVS 事务提交结果结束声音偏好快照
 * @param committed 事务是否提交成功
 */
void FinishSoundStorage(bool committed);

/**
 * @brief 将 WLAN 偏好脏快照暂存到当前 NVS 事务
 * @param handle 已打开的共享 NVS 句柄
 * @return 无修改、暂存成功或暂存失败
 */
StorageStageResult StageWifiPreferencesStorage(nvs_handle_t handle);

/**
 * @brief 根据 NVS 事务提交结果结束 WLAN 偏好快照
 * @param committed 事务是否提交成功
 */
void FinishWifiPreferencesStorage(bool committed);

/**
 * @brief 将已保存 WLAN 凭据脏快照暂存到当前 NVS 事务
 * @param handle 已打开的共享 NVS 句柄
 * @return 无修改、暂存成功或暂存失败
 */
StorageStageResult StageWifiSavedNetworksStorage(nvs_handle_t handle);

/**
 * @brief 根据 NVS 事务提交结果结束已保存 WLAN 凭据快照
 * @param committed 事务是否提交成功
 */
void FinishWifiSavedNetworksStorage(bool committed);

}  // namespace lilygo_box::app

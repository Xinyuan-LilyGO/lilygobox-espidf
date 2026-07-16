/*
 * @Description: 延迟 NVS 持久化内部协调接口
 * @Author: LILYGO_L
 * @Date: 2026-07-16 00:00:00
 * @LastEditTime: 2026-07-16 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstdint>

#include "nvs.h"

namespace lilygo_box::app {

enum class StorageDomain : uint8_t {
  kDisplay,
  kFirstBoot,
  kHaptic,
  kMusicSources,
  kRfProfiles,
  kSound,
  kWifiPreferences,
  kWifiSavedNetworks,
  kCount,
};

enum class StorageStageResult : uint8_t {
  kClean,
  kStaged,
  kFailed,
};

/**
 * @brief 统一保护全部长期 RAM 偏好缓存的作用域锁
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
 * @brief 保存当前值、已落盘值和写入快照的长期 RAM 缓存
 *
 * Update() 只修改 RAM；BeginFlush() 固定本次实际写入的快照；
 * FinishFlush() 只在提交成功后推进已落盘快照。写入失败会保留强制
 * 重试状态，即使当前值恰好又等于上一次已落盘值也不会误清除。
 */
template <typename T>
class DeferredStorageCache final {
 public:
  using EqualFunction = bool (*)(const T&, const T&);

  DeferredStorageCache(StorageDomain domain, EqualFunction equal)
      : domain_(domain), equal_(equal) {}

  DeferredStorageCache(const DeferredStorageCache&) = delete;
  DeferredStorageCache& operator=(const DeferredStorageCache&) = delete;

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

  bool Update(const T& value) {
    StorageCacheLock lock;
    if (!lock.IsLocked() || !initialized_ || equal_ == nullptr ||
        AreStorageUpdatesFrozenLocked()) {
      return false;
    }
    if (!equal_(current_, value)) {
      current_ = value;
    }
    UpdateDirtyStateLocked();
    return true;
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

StorageStageResult StageDisplayStorage(nvs_handle_t handle);
void FinishDisplayStorage(bool committed);
StorageStageResult StageFirstBootStorage(nvs_handle_t handle);
void FinishFirstBootStorage(bool committed);
StorageStageResult StageHapticStorage(nvs_handle_t handle);
void FinishHapticStorage(bool committed);
StorageStageResult StageMusicStorage(nvs_handle_t handle);
void FinishMusicStorage(bool committed);
StorageStageResult StageRfStorage(nvs_handle_t handle);
void FinishRfStorage(bool committed);
StorageStageResult StageSoundStorage(nvs_handle_t handle);
void FinishSoundStorage(bool committed);
StorageStageResult StageWifiPreferencesStorage(nvs_handle_t handle);
void FinishWifiPreferencesStorage(bool committed);
StorageStageResult StageWifiSavedNetworksStorage(nvs_handle_t handle);
void FinishWifiSavedNetworksStorage(bool committed);

}  // namespace lilygo_box::app

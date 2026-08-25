/*
 * @Description: UI 分层返回目标管理实现
 * @Author: LILYGO_L
 * @Date: 2026-08-24 00:00:00
 * @LastEditTime: 2026-08-24 00:00:00
 * @License: GPL 3.0
 */
#include "ui/input/back_navigation_controller.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace lilygo_box::ui {
namespace {

constexpr size_t kMaximumBackNavigationHandlerCount = 48;

struct BackNavigationEntry {
  lv_obj_t* owner = nullptr;
  ConditionalBackNavigationCallback callback;
  uint64_t sequence = 0;
};

std::array<BackNavigationEntry, kMaximumBackNavigationHandlerCount>
    g_back_navigation_entries;
uint64_t g_next_sequence = 1;

/**
 * @brief 清空一个返回处理器条目
 * @param entry 待清空的条目
 */
void ClearEntry(BackNavigationEntry* entry) {
  if (entry == nullptr) {
    return;
  }
  entry->owner = nullptr;
  entry->callback = {};
  entry->sequence = 0;
}

/**
 * @brief 注销指定页面对象关联的所有返回处理器
 * @param owner 返回目标所属的页面对象
 */
void UnregisterBackNavigationOwner(lv_obj_t* owner) {
  for (BackNavigationEntry& entry : g_back_navigation_entries) {
    if (entry.owner == owner) {
      ClearEntry(&entry);
    }
  }
}

/**
 * @brief 在页面对象删除时注销其返回处理器
 * @param event LVGL 删除事件
 */
void BackNavigationOwnerDeletedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_DELETE) {
    UnregisterBackNavigationOwner(lv_event_get_target_obj(event));
  }
}

}  // namespace

/**
 * @brief 注册与页面对象生命周期绑定的返回处理器
 * @param owner 返回目标所属的页面对象
 * @param callback 返回操作
 * @return 注册成功返回 true，否则返回 false
 */
bool RegisterBackNavigationHandler(
    lv_obj_t* owner, BackNavigationCallback callback) {
  if (!callback) {
    return false;
  }
  return RegisterConditionalBackNavigationHandler(owner,
      [callback = std::move(callback)]() {
        callback();
        return true;
      });
}

/**
 * @brief 注册可决定是否处理本次返回请求的页面处理器
 * @param owner 返回目标所属的页面对象
 * @param callback 返回操作；处理成功时返回 true
 * @return 注册成功返回 true，否则返回 false
 */
bool RegisterConditionalBackNavigationHandler(
    lv_obj_t* owner, ConditionalBackNavigationCallback callback) {
  if (owner == nullptr || !callback || !lv_obj_is_valid(owner)) {
    return false;
  }

  BackNavigationEntry* available_entry = nullptr;
  for (BackNavigationEntry& entry : g_back_navigation_entries) {
    if (entry.owner == owner) {
      available_entry = &entry;
      break;
    }
    if (available_entry == nullptr && entry.owner == nullptr) {
      available_entry = &entry;
    }
  }
  if (available_entry == nullptr) {
    return false;
  }

  const bool newly_registered = available_entry->owner == nullptr;
  available_entry->owner = owner;
  available_entry->callback = std::move(callback);
  available_entry->sequence = g_next_sequence++;
  if (newly_registered) {
    lv_obj_add_event_cb(owner, BackNavigationOwnerDeletedEventCallback,
        LV_EVENT_DELETE, nullptr);
  }
  return true;
}

/**
 * @brief 执行当前最上层可见页面的返回操作
 * @return 找到并执行返回目标时返回 true，否则返回 false
 */
bool RequestBackNavigation() {
  uint64_t sequence_limit = std::numeric_limits<uint64_t>::max();
  while (sequence_limit > 0) {
    BackNavigationEntry* selected_entry = nullptr;
    for (BackNavigationEntry& entry : g_back_navigation_entries) {
      if (entry.owner != nullptr && !lv_obj_is_valid(entry.owner)) {
        ClearEntry(&entry);
        continue;
      }
      if (entry.owner == nullptr || !entry.callback ||
          !lv_obj_is_visible(entry.owner) ||
          entry.sequence >= sequence_limit) {
        continue;
      }
      if (selected_entry == nullptr ||
          entry.sequence > selected_entry->sequence) {
        selected_entry = &entry;
      }
    }
    if (selected_entry == nullptr) {
      return false;
    }

    sequence_limit = selected_entry->sequence;
    ConditionalBackNavigationCallback callback = selected_entry->callback;
    if (callback()) {
      return true;
    }
  }
  return false;
}

}  // namespace lilygo_box::ui

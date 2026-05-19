/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-19 13:50:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

namespace lilygo_box::app {

constexpr size_t kMaxCitTestEntryCount = 16;

enum class CitTestStatus {
  kReady,
  kFailed,
  kPending,
};

struct CitTestEntry {
  const char* id = nullptr;
  const char* name = nullptr;
  CitTestStatus status = CitTestStatus::kPending;
};

struct CitTestCatalog {
  const CitTestEntry* entries = nullptr;
  size_t entry_count = 0;
};

/**
 * @brief 获取 CIT 页面显示的硬件自检入口目录
 * @return CIT 自检入口目录引用
 */
const CitTestCatalog& GetCitTestCatalog();

/**
 * @brief 获取 CIT 测试状态文本
 * @param status CIT 测试状态
 * @return 状态文本
 */
const char* GetCitTestStatusText(CitTestStatus status);

}  // namespace lilygo_box::app

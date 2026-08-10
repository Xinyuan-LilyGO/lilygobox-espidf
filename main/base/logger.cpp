/*
 * @Description: 应用日志等级过滤与格式化输出实现
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 13:27:05
 * @License: GPL 3.0
 */
#include "base/logger.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <memory>

#include "sdkconfig.h"

namespace lilygo_box {
namespace {

constexpr uint16_t kMaxLogBufferSize = 1024;

#if defined(CONFIG_LILYGO_BOX_LOG_LEVEL_DEBUG)
constexpr LogLevel kDefaultMinimumLogLevel = LogLevel::kDebug;
#elif defined(CONFIG_LILYGO_BOX_LOG_LEVEL_INFO)
constexpr LogLevel kDefaultMinimumLogLevel = LogLevel::kInfo;
#elif defined(CONFIG_LILYGO_BOX_LOG_LEVEL_WARNING)
constexpr LogLevel kDefaultMinimumLogLevel = LogLevel::kWarning;
#elif defined(CONFIG_LILYGO_BOX_LOG_LEVEL_ERROR)
constexpr LogLevel kDefaultMinimumLogLevel = LogLevel::kError;
#elif defined(CONFIG_LILYGO_BOX_LOG_LEVEL_NONE)
constexpr LogLevel kDefaultMinimumLogLevel = LogLevel::kNone;
#else
constexpr LogLevel kDefaultMinimumLogLevel = LogLevel::kInfo;
#endif

std::atomic<LogLevel> g_minimum_log_level{kDefaultMinimumLogLevel};

/**
 * @brief 获取日志级别名称
 * @param level 日志级别
 * @return 日志级别名称
 */
const char* LogLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "Debug";
    case LogLevel::kInfo:
      return "Info";
    case LogLevel::kWarning:
      return "Warning";
    case LogLevel::kError:
      return "Error";
    case LogLevel::kNone:
      return "None";
    default:
      return "Unknown";
  }
}

}  // namespace

void SetMinimumLogLevel(LogLevel level) {
  if (level > LogLevel::kNone) {
    level = LogLevel::kNone;
  }
  g_minimum_log_level.store(level, std::memory_order_relaxed);
}

LogLevel GetMinimumLogLevel() {
  return g_minimum_log_level.load(std::memory_order_relaxed);
}

bool ShouldLog(LogLevel level) {
  if (level > LogLevel::kError) {
    return false;
  }
  const LogLevel minimum_level = GetMinimumLogLevel();
  return minimum_level != LogLevel::kNone && level >= minimum_level;
}

void LogMessage(LogLevel level, const char* file_name, size_t line_number,
    const char* format, ...) {
  if (!ShouldLog(level)) {
    return;
  }

  va_list args;
  va_start(args, format);
  auto buffer = std::make_unique<char[]>(kMaxLogBufferSize);
  snprintf(buffer.get(), kMaxLogBufferSize,
      "[lilygo_box log][%s]->[%s][%u line]: %s", LogLevelName(level), file_name,
      static_cast<unsigned int>(line_number), format);
  vprintf(buffer.get(), args);
  va_end(args);
}

}  // namespace lilygo_box

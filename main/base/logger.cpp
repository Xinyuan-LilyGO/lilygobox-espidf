/*
 * @Description: 应用日志等级过滤与格式化输出实现
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 13:27:05
 * @License: GPL 3.0
 */
#include "base/logger.h"

#include <cstdarg>
#include <cstdio>
#include <memory>

namespace lilygo_box {
namespace {

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
    default:
      return "Unknown";
  }
}

/**
 * @brief 判断指定日志级别是否允许输出
 * @param level 日志级别
 * @return 允许输出返回 true，否则返回 false
 */
bool IsLogLevelEnabled(LogLevel level) {
  switch (level) {
#if defined(LILYGO_BOX_LOG_LEVEL_DEBUG)
    case LogLevel::kDebug:
      return true;
#endif
#if defined(LILYGO_BOX_LOG_LEVEL_INFO)
    case LogLevel::kInfo:
      return true;
#endif
#if defined(LILYGO_BOX_LOG_LEVEL_WARNING)
    case LogLevel::kWarning:
      return true;
#endif
#if defined(LILYGO_BOX_LOG_LEVEL_ERROR)
    case LogLevel::kError:
      return true;
#endif
    default:
      return false;
  }
}

}  // namespace

void LogMessage(LogLevel level, const char* file_name, size_t line_number,
    const char* format, ...) {
  if (!IsLogLevelEnabled(level)) {
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

/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-10 23:29:15
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "sdkconfig.h"

#if defined(CONFIG_LILYGO_BOX_LOG_LEVEL_DEBUG)
#define LILYGO_BOX_LOG_LEVEL_DEBUG
#define LILYGO_BOX_LOG_LEVEL_INFO
#define LILYGO_BOX_LOG_LEVEL_WARNING
#define LILYGO_BOX_LOG_LEVEL_ERROR
#elif defined(CONFIG_LILYGO_BOX_LOG_LEVEL_INFO)
#define LILYGO_BOX_LOG_LEVEL_INFO
#define LILYGO_BOX_LOG_LEVEL_WARNING
#define LILYGO_BOX_LOG_LEVEL_ERROR
#elif defined(CONFIG_LILYGO_BOX_LOG_LEVEL_WARNING)
#define LILYGO_BOX_LOG_LEVEL_WARNING
#define LILYGO_BOX_LOG_LEVEL_ERROR
#elif defined(CONFIG_LILYGO_BOX_LOG_LEVEL_ERROR)
#define LILYGO_BOX_LOG_LEVEL_ERROR
#elif defined(CONFIG_LILYGO_BOX_LOG_LEVEL_NONE)
#else
#define LILYGO_BOX_LOG_LEVEL_INFO
#define LILYGO_BOX_LOG_LEVEL_WARNING
#define LILYGO_BOX_LOG_LEVEL_ERROR
#endif

namespace lilygo_box {

static constexpr uint16_t kMaxLogBufferSize = 1024;

enum class LogLevel {
  kDebug,
  kInfo,
  kWarning,
  kError,
};

// Writes a formatted LilygoBox log line without depending on ESP-IDF logging.
void LogMessage(LogLevel level, const char* file_name, size_t line_number,
    const char* format, ...);

}  // namespace lilygo_box

/*
 * @Description: 应用日志等级与格式化输出接口
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-12 22:55:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace lilygo_box {

enum class LogLevel : uint8_t {
  kDebug,
  kInfo,
  kWarning,
  kError,
  kNone,
};

/**
 * @brief 设置应用最低日志输出等级
 * @param level 最低日志等级，kNone 表示禁止全部日志
 */
void SetMinimumLogLevel(LogLevel level);

/**
 * @brief 获取应用最低日志输出等级
 * @return 当前最低日志等级
 */
LogLevel GetMinimumLogLevel();

/**
 * @brief 判断指定等级的日志当前是否允许输出
 * @param level 待判断的日志等级
 * @return 允许输出返回 true，否则返回 false
 */
bool ShouldLog(LogLevel level);

/**
 * @brief 按当前最低日志等级输出 LilygoBox 格式化日志
 * @param level 日志等级
 * @param file_name 源文件名
 * @param line_number 源文件行号
 * @param format 格式化字符串
 */
void LogMessage(LogLevel level, const char* file_name, size_t line_number,
    const char* format, ...);

}  // namespace lilygo_box

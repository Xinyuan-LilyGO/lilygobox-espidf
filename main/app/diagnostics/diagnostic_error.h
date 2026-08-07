/*
 * @Description: Common diagnostic error description
 * @Author: LILYGO_L
 * @Date: 2026-08-04 00:00:00
 * @LastEditTime: 2026-08-04 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box {

struct DiagnosticError {
  const char* code;
  const char* text;
};

}  // namespace lilygo_box

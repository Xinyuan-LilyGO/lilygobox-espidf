/*
 * @Description: P4 V1/V2 与 Air 的屏幕显示与亮度公共接口
 */
#pragma once
#include <cstdint>

#include "cpp_bus_driver.h"
#include "esp_lcd_mipi_dsi.h"
#include "hal/providers/screen_provider.h"
namespace lilygo_box::hal::screen_utils {
// 板级入口提供当前屏幕与就绪状态；指针借用驱动现有对象，不转移所有权。
struct Hardware {
  enum class Type { kNone, kHi8561, kRm69a10 };
  Type type = Type::kNone;
  bool ready = false;
  cpp_bus_driver::Hi8561* hi8561 = nullptr;
  cpp_bus_driver::Rm69a10* rm69a10 = nullptr;
  esp_lcd_panel_handle_t panel = nullptr;
};

/**
 * @brief 注册 MIPI 刷新与传输完成回调
 * @param panel 屏幕的 MIPI 面板句柄
 * @param callbacks 常驻回调对象，其寿命必须覆盖驱动回调
 * @return 面板有效且注册成功时返回 true
 */
bool RegisterDisplayCallbacks(
    esp_lcd_panel_handle_t panel, ScreenProviderDisplayCallbacks* callbacks);

/**
 * @brief 将像素流写入当前屏幕的指定区域
 * @param hardware 当前屏幕与就绪状态
 * @param x_start 起始列，含该列
 * @param y_start 起始行，含该行
 * @param x_end 结束列，不含该列
 * @param y_end 结束行，不含该行
 * @param pixels 驱动要求的像素格式与缓冲；异步传输完成前保持有效
 * @return 屏幕就绪且驱动接受传输时返回 true
 */
bool WritePixels(const Hardware& hardware, int x_start, int y_start, int x_end,
    int y_end, const void* pixels);

/**
 * @brief 复制显示回调到设备成员并注册到 MIPI 驱动
 * @param hardware 当前屏幕与就绪状态
 * @param callbacks 要注册的回调及用户上下文
 * @param stored_callbacks 设备常驻成员；注册失败也可能已复制 callbacks
 * @return 屏幕就绪且注册成功时返回 true
 */
bool RegisterDisplayCallbacks(const Hardware& hardware,
    const ScreenProviderDisplayCallbacks& callbacks,
    ScreenProviderDisplayCallbacks* stored_callbacks);

/**
 * @brief 设置屏幕亮度并同步 RM69A10 的亮度状态
 * @param hardware 当前屏幕与就绪状态
 * @param backlight HI8561 使用的 PWM 背光；RM69A10 可传 nullptr
 * @param percent 目标亮度百分比，限制在 0～100
 * @param rm69a10_percent 有效状态指针；仅在 RM69A10 设置成功时更新
 * @return 屏幕就绪且亮度设置成功时返回 true
 */
bool SetBrightness(const Hardware& hardware, cpp_bus_driver::Pwm* backlight,
    int percent, int* rm69a10_percent);

/**
 * @brief 渐变屏幕亮度，HI8561 使用 PWM，RM69A10 使用软件分步
 * @param hardware 当前屏幕与就绪状态
 * @param backlight HI8561 使用的 PWM 背光；RM69A10 可传 nullptr
 * @param percent 目标亮度百分比，限制在 0～100
 * @param duration_ms 渐变时长，单位毫秒；为 0 时立即设置，此接口阻塞等待
 * @param rm69a10_percent 有效状态指针；RM69A10 失败时保留最后成功步骤的亮度
 * @return 全部亮度步骤成功时返回 true
 */
bool FadeBrightness(const Hardware& hardware, cpp_bus_driver::Pwm* backlight,
    int percent, uint32_t duration_ms, int* rm69a10_percent);
}  // namespace lilygo_box::hal::screen_utils

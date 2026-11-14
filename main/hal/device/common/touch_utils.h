/*
 * @Description: P4 V1/V2 与 Air 的触摸读取与手势处理公共接口
 */
#pragma once
#include <cstddef>
#include <cstdint>

#include "cpp_bus_driver.h"
#include "hal/providers/screen_provider.h"
namespace lilygo_box::hal::touch_utils {
// 板级入口选择实际控制器，公共实现不持有板级驱动。
struct Hardware {
  enum class Type { kNone, kHi8561, kGt9895 };
  Type type = Type::kNone;
  bool ready = false;
  cpp_bus_driver::Hi8561Touch* hi8561_touch = nullptr;
  cpp_bus_driver::Gt9895* gt9895 = nullptr;
};

/**
 * @brief 判断当前控制器与屏幕方向是否支持硬件边缘候选提示
 * @param hardware 当前触摸控制器
 * @param angle 显示旋转角度，单位度；GT9895 仅支持 0 或 180
 * @return 支持硬件边缘候选提示时返回 true
 */
bool SupportsHardwareEdgeTouchHint(const Hardware& hardware, int angle);

/**
 * @brief 判断熄屏期间是否需要软件轮询触摸
 * @param hardware 当前触摸控制器
 * @param gesture_enabled 固件手势模式是否配置成功
 * @return 已知控制器尚未配置手势模式时返回 true
 */
bool RequiresSleepingTouchPolling(
    const Hardware& hardware, bool gesture_enabled);

/**
 * @brief 切换实际触摸控制器的固件手势模式
 * @param hardware 当前控制器与就绪状态
 * @param enabled true 为进入手势模式，false 为恢复普通坐标报告
 * @return 控制器就绪且配置成功时返回 true
 */
bool SetTouchGestureWakeEnabled(const Hardware& hardware, bool enabled);

/**
 * @brief 重新配置熄屏手势并同步恢复状态
 * @param hardware 当前控制器与就绪状态
 * @param gesture_enabled 有效输出指针；失败时置 false
 * @param consecutive_errors 有效计数指针；仅在配置成功时清零
 * @return 手势模式配置成功时返回 true
 */
bool RefreshTouchWakeConfiguration(const Hardware& hardware,
    bool* gesture_enabled, uint8_t* consecutive_errors);

/**
 * @brief 读取实际控制器的单点或完整报告，不消费板级中断
 * @param hardware 当前控制器与就绪状态
 * @param frame 报告输出；失败时内容由底层驱动决定，不能作为有效坐标使用
 * @param primary_only true 只读主触摸点，false 读取完整报告
 * @return 底层读取状态；未就绪、缺少芯片或 frame 为空时返回 kInvalidData
 */
cpp_bus_driver::TouchReadStatus ReadTouchFrame(const Hardware& hardware,
    cpp_bus_driver::TouchFrame* frame, bool primary_only);

/**
 * @brief 按 P4 V1 规则转换主触摸点、双击或边缘候选
 * @param status 底层报告读取状态
 * @param frame 底层已初始化的报告
 * @param rm69a10 true 表示 RM69A10 对应的 GT9895 控制器
 * @param interrupt_received 本次是否收到板级触摸通知，仅在无坐标时作为边缘候选
 * @param point 有效输出指针；调用时先重置，失败后不可作为有效触摸使用
 * @return 产生触摸、双击或边缘候选时返回 true
 */
bool DecodePrimaryTouch(cpp_bus_driver::TouchReadStatus status,
    const cpp_bus_driver::TouchFrame& frame, bool rm69a10,
    bool interrupt_received, TouchPoint* point);

/**
 * @brief 将完整报告转换为触摸点，沿用 P4 V1 的边缘候选规则
 * @param status 底层报告读取状态
 * @param frame 底层已初始化的报告
 * @param rm69a10 true 表示 RM69A10 对应的 GT9895 控制器
 * @param interrupt_received 本次是否收到板级触摸通知
 * @param points 输出数组，容量至少为 max_points；失败时不保证清空原值
 * @param max_points 输出容量，必须大于 0
 * @param point_count 有效输出指针；失败时为 0，成功时为实际写入数
 * @return 至少输出一个有效触摸点或边缘候选时返回 true
 */
bool DecodeTouchPoints(cpp_bus_driver::TouchReadStatus status,
    const cpp_bus_driver::TouchFrame& frame, bool rm69a10,
    bool interrupt_received, TouchPoint* points, size_t max_points,
    size_t* point_count);

/**
 * @brief 在连续三次通信错误后请求恢复触摸手势配置
 * @param status 本次读取状态；非通信错误会清零连续错误计数
 * @param consecutive_errors 有效计数指针，计数最多保留为 3
 * @param gesture_enabled 有效配置标志；达到阈值时置 false，供上层重配
 */
void UpdateTouchRecovery(cpp_bus_driver::TouchReadStatus status,
    uint8_t* consecutive_errors, bool* gesture_enabled);

/**
 * @brief 记录触摸读取诊断，调用方负责日志级别判断与限频
 * @param status 本次读取状态
 * @param frame 本次已初始化的报告
 * @param interrupt_gpio 实际触摸中断引脚号
 * @param interrupt_low 当前中断引脚是否为低电平
 * @param interrupt_initialized 中断是否已注册
 * @param gesture_enabled 手势模式是否配置成功
 */
void LogTouchRead(cpp_bus_driver::TouchReadStatus status,
    const cpp_bus_driver::TouchFrame& frame, int interrupt_gpio,
    bool interrupt_low, bool interrupt_initialized, bool gesture_enabled);

}  // namespace lilygo_box::hal::touch_utils

/*
 * @Description: OTG reverse-power provider
 * @Author: LILYGO_L
 * @Date: 2026-08-18 00:00:00
 * @LastEditTime: 2026-08-18 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::hal {

class OtgProvider {
 public:
  virtual ~OtgProvider() = default;

  /**
   * @brief 设置 OTG 反向供电状态
   * @param enabled true 开启 Type-C Source 反向供电，false 保持受电角色
   * @return 设置成功返回 true，否则返回 false
   */
  virtual bool SetOtgPowerEnabled(bool enabled) = 0;

  /**
   * @brief 根据 Type-C 连接角色更新 OTG 反向供电状态
   * @return 状态更新成功返回 true，否则返回 false
   */
  virtual bool UpdateOtgPowerState() = 0;

  /**
   * @brief 读取 Type-C 接口是否检测到外部供电端
   * @param present 外部电源连接状态输出地址
   * @return 状态读取成功返回 true，否则返回 false
   */
  virtual bool ReadExternalPowerPresent(bool* present) = 0;
};

}  // namespace lilygo_box::hal

/*
 * @Description: PPA scale/rotate/mirror helper
 * @Author: LILYGO_L
 * @Date: 2026-07-02 00:00:00
 * @LastEditTime: 2026-09-02 17:52:32
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/ppa.h"
#include "esp_err.h"

namespace lilygo_box::hal {

struct PpaSrmImageConfig {
  void* buffer = nullptr;
  size_t buffer_size = 0;
  uint32_t pic_width = 0;
  uint32_t pic_height = 0;
  uint32_t block_width = 0;
  uint32_t block_height = 0;
  uint32_t block_offset_x = 0;
  uint32_t block_offset_y = 0;
  ppa_srm_color_mode_t color_mode = PPA_SRM_COLOR_MODE_RGB565;
};

struct PpaSrmTransformConfig {
  ppa_srm_rotation_angle_t rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  float scale_x = 1.0f;
  float scale_y = 1.0f;
  bool mirror_x = false;
  bool mirror_y = false;
  bool rgb_swap = false;
  bool byte_swap = false;
};

class PpaSrmHelper {
 public:
  PpaSrmHelper() = default;
  ~PpaSrmHelper();

  /**
   * @brief 初始化 PPA SRM client 和 cache 对齐信息
   * @return 初始化成功返回 true，否则返回 false
   */
  bool Init();

  /**
   * @brief 释放 PPA SRM client
   */
  void Deinit();

  /**
   * @brief 获取 SPIRAM cache line 对齐大小
   * @return cache line 对齐大小，未初始化时返回 0
   */
  size_t CacheLineSize() const { return cache_line_size_; }

  /**
   * @brief 执行 PPA SRM 转换
   * @param input 输入图像配置
   * @param output 输出图像配置
   * @param transform 转换配置
   * @return 转换成功返回 true，否则返回 false
   */
  bool Transform(const PpaSrmImageConfig& input,
      const PpaSrmImageConfig& output, const PpaSrmTransformConfig& transform);

 private:
  ppa_client_handle_t handle_ = nullptr;
  size_t cache_line_size_ = 0;
};

/**
 * @brief 向上按对齐值取整
 * @param value 原始值
 * @param alignment 对齐值
 * @return 对齐后的值
 */
size_t AlignUp(size_t value, size_t alignment);

}  // namespace lilygo_box::hal

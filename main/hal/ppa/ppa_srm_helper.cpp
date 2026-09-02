/*
 * @Description: PPA scale/rotate/mirror helper
 * @Author: LILYGO_L
 * @Date: 2026-07-02 00:00:00
 * @LastEditTime: 2026-09-02 17:52:30
 * @License: GPL 3.0
 */
#include "hal/ppa/ppa_srm_helper.h"

#include "esp_private/esp_cache_private.h"

namespace lilygo_box::hal {

PpaSrmHelper::~PpaSrmHelper() { Deinit(); }

bool PpaSrmHelper::Init() {
  if (handle_ != nullptr) {
    return true;
  }

  ppa_client_config_t config = {};
  config.oper_type = PPA_OPERATION_SRM;
  if (ppa_register_client(&config, &handle_) != ESP_OK) {
    handle_ = nullptr;
    return false;
  }

  if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &cache_line_size_) != ESP_OK) {
    Deinit();
    return false;
  }
  return true;
}

void PpaSrmHelper::Deinit() {
  if (handle_ == nullptr) {
    return;
  }

  ppa_unregister_client(handle_);
  handle_ = nullptr;
  cache_line_size_ = 0;
}

bool PpaSrmHelper::Transform(const PpaSrmImageConfig& input,
    const PpaSrmImageConfig& output, const PpaSrmTransformConfig& transform) {
  if (handle_ == nullptr || input.buffer == nullptr ||
      output.buffer == nullptr) {
    return false;
  }

  ppa_srm_oper_config_t config = {};
  config.in.buffer = input.buffer;
  config.in.pic_w = input.pic_width;
  config.in.pic_h = input.pic_height;
  config.in.block_w = input.block_width;
  config.in.block_h = input.block_height;
  config.in.block_offset_x = input.block_offset_x;
  config.in.block_offset_y = input.block_offset_y;
  config.in.srm_cm = input.color_mode;
  config.out.buffer = output.buffer;
  config.out.buffer_size = output.buffer_size;
  config.out.pic_w = output.pic_width;
  config.out.pic_h = output.pic_height;
  config.out.block_offset_x = output.block_offset_x;
  config.out.block_offset_y = output.block_offset_y;
  config.out.srm_cm = output.color_mode;
  config.rotation_angle = transform.rotation_angle;
  config.scale_x = transform.scale_x;
  config.scale_y = transform.scale_y;
  config.mirror_x = transform.mirror_x;
  config.mirror_y = transform.mirror_y;
  config.rgb_swap = transform.rgb_swap;
  config.byte_swap = transform.byte_swap;
  config.mode = PPA_TRANS_MODE_BLOCKING;
  return ppa_do_scale_rotate_mirror(handle_, &config) == ESP_OK;
}

size_t AlignUp(size_t value, size_t alignment) {
  if (alignment == 0) {
    return value;
  }
  return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace lilygo_box::hal

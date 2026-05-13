/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-05-10 13:27:05
 * @LastEditTime: 2026-05-13 09:55:00
 * @License: GPL 3.0
 */
#include "hal/screen_device.h"

namespace lilygo_box::hal {

bool ScreenDevice::PlayVibrationTest(uint8_t* waveform_count) {
  if (waveform_count != nullptr) {
    *waveform_count = 0;
  }
  return false;
}

bool ScreenDevice::PlaySpeakerTest(size_t* bytes_written) {
  if (bytes_written != nullptr) {
    *bytes_written = 0;
  }
  return false;
}

bool ScreenDevice::StartSpeakerTest() { return false; }

bool ScreenDevice::ReadSpeakerTestStatus(SpeakerTestPlaybackStatus* status) {
  if (status != nullptr) {
    *status = SpeakerTestPlaybackStatus();
  }
  return false;
}

bool ScreenDevice::ReadTouchPoints(
    TouchPoint* points, size_t max_points, size_t* point_count) {
  if (point_count != nullptr) {
    *point_count = 0;
  }
  if (points == nullptr || max_points == 0 || point_count == nullptr) {
    return false;
  }

  TouchPoint point;
  if (!ReadTouch(&point)) {
    return false;
  }

  points[0] = point;
  *point_count = 1;
  return true;
}

}  // namespace lilygo_box::hal

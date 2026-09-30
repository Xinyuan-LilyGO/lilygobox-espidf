/*
 * @Description: Material 风格指南针罗盘、定位卡片与校准界面
 * @Author: LILYGO_L
 * @Date: 2026-09-28 00:00:00
 * @LastEditTime: 2026-09-28 00:00:00
 * @License: GPL 3.0
 */
#include "ui/views/compass_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <new>

#include "app/compass_session.h"
#include "esp_timer.h"
#include "ui/haptic_feedback.h"
#include "ui/input/press_cancel.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/theme/theme_provider.h"
#include "ui/widgets/navigation_drawer.h"

namespace lilygo_box::ui {
namespace {

constexpr float kRadians = 0.017453292519943295F;
constexpr int kHeaderTop = 66;
constexpr int kContentTop = 152;
constexpr int kPortraitTopInset = 72;
constexpr int kDialReadoutGap = 40;
constexpr int kReadoutHeight = 112;
constexpr int kReadoutGpsGap = 28;
constexpr int kGpsCardHeight = 235;
constexpr int kTabIndicatorWidth = 48;
constexpr int kTabIndicatorHeight = 6;
constexpr int kCalibrationRingSegments = 72;
constexpr int kDialNumberInset = 56;
constexpr int kDialDirectionGap = 44;
constexpr int kDialNeedleGap = 28;
constexpr uint32_t kNorthColor = 0xFF7D69;
constexpr float kHeadingHapticStep = 30.0F;
constexpr float kHeadingHapticRearmDegrees = 3.0F;
constexpr float kLevelCenteredDegrees = 1.0F;
constexpr float kLevelHapticRearmDegrees = 2.0F;

struct CompassViewState {
  AppViewConfig config;
  app::CompassSession session;
  app::CompassSnapshot snapshot;
  lv_timer_t* timer = nullptr;
  lv_obj_t* root = nullptr;
  lv_obj_t* level = nullptr;
  lv_obj_t* bubble = nullptr;
  lv_obj_t* calibration = nullptr;
  lv_obj_t* calibration_track = nullptr;
  lv_obj_t* calibration_ball = nullptr;
  bool calibration_contacts[kCalibrationRingSegments] = {};
  lv_obj_t* reference = nullptr;
  lv_obj_t* tabs[2] = {};
  lv_obj_t* tab_labels[2] = {};
  lv_obj_t* tab_indicators[2] = {};
  NavigationDrawerState drawer;
  bool level_selected = false;
  lv_obj_t* dial = nullptr;
  lv_obj_t* heading = nullptr;
  lv_obj_t* degree_labels[12] = {};
  lv_obj_t* direction_labels[4] = {};
  lv_obj_t* latitude = nullptr;
  lv_obj_t* longitude = nullptr;
  lv_obj_t* altitude = nullptr;
  lv_obj_t* speed = nullptr;
  lv_obj_t* location_status = nullptr;
  lv_obj_t* calibration_button = nullptr;
  bool paused = false;
  bool started = false;
  bool heading_ready = false;
  float display_heading = 0.0F;
  bool haptic_heading_valid = false;
  float haptic_previous_heading = 0.0F;
  int haptic_last_tick = -1;
  lv_display_rotation_t haptic_rotation = LV_DISPLAY_ROTATION_0;
  bool haptic_level_valid = false;
  bool haptic_level_centered = false;
  bool calibration_haptic_armed = false;

  explicit CompassViewState(const AppViewConfig& view_config)
      : config(view_config), session(view_config.imu, view_config.gps) {}
};

/**
 * @brief 设置无边框、无内边距的页面容器样式
 * @param object 待设置样式的 LVGL 对象
 * @param color 背景颜色，使用 RGB 十六进制值
 * @param radius 圆角半径，单位为像素，可使用 LV_RADIUS_CIRCLE
 */
void StyleContainer(lv_obj_t* object, uint32_t color, int radius) {
  lv_obj_set_style_bg_color(object, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(object, radius, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief 创建使用现有系统字体和主题色的标签
 * @param parent 父对象
 * @param text 标签文本
 * @param font 标签使用的字体
 * @param color 文本颜色，使用 RGB 十六进制值
 * @return 创建成功返回标签对象，否则返回 nullptr
 */
lv_obj_t* Label(lv_obj_t* parent, const char* text, const lv_font_t* font,
    uint32_t color) {
  lv_obj_t* label = lv_label_create(parent);
  if (label != nullptr) {
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  }
  return label;
}

/**
 * @brief 将极坐标转换成 LVGL 绘图坐标，零度位于上方
 * @param x 圆心横坐标，单位为像素
 * @param y 圆心纵坐标，单位为像素
 * @param radius 距圆心的距离，单位为像素
 * @param degrees 顺时针角度，单位为度
 * @return 转换后的 LVGL 绘图坐标
 */
lv_point_precise_t Polar(float x, float y, float radius, float degrees) {
  lv_point_precise_t point;
  point.x = static_cast<decltype(point.x)>(
      std::lround(x + radius * std::sin(degrees * kRadians)));
  point.y = static_cast<decltype(point.y)>(
      std::lround(y - radius * std::cos(degrees * kRadians)));
  return point;
}

/**
 * @brief 绘制刻度及朝向磁北的双向指针，无有效数据时以默认角度显示指针
 * @param event 罗盘主绘制事件，用户数据指向指南针页面状态
 */
void DrawDial(lv_event_t* event) {
  auto* state = static_cast<CompassViewState*>(lv_event_get_user_data(event));
  lv_layer_t* layer = lv_event_get_layer(event);
  lv_area_t area;
  lv_obj_get_coords(state->dial, &area);
  const float x = (area.x1 + area.x2) * 0.5F;
  const float y = (area.y1 + area.y2) * 0.5F;
  const float radius = lv_area_get_width(&area) * 0.5F - 12.0F;
  const auto& colors = theme::GetThemeColors(state->config.theme_provider);
  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  const float bearing = state->heading_ready ? state->display_heading : 0.0F;
  for (int i = 0; i < 60; ++i) {
    const bool major = i % 5 == 0;
    line.color = lv_color_hex(i == 0 ? kNorthColor : colors.outline);
    line.width = major ? 3 : 1;
    line.p1 = Polar(x, y, radius, i * 6.0F - bearing);
    line.p2 = Polar(x, y, radius - (major ? 17.0F : 8.0F), i * 6.0F - bearing);
    lv_draw_line(layer, &line);
  }
  // 固定的顶部标记表示设备朝向，与随磁北旋转的盘面区分。
  lv_draw_triangle_dsc_t marker;
  lv_draw_triangle_dsc_init(&marker);
  marker.color = lv_color_hex(colors.primary);
  marker.p[0] = Polar(x, y, radius + 4.0F, 0.0F);
  marker.p[1] = Polar(x, y, radius + 11.0F, -2.0F);
  marker.p[2] = Polar(x, y, radius + 11.0F, 2.0F);
  lv_draw_triangle(layer, &marker);
  // 无有效方向时以默认角度绘制指针，北端始终为红色，避免启动时变色。
  // 罗盘与方位标记一起旋转；设备朝东时磁北位于屏幕左侧。
  const float angle = -bearing;
  const float needle_length = std::max(12.0F,
      lv_area_get_width(&area) * 0.5F - kDialNumberInset -
          kDialDirectionGap - kDialNeedleGap);
  lv_draw_triangle_dsc_t needle;
  lv_draw_triangle_dsc_init(&needle);
  needle.p[0] = Polar(x, y, needle_length, angle);
  needle.p[1] = Polar(x, y, 10.0F, angle + 90.0F);
  needle.p[2] = Polar(x, y, 10.0F, angle - 90.0F);
  needle.color = lv_color_hex(kNorthColor);
  lv_draw_triangle(layer, &needle);
  needle.p[0] = Polar(x, y, needle_length, angle + 180.0F);
  needle.color = lv_color_hex(colors.on_surface_variant);
  lv_draw_triangle(layer, &needle);
}

/**
 * @brief 将有效 NMEA 坐标转换为十进制度；无定位时保留占位文本
 * @param label 坐标显示标签
 * @param title 坐标名称，例如 Lat 或 Lon
 * @param coordinate NMEA 坐标及方向信息
 * @param positioned 当前定位数据是否有效
 */
void CoordinateText(lv_obj_t* label, const char* title,
    const hal::GpsCoordinate& coordinate, bool positioned) {
  if (!positioned || !coordinate.ready || !std::isfinite(coordinate.minutes)) {
    lv_label_set_text_fmt(label, "%s  --", title);
    return;
  }
  const double degrees = coordinate.degrees + coordinate.minutes / 60.0;
  // LVGL 内置格式化未启用浮点支持，先用标准库格式化再设置标签。
  char text[64];
  std::snprintf(text, sizeof(text), "%s  %.5f° %s", title, degrees,
      coordinate.direction);
  lv_label_set_text(label, text);
}

/**
 * @brief 根据采样和校准状态更新侧边栏操作文字及可用状态
 * @param state 指南针页面状态
 */
void UpdateStatus(CompassViewState* state) {
  if (state->calibration_button == nullptr) {
    return;
  }
  using app::CompassCalibration;
  const auto& snapshot = state->snapshot;
  const bool collecting = snapshot.calibration == CompassCalibration::kCollecting;
  SetNavigationDrawerItemText(state->calibration_button,
      collecting ? "Cancel calibration" : "Calibrate");
  if (state->started && snapshot.imu_started && !state->paused) {
    lv_obj_remove_state(state->calibration_button, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(state->calibration_button, LV_STATE_DISABLED);
  }
}

/**
 * @brief 绘制校准细圆环，并将本轮小球触碰过的圆弧加粗
 * @param event 圆环绘制事件，用户数据指向指南针页面状态
 */
void DrawCalibrationRing(lv_event_t* event) {
  auto* state = static_cast<CompassViewState*>(lv_event_get_user_data(event));
  lv_area_t area;
  lv_obj_get_coords(state->calibration_track, &area);
  const auto& colors = theme::GetThemeColors(state->config.theme_provider);
  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.center.x = (area.x1 + area.x2) / 2;
  arc.center.y = (area.y1 + area.y2) / 2;
  arc.radius = lv_area_get_width(&area) / 2;
  arc.width = 2;
  arc.color = lv_color_hex(colors.outline);
  arc.start_angle = 0;
  arc.end_angle = 360;
  lv_layer_t* layer = lv_event_get_layer(event);
  lv_draw_arc(layer, &arc);
  arc.width = 6;
  arc.rounded = true;
  arc.color = lv_color_hex(colors.on_surface);
  for (int i = 0; i < kCalibrationRingSegments;) {
    if (!state->calibration_contacts[i]) {
      ++i;
      continue;
    }
    const int start = i;
    while (i < kCalibrationRingSegments && state->calibration_contacts[i]) {
      ++i;
    }
    arc.start_angle = start * 360 / kCalibrationRingSegments;
    arc.end_angle = i * 360 / kCalibrationRingSegments;
    lv_draw_arc(layer, &arc);
  }
}

/**
 * @brief 按实时姿态移动校准小球，并显示转动或质量验证提示
 * @param state 指南针页面状态
 * @param now 当前单调时钟时间，单位为毫秒
 */
void UpdateCalibration(CompassViewState* state, int64_t now) {
  const auto& snapshot = state->snapshot;
  const auto& colors = theme::GetThemeColors(state->config.theme_provider);
  const bool ready = snapshot.attitude_ready &&
                     now - snapshot.attitude_time_ms <= 1500;
  lv_label_set_text(state->heading, "Calibrate");
  lv_obj_set_style_bg_color(state->calibration_ball,
      lv_color_hex(ready ? kNorthColor : colors.outline), LV_PART_MAIN);
  if (!ready) {
    lv_obj_center(state->calibration_ball);
    lv_label_set_text(state->reference, "WAITING FOR SENSOR");
    return;
  }
  const float pitch = snapshot.pitch_deg * kRadians;
  const float roll = snapshot.roll_deg * kRadians;
  const float rotation = static_cast<int>(lv_display_get_rotation(
      lv_obj_get_display(state->calibration))) * 90.0F * kRadians;
  const float x = std::sin(roll) * std::cos(pitch);
  const float y = -std::sin(pitch);
  // 与 LVGL 触摸坐标旋转一致，将竖屏基准向量转换到当前显示坐标。
  const float dx = x * std::cos(rotation) - y * std::sin(rotation);
  const float dy = y * std::cos(rotation) + x * std::sin(rotation);
  const float radius = (lv_obj_get_width(state->calibration_track) -
      lv_obj_get_width(state->calibration_ball)) * 0.5F - 2.0F;
  const int ball_x = static_cast<int>(std::lround(radius * dx));
  const int ball_y = static_cast<int>(std::lround(radius * dy));
  lv_obj_align(state->calibration_ball, LV_ALIGN_CENTER, ball_x, ball_y);
  // 仅在球边缘接触圆环时记录轨迹，允许 2 像素的取整及姿态抖动误差。
  // 轨迹只用于动作反馈，不作为磁场校准成功的判据。
  if (std::hypot(static_cast<float>(ball_x), static_cast<float>(ball_y)) >=
      radius - 2.0F) {
    const float angle = std::atan2(dy, dx) / kRadians + 360.0F;
    const int segment = static_cast<int>(std::lround(
        angle * kCalibrationRingSegments / 360.0F)) % kCalibrationRingSegments;
    bool changed = false;
    for (int offset = -1; offset <= 1; ++offset) {
      const int index = (segment + offset + kCalibrationRingSegments) %
                        kCalibrationRingSegments;
      changed |= !state->calibration_contacts[index];
      state->calibration_contacts[index] = true;
    }
    if (changed) {
      lv_obj_invalidate(state->calibration_track);
    }
  }
  const bool ring_complete = std::all_of(state->calibration_contacts,
      state->calibration_contacts + kCalibrationRingSegments,
      [](bool touched) { return touched; });
  lv_label_set_text(state->reference, snapshot.calibration_verifying
      ? "Keep rotating · Checking"
      : ring_complete ? "Tilt in other directions" : "Tilt and rotate slowly");
}

/**
 * @brief 方位角跨过 30° 刻度时振动，同一刻度须离开一定距离后才能再次触发
 * @param state 指南针页面状态
 * @param rotation 当前显示旋转角度枚举，改变时重新建立参考
 */
void UpdateHeadingHaptic(CompassViewState* state, lv_display_rotation_t rotation) {
  const float heading = state->display_heading;
  if (!state->haptic_heading_valid || state->haptic_rotation != rotation) {
    state->haptic_heading_valid = true;
    state->haptic_previous_heading = heading;
    state->haptic_last_tick = -1;
    state->haptic_rotation = rotation;
    return;
  }

  const float previous = state->haptic_previous_heading;
  const float delta = std::remainder(heading - previous, 360.0F);
  const float unwrapped = previous + delta;
  const float tick = delta > 0.0F
      ? std::floor(unwrapped / kHeadingHapticStep) * kHeadingHapticStep
      : std::ceil(unwrapped / kHeadingHapticStep) * kHeadingHapticStep;
  const bool crossed = delta > 0.0F ? tick > previous
                                   : delta < 0.0F && tick < previous;
  state->haptic_previous_heading = heading;
  if (state->haptic_last_tick >= 0 &&
      std::fabs(std::remainder(
          heading - state->haptic_last_tick * kHeadingHapticStep, 360.0F)) >=
          kHeadingHapticRearmDegrees) {
    state->haptic_last_tick = -1;
  }
  if (!crossed) {
    return;
  }

  constexpr int kTickCount = 12;
  const int index = (static_cast<int>(std::lround(tick / kHeadingHapticStep)) %
      kTickCount + kTickCount) % kTickCount;
  if (index != state->haptic_last_tick) {
    state->haptic_last_tick = index;
    // 单次刷新跨过多个刻度时合并为一次反馈，避免补发振动形成长时间连震。
    PlayUiHapticFeedback();
  }
}

/**
 * @brief 水平仪进入中心范围时振动，离开较大范围后才允许再次触发
 * @param state 指南针页面状态
 * @param tilt 当前倾斜角，单位为度
 */
void UpdateLevelHaptic(CompassViewState* state, float tilt) {
  if (!state->haptic_level_valid) {
    state->haptic_level_valid = true;
    state->haptic_level_centered = tilt <= kLevelCenteredDegrees;
    return;
  }
  if (tilt >= kLevelHapticRearmDegrees) {
    state->haptic_level_centered = false;
  } else if (tilt <= kLevelCenteredDegrees && !state->haptic_level_centered) {
    state->haptic_level_centered = true;
    PlayUiHapticFeedback();
  }
}

/**
 * @brief 根据快照切换校准仪表并刷新读数，过期数据使用等待提示
 * @param state 指南针页面状态
 */
void UpdateDisplay(CompassViewState* state) {
  const auto& snapshot = state->snapshot;
  const int64_t now = esp_timer_get_time() / 1000;
  state->heading_ready = snapshot.heading_ready &&
                         now - snapshot.heading_time_ms <= 1500;
  const bool collecting =
      snapshot.calibration == app::CompassCalibration::kCollecting;
  if (collecting) {
    state->calibration_haptic_armed = true;
  } else {
    // 只反馈本轮采集后的成功事件，恢复 NVS 校准或取消校准不会触发。
    if (state->calibration_haptic_armed &&
        snapshot.calibration == app::CompassCalibration::kReady &&
        snapshot.calibration_completed_time_ms != 0) {
      PlayUiHapticFeedback();
    }
    state->calibration_haptic_armed = false;
  }
  if (collecting || state->level_selected || !state->heading_ready) {
    state->haptic_heading_valid = false;
  }
  if (collecting || !state->level_selected) {
    state->haptic_level_valid = false;
  }
  lv_obj_set_flag(state->dial, LV_OBJ_FLAG_HIDDEN,
      collecting || state->level_selected);
  lv_obj_set_flag(state->level, LV_OBJ_FLAG_HIDDEN,
      collecting || !state->level_selected);
  lv_obj_set_flag(state->calibration, LV_OBJ_FLAG_HIDDEN, !collecting);
  if (collecting) {
    UpdateCalibration(state, now);
  } else if (state->level_selected) {
    const bool ready = snapshot.attitude_ready &&
                       now - snapshot.attitude_time_ms <= 1500;
    if (ready) {
      const float pitch = snapshot.pitch_deg * kRadians;
      const float roll = snapshot.roll_deg * kRadians;
      const float vertical = std::clamp(
          std::fabs(std::cos(pitch) * std::cos(roll)), 0.0F, 1.0F);
      const float tilt = std::acos(vertical) / kRadians;
      UpdateLevelHaptic(state, tilt);
      const float rotation = static_cast<int>(lv_display_get_rotation(
          lv_obj_get_display(state->level))) * 90.0F * kRadians;
      const float x = std::sin(roll) * std::cos(pitch);
      const float y = -std::sin(pitch);
      const float radius = lv_obj_get_width(state->level) * 0.5F - 26.0F;
      // 屏幕旋转后，小球仍沿当前屏幕对应的倾斜方向移动。
      const int dx = static_cast<int>(std::lround(
          radius * (x * std::cos(rotation) - y * std::sin(rotation))));
      const int dy = static_cast<int>(std::lround(
          radius * (y * std::cos(rotation) + x * std::sin(rotation))));
      lv_obj_align(state->bubble, LV_ALIGN_CENTER, dx, dy);
      lv_obj_remove_flag(state->bubble, LV_OBJ_FLAG_HIDDEN);
      const auto& colors = theme::GetThemeColors(state->config.theme_provider);
      lv_obj_set_style_border_color(state->bubble,
          lv_color_hex(tilt <= kLevelCenteredDegrees ? colors.success : kNorthColor),
          LV_PART_MAIN);
      char text[32];
      std::snprintf(text, sizeof(text), "%.1f°", static_cast<double>(tilt));
      lv_label_set_text(state->heading, text);
      lv_label_set_text(state->reference,
          tilt <= kLevelCenteredDegrees ? "LEVEL" : "TILT ANGLE");
    } else {
      state->haptic_level_valid = false;
      lv_obj_add_flag(state->bubble, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(state->heading, "--°");
      lv_label_set_text(state->reference, "WAITING FOR SENSOR");
    }
  } else {
    if (state->heading_ready) {
      const auto rotation = lv_display_get_rotation(lv_obj_get_display(state->dial));
      const float rotation_deg = static_cast<int>(rotation) * 90.0F;
      // LVGL 的 90° 显示旋转使画面顶部对应竖屏左侧，方位角应减去旋转角。
      // 以当前画面顶部为参考；数值、方位文字、刻度和指针共用此结果。
      state->display_heading =
          std::fmod(snapshot.heading_deg - rotation_deg + 360.0F, 360.0F);
      UpdateHeadingHaptic(state, rotation);
      const int heading = static_cast<int>(std::lround(state->display_heading)) % 360;
      constexpr const char* kDirections[] = {
          "N", "NE", "E", "SE", "S", "SW", "W", "NW"};
      const int direction =
          static_cast<int>((state->display_heading + 22.5F) / 45.0F) % 8;
      lv_label_set_text_fmt(state->heading, "%s  %d°", kDirections[direction], heading);
    } else {
      lv_label_set_text(state->heading, "--°");
    }
    lv_label_set_text(state->reference,
        state->heading_ready ? "MAGNETIC NORTH" : "WAITING FOR SENSOR");
  }
  if (snapshot.calibration == app::CompassCalibration::kReady &&
      snapshot.calibration_completed_time_ms != 0 &&
      now - snapshot.calibration_completed_time_ms < 3000) {
    lv_label_set_text(state->reference, snapshot.calibration_save_failed
        ? "Calibrated · Save failed" : "Calibration complete");
  }
  if (snapshot.calibration == app::CompassCalibration::kFailed) {
    lv_label_set_text(state->reference, "Calibration failed · Retry");
  }
  const float number_radius =
      lv_obj_get_width(state->dial) * 0.5F - kDialNumberInset;
  const float bearing = state->heading_ready ? state->display_heading : 0.0F;
  for (int i = 0; i < 12; ++i) {
    const auto point = Polar(0, 0, number_radius, i * 30.0F - bearing);
    lv_obj_align(state->degree_labels[i], LV_ALIGN_CENTER, point.x, point.y);
  }
  for (int i = 0; i < 4; ++i) {
    const auto point = Polar(0, 0, number_radius - kDialDirectionGap,
        i * 90.0F - bearing);
    lv_obj_align(state->direction_labels[i], LV_ALIGN_CENTER, point.x, point.y);
  }
  lv_obj_invalidate(state->dial);
  const bool positioned = snapshot.gps.positioned && snapshot.gps_time_ms != 0 &&
                          now - snapshot.gps_time_ms <= 10000;
  CoordinateText(state->latitude, "Lat", snapshot.gps.latitude, positioned);
  CoordinateText(state->longitude, "Lon", snapshot.gps.longitude, positioned);
  if (positioned && snapshot.gps.altitude_ready &&
      std::isfinite(snapshot.gps.altitude)) {
    char text[64];
    std::snprintf(text, sizeof(text), "Altitude  %.1f %s",
        static_cast<double>(snapshot.gps.altitude),
        snapshot.gps.altitude_unit[0] != '\0' ? snapshot.gps.altitude_unit : "m");
    lv_label_set_text(state->altitude, text);
  } else {
    lv_label_set_text(state->altitude, "Altitude  --");
  }
  if (positioned && snapshot.gps.speed_ready &&
      std::isfinite(snapshot.gps.speed_kmh) && snapshot.gps.speed_kmh >= 0.0F) {
    char text[64];
    std::snprintf(text, sizeof(text), "Speed  %.1f km/h",
        static_cast<double>(snapshot.gps.speed_kmh));
    lv_label_set_text(state->speed, text);
  } else {
    lv_label_set_text(state->speed, "Speed  --");
  }
  const char* location_status = "GPS · No fix";
  if (positioned && !snapshot.gps_failed) {
    // 定位维度来自 GSA，不根据海拔或卫星数量推断。
    location_status = "GPS · --";
    if (snapshot.gps.fix_mode_ready) {
      switch (snapshot.gps.fix_mode) {
        case 1: location_status = "GPS · No fix"; break;
        case 2: location_status = "GPS · 2D"; break;
        case 3: location_status = "GPS · 3D"; break;
        default: break;
      }
    }
  }
  lv_label_set_text(state->location_status, location_status);
  UpdateStatus(state);
}

/**
 * @brief 定时读取后台快照，刷新当前仪表及 GPS 信息
 * @param timer 页面刷新定时器，用户数据指向指南针页面状态
 */
void Refresh(lv_timer_t* timer) {
  auto* state = static_cast<CompassViewState*>(lv_timer_get_user_data(timer));
  if (state->paused) {
    return;
  }
  state->session.Read(&state->snapshot);
  UpdateDisplay(state);
}

/**
 * @brief 启动或取消后台磁场校准，并立即切换对应仪表
 * @param event 校准按钮点击事件，用户数据指向指南针页面状态
 */
void Calibrate(lv_event_t* event) {
  auto* state = static_cast<CompassViewState*>(lv_event_get_user_data(event));
  const bool start =
      state->snapshot.calibration != app::CompassCalibration::kCollecting;
  if (start) {
    std::fill_n(state->calibration_contacts, kCalibrationRingSegments, false);
    lv_obj_invalidate(state->calibration_track);
  }
  state->session.SetCalibrating(start);
  state->snapshot.calibration = start ? app::CompassCalibration::kCollecting
                                     : app::CompassCalibration::kNeeded;
  state->snapshot.calibration_verifying = false;
  state->snapshot.calibration_completed_time_ms = 0;
  state->snapshot.calibration_save_failed = false;
  UpdateDisplay(state);
  CloseNavigationDrawer(&state->drawer);
}

/**
 * @brief 停止刷新、解除锁屏回调并等待后台设备关闭后释放页面状态
 * @param event 页面删除事件，用户数据指向待释放的指南针页面状态
 */
void DeleteView(lv_event_t* event) {
  auto* state = static_cast<CompassViewState*>(lv_event_get_user_data(event));
  if (state->timer != nullptr) {
    lv_timer_delete(state->timer);
  }
  if (state->config.set_lock_screen_visibility_callback) {
    state->config.set_lock_screen_visibility_callback({});
  }
  // 抽屉的删除回调引用页面状态，必须在释放状态前销毁抽屉。
  if (state->drawer.overlay != nullptr) {
    lv_obj_delete(state->drawer.overlay);
  }
  delete state;
}

/**
 * @brief 创建圆形罗盘，外层显示度数数字，内层显示较大的主方向字母
 * @param parent 罗盘区域的父对象
 * @param state 指南针页面状态，用于保存创建的控件
 * @param diameter 罗盘直径，单位为像素
 * @return 所有控件创建成功返回 true，否则返回 false
 */
bool CreateDial(lv_obj_t* parent, CompassViewState* state, int diameter) {
  const auto& colors = theme::GetThemeColors(state->config.theme_provider);
  state->dial = lv_obj_create(parent);
  if (state->dial == nullptr) {
    return false;
  }
  StyleContainer(state->dial, colors.surface_container_low, LV_RADIUS_CIRCLE);
  lv_obj_set_size(state->dial, diameter, diameter);
  lv_obj_align(state->dial, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_add_event_cb(state->dial, DrawDial, LV_EVENT_DRAW_MAIN, state);
  const float number_radius = diameter * 0.5F - kDialNumberInset;
  for (int i = 0; i < 12; ++i) {
    char text[4];
    std::snprintf(text, sizeof(text), "%d", i * 30);
    lv_obj_t* label = Label(state->dial, text,
        &lvgl_font_google_sans_flex_22, colors.on_surface_variant);
    if (label == nullptr) {
      return false;
    }
    state->degree_labels[i] = label;
    const auto point = Polar(0, 0, number_radius, i * 30.0F);
    lv_obj_align(label, LV_ALIGN_CENTER, point.x, point.y);
  }
  constexpr const char* kDirections[] = {"N", "E", "S", "W"};
  for (int i = 0; i < 4; ++i) {
    lv_obj_t* label = Label(state->dial, kDirections[i],
        &lvgl_font_google_sans_flex_32, 0x000000);
    if (label == nullptr) {
      return false;
    }
    state->direction_labels[i] = label;
    const auto point = Polar(0, 0, number_radius - kDialDirectionGap, i * 90.0F);
    lv_obj_align(label, LV_ALIGN_CENTER, point.x, point.y);
  }
  lv_obj_t* center = lv_obj_create(state->dial);
  if (center == nullptr) {
    return false;
  }
  StyleContainer(center, colors.primary, LV_RADIUS_CIRCLE);
  lv_obj_set_size(center, 18, 18);
  lv_obj_center(center);
  return true;
}

/**
 * @brief 创建方位或倾斜读数及说明，供横竖屏布局独立定位
 * @param parent 读数区域父对象
 * @param state 指南针页面状态，用于保存文字控件
 * @return 所有文字控件创建成功返回 true，否则返回 false
 */
bool CreateReadout(lv_obj_t* parent, CompassViewState* state) {
  const auto& colors = theme::GetThemeColors(state->config.theme_provider);
  state->heading = Label(parent, "--°",
      &lvgl_font_google_sans_flex_64, colors.on_surface);
  if (state->heading == nullptr) {
    return false;
  }
  lv_obj_align(state->heading, LV_ALIGN_TOP_MID, 0, 0);
  state->reference = Label(parent, "MAGNETIC NORTH",
      &lvgl_font_google_sans_flex_22, colors.on_surface_variant);
  if (state->reference == nullptr) {
    return false;
  }
  lv_obj_align(state->reference, LV_ALIGN_TOP_MID, 0, 76);
  return true;
}

/**
 * @brief 创建共用的 GPS 定位信息卡片
 * @param parent 信息区域的父对象
 * @param state 指南针页面状态，用于保存创建的控件
 * @param width 信息区域宽度，单位为像素
 * @return 所有控件创建成功返回 true，否则返回 false
 */
bool CreateDetails(lv_obj_t* parent, CompassViewState* state, int width) {
  const auto& colors = theme::GetThemeColors(state->config.theme_provider);
  lv_obj_t* location = lv_obj_create(parent);
  if (location == nullptr) {
    return false;
  }
  StyleContainer(location, colors.surface_container, 28);
  lv_obj_set_size(location, width, kGpsCardHeight);
  state->location_status = Label(location, "GPS · No fix",
      &lvgl_font_google_sans_flex_22, colors.on_surface_variant);
  state->latitude = Label(location, "Lat  --",
      &lvgl_font_google_sans_flex_24, colors.on_surface);
  state->longitude = Label(location, "Lon  --",
      &lvgl_font_google_sans_flex_24, colors.on_surface);
  state->altitude = Label(location, "Altitude  --",
      &lvgl_font_google_sans_flex_24, colors.on_surface);
  state->speed = Label(location, "Speed  --",
      &lvgl_font_google_sans_flex_24, colors.on_surface);
  if (!state->location_status || !state->latitude || !state->longitude ||
      !state->altitude || !state->speed) {
    return false;
  }
  lv_obj_set_pos(state->location_status, 22, 18);
  lv_obj_set_pos(state->latitude, 22, 58);
  lv_obj_set_pos(state->longitude, 22, 99);
  lv_obj_set_pos(state->altitude, 22, 140);
  lv_obj_set_pos(state->speed, 22, 181);
  for (auto* label : {state->latitude, state->longitude, state->altitude,
           state->speed}) {
    lv_obj_set_width(label, width - 44);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  }
  return true;
}

/**
 * @brief 抽屉关闭时清除校准控件引用，避免定时器访问已删除对象
 * @param event 抽屉删除事件，用户数据指向指南针页面状态
 */
void DrawerDeleted(lv_event_t* event) {
  auto* state = static_cast<CompassViewState*>(lv_event_get_user_data(event));
  state->calibration_button = nullptr;
}

/**
 * @brief 使用公共侧边栏显示指南针校准操作
 * @param event 菜单点击事件，用户数据指向指南针页面状态
 */
void OpenDrawer(lv_event_t* event) {
  auto* state = static_cast<CompassViewState*>(lv_event_get_user_data(event));
  if (IsNavigationDrawerOpen(&state->drawer)) {
    return;
  }
  const auto config = MakeNavigationDrawerConfig(
      state->config.width, state->config.height, "Compass");
  lv_obj_t* panel = OpenNavigationDrawer(state->root, &state->drawer, config);
  if (panel == nullptr) {
    return;
  }
  lv_obj_add_event_cb(state->drawer.overlay, DrawerDeleted, LV_EVENT_DELETE, state);
  state->calibration_button = CreateNavigationDrawerItem(&state->drawer,
      icon::kRefresh, "Calibrate", kNavigationDrawerContentTop, Calibrate, state);
  if (state->calibration_button == nullptr) {
    CloseNavigationDrawer(&state->drawer);
    return;
  }
  CreateNavigationDrawerDivider(&state->drawer,
      kNavigationDrawerContentTop + kNavigationDrawerItemHeight + 12);
  UpdateStatus(state);
  PresentNavigationDrawer(&state->drawer);
}

/**
 * @brief 创建水平仪圆环、中心参考点和随倾斜移动的气泡
 * @param parent 仪表区域父对象
 * @param state 指南针页面状态
 * @param diameter 圆环直径，单位为像素
 * @return 所有控件创建成功返回 true，否则返回 false
 */
bool CreateLevel(lv_obj_t* parent, CompassViewState* state, int diameter) {
  const auto& colors = theme::GetThemeColors(state->config.theme_provider);
  state->level = lv_obj_create(parent);
  if (state->level == nullptr) {
    return false;
  }
  StyleContainer(state->level, colors.surface, LV_RADIUS_CIRCLE);
  lv_obj_set_size(state->level, diameter, diameter);
  lv_obj_align(state->level, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_border_width(state->level, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(state->level, lv_color_hex(colors.outline),
      LV_PART_MAIN);
  lv_obj_t* center = lv_obj_create(state->level);
  state->bubble = lv_obj_create(state->level);
  if (!center || !state->bubble) {
    return false;
  }
  for (auto* circle : {center, state->bubble}) {
    StyleContainer(circle, colors.surface, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(circle, 3, LV_PART_MAIN);
    lv_obj_center(circle);
  }
  lv_obj_set_size(center, 18, 18);
  lv_obj_set_style_border_color(center, lv_color_hex(colors.on_surface),
      LV_PART_MAIN);
  lv_obj_set_size(state->bubble, 36, 36);
  lv_obj_set_style_border_color(state->bubble, lv_color_hex(kNorthColor),
      LV_PART_MAIN);
  lv_obj_add_flag(state->level, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(state->bubble, LV_OBJ_FLAG_HIDDEN);
  return true;
}

/**
 * @brief 创建校准专用触碰轨迹圆环及实心姿态小球
 * @param parent 仪表区域父对象
 * @param state 指南针页面状态
 * @param diameter 仪表直径，单位为像素
 * @return 所有控件创建成功返回 true，否则返回 false
 */
bool CreateCalibration(lv_obj_t* parent, CompassViewState* state, int diameter) {
  const auto& colors = theme::GetThemeColors(state->config.theme_provider);
  state->calibration = lv_obj_create(parent);
  if (state->calibration == nullptr) {
    return false;
  }
  StyleContainer(state->calibration, colors.surface, LV_RADIUS_CIRCLE);
  lv_obj_set_size(state->calibration, diameter, diameter);
  lv_obj_align(state->calibration, LV_ALIGN_TOP_MID, 0, 0);
  state->calibration_track = lv_obj_create(state->calibration);
  state->calibration_ball = lv_obj_create(state->calibration);
  if (!state->calibration_track || !state->calibration_ball) {
    return false;
  }
  lv_obj_t* track = state->calibration_track;
  StyleContainer(track, colors.surface, LV_RADIUS_CIRCLE);
  lv_obj_set_size(track, diameter - 4, diameter - 4);
  lv_obj_center(track);
  lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(track, DrawCalibrationRing, LV_EVENT_DRAW_MAIN, state);
  StyleContainer(state->calibration_ball, kNorthColor, LV_RADIUS_CIRCLE);
  lv_obj_set_size(state->calibration_ball, diameter / 10, diameter / 10);
  lv_obj_center(state->calibration_ball);
  lv_obj_remove_flag(state->calibration_ball, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(state->calibration, LV_OBJ_FLAG_HIDDEN);
  return true;
}

/**
 * @brief 切换指南针和水平仪；校准期间保留引导，结束后恢复选中页
 * @param event 顶部标签点击事件，用户数据指向指南针页面状态
 */
void SelectTab(lv_event_t* event) {
  auto* state = static_cast<CompassViewState*>(lv_event_get_user_data(event));
  state->level_selected = lv_event_get_current_target_obj(event) == state->tabs[1];
  const auto& colors = theme::GetThemeColors(state->config.theme_provider);
  for (int i = 0; i < 2; ++i) {
    const bool selected = (i == 1) == state->level_selected;
    lv_obj_set_style_text_color(state->tab_labels[i],
        lv_color_hex(selected ? colors.on_surface : colors.on_surface_variant),
        LV_PART_MAIN);
    if (selected) {
      lv_obj_remove_flag(state->tab_indicators[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(state->tab_indicators[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  Refresh(state->timer);
}

}  // namespace

lv_obj_t* CreateCompassView(lv_obj_t* parent, const app::AppEntry& app_entry,
    const AppViewConfig& config) {
  if (parent == nullptr || config.width <= 0 || config.height <= kContentTop) {
    return nullptr;
  }
  auto* state = new (std::nothrow) CompassViewState(config);
  if (state == nullptr) {
    return nullptr;
  }
  lv_obj_t* root = lv_obj_create(parent);
  if (root == nullptr) {
    delete state;
    return nullptr;
  }
  const auto& colors = theme::GetThemeColors(config.theme_provider);
  StyleContainer(root, colors.surface, 0);
  lv_obj_set_size(root, config.width, config.height);
  lv_obj_center(root);
  lv_obj_add_event_cb(root, DeleteView, LV_EVENT_DELETE, state);
  state->root = root;
  lv_obj_t* menu = lv_button_create(root);
  if (menu == nullptr) {
    lv_obj_delete(root);
    return nullptr;
  }
  lv_obj_remove_style_all(menu);
  lv_obj_add_flag(menu, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(menu, 72, 72);
  lv_obj_set_pos(menu, 20, kHeaderTop - 2);
  lv_obj_t* menu_icon = Label(menu, icon::kMenu,
      &lvgl_font_material_symbols_fill_56, colors.on_surface);
  if (!menu_icon || !AddPressCancelOnLeave(menu)) {
    lv_obj_delete(root);
    return nullptr;
  }
  lv_obj_center(menu_icon);
  lv_obj_add_event_cb(menu, OpenDrawer, LV_EVENT_CLICKED, state);
  const int tab_width = (config.width - 124) / 2;
  for (int i = 0; i < 2; ++i) {
    state->tabs[i] = lv_button_create(root);
    if (state->tabs[i] == nullptr) {
      lv_obj_delete(root);
      return nullptr;
    }
    lv_obj_remove_style_all(state->tabs[i]);
    lv_obj_set_size(state->tabs[i], tab_width, 64);
    lv_obj_set_pos(state->tabs[i], 104 + i * tab_width, kHeaderTop);
    state->tab_labels[i] = Label(state->tabs[i], i == 0 ? app_entry.title : "Level",
        &lvgl_font_google_sans_flex_32,
        i == 0 ? colors.on_surface : colors.on_surface_variant);
    if (!state->tab_labels[i] || !AddPressCancelOnLeave(state->tabs[i])) {
      lv_obj_delete(root);
      return nullptr;
    }
    lv_obj_center(state->tab_labels[i]);
    state->tab_indicators[i] = lv_obj_create(state->tabs[i]);
    if (state->tab_indicators[i] == nullptr) {
      lv_obj_delete(root);
      return nullptr;
    }
    StyleContainer(state->tab_indicators[i], colors.primary, 3);
    lv_obj_remove_flag(state->tab_indicators[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(state->tab_indicators[i], kTabIndicatorWidth,
        kTabIndicatorHeight);
    lv_obj_align(state->tab_indicators[i], LV_ALIGN_BOTTOM_MID, 0, 0);
    if (i != 0) {
      lv_obj_add_flag(state->tab_indicators[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_event_cb(state->tabs[i], SelectTab, LV_EVENT_CLICKED, state);
  }

  lv_obj_t* body = lv_obj_create(root);
  if (body == nullptr) {
    lv_obj_delete(root);
    return nullptr;
  }
  StyleContainer(body, colors.surface, 0);
  lv_obj_set_pos(body, 24, kContentTop);
  const int body_width = config.width - 48;
  const int body_height = config.height - kContentTop - 24;
  const bool landscape = config.width > config.height;
  lv_obj_set_size(body, body_width, body_height);
  if (!landscape) {
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
  }
  const int panel_width = landscape ? (body_width - 24) / 2 : body_width;
  const int diameter = std::max(180, std::min(panel_width - 16,
      landscape ? std::min(340, body_height) : 440));
  lv_obj_t* compass = lv_obj_create(body);
  lv_obj_t* readout = lv_obj_create(body);
  lv_obj_t* details = lv_obj_create(body);
  if (!compass || !readout || !details) {
    lv_obj_delete(root);
    return nullptr;
  }
  StyleContainer(compass, colors.surface, 0);
  lv_obj_set_size(compass, panel_width, diameter);
  StyleContainer(readout, colors.surface, 0);
  lv_obj_set_size(readout, panel_width, kReadoutHeight);
  StyleContainer(details, colors.surface, 0);
  lv_obj_set_size(details, panel_width, kGpsCardHeight);
  if (landscape) {
    // 左侧仅保留仪表；右侧读数在 GPS 上方，按可用高度整体居中。
    const int gps_offset = std::min(kReadoutHeight + 14,
        std::max(104, body_height - kGpsCardHeight));
    const int right_top =
        std::max(0, (body_height - gps_offset - kGpsCardHeight) / 2);
    lv_obj_set_pos(compass, 0, std::max(0, (body_height - diameter) / 2));
    lv_obj_set_size(readout, panel_width, gps_offset);
    lv_obj_set_pos(readout, panel_width + 24, right_top);
    lv_obj_set_pos(details, panel_width + 24, right_top + gps_offset);
  } else {
    // 竖屏从仪表开始整体下移，并为读数保留更宽松的间隔。
    const int readout_top = kPortraitTopInset + diameter + kDialReadoutGap;
    lv_obj_set_pos(compass, 0, kPortraitTopInset);
    lv_obj_set_pos(readout, 0, readout_top);
    lv_obj_set_pos(details, 0,
        readout_top + kReadoutHeight + kReadoutGpsGap);
  }
  if (!CreateDial(compass, state, diameter) ||
      !CreateLevel(compass, state, diameter) ||
      !CreateCalibration(compass, state, diameter) ||
      !CreateReadout(readout, state) ||
      !CreateDetails(details, state, panel_width)) {
    lv_obj_delete(root);
    return nullptr;
  }
  state->timer = lv_timer_create(Refresh, 100, state);
  if (state->timer == nullptr) {
    lv_obj_delete(root);
    return nullptr;
  }
  state->started = state->session.Start();
  UpdateStatus(state);
  if (config.set_status_bar_visible) {
    config.set_status_bar_visible(true);
  }
  if (config.set_status_bar_text_color) {
    config.set_status_bar_text_color(colors.on_surface);
  }
  if (config.set_lock_screen_visibility_callback) {
    config.set_lock_screen_visibility_callback([state](bool visible) {
      state->paused = visible;
      state->session.SetPaused(visible);
      if (visible) {
        state->snapshot = app::CompassSnapshot();
        state->heading_ready = false;
        state->haptic_heading_valid = false;
        state->haptic_level_valid = false;
        state->calibration_haptic_armed = false;
      }
    });
  }
  return root;
}

}  // namespace lilygo_box::ui

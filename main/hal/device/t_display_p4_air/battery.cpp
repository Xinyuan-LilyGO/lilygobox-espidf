/*
 * @Description: T-Display-P4-Air AXP517 电池管理实现
 * @Author: LILYGO_L
 * @Date: 2026-09-01 00:00:00
 * @LastEditTime: 2026-09-01 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4_air/device.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace lilygo_box::hal {
namespace {

constexpr int kMinimumBatteryCapacityMah = 1;
constexpr int kMaximumBatteryCapacityMah = UINT16_MAX;
constexpr int kMinimumBatteryEstimateCurrentMa = 5;

/**
 * @brief 根据额定容量和有效健康度估算当前满充可用容量
 * @param rated_capacity_mah 用户设置的电池额定容量
 * @param health_percent AXP517 报告的电池健康度
 * @return 估算的满充可用容量，健康度无效时返回额定容量
 */
int EstimateUsableBatteryCapacityMah(
    int rated_capacity_mah, int health_percent) {
  if (rated_capacity_mah <= 0) {
    return 0;
  }
  if (health_percent <= 0 || health_percent > 100) {
    return rated_capacity_mah;
  }
  return std::max(1, rated_capacity_mah * health_percent / 100);
}

/**
 * @brief 按可用容量、电量百分比和实时电流估算剩余分钟数
 * @param capacity_mah 当前满充可用容量
 * @param charge_percent 当前电量百分比
 * @param current_ma 当前充电或放电电流
 * @param charging true 估算充满时间，false 估算放空时间
 * @return 预计分钟数，电流过小时返回 0
 */
int EstimateBatteryRemainingMinutes(int capacity_mah, int charge_percent,
    int current_ma, bool charging) {
  const int64_t current_magnitude = current_ma < 0
      ? -static_cast<int64_t>(current_ma)
      : static_cast<int64_t>(current_ma);
  if (capacity_mah <= 0 ||
      current_magnitude < kMinimumBatteryEstimateCurrentMa) {
    return 0;
  }
  const int percent = std::clamp(charge_percent, 0, 100);
  const int remaining_percent = charging ? 100 - percent : percent;
  const int64_t minutes = static_cast<int64_t>(capacity_mah) *
      remaining_percent * 60 / (100 * current_magnitude);
  return static_cast<int>(std::clamp<int64_t>(
      minutes, 0, std::numeric_limits<int>::max()));
}

}  // namespace

bool TDisplayP4AirDevice::ReadBatteryManagementStatus(
    BatteryManagementStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = BatteryManagementStatus();

  if (!driver_.IsAxp517Ready() || driver_.chip().axp517 == nullptr) {
    return false;
  }

  auto& axp517 = *driver_.chip().axp517;
  cpp_bus_driver::Axp517::ChipStatus0 chip_status0;
  cpp_bus_driver::Axp517::ChipStatus1 chip_status1;
  if (!axp517.GetChipStatus0(chip_status0) ||
      !axp517.GetChipStatus1(chip_status1)) {
    return false;
  }

  const uint16_t voltage_mv = axp517.GetBatteryVoltage();
  const uint8_t charge_percent = axp517.GetBatteryLevel();
  const uint8_t health_percent = axp517.GetBatteryHealth();
  const float current_ma = axp517.GetBatteryCurrent();
  const int capacity_mah = battery_capacity_mah_.load();
  if (charge_percent > 100) {
    return false;
  }
  const bool health_valid = health_percent > 0 && health_percent <= 100;
  const int usable_capacity_mah = EstimateUsableBatteryCapacityMah(
      capacity_mah, health_percent);

  status->capabilities.capacity = capacity_mah > 0;
  status->capabilities.remaining_time = capacity_mah > 0;
  status->ready = true;
  status->pack_present = chip_status0.battery_present_status;
  status->charging = status->pack_present && chip_status0.vbus_good_indication &&
                     (chip_status1.charging_status ==
                             cpp_bus_driver::Axp517::ChargeStatus::kTrickleCharge ||
                         chip_status1.charging_status ==
                             cpp_bus_driver::Axp517::ChargeStatus::kPrecharge ||
                         chip_status1.charging_status ==
                             cpp_bus_driver::Axp517::ChargeStatus::kConstantCurrent ||
                         chip_status1.charging_status ==
                             cpp_bus_driver::Axp517::ChargeStatus::kConstantVoltage ||
                         chip_status1.charging_status ==
                             cpp_bus_driver::Axp517::ChargeStatus::kChargeDone);
  status->full_charged =
      chip_status1.charging_status ==
          cpp_bus_driver::Axp517::ChargeStatus::kChargeDone ||
      charge_percent == 100;
  status->full_discharged = status->pack_present && charge_percent == 0;
  status->voltage_mv = voltage_mv;
  status->current_ma = static_cast<int>(std::lround(current_ma));
  status->charge_percent = charge_percent;
  status->health_percent = health_valid ? health_percent : 100;
  status->design_capacity_mah = capacity_mah;
  status->full_charge_capacity_mah = usable_capacity_mah;
  status->remaining_capacity_mah =
      usable_capacity_mah * static_cast<int>(charge_percent) / 100;
  if (status->charging) {
    status->time_to_full_min = EstimateBatteryRemainingMinutes(
        usable_capacity_mah, charge_percent, status->current_ma, true);
  } else {
    status->time_to_empty_min = EstimateBatteryRemainingMinutes(
        usable_capacity_mah, charge_percent, status->current_ma, false);
  }
  status->pack_temperature_c = axp517.GetBatteryTemperatureCelsius();
  if (axp517.SetAdcDataSelect(
          cpp_bus_driver::Axp517::AdcData::kChipTemperatureCelsius)) {
    status->chip_temperature_c = axp517.GetChipDieJunctionTemperatureCelsius();
  }
  return true;
}

bool TDisplayP4AirDevice::ReadBatteryLevel(int* percent) {
  if (percent == nullptr || !driver_.IsAxp517Ready() ||
      driver_.chip().axp517 == nullptr) {
    return false;
  }

  cpp_bus_driver::Axp517::ChipStatus0 chip_status;
  if (!driver_.chip().axp517->GetChipStatus0(chip_status) ||
      !chip_status.battery_present_status) {
    return false;
  }
  const uint8_t charge_percent = driver_.chip().axp517->GetBatteryLevel();
  if (charge_percent > 100) {
    return false;
  }
  *percent = charge_percent;
  return true;
}

BatteryCapacityRange TDisplayP4AirDevice::GetBatteryCapacityRange() const {
  return {
      .minimum_mah = kMinimumBatteryCapacityMah,
      .maximum_mah = kMaximumBatteryCapacityMah,
  };
}

bool TDisplayP4AirDevice::SetBatteryCapacityMah(int capacity_mah) {
  const BatteryCapacityRange range = GetBatteryCapacityRange();
  if (capacity_mah < range.minimum_mah ||
      capacity_mah > range.maximum_mah) {
    return false;
  }
  battery_capacity_mah_.store(capacity_mah);
  return true;
}

}  // namespace lilygo_box::hal

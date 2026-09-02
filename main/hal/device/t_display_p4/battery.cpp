/*
 * @Description: T-Display-P4 BQ27220 电池管理实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4/device.h"

#include <cstdint>

namespace lilygo_box::hal {
namespace {

constexpr int kMinimumBatteryCapacityMah = 1;
constexpr int kMaximumBatteryCapacityMah = INT16_MAX;

}  // namespace

bool TDisplayP4Device::ReadBatteryManagementStatus(
    BatteryManagementStatus* status) {
  if (status == nullptr) {
    return false;
  }

  *status = BatteryManagementStatus();
  status->capabilities.average_measurements = true;
  status->capabilities.capacity = true;
  status->capabilities.remaining_time = true;
  status->capabilities.cycle_count = true;

  if (!driver_.IsBq27220Ready()) {
    return false;
  }

  cpp_bus_driver::Bq27220::BatteryStatus battery_status;
  const bool status_read =
      driver_.chip().bq27220->GetBatteryStatus(battery_status);
  const uint16_t voltage_mv = driver_.chip().bq27220->GetVoltage();
  const int16_t current_ma = driver_.chip().bq27220->GetCurrent();
  const uint16_t charge_percent =
      driver_.chip().bq27220->GetStatusOfCharge();
  if (voltage_mv == 0 || voltage_mv == UINT16_MAX) {
    return false;
  }

  status->ready = true;
  status->voltage_mv = voltage_mv;
  status->current_ma = current_ma;
  status->average_current_ma =
      driver_.chip().bq27220->GetAverageCurrent();
  status->average_power_mw = driver_.chip().bq27220->GetAveragePower();
  status->charge_percent =
      charge_percent == UINT16_MAX ? 0 : charge_percent;
  status->health_percent = driver_.chip().bq27220->GetStatusOfHealth();
  status->design_capacity_mah =
      driver_.chip().bq27220->GetDesignCapacity();
  status->remaining_capacity_mah =
      driver_.chip().bq27220->GetRemainingCapacity();
  status->full_charge_capacity_mah =
      driver_.chip().bq27220->GetFullChargeCapacity();
  status->time_to_empty_min = driver_.chip().bq27220->GetTimeToEmpty();
  status->time_to_full_min = driver_.chip().bq27220->GetTimeToFull();
  status->cycle_count = driver_.chip().bq27220->GetCycleCount();
  status->pack_temperature_c =
      driver_.chip().bq27220->GetTemperatureCelsius();
  status->chip_temperature_c =
      driver_.chip().bq27220->GetChipTemperatureCelsius();

  const bool fully_charged =
      status_read && battery_status.flag.full_charged;
  const bool idle_or_charging =
      status_read && !battery_status.flag.discharging;
  status->pack_present = status_read && battery_status.flag.battery_present;
  status->charging =
      current_ma > 0 ||
      (current_ma == 0 && (fully_charged || idle_or_charging));
  status->full_charged = fully_charged;
  status->full_discharged =
      status_read && battery_status.flag.full_discharged;
  return true;
}

bool TDisplayP4Device::ReadBatteryLevel(int* percent) {
  if (percent == nullptr || !driver_.IsBq27220Ready()) {
    return false;
  }

  cpp_bus_driver::Bq27220::BatteryStatus battery_status;
  if (!driver_.chip().bq27220->GetBatteryStatus(battery_status) ||
      !battery_status.flag.battery_present) {
    return false;
  }

  const uint16_t charge_percent =
      driver_.chip().bq27220->GetStatusOfCharge();
  if (charge_percent > 100) {
    return false;
  }

  *percent = charge_percent;
  return true;
}

BatteryCapacityRange TDisplayP4Device::GetBatteryCapacityRange() const {
  return {
      .minimum_mah = kMinimumBatteryCapacityMah,
      .maximum_mah = kMaximumBatteryCapacityMah,
  };
}

bool TDisplayP4Device::SetBatteryCapacityMah(int capacity_mah) {
  const BatteryCapacityRange range = GetBatteryCapacityRange();
  if (capacity_mah < range.minimum_mah ||
      capacity_mah > range.maximum_mah) {
    return false;
  }
  if (driver_.IsBq27220Ready() &&
      !driver_.chip().bq27220->SetBatteryCapacity(
          static_cast<uint16_t>(capacity_mah))) {
    return false;
  }
  battery_capacity_mah_.store(capacity_mah);
  return true;
}

}  // namespace lilygo_box::hal

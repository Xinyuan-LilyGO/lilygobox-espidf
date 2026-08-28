/*
 * @Description: T-Display-P4-Air 蜂窝网络硬件实现
 * @Author: LILYGO_L
 * @Date: 2026-08-28 00:00:00
 * @LastEditTime: 2026-08-28 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/t_display_p4_air/device.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "base/logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/device/common/device_utils.h"

namespace lilygo_box::hal {
namespace {

constexpr uint32_t kCellularTaskStackBytes = 6 * 1024;
constexpr UBaseType_t kCellularTaskPriority = 3;
constexpr uint32_t kCellularCommandTimeoutMs = 2000;
constexpr uint32_t kCellularStatusPollMs = 2000;
constexpr uint32_t kCellularNetworkTimePollMs = 10000;
constexpr uint32_t kCellularSimStartupDelayMs = 3000;
constexpr uint32_t kCellularTaskStopTimeoutMs = 12000;

/**
 * @brief 删除字符串首尾的 ASCII 空白字符
 * @param text 待处理字符串
 * @return 删除空白后的字符串副本
 */
std::string TrimAsciiWhitespace(const std::string& text) {
  size_t first = 0;
  while (first < text.size() &&
         std::isspace(static_cast<unsigned char>(text[first])) != 0) {
    ++first;
  }
  size_t last = text.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(text[last - 1])) != 0) {
    --last;
  }
  return text.substr(first, last - first);
}

/**
 * @brief 从 AT 完整响应中提取指定前缀所在的数据行
 * @param response AT 完整响应
 * @param prefix 数据行前缀
 * @param value 数据行去除前缀后的输出地址
 * @return 找到有效数据行返回 true
 */
bool ExtractAtPrefixedValue(
    const std::string& response, const char* prefix, std::string* value) {
  if (prefix == nullptr || value == nullptr) {
    return false;
  }
  const size_t prefix_position = response.find(prefix);
  if (prefix_position == std::string::npos) {
    return false;
  }
  const size_t value_start = prefix_position + std::strlen(prefix);
  const size_t line_end = response.find_first_of("\r\n", value_start);
  *value = TrimAsciiWhitespace(response.substr(
      value_start, line_end == std::string::npos ? std::string::npos
                                                 : line_end - value_start));
  return !value->empty();
}

/**
 * @brief 从 AT 响应中提取首个纯数字数据行
 * @param response AT 完整响应
 * @param value 数字字符串输出地址
 * @return 找到纯数字数据行返回 true
 */
bool ExtractAtNumericLine(const std::string& response, std::string* value) {
  if (value == nullptr) {
    return false;
  }
  size_t line_start = 0;
  while (line_start < response.size()) {
    const size_t line_end = response.find_first_of("\r\n", line_start);
    const std::string line = TrimAsciiWhitespace(response.substr(
        line_start, line_end == std::string::npos ? std::string::npos
                                                  : line_end - line_start));
    if (!line.empty() &&
        std::all_of(line.begin(), line.end(), [](unsigned char character) {
          return std::isdigit(character) != 0;
        })) {
      *value = line;
      return true;
    }
    if (line_end == std::string::npos) {
      break;
    }
    line_start = line_end + 1;
  }
  return false;
}

/**
 * @brief 解析 +CPIN 响应中的 SIM 卡访问状态
 * @param response AT+CPIN? 完整响应
 * @param state SIM 卡状态输出地址
 * @return 解析成功返回 true
 */
bool ParseCellularSimState(
    const std::string& response, CellularSimState* state) {
  std::string value;
  if (state == nullptr ||
      !ExtractAtPrefixedValue(response, "+CPIN:", &value)) {
    return false;
  }
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.size() - 2);
  }

  if (value == "READY") {
    *state = CellularSimState::kReady;
  } else if (value == "SIM PIN" || value == "SIM PIN2") {
    *state = CellularSimState::kPinRequired;
  } else if (value == "SIM PUK" || value == "SIM PUK2") {
    *state = CellularSimState::kPukRequired;
  } else if (value.rfind("PH-", 0) == 0) {
    *state = CellularSimState::kBlocked;
  } else {
    *state = CellularSimState::kFailure;
    return false;
  }
  return true;
}

/**
 * @brief 获取 SIM 卡状态的日志文本
 * @param state SIM 卡状态
 * @return 静态文本
 */
const char* CellularSimStateLogText(CellularSimState state) {
  switch (state) {
    case CellularSimState::kReady:
      return "ready";
    case CellularSimState::kPinRequired:
      return "PIN required";
    case CellularSimState::kPukRequired:
      return "PUK required";
    case CellularSimState::kBlocked:
      return "blocked";
    case CellularSimState::kFailure:
      return "invalid status";
    case CellularSimState::kUnavailable:
      return "unavailable";
    case CellularSimState::kUnknown:
      return "unknown";
  }
  return "unknown";
}

/**
 * @brief 将 CEREG 数值转换为应用层注册状态
 * @param registration CEREG 注册数值
 * @return 应用层注册状态
 */
CellularRegistrationState ToCellularRegistrationState(int registration) {
  switch (registration) {
    case 0:
      return CellularRegistrationState::kNotRegistered;
    case 1:
      return CellularRegistrationState::kRegisteredHome;
    case 2:
      return CellularRegistrationState::kSearching;
    case 3:
      return CellularRegistrationState::kDenied;
    case 5:
      return CellularRegistrationState::kRegisteredRoaming;
    default:
      return CellularRegistrationState::kUnknown;
  }
}

/**
 * @brief 判断蜂窝网络是否已经完成注册
 * @param state 当前网络注册状态
 * @return 已注册到本地或漫游网络时返回 true
 */
bool IsCellularRegistered(CellularRegistrationState state) {
  return state == CellularRegistrationState::kRegisteredHome ||
         state == CellularRegistrationState::kRegisteredRoaming;
}

/**
 * @brief 解析 +CEREG 响应中的网络注册状态
 * @param response AT+CEREG? 完整响应
 * @param state 注册状态输出地址
 * @return 解析成功返回 true
 */
bool ParseCellularRegistration(
    const std::string& response, CellularRegistrationState* state) {
  std::string value;
  if (state == nullptr ||
      !ExtractAtPrefixedValue(response, "+CEREG:", &value)) {
    return false;
  }
  int reporting_mode = 0;
  int registration = 0;
  const int parsed =
      std::sscanf(value.c_str(), "%d,%d", &reporting_mode, &registration);
  if (parsed == 1) {
    registration = reporting_mode;
  } else if (parsed != 2) {
    return false;
  }
  *state = ToCellularRegistrationState(registration);
  return true;
}

/**
 * @brief 解析 +CSQ 响应并换算 RSSI
 * @param response AT+CSQ 完整响应
 * @param signal_quality CSQ 输出地址
 * @param rssi_dbm RSSI 输出地址
 * @return 解析成功返回 true
 */
bool ParseCellularSignal(
    const std::string& response, int* signal_quality, int* rssi_dbm) {
  std::string value;
  if (signal_quality == nullptr || rssi_dbm == nullptr ||
      !ExtractAtPrefixedValue(response, "+CSQ:", &value)) {
    return false;
  }
  int quality = 99;
  int bit_error_rate = 99;
  if (std::sscanf(value.c_str(), "%d,%d", &quality, &bit_error_rate) != 2) {
    return false;
  }
  *signal_quality = quality;
  *rssi_dbm = quality >= 0 && quality <= 31 ? -113 + quality * 2 : 0;
  return true;
}

/**
 * @brief 解析 +COPS 响应中的运营商字段
 * @param response AT+COPS? 完整响应
 * @param operator_name 运营商字符串输出地址
 * @return 解析成功返回 true
 */
bool ParseCellularOperator(
    const std::string& response, std::string* operator_name) {
  std::string value;
  if (operator_name == nullptr ||
      !ExtractAtPrefixedValue(response, "+COPS:", &value)) {
    return false;
  }
  const size_t first_quote = value.find('"');
  if (first_quote != std::string::npos) {
    const size_t second_quote = value.find('"', first_quote + 1);
    if (second_quote != std::string::npos) {
      *operator_name =
          value.substr(first_quote + 1, second_quote - first_quote - 1);
      return !operator_name->empty();
    }
  }
  const size_t last_comma = value.rfind(',');
  *operator_name = TrimAsciiWhitespace(
      last_comma == std::string::npos ? value : value.substr(last_comma + 1));
  return !operator_name->empty();
}

/**
 * @brief 解析 +CCLK 响应中的网络时间
 * @param response AT+CCLK? 完整响应
 * @param network_time 网络时间字符串输出地址
 * @return 解析并校验成功返回 true
 */
bool ParseCellularNetworkTime(
    const std::string& response, std::string* network_time) {
  std::string value;
  if (network_time == nullptr ||
      !ExtractAtPrefixedValue(response, "+CCLK:", &value)) {
    return false;
  }

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int timezone_quarters = 0;
  char timezone_sign = '\0';
  if (std::sscanf(value.c_str(), "\"%d/%d/%d,%d:%d:%d%c%d\"", &year,
          &month, &day, &hour, &minute, &second, &timezone_sign,
          &timezone_quarters) != 8 ||
      year < 0 || year > 99 || month < 1 || month > 12 || day < 1 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
      second > 59 || (timezone_sign != '+' && timezone_sign != '-') ||
      timezone_quarters < 0 || timezone_quarters > 48) {
    return false;
  }

  constexpr std::array<int, 12> kDaysInMonth = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int maximum_day = kDaysInMonth[static_cast<size_t>(month - 1)];
  const int full_year = 2000 + year;
  if (month == 2 &&
      ((full_year % 4 == 0 && full_year % 100 != 0) || full_year % 400 == 0)) {
    maximum_day = 29;
  }
  if (day > maximum_day) {
    return false;
  }

  const size_t first_quote = value.find('"');
  const size_t second_quote = value.find('"', first_quote + 1);
  if (first_quote == std::string::npos || second_quote == std::string::npos ||
      second_quote <= first_quote + 1) {
    return false;
  }
  *network_time =
      value.substr(first_quote + 1, second_quote - first_quote - 1);
  return true;
}

}  // namespace

bool TDisplayP4AirDevice::SetCellularEnabled(bool enabled) {
  if (!enabled) {
    cellular_.stop_requested.store(true);
    for (uint32_t elapsed_ms = 0;
        elapsed_ms < kCellularTaskStopTimeoutMs && cellular_.task_active.load();
        elapsed_ms += kPowerOffTaskPollMs) {
      vTaskDelay(pdMS_TO_TICKS(kPowerOffTaskPollMs));
    }
    return !cellular_.task_active.load();
  }

  if (cellular_.task_active.load()) {
    return true;
  }
  if (nrf9151_mutex_ == nullptr || cellular_.status_mutex == nullptr ||
      driver_.chip().nrf9151 == nullptr ||
      driver_.bus().nrf9151_uart_bus == nullptr) {
    return false;
  }
  // nRF9151 的系统模式和 UART 为单实例资源，蜂窝模式启动前先结束 GNSS。
  if (gps_running_ && !SetGpsEnabled(false)) {
    return false;
  }

  if (xSemaphoreTake(cellular_.status_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  cellular_.status = CellularStatus();
  cellular_.status.hardware_ready = true;
  cellular_.status.enabled = true;
  xSemaphoreGive(cellular_.status_mutex);

  cellular_.stop_requested.store(false);
  cellular_.task_active.store(true);
  const BaseType_t task_result = xTaskCreate(CellularTaskEntry, "cellular",
      kCellularTaskStackBytes, this, kCellularTaskPriority, nullptr);
  if (task_result != pdPASS) {
    cellular_.task_active.store(false);
    if (xSemaphoreTake(cellular_.status_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      cellular_.status.enabled = false;
      cellular_.status.last_error = ESP_ERR_NO_MEM;
      xSemaphoreGive(cellular_.status_mutex);
    }
    return false;
  }
  return true;
}

bool TDisplayP4AirDevice::ReadCellularStatus(CellularStatus* status) {
  if (status == nullptr) {
    return false;
  }
  if (cellular_.status_mutex == nullptr ||
      xSemaphoreTake(cellular_.status_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return false;
  }
  *status = cellular_.status;
  status->enabled =
      cellular_.task_active.load() && !cellular_.stop_requested.load();
  xSemaphoreGive(cellular_.status_mutex);
  return true;
}

bool TDisplayP4AirDevice::SendCellularCommand(const char* command,
    char* response, size_t response_size, uint32_t timeout_ms) {
  if (response != nullptr && response_size > 0) {
    response[0] = '\0';
  }
  if (command == nullptr || std::strncmp(command, "AT", 2) != 0 ||
      response == nullptr || response_size == 0 || timeout_ms == 0 ||
      !cellular_.task_active.load() || nrf9151_mutex_ == nullptr) {
    return false;
  }
  if (xSemaphoreTake(nrf9151_mutex_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    return false;
  }
  if (!driver_.IsNrf9151Ready() || driver_.chip().nrf9151 == nullptr) {
    xSemaphoreGive(nrf9151_mutex_);
    return false;
  }

  std::string command_response;
  const auto result = driver_.chip().nrf9151->SendCommand(
      command, &command_response, timeout_ms);
  std::snprintf(response, response_size, "%s", command_response.c_str());
  xSemaphoreGive(nrf9151_mutex_);
  return result == cpp_bus_driver::Nrf9151::CommandResult::kOk;
}

void TDisplayP4AirDevice::CellularTaskEntry(void* context) {
  auto* self = static_cast<TDisplayP4AirDevice*>(context);
  if (self != nullptr) {
    self->RunCellularTask();
  }
  vTaskDelete(nullptr);
}

void TDisplayP4AirDevice::RunCellularTask() {
  CellularStatus snapshot;
  snapshot.hardware_ready = driver_.chip().nrf9151 != nullptr &&
                            driver_.bus().nrf9151_uart_bus != nullptr;
  snapshot.enabled = true;

  const auto publish_status = [this, &snapshot]() {
    if (xSemaphoreTake(cellular_.status_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      cellular_.status = snapshot;
      xSemaphoreGive(cellular_.status_mutex);
    }
  };
  using CellularCommandResult = cpp_bus_driver::Nrf9151::CommandResult;
  CellularCommandResult last_command_result = CellularCommandResult::kOk;
  const auto send_command =
      [this, &snapshot, &last_command_result](
          const char* command, std::string* response) {
        last_command_result = driver_.chip().nrf9151->SendCommand(
            command, response, kCellularCommandTimeoutMs);
        snapshot.last_error = static_cast<int>(last_command_result);
        return last_command_result == CellularCommandResult::kOk;
      };
  const auto log_command_error = [&last_command_result](const char* operation,
                                     const char* command,
                                     const std::string& response) {
    std::string compact_response = TrimAsciiWhitespace(response);
    std::replace(
        compact_response.begin(), compact_response.end(), '\r', ' ');
    std::replace(
        compact_response.begin(), compact_response.end(), '\n', ' ');
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
        "Cellular test %s (command: %s, result: %s, response: %s)\n",
        operation, command,
        cpp_bus_driver::Nrf9151::CommandResultToString(last_command_result),
        compact_response.empty() ? "-" : compact_response.c_str());
  };

  const bool modem_locked =
      snapshot.hardware_ready &&
      xSemaphoreTake(nrf9151_mutex_, portMAX_DELAY) == pdTRUE;
  bool initialized = modem_locked;
  if (initialized) {
    snapshot.powered = driver_.InitNrf9151();
    initialized = snapshot.powered && driver_.IsNrf9151Ready();
  }

  if (initialized && !cellular_.stop_requested.load()) {
    std::string response;
    initialized = send_command("AT+CFUN=0", &response);
    if (!initialized) {
      log_command_error(
          "failed to stop the modem before configuration", "AT+CFUN=0",
          response);
    }
    constexpr std::array<const char*, 3> kSystemModeCommands = {{
        "AT%XSYSTEMMODE=1,1,0,0",
        "AT%XSYSTEMMODE=1,0,0,0",
        "AT%XSYSTEMMODE=0,1,0,0",
    }};
    if (initialized) {
      // 优先同时启用 LTE-M 和 NB-IoT；旧固件不支持时依次退回单模式。
      bool mode_selected = false;
      for (const char* command : kSystemModeCommands) {
        if (cellular_.stop_requested.load()) {
          break;
        }
        response.clear();
        if (send_command(command, &response)) {
          mode_selected = true;
          break;
        }
      }
      if (!mode_selected && !cellular_.stop_requested.load()) {
        log_command_error(
            "failed to configure a supported cellular system mode",
            kSystemModeCommands.back(), response);
      }
      initialized = mode_selected;
    }
    if (initialized && !cellular_.stop_requested.load()) {
      response.clear();
      initialized = send_command("AT+CFUN=1", &response);
      if (!initialized) {
        log_command_error(
            "failed to activate the modem", "AT+CFUN=1", response);
      }
    }
    if (initialized && !cellular_.stop_requested.load()) {
      response.clear();
      if (!send_command("AT+CMEE=1", &response)) {
        log_command_error(
            "failed to enable numeric modem errors", "AT+CMEE=1",
            response);
      }
    }
  }

  if (initialized && !cellular_.stop_requested.load()) {
    device_utils::CopyString(snapshot.model, sizeof(snapshot.model),
        driver_.chip().nrf9151->chip_id());
    std::string response;
    const bool imei_command_ok = send_command("AT+CGSN", &response);
    if (imei_command_ok) {
      std::string imei;
      if (ExtractAtNumericLine(response, &imei)) {
        device_utils::CopyString(snapshot.imei, sizeof(snapshot.imei), imei);
      } else {
        log_command_error(
            "received an invalid IMEI response", "AT+CGSN", response);
      }
    } else {
      log_command_error("failed to query IMEI", "AT+CGSN", response);
    }
    std::string firmware;
    if (driver_.chip().nrf9151->GetModemFirmwareVersion(
            &firmware, kCellularCommandTimeoutMs)) {
      device_utils::CopyString(snapshot.firmware, sizeof(snapshot.firmware), firmware);
      snapshot.last_error = 0;
    } else {
      LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
          "Cellular test failed to query modem firmware version\n");
    }
  }
  if (modem_locked) {
    xSemaphoreGive(nrf9151_mutex_);
  }

  if (!initialized) {
    if (!cellular_.stop_requested.load()) {
      LogMessage(LogLevel::kError, __FILE__, __LINE__,
          "Cellular test initialization failed (hardware: %s, power: %s, "
          "result: %s)\n",
          snapshot.hardware_ready ? "ready" : "unavailable",
          snapshot.powered ? "on" : "off",
          cpp_bus_driver::Nrf9151::CommandResultToString(
              last_command_result));
    }
    snapshot.enabled = false;
    publish_status();
    if (snapshot.powered) {
      if (xSemaphoreTake(nrf9151_mutex_, portMAX_DELAY) == pdTRUE) {
        driver_.DeinitNrf9151();
        xSemaphoreGive(nrf9151_mutex_);
      }
      snapshot.powered = false;
      publish_status();
    }
    cellular_.task_active.store(false);
    return;
  }
  snapshot.last_error = 0;
  publish_status();

  // CFUN=1 后 UICC 仍在异步启动，过早查询 CPIN 会短暂返回 ERROR。
  for (uint32_t elapsed_ms = 0;
      elapsed_ms < kCellularSimStartupDelayMs &&
      !cellular_.stop_requested.load();
      elapsed_ms += 100) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  uint32_t network_time_poll_elapsed_ms = kCellularNetworkTimePollMs;
  bool sim_error_reported = false;
  bool registration_error_reported = false;
  bool signal_error_reported = false;
  bool operator_error_reported = false;
  bool network_time_error_reported = false;
  while (!cellular_.stop_requested.load()) {
    if (xSemaphoreTake(nrf9151_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
      std::string response;
      const CellularSimState previous_sim_state = snapshot.sim_state;
      CellularSimState sim_state = CellularSimState::kUnavailable;
      const bool sim_command_ok = send_command("AT+CPIN?", &response);
      const bool sim_status_valid =
          sim_command_ok && ParseCellularSimState(response, &sim_state);
      if (!sim_status_valid && !sim_error_reported) {
        log_command_error(sim_command_ok ? "received an invalid SIM response"
                                         : "failed to query SIM status",
            "AT+CPIN?", response);
      }
      sim_error_reported = !sim_status_valid;
      if (sim_status_valid && sim_state != previous_sim_state) {
        LogMessage(sim_state == CellularSimState::kReady ? LogLevel::kInfo
                                                         : LogLevel::kWarning,
            __FILE__, __LINE__, "Cellular SIM status changed: %s\n",
            CellularSimStateLogText(sim_state));
      }
      snapshot.sim_state = sim_state;

      const bool sim_ready = snapshot.sim_state == CellularSimState::kReady;
      if (!sim_ready) {
        snapshot.registration = CellularRegistrationState::kUnknown;
        snapshot.signal_quality = 99;
        snapshot.rssi_dbm = 0;
        snapshot.operator_name[0] = '\0';
        snapshot.network_time[0] = '\0';
        snapshot.network_time_ready = false;
        network_time_poll_elapsed_ms = kCellularNetworkTimePollMs;
        registration_error_reported = false;
        signal_error_reported = false;
        operator_error_reported = false;
        network_time_error_reported = false;
      }

      if (sim_ready && !cellular_.stop_requested.load()) {
        response.clear();
        CellularRegistrationState registration =
            CellularRegistrationState::kUnknown;
        const bool registration_command_ok =
            send_command("AT+CEREG?", &response);
        const bool registration_status_valid =
            registration_command_ok &&
            ParseCellularRegistration(response, &registration);
        if (!registration_status_valid && !registration_error_reported) {
          log_command_error(
              registration_command_ok
                  ? "received an invalid network registration response"
                  : "failed to query network registration",
              "AT+CEREG?", response);
        }
        registration_error_reported = !registration_status_valid;
        if (registration_status_valid &&
            registration == CellularRegistrationState::kDenied &&
            snapshot.registration != CellularRegistrationState::kDenied) {
          LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
              "Cellular network registration denied\n");
        }
        snapshot.registration = registration;
      }
      if (sim_ready && !cellular_.stop_requested.load()) {
        response.clear();
        int signal_quality = 99;
        int rssi_dbm = 0;
        const bool signal_command_ok = send_command("AT+CSQ", &response);
        const bool signal_status_valid =
            signal_command_ok &&
            ParseCellularSignal(response, &signal_quality, &rssi_dbm);
        if (!signal_status_valid && !signal_error_reported) {
          log_command_error(signal_command_ok
                                ? "received an invalid signal response"
                                : "failed to query signal quality",
              "AT+CSQ", response);
        }
        signal_error_reported = !signal_status_valid;
        snapshot.signal_quality = signal_quality;
        snapshot.rssi_dbm = rssi_dbm;
      }
      if (sim_ready && !cellular_.stop_requested.load()) {
        response.clear();
        std::string operator_name;
        const bool operator_command_ok = send_command("AT+COPS?", &response);
        const bool operator_status_valid =
            operator_command_ok &&
            ParseCellularOperator(response, &operator_name);
        if (!operator_status_valid && !operator_error_reported) {
          log_command_error(operator_command_ok
                                ? "received an invalid operator response"
                                : "failed to query operator",
              "AT+COPS?", response);
        }
        operator_error_reported = !operator_status_valid;
        device_utils::CopyString(snapshot.operator_name, sizeof(snapshot.operator_name),
            operator_name);
      }

      const bool network_registered =
          sim_ready && IsCellularRegistered(snapshot.registration);
      if (!network_registered) {
        snapshot.network_time[0] = '\0';
        snapshot.network_time_ready = false;
        network_time_poll_elapsed_ms = kCellularNetworkTimePollMs;
        network_time_error_reported = false;
      } else if (!cellular_.stop_requested.load() &&
                 network_time_poll_elapsed_ms >=
                     kCellularNetworkTimePollMs) {
        response.clear();
        std::string network_time;
        const bool network_time_command_ok =
            send_command("AT+CCLK?", &response);
        snapshot.network_time_ready = network_time_command_ok &&
                                      ParseCellularNetworkTime(
                                          response, &network_time);
        if (!snapshot.network_time_ready && !network_time_error_reported) {
          log_command_error(
              network_time_command_ok
                  ? "received an invalid network time response"
                  : "failed to query network time",
              "AT+CCLK?", response);
        }
        network_time_error_reported = !snapshot.network_time_ready;
        if (snapshot.network_time_ready) {
          device_utils::CopyString(snapshot.network_time, sizeof(snapshot.network_time),
              network_time);
        } else {
          snapshot.network_time[0] = '\0';
        }
        network_time_poll_elapsed_ms = 0;
      }
      xSemaphoreGive(nrf9151_mutex_);
      publish_status();
    }

    for (uint32_t elapsed_ms = 0;
        elapsed_ms < kCellularStatusPollMs && !cellular_.stop_requested.load();
        elapsed_ms += 100) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    network_time_poll_elapsed_ms =
        std::min(kCellularNetworkTimePollMs,
            network_time_poll_elapsed_ms + kCellularStatusPollMs);
  }

  if (xSemaphoreTake(nrf9151_mutex_, portMAX_DELAY) == pdTRUE) {
    if (driver_.IsNrf9151Ready() && driver_.chip().nrf9151 != nullptr) {
      std::string response;
      send_command("AT+CFUN=0", &response);
    }
    driver_.DeinitNrf9151();
    xSemaphoreGive(nrf9151_mutex_);
  }
  snapshot.enabled = false;
  snapshot.powered = false;
  publish_status();
  cellular_.task_active.store(false);
}

}  // namespace lilygo_box::hal

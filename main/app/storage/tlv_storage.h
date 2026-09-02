/*
 * @Description: NVS 长期配置 TLV 编解码公共接口
 * @Author: LILYGO_L
 * @Date: 2026-07-22 00:00:00
 * @LastEditTime: 2026-09-02 17:51:52
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "esp_err.h"
#include "nvs.h"

namespace lilygo_box::app::storage {

// 每个配置域使用独立编号，发布后的编号只允许保留，禁止改号或复用。
enum class TlvDomain : uint16_t {
  kDisplay = 1,
  kHaptic = 2,
  kMusicSources = 3,
  kRadioProfiles = 4,
  kRadioProfile = 5,
  kSound = 6,
  kWifiPreferences = 7,
  kWifiSavedNetworks = 8,
  kWifiSavedNetwork = 9,
  kPowerState = 10,
  kOtg = 11,
  kKeyboardExpansion = 12,
  kInputMethod = 13,
  kBattery = 14,
};

enum class TlvReadResult : uint8_t {
  kField,
  kEnd,
  kInvalid,
};

enum class TlvLoadResult : uint8_t {
  kLoaded,
  kNotFound,
  kInvalid,
  kError,
};

// 从 NVS 读取 TLV 时使用的临时拥有型缓冲区。
struct TlvBuffer {
  std::unique_ptr<uint8_t[]> data;
  size_t size = 0;
};

// 表示一个已经通过边界检查的 TLV 字段。
class TlvField final {
 public:
  uint16_t tag() const { return tag_; }
  size_t size() const { return size_; }
  const uint8_t* data() const { return data_; }

  bool ReadBool(bool* value) const;
  bool ReadUint8(uint8_t* value) const;
  bool ReadInt8(int8_t* value) const;
  bool ReadUint16(uint16_t* value) const;
  bool ReadUint32(uint32_t* value) const;
  bool ReadInt32(int32_t* value) const;
  bool CopyString(char* output, size_t capacity) const;

 private:
  friend class TlvReader;

  uint16_t tag_ = 0;
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
};

// 将一个配置域编码到调用方提供的固定容量缓冲区。
class TlvWriter final {
 public:
  TlvWriter(TlvDomain domain, uint8_t* output, size_t output_capacity);

  TlvWriter(const TlvWriter&) = delete;
  TlvWriter& operator=(const TlvWriter&) = delete;

  bool WriteBool(uint16_t tag, bool value);
  bool WriteUint8(uint16_t tag, uint8_t value);
  bool WriteInt8(uint16_t tag, int8_t value);
  bool WriteUint16(uint16_t tag, uint16_t value);
  bool WriteUint32(uint16_t tag, uint32_t value);
  bool WriteInt32(uint16_t tag, int32_t value);
  bool WriteString(uint16_t tag, const char* value, size_t value_capacity);
  bool WriteBytes(uint16_t tag, const uint8_t* value, size_t size);

  // 完成容器头和 CRC；调用成功后禁止继续写字段。
  bool Finalize(size_t* encoded_size);

 private:
  bool WriteField(uint16_t tag, const uint8_t* value, size_t size);

  uint8_t* output_ = nullptr;
  size_t capacity_ = 0;
  size_t cursor_ = 0;
  bool valid_ = false;
  bool finalized_ = false;
};

// 验证容器头和 CRC，并按顺序遍历字段。
class TlvReader final {
 public:
  TlvReader(TlvDomain expected_domain, const uint8_t* data, size_t size);

  TlvReader(const TlvReader&) = delete;
  TlvReader& operator=(const TlvReader&) = delete;

  bool IsValid() const { return valid_; }
  TlvReadResult Next(TlvField* field);

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t cursor_ = 0;
  bool valid_ = false;
};

/**
 * @brief 从 NVS 读取并验证一个完整 TLV 容器
 * @param handle 已打开的 NVS 句柄
 * @param key NVS Key
 * @param domain 预期配置域
 * @param maximum_size 允许读取的最大字节数
 * @param buffer 成功时接收数据所有权
 * @param nvs_error 接收底层 NVS 结果，允许为空
 * @return 加载结果
 */
TlvLoadResult LoadTlvBuffer(nvs_handle_t handle, const char* key,
    TlvDomain domain, size_t maximum_size, TlvBuffer* buffer,
    esp_err_t* nvs_error);

}  // namespace lilygo_box::app::storage

/*
 * @Description: NVS 长期配置 TLV 编解码公共实现
 * @Author: LILYGO_L
 * @Date: 2026-07-22 00:00:00
 * @LastEditTime: 2026-09-02 17:51:51
 * @License: GPL 3.0
 */
#include "app/storage/tlv_storage.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>

namespace lilygo_box::app::storage {
namespace {

constexpr uint32_t kContainerMagic = 0x31564C54;
constexpr uint16_t kContainerFormatVersion = 1;
constexpr size_t kContainerHeaderSize = 16;
constexpr size_t kFieldHeaderSize = 4;
constexpr size_t kMaximumFieldSize = UINT16_MAX;

uint16_t ReadLittleEndian16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1]) << 8;
}

uint32_t ReadLittleEndian32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 |
         static_cast<uint32_t>(data[2]) << 16 |
         static_cast<uint32_t>(data[3]) << 24;
}

void WriteLittleEndian16(uint8_t* output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
}

void WriteLittleEndian32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t CalculateCrc32(const uint8_t* data, size_t size) {
  uint32_t crc = UINT32_MAX;
  for (size_t index = 0; index < size; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

}  // namespace

bool TlvField::ReadBool(bool* value) const {
  uint8_t encoded = 0;
  if (value == nullptr || !ReadUint8(&encoded) || encoded > 1) {
    return false;
  }
  *value = encoded != 0;
  return true;
}

bool TlvField::ReadUint8(uint8_t* value) const {
  if (value == nullptr || size_ != sizeof(uint8_t)) {
    return false;
  }
  *value = data_[0];
  return true;
}

bool TlvField::ReadInt8(int8_t* value) const {
  uint8_t encoded = 0;
  if (value == nullptr || !ReadUint8(&encoded)) {
    return false;
  }
  *value = static_cast<int8_t>(encoded);
  return true;
}

bool TlvField::ReadUint16(uint16_t* value) const {
  if (value == nullptr || size_ != sizeof(uint16_t)) {
    return false;
  }
  *value = ReadLittleEndian16(data_);
  return true;
}

bool TlvField::ReadUint32(uint32_t* value) const {
  if (value == nullptr || size_ != sizeof(uint32_t)) {
    return false;
  }
  *value = ReadLittleEndian32(data_);
  return true;
}

bool TlvField::ReadInt32(int32_t* value) const {
  uint32_t encoded = 0;
  if (value == nullptr || !ReadUint32(&encoded)) {
    return false;
  }
  *value = static_cast<int32_t>(encoded);
  return true;
}

bool TlvField::CopyString(char* output, size_t capacity) const {
  if (output == nullptr || capacity == 0 || size_ >= capacity) {
    return false;
  }
  if (size_ > 0) {
    std::memcpy(output, data_, size_);
  }
  output[size_] = '\0';
  std::fill(output + size_ + 1, output + capacity, '\0');
  return true;
}

TlvWriter::TlvWriter(TlvDomain domain, uint8_t* output, size_t output_capacity)
    : output_(output), capacity_(output_capacity) {
  if (output_ == nullptr || capacity_ < kContainerHeaderSize) {
    return;
  }
  std::fill(output_, output_ + kContainerHeaderSize, 0);
  WriteLittleEndian32(output_, kContainerMagic);
  WriteLittleEndian16(output_ + 4, kContainerFormatVersion);
  WriteLittleEndian16(output_ + 6, static_cast<uint16_t>(domain));
  cursor_ = kContainerHeaderSize;
  valid_ = true;
}

bool TlvWriter::WriteBool(uint16_t tag, bool value) {
  return WriteUint8(tag, value ? 1 : 0);
}

bool TlvWriter::WriteUint8(uint16_t tag, uint8_t value) {
  return WriteField(tag, &value, sizeof(value));
}

bool TlvWriter::WriteInt8(uint16_t tag, int8_t value) {
  const uint8_t encoded = static_cast<uint8_t>(value);
  return WriteField(tag, &encoded, sizeof(encoded));
}

bool TlvWriter::WriteUint16(uint16_t tag, uint16_t value) {
  uint8_t encoded[sizeof(value)] = {};
  WriteLittleEndian16(encoded, value);
  return WriteField(tag, encoded, sizeof(encoded));
}

bool TlvWriter::WriteUint32(uint16_t tag, uint32_t value) {
  uint8_t encoded[sizeof(value)] = {};
  WriteLittleEndian32(encoded, value);
  return WriteField(tag, encoded, sizeof(encoded));
}

bool TlvWriter::WriteInt32(uint16_t tag, int32_t value) {
  return WriteUint32(tag, static_cast<uint32_t>(value));
}

bool TlvWriter::WriteString(
    uint16_t tag, const char* value, size_t value_capacity) {
  if (value == nullptr || value_capacity == 0) {
    return false;
  }
  size_t length = 0;
  while (length < value_capacity && value[length] != '\0') {
    ++length;
  }
  if (length == value_capacity) {
    return false;
  }
  return WriteField(tag, reinterpret_cast<const uint8_t*>(value), length);
}

bool TlvWriter::WriteBytes(uint16_t tag, const uint8_t* value, size_t size) {
  return WriteField(tag, value, size);
}

bool TlvWriter::Finalize(size_t* encoded_size) {
  if (!valid_ || finalized_ || encoded_size == nullptr) {
    return false;
  }
  const size_t payload_size = cursor_ - kContainerHeaderSize;
  if (payload_size > UINT32_MAX) {
    return false;
  }
  WriteLittleEndian32(output_ + 8, static_cast<uint32_t>(payload_size));
  WriteLittleEndian32(output_ + 12,
      CalculateCrc32(output_ + kContainerHeaderSize, payload_size));
  finalized_ = true;
  *encoded_size = cursor_;
  return true;
}

bool TlvWriter::WriteField(uint16_t tag, const uint8_t* value, size_t size) {
  if (!valid_ || finalized_ || tag == 0 || size > kMaximumFieldSize ||
      (value == nullptr && size > 0) || cursor_ > capacity_) {
    valid_ = false;
    return false;
  }
  const size_t remaining = capacity_ - cursor_;
  if (remaining < kFieldHeaderSize || size > remaining - kFieldHeaderSize) {
    valid_ = false;
    return false;
  }
  WriteLittleEndian16(output_ + cursor_, tag);
  WriteLittleEndian16(output_ + cursor_ + 2, static_cast<uint16_t>(size));
  cursor_ += kFieldHeaderSize;
  if (size > 0) {
    std::memcpy(output_ + cursor_, value, size);
    cursor_ += size;
  }
  return true;
}

TlvReader::TlvReader(
    TlvDomain expected_domain, const uint8_t* data, size_t size)
    : data_(data), size_(size) {
  if (data_ == nullptr || size_ < kContainerHeaderSize ||
      ReadLittleEndian32(data_) != kContainerMagic ||
      ReadLittleEndian16(data_ + 4) != kContainerFormatVersion ||
      ReadLittleEndian16(data_ + 6) != static_cast<uint16_t>(expected_domain)) {
    return;
  }
  const uint32_t payload_size = ReadLittleEndian32(data_ + 8);
  if (payload_size != size_ - kContainerHeaderSize) {
    return;
  }
  const uint32_t stored_crc = ReadLittleEndian32(data_ + 12);
  const uint32_t calculated_crc =
      CalculateCrc32(data_ + kContainerHeaderSize, payload_size);
  if (stored_crc != calculated_crc) {
    return;
  }
  cursor_ = kContainerHeaderSize;
  valid_ = true;
}

TlvReadResult TlvReader::Next(TlvField* field) {
  if (!valid_ || field == nullptr) {
    return TlvReadResult::kInvalid;
  }
  if (cursor_ == size_) {
    return TlvReadResult::kEnd;
  }
  if (size_ - cursor_ < kFieldHeaderSize) {
    valid_ = false;
    return TlvReadResult::kInvalid;
  }
  const uint16_t tag = ReadLittleEndian16(data_ + cursor_);
  const uint16_t field_size = ReadLittleEndian16(data_ + cursor_ + 2);
  cursor_ += kFieldHeaderSize;
  if (tag == 0 || field_size > size_ - cursor_) {
    valid_ = false;
    return TlvReadResult::kInvalid;
  }
  field->tag_ = tag;
  field->data_ = data_ + cursor_;
  field->size_ = field_size;
  cursor_ += field_size;
  return TlvReadResult::kField;
}

TlvLoadResult LoadTlvBuffer(nvs_handle_t handle, const char* key,
    TlvDomain domain, size_t maximum_size, TlvBuffer* buffer,
    esp_err_t* nvs_error) {
  if (key == nullptr || buffer == nullptr || maximum_size == 0) {
    if (nvs_error != nullptr) {
      *nvs_error = ESP_ERR_INVALID_ARG;
    }
    return TlvLoadResult::kError;
  }
  buffer->data.reset();
  buffer->size = 0;

  size_t size = 0;
  esp_err_t result = nvs_get_blob(handle, key, nullptr, &size);
  if (nvs_error != nullptr) {
    *nvs_error = result;
  }
  if (result == ESP_ERR_NVS_NOT_FOUND) {
    return TlvLoadResult::kNotFound;
  }
  if (result != ESP_OK) {
    return TlvLoadResult::kError;
  }
  if (size < kContainerHeaderSize || size > maximum_size) {
    return TlvLoadResult::kInvalid;
  }

  auto data = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[size]);
  if (data == nullptr) {
    if (nvs_error != nullptr) {
      *nvs_error = ESP_ERR_NO_MEM;
    }
    return TlvLoadResult::kError;
  }
  result = nvs_get_blob(handle, key, data.get(), &size);
  if (nvs_error != nullptr) {
    *nvs_error = result;
  }
  if (result != ESP_OK) {
    return TlvLoadResult::kError;
  }
  const TlvReader reader(domain, data.get(), size);
  if (!reader.IsValid()) {
    return TlvLoadResult::kInvalid;
  }
  buffer->data = std::move(data);
  buffer->size = size;
  return TlvLoadResult::kLoaded;
}

}  // namespace lilygo_box::app::storage

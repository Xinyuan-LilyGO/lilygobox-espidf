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
  /**
   * @brief 读取字段标签
   * @return 字段标签编号
   */
  uint16_t tag() const { return tag_; }

  /**
   * @brief 读取字段数据长度
   * @return 字段数据的字节数
   */
  size_t size() const { return size_; }

  /**
   * @brief 获取字段数据的只读视图
   * @return 指向原始容器缓冲区的指针，不转移所有权
   */
  const uint8_t* data() const { return data_; }

  /**
   * @brief 将字段解码为布尔值，仅接受 0 或 1 编码
   * @param value 解码结果输出地址
   * @return 解码成功返回 true，空指针或字段格式不符时返回 false
   */
  bool ReadBool(bool* value) const;

  /**
   * @brief 将字段解码为8 位无符号整数
   * @param value 解码结果输出地址
   * @return 解码成功返回 true，空指针或字段格式不符时返回 false
   */
  bool ReadUint8(uint8_t* value) const;

  /**
   * @brief 将字段解码为8 位有符号整数
   * @param value 解码结果输出地址
   * @return 解码成功返回 true，空指针或字段格式不符时返回 false
   */
  bool ReadInt8(int8_t* value) const;

  /**
   * @brief 将字段解码为小端编码的 16 位无符号整数
   * @param value 解码结果输出地址
   * @return 解码成功返回 true，空指针或字段格式不符时返回 false
   */
  bool ReadUint16(uint16_t* value) const;

  /**
   * @brief 将字段解码为小端编码的 32 位无符号整数
   * @param value 解码结果输出地址
   * @return 解码成功返回 true，空指针或字段格式不符时返回 false
   */
  bool ReadUint32(uint32_t* value) const;

  /**
   * @brief 将字段解码为小端编码的 32 位有符号整数
   * @param value 解码结果输出地址
   * @return 解码成功返回 true，空指针或字段格式不符时返回 false
   */
  bool ReadInt32(int32_t* value) const;

  /**
   * @brief 复制字段字符串并将输出缓冲区剩余部分补零
   * @param output 字符串输出缓冲区
   * @param capacity 输出容量，须大于字段长度以容纳终止符
   * @return 复制成功返回 true，参数无效或容量不足时返回 false
   */
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
  /**
   * @brief 使用调用方缓冲区初始化指定配置域的 TLV 容器
   * @param domain 配置域编号
   * @param output 编码输出缓冲区，不转移所有权
   * @param output_capacity 输出缓冲区容量，单位为字节
   */
  TlvWriter(TlvDomain domain, uint8_t* output, size_t output_capacity);

  TlvWriter(const TlvWriter&) = delete;
  TlvWriter& operator=(const TlvWriter&) = delete;

  /**
   * @brief 写入布尔字段
   * @param tag 非零字段标签
   * @param value 待编码的值
   * @return 写入成功返回 true，状态、标签无效或容量不足时返回 false
   */
  bool WriteBool(uint16_t tag, bool value);

  /**
   * @brief 写入8 位无符号整数字段
   * @param tag 非零字段标签
   * @param value 待编码的值
   * @return 写入成功返回 true，状态、标签无效或容量不足时返回 false
   */
  bool WriteUint8(uint16_t tag, uint8_t value);

  /**
   * @brief 写入8 位有符号整数字段
   * @param tag 非零字段标签
   * @param value 待编码的值
   * @return 写入成功返回 true，状态、标签无效或容量不足时返回 false
   */
  bool WriteInt8(uint16_t tag, int8_t value);

  /**
   * @brief 写入小端 16 位无符号整数字段
   * @param tag 非零字段标签
   * @param value 待编码的值
   * @return 写入成功返回 true，状态、标签无效或容量不足时返回 false
   */
  bool WriteUint16(uint16_t tag, uint16_t value);

  /**
   * @brief 写入小端 32 位无符号整数字段
   * @param tag 非零字段标签
   * @param value 待编码的值
   * @return 写入成功返回 true，状态、标签无效或容量不足时返回 false
   */
  bool WriteUint32(uint16_t tag, uint32_t value);

  /**
   * @brief 写入小端 32 位有符号整数字段
   * @param tag 非零字段标签
   * @param value 待编码的值
   * @return 写入成功返回 true，状态、标签无效或容量不足时返回 false
   */
  bool WriteInt32(uint16_t tag, int32_t value);

  /**
   * @brief 写入字符串字段，不保存结尾的空字符
   * @param tag 非零字段标签
   * @param value 待编码的字符串
   * @param value_capacity 输入缓冲区容量，范围内须存在字符串终止符
   * @return 写入成功返回 true，参数、状态无效或容量不足时返回 false
   */
  bool WriteString(uint16_t tag, const char* value, size_t value_capacity);

  /**
   * @brief 写入原始字节字段
   * @param tag 非零字段标签
   * @param value 数据地址，长度为零时允许为空
   * @param size 数据字节数
   * @return 写入成功返回 true，参数、状态无效或容量不足时返回 false
   */
  bool WriteBytes(uint16_t tag, const uint8_t* value, size_t size);

  /**
   * @brief 完成容器长度和 CRC，成功后禁止继续写入字段
   * @param encoded_size 完整容器字节数输出地址
   * @return 完成成功返回 true，空指针、无效状态或重复完成时返回 false
   */
  bool Finalize(size_t* encoded_size);

 private:
  /**
   * @brief 校验并追加字段头和数据，失败时将编码器标记为无效
   * @param tag 非零字段标签
   * @param value 数据地址，长度为零时允许为空
   * @param size 数据字节数
   * @return 写入成功返回 true，否则返回 false
   */
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
  /**
   * @brief 验证 TLV 容器头、配置域与 CRC 并初始化遍历位置
   * @param expected_domain 预期配置域编号
   * @param data 容器缓冲区，须在读取及使用字段视图期间保持有效
   * @param size 容器字节数
   */
  TlvReader(TlvDomain expected_domain, const uint8_t* data, size_t size);

  TlvReader(const TlvReader&) = delete;
  TlvReader& operator=(const TlvReader&) = delete;

  /**
   * @brief 检查容器验证及字段遍历状态
   * @return 尚未检测到格式错误时返回 true，否则返回 false
   */
  bool IsValid() const { return valid_; }

  /**
   * @brief 读取下一字段并推进遍历位置
   * @param field 字段视图输出地址，不持有原始数据所有权
   * @return 读到字段返回 kField，结束返回 kEnd，参数或格式错误返回 kInvalid
   */
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

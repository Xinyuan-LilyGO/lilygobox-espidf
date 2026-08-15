/*
 * @Description: NFC 读卡器发现控制与卡片状态 Provider 接口
 * @Author: LILYGO_L
 * @Date: 2026-07-30 00:00:00
 * @LastEditTime: 2026-07-30 18:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace lilygo_box::hal {

inline constexpr size_t kNfcIdentifierCapacity = 16;
inline constexpr size_t kNfcContentCapacity = 192;
inline constexpr size_t kNfcLanguageCapacity = 8;

// 应用层可识别的 NFC 轮询技术。
enum class NfcTechnology : uint8_t {
  kUnknown,
  kTypeA,
  kTypeB,
  kTypeF,
  kTypeV,
  kSt25Tb,
};

// NFC Forum 标签类型或设备类别。
enum class NfcTagType : uint8_t {
  kUnknown,
  kType1,
  kType2,
  kType3,
  kType4,
  kType5,
  kPeerToPeer,
  kProprietary,
};

// 已激活标签使用的 RFAL 接口。
enum class NfcRfInterface : uint8_t {
  kUnknown,
  kRf,
  kIsoDep,
  kNfcDep,
};

// 从首个可读 NDEF 记录中提取的内容类型。
enum class NfcNdefRecordType : uint8_t {
  kNone,
  kText,
  kUri,
  kUnsupported,
};

// NFC 发现任务和最近一张卡片的快照。
struct NfcStatus {
  // ST25R3916 及 RFAL 协议栈是否已经完成初始化。
  bool hardware_ready = false;
  // 后台轮询任务是否正在运行。
  bool polling = false;
  // 天线区域内是否存在已经激活的卡片。
  bool card_present = false;
  // 最近一次识别到的 NFC 技术。
  NfcTechnology technology = NfcTechnology::kUnknown;
  // NFC Forum 标签类型或设备类别。
  NfcTagType tag_type = NfcTagType::kUnknown;
  // 标签激活后使用的 RF 接口。
  NfcRfInterface rf_interface = NfcRfInterface::kUnknown;
  // 最近一次识别到的 NFC 标识符。
  uint8_t identifier[kNfcIdentifierCapacity] = {};
  // identifier 中的有效字节数。
  size_t identifier_length = 0;
  // NFC-A 的 ATQA/SENS_RES，其他技术保持为零。
  uint16_t atqa = 0;
  // NFC-A 的 SAK/SEL_RES，其他技术保持为零。
  uint8_t sak = 0;
  // NFC-B 的应用族标识符 AFI，其他技术保持为零。
  uint8_t afi = 0;
  // NFC-F 的请求数据或系统码，其他技术保持为零。
  uint16_t system_code = 0;
  // NFC-V 的数据存储格式标识符 DSFID，其他技术保持为零。
  uint8_t dsfid = 0;
  // NFC-V UID 中的 IC 制造商代码，无法识别时为零。
  uint8_t manufacturer_code = 0;
  // ST25TB 会话芯片标识符，其他技术保持为零。
  uint8_t chip_id = 0;
  // Type 2 Capability Container 报告的用户数据区容量。
  size_t memory_capacity_bytes = 0;
  // 标签是否包含 NFC Forum NDEF Capability Container。
  bool ndef_formatted = false;
  // 标签数据区是否包含 NDEF Message TLV。
  bool ndef_present = false;
  // 标签 Capability Container 是否声明为只读。
  bool read_only = false;
  // NDEF 消息声明的总字节数。
  size_t ndef_message_length = 0;
  // 首条 NDEF 记录的类型。
  NfcNdefRecordType ndef_record_type = NfcNdefRecordType::kNone;
  // 文本记录的语言代码。
  char ndef_language[kNfcLanguageCapacity] = {};
  // 适合在 CIT 页面显示的首条文本或 URI 内容。
  char content[kNfcContentCapacity] = {};
  // 标签容量或首条 NDEF 记录超过读取和显示上限。
  bool content_truncated = false;
  // 标签内容读取或解析错误，零表示正常。
  int content_error = 0;
  // 本次启动轮询后检测到的新卡片次数。
  uint32_t detection_count = 0;
  // 最近一次 RFAL 或平台错误码，0 表示无错误。
  int last_error = 0;
};

class NfcProvider {
 public:
  virtual ~NfcProvider() = default;

  /**
   * @brief 启动或停止 NFC-A、B、F、V 与 ST25TB 后台发现
   * @param enabled true 启动轮询，false 停止轮询并关闭射频场
   * @return 请求成功接受或目标状态已经满足返回 true
   */
  virtual bool SetNfcPollingEnabled(bool enabled) = 0;

  /**
   * @brief 非阻塞读取 NFC 轮询和最近卡片状态
   * @param status NFC 状态输出地址
   * @return 状态读取成功返回 true，否则返回 false
   */
  virtual bool ReadNfcStatus(NfcStatus* status) = 0;
};

}  // namespace lilygo_box::hal

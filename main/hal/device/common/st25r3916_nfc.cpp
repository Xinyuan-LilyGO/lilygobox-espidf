/*
 * @Description: ST25R3916 NFC 发现与卡片状态公共辅助实现
 * @Author: LILYGO_L
 * @Date: 2026-08-21 00:00:00
 * @LastEditTime: 2026-08-21 00:00:00
 * @License: GPL 3.0
 */
#include "hal/device/common/st25r3916_nfc.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

extern "C" {
#include "rfal_t2t.h"
}

namespace lilygo_box::hal::st25r3916_nfc {
namespace {

constexpr uint8_t kType2NdefMagic = 0xE1;
constexpr uint8_t kType2NullTlv = 0x00;
constexpr uint8_t kType2NdefMessageTlv = 0x03;
constexpr uint8_t kType2TerminatorTlv = 0xFE;
constexpr size_t kType2HeaderBytes = 16;
constexpr size_t kType2MaximumReadBytes = 256;

NfcRfInterface ToNfcRfInterface(rfalNfcRfInterface rf_interface) {
  switch (rf_interface) {
    case RFAL_NFC_INTERFACE_RF:
      return NfcRfInterface::kRf;
    case RFAL_NFC_INTERFACE_ISODEP:
      return NfcRfInterface::kIsoDep;
    case RFAL_NFC_INTERFACE_NFCDEP:
      return NfcRfInterface::kNfcDep;
    default:
      return NfcRfInterface::kUnknown;
  }
}

bool NfcBytesEqualText(
    const uint8_t* data, size_t length, const char* text) {
  return data != nullptr && text != nullptr && std::strlen(text) == length &&
         std::memcmp(data, text, length) == 0;
}

void AppendNfcDisplayText(const uint8_t* data, size_t length, char* output,
    size_t output_size, size_t* used, bool* truncated) {
  if (data == nullptr || output == nullptr || output_size == 0 ||
      used == nullptr || truncated == nullptr) {
    return;
  }
  for (size_t index = 0; index < length; ++index) {
    if (*used + 1 >= output_size) {
      *truncated = true;
      break;
    }
    const uint8_t value = data[index];
    const bool whitespace = value == '\r' || value == '\n' || value == '\t';
    const bool control_character = value < 0x20 || value == 0x7F;
    output[(*used)++] = whitespace
                            ? ' '
                            : (control_character ? '.'
                                                 : static_cast<char>(value));
  }
  output[*used] = '\0';
}

void AppendNfcContentText(
    const char* text, NfcStatus* status, size_t* used) {
  if (text == nullptr || status == nullptr || used == nullptr) {
    return;
  }
  AppendNfcDisplayText(reinterpret_cast<const uint8_t*>(text),
      std::strlen(text), status->content, sizeof(status->content), used,
      &status->content_truncated);
}

void CopyNfcDisplayText(const uint8_t* data, size_t length, char* output,
    size_t output_size, bool* truncated) {
  if (output == nullptr || output_size == 0 || truncated == nullptr) {
    return;
  }
  output[0] = '\0';
  size_t used = 0;
  AppendNfcDisplayText(
      data, length, output, output_size, &used, truncated);
}

const char* NfcNdefUriPrefix(uint8_t code) {
  switch (code) {
    case 1:
      return "http://www.";
    case 2:
      return "https://www.";
    case 3:
      return "http://";
    case 4:
      return "https://";
    case 5:
      return "tel:";
    case 6:
      return "mailto:";
    default:
      return "";
  }
}

void SetNfcNdefParseFailure(bool complete, NfcStatus* status) {
  if (status == nullptr) {
    return;
  }
  if (complete) {
    status->content_error = RFAL_ERR_PROTO;
  } else {
    status->content_truncated = true;
  }
}

void ParseFirstNfcNdefRecord(const uint8_t* message, size_t message_length,
    bool complete, NfcStatus* status) {
  if (message == nullptr || status == nullptr || message_length < 3) {
    SetNfcNdefParseFailure(complete, status);
    return;
  }

  size_t offset = 0;
  const uint8_t header = message[offset++];
  const bool chunked = (header & 0x20) != 0;
  const bool short_record = (header & 0x10) != 0;
  const bool id_present = (header & 0x08) != 0;
  const uint8_t tnf = header & 0x07;
  const size_t type_length = message[offset++];

  size_t payload_length = 0;
  if (short_record) {
    payload_length = message[offset++];
  } else {
    if (message_length - offset < 4) {
      SetNfcNdefParseFailure(complete, status);
      return;
    }
    payload_length = static_cast<uint32_t>(message[offset]) << 24U |
                     static_cast<uint32_t>(message[offset + 1]) << 16U |
                     static_cast<uint32_t>(message[offset + 2]) << 8U |
                     message[offset + 3];
    offset += 4;
  }

  size_t id_length = 0;
  if (id_present) {
    if (offset >= message_length) {
      SetNfcNdefParseFailure(complete, status);
      return;
    }
    id_length = message[offset++];
  }
  const size_t remaining = message_length - offset;
  if (type_length > remaining || id_length > remaining - type_length ||
      payload_length > remaining - type_length - id_length) {
    SetNfcNdefParseFailure(complete, status);
    return;
  }

  const uint8_t* type = message + offset;
  offset += type_length + id_length;
  const uint8_t* payload = message + offset;
  if (chunked) {
    status->ndef_record_type = NfcNdefRecordType::kUnsupported;
    return;
  }

  if (tnf == 0x01 && NfcBytesEqualText(type, type_length, "T")) {
    status->ndef_record_type = NfcNdefRecordType::kText;
    if (payload_length == 0) {
      return;
    }
    const uint8_t text_status = payload[0];
    const size_t language_length = text_status & 0x3F;
    if (language_length + 1 > payload_length) {
      SetNfcNdefParseFailure(complete, status);
      return;
    }
    bool language_truncated = false;
    CopyNfcDisplayText(payload + 1, language_length, status->ndef_language,
        sizeof(status->ndef_language), &language_truncated);
    status->content_truncated |= language_truncated;
    const uint8_t* text = payload + language_length + 1;
    const size_t text_length = payload_length - language_length - 1;
    if ((text_status & 0x80) != 0) {
      std::snprintf(status->content, sizeof(status->content),
          "UTF-16 text (%u bytes)", static_cast<unsigned>(text_length));
      return;
    }
    CopyNfcDisplayText(text, text_length, status->content,
        sizeof(status->content), &status->content_truncated);
    return;
  }

  if (tnf == 0x01 && NfcBytesEqualText(type, type_length, "U")) {
    status->ndef_record_type = NfcNdefRecordType::kUri;
    if (payload_length == 0) {
      return;
    }
    size_t used = 0;
    AppendNfcContentText(NfcNdefUriPrefix(payload[0]), status, &used);
    AppendNfcDisplayText(payload + 1, payload_length - 1, status->content,
        sizeof(status->content), &used, &status->content_truncated);
    return;
  }

  if (tnf == 0x03) {
    status->ndef_record_type = NfcNdefRecordType::kUri;
    CopyNfcDisplayText(type, type_length, status->content,
        sizeof(status->content), &status->content_truncated);
    return;
  }
  status->ndef_record_type = NfcNdefRecordType::kUnsupported;
}

void ParseNfcType2Tlvs(
    const uint8_t* data, size_t data_length, NfcStatus* status) {
  if (data == nullptr || status == nullptr) {
    return;
  }
  size_t offset = 0;
  while (offset < data_length) {
    const uint8_t type = data[offset++];
    if (type == kType2NullTlv) {
      continue;
    }
    if (type == kType2TerminatorTlv) {
      return;
    }
    if (offset >= data_length) {
      status->content_truncated = true;
      return;
    }

    size_t value_length = data[offset++];
    if (value_length == 0xFF) {
      if (data_length - offset < 2) {
        status->content_truncated = true;
        return;
      }
      value_length =
          static_cast<size_t>(data[offset]) << 8U | data[offset + 1];
      offset += 2;
    }
    const size_t available_length =
        std::min(value_length, data_length - offset);
    if (type == kType2NdefMessageTlv) {
      status->ndef_present = true;
      status->ndef_message_length = value_length;
      status->content_truncated |= available_length < value_length;
      ParseFirstNfcNdefRecord(data + offset, available_length,
          available_length == value_length, status);
      return;
    }
    if (available_length < value_length) {
      status->content_truncated = true;
      return;
    }
    offset += value_length;
  }
}

void ReadNfcType2Content(NfcStatus* status) {
  if (status == nullptr) {
    return;
  }
  std::array<uint8_t, kType2MaximumReadBytes> memory = {};
  uint16_t received_length = 0;
  ReturnCode result = rfalT2TPollerRead(
      0, memory.data(), RFAL_T2T_READ_DATA_LEN, &received_length);
  if (result != RFAL_ERR_NONE || received_length < kType2HeaderBytes) {
    status->content_error = result == RFAL_ERR_NONE ? RFAL_ERR_PROTO : result;
    return;
  }

  const uint8_t* capability = memory.data() + 12;
  status->ndef_formatted = capability[0] == kType2NdefMagic;
  if (!status->ndef_formatted) {
    return;
  }
  status->memory_capacity_bytes = static_cast<size_t>(capability[2]) * 8;
  status->read_only = (capability[3] & 0x0F) == 0x0F;
  const size_t total_memory_bytes =
      kType2HeaderBytes + status->memory_capacity_bytes;
  const size_t read_limit = std::min(total_memory_bytes, memory.size());
  status->content_truncated = total_memory_bytes > read_limit;

  size_t bytes_read = kType2HeaderBytes;
  while (bytes_read < read_limit) {
    const size_t page = bytes_read / RFAL_T2T_BLOCK_LEN;
    std::array<uint8_t, RFAL_T2T_READ_DATA_LEN> block = {};
    received_length = 0;
    result = rfalT2TPollerRead(static_cast<uint8_t>(page), block.data(),
        static_cast<uint16_t>(block.size()), &received_length);
    if (result != RFAL_ERR_NONE || received_length < block.size()) {
      status->content_error =
          result == RFAL_ERR_NONE ? RFAL_ERR_PROTO : result;
      status->content_truncated = true;
      break;
    }
    const size_t copy_length =
        std::min(block.size(), read_limit - bytes_read);
    std::memcpy(memory.data() + bytes_read, block.data(), copy_length);
    bytes_read += copy_length;
  }

  if (bytes_read > kType2HeaderBytes) {
    ParseNfcType2Tlvs(memory.data() + kType2HeaderBytes,
        bytes_read - kType2HeaderBytes, status);
  }
}

}  // namespace

NfcTechnology ToNfcTechnology(rfalNfcDevType type) {
  switch (type) {
    case RFAL_NFC_LISTEN_TYPE_NFCA:
      return NfcTechnology::kTypeA;
    case RFAL_NFC_LISTEN_TYPE_NFCB:
      return NfcTechnology::kTypeB;
    case RFAL_NFC_LISTEN_TYPE_NFCF:
      return NfcTechnology::kTypeF;
    case RFAL_NFC_LISTEN_TYPE_NFCV:
      return NfcTechnology::kTypeV;
    case RFAL_NFC_LISTEN_TYPE_ST25TB:
      return NfcTechnology::kSt25Tb;
    default:
      return NfcTechnology::kUnknown;
  }
}

void PopulateNfcTagDetails(const rfalNfcDevice& device, NfcStatus* status) {
  if (status == nullptr) {
    return;
  }
  status->technology = ToNfcTechnology(device.type);
  status->rf_interface = ToNfcRfInterface(device.rfInterface);

  switch (device.type) {
    case RFAL_NFC_LISTEN_TYPE_NFCA:
      status->atqa =
          static_cast<uint16_t>(device.dev.nfca.sensRes.platformInfo) << 8U |
          device.dev.nfca.sensRes.anticollisionInfo;
      status->sak = device.dev.nfca.selRes.sak;
      switch (device.dev.nfca.type) {
        case RFAL_NFCA_T1T:
          status->tag_type = NfcTagType::kType1;
          break;
        case RFAL_NFCA_T2T:
          status->tag_type = NfcTagType::kType2;
          ReadNfcType2Content(status);
          break;
        case RFAL_NFCA_T4T:
        case RFAL_NFCA_T4T_NFCDEP:
          status->tag_type = NfcTagType::kType4;
          break;
        case RFAL_NFCA_NFCDEP:
          status->tag_type = NfcTagType::kPeerToPeer;
          break;
        default:
          break;
      }
      break;
    case RFAL_NFC_LISTEN_TYPE_NFCB:
      status->tag_type = rfalNfcbIsIsoDepSupported(&device.dev.nfcb)
          ? NfcTagType::kType4
          : NfcTagType::kUnknown;
      status->afi = device.dev.nfcb.sensbRes.appData.AFI;
      break;
    case RFAL_NFC_LISTEN_TYPE_NFCF:
      status->tag_type = rfalNfcfIsNfcDepSupported(&device.dev.nfcf)
          ? NfcTagType::kPeerToPeer
          : NfcTagType::kType3;
      status->system_code =
          static_cast<uint16_t>(device.dev.nfcf.sensfRes.RD[0]) << 8U |
          device.dev.nfcf.sensfRes.RD[1];
      break;
    case RFAL_NFC_LISTEN_TYPE_NFCV:
      status->tag_type = NfcTagType::kType5;
      status->dsfid = device.dev.nfcv.InvRes.DSFID;
      if (device.dev.nfcv.InvRes.UID[RFAL_NFCV_UID_LEN - 1U] == 0xE0) {
        status->manufacturer_code =
            device.dev.nfcv.InvRes.UID[RFAL_NFCV_UID_LEN - 2U];
      }
      break;
    case RFAL_NFC_LISTEN_TYPE_ST25TB:
      status->tag_type = NfcTagType::kProprietary;
      status->chip_id = device.dev.st25tb.chipID;
      break;
    default:
      status->tag_type = NfcTagType::kUnknown;
      break;
  }
}

rfalNfcDiscoverParam CreateNfcDiscoveryParameters() {
  rfalNfcDiscoverParam parameters = {};
  parameters.compMode = RFAL_COMPLIANCE_MODE_NFC;
  parameters.techs2Find = RFAL_NFC_POLL_TECH_A | RFAL_NFC_POLL_TECH_B |
                          RFAL_NFC_POLL_TECH_F | RFAL_NFC_POLL_TECH_V |
                          RFAL_NFC_POLL_TECH_ST25TB;
  parameters.totalDuration = kDiscoveryDurationMs;
  parameters.devLimit = 1;
  parameters.maxBR = RFAL_BR_848;
  parameters.nfcfBR = RFAL_BR_212;
  parameters.ap2pBR = RFAL_BR_424;
  parameters.notifyCb = nullptr;
  parameters.wakeupEnabled = false;
  parameters.wakeupConfigDefault = true;
  return parameters;
}

}  // namespace lilygo_box::hal::st25r3916_nfc

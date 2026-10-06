/*
 * @Description: 使用 mbedTLS 生成 ECDSA P-256 自签证书，并在应用 NVS
 * 保存设备身份。
 * @Author: LILYGO_L
 * @Date: 2026-10-06 16:00:00
 * @LastEditTime: 2026-10-06 16:00:00
 * @License: GPL 3.0
 */
#include "app/connection_tls.h"

#include <cstdint>
#include <cstring>

#include "app/storage/storage_internal.h"
#include "base/logger.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"

namespace lilygo_box::app {
namespace {

// 密钥和证书作为单个版本化记录写入，防止断电后出现不匹配的两份数据。
struct TlsIdentity {
  uint32_t version = 1;
  unsigned char key[512] = {};
  unsigned char certificate[1024] = {};
};
TlsIdentity g_identity;
bool g_loaded = false;

/**
 * @brief 生成或校验 P-256 密钥和证书，所有密码学操作使用 mbedTLS
 * @param generate 是否首次生成身份；false 表示校验 NVS 中的现有身份
 * @return 生成或校验成功返回 true
 */
bool PrepareIdentity(bool generate) {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  mbedtls_pk_context key;
  mbedtls_x509_crt parsed;
  mbedtls_x509write_cert writer;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&random);
  mbedtls_pk_init(&key);
  mbedtls_x509_crt_init(&parsed);
  mbedtls_x509write_crt_init(&writer);
  const bool success = [&]() {
    constexpr unsigned char purpose[] = "LilygoBox device TLS identity";
    if (mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy, purpose,
            sizeof(purpose) - 1) != 0)
      return false;
    if (!generate) {
      if (g_identity.version != 1 ||
          std::memchr(g_identity.key, 0, sizeof(g_identity.key)) == nullptr ||
          std::memchr(g_identity.certificate, 0,
              sizeof(g_identity.certificate)) == nullptr)
        return false;
      return mbedtls_pk_parse_key(&key, g_identity.key,
                 std::strlen(reinterpret_cast<char*>(g_identity.key)) + 1,
                 nullptr, 0, mbedtls_ctr_drbg_random, &random) == 0 &&
             mbedtls_x509_crt_parse(&parsed, g_identity.certificate,
                 std::strlen(reinterpret_cast<char*>(g_identity.certificate)) +
                     1) == 0 &&
             mbedtls_pk_check_pair(
                 &parsed.pk, &key, mbedtls_ctr_drbg_random, &random) == 0;
    }
    if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) !=
            0 ||
        mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
            mbedtls_ctr_drbg_random, &random) != 0)
      return false;
    unsigned char serial_bytes[16];
    if (mbedtls_ctr_drbg_random(&random, serial_bytes, sizeof(serial_bytes)) !=
        0)
      return false;
    serial_bytes[0] = (serial_bytes[0] & 0x7f) | 1;
    mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&writer, &key);
    mbedtls_x509write_crt_set_issuer_key(&writer, &key);
    // 身份由客户端固定证书验证，不依赖互联网时间或局域网 IP 是否改变。
    if (mbedtls_x509write_crt_set_serial_raw(
            &writer, serial_bytes, sizeof(serial_bytes)) != 0 ||
        mbedtls_x509write_crt_set_subject_name(&writer, "CN=LilygoBox") != 0 ||
        mbedtls_x509write_crt_set_issuer_name(&writer, "CN=LilygoBox") != 0 ||
        mbedtls_x509write_crt_set_validity(
            &writer, "20200101000000", "20991231235959") != 0 ||
        mbedtls_x509write_crt_set_basic_constraints(&writer, 0, -1) != 0 ||
        mbedtls_x509write_crt_set_key_usage(
            &writer, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) != 0 ||
        mbedtls_pk_write_key_pem(
            &key, g_identity.key, sizeof(g_identity.key)) != 0 ||
        mbedtls_x509write_crt_pem(&writer, g_identity.certificate,
            sizeof(g_identity.certificate), mbedtls_ctr_drbg_random,
            &random) != 0)
      return false;
    return true;
  }();
  mbedtls_x509write_crt_free(&writer);
  mbedtls_x509_crt_free(&parsed);
  mbedtls_pk_free(&key);
  mbedtls_ctr_drbg_free(&random);
  mbedtls_entropy_free(&entropy);
  return success;
}

/**
 * @brief 读取独立 TLS 身份记录，仅在记录不存在时生成并立即提交
 * @return 证书可用且已经保存返回 true
 */
bool LoadIdentity() {
  if (g_loaded) return true;
  if (!EnsureApplicationNvsInitialized()) return false;
  nvs_handle_t handle;
  esp_err_t result = OpenApplicationNvs("app_tls", NVS_READWRITE, &handle);
  if (result != ESP_OK) return false;
  size_t size = sizeof(g_identity);
  result = nvs_get_blob(handle, "identity_v1", &g_identity, &size);
  const bool missing = result == ESP_ERR_NVS_NOT_FOUND;
  if (missing) g_identity = {};
  if ((missing || (result == ESP_OK && size == sizeof(g_identity))) &&
      PrepareIdentity(missing)) {
    if (missing) {
      result =
          nvs_set_blob(handle, "identity_v1", &g_identity, sizeof(g_identity));
      if (result == ESP_OK) result = nvs_commit(handle);
    }
    g_loaded = result == ESP_OK;
  }
  nvs_close(handle);
  if (!g_loaded) {
    mbedtls_platform_zeroize(&g_identity, sizeof(g_identity));
    LogMessage(LogLevel::kError, __FILE__, __LINE__,
        "TLS identity unavailable; encrypted service not started\n");
  } else {
    LogMessage(LogLevel::kInfo, __FILE__, __LINE__, "Device TLS identity %s\n",
        missing ? "created" : "loaded");
  }
  return g_loaded;
}

}  // namespace

/**
 * @brief 加载持久设备身份并向 HTTPS 服务提供稳定的证书与密钥缓冲区
 * @param config 待填充的 HTTPS 配置
 * @return 证书可用返回 true，否则禁止启动服务
 */
bool ConfigureConnectionTls(httpd_ssl_config_t* config) {
  if (config == nullptr || !LoadIdentity()) return false;
  config->servercert = g_identity.certificate;
  config->servercert_len =
      std::strlen(reinterpret_cast<char*>(g_identity.certificate)) + 1;
  config->prvtkey_pem = g_identity.key;
  config->prvtkey_len =
      std::strlen(reinterpret_cast<char*>(g_identity.key)) + 1;
  config->transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
  return true;
}

}  // namespace lilygo_box::app

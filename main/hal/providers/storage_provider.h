/*
 * @Description: Storage provider
 * @Author: LILYGO_L
 * @Date: 2026-07-09 00:00:00
 * @LastEditTime: 2026-07-09 00:00:00
 * @License: GPL 3.0
 */
#pragma once

namespace lilygo_box::hal {

class StorageProvider {
 public:
  virtual ~StorageProvider() = default;

  /**
   * @brief 确保 SD 卡已经挂载到文件系统
   * @return 挂载成功或已经挂载返回 true，否则返回 false
   */
  virtual bool EnsureSdCardMounted() = 0;

  /**
   * @brief 卸载 SD 卡文件系统并释放总线资源
   * @return 卸载成功或当前未挂载返回 true，否则返回 false
   */
  virtual bool UnmountSdCard() = 0;

  /**
   * @brief 判断 SD 卡文件系统是否已经挂载
   * @return 已挂载返回 true，否则返回 false
   */
  virtual bool IsSdCardMounted() const = 0;

  /**
   * @brief 获取 SD 卡挂载路径
   * @return SD 卡挂载路径字符串
   */
  virtual const char* SdCardBasePath() const = 0;
};

}  // namespace lilygo_box::hal

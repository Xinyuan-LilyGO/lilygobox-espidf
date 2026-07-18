/*
 * @Description: LittleFS 内部存储挂载与容量查询接口
 * @Author: LILYGO_L
 * @Date: 2026-07-17 15:20:00
 * @LastEditTime: 2026-07-18 00:00:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

namespace lilygo_box::app {

/**
 * @brief 挂载应用使用的 LittleFS 内部存储分区
 * @return 挂载成功或分区已经挂载时返回 true
 */
bool InitLittleFsStorage();

/**
 * @brief 查询 LittleFS 内部存储是否已经挂载
 * @return 内部存储可访问时返回 true
 */
bool IsLittleFsStorageMounted();

/**
 * @brief 获取 LittleFS 内部存储的 VFS 根路径
 * @return 生命周期覆盖当前进程的挂载路径
 */
const char* LittleFsStorageBasePath();

/**
 * @brief 查询 LittleFS 内部存储的总容量和已用容量
 * @param total_bytes 总容量输出
 * @param used_bytes 已用容量输出
 * @return 容量信息读取成功时返回 true
 */
bool GetLittleFsStorageInfo(size_t* total_bytes, size_t* used_bytes);

/**
 * @brief 卸载并完整擦除全部 LittleFS 数据分区
 * @return 找到的全部 LittleFS 分区均擦除成功时返回 true
 */
bool EraseAllLittleFsStorage();

}  // namespace lilygo_box::app

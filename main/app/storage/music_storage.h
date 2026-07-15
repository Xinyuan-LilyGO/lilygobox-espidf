/*
 * @Description: 音乐源文件夹偏好存储接口
 * @Author: LILYGO_L
 * @Date: 2026-07-14 23:25:00
 * @LastEditTime: 2026-07-14 23:25:00
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

namespace lilygo_box::app {

constexpr size_t kMusicSourceCapacity = 8;
constexpr size_t kMusicSourcePathCapacity = 384;

struct MusicSourcePreferences {
  char paths[kMusicSourceCapacity][kMusicSourcePathCapacity] = {};
};

/**
 * @brief 从 NVS 读取音乐源文件夹配置
 * @param preferences 音乐源配置输出地址
 * @return 读取成功或尚未保存配置返回 true，否则返回 false
 */
bool LoadMusicSourcePreferences(MusicSourcePreferences* preferences);

/**
 * @brief 将音乐源文件夹配置写入 NVS
 * @param preferences 音乐源配置
 * @return 保存成功返回 true，否则返回 false
 */
bool SaveMusicSourcePreferences(
    const MusicSourcePreferences& preferences);

}  // namespace lilygo_box::app

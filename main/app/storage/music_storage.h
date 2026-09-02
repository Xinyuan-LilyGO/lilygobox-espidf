/*
 * @Description: 音乐源文件夹偏好存储接口
 * @Author: LILYGO_L
 * @Date: 2026-07-14 23:25:00
 * @LastEditTime: 2026-09-02 17:51:34
 * @License: GPL 3.0
 */
#pragma once

#include <cstddef>

namespace lilygo_box::app {

constexpr size_t kMusicSourceCapacity = 8;
constexpr size_t kMusicSourcePathCapacity = 384;

struct MusicSourcePreferences {
  // 用户选择的音乐源目录，空字符串表示该槽位未使用。
  char paths[kMusicSourceCapacity][kMusicSourcePathCapacity] = {};
};

/**
 * @brief 启动时从 NVS 初始化音乐源文件夹 RAM 缓存
 */
void InitMusicCache();

/**
 * @brief 从 RAM 缓存读取音乐源文件夹配置
 * @param preferences 音乐源配置输出地址
 * @return 缓存读取成功返回 true，否则返回 false
 */
bool GetMusicSourcePreferences(MusicSourcePreferences* preferences);

/**
 * @brief 比较并更新音乐源配置，存在变化时立即写入 NVS
 * @param preferences 音乐源配置
 * @return 无变化或 NVS 提交成功返回 true，否则返回 false
 */
bool UpdateMusicSourcePreferences(const MusicSourcePreferences& preferences);

}  // namespace lilygo_box::app

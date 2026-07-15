/*
 * @Description: SD 卡 MP3 曲库扫描与曲目信息实现
 * @Author: LILYGO_L
 * @Date: 2026-07-14 22:55:00
 * @LastEditTime: 2026-07-14 23:43:18
 * @License: GPL 3.0
 */
#include "app/music_library.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <utility>

#include "audio/mp3_metadata.h"

namespace lilygo_box::app {
namespace {

constexpr int kMaximumDirectoryDepth = 16;
constexpr size_t kMaximumTrackCount = 2048;

/**
 * @brief 将 ASCII 字符串转换为小写形式
 * @param value 原始字符串
 * @return 小写字符串
 */
std::string AsciiLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
      [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
  return value;
}

/**
 * @brief 判断路径是否为 MP3 文件
 * @param path 文件路径
 * @return 扩展名为 mp3 返回 true，否则返回 false
 */
bool IsMp3Path(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  return dot != std::string::npos && AsciiLower(path.substr(dot)) == ".mp3";
}

/**
 * @brief 从文件路径中提取不带扩展名的文件名
 * @param path 文件路径
 * @return 文件标题
 */
std::string FileTitle(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  const size_t begin = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = path.find_last_of('.');
  const size_t end = dot == std::string::npos || dot < begin
                         ? path.size()
                         : dot;
  return path.substr(begin, end - begin);
}

/**
 * @brief 将目录与子项名称组合为路径
 * @param directory 目录路径
 * @param name 子项名称
 * @return 组合后的路径
 */
std::string JoinPath(const std::string& directory, const char* name) {
  if (!directory.empty() && directory.back() == '/') {
    return directory + name;
  }
  return directory + "/" + name;
}

/**
 * @brief 将一个 MP3 文件加入曲库
 * @param path MP3 文件路径
 * @param known_paths 已加入路径集合
 * @param tracks 曲库输出列表
 */
void AddMp3Track(const std::string& path,
    std::unordered_set<std::string>* known_paths,
    std::vector<MusicTrack>* tracks) {
  if (!known_paths->insert(path).second) {
    return;
  }
  audio::Mp3Metadata metadata;
  if (!audio::ReadMp3Metadata(path.c_str(), &metadata)) {
    return;
  }
  MusicTrack track;
  track.path = path;
  track.title = metadata.title.empty() ? FileTitle(path) : metadata.title;
  track.artist = metadata.artist.empty() ? "Unknown Artist" : metadata.artist;
  track.duration_ms = metadata.duration_ms;
  tracks->push_back(std::move(track));
}

/**
 * @brief 递归扫描一个音乐源目录
 * @param directory 当前目录
 * @param depth 当前递归深度
 * @param known_paths 已加入路径集合
 * @param tracks 曲库输出列表
 * @return 当前目录读取成功返回 true，否则返回 false
 */
bool ScanDirectory(const std::string& directory, int depth,
    std::unordered_set<std::string>* known_paths,
    std::vector<MusicTrack>* tracks) {
  if (depth > kMaximumDirectoryDepth ||
      tracks->size() >= kMaximumTrackCount) {
    return true;
  }
  DIR* handle = opendir(directory.c_str());
  if (handle == nullptr) {
    return false;
  }

  bool success = true;
  while (dirent* entry = readdir(handle)) {
    if (std::string(entry->d_name) == "." ||
        std::string(entry->d_name) == "..") {
      continue;
    }
    const std::string path = JoinPath(directory, entry->d_name);
    struct stat information {};
    if (stat(path.c_str(), &information) != 0) {
      success = false;
      continue;
    }
    if (S_ISDIR(information.st_mode)) {
      success = ScanDirectory(path, depth + 1, known_paths, tracks) && success;
    } else if (S_ISREG(information.st_mode) && IsMp3Path(path)) {
      AddMp3Track(path, known_paths, tracks);
    }
    if (tracks->size() >= kMaximumTrackCount) {
      break;
    }
  }
  closedir(handle);
  return success;
}

}  // namespace

bool ScanMusicLibrary(const std::vector<std::string>& source_paths,
    std::vector<MusicTrack>* tracks) {
  if (tracks == nullptr) {
    return false;
  }
  tracks->clear();
  std::unordered_set<std::string> known_paths;
  bool success = true;
  for (const std::string& source_path : source_paths) {
    if (source_path.empty()) {
      continue;
    }
    success = ScanDirectory(source_path, 0, &known_paths, tracks) && success;
  }
  std::sort(tracks->begin(), tracks->end(),
      [](const MusicTrack& left, const MusicTrack& right) {
        return AsciiLower(left.title) < AsciiLower(right.title);
      });
  return success;
}

}  // namespace lilygo_box::app

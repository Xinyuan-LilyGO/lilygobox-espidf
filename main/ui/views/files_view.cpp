/*
 * @Description: Lightweight SD card file browser
 * @Author: LILYGO_L
 * @Date: 2026-07-09 00:00:00
 * @LastEditTime: 2026-07-12 01:35:14
 * @License: GPL 3.0
 */
#include "ui/views/files_view.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "base/logger.h"
#include "esp_vfs_fat.h"
#include "hal/providers/storage_provider.h"
#include "ui/animation/transition_animation.h"
#include "ui/resources/fonts/font_assets.h"
#include "ui/resources/fonts/icon_assets.h"
#include "ui/input/edge_back_gesture.h"
#include "ui/input/press_cancel.h"
#include "ui/theme/theme_provider.h"
#include "ui/widgets/navigation_drawer.h"

namespace lilygo_box::ui {
namespace {

constexpr uint32_t kBackgroundColor = theme::LightNeutralTheme().surface;
constexpr uint32_t kPrimaryTextColor = theme::LightNeutralTheme().on_surface;
constexpr uint32_t kSecondaryTextColor =
    theme::LightNeutralTheme().on_surface_variant;
constexpr uint32_t kIconColor = theme::LightNeutralTheme().on_surface_variant;
constexpr uint32_t kDividerColor = theme::LightNeutralTheme().outline_variant;
constexpr uint32_t kPressedColor = theme::LightNeutralTheme().state_layer;
constexpr uint32_t kSelectedStorageColor =
    theme::LightNeutralTheme().action_container_pressed;
constexpr uint32_t kActionColor = theme::LightNeutralTheme().action;
constexpr uint32_t kActionTextColor = theme::LightNeutralTheme().on_action;
constexpr uint32_t kStatusIconBackgroundColor =
    theme::LightNeutralTheme().action_container;
constexpr int kHeaderTop = 68;
constexpr int kHeaderSidePadding = 28;
constexpr int kHeaderTitleX = 112;
constexpr int kBreadcrumbTop = 158;
constexpr int kBreadcrumbHeight = 48;
constexpr int kBreadcrumbTextMaxWidth = 156;
constexpr int kBreadcrumbTextHeight = 34;
constexpr int kStorageListTop = 218;
constexpr int kStorageRowHeight = 88;
constexpr int kStorageRowTextX = 94;
constexpr int kStorageNameHeight = 36;
constexpr int kStorageDescriptionHeight = 28;
constexpr int kStorageRetryPeriodMs = 850;
constexpr int kStorageMonitorPeriodMs = 1000;
constexpr int kStorageMissingCheckCount = 2;
constexpr int kFolderPickerActionHeight = 94;
constexpr int kFolderPickerButtonHeight = 64;
constexpr size_t kMaxDirectoryEntryCount = 256;

struct FilesViewState {
  AppViewConfig config;
  lv_obj_t* root = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* subtitle_label = nullptr;
  lv_obj_t* content = nullptr;
  NavigationDrawerState drawer;
  lv_timer_t* storage_retry_timer = nullptr;
  lv_timer_t* storage_monitor_timer = nullptr;
  EdgeBackSwipeState directory_swipe;
  std::string current_path;
  FolderPickerViewConfig picker_config;
  int storage_missing_checks = 0;
  bool storage_was_mounted = false;
  bool folder_picker_mode = false;
  bool folder_picker_closing = false;
};

struct FileEntry {
  std::string name;
  uint64_t size = 0;
  bool directory = false;
};

struct PathClickContext {
  FilesViewState* state = nullptr;
  std::string path;
};

/**
 * @brief 关闭文件管理导航侧边栏
 * @param state 文件管理页面状态
 */
void CloseDrawer(FilesViewState* state);

/**
 * @brief 使用退出动画关闭临时文件夹选择页面
 * @param state 文件管理页面状态
 */
void CloseFolderPicker(FilesViewState* state);

/**
 * @brief 读取并显示指定目录内容
 * @param state 文件管理页面状态
 * @param path 需要显示的目录路径
 * @return 显示成功返回 true，否则返回 false
 */
bool RenderDirectoryContent(FilesViewState* state, const std::string& path);

/**
 * @brief 启动 SD 卡发现和目录加载流程
 * @param state 文件管理页面状态
 */
void StartStorageDiscovery(FilesViewState* state);

/**
 * @brief 处理已挂载 SD 卡被拔出的状态切换
 * @param state 文件管理页面状态
 */
void HandleStorageRemoval(FilesViewState* state);

/**
 * @brief 获取当前文件页面显示的标题
 * @param state 文件管理页面状态
 * @return 页面标题文本
 */
const char* FilesHeaderTitle(const FilesViewState* state) {
  if (state != nullptr && state->folder_picker_mode &&
      state->picker_config.title != nullptr) {
    return state->picker_config.title;
  }
  return "Files";
}

/**
 * @brief 将对象背景、边框、轮廓、阴影和内边距设为透明样式
 * @param object LVGL 对象
 */
void MakeTransparent(lv_obj_t* object) {
  if (object == nullptr) {
    return;
  }
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
}

/**
 * @brief 设置文本对象的颜色和字体
 * @param object LVGL 文本对象
 * @param color 文本颜色
 * @param font 文本字体
 */
void SetTextStyle(lv_obj_t* object, lv_color_t color, const lv_font_t* font) {
  lv_obj_set_style_text_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
}

/**
 * @brief 获取 22 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font22() { return &lvgl_font_google_sans_flex_22; }

/**
 * @brief 获取 24 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font24() { return &lvgl_font_google_sans_flex_24; }

/**
 * @brief 获取 28 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font28() { return &lvgl_font_google_sans_flex_28; }

/**
 * @brief 获取 36 号 Google Sans 字体
 * @return 字体指针
 */
const lv_font_t* Font36() { return &lvgl_font_google_sans_flex_36; }

/**
 * @brief 获取 32 号 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialFillIconFont32() {
  return &lvgl_font_material_symbols_fill_32;
}

/**
 * @brief 获取 44 号 Material Symbols 字体
 * @return 字体指针
 */
const lv_font_t* MaterialOutlineIconFont44() {
  return &lvgl_font_material_symbols_outline_44;
}

/**
 * @brief 获取 44 号文件管理抽屉图标字体
 * @return 字体指针
 */
const lv_font_t* FilesFillIconFont44() {
  return &lvgl_font_material_symbols_fill_44;
}

/**
 * @brief 获取 56 号文件管理大图标字体
 * @return 字体指针
 */
const lv_font_t* FilesFillIconFont56() {
  return &lvgl_font_material_symbols_fill_56;
}

/**
 * @brief 创建文件管理文本标签
 * @param parent 父对象
 * @param text 标签文本
 * @param color 文本颜色
 * @param font 文本字体
 * @return 创建成功返回标签对象，否则返回 nullptr
 */
lv_obj_t* CreateLabel(lv_obj_t* parent, const char* text, lv_color_t color,
                      const lv_font_t* font) {
  lv_obj_t* label = lv_label_create(parent);
  if (label == nullptr) {
    return nullptr;
  }
  lv_label_set_text(label, text == nullptr ? "" : text);
  SetTextStyle(label, color, font);
  return label;
}

/**
 * @brief 创建 Material Symbols 图标标签
 * @param parent 父对象
 * @param symbol 图标字符
 * @param color 图标颜色
 * @param font 图标字体，为空时使用 32 号字体
 * @return 创建成功返回图标对象，否则返回 nullptr
 */
lv_obj_t* CreateMaterialIcon(lv_obj_t* parent, const char* symbol,
                             lv_color_t color,
                             const lv_font_t* font = nullptr) {
  lv_obj_t* icon_label = CreateLabel(
      parent, symbol, color,
      font == nullptr ? MaterialFillIconFont32() : font);
  if (icon_label != nullptr) {
    lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  }
  return icon_label;
}

/**
 * @brief 创建顶部圆形图标按钮
 * @param parent 父对象
 * @param symbol 图标字符
 * @return 创建成功返回按钮对象，否则返回 nullptr
 */
lv_obj_t* CreateIconButton(lv_obj_t* parent, const char* symbol) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }
  lv_obj_remove_style_all(button);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(button, 72, 72);
  lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(kPressedColor),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_radius(button, 36, LV_PART_MAIN);
  lv_obj_set_style_radius(button, 36, LV_STATE_PRESSED);

  lv_obj_t* icon_label =
      CreateMaterialIcon(button, symbol, lv_color_hex(kPrimaryTextColor),
                         FilesFillIconFont56());
  if (icon_label != nullptr) {
    lv_obj_center(icon_label);
  }
  return button;
}

/**
 * @brief 判断目录项是否为当前目录或上级目录
 * @param name 目录项名称
 * @return 是特殊目录项返回 true，否则返回 false
 */
bool IsDotDirectoryEntry(const char* name) {
  return name == nullptr || std::strcmp(name, ".") == 0 ||
         std::strcmp(name, "..") == 0;
}

/**
 * @brief 拼接父路径和文件名
 * @param parent 父路径
 * @param name 文件名
 * @return 拼接后的完整路径
 */
std::string JoinPath(const std::string& parent, const char* name) {
  if (parent.empty()) {
    return name == nullptr ? std::string() : std::string(name);
  }
  if (parent.back() == '/') {
    return parent + (name == nullptr ? "" : name);
  }
  return parent + "/" + (name == nullptr ? "" : name);
}

/**
 * @brief 获取受存储根目录约束的上级路径
 * @param path 当前路径
 * @param base_path 存储根路径
 * @return 上级路径，无法继续向上时返回存储根路径
 */
std::string ParentPath(const std::string& path, const std::string& base_path) {
  if (path.empty() || base_path.empty() || path == base_path ||
      path.compare(0, base_path.size(), base_path) != 0) {
    return base_path;
  }
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash <= base_path.size()) {
    return base_path;
  }
  return path.substr(0, slash);
}

/**
 * @brief 忽略 ASCII 字母大小写比较两个文件名
 * @param left 左侧文件名
 * @param right 右侧文件名
 * @return 左侧较小时返回负数，相等返回 0，否则返回正数
 */
int CompareNamesIgnoreCase(const std::string& left, const std::string& right) {
  const size_t count = std::min(left.size(), right.size());
  for (size_t i = 0; i < count; ++i) {
    const int left_char = std::tolower(static_cast<unsigned char>(left[i]));
    const int right_char = std::tolower(static_cast<unsigned char>(right[i]));
    if (left_char != right_char) {
      return left_char < right_char ? -1 : 1;
    }
  }
  if (left.size() == right.size()) {
    return 0;
  }
  return left.size() < right.size() ? -1 : 1;
}

/**
 * @brief 读取并排序指定目录中的文件项
 * @param path 目录路径
 * @param entries 文件项输出列表
 * @param truncated 是否因数量上限而截断
 * @return 读取成功返回 true，否则返回 false
 */
bool ReadDirectoryEntries(const std::string& path,
                          std::vector<FileEntry>* entries, bool* truncated) {
  if (entries == nullptr || truncated == nullptr) {
    return false;
  }
  entries->clear();
  *truncated = false;
  DIR* directory = opendir(path.c_str());
  if (directory == nullptr) {
    return false;
  }

  while (true) {
    dirent* entry = readdir(directory);
    if (entry == nullptr) {
      break;
    }
    if (IsDotDirectoryEntry(entry->d_name)) {
      continue;
    }
    if (entries->size() >= kMaxDirectoryEntryCount) {
      *truncated = true;
      break;
    }

    FileEntry item;
    item.name = entry->d_name;
    const std::string full_path = JoinPath(path, entry->d_name);
    struct stat entry_stat = {};
    if (stat(full_path.c_str(), &entry_stat) == 0) {
      item.directory = S_ISDIR(entry_stat.st_mode);
      item.size =
          item.directory ? 0 : static_cast<uint64_t>(entry_stat.st_size);
    } else {
#ifdef DT_DIR
      item.directory = entry->d_type == DT_DIR;
#endif
    }
    entries->push_back(std::move(item));
  }
  closedir(directory);

  std::sort(entries->begin(), entries->end(),
            [](const FileEntry& left, const FileEntry& right) {
              if (left.directory != right.directory) {
                return left.directory && !right.directory;
              }
              return CompareNamesIgnoreCase(left.name, right.name) < 0;
            });
  return true;
}

/**
 * @brief 将字节数格式化为易读容量文本
 * @param bytes 字节数
 * @param output 输出缓冲区
 * @param output_size 输出缓冲区大小
 */
void FormatSize(uint64_t bytes, char* output, size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return;
  }
  if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
    std::snprintf(output, output_size, "%.2f GB",
                  static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
  } else if (bytes >= 1024ULL * 1024ULL) {
    std::snprintf(output, output_size, "%.1f MB",
                  static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024ULL) {
    std::snprintf(output, output_size, "%.1f KB",
                  static_cast<double>(bytes) / 1024.0);
  } else {
    std::snprintf(output, output_size, "%llu B",
                  static_cast<unsigned long long>(bytes));
  }
}

/**
 * @brief 读取存储空间容量摘要
 * @param base_path 存储挂载路径
 * @param output 输出缓冲区
 * @param output_size 输出缓冲区大小
 * @return 读取成功返回 true，否则返回 false
 */
bool ReadStorageSummary(const char* base_path, char* output,
                        size_t output_size) {
  if (base_path == nullptr || output == nullptr || output_size == 0) {
    return false;
  }
  uint64_t total_bytes = 0;
  uint64_t free_bytes = 0;
  if (esp_vfs_fat_info(base_path, &total_bytes, &free_bytes) != ESP_OK) {
    return false;
  }
  char free_text[24] = {};
  char total_text[24] = {};
  FormatSize(free_bytes, free_text, sizeof(free_text));
  FormatSize(total_bytes, total_text, sizeof(total_text));
  std::snprintf(output, output_size, "%s free of %s", free_text, total_text);
  return true;
}

/**
 * @brief 格式化当前目录的项目数量
 * @param count 已读取项目数
 * @param truncated 是否因数量上限而截断
 * @param output 输出缓冲区
 * @param output_size 输出缓冲区大小
 */
void FormatItemCount(size_t count, bool truncated, char* output,
                     size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return;
  }
  if (truncated) {
    std::snprintf(output, output_size, "%u+ items",
                  static_cast<unsigned>(count));
    return;
  }
  std::snprintf(output, output_size, "%u %s", static_cast<unsigned>(count),
                count == 1 ? "item" : "items");
}

/**
 * @brief 清空文件管理动态内容区域
 * @param state 文件管理页面状态
 */
void ClearContent(FilesViewState* state) {
  if (state != nullptr && state->content != nullptr) {
    lv_obj_clean(state->content);
  }
}

/**
 * @brief 更新文件管理顶部标题和副标题
 * @param state 文件管理页面状态
 * @param title 标题文本
 * @param subtitle 副标题文本
 */
void SetHeader(FilesViewState* state, const char* title, const char* subtitle) {
  if (state == nullptr) {
    return;
  }
  if (state->title_label != nullptr) {
    lv_label_set_text(state->title_label, title == nullptr ? "" : title);
  }
  if (state->subtitle_label != nullptr) {
    lv_label_set_text(state->subtitle_label,
                      subtitle == nullptr ? "" : subtitle);
  }
}

/**
 * @brief 删除路径点击事件上下文
 * @param event LVGL 事件对象
 */
void PathClickContextDeleteCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_DELETE) {
    delete static_cast<PathClickContext*>(lv_event_get_user_data(event));
  }
}

/**
 * @brief 处理面包屑或目录行点击事件
 * @param event LVGL 事件对象
 */
void PathClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* context = static_cast<PathClickContext*>(lv_event_get_user_data(event));
  if (context == nullptr || context->state == nullptr ||
      context->path.empty()) {
    return;
  }
  if (!RenderDirectoryContent(context->state, context->path)) {
    FilesViewState* state = context->state;
    if (state->config.storage != nullptr && state->storage_was_mounted &&
        !state->config.storage->IsSdCardMounted()) {
      HandleStorageRemoval(state);
    } else {
      SetHeader(state, FilesHeaderTitle(state), "Unable to open folder");
    }
  }
}

/**
 * @brief 创建面包屑路径项
 * @param parent 父对象
 * @param state 文件管理页面状态
 * @param text 显示文本
 * @param path 点击后打开的路径
 * @param x 起始 X 坐标
 * @param current 是否为当前目录
 * @return 下一个面包屑项的起始 X 坐标
 */
int CreateBreadcrumbItem(lv_obj_t* parent, FilesViewState* state,
                         const char* text, const std::string& path, int x,
                         bool current) {
  lv_obj_t* item = current ? lv_obj_create(parent) : lv_button_create(parent);
  if (item == nullptr) {
    return x;
  }
  lv_obj_remove_style_all(item);
  lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(item, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(item, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_pos(item, x, 0);
  lv_obj_set_height(item, kBreadcrumbHeight);
  if (!current) {
    lv_obj_set_style_bg_color(item, lv_color_hex(kPressedColor),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_radius(item, 20, LV_STATE_PRESSED);
    auto* context = new PathClickContext{
        .state = state,
        .path = path,
    };
    lv_obj_add_event_cb(item, PathClickedEventCallback, LV_EVENT_CLICKED,
                        context);
    lv_obj_add_event_cb(item, PathClickContextDeleteCallback, LV_EVENT_DELETE,
                        context);
  }

  lv_obj_t* label = CreateLabel(
      item, text,
      lv_color_hex(current ? kPrimaryTextColor : kSecondaryTextColor),
      Font24());
  if (label == nullptr) {
    lv_obj_set_width(item, 80);
    return x + 88;
  }
  lv_obj_set_width(label, LV_SIZE_CONTENT);
  lv_obj_update_layout(label);
  const int text_width =
      std::min<int32_t>(lv_obj_get_width(label), kBreadcrumbTextMaxWidth);
  lv_obj_set_size(label, text_width, kBreadcrumbTextHeight);
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 12, 0);
  const int item_width = text_width + 24;
  lv_obj_set_width(item, item_width);
  return x + item_width;
}

/**
 * @brief 创建当前目录的横向面包屑导航栏
 * @param parent 父对象
 * @param state 文件管理页面状态
 * @param base_path 存储根路径
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateBreadcrumbBar(lv_obj_t* parent, FilesViewState* state,
                         const std::string& base_path) {
  if (state == nullptr || state->current_path.empty()) {
    return false;
  }
  lv_obj_t* bar = lv_obj_create(parent);
  if (bar == nullptr) {
    return false;
  }
  MakeTransparent(bar);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(bar, state->config.width - 56, kBreadcrumbHeight);
  lv_obj_set_pos(bar, 28, kBreadcrumbTop);
  lv_obj_set_scroll_dir(bar, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);

  const std::string& current_path = state->current_path;
  const bool at_root = current_path == base_path;
  int x = CreateBreadcrumbItem(bar, state, "SD Card", base_path, 0, at_root);
  if (at_root) {
    return true;
  }

  if (current_path.compare(0, base_path.size(), base_path) != 0) {
    return true;
  }
  std::string relative = current_path.substr(base_path.size());
  while (!relative.empty() && relative.front() == '/') {
    relative.erase(relative.begin());
  }
  std::string path = base_path;
  size_t start = 0;
  while (start < relative.size()) {
    const size_t slash = relative.find('/', start);
    const std::string part = relative.substr(
        start, slash == std::string::npos ? std::string::npos : slash - start);
    if (!part.empty()) {
      lv_obj_t* separator = CreateMaterialIcon(
          bar, icon::kChevronRight, lv_color_hex(kSecondaryTextColor));
      if (separator != nullptr) {
        lv_obj_set_pos(separator, x, 8);
      }
      x += 28;
      path = JoinPath(path, part.c_str());
      const bool current = slash == std::string::npos;
      x = CreateBreadcrumbItem(bar, state, part.c_str(), path, x, current);
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  lv_obj_scroll_to_x(bar, x, LV_ANIM_OFF);
  return true;
}

/**
 * @brief 根据文件项类型选择 Material Symbols 图标
 * @param entry 文件项
 * @return 对应的图标字符
 */
const char* EntryIcon(const FileEntry& entry) {
  if (entry.directory) {
    return icon::kFolder;
  }
  const size_t dot = entry.name.find_last_of('.');
  if (dot == std::string::npos) {
    return icon::kDraft;
  }
  std::string extension = entry.name.substr(dot + 1);
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  if (extension == "jpg" || extension == "jpeg" || extension == "png" ||
      extension == "bmp" || extension == "gif") {
    return icon::kImage;
  }
  if (extension == "mp3" || extension == "wav" || extension == "flac" ||
      extension == "aac") {
    return icon::kMusic;
  }
  return icon::kDraft;
}

/**
 * @brief 创建文件或目录列表行
 * @param parent 父对象
 * @param state 文件管理页面状态
 * @param entry 文件项
 * @param y 行顶部 Y 坐标
 * @param width 行宽度
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFileRow(lv_obj_t* parent, FilesViewState* state,
                   const FileEntry& entry, int y, int width) {
  lv_obj_t* row =
      entry.directory ? lv_button_create(parent) : lv_obj_create(parent);
  if (row == nullptr) {
    return false;
  }
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(row, width, kStorageRowHeight);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);

  if (entry.directory && state != nullptr) {
    lv_obj_set_style_bg_color(row, lv_color_hex(kPressedColor),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    if (!AddPressCancelOnLeave(row)) {
      lv_obj_delete(row);
      return false;
    }
    auto* context = new PathClickContext{
        .state = state,
        .path = JoinPath(state->current_path, entry.name.c_str()),
    };
    lv_obj_add_event_cb(row, PathClickedEventCallback, LV_EVENT_CLICKED,
                        context);
    lv_obj_add_event_cb(row, PathClickContextDeleteCallback, LV_EVENT_DELETE,
                        context);
  }

  lv_obj_t* icon_label =
      CreateMaterialIcon(row, EntryIcon(entry), lv_color_hex(kIconColor));
  if (icon_label != nullptr) {
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 34, 0);
  }

  lv_obj_t* name_label = CreateLabel(row, entry.name.c_str(),
                                     lv_color_hex(kPrimaryTextColor), Font28());
  if (name_label != nullptr) {
    lv_obj_set_size(name_label, width - kStorageRowTextX - 32,
                    kStorageNameHeight);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, kStorageRowTextX, 12);
  }

  char description[32] = {};
  if (entry.directory) {
    std::snprintf(description, sizeof(description), "Folder");
  } else {
    FormatSize(entry.size, description, sizeof(description));
  }
  lv_obj_t* description_label = CreateLabel(
      row, description, lv_color_hex(kSecondaryTextColor), Font22());
  if (description_label != nullptr) {
    lv_obj_set_size(description_label, width - kStorageRowTextX - 32,
                    kStorageDescriptionHeight);
    lv_label_set_long_mode(description_label, LV_LABEL_LONG_DOT);
    lv_obj_align(description_label, LV_ALIGN_TOP_LEFT, kStorageRowTextX, 50);
  }

  lv_obj_t* divider = lv_obj_create(row);
  if (divider != nullptr) {
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(divider, width - kStorageRowTextX - 28, 1);
    lv_obj_align(divider, LV_ALIGN_BOTTOM_RIGHT, -28, 0);
    lv_obj_set_style_bg_color(
        divider, lv_color_hex(kDividerColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(divider, 0, LV_PART_MAIN);
  }
  return true;
}

/**
 * @brief 创建空目录提示内容
 * @param parent 父对象
 * @param state 文件管理页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateEmptyDirectoryContent(lv_obj_t* parent, FilesViewState* state) {
  if (state == nullptr) {
    return false;
  }
  lv_obj_t* group = lv_obj_create(parent);
  if (group == nullptr) {
    return false;
  }
  MakeTransparent(group);
  lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(group, state->config.width, 184);
  lv_obj_set_pos(group, 0, kStorageListTop + 74);

  lv_obj_t* icon_background = lv_obj_create(group);
  if (icon_background == nullptr) {
    return false;
  }
  lv_obj_remove_flag(icon_background, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(icon_background, 96, 96);
  lv_obj_set_style_radius(icon_background, 48, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      icon_background, lv_color_hex(kStatusIconBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(icon_background, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(icon_background, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(icon_background, 0, LV_PART_MAIN);
  lv_obj_align(icon_background, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t* icon_label = CreateMaterialIcon(icon_background, icon::kFolderOpen,
                                            lv_color_hex(kActionColor));
  if (icon_label != nullptr) {
    lv_obj_center(icon_label);
  }
  lv_obj_t* message = CreateLabel(group, "This folder is empty",
                                  lv_color_hex(kPrimaryTextColor), Font28());
  if (message != nullptr) {
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 122);
  }
  return true;
}

/**
 * @brief 创建面包屑和文件列表内容
 * @param parent 父对象
 * @param state 文件管理页面状态
 * @param entries 文件项列表
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateStorageListContent(lv_obj_t* parent, FilesViewState* state,
                              const std::vector<FileEntry>& entries) {
  if (state == nullptr || state->config.storage == nullptr) {
    return false;
  }
  const char* base_path = state->config.storage->SdCardBasePath();
  if (base_path == nullptr || base_path[0] == '\0' ||
      !CreateBreadcrumbBar(parent, state, base_path)) {
    return false;
  }
  if (entries.empty()) {
    return CreateEmptyDirectoryContent(parent, state);
  }

  lv_obj_t* list = lv_list_create(parent);
  if (list == nullptr) {
    return false;
  }
  MakeTransparent(list);
  lv_obj_add_flag(list, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(list, LV_OBJ_FLAG_GESTURE_BUBBLE);
  const int picker_reserved_height =
      state->folder_picker_mode ? kFolderPickerActionHeight : 0;
  lv_obj_set_size(list, state->config.width,
                  state->config.height - kStorageListTop -
                      picker_reserved_height);
  lv_obj_set_pos(list, 0, kStorageListTop);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_style_pad_row(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(list, 24, LV_PART_MAIN);

  int y = 0;
  for (const FileEntry& entry : entries) {
    if (!CreateFileRow(list, state, entry, y, state->config.width)) {
      return false;
    }
    y += kStorageRowHeight;
  }
  return true;
}

/**
 * @brief 创建主要操作按钮
 * @param parent 父对象
 * @param text 按钮文本
 * @param width 按钮宽度
 * @param callback 点击事件回调
 * @param user_data 事件用户数据
 * @return 创建成功返回按钮对象，否则返回 nullptr
 */
lv_obj_t* CreatePrimaryActionButton(lv_obj_t* parent, const char* text,
                                    int width, lv_event_cb_t callback,
                                    void* user_data) {
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return nullptr;
  }
  lv_obj_remove_style_all(button);
  lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(button, width, 64);
  lv_obj_set_style_bg_color(button, lv_color_hex(kActionColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(
      button, lv_color_hex(theme::LightNeutralTheme().action_pressed),
      LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_radius(button, 32, LV_PART_MAIN);
  lv_obj_set_style_radius(button, 32, LV_STATE_PRESSED);
  if (callback != nullptr) {
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
  }
  lv_obj_t* label =
      CreateLabel(button, text, lv_color_hex(kActionTextColor), Font24());
  if (label != nullptr) {
    lv_obj_center(label);
  }
  return button;
}

/**
 * @brief 处理无存储页面的重新扫描按钮点击事件
 * @param event LVGL 事件对象
 */
void RefreshStorageClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    StartStorageDiscovery(
        static_cast<FilesViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 渲染 SD 卡扫描中的加载状态
 * @param state 文件管理页面状态
 */
void RenderScanningContent(FilesViewState* state) {
  if (state == nullptr) {
    return;
  }
  ClearContent(state);
  SetHeader(state, FilesHeaderTitle(state), "Scanning SD card");

  lv_obj_t* group = lv_obj_create(state->content);
  if (group == nullptr) {
    return;
  }
  MakeTransparent(group);
  lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(group, state->config.width, 180);
  lv_obj_align(group, LV_ALIGN_CENTER, 0, 26);

  lv_obj_t* spinner = lv_spinner_create(group);
  if (spinner != nullptr) {
    lv_obj_set_size(spinner, 68, 68);
    lv_spinner_set_anim_params(spinner, 850, 250);
    lv_obj_set_style_arc_color(
        spinner,
        lv_color_hex(theme::LightNeutralTheme().surface_container_high),
        LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(kActionColor),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 7, LV_PART_INDICATOR);
    lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, 0);
  }

  lv_obj_t* message = CreateLabel(group, "Looking for an SD card...",
                                  lv_color_hex(kPrimaryTextColor), Font28());
  if (message != nullptr) {
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 96);
  }
  lv_obj_t* hint = CreateLabel(group, "Keep the card inserted while scanning",
                               lv_color_hex(kSecondaryTextColor), Font22());
  if (hint != nullptr) {
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 138);
  }
  EnableEdgeBackSwipeEventBubble(state->content);
}

/**
 * @brief 渲染单次扫描后未发现存储设备的空状态
 * @param state 文件管理页面状态
 */
void RenderNoStorageContent(FilesViewState* state) {
  if (state == nullptr) {
    return;
  }
  ClearContent(state);
  state->current_path.clear();
  SetHeader(state, FilesHeaderTitle(state), "No SD card");

  lv_obj_t* group = lv_obj_create(state->content);
  if (group == nullptr) {
    return;
  }
  MakeTransparent(group);
  lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(group, state->config.width, 280);
  lv_obj_align(group, LV_ALIGN_CENTER, 0, 26);

  lv_obj_t* icon_background = lv_obj_create(group);
  if (icon_background != nullptr) {
    lv_obj_remove_flag(icon_background, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(icon_background, 96, 96);
    lv_obj_set_style_radius(icon_background, 48, LV_PART_MAIN);
    lv_obj_set_style_bg_color(icon_background,
                              lv_color_hex(kStatusIconBackgroundColor),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(icon_background, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon_background, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon_background, 0, LV_PART_MAIN);
    lv_obj_align(icon_background, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t* icon_label = CreateMaterialIcon(icon_background, icon::kSdStorage,
                                              lv_color_hex(kActionColor));
    if (icon_label != nullptr) {
      lv_obj_center(icon_label);
    }
  }

  lv_obj_t* message = CreateLabel(group, "SD card not found",
                                  lv_color_hex(kPrimaryTextColor), Font28());
  if (message != nullptr) {
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 116);
  }
  lv_obj_t* hint = CreateLabel(group, "Insert a card and scan again.",
                               lv_color_hex(kSecondaryTextColor), Font22());
  if (hint != nullptr) {
    lv_obj_set_width(hint, state->config.width - 80);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 154);
  }
  lv_obj_t* refresh = CreatePrimaryActionButton(
      group, "Scan again", 196, RefreshStorageClickedEventCallback, state);
  if (refresh != nullptr) {
    lv_obj_align(refresh, LV_ALIGN_TOP_MID, 0, 194);
  }
  EnableEdgeBackSwipeEventBubble(state->content);
}

/**
 * @brief 读取并渲染指定目录
 * @param state 文件管理页面状态
 * @param path 需要打开的目录路径
 * @return 渲染成功返回 true，否则返回 false
 */
bool RenderDirectoryContent(FilesViewState* state, const std::string& path) {
  if (state == nullptr || path.empty()) {
    return false;
  }
  std::vector<FileEntry> entries;
  bool truncated = false;
  if (!ReadDirectoryEntries(path, &entries, &truncated)) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
               "Unable to read directory: %s\n", path.c_str());
    return false;
  }

  state->current_path = path;
  ClearContent(state);
  char item_count_text[32] = {};
  FormatItemCount(entries.size(), truncated, item_count_text,
                  sizeof(item_count_text));
  SetHeader(state, FilesHeaderTitle(state), item_count_text);
  if (!CreateStorageListContent(state->content, state, entries)) {
    return false;
  }
  EnableEdgeBackSwipeEventBubble(state->content);
  return true;
}

/**
 * @brief 停止当前存储设备自动扫描计时器
 * @param state 文件管理页面状态
 */
void StopStorageDiscovery(FilesViewState* state) {
  if (state == nullptr || state->storage_retry_timer == nullptr) {
    return;
  }
  lv_timer_t* timer = state->storage_retry_timer;
  state->storage_retry_timer = nullptr;
  lv_timer_delete(timer);
}

/**
 * @brief 停止 SD 卡在线状态监控计时器
 * @param state 文件管理页面状态
 */
void StopStorageMonitor(FilesViewState* state) {
  if (state == nullptr || state->storage_monitor_timer == nullptr) {
    return;
  }
  lv_timer_t* timer = state->storage_monitor_timer;
  state->storage_monitor_timer = nullptr;
  lv_timer_delete(timer);
}

/**
 * @brief 处理已挂载 SD 卡被拔出的状态切换
 * @param state 文件管理页面状态
 */
void HandleStorageRemoval(FilesViewState* state) {
  if (state == nullptr || state->config.storage == nullptr) {
    return;
  }
  StopStorageDiscovery(state);
  state->storage_was_mounted = false;
  state->storage_missing_checks = 0;
  state->current_path.clear();
  state->config.storage->UnmountSdCard();
  CloseDrawer(state);
  StartStorageDiscovery(state);
}

/**
 * @brief 处理 SD 卡在线状态监控计时器事件
 * @param timer LVGL 计时器
 */
void StorageMonitorTimerCallback(lv_timer_t* timer) {
  auto* state = static_cast<FilesViewState*>(lv_timer_get_user_data(timer));
  if (state == nullptr || state->storage_monitor_timer != timer ||
      state->config.storage == nullptr) {
    return;
  }

  if (state->config.storage->IsSdCardMounted()) {
    state->storage_was_mounted = true;
    state->storage_missing_checks = 0;
    return;
  }
  if (!state->storage_was_mounted) {
    return;
  }

  ++state->storage_missing_checks;
  if (state->storage_missing_checks >= kStorageMissingCheckCount) {
    HandleStorageRemoval(state);
  }
}

/**
 * @brief 启动 SD 卡在线状态监控计时器
 * @param state 文件管理页面状态
 */
void StartStorageMonitor(FilesViewState* state) {
  if (state == nullptr || state->storage_monitor_timer != nullptr) {
    return;
  }
  state->storage_monitor_timer = lv_timer_create(
      StorageMonitorTimerCallback, kStorageMonitorPeriodMs, state);
}

/**
 * @brief 处理存储设备自动扫描计时器事件
 * @param timer LVGL 计时器
 */
void StorageRetryTimerCallback(lv_timer_t* timer) {
  auto* state = static_cast<FilesViewState*>(lv_timer_get_user_data(timer));
  if (state == nullptr || state->storage_retry_timer != timer) {
    return;
  }
  const bool mounted = state->config.storage != nullptr &&
                       state->config.storage->EnsureSdCardMounted();
  if (mounted) {
    state->storage_was_mounted = true;
    state->storage_missing_checks = 0;
    const char* base_path = state->config.storage->SdCardBasePath();
    if (base_path != nullptr && base_path[0] != '\0' &&
        RenderDirectoryContent(state, base_path)) {
      StopStorageDiscovery(state);
      CloseDrawer(state);
      return;
    }
    state->storage_was_mounted = false;
    state->config.storage->UnmountSdCard();
  }

  StopStorageDiscovery(state);
  RenderNoStorageContent(state);
}

/**
 * @brief 启动存储设备自动扫描流程
 * @param state 文件管理页面状态
 */
void StartStorageDiscovery(FilesViewState* state) {
  if (state == nullptr) {
    return;
  }
  StopStorageDiscovery(state);

  if (state->config.storage == nullptr) {
    state->storage_was_mounted = false;
    RenderNoStorageContent(state);
    return;
  }
  if (state->config.storage->IsSdCardMounted()) {
    state->storage_was_mounted = true;
    state->storage_missing_checks = 0;
    const char* base_path = state->config.storage->SdCardBasePath();
    if (base_path != nullptr && base_path[0] != '\0' &&
        RenderDirectoryContent(state, base_path)) {
      return;
    }
    state->config.storage->UnmountSdCard();
  }

  state->storage_was_mounted = false;
  state->storage_missing_checks = 0;
  RenderScanningContent(state);
  state->storage_retry_timer =
      lv_timer_create(StorageRetryTimerCallback, kStorageRetryPeriodMs, state);
  if (state->storage_retry_timer == nullptr) {
    RenderNoStorageContent(state);
  }
}

/**
 * @brief 处理目录页面的边缘返回手势
 * @param event LVGL 事件对象
 */
void DirectoryEdgeBackEventCallback(lv_event_t* event) {
  auto* state = static_cast<FilesViewState*>(lv_event_get_user_data(event));
  if (state == nullptr) {
    return;
  }
  if (state->folder_picker_mode) {
    lv_event_stop_bubbling(event);
    if (HandleEdgeBackSwipeEvent(event, state->config.width,
                                 &state->directory_swipe)) {
      const char* base_path_text =
          state->config.storage == nullptr
              ? nullptr
              : state->config.storage->SdCardBasePath();
      if (base_path_text != nullptr && base_path_text[0] != '\0' &&
          !state->current_path.empty() &&
          state->current_path != base_path_text) {
        RenderDirectoryContent(state,
            ParentPath(state->current_path, base_path_text));
      } else {
        CloseFolderPicker(state);
      }
      lv_event_stop_processing(event);
    }
    return;
  }
  if (state->config.storage == nullptr || state->current_path.empty()) {
    return;
  }
  const char* base_path_text = state->config.storage->SdCardBasePath();
  if (base_path_text == nullptr || base_path_text[0] == '\0') {
    return;
  }
  const std::string base_path(base_path_text);
  if (state->current_path == base_path) {
    return;
  }

  lv_event_stop_bubbling(event);
  if (HandleEdgeBackSwipeEvent(event, state->config.width,
                               &state->directory_swipe)) {
    RenderDirectoryContent(state, ParentPath(state->current_path, base_path));
    lv_event_stop_processing(event);
  }
}

/**
 * @brief 关闭文件管理导航侧边栏
 * @param state 文件管理页面状态
 */
void CloseDrawer(FilesViewState* state) {
  if (state != nullptr) {
    CloseNavigationDrawer(&state->drawer);
  }
}

/**
 * @brief 处理抽屉刷新存储按钮点击事件
 * @param event LVGL 事件对象
 */
void DrawerRefreshClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<FilesViewState*>(lv_event_get_user_data(event));
  StartStorageDiscovery(state);
  CloseDrawer(state);
}

/**
 * @brief 处理抽屉存储设备入口点击事件
 * @param event LVGL 事件对象
 */
void DrawerStorageClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<FilesViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->config.storage == nullptr) {
    return;
  }
  const char* base_path = state->config.storage->SdCardBasePath();
  if (base_path != nullptr && base_path[0] != '\0') {
    RenderDirectoryContent(state, base_path);
  }
  CloseDrawer(state);
}

/**
 * @brief 创建导航抽屉中已选中的存储设备行
 * @param parent 父对象
 * @param drawer_width 抽屉宽度
 * @param title 存储设备名称
 * @param subtitle 存储容量摘要
 * @param y 行顶部 Y 坐标
 * @param state 文件管理页面状态
 * @return 创建成功返回存储设备行对象，否则返回 nullptr
 */
lv_obj_t* CreateSelectedStorageDrawerItem(lv_obj_t* parent, int drawer_width,
                                          const char* title,
                                          const char* subtitle, int y,
                                          FilesViewState* state) {
  lv_obj_t* row = lv_button_create(parent);
  if (row == nullptr) {
    return nullptr;
  }
  lv_obj_remove_style_all(row);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_size(row, drawer_width - 24, 104);
  lv_obj_set_pos(row, 0, y);
  lv_obj_set_style_bg_color(row, lv_color_hex(kSelectedStorageColor),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(row, lv_color_hex(kSelectedStorageColor),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_radius(row, 52, LV_PART_MAIN);
  lv_obj_set_style_radius(row, 52, LV_STATE_PRESSED);
  lv_obj_add_event_cb(row, DrawerStorageClickedEventCallback, LV_EVENT_CLICKED,
                      state);

  lv_obj_t* left_cap = lv_obj_create(row);
  if (left_cap != nullptr) {
    lv_obj_remove_style_all(left_cap);
    lv_obj_remove_flag(left_cap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(left_cap, 52, 104);
    lv_obj_set_pos(left_cap, 0, 0);
    lv_obj_set_style_bg_color(left_cap, lv_color_hex(kSelectedStorageColor),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(left_cap, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(left_cap, 0, LV_PART_MAIN);
  }

  lv_obj_t* icon_label =
      CreateMaterialIcon(row, icon::kSdStorage, lv_color_hex(kActionColor));
  if (icon_label != nullptr) {
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 28, 0);
  }
  lv_obj_t* title_label =
      CreateLabel(row, title, lv_color_hex(kActionColor), Font28());
  if (title_label != nullptr) {
    lv_obj_set_width(title_label, drawer_width - 142);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 88, 18);
  }
  lv_obj_t* subtitle_label =
      CreateLabel(row, subtitle, lv_color_hex(kActionColor), Font22());
  if (subtitle_label != nullptr) {
    lv_obj_set_width(subtitle_label, drawer_width - 142);
    lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_DOT);
    lv_obj_align(subtitle_label, LV_ALIGN_TOP_LEFT, 88, 60);
  }
  return row;
}

/**
 * @brief 创建导航抽屉中的未连接或扫描中存储设备行
 * @param parent 父对象
 * @param drawer_width 抽屉宽度
 * @param subtitle 存储设备状态文本
 * @param y 行顶部 Y 坐标
 * @return 创建成功返回存储设备行对象，否则返回 nullptr
 */
lv_obj_t* CreateStorageStateDrawerItem(lv_obj_t* parent, int drawer_width,
                                       const char* subtitle, int y) {
  lv_obj_t* row = lv_obj_create(parent);
  if (row == nullptr) {
    return nullptr;
  }
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, drawer_width, 104);
  lv_obj_set_pos(row, 0, y);

  lv_obj_t* icon_label = CreateMaterialIcon(row, icon::kSdStorage,
                                            lv_color_hex(kSecondaryTextColor));
  if (icon_label != nullptr) {
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 28, 0);
  }
  lv_obj_t* title_label =
      CreateLabel(row, "SD Card", lv_color_hex(kPrimaryTextColor), Font28());
  if (title_label != nullptr) {
    lv_obj_set_width(title_label, drawer_width - 142);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 88, 18);
  }
  lv_obj_t* subtitle_label =
      CreateLabel(row, subtitle, lv_color_hex(kSecondaryTextColor), Font22());
  if (subtitle_label != nullptr) {
    lv_obj_set_width(subtitle_label, drawer_width - 142);
    lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_DOT);
    lv_obj_align(subtitle_label, LV_ALIGN_TOP_LEFT, 88, 60);
  }
  return row;
}

/**
 * @brief 显示文件管理导航抽屉
 * @param state 文件管理页面状态
 */
void ShowDrawer(FilesViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      IsNavigationDrawerOpen(&state->drawer)) {
    return;
  }

  NavigationDrawerConfig drawer_config;
  drawer_config.screen_width = state->config.width;
  drawer_config.screen_height = state->config.height;
  drawer_config.background_color = kBackgroundColor;
  drawer_config.primary_text_color = kPrimaryTextColor;
  drawer_config.icon_color = kIconColor;
  drawer_config.pressed_color = kPressedColor;
  drawer_config.divider_color = kDividerColor;
  drawer_config.title = "Files";
  drawer_config.title_font = Font36();
  drawer_config.item_font = Font28();
  drawer_config.icon_font = FilesFillIconFont44();
  lv_obj_t* drawer = OpenNavigationDrawer(
      state->root, &state->drawer, drawer_config);
  if (drawer == nullptr) {
    return;
  }
  const int drawer_width = NavigationDrawerWidth(&state->drawer);

  int drawer_y = kNavigationDrawerContentTop;
  const bool mounted = state->config.storage != nullptr &&
                       state->config.storage->IsSdCardMounted();
  if (mounted) {
    char storage_summary[64] = {};
    const char* base_path = state->config.storage->SdCardBasePath();
    if (base_path == nullptr || !ReadStorageSummary(base_path, storage_summary,
                                                    sizeof(storage_summary))) {
      std::snprintf(storage_summary, sizeof(storage_summary), "SD card");
    }
    CreateSelectedStorageDrawerItem(drawer, drawer_width, "SD Card",
                                    storage_summary, drawer_y, state);
  } else {
    const char* storage_state =
        state->storage_retry_timer != nullptr ? "Scanning..." : "Not connected";
    CreateStorageStateDrawerItem(drawer, drawer_width, storage_state, drawer_y);
  }
  drawer_y += 116;

  CreateNavigationDrawerItem(&state->drawer, icon::kRefresh,
      "Refresh storage", drawer_y, DrawerRefreshClickedEventCallback, state);
  drawer_y += kNavigationDrawerItemHeight + 12;
  CreateNavigationDrawerDivider(&state->drawer, drawer_y);
  drawer_y += 18;
  CreateNavigationDrawerItem(&state->drawer, icon::kSettings, "Settings",
      drawer_y, nullptr, state);
  PresentNavigationDrawer(&state->drawer);
}

/**
 * @brief 处理顶部菜单按钮点击事件
 * @param event LVGL 事件对象
 */
void MenuButtonClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    ShowDrawer(static_cast<FilesViewState*>(lv_event_get_user_data(event)));
  }
}

/**
 * @brief 处理文件夹选择页面退出动画完成事件
 * @param animation LVGL 动画对象
 */
void FolderPickerCloseCompletedCallback(lv_anim_t* animation) {
  auto* state = static_cast<FilesViewState*>(
      lv_anim_get_user_data(animation));
  if (state == nullptr || state->root == nullptr) {
    return;
  }
  const auto closed_callback = state->picker_config.closed_callback;
  lv_obj_delete(state->root);
  if (closed_callback) {
    closed_callback();
  }
}

/**
 * @brief 使用标准右滑动画关闭文件夹选择页面
 * @param state 文件管理页面状态
 */
void CloseFolderPicker(FilesViewState* state) {
  if (state == nullptr || state->root == nullptr ||
      state->folder_picker_closing) {
    return;
  }
  state->folder_picker_closing = true;
  if (!StartSlideRightWindowTransition(state->root, state->config.width,
      state->picker_config.animation_ms, state,
      FolderPickerCloseCompletedCallback)) {
    const auto closed_callback = state->picker_config.closed_callback;
    lv_obj_delete(state->root);
    if (closed_callback) {
      closed_callback();
    }
  }
}

/**
 * @brief 关闭临时文件夹选择页面
 * @param event LVGL 事件对象
 */
void FolderPickerBackClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<FilesViewState*>(lv_event_get_user_data(event));
  if (state != nullptr && state->root != nullptr) {
    CloseFolderPicker(state);
  }
}

/**
 * @brief 确认使用当前目录
 * @param event LVGL 事件对象
 */
void FolderPickerConfirmClickedEventCallback(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  auto* state = static_cast<FilesViewState*>(lv_event_get_user_data(event));
  if (state == nullptr || state->root == nullptr ||
      state->current_path.empty()) {
    return;
  }
  if (state->picker_config.selected_callback) {
    state->picker_config.selected_callback(state->current_path.c_str());
  }
  CloseFolderPicker(state);
}

/**
 * @brief 创建文件夹选择页面底部确认按钮
 * @param parent 页面根对象
 * @param state 文件管理页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateFolderPickerAction(lv_obj_t* parent, FilesViewState* state) {
  if (parent == nullptr || state == nullptr ||
      !state->folder_picker_mode) {
    return true;
  }
  lv_obj_t* button = lv_button_create(parent);
  if (button == nullptr) {
    return false;
  }
  lv_obj_set_size(button, state->config.width - 56,
                  kFolderPickerButtonHeight);
  lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -15);
  lv_obj_set_style_radius(button, kFolderPickerButtonHeight / 2,
                          LV_PART_MAIN);
  lv_obj_set_style_bg_color(button,
      lv_color_hex(state->picker_config.action_color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
  if (!AddPressCancelOnLeave(button)) {
    lv_obj_delete(button);
    return false;
  }
  lv_obj_add_event_cb(button, FolderPickerConfirmClickedEventCallback,
                      LV_EVENT_CLICKED, state);
  const char* text = state->picker_config.action_text == nullptr
                         ? "Use this folder"
                         : state->picker_config.action_text;
  lv_obj_t* label = CreateLabel(button, text,
      lv_color_hex(state->picker_config.action_text_color), Font28());
  if (label == nullptr) {
    lv_obj_delete(button);
    return false;
  }
  lv_obj_center(label);
  return true;
}

/**
 * @brief 创建文件管理顶部工具栏
 * @param parent 父对象
 * @param state 文件管理页面状态
 * @return 创建成功返回 true，否则返回 false
 */
bool CreateHeader(lv_obj_t* parent, FilesViewState* state) {
  lv_obj_t* menu = state->folder_picker_mode
                       ? lv_button_create(parent)
                       : CreateIconButton(parent, icon::kMenu);
  if (menu == nullptr) {
    return false;
  }
  if (state->folder_picker_mode) {
    lv_obj_remove_style_all(menu);
    lv_obj_remove_flag(menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(menu, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_flag(menu, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(menu, 62, 62);
    lv_obj_set_pos(menu, 18, 66);
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_t* back_icon = CreateMaterialIcon(menu, icon::kArrowBack,
        lv_color_hex(kPrimaryTextColor), MaterialOutlineIconFont44());
    if (back_icon == nullptr) {
      return false;
    }
    lv_obj_align(back_icon, LV_ALIGN_CENTER, -4, 0);
  } else {
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_align(menu, LV_ALIGN_TOP_LEFT, kHeaderSidePadding - 8,
                 kHeaderTop - 2);
  }
  lv_obj_add_event_cb(menu,
      state->folder_picker_mode ? FolderPickerBackClickedEventCallback
                                : MenuButtonClickedEventCallback,
      LV_EVENT_CLICKED, state);

  lv_obj_t* title =
      CreateLabel(parent, FilesHeaderTitle(state),
                  lv_color_hex(kPrimaryTextColor), Font36());
  if (title == nullptr) {
    return false;
  }
  state->title_label = title;
  lv_obj_set_width(title, state->config.width - kHeaderTitleX - 32);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, kHeaderTitleX, kHeaderTop);

  lv_obj_t* subtitle = CreateLabel(parent, "Scanning SD card",
                                   lv_color_hex(kSecondaryTextColor), Font24());
  if (subtitle != nullptr) {
    state->subtitle_label = subtitle;
    lv_obj_set_width(subtitle, state->config.width - kHeaderTitleX - 32);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, kHeaderTitleX, kHeaderTop + 43);
  }
  return true;
}

}  // namespace

/**
 * @brief 创建普通文件管理或临时文件夹选择页面
 * @param parent 父对象
 * @param config 应用视图配置
 * @param picker_config 文件夹选择配置，普通文件管理传入 nullptr
 * @return 创建成功返回页面根对象，否则返回 nullptr
 */
lv_obj_t* CreateFilesViewInternal(lv_obj_t* parent,
    const AppViewConfig& config,
    const FolderPickerViewConfig* picker_config) {
  if (parent == nullptr || config.width <= 0 || config.height <= 0) {
    LogMessage(LogLevel::kWarning, __FILE__, __LINE__,
               "CreateFilesView received invalid input, parent=%p, width=%d, "
               "height=%d\n",
               parent, config.width, config.height);
    return nullptr;
  }

  auto* state = new FilesViewState{};
  state->config = config;
  if (picker_config != nullptr) {
    state->folder_picker_mode = true;
    state->picker_config = *picker_config;
  }
  lv_obj_t* root = lv_obj_create(parent);
  if (root == nullptr) {
    delete state;
    return nullptr;
  }
  state->root = root;
  lv_obj_set_size(root, config.width, config.height);
  lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(root, lv_color_hex(kBackgroundColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(root, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
  AddEdgeBackSwipeEvents(root, DirectoryEdgeBackEventCallback, state);
  lv_obj_add_event_cb(
      root,
      [](lv_event_t* event) {
        if (lv_event_get_code(event) != LV_EVENT_DELETE ||
            lv_event_get_target_obj(event) !=
                lv_event_get_current_target_obj(event)) {
          return;
        }
        auto* state =
            static_cast<FilesViewState*>(lv_event_get_user_data(event));
        if (state != nullptr) {
          state->root = nullptr;
          StopStorageDiscovery(state);
          StopStorageMonitor(state);
          delete state;
        }
      },
      LV_EVENT_DELETE, state);

  if (config.set_status_bar_visible) {
    config.set_status_bar_visible(true);
  }
  if (config.set_status_bar_text_color) {
    config.set_status_bar_text_color(kPrimaryTextColor);
  }

  state->content = lv_obj_create(root);
  if (state->content == nullptr) {
    lv_obj_delete(root);
    return nullptr;
  }
  MakeTransparent(state->content);
  lv_obj_remove_flag(state->content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(state->content, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(state->content, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(state->content, config.width, config.height);
  lv_obj_set_pos(state->content, 0, 0);

  if (!CreateHeader(root, state)) {
    lv_obj_delete(root);
    return nullptr;
  }
  if (!CreateFolderPickerAction(root, state)) {
    lv_obj_delete(root);
    return nullptr;
  }

  StartStorageDiscovery(state);
  StartStorageMonitor(state);
  return root;
}

/**
 * @brief 创建文件管理应用界面
 * @param parent 父对象
 * @param app_entry 应用条目
 * @param config 应用视图配置
 * @return 创建成功返回文件管理根对象，否则返回 nullptr
 */
lv_obj_t* CreateFilesView(lv_obj_t* parent, const app::AppEntry& app_entry,
                          const AppViewConfig& config) {
  static_cast<void>(app_entry);
  return CreateFilesViewInternal(parent, config, nullptr);
}

/**
 * @brief 创建临时文件夹选择页面并播放进入动画
 * @param parent 父对象
 * @param config 文件夹选择页面配置
 * @return 创建成功返回页面根对象，否则返回 nullptr
 */
lv_obj_t* CreateFolderPickerView(
    lv_obj_t* parent, const FolderPickerViewConfig& config) {
  lv_obj_t* picker = CreateFilesViewInternal(
      parent, config.view_config, &config);
  if (picker != nullptr) {
    StartSlideLeftWindowTransition(picker, config.view_config.width,
        config.animation_ms, nullptr, nullptr);
  }
  return picker;
}

}  // namespace lilygo_box::ui

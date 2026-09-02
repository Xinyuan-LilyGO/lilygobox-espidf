#include "ui/widgets/brand_icon.h"

#include <algorithm>

#include "ui/resources/images/image_assets.h"
#include "ui/theme/theme_provider.h"

namespace lilygo_box::ui {

lv_obj_t* CreateLilygoBoxBrandIcon(lv_obj_t* parent, int size) {
  if (parent == nullptr || size <= 0) {
    return nullptr;
  }

  lv_obj_t* tile = lv_obj_create(parent);
  if (tile == nullptr) {
    return nullptr;
  }
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(tile, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(tile, size, size);
  lv_obj_set_style_bg_color(tile,
      lv_color_hex(theme::FixedColors().brand_icon_background), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(tile, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(tile, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(tile, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(tile, size * 22 / 100, LV_PART_MAIN);

  lv_obj_t* image = lv_image_create(tile);
  if (image == nullptr) {
    lv_obj_delete(tile);
    return nullptr;
  }
  lv_image_set_src(image, &lilygobox_inner_icon_112x112);
  constexpr int kSourceSize = 112;
  const int content_size = std::max(1, size * 84 / 100);
  const int image_scale =
      std::max(1, content_size * LV_SCALE_NONE / kSourceSize);
  lv_image_set_scale(image, static_cast<uint32_t>(image_scale));
  lv_obj_center(image);
  return tile;
}

}  // namespace lilygo_box::ui

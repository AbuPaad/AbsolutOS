#include <string.h>

#include "screens.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

#include "../../../ThemeC.h"

objects_t settings_objects;

void create_screen_settings() {
    // Screen
    lv_obj_t *obj = lv_obj_create(0);
    settings_objects.settings = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 320, 156);
    // R2 (agent-added default): export had no colour calls
    lv_obj_set_style_bg_color(obj, lv_color_hex(numos_theme_bg()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    {
        lv_obj_t *parent_obj = obj;
        // Title label
        {
            lv_obj_t *o = lv_label_create(parent_obj);
            lv_obj_set_pos(o, 12, 4);
            lv_obj_set_size(o, 200, LV_SIZE_CONTENT);
            lv_label_set_text_static(o, "Settings");
            // R3: body label -> fontUi()
            lv_obj_set_style_text_font(o, numos_font_ui(), LV_PART_MAIN | LV_STATE_DEFAULT);
            // R2 (agent-added default): primary text token
            lv_obj_set_style_text_color(o, lv_color_hex(numos_theme_text()), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        // Container (transparent, scrollable)
        {
            lv_obj_t *o = lv_obj_create(parent_obj);
            lv_obj_set_pos(o, 10, 20);
            lv_obj_set_size(o, 300, 136);
            lv_obj_set_style_pad_left(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_flag(o, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scroll_dir(o, LV_DIR_VER);
            lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
        }
    }

    tick_screen_settings();
}

void tick_screen_settings() {
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t settings_tick_screen_funcs[] = {
    tick_screen_settings,
};
void settings_tick_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < 1) {
        settings_tick_screen_funcs[screen_index]();
    }
}
void settings_tick_screen_by_id(enum ScreensEnum screenId) {
    settings_tick_screen(screenId - 1);
}

void settings_create_screens() {
    // R1: LVGL default theme install removed
    create_screen_settings();
}

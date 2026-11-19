#include <string.h>

#include "screens.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

#include "../../../ThemeC.h"     // C-linkage wrappers over NumOS ThemeManager/ThemeFonts

objects_t objects;

//
// Event handlers
//

lv_obj_t *tick_value_change_obj;

//
// Screens
//

void create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    // Resolution corrected from export's 800x480 to NumOS canvas 320x156
    // (docs/DESIGN_DIMENSIONS.md). Source export was 800x480 — re-export at 320x156.
    lv_obj_set_size(obj, 320, 156);
    // R2 (agent-added default): export had no colour calls; screen bg set to bg token
    // so the screen is not transparent/black under the removed default theme.
    lv_obj_set_style_bg_color(obj, lv_color_hex(numos_theme_bg()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            // lbl_sample (unnamed in export; named here for clarity)
            lv_obj_t *obj = lv_label_create(parent_obj);
            lv_obj_set_pos(obj, 10, 10);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "Hello, world!");
            // R3: body label -> fontUi()
            lv_obj_set_style_text_font(obj, numos_font_ui(), LV_PART_MAIN | LV_STATE_DEFAULT);
            // R2 (agent-added default): primary text token
            lv_obj_set_style_text_color(obj, lv_color_hex(numos_theme_text()), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    tick_screen_main();
}

void tick_screen_main() {
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_main,
};
void tick_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < 1) {
        tick_screen_funcs[screen_index]();
    }
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen(screenId - 1);
}

//
// create_screens(): R1 applied — LVGL default theme install removed.
// NumOS owns theming via ThemeManager (activate() runs in SystemApp::begin()).
//

void create_screens() {
    create_screen_main();
}

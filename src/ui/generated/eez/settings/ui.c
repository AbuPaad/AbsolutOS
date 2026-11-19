#include "ui.h"
#include "screens.h"
#include "actions.h"
#include "vars.h"

#include <string.h>

static int16_t currentScreen = -1;

static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index == -1) {
        return 0;
    }
    return ((lv_obj_t **)&settings_objects)[index];
}

void settings_loadScreen(enum ScreensEnum screenId) {
    currentScreen = screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(currentScreen);
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void settings_ui_init() {
    settings_create_screens();
    settings_loadScreen(SCREEN_ID_SETTINGS);
}

void settings_ui_tick() {
    settings_tick_screen(currentScreen);
}

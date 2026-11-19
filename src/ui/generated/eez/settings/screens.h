#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_SETTINGS = 1,
    _SCREEN_ID_LAST = 1
};

typedef struct _objects_t {
    lv_obj_t *settings;
} objects_t;

extern objects_t settings_objects;

void create_screen_settings();
void tick_screen_settings();

void settings_tick_screen_by_id(enum ScreensEnum screenId);
void settings_tick_screen(int screen_index);

void settings_create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/

/*
 * eez_adapter_settings.h
 * Hand-written glue between the EEZ-generated Settings screen and NumOS.
 * Generated files are never edited; all manual integration lives here.
 *
 * STATIC LAYOUT ONLY: container, template row, hint strip.
 * Dynamic behaviour (row loop, focus, view switching, brightness slider,
 * portal polling, Wifi/Update sub-views) stays in SettingsApp.cpp.
 */
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void eez_settings_show();
void eez_settings_tick();

#ifdef __cplusplus
}
#endif

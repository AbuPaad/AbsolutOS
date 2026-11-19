/*
 * eez_adapter_numosshi.h
 * Hand-written glue between the EEZ-generated screen (src/ui/generated/eez/numosshi/)
 * and NumOS. Generated files are never edited; all manual integration lives here.
 *
 * This is a HELLO-WORLD staging validation screen. It is not a real app.
 */
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Build the EEZ screen if needed and load it. Idempotent.
void eez_numosshi_show();

// Optional tick (no-op for this screen).
void eez_numosshi_tick();

#ifdef __cplusplus
}
#endif

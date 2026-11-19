#include "eez_adapter_settings.h"

#include "../generated/eez/settings/ui.h"
#include "../generated/eez/settings/screens.h"

static bool s_built = false;

void eez_settings_show() {
    if (!s_built) {
        settings_ui_init();
        s_built = true;
    }
    settings_loadScreen(SCREEN_ID_SETTINGS);
}

void eez_settings_tick() {
    settings_ui_tick();
}

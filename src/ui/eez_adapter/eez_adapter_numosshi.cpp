#include "eez_adapter_numosshi.h"

#include "../generated/eez/numosshi/ui.h"
#include "../generated/eez/numosshi/screens.h"

#include "../../ui/ThemeManager.h"

static bool s_built = false;

void eez_numosshi_show() {
    if (!s_built) {
        // Lazy build (R5): only one screen, but keep the pattern.
        ui_init();
        s_built = true;
    }
    loadScreen(SCREEN_ID_MAIN);
}

void eez_numosshi_tick() {
    ui_tick();
}

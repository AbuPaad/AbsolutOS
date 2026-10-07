/*
 * NeoCalculator - NumOS
 * Copyright (C) 2026 Juan Ramon
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/**
 * Theme.cpp
 * Built-in theme table (doc 02/03/13). The `numos` initializer reproduces
 * today's exact look: STIX math fonts + today's palette/radii, so nothing
 * visibly changes when the theme system activates. `casio` is the runtime
 * alternative. A new skin = one initializer + one table row.
 */

#include "Theme.h"

#include "ThemeManager.h"

#include "../fonts/StixMathFont.h"
#include "../fonts/CasioMathFont.h"
#include "nav/InteractionModel.h"

namespace ui {

/**
 * Dark-app surfaces for the numos theme.
 *
 * These reproduce, exactly, the palettes the AI and Game Boy apps hardcoded
 * before this table existed — they are dark for legibility, and the numos
 * tokens (white bg / black ink) cannot express them. Keeping the values here
 * means the numos rendering of both apps is byte-identical to before, while the
 * literals leave the apps.
 */
const AppSurface kNUMOSSurfaces[] = {
    {
        /* appId        */ appid::kGameBoy,
        /* bg           */ 0x000000,   // black letterbox
        /* pane         */ 0x000000,
        /* row          */ 0x1C1C1C,
        /* rowFocus     */ 0x1565C0,
        /* text         */ 0xFFFFFF,
        /* textDim      */ 0x9E9E9E,
        /* title        */ 0xFFFFFF,
        /* textOnFocus  */ 0xFFFFFF,
        /* accent       */ 0x1565C0,
        /* iconInk      */ 0xFFFFFF,   // numos rows are dark: the mark stays white
        /* radiusRow    */ 4,
        /* radiusPane   */ 4,
        /* borderWidth  */ 0,
    },
    {
        /* appId        */ appid::kAi,
        /* bg           */ 0x000000,
        /* pane         */ 0x101418,   // the Ask input box
        /* row          */ 0x000000,
        /* rowFocus     */ 0x1565C0,
        /* text         */ 0xDDDDDD,
        /* textDim      */ 0x8A8A8A,
        /* title        */ 0xCCCCCC,
        /* textOnFocus  */ 0xFFFFFF,
        /* accent       */ 0x1565C0,
        /* iconInk      */ 0xFFFFFF,   // numos rows are dark: the mark stays white
        /* radiusRow    */ 4,
        /* radiusPane   */ 6,          // the Ask box is rounder than a row
        /* borderWidth  */ 1,
    },
    {
        /* appId        */ appid::kGrapher,
        /* bg           */ 0xF5F5F5,   // COL_BG (the numos grapher screen bg)
        /* pane         */ 0xFFFFFF,
        /* row          */ 0xFFFFFF,   // expr pill
        /* rowFocus     */ 0x4A90E2,   // selected pill (blue)
        /* text         */ 0x000000,
        /* textDim      */ 0x999999,
        /* title        */ 0x000000,
        /* textOnFocus  */ 0xFFFFFF,
        /* accent       */ 0x4A90E2,
        /* iconInk      */ 0xFFFFFF,   // numos rows are dark: the mark stays white
        /* radiusRow    */ 6,
        /* radiusPane   */ 6,
        /* borderWidth  */ 1,
    },
    { -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },   // terminator
};

// `numos` — must stay byte-identical to today. UI font points at the LVGL
// default (montserrat 14); the math set is the STIX set (18/12/8, ems 18/12/8).
const Theme kNUMOS = {
    /* id */         "numos",
    /* name */       "NumOS",
    /* layout */     Layout::NumOS,
    /* interaction */ &kNumOSInteraction,

    /* bg            */ 0xFFFFFF,   // pure white (COLOR_BACKGROUND)
    /* bgCanvas      */ 0xFFFFFF,
    /* bgPane        */ 0xF5F5F5,   // soft grey cards/rows
    /* bgHeader      */ 0xFF9900,   // NumWorks orange status bar
    /* bgToolbar     */ 0xFF9900,
    /* text          */ 0x000000,   // COLOR_TEXT
    /* textDim       */ 0x808080,
    /* textOnAccent  */ 0x000000,   // COLOR_HEADER_TEXT (black on orange)
    /* accent        */ 0xFF9E00,   // COLOR_PRIMARY #FFB531 (RGB565 0xFD20)
    /* accentSecondary */ 0xFFB531,
    /* border        */ 0xD0D0D0,
    /* focus         */ 0xE3F2FD,   // light-blue focused row fill
    /* focusBorder   */ 0x4A90D9,
    /* stepHighlight */ 0x1565C0,   // math intermediate step (unchanged)
    /* result        */ 0xE05500,   // math final result      (unchanged)
    /* danger        */ 0xB71C1C,
    /* plot[4]       */ { 0x1565C0, 0xE05500, 0x2E7D32, 0x6A1B9A },
    /* iconInk       */ 0xFFFFFF,   // the provider marks are a white glyph on
                                   // numos' dark rows (unchanged pixels)

    /* cornerRadius  */ 6,          // modern rounded chrome (Settings rows use 6)
    /* borderWidth   */ 0,
    /* shadows       */ true,
    /* graphLineWidth*/ 1,          // numos: hairline curves (unchanged)

    /* fontUi        */ &lv_font_montserrat_14,
    /* fontUiSmall   */ &lv_font_montserrat_12,
    /* fontUiXSmall  */ &lv_font_montserrat_10,
    /* fontDisplay   */ &lv_font_montserrat_20,
    /* fontMono      */ &lv_font_unscii_8,

    /* mathFonts */ {
        &stix_math_18, &stix_math_12, &stix_math_8,
        18, 12, 8,
    },

    /* appSurfaces */ kNUMOSSurfaces,   // AI + Game Boy keep their dark surface
};

// `casio` — the runtime alternative (docs 02/05/13).
const Theme kCASIO = {
    /* id */         "casio",
    /* name */       "Casio ClassWiz",
    /* layout */     Layout::Casio,
    /* interaction */ &kCasioInteraction,

    /* bg            */ 0x78864E,   // #78864E
    /* bgCanvas      */ 0x78864E,
    /* bgPane        */ 0x78864E,
    /* bgHeader      */ 0x78864E,
    /* bgToolbar     */ 0x78864E,
    /* text          */ 0x000000,   // black for now
    /* textDim       */ 0x3E6B2F,
    /* textOnAccent  */ 0xFFFFFF,
    /* accent        */ 0x333A21,   // #333A21
    /* accentSecondary */ 0x1B5E20,
    /* border        */ 0x2E5016,   // visible dark hairline (crude look)
    /* focus         */ 0x1E88CA,
    /* focusBorder   */ 0x0B3D91,
    /* stepHighlight */ 0x0B3D91,
    /* result        */ 0x001405,
    /* danger        */ 0xB71C1C,
    /* plot[4]       */ { 0x0B3D91, 0xC62828, 0x2E7D32, 0x6A1B9A },
    /* iconInk       */ 0x000000,   // black mark: the LCD rows are light

    /* cornerRadius  */ 0,          // crude per doc 02
    /* borderWidth   */ 1,
    /* shadows       */ false,
    /* graphLineWidth*/ 2,          // thicker: the fitted canvas is ~1/2 the OEM area

    /* fontUi        */ &casio_math_18,  // one face everywhere (operator call 2026-10-05):
    /* fontUiSmall   */ &casio_math_12,  // the CC-BY-SA math face replaces the retracted
    /* fontUiXSmall  */ &casio_math_12,  // casio_ui_* UI face. Its space glyph (U+0020) is
    /* fontDisplay   */ &casio_math_18,  // required for UI text; no 26pt rung exists, so
    /* fontMono      */ &lv_font_unscii_8,// display collapses to 18 (mono stays unscii).

    /* mathFonts */ {
        &casio_math_18, &casio_math_12, &casio_math_8,
        18, 12, 8,
    },

    /* appSurfaces */ nullptr,   // casio overrides nothing: apps inherit its tokens
};

const Theme* const kBuiltinThemes[] = {
    &kNUMOS,   // ThemeId::NumOS
    &kCASIO,   // ThemeId::Casio
};

const Theme* themeFor(ThemeId id) {
    const auto index = static_cast<uint8_t>(id);
    if (index >= sizeof(kBuiltinThemes) / sizeof(kBuiltinThemes[0])) {
        return &kNUMOS;  // unknown id -> numos default
    }
    return kBuiltinThemes[index];
}

AppColours appSurface(int appId) {
    const Theme& th = ThemeManager::instance().current();

    if (th.appSurfaces) {
        for (const AppSurface* s = th.appSurfaces; s->appId >= 0; ++s) {
            if (s->appId != appId) continue;
            return AppColours{
                s->bg, s->pane, s->row, s->rowFocus,
                s->text, s->textDim, s->title, s->textOnFocus, s->accent,
                s->iconInk,
                s->radiusRow, s->radiusPane, s->borderWidth,
            };
        }
    }

    // Inherit: the theme's own tokens. `rowFocus` deliberately mirrors `row` —
    // whether a focused row paints a fill at all is the interaction model's
    // business (`focusFill`), not the palette's.
    return AppColours{
        th.bg, th.bgPane, th.bgPane, th.bgPane,
        th.text, th.textDim, th.text, th.accent, th.accent, th.iconInk,
        th.cornerRadius, th.cornerRadius, th.borderWidth,
    };
}

} // namespace ui
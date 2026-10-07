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
 * InteractionModel.cpp (doc 12)
 * The two built-in behaviour profiles. The keymap stays shared; only the shape
 * of navigation differs between themes.
 */

#include "InteractionModel.h"

namespace ui {

const InteractionModel kNumOSInteraction = {
    /* id         */ InteractionId::NumOS,
    /* name       */ "NumOS",
    /* launcher   */ LauncherStyle::CardGrid,
    /* focus      */ FocusTopology::Grid2D,
    /* pages      */ PageStyle::Scroll,
    /* wrap       */ true,   // lv_group 2-D focus wraps around (today's behaviour)
    /* softkeyRow */ false,
    /* focusFill  */ true,   // the numos look: a filled focus card
    /* numbered   */ false,
    /* chevron    */ false,
    /* splitGraph */ false,  // numos: the grapher keeps its full-width tabs
    /* back       */ BackPolicy::ToLauncher,
    /* tallRows   */ false,  // shared 26 px rows, 1x marks, list fills the box
};

const InteractionModel kCasioInteraction = {
    /* id         */ InteractionId::Casio,
    /* name       */ "Casio",
    /* launcher   */ LauncherStyle::MenuList,
    /* focus      */ FocusTopology::Linear,
    /* pages      */ PageStyle::PageTurn,
    /* wrap       */ false,  // p3 RIGHT and p1 LEFT stay put — the edge arrows
                             // disappear at the ends, so paging must not wrap
    /* softkeyRow */ true,
    /* focusFill  */ false,  // casio: focus is an ink change, never a rectangle
    /* numbered   */ true,   // "1:COMP" — the launcher numbers its slots, so the
                             // AI's lists number theirs the same way
    /* chevron    */ true,   // corner up/down arrow on a scrollable list
    /* splitGraph */ true,   // grapher: list/table on the left, live graph right
    /* back       */ BackPolicy::ToLauncher,
    /* tallRows   */ true,   // 29 px rows, 1.25x marks, three whole rows per viewport
};

} // namespace ui
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
 * InteractionModel.h (doc 12)
 * A reusable behaviour profile the theme references: the *shape of navigation*
 * (launcher style, focus topology, paging, softkeys, back). The keymap is
 * shared across themes; the profile is navigation shape only. Profiles are
 * reusable — many themes can point at the same one — and they are NOT keymaps.
 */

#pragma once

#include <cstdint>

namespace ui {

enum class InteractionId : uint8_t {
    NumOS = 0,
    Casio = 1,
};

/** How the launcher presents its apps. */
enum class LauncherStyle : uint8_t {
    CardGrid,   // NumOS: 3-column icon grid
    MenuList,   // Casio: linear list
};

/** How focus moves through the active screen. */
enum class FocusTopology : uint8_t {
    Grid2D,     // NumOS: up/down/left/right grid navigation
    Linear,     // Casio: sequential next/prev
};

/** How a long list pages. */
enum class PageStyle : uint8_t {
    Scroll,     // NumOS: continuous scrolling list
    PageTurn,   // Casio: discrete pages
};

/** What a softkey / system BACK does at the top of an app stack. */
enum class BackPolicy : uint8_t {
    GoBack,     // pop the previous view if one exists
    ToLauncher, // always return to the launcher
};

/**
 * The behaviour profile bound to a theme (doc 03: activate() sets the theme AND
 * its profile in the same call so the two never disagree).
 */
struct InteractionModel {
    InteractionId  id;
    const char*    name;
    LauncherStyle  launcher;
    FocusTopology  focus;
    PageStyle      pages;
    bool           wrap;       // focus wraps at the screen edges. numos' lv_group
                               // grid wraps (2-D, wrap-around); casio's page-turn
                               // does NOT — the edge arrows vanish at the ends, so
                               // p3 RIGHT and p1 LEFT stay put (NAV_GRAPH appmenu table).
    bool           softkeyRow; // this profile shows a bottom softkey row (doc 12)
    bool           focusFill;  // a focused row paints a fill (numos) — casio shows
                               // focus by INK ONLY: no card, no ring (SPEC-stageC C2)
    bool           numberedSlots; // list rows carry their 1-based position as an
                               // "N:" prefix, as the casio launcher numbers its
                               // slots (MainMenu's "%d:%s"). numos: false.
    bool           scrollChevron; // a scrollable list shows the corner arrow
                               // (kCasioArrowUp/Down) that calc uses for history.
                               // numos: false — it scrolls silently.
    bool           splitGraph; // the grapher presents a two-pane split (numbered
                               // function list / value table on the left, a live
                               // graph on the right) instead of the numos
                               // tab-per-view screen. numos: false — its three
                               // tabs stay full-width. A future skin reuses this
                               // with no app change: shape from the profile, never
                               // a theme-id branch.
    BackPolicy     back;
    // Future doc-12 fields extend here; changing this POD bumps every
    // initializer, mirroring the "struct is the spec" rule.
};

// Built-in profiles (defined in InteractionModel.cpp). Many themes may point
// at the same profile; this is not a keymap.
extern const InteractionModel kNumOSInteraction;
extern const InteractionModel kCasioInteraction;

} // namespace ui
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
 * ThemeManager.h (doc 03)
 * The one object that owns the active theme and turns it into something LVGL
 * can apply. Everything else asks it — never a palette directly.
 *
 *   current()   -> const Theme&  (read tokens; safe on the draw path)
 *   activate()  -> the ONLY write path: swaps current(), binds the theme's
 *                  InteractionModel, rebuilds the LVGL theme and persists.
 *   id()        -> typed ThemeId (single owner; never a bare global int)
 *   interaction()-> the active behaviour profile (doc 12)
 */

#pragma once

#include "Theme.h"
#include "nav/InteractionModel.h"
#include <cstdint>

namespace ui {

class ThemeManager {
public:
    static ThemeManager& instance();

    const Theme& current() const;          // POD, read-only — safe on the draw path
    void         activate(ThemeId id);     // set theme + interaction + LVGL theme, persist
    ThemeId      id() const { return _id; }
    const InteractionModel& interaction() const;  // active behaviour profile (doc 12)

    /**
     * Constructs a lv_theme_t from the token palette and installs it on the
     * default display. Callers normally go through activate(); this exists as
     * the doc-03 building block and for tests.
     */
    lv_theme_t*  buildLvglTheme(const Theme& theme);

private:
    ThemeManager();

    ThemeId               _id;
    const InteractionModel* _interaction;  // bound to _id in activate()
    lv_theme_t*           _lvTheme;
};

} // namespace ui
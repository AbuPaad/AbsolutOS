/*
 * NumOS — HelpOverlay.h
 * Copyright (C) 2026 Juan Ramon
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The single help affordance (replaces every bottom hint bar).
 *
 *   HelpChip     — a 16x14 "?" chip that floats bottom-right of a given parent.
 *                  No state, no callbacks: it is a signpost, not a button.
 *   HelpOverlay  — a full-canvas page listing the keys the active screen accepts.
 *                  Opened globally by SHIFT+7, closed by AC.
 *
 * Neither object reads or mutates the active app; the overlay is owned by
 * SystemApp so every screen inherits it without per-app code.
 */

#pragma once

#include <lvgl.h>

namespace ui {

class HelpChip {
public:
    /// Draw the chip at (x, y) inside @p parent. Coordinates are parent-relative.
    static void create(lv_obj_t* parent, int x, int y);

    static constexpr int W = 16;
    static constexpr int H = 14;
};

class HelpOverlay {
public:
    /// Build the overlay on @p parent (the active screen) for the given app id.
    void open(lv_obj_t* parent, int appId);
    void close();
    bool isOpen() const { return _panel != nullptr; }

private:
    lv_obj_t* _panel = nullptr;
};

} // namespace ui

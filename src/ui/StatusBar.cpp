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
 * StatusBar.cpp — Barra de estado global LVGL (18 px)
 *
 * Implementación:
 *   · Reloj: <time.h> + oscilador interno.  En ESP32 se arranca con
 *     una hora arbitraria (00:00) a menos que haya NTP; el usuario
 *     puede ajustarla desde Settings.
 *   · Modificadores: lee KeyboardManager::instance().indicatorText()
 *   · Ángulo: lee vpam::g_angleMode
 *   · Batería: dibuja un mini icono proporcional al % cargado
 */

#include "StatusBar.h"
#include "generated/StatusBarIcons.generated.h"
#include "ThemeFonts.h"
#include "../input/KeyboardManager.h"
#include "../math/MathEvaluator.h"   // g_angleMode, AngleMode

#include <cstdio>
#include <cstring>
#include <ctime>

#ifdef ARDUINO
#include <sys/time.h>   // settimeofday (ESP-IDF)
#endif

namespace ui {

// Image descriptors for the three generated masks. Built here (not in the generated
// header) so the header stays pure data and stays C++17-safe: no designated
// initialisers of vtable-less structs, no extern "C" wrapper around a namespace.
static lv_image_dsc_t makeBatteryDsc(const uint8_t* map, uint32_t size) {
    lv_image_dsc_t d{};
    d.header.magic  = LV_IMAGE_HEADER_MAGIC;
    d.header.cf     = LV_COLOR_FORMAT_A8;
    d.header.w      = ui::kBatteryIconW;
    d.header.h      = ui::kBatteryIconH;
    d.header.stride = ui::kBatteryIconW;
    d.data_size     = size;
    d.data          = map;
    return d;
}
static const lv_image_dsc_t s_batLow  = makeBatteryDsc(ui::kLowMap,  sizeof(ui::kLowMap));
static const lv_image_dsc_t s_batHalf = makeBatteryDsc(ui::kHalfMap, sizeof(ui::kHalfMap));
static const lv_image_dsc_t s_batFull = makeBatteryDsc(ui::kFullMap, sizeof(ui::kFullMap));


#ifdef NATIVE_SIM
StatusBar* StatusBar::s_active = nullptr;

const char* StatusBar::debugActiveAngleText() {
    if (s_active && s_active->_angleLabel)
        return lv_label_get_text(s_active->_angleLabel);
    return "";
}
#endif

// ════════════════════════════════════════════════════════════════════════════
// create() — Construye la jerarquía de widgets LVGL
// ════════════════════════════════════════════════════════════════════════════

void StatusBar::create(lv_obj_t* parent) {
    if (_bar) return;  // ya creada

    // ── Contenedor principal ──────────────────────────────────────────
    _bar = lv_obj_create(parent);
    lv_obj_set_size(_bar, SCREEN_W, HEIGHT);
    lv_obj_set_pos(_bar, 0, 0);
    lv_obj_set_style_bg_color(_bar, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_bar, 0, LV_PART_MAIN);
    lv_obj_remove_flag(_bar, LV_OBJ_FLAG_SCROLLABLE);

    // ── Reloj (izquierda) ─────────────────────────────────────────────
    _clockLabel = lv_label_create(_bar);
    lv_label_set_text(_clockLabel, "00:00");
    lv_obj_set_style_text_font(_clockLabel, ui::fontUiSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_clockLabel, lv_color_hex(COL_TEXT), LV_PART_MAIN);
    lv_obj_align(_clockLabel, LV_ALIGN_LEFT_MID, 6, 0);

    // ── Título de la app (centrado) ───────────────────────────────────
    _titleLabel = lv_label_create(_bar);
    lv_label_set_text(_titleLabel, "");
    lv_obj_set_style_text_font(_titleLabel, ui::fontUiSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_titleLabel, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(_titleLabel, LV_ALIGN_CENTER, 0, 0);

    // ── Indicador de modificador (a la derecha del título) ────────────
    _modLabel = lv_label_create(_bar);
    lv_label_set_text(_modLabel, "");
    lv_obj_set_style_text_font(_modLabel, ui::fontUiSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_modLabel, lv_color_hex(0xFFD700), LV_PART_MAIN);  // Amarillo dorado
    lv_obj_align(_modLabel, LV_ALIGN_RIGHT_MID, -80, 0);

    // ── Modo angular (antes de batería) ───────────────────────────────
    _angleLabel = lv_label_create(_bar);
    lv_label_set_text(_angleLabel, "RAD");
    lv_obj_set_style_text_font(_angleLabel, ui::fontUiSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_angleLabel, lv_color_hex(COL_TEXT), LV_PART_MAIN);
    lv_obj_align(_angleLabel, LV_ALIGN_RIGHT_MID, -38, 0);

    // ── Batería — Carbon icon (low/half/full) as one A8 mask, recoloured by level ──
    _batIcon = lv_image_create(_bar);
    lv_image_set_src(_batIcon, &s_batFull);
    lv_obj_align(_batIcon, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_style_image_recolor(_batIcon, lv_color_hex(COL_BAT), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(_batIcon, LV_OPA_COVER, LV_PART_MAIN);

    // ── Línea separadora inferior (1 px) ──────────────────────────────
    _separator = lv_obj_create(parent);
    lv_obj_set_size(_separator, SCREEN_W, 1);
    lv_obj_set_pos(_separator, 0, HEIGHT);
    lv_obj_set_style_bg_color(_separator, lv_color_hex(COL_SEP), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_separator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_separator, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_separator, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_separator, 0, LV_PART_MAIN);
    lv_obj_remove_flag(_separator, LV_OBJ_FLAG_SCROLLABLE);

    // ── Inicialización del reloj ──────────────────────────────────────
#ifdef ARDUINO
    // Si no se ha establecido la hora aún, arrancar en 00:00
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec < 1000) {
        // Hora no configurada → establecer a 00:00 del 1-ene-2026
        struct tm t = {};
        t.tm_year = 126;  // 2026 - 1900
        t.tm_mon  = 0;
        t.tm_mday = 1;
        t.tm_hour = 0;
        t.tm_min  = 0;
        t.tm_sec  = 0;
        time_t epoch = mktime(&t);
        struct timeval newTv = { .tv_sec = epoch, .tv_usec = 0 };
        settimeofday(&newTv, nullptr);
    }
#endif

#ifdef NATIVE_SIM
    s_active = this;   // la barra recién creada pertenece a la app activa
#endif

    // Refresco inicial
    update();
}

// ════════════════════════════════════════════════════════════════════════════
// destroy()
// ════════════════════════════════════════════════════════════════════════════

void StatusBar::destroy() {
    // Los widgets LVGL son hijos de la pantalla padre;
    // se destruyen con ella. Sólo limpiamos punteros.
#ifdef NATIVE_SIM
    if (s_active == this) s_active = nullptr;
#endif
    _bar        = nullptr;
    _clockLabel = nullptr;
    _titleLabel = nullptr;
    _modLabel   = nullptr;
    _angleLabel = nullptr;
    _batIcon    = nullptr;
    _separator  = nullptr;
}

void StatusBar::resetPointers() {
    destroy();  // idéntico: sólo nulifica punteros, no llama lv_obj_delete
}

// ════════════════════════════════════════════════════════════════════════════
// setTitle()
// ════════════════════════════════════════════════════════════════════════════

void StatusBar::setTitle(const char* title) {
    if (_titleLabel) {
        lv_label_set_text(_titleLabel, title);
        lv_obj_align(_titleLabel, LV_ALIGN_CENTER, 0, 0);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// setBatteryLevel()
// ════════════════════════════════════════════════════════════════════════════

void StatusBar::setBatteryLevel(uint8_t pct) {
    if (pct > 100) pct = 100;
    _batLevel = pct;
    updateBatteryIcon();
}

// ════════════════════════════════════════════════════════════════════════════
// update() — Actualización global de todos los indicadores
// ════════════════════════════════════════════════════════════════════════════

void StatusBar::update() {
    if (!_bar) return;
    updateClock();
    updateModifier();
    updateAngleMode();
    updateBatteryIcon();
}

// ════════════════════════════════════════════════════════════════════════════
// updateClock() — Lee <time.h> y actualiza el label
// ════════════════════════════════════════════════════════════════════════════

void StatusBar::updateClock() {
    if (!_clockLabel) return;

    time_t now = time(nullptr);
    struct tm tmBuf;

#ifdef ARDUINO
    localtime_r(&now, &tmBuf);
#else
    // Desktop/test fallback
    #ifdef _WIN32
    localtime_s(&tmBuf, &now);
    #else
    localtime_r(&now, &tmBuf);
    #endif
#endif

    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", tmBuf.tm_hour, tmBuf.tm_min);
    lv_label_set_text(_clockLabel, buf);
}

// ════════════════════════════════════════════════════════════════════════════
// updateModifier() — Lee KeyboardManager y actualiza el label
// ════════════════════════════════════════════════════════════════════════════

void StatusBar::updateModifier() {
    if (!_modLabel) return;

    auto& km = vpam::KeyboardManager::instance();
    const char* txt = km.indicatorText();
    lv_label_set_text(_modLabel, txt);
}

// ════════════════════════════════════════════════════════════════════════════
// updateAngleMode() — Lee g_angleMode y actualiza el label
// ════════════════════════════════════════════════════════════════════════════

void StatusBar::updateAngleMode() {
    if (!_angleLabel) return;

    const char* txt = (vpam::g_angleMode == vpam::AngleMode::DEG) ? "DEG" : "RAD";
    lv_label_set_text(_angleLabel, txt);
}

// ════════════════════════════════════════════════════════════════════════════
// updateBatteryIcon() — Ajusta el relleno del icono de batería
// ════════════════════════════════════════════════════════════════════════════

void StatusBar::updateBatteryIcon() {
    if (!_batIcon) return;

    // One icon per level band; the shape carries the state, the tint carries urgency.
    if (_batLevel > 66)       lv_image_set_src(_batIcon, &s_batFull);
    else if (_batLevel > 33)  lv_image_set_src(_batIcon, &s_batHalf);
    else                      lv_image_set_src(_batIcon, &s_batLow);

    uint32_t col = (_batLevel > 50) ? COL_BAT
                 : (_batLevel > 20) ? 0xFFCC00u
                                    : 0xFF4444u;
    lv_obj_set_style_image_recolor(_batIcon, lv_color_hex(col), LV_PART_MAIN);
}

} // namespace ui


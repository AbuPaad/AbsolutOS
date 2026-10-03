/*
 * CasioSlots.generated.h — GENERATED FILE, DO NOT EDIT BY HAND.
 *
 * Source of truth: the §1 slot table in
 *   ../../../sserialprintthing/casio-cpp-transplant/IMPLEMENTATION.md
 * Regenerate:      python3 scripts/gen_casio_slots.py
 *
 * Edit the markdown table, not this file. The Casio launcher is positional
 * (N:LABEL); `id` is the real MainMenu::APPS[] app id, so launching still goes
 * through the one launch callback and APPS[] stays the single source of truth
 * for app identity. numos' order/names/pixels are not affected.
 */

#pragma once

#include <cstdint>

namespace ui {

struct CasioSlot {
    uint8_t     id;     // MainMenu::APPS[] app id
    const char* label;  // N:LABEL, <= 5 chars
};

inline constexpr CasioSlot kCasioSlots[] = {
    // ── page 1 ──
    {   0, "COMP " },   // Calculation
    {   4, "STAT " },   // Statistics
    {   1, "GRAPH" },   // Grapher
    {   3, "C'LUS" },   // Calculus
    {   2, "EQN  " },   // Equations
    {   5, "PROB " },   // Probability
    {   6, "REGR " },   // Regression
    {   7, "S'QNC" },   // Sequences
    // ── page 2 ──
    {   8, "PY   " },   // Python
    {   9, "M'TRX" },   // Matrices
    {  10, "SET  " },   // Settings
    {  11, "CHEM " },   // Chemistry
    {  12, "BRDG " },   // Bridge
    {  13, "CIRC " },   // Circuit
    {  14, "FLUID" },   // Fluid 2D
    {  15, "PRTCL" },   // ParticleLab
    // ── page 3 ──
    {  16, "NEURL" },   // Neural Lab
    {  17, "OPTIC" },   // OpticsLab
    {  18, "NEOLG" },   // NeoLang
    {  19, "FRACT" },   // Fractals
    {  21, "GB   " },   // Game Boy
    {  23, "AI   " },   // AI
};
inline constexpr int kCasioSlotCount =
    static_cast<int>(sizeof(kCasioSlots) / sizeof(kCasioSlots[0]));

} // namespace ui

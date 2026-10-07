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
 * LatexToAst.h — subset-LaTeX → VPAM MathAST compiler.
 *
 * This is the missing piece recorded in `casio-theme-ai-gb.md:80` ("math
 * renderer wired into mdrender — needs a subset-LaTeX → MathAST compiler; none
 * exists"). It turns the LaTeX-ish math the AI/notes surfaces already carry into
 * the same tree the calculator edits, so a block-math run can be drawn by the
 * real 2D renderer (vpam::MathCanvas) instead of the glyph-substitution pass.
 *
 * CONTRACT — deliberately fail-closed
 *   The vocabulary is a SUBSET. Anything outside it returns status Unsupported
 *   (and a null root); callers MUST then fall back to the existing
 *   normalizeMathTextNoAlloc() path. A compiler that guesses at unknown input
 *   would render *something plausible and wrong*, which is worse than the
 *   current behaviour — so it refuses instead.
 *
 * Supported (everything else → Unsupported):
 *   numbers         123   3.14
 *   letters         a-z A-Z   (x/y/z become NodeVariable, the rest NodeSymbol)
 *   operators       + - * / = < >  and  \cdot \times \div \pm \le \leq \ge \geq
 *                   \ne \neq  (-> OpKind)
 *   greek           \alpha \beta \gamma \delta \epsilon \zeta \eta \theta
 *                   \lambda \mu \nu \xi \rho \sigma \tau \phi \chi \psi \omega
 *                   \Gamma \Delta \Theta \Lambda \Xi \Pi \Sigma \Phi \Psi \Omega
 *   constants       \pi -> NodeConstant(Pi)      \infty -> NodeSpecialValue
 *   structures      \frac{a}{b}   \sqrt{a}   \sqrt[n]{a}   a^{b}   a_{b}
 *                   ( ) -> NodeParen        { } -> grouping
 *   functions       \sin \cos \tan \arcsin \arccos \arctan \ln \log
 *   big ops         \sum_{a}^{b} expr -> NodeSummation
 *                   \int_{a}^{b} expr \,dx -> NodeDefIntegral
 *
 * Bounds: the parser is iterative per nesting level with an explicit depth cap
 * (kMaxDepth) and a node budget (kMaxNodes) so malformed/hostile input cannot
 * blow the device stack or heap. Exceeding either → TooLarge.
 *
 * Dependencies: MathAST.h only (no LVGL, no Arduino) — host-testable.
 */

#pragma once

#include <cstddef>

#include "MathAST.h"

namespace numos {
namespace mathast {

enum class CompileStatus {
    Ok,           ///< root is a valid NodeRow.
    Unsupported,  ///< input used something outside the subset → caller falls back.
    TooLarge,     ///< depth/node budget exceeded → caller falls back.
};

/// Why a compile failed: a short literal naming the offending token/limit.
/// Points to a string literal; never allocated.
struct CompileResult {
    vpam::NodePtr root;              ///< null unless status == Ok
    CompileStatus status = CompileStatus::Unsupported;
    const char*   error  = "";       ///< diagnostic literal (empty on Ok)
};

/// Compile a subset-LaTeX string into a VPAM MathAST root row.
/// Never throws; never returns a partially-built tree (failure → null root).
CompileResult compileSubsetLatex(const char* latex);

/// Convenience wrapper: returns the root or nullptr when the input is outside
/// the subset. Use compileSubsetLatex() when the reason matters.
vpam::NodePtr tryCompileSubsetLatex(const char* latex);

/// Hard limits (exposed for tests).
constexpr int kMaxDepth = 24;
constexpr int kMaxNodes = 256;

}  // namespace mathast
}  // namespace numos

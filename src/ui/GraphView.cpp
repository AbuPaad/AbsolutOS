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
 * GraphView.cpp — Grapher MVC: View implementation (Kandinsky)
 *
 * All pixel-level rendering into the RGB565 PSRAM buffer.
 * No LVGL, no double-buffering overhead — pure direct-to-memory Kandinsky.
 */
#include "GraphView.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include "../utils/ColorUtils.h"

namespace grapher {

// ═════════════════════════════════════════════════════════════════════════
// Constructor
// ═════════════════════════════════════════════════════════════════════════

GraphView::GraphView(uint16_t* graphBuf, int bufW, int bufH)
    : _graphBuf(graphBuf), _bufW(bufW), _bufH(bufH)
    , _xMin(-10.0f), _xMax(10.0f), _yMin(-7.0f), _yMax(7.0f)
{
}

// ═════════════════════════════════════════════════════════════════════════
// Viewport management
// ═════════════════════════════════════════════════════════════════════════

void GraphView::setViewport(float xMin, float xMax, float yMin, float yMax) {
    _xMin = xMin;
    _xMax = xMax;
    _yMin = yMin;
    _yMax = yMax;
}

// ═════════════════════════════════════════════════════════════════════════
// Coordinate conversion
// ═════════════════════════════════════════════════════════════════════════

int GraphView::worldToScreenX(float wx) const {
    if (_xMax <= _xMin) return 0;
    return (int)((wx - _xMin) / (_xMax - _xMin) * _bufW);
}

int GraphView::worldToScreenY(float wy) const {
    if (_yMax <= _yMin) return 0;
    // Y-axis is inverted in screen coordinates
    return (int)((1.0f - (wy - _yMin) / (_yMax - _yMin)) * _bufH);
}

float GraphView::screenToWorldX(int sx) const {
    if (_bufW <= 0) return 0.0f;
    return _xMin + (float)sx / _bufW * (_xMax - _xMin);
}

float GraphView::screenToWorldY(int sy) const {
    if (_bufH <= 0) return 0.0f;
    return _yMax - (float)sy / _bufH * (_yMax - _yMin);
}

// ═════════════════════════════════════════════════════════════════════════
// Color conversion — delegated to utils::rgb888to565 (ColorUtils.h)
// ═════════════════════════════════════════════════════════════════════════

// ═════════════════════════════════════════════════════════════════════════
// Bresenham line rasterizer (with clipping)
// ═════════════════════════════════════════════════════════════════════════

void GraphView::fastDrawLine(int x0, int y0, int x1, int y1, uint16_t color, int w) {
    // Trivial rejection: both points outside same edge
    if ((x0 < 0 && x1 < 0) || (x0 >= _bufW && x1 >= _bufW) ||
        (y0 < 0 && y1 < 0) || (y0 >= _bufH && y1 >= _bufH)) {
        return;
    }

    if (w < 1) w = 1;
    const int off = (w - 1) / 2;   // stamp straddles the path
    // Stamp a w×w block (clipped). w == 1 is EXACTLY the old single-pixel write
    // (bounds-checked), so the numos hairline path stays byte-identical.
    auto put = [&](int x, int y) {
        for (int dy = 0; dy < w; ++dy) {
            const int py = y - off + dy;
            if ((unsigned)py >= (unsigned)_bufH) continue;
            uint16_t* row = _graphBuf + (size_t)py * (size_t)_bufW;
            for (int dx = 0; dx < w; ++dx) {
                const int px = x - off + dx;
                if ((unsigned)px < (unsigned)_bufW) row[px] = color;
            }
        }
    };

    int dx =  std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    // Fast path: both endpoints inside — the stamp still bounds-checks per pixel
    // but the loop itself needs no per-step branch.
    if ((unsigned)x0 < (unsigned)_bufW && (unsigned)x1 < (unsigned)_bufW &&
        (unsigned)y0 < (unsigned)_bufH && (unsigned)y1 < (unsigned)_bufH) {
        for (;;) {
            put(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            int e2 = err * 2;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    } else {
        // Clipping path: check each pixel before writing
        for (;;) {
            put(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            int e2 = err * 2;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Grid and axes
// ═════════════════════════════════════════════════════════════════════════

float GraphView::squareGridStep(float unitsPerPx) {
    const float rough = unitsPerPx * 48.0f;   // ~48 px target spacing
    if (!(rough > 0.0f)) return 1.0f;
    const float mag  = powf(10.0f, floorf(log10f(rough)));
    const float norm = rough / mag;
    const float nice = (norm < 1.5f) ? 1.0f : (norm < 3.5f) ? 2.0f
                     : (norm < 7.5f) ? 5.0f : 10.0f;
    return nice * mag;
}

void GraphView::drawGrid() {
    uint16_t gridColor = utils::rgb888to565(0xE0E0E0);  // Light grey

    const float xRange = _xMax - _xMin;
    const float yRange = _yMax - _yMin;
    if (xRange <= 0.0f || yRange <= 0.0f || _bufW <= 0 || _bufH <= 0) return;

    // ── Square grid cells ────────────────────────────────────────────────
    // Use a SINGLE world-step for both axes. The viewport is equal-aspect
    // (GrapherApp::normalizeAspect makes world-units-per-pixel identical on x and
    // y), so the same world step maps to the same pixel spacing horizontally and
    // vertically — i.e. visually square cells. Previously x and y picked their own
    // "nice" step (targets of 8 vs 6 ticks), so cells were rectangular even when
    // the units-per-pixel matched.
    const float unitsPerPx = xRange / static_cast<float>(_bufW);  // == yRange/_bufH
    const float step = squareGridStep(unitsPerPx);
    if (step <= 0.0f) return;

    // Vertical grid lines (every 'step' world units in x)
    float xStart = floorf(_xMin / step) * step;
    for (float x = xStart; x <= _xMax; x += step) {
        int sx = worldToScreenX(x);
        if (sx >= 0 && sx < _bufW) {
            fastDrawLine(sx, 0, sx, _bufH - 1, gridColor);
        }
    }

    // Horizontal grid lines (same 'step' → square cells)
    float yStart = floorf(_yMin / step) * step;
    for (float y = yStart; y <= _yMax; y += step) {
        int sy = worldToScreenY(y);
        if (sy >= 0 && sy < _bufH) {
            fastDrawLine(0, sy, _bufW - 1, sy, gridColor);
        }
    }
}

void GraphView::drawAxes() {
    uint16_t axisColor = utils::rgb888to565(0x333333);  // Dark grey

    // X-axis (y = 0)
    if (_yMin < 0.0f && _yMax > 0.0f) {
        int sy = worldToScreenY(0.0f);
        fastDrawLine(0, sy, _bufW - 1, sy, axisColor);
    }

    // Y-axis (x = 0)
    if (_xMin < 0.0f && _xMax > 0.0f) {
        int sx = worldToScreenX(0.0f);
        fastDrawLine(sx, 0, sx, _bufH - 1, axisColor);
    }
}

void GraphView::redrawGridAndAxes() {
    clearBuffer();
    drawGrid();
    drawAxes();
}

void GraphView::clearBuffer() {
    if (!_graphBuf || _bufW <= 0 || _bufH <= 0) return;
    std::memset(_graphBuf, 0xFF, (size_t)_bufW * (size_t)_bufH * sizeof(uint16_t));
}

void GraphView::drawFunctionSegment(float wx0, float wy0, float wx1, float wy1, uint32_t rgbColor) {
    if (std::isnan(wx0) || std::isnan(wy0) || std::isnan(wx1) || std::isnan(wy1) ||
        std::isinf(wx0) || std::isinf(wy0) || std::isinf(wx1) || std::isinf(wy1)) {
        return;
    }
    // Huge-but-finite world values (1/x² sampled at x≈1e-7 → y≈1e14) used to be
    // fed straight into the int conversion inside worldToScreen* — a float→int
    // cast that overflows int is UB, and even in-range casts sent Bresenham on
    // walks of millions of off-buffer pixels. Do the projection in float and
    // clamp far outside the buffer first: any |coord| ≤ 20000 px is bit-identical
    // to the old path, and a clamped endpoint still exits the view on the same
    // side, so visible clipping is preserved.
    auto clampS = [](float v) {
        return v < -20000.0f ? -20000.0f : (v > 20000.0f ? 20000.0f : v);
    };
    const float fx0 = (_xMax > _xMin) ? (wx0 - _xMin) / (_xMax - _xMin) * (float)_bufW : 0.0f;
    const float fx1 = (_xMax > _xMin) ? (wx1 - _xMin) / (_xMax - _xMin) * (float)_bufW : 0.0f;
    const float fy0 = (_yMax > _yMin) ? (1.0f - (wy0 - _yMin) / (_yMax - _yMin)) * (float)_bufH : 0.0f;
    const float fy1 = (_yMax > _yMin) ? (1.0f - (wy1 - _yMin) / (_yMax - _yMin)) * (float)_bufH : 0.0f;
    const uint16_t color565 = utils::rgb888to565(rgbColor);
    fastDrawLine((int)clampS(fx0), (int)clampS(fy0),
                 (int)clampS(fx1), (int)clampS(fy1), color565, _curveW);
}

void GraphView::drawSegmentPx(int x0, int y0, int x1, int y1, uint32_t rgbColor) {
    fastDrawLine(x0, y0, x1, y1, utils::rgb888to565(rgbColor), _curveW);
}

void GraphView::plotPixel(int x, int y, uint32_t rgbColor) {
    if ((unsigned)x < (unsigned)_bufW && (unsigned)y < (unsigned)_bufH)
        _graphBuf[y * _bufW + x] = utils::rgb888to565(rgbColor);
}

void GraphView::plotPixelStipple(int x, int y, uint32_t rgbColor) {
    if (((x + y) & 1) != 0) return;  // global checkerboard parity
    if ((unsigned)x < (unsigned)_bufW && (unsigned)y < (unsigned)_bufH)
        _graphBuf[y * _bufW + x] = utils::rgb888to565(rgbColor);
}

void GraphView::fillRectStipple(int x0, int y0, int x1, int y1, uint32_t rgbColor) {
    if (!_graphBuf) return;
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= _bufW) x1 = _bufW - 1;
    if (y1 >= _bufH) y1 = _bufH - 1;
    const uint16_t c = utils::rgb888to565(rgbColor);
    for (int y = y0; y <= y1; ++y) {
        // Start each row on the parity-matching column so the checkerboard tiles.
        int xs = x0 + (((x0 + y) & 1) ? 1 : 0);
        uint16_t* row = _graphBuf + (size_t)y * _bufW;
        for (int x = xs; x <= x1; x += 2) row[x] = c;
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Function curve rasterization
// ═════════════════════════════════════════════════════════════════════════

void GraphView::drawFunctionCurve(const std::vector<float>& xSamples,
                                  const std::vector<float>& ySamples,
                                  uint32_t rgbColor) {
    if (xSamples.size() < 2 || ySamples.size() != xSamples.size()) return;

    uint16_t color565 = utils::rgb888to565(rgbColor);

    // Draw polyline connecting sample points
    for (size_t i = 1; i < xSamples.size(); ++i) {
        float x0 = xSamples[i - 1];
        float y0 = ySamples[i - 1];
        float x1 = xSamples[i];
        float y1 = ySamples[i];

        // Skip segments with NaN or large jumps (discontinuities)
        if (std::isnan(y0) || std::isnan(y1)) continue;
        if (std::abs(y1 - y0) > (_yMax - _yMin) * 2.0f) continue;

        int sx0 = worldToScreenX(x0);
        int sy0 = worldToScreenY(y0);
        int sx1 = worldToScreenX(x1);
        int sy1 = worldToScreenY(y1);

        fastDrawLine(sx0, sy0, sx1, sy1, color565);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Integral shading (vertical lines from y=0 to y=f(x))
// ═════════════════════════════════════════════════════════════════════════

void GraphView::drawAreaUnderCurve(const std::vector<float>& xSamples,
                                   const std::vector<float>& ySamples,
                                   float x0, float x1,
                                   uint32_t rgbColor) {
    if (xSamples.size() < 2 || ySamples.size() != xSamples.size()) return;

    uint16_t color565 = utils::rgb888to565(rgbColor);
    int sx0 = worldToScreenX(x0);
    int sx1 = worldToScreenX(x1);

    // Clamp to buffer bounds
    sx0 = std::max(0, std::min(sx0, _bufW - 1));
    sx1 = std::max(0, std::min(sx1, _bufW - 1));
    if (sx0 > sx1) std::swap(sx0, sx1);

    // For each pixel column in the range, find the value at that x
    // and draw a vertical line from y=0 to y=f(x)
    for (int sx = sx0; sx <= sx1; ++sx) {
        float worldX = screenToWorldX(sx);

        // Interpolate y value from sample data
        float yVal = 0.0f;
        bool found = false;

        for (size_t i = 1; i < xSamples.size(); ++i) {
            if (xSamples[i - 1] <= worldX && worldX <= xSamples[i]) {
                // Linear interpolation
                float t = (worldX - xSamples[i - 1]) / (xSamples[i] - xSamples[i - 1]);
                yVal = ySamples[i - 1] + t * (ySamples[i] - ySamples[i - 1]);
                found = true;
                break;
            }
        }

        if (!found || std::isnan(yVal)) continue;

        // Draw vertical line from y=0 to y=yVal using stipple (every 2nd pixel)
        int sy0 = worldToScreenY(0.0f);
        int sy1 = worldToScreenY(yVal);
        if (sy0 > sy1) std::swap(sy0, sy1);

        for (int sy = sy0; sy <= sy1; ++sy) {
            if ((sy - sy0) % 2 == 0) {  // Stipple pattern
                if ((unsigned)sy < (unsigned)_bufH)
                    _graphBuf[sy * _bufW + sx] = color565;
            }
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Tangent line (numerical derivative + slope line)
// ═════════════════════════════════════════════════════════════════════════

void GraphView::drawTangent(const std::vector<float>& xSamples,
                            const std::vector<float>& ySamples,
                            float xTarget,
                            uint32_t rgbColor) {
    if (xSamples.size() < 3 || ySamples.size() != xSamples.size()) return;

    // Find the y value at xTarget by interpolation
    float yTarget = 0.0f;
    int idx = -1;
    for (size_t i = 1; i < xSamples.size(); ++i) {
        if (xSamples[i - 1] <= xTarget && xTarget <= xSamples[i]) {
            float t = (xTarget - xSamples[i - 1]) / (xSamples[i] - xSamples[i - 1]);
            yTarget = ySamples[i - 1] + t * (ySamples[i] - ySamples[i - 1]);
            idx = i;
            break;
        }
    }

    if (idx < 1 || idx >= (int)xSamples.size()) return;

    // Numerical derivative: (f(x+h) - f(x-h)) / (2h)
    float h = (xSamples[idx] - xSamples[idx - 1]) * 0.5f;
    if (h <= 0.0f) return;

    float yLeft = ySamples[idx - 1];
    float yRight = (idx + 1 < (int)ySamples.size()) ? ySamples[idx + 1]
                                                     : ySamples[idx];
    float derivative = (yRight - yLeft) / (2.0f * h);

    // Line equation: y - yTarget = derivative * (x - xTarget)
    // Find two points on the screen boundary to draw the line

    int sx_center = worldToScreenX(xTarget);
    int sy_center = worldToScreenY(yTarget);

    // Find left and right intersections with screen edges
    int sx_left = 0, sy_left = 0, sx_right = _bufW - 1, sy_right = 0;

    // Left edge (x = 0)
    float x_left = screenToWorldX(0);
    float y_left = yTarget + derivative * (x_left - xTarget);
    sy_left = worldToScreenY(y_left);

    // Right edge (x = bufW - 1)
    float x_right = screenToWorldX(_bufW - 1);
    float y_right = yTarget + derivative * (x_right - xTarget);
    sy_right = worldToScreenY(y_right);

    uint16_t color565 = utils::rgb888to565(rgbColor);
    fastDrawLine(sx_left, sy_left, sx_right, sy_right, color565);
}

// ═════════════════════════════════════════════════════════════════════════
// Intersection marker
// ═════════════════════════════════════════════════════════════════════════

void GraphView::drawIntersectionMarker(float xInt, float yInt, uint32_t rgbColor) {
    int sx = worldToScreenX(xInt);
    int sy = worldToScreenY(yInt);

    if (sx < 0 || sx >= _bufW || sy < 0 || sy >= _bufH) return;

    uint16_t color565 = utils::rgb888to565(rgbColor);

    // Draw a small circle (radius 3 pixels) at the intersection
    int radius = 3;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) {
                int px = sx + dx;
                int py = sy + dy;
                if ((unsigned)px < (unsigned)_bufW && (unsigned)py < (unsigned)_bufH)
                    _graphBuf[py * _bufW + px] = color565;
            }
        }
    }
}

} // namespace grapher

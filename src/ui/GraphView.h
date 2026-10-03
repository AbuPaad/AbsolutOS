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
 * GraphView.h — Grapher MVC: View layer (Kandinsky pixel-level rendering)
 *
 * This module owns the RGB565 PSRAM buffer (_graphBuf) and implements
 * all pixel-pushing operations: grid, axes, function curves, integrals,
 * tangents, and intersection markers.
 *
 * The View receives pre-evaluated float arrays from the Model and rasterizes
 * them directly into _graphBuf. No LVGL dependencies here — pure Kandinsky.
 */
#pragma once

#include <vector>
#include <cstdint>
#include <functional>

namespace grapher {

/**
 * GraphView — Kandinsky pixel-level rendering engine.
 *
 * Owns the target RGB565 buffer and implements all graphics operations
 * for grid, axes, curves, integrals, tangents, and points of interest (POI).
 */
class GraphView {
public:
    /// Default constructor (with dummy buffer; must call setBuffer before use)
    GraphView() : _graphBuf(nullptr), _bufW(0), _bufH(0)
                 , _xMin(-10.0f), _xMax(10.0f), _yMin(-7.0f), _yMax(7.0f) {}

    /// Constructor. Takes ownership of _graphBuf (must be allocated by caller).
    /// Dimensions are in pixels (typically 320×160 for graph area).
    GraphView(uint16_t* graphBuf, int bufW, int bufH);

    ~GraphView() = default;

    /// Set world-coordinate viewport
    void setViewport(float xMin, float xMax, float yMin, float yMax);

    /// Retrieve current viewport
    void getViewport(float& xMin, float& xMax, float& yMin, float& yMax) const {
        xMin = _xMin; xMax = _xMax; yMin = _yMin; yMax = _yMax;
    }

    /// Clear buffer to white and redraw grid + axes
    void redrawGridAndAxes();

    /// Clear buffer to white
    void clear() { clearBuffer(); }

    /// Rasterize grid lines and axes
    void drawGrid();
    void drawAxes();

    /// Clear buffer to white (does NOT redraw grid/axes)
    void clearBuffer();

    /// Draw a single line segment directly into the buffer
    /// x0, y0, x1, y1 are in world coordinates; this method performs coordinate transformation
    /// and Bresenham rasterization directly to the buffer
    void drawFunctionSegment(float wx0, float wy0, float wx1, float wy1, uint32_t rgbColor);

    /// Draw a line segment given in SCREEN (pixel) coordinates. Used by the
    /// implicit-equation marching-squares renderer, which interpolates contour
    /// crossings in pixel space. Clipped to the buffer.
    void drawSegmentPx(int x0, int y0, int x1, int y1, uint32_t rgbColor);

    /// Set one buffer pixel (screen coords) to rgbColor, bounds-checked.
    void plotPixel(int x, int y, uint32_t rgbColor);

    /// Inequality region shading. Fills the inclusive pixel rectangle
    /// [x0..x1]×[y0..y1] with a 50 % checkerboard ("stipple") of rgbColor so the
    /// grid/axes show through, NumWorks-style. The checkerboard parity is global
    /// ((x+y)&1), so adjacent whole-cell fills and per-pixel fills tile seamlessly.
    void fillRectStipple(int x0, int y0, int x1, int y1, uint32_t rgbColor);

    /// Single stipple pixel honoring the same global parity as fillRectStipple;
    /// used to feather inequality shading inside boundary cells.
    void plotPixelStipple(int x, int y, uint32_t rgbColor);

    /// Buffer dimensions in pixels (for callers that grid over the viewport).
    int bufW() const { return _bufW; }
    int bufH() const { return _bufH; }

    /// Rasterize function curve into buffer (sample-based adaptive curve)
    void drawFunctionCurve(const std::vector<float>& xSamples,
                           const std::vector<float>& ySamples,
                           uint32_t rgbColor);

    /// Draw integral shading: vertical lines from y=0 to y=f(x) in range [x0,x1]
    /// Uses stipple pattern (alternating pixels) to simulate transparency
    void drawAreaUnderCurve(const std::vector<float>& xSamples,
                            const std::vector<float>& ySamples,
                            float x0, float x1,
                            uint32_t rgbColor);

    /// Draw tangent line: compute numerical derivative at xTarget,
    /// then draw a line across the entire screen
    void drawTangent(const std::vector<float>& xSamples,
                     const std::vector<float>& ySamples,
                     float xTarget,
                     uint32_t rgbColor);

    /// Draw intersection marker: small circle + label at (xInt, yInt)
    void drawIntersectionMarker(float xInt, float yInt, uint32_t rgbColor);

    /// Shared "nice" grid step (1/2/5 × 10^n) for a given world-units-per-pixel,
    /// targeting ~48 px spacing. Used by BOTH the grid rasterizer (drawGrid) and
    /// the on-screen tick labels (GrapherApp) so labels land exactly on grid lines
    /// and — because the viewport is equal-aspect — cells stay visually square.
    static float squareGridStep(float unitsPerPx);

    /// Set the stroke width used for CURVES (function segments, implicit
    /// contours). 1 = hairline (numos). Casio raises it because the fitted
    /// 320x156 canvas is far smaller than the OEM 396x224 one, where a hairline
    /// curve reads as noise. Grid/axes are never thickened by this.
    void setCurveWidth(int w) { _curveW = (w < 1) ? 1 : (w > 4 ? 4 : w); }
    int  curveWidth() const { return _curveW; }

    /// Utility: convert world coords to screen pixel coords
    int worldToScreenX(float wx) const;
    int worldToScreenY(float wy) const;

    /// Utility: convert screen pixel coords to world coords
    float screenToWorldX(int sx) const;
    float screenToWorldY(int sy) const;

private:
    uint16_t* _graphBuf;    ///< RGB565 buffer (PSRAM)
    int       _bufW, _bufH; ///< Buffer dimensions in pixels
    int       _curveW = 1;  ///< stroke width for curves (1 = hairline)
    float     _xMin, _xMax, _yMin, _yMax;  ///< World viewport

    /// Bresenham line into buffer with clipping. `w` is the stroke width in
    /// pixels (a w×w stamp at every step); grid/axes pass 1, curves pass _curveW.
    void fastDrawLine(int x0, int y0, int x1, int y1, uint16_t color, int w = 1);

};

} // namespace grapher

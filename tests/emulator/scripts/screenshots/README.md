# Screenshot capture scripts — mapping to the website

These scripts put the website's `/assets/img/apps/*` frames on the real emulator, so nothing on the
site is a mockup. One script per app, all in this folder.

**Deliverables are owned by the website**, `paadweb/public/apps/index.html`: 22 apps / 64 shots, each
with the path it must land on and a description of what the frame has to show. The table below is the
transcription; if it and the website ever disagree, the website wins.

## How to run one

    ./"C:/.piobuild/numOS/emulator_pc/program" --headless --deterministic \
        --script tests/emulator/scripts/screenshots/<app>.numos \
        --fs-root tests/emulator/fs --frames 1400 --quiet

Exit 0 ok · 2 script/parse error · 3 screenshot write error · 4 assertion failure.
Screenshots land relative to the process cwd, so run from the repo root and they appear in `out/`.

## Theme: only 4 apps render the Casio look

Measured, not assumed — `Layout::Casio` / the `InteractionModel` profile and `appSurface()`:

| app | casio-aware code | state |
|---|---|---|
| Grapher | 101 refs | MIGRATED (split UI, `createCasioUI()`) |
| Calculation | 78 refs | MIGRATED (explicit `_casioLayout`) |
| AI | 14 refs | MIGRATED (appSurface + softkey band) |
| Game Boy | 8 refs | MIGRATED (appSurface) |
| Settings | 3 refs | NOT migrated — those 3 are the theme *picker*, the screen is unstyled |

Everything else in `src/apps/` has zero casio code and still carries `lv_color_hex`/`COL_` literals,
so it renders the numos look under either theme. A capture of those is a numos capture.

## The 64 shots

### Calculation — APP 00 — script `screenshots/calculation.numos`
- source: `src/apps/CalculationApp.cpp`
- theme state: MIGRATED - explicit _casioLayout (Layout::Casio)
- shots: 3
  1. `/assets/img/apps/calculation-1.webp`
     SHOW: 1/3 + 1/6 entered, result shown as a stacked fraction with the bar drawn, not as a text row.
     CAPTION: Enter 1/3 + 1/6, get a real fraction: 1/2, typeset.
  2. `/assets/img/apps/calculation-2.webp`
     SHOW: The same result after the S to D key: a 200-digit extended decimal, scrolled part way in so the digits fill the band.
     CAPTION: One key flips exact, to periodic, to a 200-digit decimal.
  3. `/assets/img/apps/calculation-3.webp`
     SHOW: The F2 step viewer for 2 + 3 x 4: step 0 the original expression, step 1 "2 + 12", step 2 "14", each step rendered as real maths.
     CAPTION: Step-by-step mode breaks the arithmetic into atomic transformations.

### Grapher — APP 01 — script `screenshots/grapher.numos`
- source: `src/apps/GrapherApp.cpp`
- theme state: MIGRATED - _casio = im.splitGraph from the InteractionModel
- shots: 3
  1. `/assets/img/apps/grapher-1.webp`
     SHOW: Four slots live at once: y = x squared minus 4, y = 2x + 3, the implicit circle x squared + y squared = 9, and the shaded inequality y is less than sin x.
     CAPTION: An explicit curve, a line, an implicit circle and a shaded region on one grid.
  2. `/assets/img/apps/grapher-2.webp`
     SHOW: The trace cursor walking the implicit circle, with the floating x and y readout, the camera locked so the circle stays centred.
     CAPTION: Trace an implicit curve: the readout follows, the curve stays put.
  3. `/assets/img/apps/grapher-3.webp`
     SHOW: The Calculate menu opened on a trace, with the intersection markers placed where the line cuts the parabola.
     CAPTION: Roots, extrema and intersections found for you and marked on the curve.

### Game Boy — APP 21 — script `screenshots/gameboy.numos`
- source: `src/apps/GameBoyApp.cpp`
- theme state: MIGRATED - reads appSurface
- shots: 3
  1. `/assets/img/apps/gameboy-1.webp`
     SHOW: The ROM picker in the Casio theme, surrounded by the digit-launch key hints, so the launcher and the theme both read at once.
     CAPTION: Pick a ROM from the card; the list wears the same theme as the OS.
  2. `/assets/img/apps/gameboy-2.webp`
     SHOW: A Game Boy Color homebrew title screen, correct palette, filling the letterboxed 160 by 144 area.
     CAPTION: Colour titles run at the right palette, not the four-shade grey.
  3. `/assets/img/apps/gameboy-3.webp`
     SHOW: A monochrome homebrew game mid-play in the classic four-shade green, the keypad mapped to the d-pad and buttons.
     CAPTION: DMG and CGB cores, the calculator keypad as a controller.

### AI — APP 23 — script `screenshots/ai.numos`
- source: `src/apps/AiApp.cpp`
- theme state: MIGRATED - reads appSurface + softkey band
- shots: 3
  1. `/assets/img/apps/ai-1.webp`
     SHOW: Page one of the balance-the-redox-equation answer: an oxidation-state table, rendered as a real table on the screen.
     CAPTION: The answer arrives as markdown and is laid out, not dumped as plain text.
  2. `/assets/img/apps/ai-2.webp`
     SHOW: The last page of the same answer: the final balanced equation with the atom count check under it.
     CAPTION: Long answers paginate; left and right walk the pages.
  3. `/assets/img/apps/ai-3.webp`
     SHOW: The model picker, each row with its provider mark, the current model highlighted.
     CAPTION: Pick the model on the device; the choice persists.

### Chemistry — APP 11 — script `screenshots/chemistry.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/chemistry-1.webp`
     SHOW: The full table with the cursor on tungsten, the detail panel showing the electron configuration [Xe] 4f14 5d4 6s2.
     CAPTION: A real table, colour coded by family, with the detail beside it.
  2. `/assets/img/apps/chemistry-2.webp`
     SHOW: The deep-dive overlay open on mercury: melting and boiling points, density, atomic radius, ionisation energy and the fun fact line.
     CAPTION: Thirteen extra properties per element, plus a line of trivia.
  3. `/assets/img/apps/chemistry-3.webp`
     SHOW: The balancer with Fe + O2 = Fe2O3 typed in and the exact answer 4Fe + 3O2 = 2Fe2O3 returned.
     CAPTION: Balancing in exact rationals, so the coefficients are always minimal integers.

### Statistics — APP 04 — script `screenshots/statistics.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/statistics-1.webp`
     SHOW: The data table with six rows filled in, one cell in the blue edit focus, the hint bar along the bottom.
     CAPTION: A value column and a frequency column, twenty rows deep.
  2. `/assets/img/apps/statistics-2.webp`
     SHOW: The stats tab with all seven readouts populated: mean, median, standard deviation, minimum, maximum, sum and count.
     CAPTION: Seven frequency-weighted statistics, recomputed as you type.
  3. `/assets/img/apps/statistics-3.webp`
     SHOW: The graph tab: a histogram of the entered frequencies with the axis auto-scaled to fit.
     CAPTION: The distribution drawn from the same numbers in the table.

### Circuit — APP 13 — script `screenshots/circuit.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/circuit-1.webp`
     SHOW: Edit mode, mid-build: a supply feeding a resistor and a transistor, a coin cell driving an AND gate into a seven-segment display, and a programmable block at the bottom.
     CAPTION: Analog and digital side by side, on a snapped grid.
  2. `/assets/img/apps/circuit-2.webp`
     SHOW: Simulation running: the voltage heatmap on, two scope traces in the corner, current arrows on the wires and a multimeter reading.
     CAPTION: Dual-trace scope, multimeter and a live voltage heatmap of the whole circuit.
  3. `/assets/img/apps/circuit-3.webp`
     SHOW: The programmable block's editor open, a few lines of Lua driving an output pin.
     CAPTION: A programmable block with a small scripting VM behind it.

### NeoLang — APP 18 — script `screenshots/neolang.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/neolang-1.webp`
     SHOW: One line in the editor differentiating a polynomial, the symbolic answer printed back in the console.
     CAPTION: Symbolic differentiation as a function call, on the device.
  2. `/assets/img/apps/neolang-2.webp`
     SHOW: A two-equation system solved in a single call, both unknowns printed.
     CAPTION: Solve a small system symbolically from the console.
  3. `/assets/img/apps/neolang-3.webp`
     SHOW: The full-screen plot overlay after a one-line plot call, axes and curve drawn straight to the screen.
     CAPTION: One line of script, full-screen plot.

### Calculus — APP 03 — script `screenshots/calculus.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/calculus-1.webp`
     SHOW: The derivative of sin x over x, the original on top and the quotient-rule result below with a real fraction bar.
     CAPTION: A chain of derivatives, typeset properly with a fraction bar.
  2. `/assets/img/apps/calculus-2.webp`
     SHOW: An indefinite integral of x squared times sin x, returned as a sum of terms with the constant of integration appended.
     CAPTION: Integration by parts, worked out symbolically.
  3. `/assets/img/apps/calculus-3.webp`
     SHOW: An integral with no elementary answer, shown honestly as the unevaluated integral rather than a wrong result.
     CAPTION: When there is no closed form, it says so and shows the integral.

### Equations — APP 02 — script `screenshots/equations.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/equations-1.webp`
     SHOW: A quadratic solved exactly, both roots shown as indexed lines.
     CAPTION: Exact roots, not decimal approximations.
  2. `/assets/img/apps/equations-2.webp`
     SHOW: The steps view showing the factored form, with the factor highlighted as the step that just changed.
     CAPTION: Each step highlights exactly what changed.
  3. `/assets/img/apps/equations-3.webp`
     SHOW: A two-equation linear system solved, both unknowns returned together.
     CAPTION: Systems up to three equations in x, y and z.

### Bridge — APP 12 — script `screenshots/bridge.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/bridge-1.webp`
     SHOW: Edit mode part way through a build: anchors at both banks, deck nodes arching across, truss members above, the active material highlighted in the toolbar.
     CAPTION: Snap-to-grid building with three materials to choose from.
  2. `/assets/img/apps/bridge-2.webp`
     SHOW: Simulation running with a truck part way across, members coloured by how hard they are working, the deck visibly sagging under the load.
     CAPTION: Stress shown as colour: green is fine, red is about to fail.
  3. `/assets/img/apps/bridge-3.webp`
     SHOW: The failure moment: one beam snapped and dimmed, its neighbours spiking red, the truck dropping through the gap.
     CAPTION: Beams break when the load beats them, and the model carries on.

### OpticsLab — APP 17 — script `screenshots/opticslab.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/optics-1.webp`
     SHOW: The default bench: an object arrow on the left, two thick lenses, a fan of rays crossing the gap and landing on the screen, the focal-length readout underneath.
     CAPTION: Seven rays, thick lenses, and the image forming where the maths says it should.
  2. `/assets/img/apps/optics-2.webp`
     SHOW: A steeply curved lens at a high index, with the outer rays bent past the critical angle and marked as total internal reflection.
     CAPTION: Total internal reflection, drawn where it actually happens.
  3. `/assets/img/apps/optics-3.webp`
     SHOW: An afocal pair: a diverging lens followed by a converging one, rays leaving parallel, the readout honestly reporting no single focal length.
     CAPTION: Ray tracing and the matrix method, agreeing in public.

### Neural Lab — APP 16 — script `screenshots/neurallab.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/neural-1.webp`
     SHOW: The XOR problem solved: a clean diagonal boundary splitting the two point classes, the network graph pulsing along the weights, the accuracy gauge green.
     CAPTION: XOR, learned from scratch on the device.
  2. `/assets/img/apps/neural-2.webp`
     SHOW: Two interleaved spirals separated by the trained boundary, with the log-scale loss curve falling away in the corner.
     CAPTION: A boundary no straight line could draw, found by training.
  3. `/assets/img/apps/neural-3.webp`
     SHOW: The network mid-training with a neuron just added to a hidden layer, the loss visibly kinking upward before it resumes falling.
     CAPTION: Change the architecture while it trains.

### Fluid 2D — APP 14 — script `screenshots/fluid2d.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/fluid-1.webp`
     SHOW: The wind tunnel preset in the velocity palette, flow entering from the left and wrapping around the obstacle in a coloured wake.
     CAPTION: Flow around an obstacle, coloured by speed and direction.
  2. `/assets/img/apps/fluid-2.webp`
     SHOW: The convection preset in the thermal palette, plumes rising from the heated floor, curling over and sinking down the sides.
     CAPTION: Heat driven convection, with nobody touching the controls.
  3. `/assets/img/apps/fluid-3.webp`
     SHOW: Two dyes in one velocity field, red from the left and blue from the right, meeting in the middle and mixing into a band of purple.
     CAPTION: Two dyes, one flow, mixing where they collide.

### ParticleLab — APP 15 — script `screenshots/particlelab.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/particle-1.webp`
     SHOW: A molten pool with water poured onto it: steam erupting upward, the lava cooling to stone where they meet, everything glowing by temperature.
     CAPTION: Lava plus water: steam, stone, and a heat glow that tracks the temperature.
  2. `/assets/img/apps/particle-2.webp`
     SHOW: A wired circuit inside the sandbox: a heated wire glowing at its hottest, a cooler pulling heat away, and a charge detonating in a fireball.
     CAPTION: Heaters, coolers and explosives wired together in the sand.
  3. `/assets/img/apps/particle-3.webp`
     SHOW: A plant climbing toward a pool of water, with the material palette open over the corner of the grid.
     CAPTION: Growth, fire, dissolution and burning, all as material rules.

### Fractals — APP 19 — script `screenshots/fractals.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/fractals-1.webp`
     SHOW: The Mandelbrot set zoomed a long way in, filaments and mini-sets still crisp rather than collapsing into blocks.
     CAPTION: Deep zoom that stays sharp, by perturbation.
  2. `/assets/img/apps/fractals-2.webp`
     SHOW: The four-module launcher, each card showing a small preview of the fractal behind it.
     CAPTION: Four fractals, each with its own preview card.
  3. `/assets/img/apps/fractals-3.webp`
     SHOW: A cross-section of the three-dimensional Mandelbulb, tunnels and chambers opening through the slice.
     CAPTION: A 3D fractal explored by sweeping a slice through it.

### Matrices — APP 09 — script `screenshots/matrices.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/matrices-1.webp`
     SHOW: The manager view with three matrix slots showing their dimensions and the operation row along the bottom.
     CAPTION: Named slots that remember their own shape.
  2. `/assets/img/apps/matrices-2.webp`
     SHOW: The editor on a three by three with one cell in edit mode, the size controls and the dimension readout in the corner.
     CAPTION: Resize and edit cells in place.
  3. `/assets/img/apps/matrices-3.webp`
     SHOW: An inverse computed and shown as a full result matrix, or a determinant shown as a single large value.
     CAPTION: Inverses by elimination, determinants by expansion.

### Probability — APP 05 — script `screenshots/probability.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/probability-1.webp`
     SHOW: The standard normal curve with the area up to a boundary shaded, and the density and cumulative values printed in full precision below.
     CAPTION: A shaded tail and both numbers, read off the same curve.
  2. `/assets/img/apps/probability-2.webp`
     SHOW: A wide real-world distribution: a mean of fifty and a spread of fifteen, with the boundary set at sixty-five.
     CAPTION: Any mean and spread, redrawn as you change them.
  3. `/assets/img/apps/probability-3.webp`
     SHOW: A parameter being edited, the cursor visible in the field and the other rows dimmed.
     CAPTION: Three parameters, edited in place.

### Regression — APP 06 — script `screenshots/regression.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 3
  1. `/assets/img/apps/regression-1.webp`
     SHOW: The equation tab after a quadratic fit: the equation, each coefficient, the coefficient of determination and the point count.
     CAPTION: The fitted equation and its coefficients, spelled out.
  2. `/assets/img/apps/regression-2.webp`
     SHOW: The graph tab: the entered points as scatter and the fitted curve passing through them.
     CAPTION: The data and the fit on the same axes.
  3. `/assets/img/apps/regression-3.webp`
     SHOW: The same graph after switching the model, the quadratic curve replaced by a straight line.
     CAPTION: Switch models and watch the fit change.

### Settings — APP 10 — script `screenshots/settings.numos`
- source: `src/apps/SettingsApp.cpp`
- theme state: NOT migrated - its casio code is the theme PICKER (row calls ThemeManager::activate), the screen itself is unstyled
- shots: 3
  1. `/assets/img/apps/settings-1.webp`
     SHOW: The settings list with a row focused, showing the current value and the key hints along the bottom.
     CAPTION: Angle mode, complex numbers, precision, theme: all in one list.
  2. `/assets/img/apps/settings-2.webp`
     SHOW: The Wi-Fi sub-screen with the setup portal running: the network name, the address to open, and how many devices have joined.
     CAPTION: The calculator hosts its own setup network for your phone.
  3. `/assets/img/apps/settings-3.webp`
     SHOW: The theme row switched to the Casio skin, with the rest of the list redrawn to match.
     CAPTION: Two full themes, switched on the device.

### Python — APP 08 — script `screenshots/python.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 2
  1. `/assets/img/apps/python-1.webp`
     SHOW: The editor with the word-completion popup open above a partly typed line.
     CAPTION: An editor with completion, and a console underneath.
  2. `/assets/img/apps/python-2.webp`
     SHOW: The console after a short script has run, showing its printed output.
     CAPTION: Scripts are saved on the card and run from the list.

### Sequences — APP 07 — script `screenshots/sequences.numos`
- source: `src/apps/(no app-specific casio file)`
- theme state: numos only - no casio code paths
- shots: 2
  1. `/assets/img/apps/sequences-1.webp`
     SHOW: The define tab with two sequence formulas, one row selected.
     CAPTION: Two sequences, defined as short formulas.
  2. `/assets/img/apps/sequences-2.webp`
     SHOW: The table tab, the first terms of both sequences listed against n.
     CAPTION: A table of the first twenty terms.

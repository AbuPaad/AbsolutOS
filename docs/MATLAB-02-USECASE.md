# MATLAB-02-USECASE — what NumOS should actually do with MATLAB

Date: 2026-10-18 · Branch: `main` · Author: scope run (no app code modified)
Supersedes §5 of `MATLAB-SCOPE-01.md`. Read this one first.

---

## 0. Why this document exists

`MATLAB-SCOPE-01` asked "should NumOS run MATLAB?" and answered "no — port the
syntax". That was the wrong question. This document answers the right one:
**what is MATLAB actually for, and where does a calculator sit in that loop?**

Spoiler: the calculator's job is not to *run* MATLAB. It is to be **the best
place on earth to type a matrix** — and then hand it to a PC that runs MATLAB.
That gap is real, it is unserved, and roughly 80 % of it already exists in this
firmware.

---

## 1. What MATLAB actually is (for someone who has never used it)

**MATLAB is a spreadsheet for matrices with a programming language bolted on.**

Every value is an array. The number `7` is a 1×1 array. `[1 2 3]` is a 1×3. A
vector, a matrix, an image, a signal — all the same object. Because of that, the
things that take 20 lines in C are one line in MATLAB:

```matlab
x = A\b            % solve a linear system  — this is the whole reason it exists
[p,S] = polyfit(t,v,2)   % fit a parabola through data
plot(t,v,'o',t,polyval(p,t))   % ...and draw it
```

That's it. That's the product. Not symbolic algebra (that's a paid toolbox and
Giac already beats it), not speed, not programming. **Matrices in, answer out.**

### Where it is actually used by people NumOS sells to

| Course | The MATLAB line | The matrix that comes from |
|:--|:--|:--|
| Circuits | `V = G\I` | nodal analysis conductance matrix |
| Statics / structures | `u = K\F` | stiffness matrix |
| Signals | `Y = fft(y)`, `conv(h,x)` | sampled data |
| Control | `bode(tf(num,den))` | transfer function coefficients |
| Numerics | `fzero`, `ode45` | a function, not a matrix |
| Labs / reports | `polyfit`, `std`, `histogram` | measured readings |
| Linear algebra | `eig`, `svd`, `rref` | any matrix |

The pattern is unmistakable: **a human reads a problem off paper, writes down a
matrix, and the computer multiplies.** MIT's circuits course and Auburn's EE lab
both teach exactly this (`circuits.mit.edu` pset 1, `eng.auburn.edu` Lab 4).
The matrix is the human's contribution. Everything after is arithmetic.

## 2. The problem nobody solves: typing the matrix

That moment — human has the matrix in their head, computer needs it in a text
buffer — is the worst part of the workflow, and MATLAB does nothing about it.

A real example. Three-node resistor network, R = 10/5/20/4/8 Ω, 2 A into node 1,
1 A into node 3. Nodal analysis gives you conductances 0.1, 0.2, 0.05, 0.25,
0.125 and the system:

```matlab
G = [ 0.30 -0.20  0.00
     -0.20  0.50 -0.25
      0.00 -0.25  0.375];
I = [2; 0; 1];
V = G\I        % →  13.3333   10.0000    9.3333
```

*(verified: residuals zero on all three rows)*

Now count what the student has to get right on a QWERTY keyboard: **9 signed
decimals, 5 of them three-digit, two of them negative, plus a semicolon/space
convention that silently creates a transposed matrix if you get it wrong.** One
typo produces a plausible-looking wrong answer with no error. This is the single
most common way an engineering student loses an evening.

And critically: **MATLAB cannot tell you the matrix is wrong.** It will solve a
mistyped matrix perfectly.

## 3. What the calculator does that MATLAB can't

NumOS can **check the matrix before it becomes code**, because the check is
local and cheap:

- `det(G)` ≠ 0 — is it even solvable? (`det` already exists in `NeoStdLib`)
- `inv(G)*I` — the answer, on-device, in the palm, before the PC is involved
  (`inv` already exists; `MatrixEngine::inverse` is implemented)
- the matrix rendered **as a matrix** in 2D on the 320×156 canvas, so your eye
  catches the misplaced minus sign the way it catches a typo in a sentence

So the loop becomes:

> read the problem → key it in → **see it and solve it on-device** → push
> `G`, `I`, `V = G\I` to the PC as runnable code → MATLAB does the expensive
> thing (plot, report, 500-node version)

**The calculator is where you verify; the PC is where you use.** That's the
pitch, and it needs no MATLAB inside the calculator at all.

## 4. The pivot: don't build MATLAB-in, build math-out

The moment you see the calculator as a **matrix input device**, MATLAB stops
being the feature and becomes **one of the output dialects**. Same keystrokes,
five targets:

| Target | Why someone wants it |
|:--|:--|
| **MATLAB / Octave** | the coursework default |
| **Python / numpy** | the other half of engineering school (`np.array([[…]])`, `np.linalg.solve`) |
| **LaTeX** | `\begin{bmatrix}…\end{bmatrix}` for the lab report — Mathpix proved people pay for exactly this |
| **Excel / CSV** | hand the readings to a spreadsheet |
| **Julia / R** | cheap once the printer exists |

This is a much better product than "MATLAB on a calculator" for three reasons:

1. **No licence, no trademark, no MathWorks dependency.** You are not claiming
   compatibility with anything; you are emitting text.
2. **Every buyer gets value**, including the half who use Python.
3. **It is a *better* MATLAB feature than running MATLAB**, because it fixes the
   step MATLAB is worst at instead of reimplementing the step it's best at.

## 5. It is ~80 % already built

| Piece needed | Status in this repo |
|:--|:--|
| Matrix entry UI | `src/apps/MatricesApp.cpp` — exists |
| Matrix arithmetic | `src/apps/MatrixEngine.cpp` — add/sub/mul/det/inverse implemented, **`MAX_DIM = 5`** |
| on-device solve | `det`, `inv` already wired in `NeoStdLib.cpp:280-281`; Giac for exact answers |
| symbolic check | Giac in-firmware (2.66 MiB, `simplify` 373 µs) |
| transport to PC | **`src/net/Portal.cpp` is already an HTTP file server** — `GET /api/files`, `GET /api/file`, `POST /api/file`, mkdir/delete/rename |
| 2D rendering | existing math renderer + `MathRenderVisualTestApp` |

**`MAX_DIM = 5` is not a bug, it's the product boundary** — and it lands exactly
where the screen does. A 320×156 canvas at ~40 cols × ~12 rows shows a 4×4
comfortably and a 5×5 with engineering notation. Coursework nodal/stiffness
matrices are 3×3 to 5×5. The hardware ceiling and the use case agree.

What is genuinely missing: **a matrix→code printer** (one file, a few hundred
lines) and **a "send to PC" button** that writes the snippet and exposes it via
the Portal that already runs.

## 6. Transport, cheapest first

0. **A MATLAB add-on (`.mltbx`) that is the client — decided 2026-10-18.**
   MATLAB becomes the HTTP client (`webread`/`webwrite` are *base* MATLAB;
   ThingSpeak's own toolbox uses this pattern) and pulls from `Portal.cpp`
   directly. **No firmware change is required for v0** — all four routes
   already exist. Supersedes the curl step below for anyone with MATLAB, and
   adds the reverse direction (`POST /api/file` → push a MATLAB result onto the
   calc as a bench display). Full design: `MATLAB-04-MATLAB-ADDON.md`.
   *Blocker to fix before promoting it:* Portal has no auth and exposes
   `POST /api/delete` — an unauthenticated filesystem API stops being a demo
   detail the moment it is advertised.
1. **Fallback: Portal file fetch via curl or the browser — zero new subsystems.**
   Calc writes `/mx/last.m` to LittleFS; `Portal.cpp` already serves
   `/api/file`. Works for Python/VS Code/Jupyter users who have no MATLAB.
   *This is the second thing to build, and covers the non-MATLAB half.*
2. **USB HID keyboard — hardware and CDC are already there; HID is the new bit.**
   ESP32-S3 native USB is on **GPIO19/GPIO20 (schematic-confirmed,
   PCBWay-tested)** and **native USB CDC is already the production serial
   backend** — `ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`,
   `NUMOS_SERIAL_BACKEND_USB_CDC=1` in both the base and production envs, and
   `scripts/check-production-target.py` fails the build without them. What is
   *not* enabled is **HID**: `ARDUINO_USB_MODE=1` selects the ROM's USB
   Serial/JTAG peripheral, whose descriptor is fixed in ROM, so it cannot present
   a HID interface. HID needs `ARDUINO_USB_MODE=0` (TinyUSB) in a **separate
   build env** — see `MATLAB-03-USB-HID.md`. Payoff: the calc types the matrix
   into whatever window has focus, zero drivers.
3. **MCP (`matlab-mcp-server`) — the long game.** Official MathWorks server
   (Go, stdio) that writes and runs MATLAB code. Fits `AiApp`'s existing
   tool-server dispatch (`AiApp.cpp:643` currently hardcodes `wolfram`). Use
   when the *answer* needs remote compute, not just the *input*.

## 7. What would kill it

- **The keypad is slower than QWERTY for someone already at a keyboard.** True,
  and that's fine — the value is when you are *not* at a keyboard. Don't market
  it as replacing MATLAB typing; market it as the bench/park/library/without-a-laptop case.
- **`MAX_DIM = 5` is a hard ceiling** unless someone raises it. Say 5×5 in the
  spec and mean it, or raise it deliberately — don't discover it in a demo.
- **It's a bridge, so it needs a PC at the other end.** Offline it's still a
  calculator that solves the matrix — which is a complete product on its own,
  so this is a soft failure, not a hard one.
- **Portal is a hotspot-only HTTP server.** Confirm it serves on the station
  (home Wi-Fi) path, not just the provisioning AP, before promising "over Wi-Fi".

## 8. First step

**Write the printer, not the interpreter.** One function:
`matrixToCode(Matrix, dialect) → string` for `matlab | python | latex | csv`,
plus a `Send to PC` key in `MatricesApp` that writes `/mx/<slug>.<ext>` and
prints the Portal URL. No new parser, no new language, no flash risk beyond a
few KB.

Success test, and it's a five-minute demo: key the 3-node example in §2 on the
device, press Send, `curl` the snippet on a laptop, paste into MATLAB, get
`13.3333  10.0000  9.3333`. If that works, the product sentence is already
true: *"the calculator is where you build the matrix; the PC is where you use
it."*

---

### Evidence index

- `src/apps/MatrixEngine.cpp` / `.h` — add/sub/mul/det/inverse, `MAX_DIM = 5`
- `src/apps/NeoStdLib.cpp:280-281` — `det`, `inv` registered
- `src/net/Portal.cpp:1206-1214` — HTTP file server routes
- `docs/HARDWARE.md` §1 — native USB on GPIO19/20. **§6 is stale:** it says USB
  CDC "is disabled in the production environment", which contradicts
  `platformio.ini` lines 49-51 / 161-163 and the `check-production-target.py`
  gate. CDC is on and required; do not trust §6.
- `docs/PRODUCTION_PCBA_HARDWARE_CONTRACT.md` — GPIO19/GPIO20 schematic + PCBWay-confirmed
- `docs/MATLAB-SCOPE-01.md` — engine scope (superseded by §4–5 here)
- `github.com/matlab/matlab-mcp-server` — official, Go, stdio
- mathpix.com/math-ocr — math → LaTeX is a paid market
- `circuits.mit.edu/S26/psets/01/nodes_G_matrix`, `eng.auburn.edu` Lab 4 — nodal
  analysis as taught, i.e. the matrix the student must type

# MATLAB-SCOPE-01 — MATLAB in NumOS: scope, prior art, form-factor value

Date: 2026-10-18 · Branch: `main` · Author: scope run (no app code modified)
Status: **recommendation, not a commitment.** Nothing here is built.

---

## 1. Question

Should NumOS speak MATLAB, and if so *which* MATLAB — the language, the engine,
or the desktop session? What do other calculators do, and what does a handheld
form factor actually buy a MATLAB user that a laptop does not?

## 2. Verdict in one paragraph

**No calculator has ever run MATLAB, and NumOS should not be the first to try
running the real thing** — MATLAB is 4.6 GB minimum, x86-64/ARM64 only, licensed,
and its engine is *not* the interesting part because **Giac (KhiCAS) already
ships in this firmware** and beats MATLAB's symbolic toolbox. What MATLAB
actually owns is **the language people type and the arrays they think in**. That
is a ~150 KB front-end on top of machinery NumOS already has (`NeoValue`,
`NeoInterpreter`, `NeoStdLib`, vendored Giac). So: scope MATLAB as a
**`.m` dialect + array semantics**, not as an engine, and wire real MATLAB in
over the network as a *tool server* (`matlab-mcp-server`) for the cases where a
pocket device genuinely needs the big one.

---

## 3. What "MATLAB" actually is

MATLAB is a matrix-first language plus ~2,300 core functions plus toolboxes.
The install footprint, per MathWorks' own requirements page: **4.6 GB for MATLAB
alone, 5–8 GB typical, 25 GB all products**, on x86-64 (or Apple silicon).
It cannot go on an ESP32-S3. Every viable scope therefore discards the engine
and keeps some subset of the surface.

Grouping the surface by what a handheld can genuinely honour:

| MATLAB area | Representative functions | Handheld verdict |
|:--|:--|:--|
| **Array core** | `zeros ones eye linspace : A(i,j) A(:,k) end size numel reshape` | **The whole point.** Fits; 1-based indexing and `end` are cheap once a real array type exists |
| **Element-wise vs matrix ops** | `.* ./ .^ ' ^ *` | Fits. This distinction is the single biggest semantic gap vs Python-shaped `NeoLanguage` |
| **Linear algebra** | `\ inv det rank rref lu qr svd eig norm cond` | Fits — 3×3…6×6 realistically (432×432 numbers is what PSRAM allows; the *screen* is the limit, see §6) |
| **Stats / curves** | `mean std median polyfit corrcoef histogram interp1 trapz` | Fits well. `polyfit` on 20 points is a calculator-sized problem |
| **Roots / ODE / numeric** | `fzero fsolve ode45 integral fft` | `fzero`/`integral`/short `ode45` fit. `fft` fits. Long ODE runs are a battery/time problem, not a memory one |
| **Symbolic** | `syms solve diff int simplify vpa` | **Already solved better.** Giac is in the firmware (`docs/GIAC_FEASIBILITY_SPIKE.md`: `simplify((x^2-1)/(x-1))` → `x+1` in 373 µs). MATLAB needs a paid toolbox for this |
| **Plotting** | `plot semilogx loglog hold on legend axis` | **Best fit on this hardware.** `plot` → 320×156 is a natural map; `NeoLanguageApp` already has a graphics overlay |
| **Control systems** | `tf bode step rlocus` | Bode/step on a pocket screen is a genuinely good calculator demo; toolbox in MATLAB (paid) |
| **Strings / `fprintf` / `format`** | `disp sprintf fprintf format short long` | Fits, but see §6 — column widths kill it fast |
| **File I/O** | `save load` (`.mat` v4/v5) | Writing MAT v5 by hand is doable; reading needs care. Low priority |
| **Units** | (not MATLAB — Simulink only) | NumOS already has `NeoUnits`; MATLAB has nothing here |
| **Toolboxes at large** | Simulink, DSP, Image, Deep Learning | Out of scope. Do not promise |

Two facts worth internalising: **RunMat** (Rust, open) covers MATLAB's core
language with **400+ builtins**, and **ccalc** ships Octave/MATLAB syntax as
*one binary with no runtime*. Both are proof that the *language* is a small,
bounded implementable target once you deliberately exclude toolboxes.

---

## 4. Prior art — does anything else do this?

**Direct answer: no.** No OEM graphing calculator runs MATLAB, MathWorks ships
no handheld, and I found no third-party MATLAB interpreter for any calculator
platform. TI-92/Voyage used **Derive**; HP's CAS lineage is its own; Casio's is
its own.

The MathWorks mobile artifact is **MATLAB Mobile**, and it is a **remote client**
— it drives either a MathWorks Cloud session or a desktop MATLAB through the
**MATLAB Connector**. It is not an engine on the device. That distinction is the
whole design lesson: MathWorks' own answer to "MATLAB in your hand" was *thin
client to a real session*, not *port the engine*.

Nearest neighbours, i.e. the tradition NumOS is already standing in:

| Precedent | What it proves |
|:--|:--|
| **KhiCAS / χCAS** (Bernard Parisse) — Giac/Xcas ported to **HP Prime, TI-Nspire CX/CX II, Casio CG50, NumWorks** | A desktop-grade CAS *does* fit on a handheld. NumOS already vendors this exact lineage (`lib/giac`, KhiCAS profile) — so "big engine on a calc" is not a risk here, it's shipped |
| **TI-Nspire Lua** (official, OS 3.0+) | On-calc scripted language with a real ecosystem (ticalc.org, Inspired-Lua). Precedent that a calc *should* ship a language, and that the community will build for it |
| **HP Prime Python** (subset, since fw 2.1.15048) + HP PPL | OEMs ship *subsets* and say so. A subset is a legitimate product, not a failure |
| **NumWorks Python** | Same |
| **RunMat** (Rust, 400+ builtins, browser/WASM) and **ccalc** (single binary) | MATLAB-syntax front-ends can be *small*. This is the sizing evidence for scope A |
| **`Madona: Run Matlab/Octave code`** (Android) | There is real demand for pocket MATLAB-syntax, and it is currently served by a bad phone app |

Nobody has combined *KhiCAS-class engine* + *MATLAB syntax* + *real keypad*.
That gap is the actual finding.

---

## 5. Three candidate scopes

### A — `.m` dialect on-device (the "MATLAB calculator" answer)
A MATLAB-syntax mode: array type, 1-based indexing with `end`, `:` ranges,
element-wise `.*` family, `\` solve, command syntax (`hold on`, `format short`),
MATLAB-shaped `plot`, and MATLAB names aliased onto existing behaviour
(`solve`/`diff`/`integrate` already exist in `NeoStdLib`; Giac backs them).

- **Sits on:** `NeoValue` (has `List`, `Symbolic`, `Dict`, `Quantity`; **no
  array type yet**), `NeoParser` (`[` is both list literal and index — one
  ambiguity to resolve), `NeoStdLib` (1,903 lines of builtins), Giac.
- **New work:** array value type + copy-on-write, `.*`-family operator tokens,
  1-based indexing + `end`, backslash, ~40–60 builtin aliases, MATLAB-aware
  pretty-printer for a 40-col screen.
- **Cost estimate:** the whole Neo layer is **7,537 lines** across 10 files.
  An array type + dialect layer is plausibly **+2,000–3,000 lines**, i.e.
  roughly **100–200 KiB flash** — but **this is unmeasured**. The budget is
  real: `boards/numos-16mb.csv` puts each app slot at 6.5 MiB and the last
  measured image (5,793,581 B, 2026-10-03) leaves **~977 KiB headroom** per
  slot. A fits on paper with room to spare; it still needs a measured build.
- **Risk:** medium. The array type is the real cost, the syntax is not.

### B — Real MATLAB as a network tool server (the honest "we have MATLAB" answer)
`AiApp` already **dispatches tool servers by name** and wires exactly one
(`wolfram`). MathWorks publishes an **official MATLAB MCP Server** (Go, 26
releases, tools: *start/quit MATLAB*, *write and run MATLAB code*, *assess code
for style and correctness*). Add `matlab` as a second dispatcher entry.

- **Gets:** 100% real MATLAB, every toolbox, on the actual desktop, with the
  calculator as the input device and the answer screen.
- **Requires:** MATLAB R2021a+ on a reachable machine, and a bridge — the server
  is **stdio transport**, so a small HTTP wrapper on the PC is needed. That is
  consistent with the planned master-MCP-server architecture, not against it.
- **Cost:** low build cost, zero flash cost, no engine risk.
- **Risk:** low technically; **zero value offline**, and useless to anyone
  without a MATLAB licence. It is a power feature, not a headline.

### C — NumOS as a MATLAB/Simulink peripheral (the engineering answer)
Simulink's **Support Package for Arduino Hardware** officially lists
**ESP32 WROOM** and **ESP32 WROVER** — processor `ESP32-D0WDQ6`, i.e. **LX6**.
NumOS is **ESP32-S3 (LX7)**, which is **not on the list**. MathWorks documents a
"add custom ESP32 Arduino-compatible boards" path, but the example is built
around WROOM/WROVER. So C is **not proven** and must not be promised.

- **If** it works, the story is strong: design a filter in Simulink, flash it,
  carry the *physical* thing — and log/plot on-device. That is what an
  engineering student actually wants a handheld for.
- **Risk:** high and external (MathWorks' support matrix). Park it.

### Recommendation

**Build A, keep B cheap, park C.** A is the only one that is *a product*; B is a
two-hundred-line dispatcher entry that makes the spec sheet honest; C is a
partnership question, not an engineering one.

**Do not** frame A as "MATLAB on your calculator". MATLAB is a MathWorks
trademark and MathWorks is protective of compatibility claims. Ship it as
*"runs `.m` files (subset)"* / *"MATLAB-syntax mode"* and let users draw the
conclusion. Octave and RunMat both live inside that line; stepping over it is a
free legal problem on a product he intends to sell.

---

## 6. What the calculator form factor actually solves

Honest problems, in order of how real they are:

1. **Hands-occupied moments.** Wiring a breadboard, holding a probe, standing at
   a machine. A laptop is a two-handed, put-it-down object; a calculator is not.
   This is the one advantage no laptop has.
2. **Time-to-answer.** Not MATLAB's startup — the *laptop's*: unlock, find it,
   open the lid, wait, find the licence, launch. `Calculation → answer` on the
   keypad beats `laptop → MATLAB → prompt` for small work, which is most work.
3. **Licence-free access.** Campus licence expired, lab machine, a friend's
   house, a summer job without a seat. A calculator always has its licence
   with it.
4. **Battery and bag life.** Weeks of standby vs hours. It lives where the work
   is instead of in a charger.
5. **No distraction surface.** No tabs, no Slack. On a bench that matters.
6. **It is the only place a 320×156 plot exists.** `plot(…), grid on, axis
   equal` on a fixed tiny canvas is *more* legible than the same plot in a
   resizable window you then have to arrange.
7. **Exam reality — flag it, don't sell it.** MATLAB is banned from essentially
   every exam regime (`fx-CG50`/`Nspire` policies allow *CAS or not*, never
   MATLAB). Consistent with the main unit's positioning: this is the
   **not-exam-legal** arm, which is exactly the capability the project is built
   to stop apologising for.

And what the form factor **cannot** do — these belong in the scope so nobody
promises them later:

- **The screen is the binding constraint, not RAM.** Canvas is **320×156**;
  at ~8 px/char that is **≈40 columns × ≈12 rows**. MATLAB's default output
  (`A =\n\n    1.0000    2.0000 …`) blows through 40 columns at 4 columns of
  `%8.4f` reals. So: matrices up to **~4×4 comfortably, 6×6 cramped**, and the
  pretty-printer has to switch to `format short` / engineering notation on its
  own. A 100×100 matrix is computable and invisible — pick which one the
  product is.
- **Keypad-only means typing is the cost.** `A = [1 2 3; 4 5 6]` is ~40 presses.
  MATLAB's terseness helps here (no braces, no `print()`), which is a real
  argument *for* MATLAB syntax over Python on this hardware.
- **No SD on v1** — `.m` files and `.mat` output go to LittleFS via
  `hal/FileSystem` and must fail soft.
- **`ode45` long runs, `fft` on big N, image/DSP toolboxes:** a battery and a
  patience problem before it is a memory problem.

## 7. Risks logged

| Risk | Note |
|:--|:--|
| **Flash** | ~977 KiB headroom per 6.5 MiB slot (last measured image 5,793,581 B); A estimated 100–200 KiB but **unmeasured**. Measure before promising |
| **Trademark** | never claim "MATLAB" or "MATLAB compatible" in product copy; "MATLAB-syntax" / "runs `.m` subset" |
| **Scope balloon** | the toolbox surface is infinite. Fix the function list in the spec, ~120–150 builtins, and refuse additions by default |
| **Simulink/S3** | ESP32-S3 is off MathWorks' supported list — do not put it on a spec sheet |
| **Two languages, one device** | `NeoLanguage` is Python-shaped (`def/elif/while/for/in/import/try`). Shipping a second syntax risks the "which one do I learn" problem; the mitigation is that A is a *mode*, not a second app, and shares the stdlib |
| **Maintenance** | two dialects on one value model is a permanent tax on every `NeoValue` change |

## 8. Open questions for Paadshi

1. Is MATLAB a **feature of the .m-file crowd** (students who already write `.m`)
   or a **marketing line**? Those want different things from scope A.
2. Does A live inside **NeoLanguage as a dialect mode** (shared stdlib, one app,
   one maintenance tax) or as its own app? I'd argue dialect mode.
3. Is the array type allowed to be **PSRAM-backed** with copy-on-write, or must
   it stay in internal RAM? That single answer changes the max matrix size.
4. Does the **Wolfram tool-server slot** get generalised now (so `matlab`
   arrives for ~free), or does AI wiring wait?

## 9. First concrete step

Before any code: **measure the array type, not the syntax.** One host harness
that builds a `NeoValue` array type with MATLAB index semantics and reports
(a) flash delta on `esp32s3_n16r8`, (b) the pretty-printer's real column budget
at the Casio font. If the flash delta is under ~120 KiB, scope A is real and the
dialect layer is a week of aliasing against `NeoStdLib`. If it is 300 KiB, the
answer is B only and the honest product line is "NumOS talks to your MATLAB".

---

### Evidence index

- `docs/GIAC_FEASIBILITY_SPIKE.md` — Giac in firmware, 2.66 MiB flash, symbolic
  timings, KhiCAS lineage
- `src/apps/NeoValue.h` — types: Null, Boolean, Number, Exact, Symbolic,
  Function, **List**, NativeFunction, Quantity, String, Dictionary, Math, Error
  (**no array type**)
- `src/apps/NeoLexer.cpp` — keyword table is Python-shaped
- `src/apps/NeoParser.cpp:707` — `[` serves as list literal *and* index
- `src/apps/AiApp.cpp:643` — `if (_toolServer != "wolfram") return false;`
  (single hardcoded tool server)
- MathWorks *Supported Arduino Hardware* — ESP32 WROOM/WROVER, ESP32-D0WDQ6
- MathWorks *Add Custom ESP32 Arduino-Compatible Boards to Support Package*
- `github.com/matlab/matlab-mcp-server` — official, Go, stdio, start/run/assess
- MathWorks *MATLAB System Requirements* — 4.6 GB / 5–8 GB / 25 GB
- runmat.com/docs/compatibility — 400+ builtins, core language only
- KhiCAS/χCAS (Parisse) — HP Prime, Nspire CX/CX II, CG50, NumWorks
- `github.com/holgertkey/ccalc` — Octave/MATLAB syntax, single binary

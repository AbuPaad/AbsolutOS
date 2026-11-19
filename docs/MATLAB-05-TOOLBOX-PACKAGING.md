# MATLAB-05-TOOLBOX-PACKAGING — how to build and ship the add-on

Date: 2026-10-18 · Research note. No code written, nothing built.
Companion to `MATLAB-04-MATLAB-ADDON.md` (what it does). This is *how it ships*.

---

## 1. Verdict

**Reasonable — but the toolbox is not the feature, it is the distribution.**

A folder of `.m` files on GitHub is something nobody finds. A toolbox published to
**File Exchange** and auto-synced from **GitHub Releases** appears in MATLAB's
**Add-On Explorer** — the search box MATLAB users actually use, and a software
discovery surface no competitor (TI, Casio, HP) can buy into. Free, permanent,
in-product, and the ~24 h moderation queue lends it a legitimacy a blog post
never will. That, not the packaging mechanism, is the argument for doing it.

## 2. Building the package

### Use the programmatic route

```matlab
opts = matlab.addons.toolbox.ToolboxOptions(packageFolder, identifier);
opts.ToolboxName          = "NumOS Bridge";
opts.ToolboxVersion       = "0.1.0";
opts.Summary              = "Pull matrices from a NumOS calculator into MATLAB.";
opts.Description          = "...";
opts.AuthorName           = "...";
opts.AuthorEmail          = "...";
opts.OutputFile           = "release/NumOSBridge.mltbx";
opts.MinimumMatlabRelease = "R2017a";
opts.SupportedPlatforms.Win64        = true;
opts.SupportedPlatforms.Glnxa64      = true;
opts.SupportedPlatforms.Mac          = true;
opts.SupportedPlatforms.MatlabOnline = true;
matlab.addons.toolbox.packageToolbox(opts);
```

**Why this route and not the GUI:** the packaging UI moved in **R2025a** to a
"package task" inside a MATLAB *project*, and **R2026b** adds a TOML project
format. Those instructions differ per release, so GUI steps don't survive a
version change. `ToolboxOptions` has been stable since **R2023a** and is the only
path that scripts cleanly and behaves the same across releases.

### Sharp edges, in order of how much time they cost

| # | Edge | Consequence |
|:--|:--|:--|
| 1 | **`identifier` must be one fixed UUID, forever** (RFC 4122) | Change it and MATLAB installs the new build as a *separate package* instead of updating — users end up with two copies on their path. Generate once, commit it to the repo, never touch it |
| 2 | Building requires **MATLAB** | You cannot produce a legitimate `.mltbx` without one. Authoring the `.m` files needs nothing, so design/tests can happen before you have a licence; only the final package step is gated |
| 3 | **`MinimumMatlabRelease` vs the APIs you use** | `webread`/`webwrite`/`jsondecode`/`string` together put the real floor at **R2017a**. Declaring R2016a gets you installs that fail at runtime |
| 4 | App registration needs the right folder/manifest | A `uifigure` app must be arranged so it lands under **APPS**; otherwise it's just a function nobody calls |
| 5 | P-code + `.m` same name | MATLAB silently excludes the `.m` from the package. Don't ship both |

## 3. Shipping it

### The pipeline worth building once

1. Tag a **GitHub Release** containing `NumOSBridge.mltbx`.
2. Link the repo to File Exchange (**recommended connection method: GitHub Releases**).
3. File Exchange + Add-On Explorer **update automatically** on each new release.

That is the whole distribution. No manual re-uploads, no hosting, no account
infrastructure — matches the BYOK / no-cloud posture already chosen.

### Licence constraint you must decide early

- Uploading files **directly** to File Exchange forces the **BSD** licence
  (auto-added, text not alterable).
- **A GitHub-linked submission may use any licence.**

The firmware repo is GPL. If the toolbox shares any code with it, or you simply
want GPL everywhere, **link via GitHub** — a direct upload would force BSD and
create a licence split across your own project.

### Moderation

Submissions sit in **Pending approval**; the widely reported wait is ~24 h,
worst case a couple of business days. Not a blocker, but don't submit the night
before a demo.

### Signing

Package trust metadata comes from the fields you set. I found no evidence of a
MATLAB-level "unknown publisher" warning on locally built `.mltbx` — the
"Unknown Publisher" literature is about Windows UAC on compiled installers.
Treat as **unverified**, not as "safe".

## 4. Reach, honestly

- **MATLAB Online (basic) is free** — 20 h/month, 10 products (MATLAB, Simulink,
  Control System, Curve Fitting, …), 5 GB Drive, 15 min idle timeout. So *some*
  zero-licence users can reach the add-on.
- **But** whether a third-party `.mltbx` can be installed in MATLAB Online basic
  is **unverified**. Add-On Explorer installs should work once published;
  uploading a local `.mltbx` into the browser session is the questionable part.
- Practical floor for this add-on is therefore: **a student with a university
  licence** — which, per MathWorks' own page, most engineering students have.
  That's the right target anyway.

## 5. Cost of ownership

- **Free.** No fees, no revenue share, no review fee.
- **You cannot meaningfully monetise it inside File Exchange** — it is BSD/GitHub
  licensed community code. Correct call here: the toolbox exists to sell
  calculators, not to earn.
- **Support burden is real**: MATLAB version differences, IP configuration,
  firewall, "why won't it connect". Mitigated in `MATLAB-04` §7 by writing actual
  error messages instead of stack traces.
- Selling `.m` code you write is permissible on a Standard licence (not competing
  with MathWorks), but that is forum-level guidance, not a contract — irrelevant
  if you ship free.

## 6. What this does *not* change

The toolbox is a pipe. It is still worth nothing until the calculator produces
something worth pulling. **`matrixToCode` first** (`MATLAB-02-USECASE.md` §8),
then the push route, then the package. Building the toolbox first is building the
envelope before the letter.

---

### Evidence

- MathWorks *Interactively Create and Share Packages* — R2025a project/package-task
  change, R2026b TOML
- `matlab.addons.toolbox.ToolboxOptions` — R2023a+, UUID/RFC 4122 requirement,
  identifier stability across versions
- `matlab.addons.toolbox.packageToolbox` — R2016a
- `mpminstall` — R2024b+; `installToolbox` deprecated R2026b
- *File Exchange Licensing FAQ* — BSD forced for direct upload; other licences
  allowed when GitHub-linked
- *About File Exchange* — GitHub Releases is the recommended connection; 250 MB
  limit; Publish flow
- *MATLAB Online Versions* — basic tier: free, 20 h/month, 10 products
- MathWorks Answers on FEX approval timing — ~24 h typical

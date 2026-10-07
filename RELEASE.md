# NumOS Firmware Release Runbook

**Audience: an autonomous coding agent.** This is the exact, minimal procedure to
build new NumOS firmware and publish it to GitHub Releases so the on-device OTA
updater can find and install it. Follow it literally; the invariants in
[Do not break these](#do-not-break-these) are what make OTA work.

---

## TL;DR — the only path you normally need

```bash
# 1. (optional) bump the minor version for a feature release
echo "1.1" > VERSION

# 2. commit and push to main
git add -A && git commit -m "release: NumOS 1.1"
git push origin main
```

GitHub Actions (`.github/workflows/compile-and-release.yml`) then builds the
`esp32s3_n16r8` environment, tags `v<MAJOR>.<MINOR>.<run_number>`, and publishes
a **stable** release with exactly one asset named **`NumOS.bin`**.

To release without a new commit (e.g. a rebuild or a retry):

```bash
gh workflow run compile-and-release.yml
```

Watch it:

```bash
gh run list --workflow=compile-and-release.yml --limit 5
gh run watch "$(gh run list --workflow=compile-and-release.yml --limit 1 --json databaseId --jq '.[0].databaseId')"
```

That is the whole job. The rest of this file is what to check when it does not
behave, and how to build/publish by hand if CI is unavailable.

---

## Version model

| Piece | Source | Example |
|---|---|---|
| `MAJOR.MINOR` | repo-root `VERSION` file (single line) | `1.0` |
| `.PATCH` | `github.run_number` in CI; `NUMOS_BUILD_NUMBER` env locally | `57` |
| Full version | `MAJOR.MINOR.PATCH` | `1.0.57` |
| Git tag | `v` + full version | `v1.0.57` |
| Compiled define | `-DNUMOS_VERSION="<full version>"` via `scripts/build_metadata.py` | `"1.0.57"` |

The firmware compares its embedded `NUMOS_VERSION` against the release
`tag_name` (leading `v` stripped). **The tag and the embedded version must be the
same string**, which is why both are derived from the same inputs.

- Patch bumps automatically on every workflow run. Never reuse a tag.
- Bump `VERSION` only for a deliberate minor/major release.
- Local builds with no `NUMOS_BUILD_NUMBER` become `X.Y.0`.

---

## What CI does (step by step)

File: `.github/workflows/compile-and-release.yml`

1. `Resolve version` — `BASE=$(cat VERSION)`, `VER="${BASE}.${NUMOS_BUILD_NUMBER}"`, exports `NUMOS_VERSION`.
2. `Patch platformio.ini for Ubuntu` — strips the Windows-only `build_dir = C:/.piobuild/...`.
3. Installs PlatformIO and runs `pio run -e esp32s3_n16r8`.
4. `Locate and rename firmware` — finds `firmware.bin`, copies it to **`NumOS.bin`**, computes `FIRMWARE_SHA256`.
5. `Create GitHub Release` — `ncipollo/release-action`, tag `v$NUMOS_VERSION`, `prerelease: false`, `makeLatest: true`, artifact `NumOS.bin`, SHA-256 in the body.

Commit-message skip tokens (any commit on `main`): `[skip ci]`, `no build`,
`[no build]`, `[SKIP CI]`.

---

## Invariants — do not break these

1. **Environment is `esp32s3_n16r8`.** CI builds this exact env.
2. **The asset is named `NumOS.bin`, exactly, one per release.** The device
   fetches `https://github.com/AbuPaad/AbsolutOS/releases/latest/download/NumOS.bin`.
   Renaming it (dates, versions, spaces) breaks OTA for every device.
3. **Releases must be stable.** `prerelease: false` + `makeLatest: true`. The
   device queries `api.github.com/repos/AbuPaad/AbsolutOS/releases/latest`, which
   ignores prereleases and drafts.
4. **Tag == embedded `NUMOS_VERSION`.** Both come from `VERSION` + patch; do not
   hand-write one without the other.
5. **The asset is a bare app image**, written at `0x10000` (or OTA'd into the
   spare bank). Do not upload a merged 16 MiB factory image as `NumOS.bin`.
6. **The partition table is OTA-shaped.** `boards/numos-16mb.csv` must keep
   `app0` + `app1` + `otadata`. `scripts/check-production-target.py` enforces it.
7. **`NUMOS_OTA_REPO`** (`src/net/OtaUpdater.cpp`, default `AbuPaad/AbsolutOS`)
   must match the repo that actually holds the release.

---

## Files that participate

| File | Role |
|---|---|
| `VERSION` | `MAJOR.MINOR` source of truth |
| `scripts/build_metadata.py` | reads `VERSION` + `NUMOS_BUILD_NUMBER`, defines `NUMOS_VERSION` |
| `platformio.ini` | `[env:esp32s3_n16r8]` builds with `extra_scripts = pre:scripts/build_metadata.py` |
| `.github/workflows/compile-and-release.yml` | builds, tags, publishes |
| `boards/numos-16mb.csv` | dual 6.5 MiB banks + `otadata` |
| `src/net/OtaUpdater.{h,cpp}` | device-side check + flash |
| `src/apps/SettingsApp.cpp` | Settings → System Update UI |

---

## Manual / CI-less release (fallback)

Use this only if GitHub Actions is unavailable. The version you embed **must**
match the tag you create.

```bash
set -euo pipefail
BASE="$(tr -d '[:space:]' < VERSION)"
# Pick a patch strictly greater than every existing v${BASE}.* tag.
PATCH="$(gh release list --limit 100 --json tagName --jq '[.[].tagName] | length + 1')"

NUMOS_BUILD_NUMBER="$PATCH" pio run -e esp32s3_n16r8

# PlatformIO's build_dir is C:/.piobuild/numOS unless PLATFORMIO_BUILD_DIR is set.
FW="C:/.piobuild/numOS/esp32s3_n16r8/firmware.bin"
[ -f "$FW" ] || FW=".pio/build/esp32s3_n16r8/firmware.bin"
cp "$FW" NumOS.bin

SHA="$(sha256sum NumOS.bin | cut -d' ' -f1)"

gh release create "v${BASE}.${PATCH}" NumOS.bin \
  --latest \
  --title "NumOS v${BASE}.${PATCH}" \
  --notes "Over-the-air update supported. SHA256: \`${SHA}\`"
```

`--latest` is what makes `/releases/latest` resolve to this release. Never use
`--prerelease`.

---

## Verify a release (do this after every release)

```bash
# 1. The release is the latest, stable, and has exactly one asset named NumOS.bin
gh release view --json tagName,isLatest,isPrerelease,assets

# 2. The public API agrees  (tag_name + asset name)
gh api repos/AbuPaad/AbsolutOS/releases/latest \
  --jq '{tag: .tag_name, latest: .assets[].name, digest: .assets[].digest}'

# 3. The stable download URL resolves (expect HTTP 302 then 200)
curl -sIL https://github.com/AbuPaad/AbsolutOS/releases/latest/download/NumOS.bin | head -n 20
```

Expected: `isLatest == true`, `isPrerelease == false`, asset list is exactly
`["NumOS.bin"]`, and the download URL returns a file of several megabytes.

---

## On-device acceptance

Prerequisites the updater checks and will name if missing:

- Wi-Fi associated (`net::Wifi`), and
- the clock synced via NTP (`net::Clock`) — TLS rejects an unset RTC.

Then on the calculator: **Settings → System Update**.

- `Check for update` queries `/releases/latest` and compares semver.
- `Install update` downloads `NumOS.bin` into the spare bank, verifies, and reboots.
- An automatic check also runs once per boot, a few seconds after Wi-Fi + NTP.

A unit that still has the **old single-slot partition table** cannot OTA
(`esp_ota_get_next_update_partition` returns null → "no spare OTA bank"). It
needs one USB reflash of the new table first; that reflash also wipes LittleFS.

---

## Failure modes and fixes

| Symptom | Cause | Fix |
|---|---|---|
| Device: `releases/latest HTTP 404` | release is prerelease/draft, or no release | publish a stable `makeLatest: true` release |
| Device: `firmware download HTTP 404` | asset not named `NumOS.bin` | rename the asset |
| Device: `no spare OTA bank` | old single-slot table on the unit | USB-reflash `boards/numos-16mb.csv` layout once |
| Device: `clock not set (no NTP)` | RTC unset, TLS fails | ensure NTP reachable |
| Device: `firmware digest mismatch` | release asset changed after publish | republish `NumOS.bin` |
| Device keeps updating forever | tag ≠ embedded version | regenerate with matching `VERSION`/patch |
| CI: `Could not locate firmware.bin` | build failed or wrong env | inspect the build log; confirm `-e esp32s3_n16r8` |
| CI: tag already exists | tag collision | should not happen (run number is monotonic); bump `VERSION` |
| GitHub API 403 rate limit | too many anonymous checks from one IP | wait, or point `NUMOS_OTA_REPO` at an authenticated proxy |

---

## Release checklist

- [ ] Build is green in CI for `esp32s3_n16r8`.
- [ ] Tag is `v<MAJOR>.<MINOR>.<run_number>` and the release is **stable/latest**.
- [ ] Exactly one asset: `NumOS.bin` (bare app image, not a merged factory image).
- [ ] SHA-256 in the release body; `digest` present on the asset.
- [ ] `/releases/latest/download/NumOS.bin` resolves.
- [ ] A device on the OTA table sees "Update available" and installs + reboots.

#!/usr/bin/env python3
"""Build the NumOS web emulator and stage it for a static host (GitHub Pages).

WHAT THIS IS FOR
----------------
The website is a static site on GitHub Pages. That has three consequences this
script is built around:

  1. NO SERVER CODE. GitHub Pages serves files only. Anything that needs a secret
     or a runtime decision (chiefly: minting a capped AI key per session) cannot
     live on the site and must be a separate edge function. The site ships the
     emulator with transport=replay, which needs no key at all.
  2. NO CUSTOM HEADERS. Pages cannot send COOP/COEP, so nothing may depend on
     SharedArrayBuffer. The wasm build is verified thread-free below and the run
     aborts if that ever changes, because the failure mode is a blank screen with
     a console error nobody will read.
  3. PER-FILE AND SITE SIZE LIMITS. 100 MB per file, 1 GB per site. The wasm
     binary is by far the biggest artifact, so it is measured and reported every
     run instead of being discovered at push time.

WHAT IT DOES NOT DO
-------------------
It does not write or restyle the site, and it does not run `git`. It stages a
self-contained directory; committing and pushing stay yours.

Usage:
    python3 scripts/publish_web_demo.py                 # build + stage
    python3 scripts/publish_web_demo.py --no-build      # stage what was built
    python3 scripts/publish_web_demo.py --check         # report, write nothing
    python3 scripts/publish_web_demo.py --dest ~/site/emulator --dist <dir>
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NoReturn

REPO = Path(__file__).resolve().parent.parent
WASM_DIR = REPO / "wasm"
DEFAULT_DIST = REPO / "out" / "wasm" / "dist" / "release"
DEFAULT_DEST = Path.home() / "musings" / "numos-website" / "emulator"

# Hard limits from GitHub Pages. A build that trips one of these cannot be
# deployed by pushing, and the useful moment to say so is now, not at push time.
PAGES_MAX_FILE_BYTES = 100 * 1024 * 1024
PAGES_MAX_SITE_BYTES = 1024 * 1024 * 1024

# Files in the package that are text and therefore scannable for a secret.
SCANNABLE_SUFFIXES = {".js", ".mjs", ".json", ".html", ".css", ".map"}

# A provider key that ever reaches the web bundle is public the moment the page
# loads: the artifacts are static and fully readable, so `curl` on the .js is all
# an attacker needs. There is no such thing as a key that is "only in the wasm".
SECRET_PATTERNS = (
    re.compile(r"sk-or-[A-Za-z0-9_\-]{8,}"),
    re.compile(r"Bearer\s+[A-Za-z0-9_\-]{20,}"),
)


def fail(message: str) -> NoReturn:
    print(f"publish_web_demo: ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def human(size: int) -> str:
    value = float(size)
    for unit in ("B", "KiB", "MiB", "GiB"):
        if value < 1024 or unit == "GiB":
            return f"{value:.1f} {unit}" if unit != "B" else f"{int(value)} B"
        value /= 1024
    return f"{value:.1f} GiB"


def preflight() -> None:
    """Refuse to start a build we cannot finish, before spending minutes on it."""
    required = (WASM_DIR / "emscripten.version").read_text(encoding="utf-8").strip()
    emcc = shutil.which("emcc")
    if emcc is None:
        fail(
            f"Emscripten {required} is not installed, so the web build cannot be "
            "produced here.\n"
            "  Install it (about 1 GB), then re-run:\n"
            "    git clone https://github.com/emscripten-core/emsdk ~/emsdk\n"
            f"    ~/emsdk/emsdk install {required} && ~/emsdk/emsdk activate {required}\n"
            "    source ~/emsdk/emsdk_env.sh\n"
            "  Or stage an existing build with --no-build.",
        )
    probe = subprocess.run(
        [emcc, "--version"], capture_output=True, text=True, check=False)
    found = ""
    first = (probe.stdout or "").splitlines()[:1]
    if first:
        bits = first[0].split()
        for index, token in enumerate(bits):
            if "emcc" in token and index + 1 < len(bits):
                found = bits[index + 1]
                break
    if found != required:
        fail(f"wasm/build.sh pins Emscripten {required}; found {found or 'unknown'}")


def run_build() -> None:
    script = WASM_DIR / "build.sh"
    if not script.exists():
        fail(f"missing {script}")
    print(f"publish_web_demo: building (wasm/build.sh Release) — this takes a while")
    result = subprocess.run(["bash", str(script), "Release"], cwd=REPO, check=False)
    if result.returncode != 0:
        fail(f"wasm/build.sh exited {result.returncode}")


def load_manifest(dist: Path) -> dict:
    manifest_path = dist / "numos-assets.json"
    if not manifest_path.exists():
        fail(
            f"no packaged build at {dist} (numos-assets.json missing).\n"
            "  Build it first: bash wasm/build.sh Release",
        )
    try:
        return json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"unreadable manifest {manifest_path}: {error}")


def verify_threads(manifest: dict) -> None:
    """GitHub Pages cannot serve COOP/COEP, so a threaded build would dead-end."""
    if manifest.get("build", {}).get("pthreads"):
        fail(
            "this package declares pthreads=true, but GitHub Pages cannot send the "
            "COOP/COEP headers SharedArrayBuffer needs. The emulator would fail to "
            "start on the deployed site. Drop the threaded build or host elsewhere.",
        )


def verify_references(dist: Path, manifest: dict) -> list[str]:
    """Every referenced asset must exist, and by a relative URL.

    A root-absolute URL ("/emulator/x.js") would work on a custom domain but
    break on a project page served from /<repo>/, which is the common case.
    """
    problems: list[str] = []
    assets = manifest.get("assets", {})
    for name, entry in assets.items():
        url = str(entry.get("url", ""))
        if not url:
            problems.append(f"asset {name} has no url")
            continue
        if not url.startswith("./"):
            problems.append(
                f"asset {name} url {url!r} is not relative — a project page under "
                "/<repo>/ would 404 on it")
        target = dist / url[2:] if url.startswith("./") else dist / url
        if not target.exists():
            problems.append(f"asset {name} url {url!r} has no file on disk")
    for required in ("index.html",):
        if not (dist / required).exists():
            problems.append(f"missing {required} in the package")
    return problems


def verify_no_secrets(dist: Path) -> list[str]:
    """A key in the bundle is a published key. Check, do not assume."""
    hits: list[str] = []
    for path in sorted(dist.rglob("*")):
        if not path.is_file() or path.suffix not in SCANNABLE_SUFFIXES:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for pattern in SECRET_PATTERNS:
            match = pattern.search(text)
            if match:
                hits.append(f"{path.name}: looks like a credential ({match.group(0)[:12]}…)")
    return hits


def stage(dist: Path, dest: Path, check_only: bool) -> dict:
    files = [path for path in sorted(dist.rglob("*")) if path.is_file()]
    if not files:
        fail(f"nothing to stage in {dist}")
    total = sum(path.stat().st_size for path in files)
    report = {"files": len(files), "bytes": total, "dest": str(dest)}

    print(f"publish_web_demo: {len(files)} files, {human(total)} total")
    biggest = max(files, key=lambda path: path.stat().st_size)
    print(f"  largest: {biggest.name} ({human(biggest.stat().st_size)})")

    if biggest.stat().st_size > PAGES_MAX_FILE_BYTES:
        fail(
            f"{biggest.name} is {human(biggest.stat().st_size)}, over the GitHub "
            f"Pages per-file limit of {human(PAGES_MAX_FILE_BYTES)}. Git LFS does "
            "not help (Pages does not serve LFS objects): compress the artifact or "
            "host the bundle elsewhere and point the page at it.",
        )
    if total > PAGES_MAX_SITE_BYTES:
        fail(f"staged site would be {human(total)}, over the 1 GB Pages limit")

    if check_only:
        return report

    # Copy over the previous staging, but only the files this package owns: a
    # stale asset left behind from an older build is how a page ends up loading a
    # bundle that no longer matches its manifest.
    if dest.exists():
        for entry in dest.iterdir():
            if entry.is_dir():
                shutil.rmtree(entry)
            else:
                entry.unlink()
    else:
        dest.mkdir(parents=True)
    for path in files:
        target = dest / path.relative_to(dist)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    report["copied"] = True
    return report


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dest", type=Path, default=DEFAULT_DEST,
                        help=f"where to stage the bundle (default: {DEFAULT_DEST})")
    parser.add_argument("--dist", type=Path, default=DEFAULT_DIST,
                        help=f"packaged build to publish (default: {DEFAULT_DIST})")
    parser.add_argument("--no-build", action="store_true",
                        help="stage an existing build instead of building")
    parser.add_argument("--check", action="store_true",
                        help="validate and report, write nothing")
    args = parser.parse_args()

    if not args.no_build and not args.check:
        preflight()
        run_build()

    dist = args.dist.expanduser()
    manifest = load_manifest(dist)
    verify_threads(manifest)

    problems = verify_references(dist, manifest)
    if problems:
        for problem in problems:
            print(f"publish_web_demo: {problem}", file=sys.stderr)
        fail(f"{len(problems)} packaging problem(s); refusing to stage a broken bundle")

    secrets = verify_no_secrets(dist)
    if secrets:
        for hit in secrets:
            print(f"publish_web_demo: {hit}", file=sys.stderr)
        fail("a credential-shaped string is in the web bundle; publishing it makes "
             "it public. Remove it from the build inputs and rebuild.")

    report = stage(dist, args.dest.expanduser(), args.check)

    identity = manifest.get("build", {}).get("identity", "unknown")
    print(f"publish_web_demo: build identity {identity}")
    print(f"publish_web_demo: threads disabled (Pages-compatible), no key in bundle")
    if report.get("copied"):
        print(f"publish_web_demo: staged to {report['dest']}")
        print("  Serve that directory as-is: index.html mounts <numos-emulator>.")
        print("  For a project page, reference it as ./emulator/index.html and keep")
        print("  the whole directory together — the manifest's asset URLs are relative.")
    else:
        print("publish_web_demo: --check, nothing written")
    print("publish_web_demo: no server-side code is included; the AI arm runs on "
          "the replay fixture, so the site needs no key, no quota and no backend.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

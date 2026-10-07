#!/usr/bin/env python3
"""
Prepare a THROWAWAY emulator fs-root for the Wolfram|Alpha check.

Why a copy at all: the check requires an AppID to be OFFERED (no AppID = the tool
is not offered, by design), and the tracked fixture must stay keyless so the host
test's "keyless on the host" assertion keeps meaning something. So the AppID goes
into a clone that is deleted afterwards.

The value written here is the vendor's OWN public demo AppID, which is published
in Wolfram's API documentation and needs no account. It never authenticates
anything (every probe against it answers 401 Invalid appid) — that is the point:
it makes the emulator's live transport exercise the real wire, and it is not a
secret of anybody's. A real AppID must never be written here; use
`make_live_fs.py`'s key-file pattern for that.

Usage:
  make_check_fs.py prepare [transport]   -> builds tests/emulator/fs-check
  make_check_fs.py scrub                 -> deletes it

  transport: replay (default) — the recorded body in /ai/replay/wolfram-llm-api.txt,
                                no socket, nothing spent
             emulator          — real HTTPS through libcurl, against the real
                                endpoint with the demo AppID (expect a named 401)
"""
import json
import pathlib
import shutil
import sys

REPO = pathlib.Path("/home/fih/musings/Absolut-CAS/firmware/AbsolutOS")
SRC = REPO / "tests/emulator/fs"
DST = REPO / "tests/emulator/fs-check"

# Wolfram's published demo AppID. Public, keyless, cannot authenticate.
DEMO_APPID = "DEMO"


def prepare(transport: str) -> int:
    if transport not in ("replay", "emulator"):
        print(f"FAIL: unknown transport '{transport}' (replay|emulator)")
        return 1

    if DST.exists():
        shutil.rmtree(DST)
    shutil.copytree(SRC, DST)

    cfg = json.loads((SRC / "ai/config.json").read_text())
    cfg["transport"] = transport
    cfg["wa_appid"] = DEMO_APPID
    (DST / "ai/config.json").write_text(json.dumps(cfg, indent=2) + "\n")

    fixture = DST / "ai/replay/wolfram-llm-api.txt"
    if transport == "replay" and not fixture.exists():
        print(f"FAIL: no recorded body at {fixture.relative_to(REPO)}")
        shutil.rmtree(DST)
        return 1

    # The results dir keeps ONLY the seed answer and its existing check document:
    # Recent must be deterministic (row 1 = the answer this script walks to), and
    # the check document is left in place on purpose — a re-check of the same
    # query must NOT rewrite it (same-content dedupe).
    results = DST / "ai/results"
    keep = ("seed-derivative.md", "seed-derivative_Wolfram.md")
    for p in sorted(results.glob("*")):
        if p.name not in keep:
            p.unlink()
    print(f"  results    = {sorted(p.name for p in results.glob('*'))}")

    print(f"prepared {DST.relative_to(REPO)}")
    print(f"  transport  = {transport}")
    print(f"  wa_appid   = {DEMO_APPID} (Wolfram's public demo value, not a secret)")
    print(f"  wa fixture = {fixture.relative_to(REPO)}"
          if transport == "replay" else "  wa fixture = (none: live request)")
    return 0


def scrub() -> int:
    if DST.exists():
        shutil.rmtree(DST)
        print(f"removed {DST.relative_to(REPO)}")
    else:
        print(f"nothing to remove at {DST.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    cmd = sys.argv[1]
    if cmd == "prepare":
        sys.exit(prepare(sys.argv[2] if len(sys.argv) > 2 else "replay"))
    if cmd == "scrub":
        sys.exit(scrub())
    print(__doc__)
    sys.exit(2)

#!/usr/bin/env python3
"""
Prepare a THROWAWAY emulator fs-root for a live request.

Why not just use tests/emulator/fs: the key must never sit in a path that could
be committed. So we clone the fixture tree into `tests/emulator/fs-live`
(gitignored), inject the key from the caller's file THERE, run, and delete the
whole directory afterwards. This script never prints the key, its length, or
any prefix — only whether it was found and that the write succeeded.

Usage:
  make_live_fs.py prepare <key-file>     -> builds tests/emulator/fs-live
  make_live_fs.py scrub                  -> deletes it
"""
import json
import pathlib
import shutil
import sys

REPO = pathlib.Path("/home/fih/musings/Absolut-CAS/firmware/AbsolutOS")
SRC = REPO / "tests/emulator/fs"
DST = REPO / "tests/emulator/fs-live"


def prepare(key_file: str) -> int:
    kp = pathlib.Path(key_file)
    if not kp.exists():
        print(f"FAIL: no key file at {kp}")
        return 1
    key = kp.read_text().strip()          # never printed below
    if not key:
        print("FAIL: key file is empty")
        return 1

    if DST.exists():
        shutil.rmtree(DST)
    shutil.copytree(SRC, DST)

    cfg = json.loads((SRC / "ai/config.json").read_text())
    cfg["transport"] = "emulator"          # real HTTP(S) via libcurl
    cfg["base_url"] = "https://openrouter.ai/api/v1"
    cfg["model"] = "google/gemini-2.5-flash-lite"
    cfg["api_key"] = key
    (DST / "ai/config.json").write_text(json.dumps(cfg, indent=2) + "\n")

    # The results dir starts empty so the new answer is unambiguous.
    results = DST / "ai/results"
    if results.exists():
        shutil.rmtree(results)
    results.mkdir(parents=True)

    print(f"prepared {DST.relative_to(REPO)}")
    print(f"  transport = {cfg['transport']}  model = {cfg['model']}")
    print(f"  base_url  = {cfg['base_url']}")
    print(f"  key       = present, {len(key)} chars (value never shown)")
    print("  results dir emptied")
    return 0


def scrub() -> int:
    if DST.exists():
        shutil.rmtree(DST)
        print(f"removed {DST.relative_to(REPO)}")
    else:
        print("nothing to remove")
    return 0


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "prepare":
        if len(sys.argv) < 3:
            print("usage: make_live_fs.py prepare <key-file>")
            sys.exit(2)
        sys.exit(prepare(sys.argv[2]))
    if len(sys.argv) == 2 and sys.argv[1] == "scrub":
        sys.exit(scrub())
    print(__doc__)
    sys.exit(2)

"""Inject reproducible, read-only build identity into NumOS firmware."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess

Import("env")  # noqa: F821 - supplied by PlatformIO/SCons


def git_value(*args: str) -> str:
    try:
        return subprocess.check_output(
            ["git", *args], text=True, encoding="utf-8"
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def read_version() -> str:
    """``MAJOR.MINOR`` from the repo-root VERSION file (the OTA source of truth).

    A missing file must never break a local build; it just falls back to 0.0.
    """
    try:
        raw = (Path(env.subst("$PROJECT_DIR")) / "VERSION").read_text(
            encoding="utf-8"
        )
    except OSError:
        return "0.0"
    for line in raw.splitlines():
        line = line.strip()
        if line:
            return line
    return "0.0"


commit = git_value("rev-parse", "HEAD")
environment = env.subst("$PIOENV")
version = read_version()

# CI exports NUMOS_BUILD_NUMBER = github.run_number so every push to main gets a
# strictly increasing patch. The same string is what the workflow tags
# (``v<version>``), so the running firmware can trust its embedded NUMOS_VERSION
# to compare against the release tag. Local builds without it become X.Y.0.
build_number = os.environ.get("NUMOS_BUILD_NUMBER", "").strip()
if build_number:
    version = f"{version}.{build_number}"
elif version.count(".") < 2:
    version = f"{version}.0"

env.Append(
    CPPDEFINES=[
        ("NUMOS_BUILD_COMMIT", f'\\"{commit}\\"'),
        ("NUMOS_BUILD_ENVIRONMENT", f'\\"{environment}\\"'),
        ("NUMOS_VERSION", f'\\"{version}\\"'),
    ]
)

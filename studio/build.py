# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Build the self-contained Windows ARK CREW Studio distribution."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import sys

from . import __version__


ROOT = Path(__file__).resolve().parents[1]
STUDIO_ROOT = ROOT / "studio"
DIST_ROOT = STUDIO_ROOT / "dist"
OUTPUT_NAME = f"ARKCrewStudio-{__version__}-windows-x64"


def _safe_remove(path: Path) -> None:
    resolved = path.resolve()
    allowed = {DIST_ROOT.resolve(), (STUDIO_ROOT / "build").resolve()}
    if resolved.parent not in allowed:
        raise RuntimeError(f"refusing to remove unexpected path: {resolved}")
    if resolved.exists():
        shutil.rmtree(resolved)


def build() -> Path:
    temporary_dist = DIST_ROOT / "pyinstaller"
    final = DIST_ROOT / OUTPUT_NAME
    work = STUDIO_ROOT / "build" / "pyinstaller"
    _safe_remove(temporary_dist)
    _safe_remove(final)
    _safe_remove(work)
    DIST_ROOT.mkdir(parents=True, exist_ok=True)
    command = [
        sys.executable,
        "-m",
        "PyInstaller",
        "--noconfirm",
        "--clean",
        "--distpath",
        str(temporary_dist),
        "--workpath",
        str(work),
        str(STUDIO_ROOT / "ARKCrewStudio.spec"),
    ]
    result = subprocess.run(command, cwd=ROOT, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"PyInstaller failed with exit code {result.returncode}")
    built = temporary_dist / "ARKCrewStudio"
    if not (built / "ARKCrewStudio.exe").is_file():
        raise RuntimeError("PyInstaller completed without ARKCrewStudio.exe")
    built.replace(final)
    shutil.copy2(ROOT / "LICENSE.txt", final / "LICENSE.txt")
    shutil.copy2(STUDIO_ROOT / "THIRD_PARTY_NOTICES.txt", final / "THIRD_PARTY_NOTICES.txt")
    shutil.copytree(STUDIO_ROOT / "licenses", final / "licenses", dirs_exist_ok=True)
    _safe_remove(temporary_dist)
    print(final)
    return final


if __name__ == "__main__":
    build()

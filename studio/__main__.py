# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Command-line entry point for ARK CREW Studio."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys


SDK_ROOT = Path(__file__).resolve().parents[1]


def _run_brain(workspace: Path | None) -> int:
    _restore_standard_streams()
    workspace = workspace or _remembered_workspace()
    if workspace is not None:
        os.environ["ARK_SDK_ROOT"] = str(workspace.resolve())
    from studio.brain.server import BrainServer

    return BrainServer(workspace.resolve() if workspace else None).run()


def _remembered_workspace() -> Path | None:
    base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
    path = base / "ARKCrewStudio" / "settings.json"
    if not path.is_file():
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    workspace = str(value.get("workspace", "")).strip() if isinstance(value, dict) else ""
    return Path(workspace) if workspace else None


def _restore_standard_streams() -> None:
    for name, descriptor in (("stdout", 1), ("stderr", 2)):
        stream = getattr(sys, name)
        if stream is not None:
            if hasattr(stream, "reconfigure"):
                stream.reconfigure(encoding="utf-8", errors="strict")
            continue
        setattr(sys, name, _open_standard_stream(descriptor))


def _open_standard_stream(descriptor: int):
    try:
        return open(
            os.dup(descriptor),
            "w",
            encoding="utf-8",
            buffering=1,
            closefd=True,
        )
    except OSError:
        return open(os.devnull, "w", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path)
    parser.add_argument("--brain-worker", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--build", action="store_true")
    args = parser.parse_args()
    if args.brain_worker:
        return _run_brain(args.workspace)
    if args.check:
        problems = []
        if importlib.util.find_spec("PySide6") is None:
            problems.append("PySide6 is not installed")
        if args.workspace and not (args.workspace / "app").is_dir():
            problems.append(f"invalid ARK CREW SDK workspace: {args.workspace}")
        if problems:
            for problem in problems:
                print(f"[ERROR] {problem}", file=sys.stderr)
            return 2
        print(f"Studio root : {Path(__file__).resolve().parent}")
        print(f"Workspace   : {args.workspace.resolve() if args.workspace else 'remembered / choose in Studio'}")
        print(f"Python      : {sys.executable}")
        print("ARK CREW Studio prerequisites are available.")
        return 0
    if args.build:
        from studio.build import build

        build()
        return 0
    _entry_diagnostic("main:before-app")
    try:
        from studio.app import run
    except Exception as exc:
        _entry_diagnostic(f"main:app-import-error:{type(exc).__name__}:{exc}")
        raise
    _entry_diagnostic("main:app-imported")

    return run(args.workspace.resolve() if args.workspace else None)


def _entry_diagnostic(message: str) -> None:
    path = os.environ.get("ARK_STUDIO_DIAGNOSTIC_LOG", "").strip()
    if path:
        with Path(path).open("a", encoding="utf-8") as stream:
            stream.write(message + "\n")


if __name__ == "__main__":
    raise SystemExit(main())

# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Unified module command line for ARK CREW Studio tooling."""

from __future__ import annotations

import importlib
import sys


COMMANDS = {
    "app-create": "app_create",
    "build": "build_keil",
    "clean": "build_cleanup",
    "component-create": "component_create",
    "configure": "configure_project",
    "dts": "ark_dts",
    "dts-editor": "dts_editor",
    "encoding": "encoding_convert",
    "flash": "flash_keil",
    "keil-find": "find_keil",
    "memory": "memory_analyzer",
    "package": "package_project",
    "probe": "probe_check",
    "project": "cube_project_tool",
    "serial": "serial_monitor",
    "sync": "sync_keil_project",
}


def main(arguments: list[str] | None = None) -> int:
    values = list(sys.argv[1:] if arguments is None else arguments)
    if not values or values[0] in {"-h", "--help"}:
        print("usage: python -m studio.cli <command> [arguments]")
        print("commands: " + ", ".join(sorted(COMMANDS)))
        return 0
    command, *rest = values
    module_name = COMMANDS.get(command)
    if module_name is None:
        print(f"unknown command: {command}", file=sys.stderr)
        return 2
    module = importlib.import_module(f"studio.tooling.{module_name}")
    old_argv = sys.argv
    sys.argv = [f"studio.cli {command}", *rest]
    try:
        return int(module.main() or 0)
    finally:
        sys.argv = old_argv


if __name__ == "__main__":
    raise SystemExit(main())

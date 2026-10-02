# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Local UI-only state kept outside SDK workspaces."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any


class UiState:
    def __init__(self) -> None:
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
        self.path = base / "ARKCrewStudio" / "ui-state.json"
        self.values: dict[str, Any] = {
            "recentWorkspaces": [],
            "geometry": "",
            "windowState": "",
            "mainSplitter": [580, 260],
            "notifyOnFinish": True,
        }
        self.load()

    def load(self) -> None:
        if not self.path.is_file():
            return
        try:
            value = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return
        if isinstance(value, dict):
            self.values.update(value)

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_suffix(".tmp")
        temporary.write_text(json.dumps(self.values, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        temporary.replace(self.path)

    def remember_workspace(self, path: str) -> None:
        recent = [path, *[item for item in self.values.get("recentWorkspaces", []) if item.lower() != path.lower()]]
        self.values["recentWorkspaces"] = recent[:8]
        self.save()

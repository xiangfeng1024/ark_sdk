# /**
#  * SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
#  * SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0
#  *
#  * ARK CREW LIMITED NON-COMMERCIAL LICENSE NOTICE
#  *
#  * This source code, together with its associated documentation, examples,
#  * configuration files, and related materials, is collectively referred to
#  * as the "Software".
#  *
#  * Subject to the complete terms set forth in the LICENSE file, Ark Crew
#  * grants you a limited, non-exclusive, non-transferable, and non-sublicensable
#  * right to access, reproduce, and modify the Software solely for personal
#  * study, classroom education, academic research, and non-commercial evaluation.
#  *
#  * Commercial use of the Software, in whole or in part, is strictly prohibited
#  * without prior written authorization from Ark Crew. Prohibited activities
#  * include, without limitation, sale, sublicensing, paid distribution, use in
#  * paid consulting or training, incorporation into any commercial product or
#  * service, and internal development intended for commercial deployment.
#  *
#  * Except for the limited rights expressly granted under the applicable
#  * License, no license or other right, whether express, implied, by estoppel,
#  * or otherwise, is granted under any copyright, patent, trademark, trade
#  * secret, mask work, or other intellectual property right belonging to
#  * Ark Crew or any third party.
#  *
#  * Delivery or disclosure of the Software does not convey permission to use
#  * the Ark Crew name, trademarks, logos, visual identity, or other branding,
#  * except where strictly necessary to preserve the original attribution.
#  *
#  * THE SOFTWARE IS PROVIDED "AS IS" AND "WITH ALL FAULTS", WITHOUT ANY
#  * REPRESENTATION OR WARRANTY OF ANY KIND, WHETHER EXPRESS, IMPLIED,
#  * STATUTORY, OR OTHERWISE, INCLUDING WARRANTIES OF MERCHANTABILITY,
#  * FITNESS FOR A PARTICULAR PURPOSE, TITLE, ACCURACY, RELIABILITY, AND
#  * NON-INFRINGEMENT.
#  *
#  * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, ARK CREW SHALL NOT
#  * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
#  * PUNITIVE, OR CONSEQUENTIAL LOSS OR DAMAGE ARISING FROM OR RELATED TO
#  * THE SOFTWARE, ITS USE, OR ITS INABILITY TO BE USED.
#  *
#  * This notice shall be retained in all authorized copies or substantial
#  * portions of the Software. Removal, concealment, or unauthorized alteration
#  * of this notice is prohibited.
#  *
#  * See the LICENSE file in the root directory of this repository for the
#  * complete and controlling license terms.
#  */

"""Active ARK CREW SDK workspace discovery with an explicit unopened state."""

from __future__ import annotations

from pathlib import Path
from typing import Any
import xml.etree.ElementTree as ET

from studio.tooling.project_config import detect_synced_app, load_app_config

from .errors import BrainError
from .settings import SettingsStore


class WorkspaceService:
    def __init__(self, settings: SettingsStore, explicit_root: Path | None = None) -> None:
        self.settings = settings
        self.root: Path | None = None
        self.active_app = ""
        candidate: Path | None = explicit_root
        if candidate is None:
            saved = str(settings.get().get("workspace", "")).strip()
            candidate = Path(saved) if saved else None
        if candidate is not None:
            try:
                self.root = self._validate_root(candidate)
                self.active_app = self._initial_app()
                self.settings.update({"workspace": str(self.root), "active_app": self.active_app})
            except BrainError:
                # A stale remembered path returns the UI to /welcome.
                self.root = None
                self.active_app = ""
                self.settings.update({"workspace": "", "active_app": ""})

    @property
    def opened(self) -> bool:
        return self.root is not None

    def require_root(self) -> Path:
        if self.root is None:
            raise BrainError("WORKSPACE_REQUIRED", "请先选择ARK CREW SDK工作区")
        return self.root

    @staticmethod
    def _validate_root(value: Path) -> Path:
        root = value.expanduser().resolve()
        required = (root / "app", root / "component", root / "hal")
        if not all(path.is_dir() for path in required):
            raise BrainError("INVALID_WORKSPACE", f"not an ARK CREW SDK root: {root}")
        return root

    def _catalog_apps(self) -> list[str]:
        root = self.require_root()
        apps: list[str] = []
        for directory in sorted((root / "app").iterdir()):
            if not directory.is_dir():
                continue
            dts_files = list(directory.glob("*.dts"))
            expected = directory / f"{directory.name}.dts"
            if len(dts_files) == 1 and dts_files[0] == expected:
                apps.append(directory.name)
        return apps

    def _initial_app(self) -> str:
        apps = self._catalog_apps()
        saved = str(self.settings.get().get("active_app", ""))
        if saved in apps:
            return saved
        root = self.require_root()
        try:
            fallback = load_app_config(
                root / "app" / "c8t6_microcar_soil" /
                "c8t6_microcar_soil.dts")
            detected = detect_synced_app(fallback.keil_project)
            if detected in apps:
                return str(detected)
        except (OSError, ValueError, ET.ParseError):
            pass
        if not apps:
            raise BrainError("NO_APPS", f"no app/<name>/<name>.dts found under {root}")
        return apps[0]

    def open(self, path: str | None = None, app: str | None = None) -> dict[str, Any]:
        if path:
            self.root = self._validate_root(Path(path))
            self.active_app = self._initial_app()
        elif self.root is None:
            raise BrainError("WORKSPACE_REQUIRED", "workspace.open requires a path on first use")
        apps = self._catalog_apps()
        if app is not None:
            if app not in apps:
                raise BrainError("UNKNOWN_APP", f"unknown app '{app}'")
            self.active_app = app
        elif self.active_app not in apps:
            self.active_app = apps[0]
        root = self.require_root()
        self.settings.update({"workspace": str(root), "active_app": self.active_app})
        return self.snapshot()

    def config_path(self, app: str | None = None) -> Path:
        root = self.require_root()
        name = app or self.active_app
        path = root / "app" / name / f"{name}.dts"
        if not path.is_file():
            raise BrainError("APP_CONFIG_MISSING", f"missing app config: {path}")
        return path

    def snapshot(self) -> dict[str, Any]:
        if self.root is None:
            return {
                "opened": False, "path": "", "name": "", "apps": [], "activeApp": "",
                "keilSyncedApp": None, "configPath": "", "iocPath": "",
                "cubeProjectPath": "", "projectPath": "", "target": "",
                "device": "", "platform": "", "hal": [], "components": [], "settings": self.settings.get(),
            }
        apps = self._catalog_apps()
        config = load_app_config(self.config_path())
        detected = detect_synced_app(config.keil_project) if config.keil_project.is_file() else None
        return {
            "opened": True,
            "path": str(self.root), "name": self.root.name, "apps": apps, "activeApp": self.active_app,
            "keilSyncedApp": detected, "configPath": str(config.path),
            "iocPath": str(config.ioc_path), "cubeProjectPath": str(config.cube_project_root),
            "projectPath": str(config.keil_project),
            "target": config.target, "device": config.device, "platform": config.platform,
            "hal": list(config.hal), "components": list(config.components), "settings": self.settings.get(),
        }

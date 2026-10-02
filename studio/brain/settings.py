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

"""Per-user settings, parameter memory and bounded structured task history."""

from __future__ import annotations

import json
import os
from pathlib import Path
import threading
from typing import Any


def studio_data_dir() -> Path:
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
    else:
        base = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
    return base / "ARKCrewStudio"


class SettingsStore:
    ALLOWED_KEYS = {"workspace", "active_app", "uv4", "default_probe", "theme", "log_retention", "log_timestamps"}

    def __init__(self, root: Path | None = None) -> None:
        self.root = (root or studio_data_dir()).resolve()
        self.path = self.root / "settings.json"
        self.params_path = self.root / "tool-params.json"
        self._lock = threading.RLock()
        self._values: dict[str, Any] = {
            "workspace": "", "active_app": "", "uv4": "", "default_probe": "dap",
            "theme": "dark", "log_retention": 100, "log_timestamps": True,
        }
        self._tool_params: dict[str, dict[str, Any]] = {}
        self._load()

    def _load_json(self, path: Path) -> Any:
        if not path.is_file():
            return None
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return None

    def _load(self) -> None:
        document = self._load_json(self.path)
        if isinstance(document, dict):
            self._values.update({key: value for key, value in document.items() if key in self.ALLOWED_KEYS})
        params = self._load_json(self.params_path)
        if isinstance(params, dict):
            self._tool_params = {str(key): value for key, value in params.items() if isinstance(value, dict)}

    @staticmethod
    def _write_json(path: Path, value: Any) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + ".tmp")
        temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        temporary.replace(path)

    def get(self) -> dict[str, Any]:
        with self._lock:
            return dict(self._values)

    def update(self, values: dict[str, Any]) -> dict[str, Any]:
        unknown = set(values) - self.ALLOWED_KEYS
        if unknown:
            raise ValueError(f"unsupported settings: {', '.join(sorted(unknown))}")
        if "default_probe" in values and values["default_probe"] not in ("dap", "stlink"):
            raise ValueError("default_probe must be 'dap' or 'stlink'")
        for key in ("workspace", "active_app", "uv4", "theme"):
            if key in values and not isinstance(values[key], str):
                raise ValueError(f"{key} must be a string")
        if "log_retention" in values:
            retention = values["log_retention"]
            if not isinstance(retention, int) or not 10 <= retention <= 1000:
                raise ValueError("log_retention must be an integer from 10 to 1000")
        if "log_timestamps" in values and not isinstance(values["log_timestamps"], bool):
            raise ValueError("log_timestamps must be a boolean")
        with self._lock:
            self._values.update(values)
            self._write_json(self.path, self._values)
            return dict(self._values)

    @staticmethod
    def _params_key(workspace: str, app: str, tool_id: str) -> str:
        return f"{str(Path(workspace).resolve()).lower()}|{app}|{tool_id}"

    def get_tool_params(self, workspace: str, app: str, tool_id: str) -> dict[str, Any]:
        with self._lock:
            return dict(self._tool_params.get(self._params_key(workspace, app, tool_id), {}))

    def set_tool_params(self, workspace: str, app: str, tool_id: str, values: dict[str, Any]) -> dict[str, Any]:
        with self._lock:
            key = self._params_key(workspace, app, tool_id)
            self._tool_params[key] = dict(values)
            self._write_json(self.params_path, self._tool_params)
            return dict(values)


class HistoryStore:
    def __init__(self, settings: SettingsStore) -> None:
        self.settings = settings
        self.path = settings.root / "history.jsonl"
        self.logs = settings.root / "logs"
        self._lock = threading.RLock()

    def load(self) -> list[dict[str, Any]]:
        if not self.path.is_file():
            return []
        records: list[dict[str, Any]] = []
        for line in self.path.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                value = json.loads(line)
            except ValueError:
                continue
            if isinstance(value, dict):
                records.append(value)
        return records[-int(self.settings.get().get("log_retention", 100)):]

    def _read_log(self, path_value: str) -> list[dict[str, Any]]:
        path = Path(path_value)
        if not path.is_file():
            return []
        if path.suffix.lower() == ".jsonl":
            entries: list[dict[str, Any]] = []
            for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
                try:
                    item = json.loads(line)
                except ValueError:
                    item = None
                if isinstance(item, dict):
                    entries.append(item)
            return entries
        # Backward compatibility for Studio 0.1 plain-text logs.
        return [
            {"sequence": index, "timestamp": "", "level": "info", "source": "legacy", "text": line}
            for index, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(True), 1)
        ]

    def read_logs(self, record: dict[str, Any]) -> list[dict[str, Any]]:
        return self._read_log(str(record.get("logPath", "")))

    def append(self, record: dict[str, Any], complete_log: list[dict[str, Any]]) -> None:
        with self._lock:
            self.settings.root.mkdir(parents=True, exist_ok=True)
            self.logs.mkdir(parents=True, exist_ok=True)
            log_path = self.logs / f"{record['id']}.jsonl"
            log_path.write_text(
                "".join(json.dumps(entry, ensure_ascii=False) + "\n" for entry in complete_log), encoding="utf-8"
            )
            item = dict(record)
            item.pop("logs", None)
            item["logPath"] = str(log_path)
            records = self.load()
            records.append(item)
            retention = int(self.settings.get().get("log_retention", 100))
            records = records[-retention:]
            self._write_records(records)
            self._prune(records)

    def _write_records(self, records: list[dict[str, Any]]) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_suffix(".tmp")
        temporary.write_text("".join(json.dumps(value, ensure_ascii=False) + "\n" for value in records), encoding="utf-8")
        temporary.replace(self.path)

    def _prune(self, records: list[dict[str, Any]]) -> None:
        retained = {Path(str(value.get("logPath", ""))).resolve() for value in records if value.get("logPath")}
        for path in self.logs.glob("*.*"):
            if path.resolve() not in retained and path.suffix.lower() in (".log", ".jsonl"):
                try:
                    path.unlink()
                except OSError:
                    pass

    def clear_completed(self) -> dict[str, int]:
        with self._lock:
            records = self.load()
            kept = [item for item in records if item.get("state") in ("queued", "running")]
            removed = len(records) - len(kept)
            self._write_records(kept)
            self._prune(kept)
            return {"removed": removed, "remaining": len(kept)}

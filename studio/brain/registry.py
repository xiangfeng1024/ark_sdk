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

"""Manifest-driven tool discovery and validation."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
from typing import Any

from .errors import BrainError
from .models import ToolInput, ToolSpec


TOOL_ID = re.compile(r"^[a-z][a-z0-9_.-]*$")
INPUT_TYPES = {"text", "integer", "boolean", "enum", "file", "directory", "app", "probe", "serial_port"}
PROGRESS_MODES = {"event", "query", "completion"}
PARAMETER_MODES = {"none", "fixed", "dynamic"}
UI_MODES = {"task", "serial_terminal"}


class ToolRegistry:
    def __init__(self, tools_root: Path) -> None:
        self.tools_root = tools_root.resolve()
        self._tools: dict[str, ToolSpec] = {}
        self.reload()

    def reload(self) -> None:
        tools: dict[str, ToolSpec] = {}
        for path in sorted(self.tools_root.rglob("*.json")):
            document = json.loads(path.read_text(encoding="utf-8"))
            tool = self._parse(path, document)
            if tool.test_only and os.environ.get("ARK_STUDIO_TEST_MODE") != "1":
                continue
            if tool.id in tools:
                raise BrainError("DUPLICATE_TOOL", f"duplicate tool id '{tool.id}'")
            tools[tool.id] = tool
        self._tools = tools

    def _parse(self, path: Path, value: Any) -> ToolSpec:
        if not isinstance(value, dict):
            raise BrainError("INVALID_MANIFEST", f"tool manifest must be an object: {path}")
        tool_id = value.get("id")
        if not isinstance(tool_id, str) or not TOOL_ID.fullmatch(tool_id):
            raise BrainError("INVALID_MANIFEST", f"invalid tool id in {path}")
        if value.get("schema_version", 1) != 1:
            raise BrainError("INVALID_MANIFEST", f"unsupported schema_version for {tool_id}")
        availability = value.get("availability", "active")
        if availability not in ("active", "planned"):
            raise BrainError("INVALID_MANIFEST", f"invalid availability for {tool_id}")
        parameter_mode = value.get("parameter_mode")
        if parameter_mode not in PARAMETER_MODES:
            raise BrainError(
                "INVALID_MANIFEST",
                f"'parameter_mode' must be none/fixed/dynamic for {tool_id}",
            )
        entrypoint = value.get("entrypoint", {})
        if not isinstance(entrypoint, dict):
            raise BrainError("INVALID_MANIFEST", f"entrypoint must be an object for {tool_id}")
        kind = entrypoint.get("kind", "")
        if availability == "active" and kind != "builtin":
            raise BrainError("INVALID_MANIFEST", f"unsupported entrypoint kind for {tool_id}")
        if any(field in entrypoint for field in ("script", "file", "module")):
            raise BrainError("INVALID_MANIFEST", f"path-based entrypoint is forbidden for {tool_id}")
        check = value.get("check")
        if not isinstance(check, dict) or not isinstance(check.get("handler"), str) or not check.get("handler"):
            raise BrainError("INVALID_MANIFEST", f"'check.handler' is required for {tool_id}")
        timeout_ms = check.get("timeout_ms", 5000)
        if not isinstance(timeout_ms, int) or not 100 <= timeout_ms <= 120000:
            raise BrainError("INVALID_MANIFEST", f"invalid check timeout for {tool_id}")
        progress = value.get("progress")
        if not isinstance(progress, dict) or progress.get("mode") not in PROGRESS_MODES:
            raise BrainError("INVALID_MANIFEST", f"'progress.mode' is required for {tool_id}")
        if progress.get("mode") == "query":
            if not isinstance(progress.get("handler"), str) or not progress.get("handler"):
                raise BrainError("INVALID_MANIFEST", f"query progress requires a handler for {tool_id}")
            interval = progress.get("interval_ms", 500)
            if not isinstance(interval, int) or not 100 <= interval <= 10000:
                raise BrainError("INVALID_MANIFEST", f"invalid progress interval for {tool_id}")
        danger = value.get("danger", "safe")
        if danger not in ("safe", "warning", "critical"):
            raise BrainError("INVALID_MANIFEST", f"invalid danger level for {tool_id}")
        ui_mode = value.get("ui_mode", "task")
        if ui_mode not in UI_MODES:
            raise BrainError("INVALID_MANIFEST", f"invalid ui_mode for {tool_id}")
        confirmation = value.get("confirmation")
        if confirmation is not None:
            if not isinstance(confirmation, dict) or not isinstance(confirmation.get("summary"), str):
                raise BrainError("INVALID_MANIFEST", f"invalid confirmation for {tool_id}")
            for field in ("steps", "effects"):
                items = confirmation.get(field, [])
                if not isinstance(items, list) or any(not isinstance(item, str) or not item for item in items):
                    raise BrainError("INVALID_MANIFEST", f"confirmation.{field} must be strings for {tool_id}")
        for field in ("validators", "resources"):
            items = value.get(field, [])
            if not isinstance(items, list) or any(not isinstance(item, str) or not item for item in items):
                raise BrainError("INVALID_MANIFEST", f"'{field}' must be an array of strings for {tool_id}")
        inputs: list[ToolInput] = []
        for item in value.get("inputs", []):
            if not isinstance(item, dict) or item.get("type") not in INPUT_TYPES:
                raise BrainError("INVALID_MANIFEST", f"invalid input in {tool_id}")
            name = item.get("name")
            if not isinstance(name, str) or not TOOL_ID.fullmatch(name):
                raise BrainError("INVALID_MANIFEST", f"invalid input name in {tool_id}")
            options = item.get("options", [])
            if not isinstance(options, list):
                raise BrainError("INVALID_MANIFEST", f"input options must be an array in {tool_id}")
            inputs.append(ToolInput(
                name=name,
                label=str(item.get("label", name)),
                type=str(item["type"]),
                required=bool(item.get("required", False)),
                default=item.get("default"),
                options=tuple(options),
                placeholder=str(item.get("placeholder", "")),
                description=str(item.get("description", "")),
                remember=bool(item.get("remember", False)),
            ))
        if parameter_mode == "none" and inputs:
            raise BrainError(
                "INVALID_MANIFEST",
                f"parameter-free tool '{tool_id}' cannot declare inputs",
            )
        if parameter_mode in {"fixed", "dynamic"} and not inputs:
            raise BrainError(
                "INVALID_MANIFEST",
                f"parameterized tool '{tool_id}' must declare inputs",
            )
        return ToolSpec(
            manifest_path=path,
            schema_version=int(value.get("schema_version", 1)),
            id=tool_id,
            title=str(value.get("title", tool_id)),
            description=str(value.get("description", "")),
            category=str(value.get("category", "other")),
            icon=str(value.get("icon", "Wrench")),
            order=int(value.get("order", 100)),
            availability=availability,
            parameter_mode=parameter_mode,
            entrypoint=entrypoint,
            check=dict(check),
            progress=dict(progress),
            inputs=tuple(inputs),
            validators=tuple(str(item) for item in value.get("validators", [])),
            resources=tuple(str(item) for item in value.get("resources", [])),
            confirmation=dict(confirmation) if confirmation is not None else None,
            danger=str(danger),
            cancellable=bool(value.get("cancellable", True)),
            quick=bool(value.get("quick", False)),
            test_only=bool(value.get("test_only", False)),
            ui_mode=str(ui_mode),
        )

    def list(self) -> list[dict[str, Any]]:
        return [tool.to_dict() for tool in sorted(self._tools.values(), key=lambda item: (item.order, item.title))]

    def get(self, tool_id: str) -> ToolSpec:
        try:
            return self._tools[tool_id]
        except KeyError as exc:
            raise BrainError("UNKNOWN_TOOL", f"unknown tool '{tool_id}'") from exc

    def validate_params(self, tool: ToolSpec, values: dict[str, Any]) -> dict[str, Any]:
        if not isinstance(values, dict):
            raise BrainError("INVALID_PARAMS", "tool params must be an object")
        allowed = {item.name for item in tool.inputs}
        unknown = set(values) - allowed
        if unknown:
            raise BrainError("INVALID_PARAMS", f"unknown params: {', '.join(sorted(unknown))}")
        result: dict[str, Any] = {}
        for item in tool.inputs:
            value = values.get(item.name, item.default)
            if item.required and (value is None or value == ""):
                raise BrainError("INVALID_PARAMS", f"'{item.name}' is required")
            if value is None:
                continue
            if item.type == "integer":
                if isinstance(value, bool):
                    raise BrainError("INVALID_PARAMS", f"'{item.name}' must be an integer")
                try:
                    value = int(value)
                except (TypeError, ValueError) as exc:
                    raise BrainError("INVALID_PARAMS", f"'{item.name}' must be an integer") from exc
            elif item.type == "boolean":
                if not isinstance(value, bool):
                    raise BrainError("INVALID_PARAMS", f"'{item.name}' must be a boolean")
            elif not isinstance(value, str):
                raise BrainError("INVALID_PARAMS", f"'{item.name}' must be a string")
            if item.options and value not in item.options:
                raise BrainError("INVALID_PARAMS", f"'{item.name}' has an unsupported value")
            result[item.name] = value
        return result

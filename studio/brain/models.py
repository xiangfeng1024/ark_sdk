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

"""Data models shared by the registry, SDK checker and task protocol."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


LOG_LEVELS = {"command", "debug", "info", "success", "warning", "error"}


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


@dataclass(frozen=True)
class ToolInput:
    name: str
    label: str
    type: str
    required: bool = False
    default: Any = None
    options: tuple[Any, ...] = ()
    placeholder: str = ""
    description: str = ""
    remember: bool = False

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result["options"] = list(self.options)
        return result


@dataclass(frozen=True)
class ToolSpec:
    manifest_path: Path
    schema_version: int
    id: str
    title: str
    description: str
    category: str
    icon: str
    order: int
    availability: str
    parameter_mode: str
    entrypoint: dict[str, Any]
    check: dict[str, Any]
    progress: dict[str, Any]
    inputs: tuple[ToolInput, ...] = ()
    validators: tuple[str, ...] = ()
    resources: tuple[str, ...] = ()
    confirmation: dict[str, Any] | None = None
    danger: str = "safe"
    cancellable: bool = True
    quick: bool = False
    test_only: bool = False
    ui_mode: str = "task"

    def to_dict(self) -> dict[str, Any]:
        return {
            "schemaVersion": self.schema_version,
            "id": self.id,
            "title": self.title,
            "description": self.description,
            "category": self.category,
            "icon": self.icon,
            "order": self.order,
            "availability": self.availability,
            "parameterMode": self.parameter_mode,
            "entrypoint": self.entrypoint,
            "check": self.check,
            "progress": self.progress,
            "inputs": [item.to_dict() for item in self.inputs],
            "validators": list(self.validators),
            "resources": list(self.resources),
            "confirmation": self.confirmation,
            "danger": self.danger,
            "cancellable": self.cancellable,
            "quick": self.quick,
            "uiMode": self.ui_mode,
        }


@dataclass(frozen=True)
class LogRecord:
    sequence: int
    timestamp: str
    level: str
    source: str
    text: str

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass
class TaskRecord:
    id: str
    tool_id: str
    title: str
    app: str
    params: dict[str, Any]
    state: str = "queued"
    percent: int = 0
    stage: str = "queued"
    message: str = "等待执行"
    created_at: str = field(default_factory=utc_now)
    started_at: str | None = None
    finished_at: str | None = None
    exit_code: int | None = None
    error: str | None = None
    validations: list[dict[str, Any]] = field(default_factory=list)
    logs: list[LogRecord] = field(default_factory=list)
    artifacts: list[dict[str, Any]] = field(default_factory=list)

    def to_dict(self, include_logs: bool = True) -> dict[str, Any]:
        result = {
            "id": self.id,
            "toolId": self.tool_id,
            "title": self.title,
            "app": self.app,
            "params": self.params,
            "state": self.state,
            "percent": self.percent,
            "stage": self.stage,
            "message": self.message,
            "createdAt": self.created_at,
            "startedAt": self.started_at,
            "finishedAt": self.finished_at,
            "exitCode": self.exit_code,
            "error": self.error,
            "validations": self.validations,
            "artifacts": self.artifacts,
        }
        if include_logs:
            result["logs"] = [entry.to_dict() for entry in self.logs[-5000:]]
        return result

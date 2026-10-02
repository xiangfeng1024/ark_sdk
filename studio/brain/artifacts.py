# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Discover and safely preview files produced by SDK tasks."""

from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Any

from studio.tooling.project_config import load_app_config

from .errors import BrainError
from .workspace import WorkspaceService


TEXT_SUFFIXES = {".c", ".h", ".dts", ".dtsi", ".json", ".jsonl", ".log", ".map", ".txt"}
BUILD_SUFFIXES = {".axf", ".bin", ".hex", ".elf", ".zip", ".log", ".map", ".lst", ".txt"}


class ArtifactService:
    def __init__(self, workspace: WorkspaceService) -> None:
        self.workspace = workspace

    @staticmethod
    def _record(path: Path, root: Path) -> dict[str, Any]:
        stat = path.stat()
        suffix = path.suffix.lower()
        try:
            relative = str(path.relative_to(root))
        except ValueError:
            relative = path.name
        return {
            "id": hashlib.sha1(str(path).lower().encode("utf-8")).hexdigest()[:16],
            "name": path.name,
            "path": str(path),
            "relativePath": relative,
            "kind": suffix[1:].upper() if suffix else "FILE",
            "size": stat.st_size,
            "modifiedAt": stat.st_mtime,
            "textPreview": suffix in TEXT_SUFFIXES,
        }

    def list(self, app: str | None = None) -> list[dict[str, Any]]:
        root = self.workspace.require_root()
        config = load_app_config(self.workspace.config_path(app))
        app_root = root / "app" / (app or self.workspace.active_app)
        scan_roots = [(app_root, "app"), (config.keil_project.parent, "build")]
        found: dict[Path, dict[str, Any]] = {}
        for scan_root, mode in scan_roots:
            if not scan_root.is_dir():
                continue
            count = 0
            for path in scan_root.rglob("*"):
                count += 1
                if count > 20_000:
                    break
                is_app_artifact = mode == "app" and (
                    path.suffix.lower() in {".dts", ".dtsi"} or path.name in {"ark_dts_generated.c", "ark_dts_generated.h"}
                )
                is_build_artifact = mode == "build" and path.suffix.lower() in BUILD_SUFFIXES
                if path.is_file() and (is_app_artifact or is_build_artifact):
                    resolved = path.resolve()
                    found[resolved] = self._record(resolved, scan_root.resolve())
        return sorted(found.values(), key=lambda item: (-float(item["modifiedAt"]), str(item["name"])))[:500]

    def read(self, artifact_id: str, max_bytes: int = 1_048_576) -> dict[str, Any]:
        if not 1 <= max_bytes <= 2_097_152:
            raise BrainError("INVALID_LIMIT", "预览大小必须在1到2097152字节之间")
        artifact = next((item for item in self.list() if item["id"] == artifact_id), None)
        if artifact is None:
            raise BrainError("UNKNOWN_ARTIFACT", "构建产物不存在或已被移动")
        if not artifact["textPreview"]:
            raise BrainError("BINARY_ARTIFACT", "二进制产物不支持文本预览")
        path = Path(str(artifact["path"]))
        data = path.read_bytes()[:max_bytes]
        text = data.decode("utf-8", errors="replace")
        return {**artifact, "text": text, "truncated": path.stat().st_size > len(data)}

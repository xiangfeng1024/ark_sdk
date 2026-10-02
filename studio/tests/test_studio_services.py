# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Tests for native Studio Brain services."""

from __future__ import annotations

import os
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from studio.brain.artifacts import ArtifactService
from studio.brain.resource_locks import ResourceLockManager
from studio.brain.serial_service import SerialSessionService


class FakeWorkspace:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.active_app = "demo"

    def require_root(self) -> Path:
        return self.root

    def config_path(self, _app: str | None = None) -> Path:
        return self.root / "app" / "demo" / "demo.dts"


def test_resource_locks_are_shared_and_case_insensitive() -> None:
    locks = ResourceLockManager()
    assert locks.acquire("task:1", ("SERIAL:COM3",), wait=False)
    assert not locks.acquire("studio:serial", ("serial:com3",), wait=False)
    locks.release("task:1", ("serial:com3",))
    assert locks.acquire("studio:serial", ("serial:com3",), wait=False)


def test_serial_test_mode_echoes_binary_payload() -> None:
    events: list[tuple[str, dict[str, object]]] = []
    with patch.dict(os.environ, {"ARK_STUDIO_TEST_MODE": "1"}):
        service = SerialSessionService(ResourceLockManager(), lambda name, data: events.append((name, data)))
        assert service.open("LOOPBACK", 115200)["opened"]
        assert service.write("e4 b8 ad 00")["written"] == 4
        assert [name for name, _data in events][-2:] == ["serial.tx", "serial.rx"]
        assert events[-1][1]["dataHex"] == "e4 b8 ad 00"
        assert not service.close()["opened"]


def test_artifacts_include_generated_and_build_outputs_only(tmp_path: Path) -> None:
    app = tmp_path / "app" / "demo"
    build = tmp_path / "cube" / "MDK-ARM"
    app.mkdir(parents=True)
    build.mkdir(parents=True)
    (app / "demo.dts").write_text("/dts-v1/;", encoding="utf-8")
    (app / "ark_dts_generated.c").write_text("generated", encoding="utf-8")
    (app / "app_main.c").write_text("not an artifact", encoding="utf-8")
    (build / "firmware.axf").write_bytes(b"axf")
    (build / "firmware.map").write_text("map", encoding="utf-8")
    (build / "project.uvprojx").write_text("xml", encoding="utf-8")
    service = ArtifactService(FakeWorkspace(tmp_path))
    with patch("studio.brain.artifacts.load_app_config", return_value=SimpleNamespace(keil_project=build / "project.uvprojx")):
        values = service.list()
        assert {item["name"] for item in values} == {"demo.dts", "ark_dts_generated.c", "firmware.axf", "firmware.map"}
        map_item = next(item for item in values if item["name"] == "firmware.map")
        assert service.read(map_item["id"])["text"] == "map"

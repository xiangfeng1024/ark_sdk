# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Regression tests for the frozen Studio entry point and build layout."""

from __future__ import annotations

import os
from unittest.mock import patch

from studio import __main__ as entrypoint
from studio.build import DIST_ROOT, STUDIO_ROOT
from studio.icons import ASSET_ROOT
from studio.tooling.process_runner import hidden_process_kwargs


def test_missing_windows_standard_handle_falls_back_to_devnull() -> None:
    with patch.object(entrypoint.os, "dup", side_effect=OSError(6, "invalid handle")):
        stream = entrypoint._open_standard_stream(1)
    try:
        assert stream.name == os.devnull
    finally:
        stream.close()


def test_distribution_is_built_under_studio_root() -> None:
    assert DIST_ROOT == STUDIO_ROOT / "dist"


def test_distribution_sources_include_visible_license_files() -> None:
    root = STUDIO_ROOT.parent
    assert (root / "LICENSE.txt").is_file()
    assert (STUDIO_ROOT / "THIRD_PARTY_NOTICES.txt").is_file()
    assert (STUDIO_ROOT / "licenses" / "OFL-1.1.txt").is_file()


def test_brand_assets_are_bundled_and_transparent() -> None:
    from PySide6.QtGui import QImage

    image = QImage(str(ASSET_ROOT / "icons" / "ark_crew_studio_master.png"))
    assert image.size().width() == 1024
    assert image.size().height() == 1024
    assert image.hasAlphaChannel()
    assert (ASSET_ROOT / "icons" / "ARKCrewStudio.ico").is_file()
    assert (ASSET_ROOT / "fonts" / "NotoSansSC-VF.ttf").is_file()


def test_windows_background_processes_are_hidden() -> None:
    values = hidden_process_kwargs(new_process_group=True)
    if os.name == "nt":
        import subprocess

        assert values["creationflags"] & subprocess.CREATE_NO_WINDOW
        assert values["creationflags"] & subprocess.DETACHED_PROCESS
        assert values["creationflags"] & subprocess.CREATE_NEW_PROCESS_GROUP

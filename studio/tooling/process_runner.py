# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Shared subprocess settings that keep background tools invisible on Windows."""

from __future__ import annotations

import os
import subprocess
from typing import Any


def hidden_process_kwargs(new_process_group: bool = False) -> dict[str, Any]:
    if os.name != "nt":
        return {}
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    # Some console-subsystem tools (notably xPack OpenOCD) still allocate a
    # conhost under CREATE_NO_WINDOW alone. DETACHED_PROCESS prevents that
    # allocation while stdout/stderr pipes continue to work normally.
    flags = subprocess.CREATE_NO_WINDOW | subprocess.DETACHED_PROCESS
    if new_process_group:
        flags |= subprocess.CREATE_NEW_PROCESS_GROUP
    return {"startupinfo": startup, "creationflags": flags}

# SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0

"""Shared resource ownership for tasks and interactive Studio sessions."""

from __future__ import annotations

import threading


class ResourceLockManager:
    def __init__(self) -> None:
        self._owners: dict[str, str] = {}
        self._condition = threading.Condition()

    def acquire(
        self,
        owner: str,
        resources: tuple[str, ...],
        cancel: threading.Event | None = None,
        wait: bool = True,
    ) -> bool:
        normalized = tuple(value.lower() for value in resources)
        with self._condition:
            while any(value in self._owners and self._owners[value] != owner for value in normalized):
                if not wait or (cancel is not None and cancel.is_set()):
                    return False
                self._condition.wait(timeout=0.2)
            for value in normalized:
                self._owners[value] = owner
            return True

    def release(self, owner: str, resources: tuple[str, ...]) -> None:
        with self._condition:
            for value in resources:
                key = value.lower()
                if self._owners.get(key) == owner:
                    del self._owners[key]
            self._condition.notify_all()

    def notify(self) -> None:
        with self._condition:
            self._condition.notify_all()

    def owner(self, resource: str) -> str | None:
        with self._condition:
            return self._owners.get(resource.lower())

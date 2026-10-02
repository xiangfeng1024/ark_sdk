# -*- mode: python ; coding: utf-8 -*-

from pathlib import Path
from PyInstaller.utils.hooks import collect_submodules

root = Path(SPECPATH).parent
studio = root / "studio"

hiddenimports = collect_submodules("studio.brain") + collect_submodules("studio.tooling") + [
    "serial",
    "serial.tools.list_ports",
]

a = Analysis(
    [str(studio / "__main__.py")],
    pathex=[str(root)],
    binaries=[],
    datas=[
        (str(root / "LICENSE.txt"), "."),
        (str(studio / "THIRD_PARTY_NOTICES.txt"), "."),
        (str(studio / "licenses"), "licenses"),
        (str(studio / "resources"), "studio/resources"),
        (str(studio / "assets"), "studio/assets"),
    ],
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=["tkinter", "customtkinter", "PySide6.QtQml", "PySide6.QtQuick"],
    noarchive=False,
    optimize=0,
)
# Qt 6.11 on Windows links against the operating-system ICU shim. Some Python
# environments expose unrelated ICU 78 DLLs on PATH; PyInstaller may collect
# them by name, which causes QtCore to fail with ERROR_PROC_NOT_FOUND.
a.binaries = [
    entry for entry in a.binaries
    if Path(entry[0]).name.lower() not in {"icuuc.dll", "icudt78.dll"}
]
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="ARKCrewStudio",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=str(studio / "assets" / "icons" / "ARKCrewStudio.ico"),
    version=None,
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name="ARKCrewStudio",
)

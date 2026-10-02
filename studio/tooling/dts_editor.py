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

"""Check or open the active App DTS in the preferred desktop editor."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess

from studio.tooling.project_config import load_app_config
from studio.tooling.ark_dts import generate_for_app
from studio.tooling.process_runner import hidden_process_kwargs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--open", action="store_true")
    args = parser.parse_args()
    try:
        print("[ARK_PROGRESS] 15 检查DTS和中央组件目录", flush=True)
        config = load_app_config(args.config.resolve())
        _header, _source, stale = generate_for_app(config.path, check=True)
        editor = shutil.which("code") or shutil.which("code.cmd")
        if editor is None and os.name != "nt":
            raise ValueError("未找到VS Code或系统文件打开器")
        print(f"DTS: {config.path}")
        print(f"generated: {'stale' if stale else 'current'}")
        print(f"editor: {editor or 'Windows file association'}")
        print("[ARK_PROGRESS] 70 DTS编辑环境检查通过", flush=True)
        if args.open and not args.check:
            if os.environ.get("ARK_STUDIO_TEST_MODE") == "1":
                print("test mode: skip opening the desktop editor")
            elif editor is not None:
                command = [editor, "--reuse-window", str(config.path)]
                if Path(editor).suffix.lower() in {".cmd", ".bat"}:
                    command = ["cmd.exe", "/d", "/c", *command]
                subprocess.Popen(
                    command,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    close_fds=True,
                    **hidden_process_kwargs(new_process_group=True),
                )
            else:
                os.startfile(config.path)  # type: ignore[attr-defined]
            print("[ARK_PROGRESS] 100 已打开DTS配置", flush=True)
        else:
            print("[ARK_PROGRESS] 100 DTS编辑器检查完成", flush=True)
        return 0
    except (OSError, ValueError) as exc:
        print(f"error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

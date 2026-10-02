#!/usr/bin/env python3
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

"""Select a debug probe and download a Keil MDK project to the target."""

import argparse
from pathlib import Path
import re

from studio.tooling.keil_tools import find_uv4, run_uv4


PROBES = {
    "dap": ("4098", r"BIN\CMSIS_AGDI.dll"),
    "stlink": ("4101", r"STLink\ST-LINKIII-KEIL_SWO.dll"),
}


def replace_single(text: str, pattern: str, replacement: str, source: Path) -> str:
    updated, count = re.subn(pattern, lambda _match: replacement, text, count=1)
    if count != 1:
        raise ValueError(f"expected one matching setting in {source}, found {count}")
    return updated


def configure_probe(project: Path, probe: str) -> None:
    driver_selection, monitor = PROBES[probe]
    options = project.with_suffix(".uvoptx")
    if not options.is_file():
        raise FileNotFoundError(f"uVision options file does not exist: {options}")

    project_text = project.read_text(encoding="utf-8")
    project_text = replace_single(
        project_text,
        r"<DriverSelection>\d+</DriverSelection>",
        f"<DriverSelection>{driver_selection}</DriverSelection>",
        project,
    )
    options_text = options.read_text(encoding="utf-8")
    options_text = replace_single(
        options_text,
        r"<pMon>.*?</pMon>",
        f"<pMon>{monitor}</pMon>",
        options,
    )
    project.write_text(project_text, encoding="utf-8", newline="")
    options.write_text(options_text, encoding="utf-8", newline="")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path, help="Path to a .uvprojx file")
    parser.add_argument("--probe", choices=sorted(PROBES), default="stlink")
    parser.add_argument("--target", help="uVision target name")
    parser.add_argument("--uv4", help="Explicit UV4.exe path")
    parser.add_argument("--skip-build", action="store_true", help="Download the existing image")
    parser.add_argument("--configure-only", action="store_true", help="Only update probe settings")
    args = parser.parse_args()

    project = args.project.resolve()
    if not project.is_file():
        parser.error(f"project does not exist: {project}")
    configure_probe(project, args.probe)
    print(f"Configured {args.probe}: {project.name}")
    if args.configure_only:
        return 0

    uv4 = find_uv4(args.uv4)
    if not args.skip_build:
        result = run_uv4(uv4, "-r", project, args.target, project.parent / ".keil" / "build.log")
        if result != 0:
            return result
    return run_uv4(uv4, "-f", project, args.target, project.parent / ".keil" / "flash.log")


if __name__ == "__main__":
    raise SystemExit(main())

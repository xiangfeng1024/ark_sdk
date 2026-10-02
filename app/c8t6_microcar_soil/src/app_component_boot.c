/**
 * SPDX-FileCopyrightText: Copyright (c) 2026 Ark Crew. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-Ark-Crew-NonCommercial-1.0
 *
 * ARK CREW LIMITED NON-COMMERCIAL LICENSE NOTICE
 *
 * This source code, together with its associated documentation, examples,
 * configuration files, and related materials, is collectively referred to
 * as the "Software".
 *
 * Subject to the complete terms set forth in the LICENSE file, Ark Crew
 * grants you a limited, non-exclusive, non-transferable, and non-sublicensable
 * right to access, reproduce, and modify the Software solely for personal
 * study, classroom education, academic research, and non-commercial evaluation.
 *
 * Commercial use of the Software, in whole or in part, is strictly prohibited
 * without prior written authorization from Ark Crew. Prohibited activities
 * include, without limitation, sale, sublicensing, paid distribution, use in
 * paid consulting or training, incorporation into any commercial product or
 * service, and internal development intended for commercial deployment.
 *
 * Except for the limited rights expressly granted under the applicable
 * License, no license or other right, whether express, implied, by estoppel,
 * or otherwise, is granted under any copyright, patent, trademark, trade
 * secret, mask work, or other intellectual property right belonging to
 * Ark Crew or any third party.
 *
 * Delivery or disclosure of the Software does not convey permission to use
 * the Ark Crew name, trademarks, logos, visual identity, or other branding,
 * except where strictly necessary to preserve the original attribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND "WITH ALL FAULTS", WITHOUT ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND, WHETHER EXPRESS, IMPLIED,
 * STATUTORY, OR OTHERWISE, INCLUDING WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE, ACCURACY, RELIABILITY, AND
 * NON-INFRINGEMENT.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, ARK CREW SHALL NOT
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL LOSS OR DAMAGE ARISING FROM OR RELATED TO
 * THE SOFTWARE, ITS USE, OR ITS INABILITY TO BE USED.
 *
 * This notice shall be retained in all authorized copies or substantial
 * portions of the Software. Removal, concealment, or unauthorized alteration
 * of this notice is prohibited.
 *
 * See the LICENSE file in the root directory of this repository for the
 * complete and controlling license terms.
 */

#include "app_component_boot.h"

#include <stdbool.h>
#include <string.h>

#include "oled.h"
#include "ark_component.h"

static bool app_boot_oled_ready;
static size_t app_boot_init_count;
static size_t app_boot_self_test_count;
static size_t app_boot_init_success;

static void app_boot_draw(
    const ark_component_progress_info_t *progress,
    size_t completed_offset,
    size_t success_offset) {
    size_t total = app_boot_init_count + app_boot_self_test_count;
    size_t completed = completed_offset + progress->completed_count;
    size_t success = success_offset + progress->success_count;
    uint8_t percent = (total == 0U) ? 100U :
        (uint8_t)((completed * 100U) / total);

    if (!app_boot_oled_ready && (progress->component != NULL) &&
        (progress->result == ARK_COMPONENT_OK) &&
        (strcmp(progress->component->name, "oled_sh1106") == 0)) {
        app_boot_oled_ready = true;
    }
    if (!app_boot_oled_ready || (progress->state != ARK_COMPONENT_PROGRESS_RESULT)) {
        return;
    }
    oled_canvas_clear();
    oled_draw_printf(40U, 1U, 8U, "%3u%%", percent);
    oled_draw_printf(0U, 4U, 8U, "%s", progress->component->name);
    oled_draw_printf(46U, 6U, 8U, "%u/%u", (unsigned int)success, (unsigned int)total);
    oled_present();
}

static void app_boot_init_progress(const ark_component_progress_info_t *progress) {
    if (progress->state == ARK_COMPONENT_PROGRESS_FINISHED) {
        app_boot_init_success = progress->success_count;
        return;
    }
    app_boot_draw(progress, 0U, 0U);
}

static void app_boot_self_test_progress(const ark_component_progress_info_t *progress) {
    if (progress->state == ARK_COMPONENT_PROGRESS_BEGIN) {
        return;
    }
    app_boot_draw(progress, app_boot_init_count, app_boot_init_success);
}

void app_component_boot_run(void) {
    app_boot_oled_ready = false;
    app_boot_init_count = ark_component_init_count();
    app_boot_self_test_count = ark_component_self_test_count();
    app_boot_init_success = 0U;
    ark_component_init_all(app_boot_init_progress);
    ark_component_self_test_all(
        NULL,
        app_boot_self_test_progress);
}

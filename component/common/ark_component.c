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

#include "ark_component.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#define ARK_COMPONENT_MAX_COUNT 32U

static const ark_component_t *ark_components[ARK_COMPONENT_MAX_COUNT];

static const ark_component_t *ark_component_get_ordered(size_t position) {
    ark_component_init_level_t level;
    size_t index;
    size_t ordered_index = 0U;

    for (level = ARK_COMPONENT_INIT_LEVEL_1;
         level <= ARK_COMPONENT_INIT_LEVEL_3;
         level = (ark_component_init_level_t)(level + 1)) {
        for (index = 0U; index < ark_component_count(); ++index) {
            if (ark_components[index]->init_level == level) {
                if (ordered_index == position) {
                    return ark_components[index];
                }
                ++ordered_index;
            }
        }
    }
    return NULL;
}

static void ark_component_report_progress(
    ark_component_progress_report_t report,
    const ark_component_t *component,
    ark_component_progress_state_t state,
    ark_component_result_t result,
    uint32_t elapsed_ms,
    size_t completed_count,
    size_t success_count,
    size_t total_count) {
    ark_component_progress_info_t progress;

    if (report == NULL) {
        return;
    }
    progress.component = component;
    progress.state = state;
    progress.result = result;
    progress.elapsed_ms = elapsed_ms;
    progress.percent = (total_count == 0U) ? 100U :
        (uint8_t)((completed_count * 100U) / total_count);
    progress.completed_count = completed_count;
    progress.success_count = success_count;
    progress.total_count = total_count;
    report(&progress);
}

bool ark_component_register(const ark_component_t *component) {
    size_t index;

    if ((component == NULL) || (component->name == NULL) ||
        ((component->self_test != NULL) &&
         (component->self_test_expected_ms == 0U)) ||
        (component->init_level < ARK_COMPONENT_INIT_LEVEL_1) ||
        (component->init_level > ARK_COMPONENT_INIT_LEVEL_3)) {
        return false;
    }

    for (index = 0U; index < ARK_COMPONENT_MAX_COUNT; ++index) {
        if (ark_components[index] == NULL) {
            ark_components[index] = component;
            return true;
        }
        if (strcmp(ark_components[index]->name, component->name) == 0) {
            return false;
        }
    }
    return false;
}

size_t ark_component_count(void) {
    size_t count = 0U;

    while ((count < ARK_COMPONENT_MAX_COUNT) && (ark_components[count] != NULL)) {
        ++count;
    }
    return count;
}

size_t ark_component_init_count(void) {
    size_t count = 0U;
    size_t index;

    for (index = 0U; index < ark_component_count(); ++index) {
        if (ark_components[index]->init != NULL) {
            ++count;
        }
    }
    return count;
}

size_t ark_component_self_test_count(void) {
    size_t count = 0U;
    size_t index;

    for (index = 0U; index < ark_component_count(); ++index) {
        if (ark_components[index]->self_test != NULL) {
            ++count;
        }
    }
    return count;
}

const ark_component_t *ark_component_get(size_t index) {
    if (index >= ark_component_count()) {
        return NULL;
    }
    return ark_components[index];
}

ark_component_result_t ark_component_init_all(
    ark_component_progress_report_t progress_report) {
    size_t index;
    size_t total_count = ark_component_init_count();
    size_t completed_count = 0U;
    size_t success_count = 0U;
    ark_component_result_t overall = ARK_COMPONENT_OK;

    ark_component_report_progress(
        progress_report, NULL, ARK_COMPONENT_PROGRESS_BEGIN,
        ARK_COMPONENT_OK, 0U, 0U, 0U, total_count);

    for (index = 0U; index < ark_component_count(); ++index) {
        const ark_component_t *component = ark_component_get_ordered(index);
        ark_component_result_t result;

        if ((component == NULL) || (component->init == NULL)) {
            continue;
        }
        result = component->init();
        ++completed_count;
        if (result == ARK_COMPONENT_OK) {
            ++success_count;
        } else if (overall == ARK_COMPONENT_OK) {
            overall = result;
        }
        ark_component_report_progress(
            progress_report, component, ARK_COMPONENT_PROGRESS_RESULT,
            result, 0U, completed_count, success_count, total_count);
    }
    ark_component_report_progress(
        progress_report, NULL, ARK_COMPONENT_PROGRESS_FINISHED,
        overall, 0U, completed_count, success_count, total_count);
    return overall;
}

ark_component_result_t ark_component_self_test_all(
    ark_component_self_test_report_t report,
    ark_component_progress_report_t progress_report) {
    size_t index;
    size_t total_count = ark_component_self_test_count();
    size_t completed_count = 0U;
    size_t success_count = 0U;
    ark_component_result_t overall = ARK_COMPONENT_OK;

    ark_component_report_progress(
        progress_report, NULL, ARK_COMPONENT_PROGRESS_BEGIN,
        ARK_COMPONENT_OK, 0U, 0U, 0U, total_count);

    for (index = 0U; index < ark_component_count(); ++index) {
        const ark_component_t *component = ark_component_get_ordered(index);
        TickType_t started;
        uint32_t elapsed_ms;
        ark_component_result_t result;

        if ((component == NULL) || (component->self_test == NULL)) {
            continue;
        }
        started = xTaskGetTickCount();
        result = component->self_test();
        elapsed_ms = (uint32_t)(
            (xTaskGetTickCount() - started) * portTICK_PERIOD_MS);
        ++completed_count;
        if (result == ARK_COMPONENT_OK) {
            ++success_count;
        } else if (overall == ARK_COMPONENT_OK) {
            overall = result;
        }
        ark_component_report_progress(
            progress_report, component, ARK_COMPONENT_PROGRESS_RESULT,
            result, elapsed_ms, completed_count, success_count, total_count);
        if (report != NULL) {
            report(component, result, elapsed_ms);
        }
    }
    ark_component_report_progress(
        progress_report, NULL, ARK_COMPONENT_PROGRESS_FINISHED,
        overall, 0U, completed_count, success_count, total_count);
    return overall;
}

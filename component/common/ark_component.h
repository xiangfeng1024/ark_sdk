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

#ifndef ARK_COMPONENT_H
#define ARK_COMPONENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ARK_COMPONENT_OK = 0,
    ARK_COMPONENT_ERROR,
    ARK_COMPONENT_TIMEOUT
} ark_component_result_t;

typedef enum {
    ARK_COMPONENT_INIT_LEVEL_1 = 1,
    ARK_COMPONENT_INIT_LEVEL_2,
    ARK_COMPONENT_INIT_LEVEL_3
} ark_component_init_level_t;

typedef enum {
    ARK_COMPONENT_PROGRESS_BEGIN = 0,
    ARK_COMPONENT_PROGRESS_RUNNING,
    ARK_COMPONENT_PROGRESS_RESULT,
    ARK_COMPONENT_PROGRESS_FINISHED
} ark_component_progress_state_t;

typedef ark_component_result_t (*ark_component_action_t)(void);

typedef struct {
    const char *name;
    ark_component_action_t init;
    ark_component_action_t self_test;
    uint32_t self_test_expected_ms;
    ark_component_init_level_t init_level;
} ark_component_t;

typedef struct {
    const ark_component_t *component;
    ark_component_progress_state_t state;
    ark_component_result_t result;
    uint32_t elapsed_ms;
    uint8_t percent;
    size_t completed_count;
    size_t success_count;
    size_t total_count;
} ark_component_progress_info_t;

typedef void (*ark_component_self_test_report_t)(
    const ark_component_t *component,
    ark_component_result_t result,
    uint32_t elapsed_ms);

typedef void (*ark_component_progress_report_t)(
    const ark_component_progress_info_t *progress);

bool ark_component_register(const ark_component_t *component);
size_t ark_component_count(void);
size_t ark_component_init_count(void);
size_t ark_component_self_test_count(void);
const ark_component_t *ark_component_get(size_t index);
ark_component_result_t ark_component_init_all(
    ark_component_progress_report_t progress_report);
ark_component_result_t ark_component_self_test_all(
    ark_component_self_test_report_t report,
    ark_component_progress_report_t progress_report);

#endif /* ARK_COMPONENT_H */

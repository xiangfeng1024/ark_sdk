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

#include "motor.h"

#include "ark_component.h"
#include "ark_dts.h"
#include "motor_backend.h"

typedef struct {
    const ark_of_node_t *node;
    const motor_backend_t *backend;
    bool is_ready;
} motor_context_t;

static motor_context_t motor_context;

static bool motor_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "motor");

    motor_context.node = node;
    return node != NULL;
}

bool motor_backend_attach(const motor_backend_t *backend) {
    if ((backend == NULL) || (backend->name == NULL) ||
        (backend->is_ready == NULL) || (backend->stop == NULL) ||
        (motor_context.backend != NULL)) {
        return false;
    }
    motor_context.backend = backend;
    return true;
}

static ark_component_result_t motor_init(void) {
    if (!motor_load_config() || (motor_context.backend == NULL) ||
        !motor_context.backend->is_ready()) {
        return ARK_COMPONENT_ERROR;
    }
    motor_context.backend->stop();
    motor_context.is_ready = true;
    return ARK_COMPONENT_OK;
}

bool motor_is_ready(void) {
    return motor_context.is_ready && (motor_context.backend != NULL) &&
           motor_context.backend->is_ready();
}

const char *motor_backend_name(void) {
    return (motor_context.backend == NULL) ? NULL : motor_context.backend->name;
}

bool motor_set_duty_permille(motor_channel_t channel, int16_t duty_permille) {
    return motor_is_ready() &&
           (channel <= MOTOR_CHANNEL_RIGHT) &&
           (duty_permille >= -1000) && (duty_permille <= 1000) &&
           (motor_context.backend->set_duty_permille != NULL) &&
           motor_context.backend->set_duty_permille(
               (uint8_t)channel, duty_permille);
}

bool motor_set_currents(
    int16_t current_1,
    int16_t current_2,
    int16_t current_3,
    int16_t current_4) {
    return motor_is_ready() &&
           (motor_context.backend->set_currents != NULL) &&
           motor_context.backend->set_currents(
               current_1, current_2, current_3, current_4);
}

void motor_stop(void) {
    if (motor_context.backend != NULL) {
        motor_context.backend->stop();
    }
}

static const ark_component_t motor_component = {
    .name = "motor",
    .init = motor_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool motor_register(void) {
    return ark_component_register(&motor_component);
}

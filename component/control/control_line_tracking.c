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

#include "control_line_tracking.h"

#include "FreeRTOS.h"
#include "task.h"
#include "control.h"
#include "control_pid.h"
#include "ark_dts.h"

typedef struct {
    int32_t base_speed_rpm10;
    control_pid_t steering_pid;
    int16_t position;
    bool observation_valid;
    bool is_configured;
} control_line_tracking_context_t;

static control_line_tracking_context_t control_line_tracking_context;

static int32_t control_line_tracking_clamp(int32_t value) {
    return (value < -3000) ? -3000 : (value > 3000) ? 3000 : value;
}

static bool control_line_tracking_load_pid(
    const ark_of_node_t *node,
    control_pid_config_t *config) {
    uint32_t values[8];

    if ((ark_of_property_read_u32_array(
             node, "ark,steering-pid", values, 8U) != 0) ||
        (values[0] > CONTROL_PID_INCREMENTAL)) {
        return false;
    }
    config->mode = (control_pid_mode_t)values[0];
    config->kp_milli = (int32_t)values[1];
    config->ki_milli = (int32_t)values[2];
    config->kd_milli = (int32_t)values[3];
    config->output_min = (int32_t)values[4];
    config->output_max = (int32_t)values[5];
    config->integral_min = (int32_t)values[6];
    config->integral_max = (int32_t)values[7];
    return true;
}

static const ark_of_node_t *control_line_tracking_node(void) {
    return ark_of_find_node_by_path("/software/line_follow");
}

static bool control_line_tracking_load_config(void) {
    const ark_of_node_t *node = control_line_tracking_node();
    control_pid_config_t pid_config;
    uint32_t base_speed;

    if ((node == NULL) ||
        (ark_of_property_read_u32(
             node, "ark,base-speed-rpm10", &base_speed) != 0) ||
        (base_speed > 3000U) ||
        !control_line_tracking_load_pid(node, &pid_config)) {
        return false;
    }
    control_line_tracking_context.base_speed_rpm10 = (int32_t)base_speed;
    control_pid_init(&control_line_tracking_context.steering_pid, &pid_config);
    control_line_tracking_context.observation_valid = false;
    control_line_tracking_context.is_configured = true;
    return true;
}

static bool control_line_tracking_start(void *context) {
    control_line_tracking_context_t *line_context = context;

    if (line_context == NULL) {
        return false;
    }
    if (!control_line_tracking_context.is_configured &&
        !control_line_tracking_load_config()) {
        return false;
    }
    control_pid_reset(&control_line_tracking_context.steering_pid);
    control_line_tracking_context.observation_valid = false;
    return true;
}

static bool control_line_tracking_update(
    void *context,
    control_command_t *command) {
    control_line_tracking_context_t *line_context = context;
    int16_t position;
    bool observation_valid;
    int32_t turn;

    if ((line_context == NULL) || (command == NULL) ||
        !line_context->is_configured) {
        return false;
    }
    taskENTER_CRITICAL();
    position = line_context->position;
    observation_valid = line_context->observation_valid;
    taskEXIT_CRITICAL();
    if (!observation_valid) {
        return false;
    }
    turn = control_pid_update(
        &line_context->steering_pid,
        0,
        position,
        control_config_get()->period_ms);
    command->left_rpm10 = control_line_tracking_clamp(
        line_context->base_speed_rpm10 - turn);
    command->right_rpm10 = control_line_tracking_clamp(
        line_context->base_speed_rpm10 + turn);
    return true;
}

static void control_line_tracking_stop(void *context) {
    control_line_tracking_context_t *line_context = context;

    if (line_context != NULL) {
        control_pid_reset(&line_context->steering_pid);
        line_context->observation_valid = false;
    }
}

static const control_strategy_t control_line_tracking_strategy = {
    .name = "line_tracking",
    .start = control_line_tracking_start,
    .update = control_line_tracking_update,
    .stop = control_line_tracking_stop,
    .context = &control_line_tracking_context,
};

bool control_line_tracking_register(void) {
    if (control_line_tracking_node() == NULL) {
        return true;
    }
    return control_line_tracking_load_config() &&
           control_strategy_register(&control_line_tracking_strategy);
}

bool control_line_tracking_observe(int16_t position, bool is_valid) {
    if (!control_line_tracking_context.is_configured) {
        return false;
    }
    taskENTER_CRITICAL();
    control_line_tracking_context.position = position;
    control_line_tracking_context.observation_valid = is_valid;
    taskEXIT_CRITICAL();
    return true;
}

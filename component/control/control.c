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

#include "control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_dts_generated.h"
#include "control_line_tracking.h"
#include "encoder.h"
#include "motor.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"

static control_pid_t control_left_pid;
static control_pid_t control_right_pid;
static control_speed_data_t control_speed;
static int32_t control_left_command;
static int32_t control_right_command;
static bool control_ready;
static control_config_t control_config;
static const control_strategy_t *control_strategies[4];
static size_t control_strategy_count;
static const control_strategy_t *control_active_strategy;

const control_config_t *control_config_get(void) {
    return &control_config;
}

static bool control_load_pid(
    const ark_of_node_t *node,
    const char *name,
    control_pid_config_t *pid) {
    uint32_t values[8];

    if (ark_of_property_read_u32_array(node, name, values, 8U) != 0 ||
        values[0] > CONTROL_PID_INCREMENTAL) {
        return false;
    }
    pid->mode = (control_pid_mode_t)values[0];
    pid->kp_milli = (int32_t)values[1];
    pid->ki_milli = (int32_t)values[2];
    pid->kd_milli = (int32_t)values[3];
    pid->output_min = (int32_t)values[4];
    pid->output_max = (int32_t)values[5];
    pid->integral_min = (int32_t)values[6];
    pid->integral_max = (int32_t)values[7];
    return true;
}

static bool control_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "control");
    uint32_t value;

    if ((node == NULL) ||
        (ark_of_property_read_u32(node, "ark,period-ms", &control_config.period_ms) != 0) ||
        !control_load_pid(node, "ark,left-pid", &control_config.left_pid) ||
        !control_load_pid(node, "ark,right-pid", &control_config.right_pid)) {
        return false;
    }
    control_config.smoothing.enabled =
        ark_of_property_read_bool(node, "ark,smoothing-enabled");
    if (ark_of_property_read_u32(node, "ark,rise-step-rpm10", &value) != 0) {
        return false;
    }
    control_config.smoothing.rise_step_rpm10 = (int32_t)value;
    if (ark_of_property_read_u32(node, "ark,fall-step-rpm10", &value) != 0) {
        return false;
    }
    control_config.smoothing.fall_step_rpm10 = (int32_t)value;
    if (ark_of_property_read_u32(node, "ark,minimum-step-rpm10", &value) != 0) {
        return false;
    }
    control_config.smoothing.minimum_step_rpm10 = (int32_t)value;
    return true;
}

bool control_strategy_register(const control_strategy_t *strategy) {
    size_t index;

    if ((strategy == NULL) || (strategy->name == NULL) ||
        (strategy->name[0] == '\0') || (strategy->update == NULL) ||
        (control_strategy_count >= 4U)) {
        return false;
    }
    for (index = 0U; index < control_strategy_count; ++index) {
        if (strcmp(control_strategies[index]->name, strategy->name) == 0) {
            return false;
        }
    }
    control_strategies[control_strategy_count++] = strategy;
    return true;
}

bool control_strategy_select(const char *name) {
    const control_strategy_t *next = NULL;
    size_t index;

    if (name == NULL) {
        return false;
    }
    if (strcmp(name, "manual") != 0) {
        for (index = 0U; index < control_strategy_count; ++index) {
            if (strcmp(control_strategies[index]->name, name) == 0) {
                next = control_strategies[index];
                break;
            }
        }
        if (next == NULL) {
            return false;
        }
    }
    if ((control_active_strategy != NULL) &&
        (control_active_strategy->stop != NULL)) {
        control_active_strategy->stop(control_active_strategy->context);
    }
    if ((next != NULL) && (next->start != NULL) &&
        !next->start(next->context)) {
        control_active_strategy = NULL;
        return false;
    }
    control_active_strategy = next;
    return true;
}

const char *control_strategy_current(void) {
    return (control_active_strategy == NULL) ?
           "manual" : control_active_strategy->name;
}

bool control_is_ready(void) {
    return control_ready && motor_is_ready();
}

static bool control_targets_set(int32_t left_rpm10, int32_t right_rpm10) {
    if ((left_rpm10 < -3000) || (left_rpm10 > 3000) ||
        (right_rpm10 < -3000) || (right_rpm10 > 3000)) {
        return false;
    }
    taskENTER_CRITICAL();
    control_speed.left_target_rpm10 = left_rpm10;
    control_speed.right_target_rpm10 = right_rpm10;
    taskEXIT_CRITICAL();
    return true;
}

static int32_t control_smooth_target(
    int32_t current,
    int32_t target,
    bool is_rising) {
    int32_t distance;
    int32_t full_distance;
    int32_t step_limit;
    int32_t minimum_step = ark_control_config.smoothing.minimum_step_rpm10;
    int64_t ratio_squared;
    int32_t step;

    if (!ark_control_config.smoothing.enabled || (current == target)) {
        return target;
    }
    distance = abs(target - current);
    full_distance = abs(target);
    if (full_distance < distance) {
        full_distance = distance;
    }
    step_limit = is_rising ? ark_control_config.smoothing.rise_step_rpm10 :
                          ark_control_config.smoothing.fall_step_rpm10;
    if (step_limit < minimum_step) {
        step_limit = minimum_step;
    }
    ratio_squared = ((int64_t)distance * 1000LL / full_distance);
    ratio_squared = ratio_squared * ratio_squared / 1000LL;
    step = minimum_step + (int32_t)(((int64_t)(step_limit - minimum_step) *
                                     ratio_squared) / 1000LL);
    if (step < minimum_step) {
        step = minimum_step;
    }
    if (abs(target - current) <= step) {
        return target;
    }
    return current + (((target > current) ? 1 : -1) * step);
}

bool control_set_speed(int32_t left_rpm10, int32_t right_rpm10) {
    return control_strategy_select("manual") &&
           control_targets_set(left_rpm10, right_rpm10);
}

bool control_get_speed(control_speed_data_t *data) {
    if (data == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    *data = control_speed;
    taskEXIT_CRITICAL();
    return true;
}

bool control_update(void) {
    encoder_data_t encoder;
    control_command_t strategy_command;
    int32_t left_target;
    int32_t right_target;
    int32_t left_actual;
    int32_t right_actual;
    int32_t left_output;
    int32_t right_output;

    taskENTER_CRITICAL();
    control_speed.update_count++;
    taskEXIT_CRITICAL();
    if (!control_is_ready() || !encoder_get(&encoder)) {
        taskENTER_CRITICAL();
        control_speed.update_failures++;
        taskEXIT_CRITICAL();
        return false;
    }
    if (control_active_strategy != NULL) {
        strategy_command = (control_command_t){0};
        if (!control_active_strategy->update(
                control_active_strategy->context, &strategy_command) ||
            !control_targets_set(
                strategy_command.left_rpm10,
                strategy_command.right_rpm10)) {
            motor_stop();
            taskENTER_CRITICAL();
            control_speed.update_failures++;
            taskEXIT_CRITICAL();
            return false;
        }
    }
    left_actual = encoder_delta_to_rpm10(
        encoder.left_delta, ark_control_config.period_ms);
    right_actual = encoder_delta_to_rpm10(
        encoder.right_delta, ark_control_config.period_ms);
    taskENTER_CRITICAL();
    left_target = control_speed.left_target_rpm10;
    right_target = control_speed.right_target_rpm10;
    taskEXIT_CRITICAL();
    control_left_command = control_smooth_target(
        control_left_command, left_target, abs(left_target) >= abs(control_left_command));
    control_right_command = control_smooth_target(
        control_right_command, right_target, abs(right_target) >= abs(control_right_command));
    left_output = control_pid_update(
        &control_left_pid, control_left_command, left_actual,
        ark_control_config.period_ms);
    right_output = control_pid_update(
        &control_right_pid, control_right_command, right_actual,
        ark_control_config.period_ms);
    if (!motor_set_duty_permille(
            MOTOR_CHANNEL_LEFT, (int16_t)left_output) ||
        !motor_set_duty_permille(
            MOTOR_CHANNEL_RIGHT, (int16_t)right_output)) {
        return false;
    }
    taskENTER_CRITICAL();
    control_speed.left_command_rpm10 = control_left_command;
    control_speed.right_command_rpm10 = control_right_command;
    control_speed.left_actual_rpm10 = left_actual;
    control_speed.right_actual_rpm10 = right_actual;
    control_speed.left_encoder_count = encoder.left_count;
    control_speed.right_encoder_count = encoder.right_count;
    control_speed.distance_count +=
        (abs(encoder.left_delta) + abs(encoder.right_delta)) / 2;
    control_speed.left_output_permille = (int16_t)left_output;
    control_speed.right_output_permille = (int16_t)right_output;
    taskEXIT_CRITICAL();
    return true;
}

void control_stop(void) {
    (void)control_strategy_select("manual");
    (void)control_targets_set(0, 0);
    control_left_command = 0;
    control_right_command = 0;
    control_pid_reset(&control_left_pid);
    control_pid_reset(&control_right_pid);
    motor_stop();
}

#if ARK_DTS_HAS_UART_CLI
static int control_speed_cli(int argc, char *argv[]) {
    control_speed_data_t data;
    char *end;
    long left;
    long right;

    if ((argc == 1) && control_get_speed(&data)) {
        printf(
            "speed strategy=%s update=%lu fail=%lu target=%ld/%ld command=%ld/%ld "
            "actual=%ld/%ld pwm=%d/%d enc=%ld/%ld distance=%ld\r\n",
               control_strategy_current(),
               (unsigned long)data.update_count,
               (unsigned long)data.update_failures,
               (long)data.left_target_rpm10,
               (long)data.right_target_rpm10,
               (long)data.left_command_rpm10,
               (long)data.right_command_rpm10,
               (long)data.left_actual_rpm10,
               (long)data.right_actual_rpm10,
               (int)data.left_output_permille,
               (int)data.right_output_permille,
               (long)data.left_encoder_count,
               (long)data.right_encoder_count,
               (long)data.distance_count);
        return 0;
    }
    if (argc != 3) {
        printf("usage: %s [<left_rpm10> <right_rpm10>]\r\n", argv[0]);
        return -1;
    }
    left = strtol(argv[1], &end, 10);
    if ((*end != '\0') || ((right = strtol(argv[2], &end, 10)), *end != '\0') ||
        !control_set_speed((int32_t)left, (int32_t)right)) {
        return -1;
    }
    printf("speed target=%ld/%ld rpm10 smoothing=%u\r\n",
           left, right, ark_control_config.smoothing.enabled ? 1U : 0U);
    return 0;
}

static ark_cli_command_t control_speed_command = {
    .name = "speed",
    .usage = "speed [<left_rpm10> <right_rpm10>]",
    .description = "show or set closed-loop wheel speed",
    .handler = control_speed_cli,
};
#endif

static ark_component_result_t control_init(void) {
    if (!control_load_config() || !motor_is_ready()) {
        return ARK_COMPONENT_ERROR;
    }
    control_pid_init(&control_left_pid, &ark_control_config.left_pid);
    control_pid_init(&control_right_pid, &ark_control_config.right_pid);
    control_speed = (control_speed_data_t){0};
    control_left_command = 0;
    control_right_command = 0;
    control_strategy_count = 0U;
    control_active_strategy = NULL;
    if (!control_line_tracking_register()) {
        return ARK_COMPONENT_ERROR;
    }
    control_ready = true;
    control_stop();
    return ARK_COMPONENT_OK;
}

static const ark_component_t control_component = {
    .name = "control",
    .init = control_init,
    .self_test = NULL,
    .self_test_expected_ms = 0U,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool control_register(void) {
#if ARK_DTS_HAS_UART_CLI
    (void)ark_cli_register(&control_speed_command);
#endif
    return ark_component_register(&control_component);
}

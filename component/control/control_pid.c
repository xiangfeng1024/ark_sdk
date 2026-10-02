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

#include "control_pid.h"

#include <limits.h>
#include <stddef.h>

static int32_t control_pid_clamp(
    int32_t value,
    int32_t minimum,
    int32_t maximum) {
    return (value < minimum) ? minimum :
           (value > maximum) ? maximum : value;
}

void control_pid_init(control_pid_t *pid, const control_pid_config_t *config) {
    if ((pid == NULL) || (config == NULL)) {
        return;
    }
    pid->config = *config;
    control_pid_reset(pid);
}

void control_pid_reset(control_pid_t *pid) {
    if (pid == NULL) {
        return;
    }
    pid->integral = 0;
    pid->previous_error = 0;
    pid->previous_previous_error = 0;
    pid->output = 0;
}

int32_t control_pid_update(
    control_pid_t *pid,
    int32_t setpoint,
    int32_t measurement,
    uint32_t period_ms) {
    int32_t error;
    int32_t derivative;
    int64_t integral;
    int64_t output;

    if ((pid == NULL) || (period_ms == 0U)) {
        return 0;
    }
    error = setpoint - measurement;
    integral = (int64_t)pid->integral + (int64_t)error * period_ms;
    pid->integral = control_pid_clamp(
        (integral > INT32_MAX) ? INT32_MAX :
        (integral < INT32_MIN) ? INT32_MIN : (int32_t)integral,
        pid->config.integral_min,
        pid->config.integral_max);
    derivative = (error - pid->previous_error) / (int32_t)period_ms;

    if (pid->config.mode == CONTROL_PID_INCREMENTAL) {
        output = pid->output;
        output += ((int64_t)pid->config.kp_milli *
                   (error - pid->previous_error)) / 1000LL;
        output += ((int64_t)pid->config.ki_milli * error * period_ms) /
                  1000000LL;
        output += ((int64_t)pid->config.kd_milli *
                   (error - 2LL * pid->previous_error +
                    pid->previous_previous_error)) /
                  ((int64_t)period_ms * 1000LL);
    } else {
        output = ((int64_t)pid->config.kp_milli * error) / 1000LL;
        output += ((int64_t)pid->config.ki_milli * pid->integral) / 1000000LL;
        output += ((int64_t)pid->config.kd_milli * derivative) / 1000LL;
    }
    pid->previous_previous_error = pid->previous_error;
    pid->previous_error = error;
    pid->output = control_pid_clamp(
        (output > INT32_MAX) ? INT32_MAX :
        (output < INT32_MIN) ? INT32_MIN : (int32_t)output,
        pid->config.output_min,
        pid->config.output_max);
    return pid->output;
}

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

#include "app_soil_probe.h"
#include "app_dts_config.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "adc_sensor.h"
#include "buzzer.h"
#include "servo.h"

typedef struct {
    app_soil_probe_snapshot_t snapshot;
    TickType_t started_tick;
    TickType_t state_tick;
    uint32_t baseline_sum;
    uint8_t baseline_count;
    bool initialized;
} app_soil_probe_context_t;

static app_soil_probe_context_t app_soil_probe_context;

static uint16_t app_soil_probe_threshold(uint16_t baseline) {
    return (baseline > APP_SOIL_INSERT_DROP_RAW) ?
           (uint16_t)(baseline - APP_SOIL_INSERT_DROP_RAW) : 0U;
}

static int16_t app_soil_probe_insert_angle(void) {
    int32_t angle = SERVO_RESET_ANGLE +
        APP_SOIL_PROBE_DIRECTION * APP_SOIL_PROBE_INSERT_DELTA_DEG;

    if (angle < SERVO_MIN_ANGLE) {
        angle = SERVO_MIN_ANGLE;
    } else if (angle > SERVO_MAX_ANGLE) {
        angle = SERVO_MAX_ANGLE;
    }
    return (int16_t)angle;
}

static void app_soil_probe_publish(const app_soil_probe_snapshot_t *snapshot) {
    taskENTER_CRITICAL();
    app_soil_probe_context.snapshot = *snapshot;
    taskEXIT_CRITICAL();
}

void app_soil_probe_init(void) {
    memset(&app_soil_probe_context, 0, sizeof(app_soil_probe_context));
    app_soil_probe_context.snapshot.state = APP_SOIL_PROBE_STATE_IDLE;
    app_soil_probe_context.snapshot.commanded_angle = SERVO_RESET_ANGLE;
    app_soil_probe_context.initialized = true;
    buzzer_set(false);
}

void app_soil_probe_observe_idle(uint16_t raw) {
    app_soil_probe_snapshot_t next;

    taskENTER_CRITICAL();
    if (!app_soil_probe_context.initialized ||
        app_soil_probe_context.snapshot.active ||
        (app_soil_probe_context.snapshot.commanded_angle != SERVO_RESET_ANGLE)) {
        taskEXIT_CRITICAL();
        return;
    }
    next = app_soil_probe_context.snapshot;
    if (!next.baseline_ready) {
        app_soil_probe_context.baseline_sum += raw;
        app_soil_probe_context.baseline_count++;
        if (app_soil_probe_context.baseline_count >=
            APP_SOIL_BASELINE_SAMPLE_COUNT) {
            next.baseline_raw = (uint16_t)(
                app_soil_probe_context.baseline_sum /
                APP_SOIL_BASELINE_SAMPLE_COUNT);
            next.insert_threshold_raw =
                app_soil_probe_threshold(next.baseline_raw);
            next.baseline_ready = true;
        }
    } else {
        next.baseline_raw = (uint16_t)(
            (((uint32_t)next.baseline_raw << APP_SOIL_BASELINE_FILTER_SHIFT) -
             next.baseline_raw + raw +
             (1U << (APP_SOIL_BASELINE_FILTER_SHIFT - 1U))) >>
            APP_SOIL_BASELINE_FILTER_SHIFT);
        next.insert_threshold_raw =
            app_soil_probe_threshold(next.baseline_raw);
    }
    next.inserted = next.baseline_ready &&
                    (raw < next.insert_threshold_raw);
    app_soil_probe_context.snapshot = next;
    taskEXIT_CRITICAL();
}

bool app_soil_probe_sample_is_inserted(uint16_t raw) {
    bool inserted;

    taskENTER_CRITICAL();
    inserted = app_soil_probe_context.initialized &&
               app_soil_probe_context.snapshot.baseline_ready &&
               (raw < app_soil_probe_context.snapshot.insert_threshold_raw);
    taskEXIT_CRITICAL();
    return inserted;
}

bool app_soil_probe_start(void) {
    app_soil_probe_snapshot_t next;
    int16_t insert_angle;

    if (!app_soil_probe_context.initialized ||
        app_soil_probe_context.snapshot.active ||
        !app_soil_probe_context.snapshot.baseline_ready) {
        return false;
    }
    buzzer_set(false);
    insert_angle = app_soil_probe_insert_angle();
    if (!servo_set_angle(insert_angle)) {
        return false;
    }
    memset(&next, 0, sizeof(next));
    next.state = APP_SOIL_PROBE_STATE_INSERTING;
    next.active = true;
    next.commanded_angle = insert_angle;
    next.baseline_ready = app_soil_probe_context.snapshot.baseline_ready;
    next.baseline_raw = app_soil_probe_context.snapshot.baseline_raw;
    next.insert_threshold_raw =
        app_soil_probe_context.snapshot.insert_threshold_raw;
    app_soil_probe_context.started_tick = xTaskGetTickCount();
    app_soil_probe_context.state_tick = app_soil_probe_context.started_tick;
    app_soil_probe_publish(&next);
    return true;
}

void app_soil_probe_stop(void) {
    app_soil_probe_snapshot_t next;

    if (!app_soil_probe_context.initialized) {
        return;
    }
    taskENTER_CRITICAL();
    next = app_soil_probe_context.snapshot;
    taskEXIT_CRITICAL();
    buzzer_set(false);
    if (!next.active &&
        (next.commanded_angle == SERVO_RESET_ANGLE) &&
        (next.state != APP_SOIL_PROBE_STATE_ERROR)) {
        next.state = APP_SOIL_PROBE_STATE_IDLE;
        app_soil_probe_publish(&next);
        return;
    }
    if (!servo_reset()) {
        next.state = APP_SOIL_PROBE_STATE_ERROR;
    } else {
        next.state = APP_SOIL_PROBE_STATE_IDLE;
    }
    next.active = false;
    next.commanded_angle = SERVO_RESET_ANGLE;
    app_soil_probe_publish(&next);
}

void app_soil_probe_step(void) {
    app_soil_probe_snapshot_t next;
    TickType_t now;
    uint32_t state_elapsed_ms;

    if (!app_soil_probe_context.initialized) {
        return;
    }
    taskENTER_CRITICAL();
    next = app_soil_probe_context.snapshot;
    taskEXIT_CRITICAL();
    if (!next.active) {
        return;
    }
    now = xTaskGetTickCount();
    next.elapsed_ms =
        (uint32_t)(now - app_soil_probe_context.started_tick) * portTICK_PERIOD_MS;
    state_elapsed_ms =
        (uint32_t)(now - app_soil_probe_context.state_tick) * portTICK_PERIOD_MS;
    switch (next.state) {
    case APP_SOIL_PROBE_STATE_INSERTING:
        if (state_elapsed_ms >= APP_SOIL_PROBE_MOVE_SETTLE_MS) {
            next.state = APP_SOIL_PROBE_STATE_HOLDING;
            app_soil_probe_context.state_tick = now;
        }
        break;
    case APP_SOIL_PROBE_STATE_HOLDING:
        if (state_elapsed_ms >= APP_SOIL_PROBE_HOLD_MS) {
            next.state = APP_SOIL_PROBE_STATE_SAMPLING;
            app_soil_probe_context.state_tick = now;
        }
        break;
    case APP_SOIL_PROBE_STATE_SAMPLING: {
        adc_sensor_sample_t sample;

        if (adc_sensor_read(ADC_SENSOR_SOIL, &sample)) {
            next.soil_raw = sample.raw;
            next.soil_millivolts = sample.millivolts;
            next.inserted = app_soil_probe_sample_is_inserted(sample.raw);
            next.sample_valid = next.inserted;
        }
        if (next.sample_valid) {
            buzzer_set(true);
            next.state = APP_SOIL_PROBE_STATE_ALARMING;
            app_soil_probe_context.state_tick = now;
        } else if (state_elapsed_ms >= APP_SOIL_PROBE_SAMPLE_TIMEOUT_MS) {
            if (!servo_reset()) {
                next.state = APP_SOIL_PROBE_STATE_ERROR;
                next.active = false;
                break;
            }
            next.state = APP_SOIL_PROBE_STATE_RETURNING;
            next.commanded_angle = SERVO_RESET_ANGLE;
            app_soil_probe_context.state_tick = xTaskGetTickCount();
        }
        break;
    }
    case APP_SOIL_PROBE_STATE_ALARMING:
        if (state_elapsed_ms >= APP_SOIL_PROBE_ALARM_MS) {
            buzzer_set(false);
            if (!servo_reset()) {
                next.state = APP_SOIL_PROBE_STATE_ERROR;
                next.active = false;
                break;
            }
            next.state = APP_SOIL_PROBE_STATE_RETURNING;
            next.commanded_angle = SERVO_RESET_ANGLE;
            app_soil_probe_context.state_tick = xTaskGetTickCount();
        }
        break;
    case APP_SOIL_PROBE_STATE_RETURNING:
        if (state_elapsed_ms >= APP_SOIL_PROBE_MOVE_SETTLE_MS) {
            next.state = APP_SOIL_PROBE_STATE_COMPLETE;
            next.active = false;
        }
        break;
    case APP_SOIL_PROBE_STATE_IDLE:
    case APP_SOIL_PROBE_STATE_COMPLETE:
    case APP_SOIL_PROBE_STATE_ERROR:
    default:
        break;
    }
    app_soil_probe_publish(&next);
}

bool app_soil_probe_get_snapshot(app_soil_probe_snapshot_t *snapshot) {
    if ((snapshot == NULL) || !app_soil_probe_context.initialized) {
        return false;
    }
    taskENTER_CRITICAL();
    *snapshot = app_soil_probe_context.snapshot;
    taskEXIT_CRITICAL();
    return true;
}

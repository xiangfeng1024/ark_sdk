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

#include "app_flower_tracking.h"
#include "app_dts_config.h"

#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "control.h"
#include "maixcam.h"

typedef struct {
    app_flower_tracking_snapshot_t snapshot;
    TickType_t started_tick;
    TickType_t lost_tick;
    TickType_t last_frame_tick;
    uint32_t timeout_ms;
    int8_t search_direction;
    uint8_t detected_frames;
    uint8_t missed_frames;
    bool settling;
    bool initialized;
} app_flower_tracking_context_t;

static app_flower_tracking_context_t app_flower_context;

static int32_t app_flower_clamp(int32_t value, int32_t minimum, int32_t maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static int32_t app_flower_apply_minimum(int32_t value) {
    if ((value > 0) && (value < APP_FLOWER_MIN_SPEED_RPM10)) {
        return APP_FLOWER_MIN_SPEED_RPM10;
    }
    if ((value < 0) && (value > -APP_FLOWER_MIN_SPEED_RPM10)) {
        return -APP_FLOWER_MIN_SPEED_RPM10;
    }
    return value;
}

static void app_flower_set_wheels(
    app_flower_tracking_snapshot_t *snapshot,
    int32_t left,
    int32_t right) {
    left = app_flower_clamp(
        left, -APP_FLOWER_MAX_WHEEL_RPM10, APP_FLOWER_MAX_WHEEL_RPM10);
    right = app_flower_clamp(
        right, -APP_FLOWER_MAX_WHEEL_RPM10, APP_FLOWER_MAX_WHEEL_RPM10);
    (void)control_set_speed(left, right);
    snapshot->left_speed_rpm10 = (int16_t)left;
    snapshot->right_speed_rpm10 = (int16_t)right;
}

static void app_flower_publish(const app_flower_tracking_snapshot_t *snapshot) {
    taskENTER_CRITICAL();
    app_flower_context.snapshot = *snapshot;
    taskEXIT_CRITICAL();
}

void app_flower_tracking_init(void) {
    memset(&app_flower_context, 0, sizeof(app_flower_context));
    app_flower_context.snapshot.state = APP_FLOWER_STATE_IDLE;
    app_flower_context.search_direction = 1;
    app_flower_context.initialized = true;
}

bool app_flower_tracking_start_target(
    int8_t search_direction,
    uint32_t timeout_ms,
    uint16_t target_x,
    uint16_t target_y) {
    app_flower_tracking_snapshot_t next;

    if (!app_flower_context.initialized || app_flower_context.snapshot.active) {
        return false;
    }
    memset(&next, 0, sizeof(next));
    next.state = APP_FLOWER_STATE_SEARCHING;
    next.active = true;
    next.target_x = target_x;
    next.target_y = target_y;
    app_flower_context.started_tick = xTaskGetTickCount();
    app_flower_context.lost_tick = app_flower_context.started_tick;
    app_flower_context.last_frame_tick = 0U;
    app_flower_context.detected_frames = 0U;
    app_flower_context.missed_frames = 0U;
    app_flower_context.settling = false;
    app_flower_context.timeout_ms = timeout_ms;
    app_flower_context.search_direction =
        ((search_direction < 0) ? -1 : 1) * APP_FLOWER_SEARCH_POLARITY;
    control_stop();
    app_flower_publish(&next);
    return true;
}

bool app_flower_tracking_start(int8_t search_direction, uint32_t timeout_ms) {
    return app_flower_tracking_start_target(
        search_direction,
        timeout_ms,
        APP_FLOWER_TARGET_X,
        APP_FLOWER_TARGET_Y);
}

void app_flower_tracking_stop(void) {
    app_flower_tracking_snapshot_t next;

    if (!app_flower_context.initialized) {
        return;
    }
    taskENTER_CRITICAL();
    next = app_flower_context.snapshot;
    taskEXIT_CRITICAL();
    control_stop();
    next.state = APP_FLOWER_STATE_IDLE;
    next.active = false;
    next.left_speed_rpm10 = 0;
    next.right_speed_rpm10 = 0;
    next.stable_frames = 0U;
    app_flower_context.detected_frames = 0U;
    app_flower_context.missed_frames = 0U;
    app_flower_context.settling = false;
    app_flower_publish(&next);
}

void app_flower_tracking_step(void) {
    app_flower_tracking_snapshot_t next;
    maixcam_result_t camera;
    TickType_t now;
    bool has_result;
    bool new_frame;
    bool was_detected;

    if (!app_flower_context.initialized) {
        return;
    }
    taskENTER_CRITICAL();
    next = app_flower_context.snapshot;
    taskEXIT_CRITICAL();
    was_detected = next.detected;
    if (!next.active) {
        return;
    }
    now = xTaskGetTickCount();
    next.elapsed_ms = (uint32_t)(now - app_flower_context.started_tick) * portTICK_PERIOD_MS;
    if ((app_flower_context.timeout_ms > 0U) &&
        (next.elapsed_ms >= app_flower_context.timeout_ms)) {
        control_stop();
        next.state = APP_FLOWER_STATE_TIMEOUT;
        next.active = false;
        next.left_speed_rpm10 = 0;
        next.right_speed_rpm10 = 0;
        app_flower_publish(&next);
        return;
    }

    has_result = maixcam_get_result(&camera);
    next.frame_age_ms = has_result ?
        ((uint32_t)(now - camera.tick) * portTICK_PERIOD_MS) : UINT32_MAX;
    next.frame_fresh = has_result &&
        ((int32_t)(camera.tick - app_flower_context.started_tick) >= 0) &&
        (next.frame_age_ms <= APP_FLOWER_FRAME_STALE_MS);
    new_frame = next.frame_fresh && (camera.tick != app_flower_context.last_frame_tick);
    if (new_frame) {
        app_flower_context.last_frame_tick = camera.tick;
    }
    if (!next.frame_fresh || !camera.detected) {
        if (next.frame_fresh && was_detected) {
            if (new_frame && (app_flower_context.missed_frames < UINT8_MAX)) {
                app_flower_context.missed_frames++;
            }
            if ((app_flower_context.missed_frames > 0U) &&
                (app_flower_context.missed_frames <=
                 APP_FLOWER_MISSED_FRAME_TOLERANCE)) {
                app_flower_publish(&next);
                return;
            }
        }
        next.detected = false;
        next.stable_frames = 0U;
        app_flower_context.detected_frames = 0U;
        app_flower_context.missed_frames = 0U;
        app_flower_context.settling = false;
        if (was_detected) {
            app_flower_context.lost_tick = now;
        }
        next.state = APP_FLOWER_STATE_SEARCHING;
        if (((uint32_t)(now - app_flower_context.lost_tick) * portTICK_PERIOD_MS) <
            APP_FLOWER_SEARCH_DELAY_MS) {
            app_flower_set_wheels(&next, 0, 0);
        } else {
            app_flower_set_wheels(
                &next,
                app_flower_context.search_direction * APP_FLOWER_SEARCH_SPEED_RPM10,
                -app_flower_context.search_direction * APP_FLOWER_SEARCH_SPEED_RPM10);
        }
        app_flower_publish(&next);
        return;
    }

    app_flower_context.missed_frames = 0U;
    if (!was_detected) {
        if (new_frame && (app_flower_context.detected_frames < UINT8_MAX)) {
            app_flower_context.detected_frames++;
        }
        if (app_flower_context.detected_frames <
            APP_FLOWER_DETECT_CONFIRM_FRAMES) {
            next.detected = false;
            app_flower_set_wheels(&next, 0, 0);
            app_flower_publish(&next);
            return;
        }
    }

    next.detected = true;
    next.camera_x = camera.center_x;
    next.camera_y = camera.center_y;
    next.camera_width = camera.width;
    next.camera_height = camera.height;
    next.error_x = (int16_t)((int32_t)camera.center_x - next.target_x);
    next.error_y = (int16_t)((int32_t)camera.center_y - next.target_y);
    if (new_frame && !next.initial_error_valid) {
        next.initial_camera_x = camera.center_x;
        next.initial_camera_y = camera.center_y;
        next.initial_error_x = next.error_x;
        next.initial_error_y = next.error_y;
        next.initial_error_valid = true;
    }
    if (((abs(next.error_x) <= APP_FLOWER_X_DEADBAND) &&
         (abs(next.error_y) <= APP_FLOWER_Y_DEADBAND)) ||
        (app_flower_context.settling &&
         (abs(next.error_x) <= APP_FLOWER_X_HOLD_DEADBAND) &&
         (abs(next.error_y) <= APP_FLOWER_Y_HOLD_DEADBAND))) {
        app_flower_context.settling = true;
        app_flower_set_wheels(&next, 0, 0);
        if (new_frame && (next.stable_frames < APP_FLOWER_STABLE_FRAMES)) {
            next.stable_frames++;
        }
        if (next.stable_frames >= APP_FLOWER_STABLE_FRAMES) {
            control_stop();
            next.state = APP_FLOWER_STATE_ALIGNED;
            next.active = false;
        } else {
            next.state = APP_FLOWER_STATE_TRACKING;
        }
    } else {
        int32_t linear;
        int32_t steering;

        next.stable_frames = 0U;
        app_flower_context.settling = false;
        next.state = APP_FLOWER_STATE_TRACKING;
        linear = app_flower_clamp(
            APP_FLOWER_LINEAR_POLARITY * next.error_y * APP_FLOWER_Y_GAIN_RPM10,
            -APP_FLOWER_MAX_LINEAR_RPM10,
            APP_FLOWER_MAX_LINEAR_RPM10);
        steering = app_flower_clamp(
            APP_FLOWER_STEER_POLARITY * next.error_x * APP_FLOWER_X_GAIN_RPM10,
            -APP_FLOWER_MAX_STEER_RPM10,
            APP_FLOWER_MAX_STEER_RPM10);
        if (abs(next.error_y) <= APP_FLOWER_Y_DEADBAND) {
            linear = 0;
        } else {
            linear = app_flower_apply_minimum(linear);
        }
        if (abs(next.error_x) <= APP_FLOWER_X_DEADBAND) {
            steering = 0;
        } else {
            steering = app_flower_apply_minimum(steering);
        }
        app_flower_set_wheels(&next, linear - steering, linear + steering);
    }
    app_flower_publish(&next);
}

bool app_flower_tracking_get_snapshot(app_flower_tracking_snapshot_t *snapshot) {
    if ((snapshot == NULL) || !app_flower_context.initialized) {
        return false;
    }
    taskENTER_CRITICAL();
    *snapshot = app_flower_context.snapshot;
    taskEXIT_CRITICAL();
    return true;
}

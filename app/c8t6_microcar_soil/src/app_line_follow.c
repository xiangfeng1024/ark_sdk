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

#include "app_line_follow.h"

#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "adc38_tracking.h"
#include "app_flower_tracking.h"
#include "app_dts_config.h"
#include "app_soil_probe.h"
#include "app_tracking_calibration.h"
#include "control.h"
#include "control_line_tracking.h"
#include "maixcam.h"

typedef struct {
    app_line_follow_snapshot_t snapshot;
    int32_t turn_start_left;
    int32_t turn_start_right;
    int32_t corner_approach_start;
    int32_t flower_start_left;
    int32_t flower_start_right;
    int32_t flower_path_left;
    int32_t flower_path_right;
    int32_t return_start_left;
    int32_t return_start_right;
    uint16_t flower_initial_x;
    uint16_t flower_initial_y;
    TickType_t start_delay_started;
    TickType_t pre_turn_pause_started;
    TickType_t flower_done_started;
    int8_t pending_turn;
    uint8_t corner_ticks;
    uint8_t corner_clear_ticks;
    uint8_t lost_ticks;
    uint8_t reacquire_ticks;
    uint8_t flower_attempts;
    uint32_t camera_command_sequence;
    TickType_t task_ready_last_tick;
    bool corner_armed;
    bool initialized;
} app_line_follow_context_t;

volatile app_course_direction_t g_app_course_direction =
    APP_COURSE_DIRECTION_UNKNOWN;
volatile uint32_t g_app_corner_count;

static app_line_follow_context_t app_line_context;

static uint8_t app_line_analyze(
    const adc38_tracking_sample_t *sample,
    uint8_t *strong_mask) {
    uint8_t channel;
    uint8_t middle_strong_count = 0U;

    *strong_mask = 0U;
    for (channel = 0U; channel < ADC38_TRACKING_CHANNEL_COUNT; ++channel) {
        uint32_t strength = 4095U - sample->normalized[channel];

        if (strength >= APP_LINE_STRONG_MIN) {
            *strong_mask |= (uint8_t)(1U << channel);
            if ((channel >= 1U) && (channel <= 6U)) {
                middle_strong_count++;
            }
        }
    }
    return middle_strong_count;
}

static int16_t app_line_middle_position(const adc38_tracking_sample_t *sample) {
    int64_t weighted = 0;
    uint32_t strength_sum = 0U;
    uint8_t channel;

    for (channel = 1U; channel <= 6U; ++channel) {
        uint32_t strength = 4095U - sample->normalized[channel];

        strength_sum += strength;
        weighted += (int64_t)strength * APP_LINE_MIDDLE_WEIGHTS[channel - 1U];
    }
    if (strength_sum == 0U) {
        return 0;
    }
    weighted /= (int32_t)strength_sum;
    if (weighted < -1024) {
        return -1024;
    }
    if (weighted > 1024) {
        return 1024;
    }
    return (int16_t)weighted;
}

static int16_t app_line_reported_position(
    int16_t middle_position,
    uint8_t strong_mask) {
    bool left_outer = (strong_mask & 0x01U) != 0U;
    bool right_outer = (strong_mask & 0x80U) != 0U;

    if (left_outer == right_outer) {
        return (left_outer && right_outer) ? 0 : middle_position;
    }
    return left_outer ? -2048 : 2048;
}

static int8_t app_line_corner_direction(uint8_t strong_mask) {
    bool left_outer = (strong_mask & 0x01U) != 0U;
    bool right_outer = (strong_mask & 0x80U) != 0U;

    if (left_outer == right_outer) {
        return 0;
    }
    return left_outer ? -1 : 1;
}

static bool app_line_start_flower_attempt(void) {
    int8_t search_direction = app_line_context.pending_turn;

    if ((app_line_context.flower_attempts & 1U) == 0U) {
        search_direction = -search_direction;
    }
    return app_flower_tracking_start(
        search_direction,
        APP_LINE_FLOWER_TIMEOUT_MS);
}

static int32_t app_line_average_distance(const control_speed_data_t *speed) {
    return speed->distance_count;
}

static int32_t app_line_turn_progress(const control_speed_data_t *speed) {
    int32_t left = abs(speed->left_encoder_count - app_line_context.turn_start_left);
    int32_t right = abs(speed->right_encoder_count - app_line_context.turn_start_right);

    return (left + right) / 2;
}

static void app_line_publish(const app_line_follow_snapshot_t *snapshot) {
    taskENTER_CRITICAL();
    app_line_context.snapshot = *snapshot;
    taskEXIT_CRITICAL();
}

static void app_line_stop(
    app_line_follow_snapshot_t *snapshot,
    bool is_fault) {
    control_stop();
    if (is_fault) {
        app_flower_tracking_stop();
        app_soil_probe_stop();
    }
    snapshot->state = is_fault ? APP_LINE_STATE_FAULT : APP_LINE_STATE_STOPPED;
}

static void app_line_notify_task_ready(app_line_state_t state) {
    TickType_t now;

    if ((state != APP_LINE_STATE_STOPPED) &&
        (state != APP_LINE_STATE_FAULT)) {
        return;
    }
    now = xTaskGetTickCount();
    if ((now - app_line_context.task_ready_last_tick) <
        pdMS_TO_TICKS(APP_LINE_READY_RETRY_MS)) {
        return;
    }
    app_line_context.task_ready_last_tick = now;
    (void)maixcam_notify_task_ready();
}

static bool app_line_follow(int16_t position, bool is_valid) {
    if (strcmp(control_strategy_current(), "line_tracking") != 0) {
        if (!control_strategy_select("line_tracking")) {
            return false;
        }
    }
    return control_line_tracking_observe(position, is_valid);
}

static void app_line_update_flower(app_line_follow_snapshot_t *snapshot) {
    app_flower_tracking_snapshot_t flower;

    snapshot->vision_enabled = true;
    if (!app_flower_tracking_get_snapshot(&flower)) {
        snapshot->flower_detected = false;
        snapshot->flower_aligned = false;
        snapshot->camera_x = 0U;
        snapshot->camera_y = 0U;
        snapshot->flower_error_x = 0;
        snapshot->flower_error_y = 0;
        snapshot->flower_stable_frames = 0U;
        return;
    }
    snapshot->flower_detected = flower.detected && flower.frame_fresh;
    snapshot->flower_aligned = flower.state == APP_FLOWER_STATE_ALIGNED;
    snapshot->camera_x = flower.camera_x;
    snapshot->camera_y = flower.camera_y;
    snapshot->flower_error_x = flower.error_x;
    snapshot->flower_error_y = flower.error_y;
    snapshot->flower_stable_frames = flower.stable_frames;
    if (!snapshot->flower_initial_error_valid && flower.initial_error_valid) {
        snapshot->flower_initial_error_valid = true;
        snapshot->flower_initial_error_x = flower.initial_error_x;
        snapshot->flower_initial_error_y = flower.initial_error_y;
    }
}

static bool app_line_update_soil(app_line_follow_snapshot_t *snapshot) {
    app_soil_probe_snapshot_t soil;

    if (!app_soil_probe_get_snapshot(&soil)) {
        return false;
    }
    snapshot->soil_sample_valid = soil.sample_valid;
    snapshot->soil_raw = soil.soil_raw;
    snapshot->soil_millivolts = soil.soil_millivolts;
    return true;
}

void app_line_follow_init(void) {
    memset(&app_line_context, 0, sizeof(app_line_context));
    app_line_context.snapshot.state = APP_LINE_STATE_STOPPED;
    app_line_context.task_ready_last_tick =
        xTaskGetTickCount() - pdMS_TO_TICKS(APP_LINE_READY_RETRY_MS);
    g_app_course_direction = APP_COURSE_DIRECTION_UNKNOWN;
    g_app_corner_count = 0U;
    app_flower_tracking_init();
    app_soil_probe_init();
    app_tracking_calibration_init();
    app_line_context.initialized = true;
    control_stop();
    app_line_publish(&app_line_context.snapshot);
}

void app_line_follow_step(void) {
    app_line_follow_snapshot_t next;
    adc38_tracking_sample_t sample;
    control_speed_data_t speed;
    uint8_t strong_count;
    uint8_t strong_mask;
    int16_t middle_position;
    int32_t distance;
    maixcam_command_request_t camera_request;
    bool start_requested = false;

    if (!app_line_context.initialized) {
        return;
    }
    app_flower_tracking_step();
    app_soil_probe_step();
    app_tracking_calibration_step(
        (app_line_context.snapshot.state == APP_LINE_STATE_STOPPED) ||
        (app_line_context.snapshot.state == APP_LINE_STATE_FAULT));
    if (maixcam_get_command(&camera_request) &&
        (camera_request.sequence != app_line_context.camera_command_sequence)) {
        app_line_context.camera_command_sequence = camera_request.sequence;
        if (camera_request.command == MAIXCAM_COMMAND_START_TASK) {
            bool accepted =
                (app_line_context.snapshot.state == APP_LINE_STATE_STOPPED) ||
                (app_line_context.snapshot.state == APP_LINE_STATE_FAULT);

            if (accepted) {
                start_requested = true;
            }
            (void)maixcam_send_response(
                camera_request.command,
                accepted ? MAIXCAM_STATUS_OK : MAIXCAM_STATUS_ERROR,
                NULL,
                0U);
        }
    }
    if (!control_get_speed(&speed) ||
        !adc38_tracking_read(&sample)) {
        next = app_line_context.snapshot;
        next.sensor_valid = false;
        app_line_stop(&next, true);
        app_line_notify_task_ready(next.state);
        app_line_publish(&next);
        return;
    }
    next = app_line_context.snapshot;
    next.sensor_valid = true;
    distance = app_line_average_distance(&speed);
    next.distance = distance;
    next.left_encoder_count = speed.left_encoder_count;
    next.right_encoder_count = speed.right_encoder_count;
    strong_count = app_line_analyze(&sample, &strong_mask);
    middle_position = app_line_middle_position(&sample);
    sample.position = middle_position;
    next.strong_count = strong_count;
    next.strong_mask = strong_mask;
    next.line_offset = app_line_reported_position(middle_position, strong_mask);
    if (start_requested) {
        app_flower_tracking_stop();
        control_stop();
        app_line_context.pending_turn = 0;
        app_line_context.corner_ticks = 0U;
        app_line_context.corner_clear_ticks = 0U;
        app_line_context.lost_ticks = 0U;
        app_line_context.reacquire_ticks = 0U;
        app_line_context.flower_attempts = 0U;
        app_line_context.corner_armed = true;
        app_line_context.start_delay_started = xTaskGetTickCount();
        next.state = APP_LINE_STATE_START_DELAY;
        next.course_direction = APP_COURSE_DIRECTION_UNKNOWN;
        next.segment_distance = 0;
        next.corner_approach = 0;
        next.turn_progress = 0;
        next.vision_enabled = false;
        next.flower_detected = false;
        next.camera_x = 0U;
        next.camera_y = 0U;
        next.flower_error_x = 0;
        next.flower_error_y = 0;
        next.flower_aligned = false;
        next.flower_initial_error_valid = false;
        next.soil_sample_valid = false;
        next.completion_blink_on = false;
        g_app_course_direction = APP_COURSE_DIRECTION_UNKNOWN;
        g_app_corner_count = 0U;
    }

    switch (next.state) {
    case APP_LINE_STATE_START_DELAY:
        control_stop();
        if ((xTaskGetTickCount() - app_line_context.start_delay_started) >=
            pdMS_TO_TICKS(APP_LINE_START_DELAY_MS)) {
            next.state = APP_LINE_STATE_FOLLOW_TO_CORNER;
        }
        break;
    case APP_LINE_STATE_FOLLOW_TO_CORNER: {
        int8_t direction;

        if (!app_line_follow(middle_position, strong_count > 0U)) {
            app_line_stop(&next, true);
            break;
        }
        if (!app_line_context.corner_armed) {
            app_line_context.corner_ticks = 0U;
            app_line_context.pending_turn = 0;
            app_line_context.lost_ticks = 0U;
            if ((strong_mask & 0x81U) == 0U) {
                if (++app_line_context.corner_clear_ticks >=
                    APP_LINE_CORNER_RELEASE_TICKS) {
                    app_line_context.corner_armed = true;
                    app_line_context.corner_clear_ticks = 0U;
                }
            } else {
                app_line_context.corner_clear_ticks = 0U;
            }
            next.corner_confirm_ticks = 0U;
            break;
        }
        if (strong_count == 0U) {
            if (++app_line_context.lost_ticks >= APP_LINE_LOST_LIMIT_TICKS) {
                app_line_stop(&next, true);
                break;
            }
        } else {
            app_line_context.lost_ticks = 0U;
        }
        direction = app_line_corner_direction(strong_mask);
        if (direction == 0) {
            app_line_context.corner_ticks = 0U;
            app_line_context.pending_turn = 0;
        } else if (direction != app_line_context.pending_turn) {
            app_line_context.pending_turn = direction;
            app_line_context.corner_ticks = 1U;
        } else if (++app_line_context.corner_ticks >= APP_LINE_CORNER_CONFIRM_TICKS) {
            app_line_context.corner_approach_start = distance;
            next.corner_approach = 0;
            next.state = APP_LINE_STATE_APPROACH_CORNER;
            app_line_context.corner_armed = false;
            (void)control_strategy_select("manual");
        }
        next.corner_confirm_ticks = app_line_context.corner_ticks;
        break;
    }
    case APP_LINE_STATE_APPROACH_CORNER:
        next.corner_approach = abs(distance - app_line_context.corner_approach_start);
        (void)control_set_speed(
            APP_LINE_BASE_SPEED_RPM10,
            APP_LINE_BASE_SPEED_RPM10);
        if (next.corner_approach >= APP_LINE_CORNER_APPROACH_DISTANCE) {
            control_stop();
            app_line_context.pre_turn_pause_started = xTaskGetTickCount();
            next.state = APP_LINE_STATE_PRE_TURN_PAUSE;
        }
        break;
    case APP_LINE_STATE_PRE_TURN_PAUSE:
        control_stop();
        if ((xTaskGetTickCount() - app_line_context.pre_turn_pause_started) >=
            pdMS_TO_TICKS(APP_LINE_PRE_TURN_PAUSE_MS)) {
            app_line_context.turn_start_left = speed.left_encoder_count;
            app_line_context.turn_start_right = speed.right_encoder_count;
            app_line_context.reacquire_ticks = 0U;
            next.turn_progress = 0;
            next.state = APP_LINE_STATE_TURNING;
        }
        break;
    case APP_LINE_STATE_TURNING:
        next.turn_progress = app_line_turn_progress(&speed);
        (void)control_set_speed(
            app_line_context.pending_turn * APP_LINE_TURN_SPEED_RPM10,
            -app_line_context.pending_turn * APP_LINE_TURN_SPEED_RPM10);
        if ((next.turn_progress >= APP_LINE_TURN_MIN_COUNTS) &&
            (strong_count > 0U) &&
            (abs(sample.position) <= APP_LINE_TURN_CENTER_LIMIT)) {
            if (++app_line_context.reacquire_ticks >= APP_LINE_TURN_REACQUIRE_TICKS) {
                control_stop();
                if (g_app_course_direction == APP_COURSE_DIRECTION_UNKNOWN) {
                    g_app_course_direction = (app_line_context.pending_turn > 0) ?
                        APP_COURSE_DIRECTION_CLOCKWISE :
                        APP_COURSE_DIRECTION_COUNTERCLOCKWISE;
                }
                g_app_corner_count++;
                next.course_direction = g_app_course_direction;
                next.state = APP_LINE_STATE_FLOWER_TRACKING;
                next.vision_enabled = true;
                next.flower_aligned = false;
                next.flower_initial_error_valid = false;
                next.completion_blink_on = false;
                app_line_context.flower_start_left = speed.left_encoder_count;
                app_line_context.flower_start_right = speed.right_encoder_count;
                app_line_context.flower_attempts = 1U;
                if (!app_line_start_flower_attempt()) {
                    app_line_stop(&next, true);
                }
            }
        } else {
            app_line_context.reacquire_ticks = 0U;
        }
        if (next.turn_progress >= APP_LINE_TURN_MAX_COUNTS) {
            app_line_stop(&next, true);
        }
        break;
    case APP_LINE_STATE_FLOWER_TRACKING: {
        app_flower_tracking_snapshot_t flower;

        app_line_update_flower(&next);
        if (app_flower_tracking_get_snapshot(&flower) &&
            (flower.state == APP_FLOWER_STATE_ALIGNED)) {
            control_stop();
            app_line_context.flower_path_left =
                speed.left_encoder_count - app_line_context.flower_start_left;
            app_line_context.flower_path_right =
                speed.right_encoder_count - app_line_context.flower_start_right;
            app_line_context.flower_initial_x = flower.initial_camera_x;
            app_line_context.flower_initial_y = flower.initial_camera_y;
            next.flower_aligned = true;
            next.return_left_target = -app_line_context.flower_path_left;
            next.return_right_target = -app_line_context.flower_path_right;
            next.soil_sample_valid = false;
            if (app_soil_probe_start()) {
                next.state = APP_LINE_STATE_SOIL_PROBING;
            } else {
                app_line_stop(&next, true);
            }
        } else if (app_flower_tracking_get_snapshot(&flower) &&
                   (flower.state == APP_FLOWER_STATE_TIMEOUT)) {
            if (app_line_context.flower_attempts <
                APP_LINE_FLOWER_MAX_ATTEMPTS) {
                app_line_context.flower_attempts++;
                app_flower_tracking_stop();
                if (!app_line_start_flower_attempt()) {
                    app_line_stop(&next, true);
                }
            } else {
                app_line_stop(&next, true);
            }
        }
        break;
    }
    case APP_LINE_STATE_SOIL_PROBING: {
        app_soil_probe_snapshot_t soil;

        control_stop();
        app_line_update_soil(&next);
        if (app_soil_probe_get_snapshot(&soil) &&
            (soil.state == APP_SOIL_PROBE_STATE_COMPLETE)) {
            app_line_context.return_start_left = speed.left_encoder_count;
            app_line_context.return_start_right = speed.right_encoder_count;
            next.return_left_progress = 0;
            next.return_right_progress = 0;
            next.vision_enabled = true;
            if (app_flower_tracking_start_target(
                    -app_line_context.pending_turn,
                    APP_LINE_RETURN_TIMEOUT_MS,
                    app_line_context.flower_initial_x,
                    app_line_context.flower_initial_y)) {
                next.state = APP_LINE_STATE_RETURN_TO_LINE;
            } else {
                app_line_stop(&next, true);
            }
        } else if (app_soil_probe_get_snapshot(&soil) &&
                   (soil.state == APP_SOIL_PROBE_STATE_ERROR)) {
            app_line_stop(&next, true);
        }
        break;
    }
    case APP_LINE_STATE_RETURN_TO_LINE: {
        app_flower_tracking_snapshot_t flower;

        app_line_update_flower(&next);
        next.return_left_progress =
            speed.left_encoder_count - app_line_context.return_start_left;
        next.return_right_progress =
            speed.right_encoder_count - app_line_context.return_start_right;
        if (app_flower_tracking_get_snapshot(&flower) &&
            (flower.state == APP_FLOWER_STATE_ALIGNED)) {
            control_stop();
            app_line_context.flower_done_started = xTaskGetTickCount();
            next.state = APP_LINE_STATE_FLOWER_DONE;
            next.vision_enabled = false;
            next.completion_blink_on = true;
        } else if (app_flower_tracking_get_snapshot(&flower) &&
                   (flower.state == APP_FLOWER_STATE_TIMEOUT)) {
            app_line_stop(&next, true);
        }
        break;
    }
    case APP_LINE_STATE_FLOWER_DONE: {
        uint32_t elapsed_ms =
            (uint32_t)(xTaskGetTickCount() - app_line_context.flower_done_started) *
            portTICK_PERIOD_MS;

        control_stop();
        next.completion_blink_on =
            ((elapsed_ms / APP_LINE_FLOWER_BLINK_HALF_MS) & 1U) == 0U;
        if (elapsed_ms >= APP_LINE_FLOWER_DONE_MS) {
            app_line_context.pending_turn = 0;
            app_line_context.corner_ticks = 0U;
            app_line_context.corner_clear_ticks = 0U;
            app_line_context.corner_armed = false;
            app_line_context.lost_ticks = 0U;
            app_line_context.reacquire_ticks = 0U;
            next.corner_confirm_ticks = 0U;
            next.vision_enabled = false;
            next.flower_detected = false;
            next.flower_aligned = false;
            next.completion_blink_on = false;
            app_line_context.lost_ticks = 0U;
            if (g_app_corner_count >= APP_LINE_TARGET_CORNER_COUNT) {
                app_line_stop(&next, false);
            } else {
                next.state = APP_LINE_STATE_FOLLOW_TO_CORNER;
            }
        }
        break;
    }
    case APP_LINE_STATE_STOPPED:
        if (next.vision_enabled) {
            app_line_update_flower(&next);
        }
        break;
    case APP_LINE_STATE_FAULT:
    default:
        control_stop();
        if (next.vision_enabled) {
            app_line_update_flower(&next);
        }
        break;
    }
    next.course_direction = g_app_course_direction;
    next.corner_count = g_app_corner_count;
    app_line_notify_task_ready(next.state);
    app_line_publish(&next);
}

bool app_line_follow_get_snapshot(app_line_follow_snapshot_t *snapshot) {
    if ((snapshot == NULL) || !app_line_context.initialized) {
        return false;
    }
    taskENTER_CRITICAL();
    *snapshot = app_line_context.snapshot;
    taskEXIT_CRITICAL();
    return true;
}

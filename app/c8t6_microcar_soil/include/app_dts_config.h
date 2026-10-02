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

#ifndef MICROCAR_SOIL_APP_DTS_CONFIG_H
#define MICROCAR_SOIL_APP_DTS_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t control_stack_words, sensor_stack_words, oled_stack_words, business_stack_words;
    uint32_t control_priority, sensor_priority, oled_priority, business_priority;
    uint32_t control_period_ms, sensor_period_ms, oled_period_ms, business_period_ms;
    uint32_t oled_page_period_ms, bluetooth_period_ms;
    uint32_t light_max_lux, ws2812b_max_level, ws2812b_max_white;
    uint32_t line_period_ms, line_flower_timeout_ms, line_flower_max_attempts;
    uint32_t line_flower_done_ms, line_flower_blink_half_ms, line_return_timeout_ms;
    uint32_t line_target_corner_count, line_pre_turn_pause_ms, line_start_delay_ms;
    uint32_t line_ready_retry_ms, line_strong_min, line_corner_confirm_ticks;
    uint32_t line_corner_release_ticks, line_lost_limit_ticks, line_turn_reacquire_ticks;
    int32_t line_base_speed_rpm10, line_turn_speed_rpm10, line_corner_approach_distance;
    int32_t line_turn_min_counts, line_turn_max_counts, line_turn_center_limit;
    int32_t line_middle_weights[6];
    int32_t flower_target_x, flower_target_y, flower_x_deadband, flower_y_deadband;
    int32_t flower_x_hold_deadband, flower_y_hold_deadband;
    int32_t flower_x_gain_rpm10, flower_y_gain_rpm10;
    int32_t flower_linear_polarity, flower_steer_polarity;
    int32_t flower_min_speed_rpm10, flower_max_linear_rpm10, flower_max_steer_rpm10;
    int32_t flower_max_wheel_rpm10, flower_search_speed_rpm10, flower_search_polarity;
    uint32_t flower_search_delay_ms, flower_frame_stale_ms;
    uint32_t flower_detect_confirm_frames, flower_missed_frame_tolerance, flower_stable_frames;
    int32_t soil_insert_delta_deg, soil_direction;
    uint32_t soil_move_settle_ms, soil_hold_ms, soil_sample_timeout_ms, soil_alarm_ms;
    uint32_t soil_baseline_sample_count, soil_insert_drop_raw, soil_baseline_filter_shift;
} app_dts_config_t;

bool app_dts_config_load(void);
const app_dts_config_t *app_dts_config_get(void);

#define APP_CONTROL_TASK_STACK_DEPTH (app_dts_config_get()->control_stack_words)
#define APP_SENSOR_TASK_STACK_DEPTH (app_dts_config_get()->sensor_stack_words)
#define APP_OLED_TASK_STACK_DEPTH (app_dts_config_get()->oled_stack_words)
#define APP_BUSINESS_TASK_STACK_DEPTH (app_dts_config_get()->business_stack_words)
#define APP_CONTROL_PERIOD_MS (app_dts_config_get()->control_period_ms)
#define APP_SENSOR_PERIOD_MS (app_dts_config_get()->sensor_period_ms)
#define APP_OLED_PERIOD_MS (app_dts_config_get()->oled_period_ms)
#define APP_BUSINESS_PERIOD_MS (app_dts_config_get()->business_period_ms)
#define APP_OLED_PAGE_PERIOD_MS (app_dts_config_get()->oled_page_period_ms)
#define APP_BLUETOOTH_PERIOD_MS (app_dts_config_get()->bluetooth_period_ms)
#define APP_LIGHT_MAX_LUX (app_dts_config_get()->light_max_lux)
#define APP_WS2812B_MAX_LEVEL (app_dts_config_get()->ws2812b_max_level)
#define APP_WS2812B_MAX_WHITE (app_dts_config_get()->ws2812b_max_white)
#define APP_LINE_PERIOD_MS (app_dts_config_get()->line_period_ms)
#define APP_LINE_BASE_SPEED_RPM10 (app_dts_config_get()->line_base_speed_rpm10)
#define APP_LINE_TURN_SPEED_RPM10 (app_dts_config_get()->line_turn_speed_rpm10)
#define APP_LINE_FLOWER_TIMEOUT_MS (app_dts_config_get()->line_flower_timeout_ms)
#define APP_LINE_FLOWER_MAX_ATTEMPTS (app_dts_config_get()->line_flower_max_attempts)
#define APP_LINE_FLOWER_DONE_MS (app_dts_config_get()->line_flower_done_ms)
#define APP_LINE_FLOWER_BLINK_HALF_MS (app_dts_config_get()->line_flower_blink_half_ms)
#define APP_LINE_RETURN_TIMEOUT_MS (app_dts_config_get()->line_return_timeout_ms)
#define APP_LINE_TARGET_CORNER_COUNT (app_dts_config_get()->line_target_corner_count)
#define APP_LINE_PRE_TURN_PAUSE_MS (app_dts_config_get()->line_pre_turn_pause_ms)
#define APP_LINE_START_DELAY_MS (app_dts_config_get()->line_start_delay_ms)
#define APP_LINE_READY_RETRY_MS (app_dts_config_get()->line_ready_retry_ms)
#define APP_LINE_STRONG_MIN (app_dts_config_get()->line_strong_min)
#define APP_LINE_CORNER_CONFIRM_TICKS (app_dts_config_get()->line_corner_confirm_ticks)
#define APP_LINE_CORNER_RELEASE_TICKS (app_dts_config_get()->line_corner_release_ticks)
#define APP_LINE_CORNER_APPROACH_DISTANCE (app_dts_config_get()->line_corner_approach_distance)
#define APP_LINE_LOST_LIMIT_TICKS (app_dts_config_get()->line_lost_limit_ticks)
#define APP_LINE_TURN_MIN_COUNTS (app_dts_config_get()->line_turn_min_counts)
#define APP_LINE_TURN_MAX_COUNTS (app_dts_config_get()->line_turn_max_counts)
#define APP_LINE_TURN_CENTER_LIMIT (app_dts_config_get()->line_turn_center_limit)
#define APP_LINE_TURN_REACQUIRE_TICKS (app_dts_config_get()->line_turn_reacquire_ticks)
#define APP_LINE_MIDDLE_WEIGHTS (app_dts_config_get()->line_middle_weights)
#define APP_FLOWER_TARGET_X (app_dts_config_get()->flower_target_x)
#define APP_FLOWER_TARGET_Y (app_dts_config_get()->flower_target_y)
#define APP_FLOWER_X_DEADBAND (app_dts_config_get()->flower_x_deadband)
#define APP_FLOWER_Y_DEADBAND (app_dts_config_get()->flower_y_deadband)
#define APP_FLOWER_X_HOLD_DEADBAND (app_dts_config_get()->flower_x_hold_deadband)
#define APP_FLOWER_Y_HOLD_DEADBAND (app_dts_config_get()->flower_y_hold_deadband)
#define APP_FLOWER_X_GAIN_RPM10 (app_dts_config_get()->flower_x_gain_rpm10)
#define APP_FLOWER_Y_GAIN_RPM10 (app_dts_config_get()->flower_y_gain_rpm10)
#define APP_FLOWER_LINEAR_POLARITY (app_dts_config_get()->flower_linear_polarity)
#define APP_FLOWER_STEER_POLARITY (app_dts_config_get()->flower_steer_polarity)
#define APP_FLOWER_MIN_SPEED_RPM10 (app_dts_config_get()->flower_min_speed_rpm10)
#define APP_FLOWER_MAX_LINEAR_RPM10 (app_dts_config_get()->flower_max_linear_rpm10)
#define APP_FLOWER_MAX_STEER_RPM10 (app_dts_config_get()->flower_max_steer_rpm10)
#define APP_FLOWER_MAX_WHEEL_RPM10 (app_dts_config_get()->flower_max_wheel_rpm10)
#define APP_FLOWER_SEARCH_SPEED_RPM10 (app_dts_config_get()->flower_search_speed_rpm10)
#define APP_FLOWER_SEARCH_POLARITY (app_dts_config_get()->flower_search_polarity)
#define APP_FLOWER_SEARCH_DELAY_MS (app_dts_config_get()->flower_search_delay_ms)
#define APP_FLOWER_FRAME_STALE_MS (app_dts_config_get()->flower_frame_stale_ms)
#define APP_FLOWER_DETECT_CONFIRM_FRAMES (app_dts_config_get()->flower_detect_confirm_frames)
#define APP_FLOWER_MISSED_FRAME_TOLERANCE (app_dts_config_get()->flower_missed_frame_tolerance)
#define APP_FLOWER_STABLE_FRAMES (app_dts_config_get()->flower_stable_frames)
#define APP_SOIL_PROBE_INSERT_DELTA_DEG (app_dts_config_get()->soil_insert_delta_deg)
#define APP_SOIL_PROBE_DIRECTION (app_dts_config_get()->soil_direction)
#define APP_SOIL_PROBE_MOVE_SETTLE_MS (app_dts_config_get()->soil_move_settle_ms)
#define APP_SOIL_PROBE_HOLD_MS (app_dts_config_get()->soil_hold_ms)
#define APP_SOIL_PROBE_SAMPLE_TIMEOUT_MS (app_dts_config_get()->soil_sample_timeout_ms)
#define APP_SOIL_PROBE_ALARM_MS (app_dts_config_get()->soil_alarm_ms)
#define APP_SOIL_BASELINE_SAMPLE_COUNT (app_dts_config_get()->soil_baseline_sample_count)
#define APP_SOIL_INSERT_DROP_RAW (app_dts_config_get()->soil_insert_drop_raw)
#define APP_SOIL_BASELINE_FILTER_SHIFT (app_dts_config_get()->soil_baseline_filter_shift)

#endif /* MICROCAR_SOIL_APP_DTS_CONFIG_H */

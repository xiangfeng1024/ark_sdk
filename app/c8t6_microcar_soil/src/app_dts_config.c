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

#include "app_dts_config.h"

#include "ark_dts.h"

static app_dts_config_t app_config;

const app_dts_config_t *app_dts_config_get(void) {
    return &app_config;
}

static bool read_u32(const ark_of_node_t *node, const char *name, uint32_t *value) {
    return ark_of_property_read_u32(node, name, value) == 0;
}

static bool read_s32(const ark_of_node_t *node, const char *name, int32_t *value) {
    return ark_of_property_read_s32(node, name, value) == 0;
}

static bool read_s32_array(
    const ark_of_node_t *node,
    const char *name,
    int32_t *values,
    size_t count) {
    uint32_t raw[6];
    size_t index;

    if ((count > 6U) ||
        (ark_of_property_read_u32_array(node, name, raw, count) != 0)) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        values[index] = (int32_t)raw[index];
    }
    return true;
}

#define READ_U(node, property, field) read_u32((node), (property), &app_config.field)
#define READ_S(node, property, field) read_s32((node), (property), &app_config.field)

bool app_dts_config_load(void) {
    const ark_of_node_t *tasks = ark_of_find_node_by_path("/software/tasks");
    const ark_of_node_t *lighting = ark_of_find_node_by_path("/software/lighting");
    const ark_of_node_t *line = ark_of_find_node_by_path("/software/line_follow");
    const ark_of_node_t *flower = ark_of_find_node_by_path("/software/flower_tracking");
    const ark_of_node_t *soil = ark_of_find_node_by_path("/software/soil_probe");

    if ((tasks == NULL) || (lighting == NULL) || (line == NULL) ||
        (flower == NULL) || (soil == NULL)) {
        return false;
    }
    return
        READ_U(tasks, "ark,control-stack-words", control_stack_words) &&
        READ_U(tasks, "ark,sensor-stack-words", sensor_stack_words) &&
        READ_U(tasks, "ark,oled-stack-words", oled_stack_words) &&
        READ_U(tasks, "ark,business-stack-words", business_stack_words) &&
        READ_U(tasks, "ark,control-priority", control_priority) &&
        READ_U(tasks, "ark,sensor-priority", sensor_priority) &&
        READ_U(tasks, "ark,oled-priority", oled_priority) &&
        READ_U(tasks, "ark,business-priority", business_priority) &&
        READ_U(tasks, "ark,control-period-ms", control_period_ms) &&
        READ_U(tasks, "ark,sensor-period-ms", sensor_period_ms) &&
        READ_U(tasks, "ark,oled-period-ms", oled_period_ms) &&
        READ_U(tasks, "ark,business-period-ms", business_period_ms) &&
        READ_U(tasks, "ark,oled-page-period-ms", oled_page_period_ms) &&
        READ_U(tasks, "ark,bluetooth-period-ms", bluetooth_period_ms) &&
        READ_U(lighting, "ark,max-lux", light_max_lux) &&
        READ_U(lighting, "ark,max-level", ws2812b_max_level) &&
        READ_U(lighting, "ark,max-white", ws2812b_max_white) &&
        READ_U(line, "ark,period-ms", line_period_ms) &&
        READ_S(line, "ark,base-speed-rpm10", line_base_speed_rpm10) &&
        READ_S(line, "ark,turn-speed-rpm10", line_turn_speed_rpm10) &&
        READ_U(line, "ark,flower-timeout-ms", line_flower_timeout_ms) &&
        READ_U(line, "ark,flower-max-attempts", line_flower_max_attempts) &&
        READ_U(line, "ark,flower-done-ms", line_flower_done_ms) &&
        READ_U(line, "ark,flower-blink-half-ms", line_flower_blink_half_ms) &&
        READ_U(line, "ark,return-timeout-ms", line_return_timeout_ms) &&
        READ_U(line, "ark,target-corner-count", line_target_corner_count) &&
        READ_U(line, "ark,pre-turn-pause-ms", line_pre_turn_pause_ms) &&
        READ_U(line, "ark,start-delay-ms", line_start_delay_ms) &&
        READ_U(line, "ark,ready-retry-ms", line_ready_retry_ms) &&
        READ_U(line, "ark,strong-min", line_strong_min) &&
        READ_U(line, "ark,corner-confirm-ticks", line_corner_confirm_ticks) &&
        READ_U(line, "ark,corner-release-ticks", line_corner_release_ticks) &&
        READ_S(line, "ark,corner-approach-distance", line_corner_approach_distance) &&
        READ_U(line, "ark,lost-limit-ticks", line_lost_limit_ticks) &&
        READ_S(line, "ark,turn-min-counts", line_turn_min_counts) &&
        READ_S(line, "ark,turn-max-counts", line_turn_max_counts) &&
        READ_S(line, "ark,turn-center-limit", line_turn_center_limit) &&
        READ_U(line, "ark,turn-reacquire-ticks", line_turn_reacquire_ticks) &&
        read_s32_array(line, "ark,middle-weights", app_config.line_middle_weights, 6U) &&
        READ_S(flower, "ark,target-x", flower_target_x) &&
        READ_S(flower, "ark,target-y", flower_target_y) &&
        READ_S(flower, "ark,x-deadband", flower_x_deadband) &&
        READ_S(flower, "ark,y-deadband", flower_y_deadband) &&
        READ_S(flower, "ark,x-hold-deadband", flower_x_hold_deadband) &&
        READ_S(flower, "ark,y-hold-deadband", flower_y_hold_deadband) &&
        READ_S(flower, "ark,x-gain-rpm10", flower_x_gain_rpm10) &&
        READ_S(flower, "ark,y-gain-rpm10", flower_y_gain_rpm10) &&
        READ_S(flower, "ark,linear-polarity", flower_linear_polarity) &&
        READ_S(flower, "ark,steer-polarity", flower_steer_polarity) &&
        READ_S(flower, "ark,min-speed-rpm10", flower_min_speed_rpm10) &&
        READ_S(flower, "ark,max-linear-rpm10", flower_max_linear_rpm10) &&
        READ_S(flower, "ark,max-steer-rpm10", flower_max_steer_rpm10) &&
        READ_S(flower, "ark,max-wheel-rpm10", flower_max_wheel_rpm10) &&
        READ_S(flower, "ark,search-speed-rpm10", flower_search_speed_rpm10) &&
        READ_S(flower, "ark,search-polarity", flower_search_polarity) &&
        READ_U(flower, "ark,search-delay-ms", flower_search_delay_ms) &&
        READ_U(flower, "ark,frame-stale-ms", flower_frame_stale_ms) &&
        READ_U(flower, "ark,detect-confirm-frames", flower_detect_confirm_frames) &&
        READ_U(flower, "ark,missed-frame-tolerance", flower_missed_frame_tolerance) &&
        READ_U(flower, "ark,stable-frames", flower_stable_frames) &&
        READ_S(soil, "ark,insert-delta-deg", soil_insert_delta_deg) &&
        READ_S(soil, "ark,direction", soil_direction) &&
        READ_U(soil, "ark,move-settle-ms", soil_move_settle_ms) &&
        READ_U(soil, "ark,hold-ms", soil_hold_ms) &&
        READ_U(soil, "ark,sample-timeout-ms", soil_sample_timeout_ms) &&
        READ_U(soil, "ark,alarm-ms", soil_alarm_ms) &&
        READ_U(soil, "ark,baseline-sample-count", soil_baseline_sample_count) &&
        READ_U(soil, "ark,insert-drop-raw", soil_insert_drop_raw) &&
        READ_U(soil, "ark,baseline-filter-shift", soil_baseline_filter_shift);
}

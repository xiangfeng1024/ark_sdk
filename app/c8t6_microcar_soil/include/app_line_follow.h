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

#ifndef MICROCAR_SOIL_APP_LINE_FOLLOW_H
#define MICROCAR_SOIL_APP_LINE_FOLLOW_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_COURSE_DIRECTION_UNKNOWN = 0,
    APP_COURSE_DIRECTION_CLOCKWISE = 1,
    APP_COURSE_DIRECTION_COUNTERCLOCKWISE = -1
} app_course_direction_t;

typedef enum {
    APP_LINE_STATE_FOLLOW_TO_CORNER = 0,
    APP_LINE_STATE_APPROACH_CORNER,
    APP_LINE_STATE_PRE_TURN_PAUSE,
    APP_LINE_STATE_TURNING,
    APP_LINE_STATE_FLOWER_TRACKING,
    APP_LINE_STATE_SOIL_PROBING,
    APP_LINE_STATE_RETURN_TO_LINE,
    APP_LINE_STATE_FLOWER_DONE,
    APP_LINE_STATE_STOPPED,
    APP_LINE_STATE_FAULT,
    APP_LINE_STATE_START_DELAY
} app_line_state_t;

typedef struct {
    app_line_state_t state;
    app_course_direction_t course_direction;
    int16_t line_offset;
    int32_t distance;
    int32_t left_encoder_count;
    int32_t right_encoder_count;
    int32_t segment_distance;
    int32_t corner_approach;
    int32_t turn_progress;
    int32_t return_left_progress;
    int32_t return_right_progress;
    int32_t return_left_target;
    int32_t return_right_target;
    bool sensor_valid;
    bool vision_enabled;
    bool flower_detected;
    uint16_t camera_x;
    uint16_t camera_y;
    int16_t flower_error_x;
    int16_t flower_error_y;
    uint8_t strong_count;
    uint8_t strong_mask;
    uint8_t corner_confirm_ticks;
    uint8_t flower_stable_frames;
    int16_t flower_initial_error_x;
    int16_t flower_initial_error_y;
    bool flower_initial_error_valid;
    uint32_t corner_count;
    bool flower_aligned;
    bool completion_blink_on;
    bool soil_sample_valid;
    uint16_t soil_raw;
    uint16_t soil_millivolts;
} app_line_follow_snapshot_t;

extern volatile app_course_direction_t g_app_course_direction;
extern volatile uint32_t g_app_corner_count;

void app_line_follow_init(void);
void app_line_follow_step(void);
bool app_line_follow_get_snapshot(app_line_follow_snapshot_t *snapshot);

#endif /* MICROCAR_SOIL_APP_LINE_FOLLOW_H */

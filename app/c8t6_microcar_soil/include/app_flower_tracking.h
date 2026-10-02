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

#ifndef MICROCAR_SOIL_APP_FLOWER_TRACKING_H
#define MICROCAR_SOIL_APP_FLOWER_TRACKING_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_FLOWER_STATE_IDLE = 0,
    APP_FLOWER_STATE_SEARCHING,
    APP_FLOWER_STATE_TRACKING,
    APP_FLOWER_STATE_ALIGNED,
    APP_FLOWER_STATE_TIMEOUT
} app_flower_state_t;

typedef struct {
    app_flower_state_t state;
    bool active;
    bool detected;
    bool frame_fresh;
    uint16_t camera_x;
    uint16_t camera_y;
    uint16_t camera_width;
    uint16_t camera_height;
    uint16_t target_x;
    uint16_t target_y;
    uint16_t initial_camera_x;
    uint16_t initial_camera_y;
    int16_t error_x;
    int16_t error_y;
    int16_t initial_error_x;
    int16_t initial_error_y;
    int16_t left_speed_rpm10;
    int16_t right_speed_rpm10;
    uint8_t stable_frames;
    bool initial_error_valid;
    uint32_t frame_age_ms;
    uint32_t elapsed_ms;
} app_flower_tracking_snapshot_t;

void app_flower_tracking_init(void);
bool app_flower_tracking_start(int8_t search_direction, uint32_t timeout_ms);
bool app_flower_tracking_start_target(
    int8_t search_direction,
    uint32_t timeout_ms,
    uint16_t target_x,
    uint16_t target_y);
void app_flower_tracking_stop(void);
void app_flower_tracking_step(void);
bool app_flower_tracking_get_snapshot(app_flower_tracking_snapshot_t *snapshot);

#endif /* MICROCAR_SOIL_APP_FLOWER_TRACKING_H */

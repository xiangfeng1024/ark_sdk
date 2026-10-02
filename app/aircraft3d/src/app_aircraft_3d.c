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

#include "app_aircraft_3d.h"

#include <math.h>
#include <stddef.h>

#include "oled.h"

#define APP_AIRCRAFT_VERTEX_COUNT 14U
#define APP_AIRCRAFT_EDGE_COUNT 24U
#define APP_AIRCRAFT_CENTER_X 64.0f
#define APP_AIRCRAFT_CENTER_Y 32.0f
#define APP_AIRCRAFT_SCALE 9.0f
#define APP_AIRCRAFT_VIEW_SINE_YAW -0.342020143f
#define APP_AIRCRAFT_VIEW_COSINE_YAW 0.939692621f
#define APP_AIRCRAFT_VIEW_SINE_TILT 0.422618262f
#define APP_AIRCRAFT_VIEW_COSINE_TILT 0.906307787f
#define APP_AIRCRAFT_DEG_TO_RAD 0.01745329252f

typedef struct {
    float x;
    float y;
    float z;
} app_aircraft_vertex_t;

typedef struct {
    int16_t x;
    int16_t y;
} app_aircraft_point_t;

static const app_aircraft_vertex_t app_aircraft_vertices[APP_AIRCRAFT_VERTEX_COUNT] = {
    { 3.2f,  0.0f,  0.0f},
    { 1.2f,  0.0f,  0.0f},
    {-2.3f,  0.0f,  0.0f},
    {-3.0f,  0.0f,  0.0f},
    { 0.2f,  3.2f,  0.0f},
    {-1.1f,  0.6f,  0.0f},
    { 0.2f, -3.2f,  0.0f},
    {-1.1f, -0.6f,  0.0f},
    {-2.2f,  1.4f,  0.0f},
    {-2.2f, -1.4f,  0.0f},
    {-2.3f,  0.0f,  1.4f},
    { 1.0f,  0.0f,  0.65f},
    {-0.3f,  0.0f,  0.55f},
    { 0.3f,  0.0f, -0.35f},
};

static const uint8_t app_aircraft_edges[APP_AIRCRAFT_EDGE_COUNT][2] = {
    {0U, 1U}, {1U, 2U}, {2U, 3U},
    {1U, 4U}, {4U, 5U}, {5U, 2U},
    {1U, 6U}, {6U, 7U}, {7U, 2U},
    {2U, 8U}, {8U, 3U}, {2U, 9U}, {9U, 3U},
    {2U, 10U}, {10U, 3U},
    {1U, 11U}, {11U, 12U}, {12U, 2U},
    {0U, 13U}, {13U, 2U},
    {4U, 6U}, {8U, 9U}, {11U, 13U}, {12U, 13U},
};

static int16_t app_aircraft_round(float value) {
    return (int16_t)(value + ((value >= 0.0f) ? 0.5f : -0.5f));
}

static app_aircraft_point_t app_aircraft_project(
    const app_aircraft_vertex_t *vertex,
    const float sine[3],
    const float cosine[3]) {
    float roll_y = cosine[0] * vertex->y - sine[0] * vertex->z;
    float roll_z = sine[0] * vertex->y + cosine[0] * vertex->z;
    float pitch_x = cosine[1] * vertex->x + sine[1] * roll_z;
    float pitch_z = -sine[1] * vertex->x + cosine[1] * roll_z;
    float yaw_x = cosine[2] * pitch_x - sine[2] * roll_y;
    float yaw_y = sine[2] * pitch_x + cosine[2] * roll_y;
    float view_x = APP_AIRCRAFT_VIEW_COSINE_YAW * yaw_x -
                   APP_AIRCRAFT_VIEW_SINE_YAW * yaw_y;
    float view_y = APP_AIRCRAFT_VIEW_SINE_YAW * yaw_x +
                   APP_AIRCRAFT_VIEW_COSINE_YAW * yaw_y;
    float projected_y = APP_AIRCRAFT_VIEW_COSINE_TILT * view_y +
                        APP_AIRCRAFT_VIEW_SINE_TILT * pitch_z;
    app_aircraft_point_t point;

    point.x = app_aircraft_round(APP_AIRCRAFT_CENTER_X + view_x * APP_AIRCRAFT_SCALE);
    point.y = app_aircraft_round(APP_AIRCRAFT_CENTER_Y - projected_y * APP_AIRCRAFT_SCALE);
    return point;
}

bool app_aircraft_3d_render(const int32_t euler_cdeg[3]) {
    app_aircraft_point_t points[APP_AIRCRAFT_VERTEX_COUNT];
    float sine[3];
    float cosine[3];
    size_t index;

    if (euler_cdeg == NULL) {
        return false;
    }
    for (index = 0U; index < 3U; ++index) {
        float radians = (float)euler_cdeg[index] *
                        (APP_AIRCRAFT_DEG_TO_RAD / 100.0f);

        sine[index] = sinf(radians);
        cosine[index] = cosf(radians);
    }
    for (index = 0U; index < APP_AIRCRAFT_VERTEX_COUNT; ++index) {
        points[index] = app_aircraft_project(
            &app_aircraft_vertices[index], sine, cosine);
    }
    oled_canvas_clear();
    for (index = 0U; index < APP_AIRCRAFT_EDGE_COUNT; ++index) {
        uint8_t first = app_aircraft_edges[index][0];
        uint8_t second = app_aircraft_edges[index][1];

        oled_draw_line(
            points[first].x,
            points[first].y,
            points[second].x,
            points[second].y);
    }
    return oled_present();
}

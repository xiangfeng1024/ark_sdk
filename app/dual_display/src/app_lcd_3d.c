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

#include "app_lcd_3d.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "lcd.h"

#define APP_LCD_Q10_ONE 1024
#define APP_LCD_TILT_DEGREES 20U
#define APP_LCD_VIEWPORT_X 48U
#define APP_LCD_VIEWPORT_Y 88U
#define APP_LCD_VIEWPORT_WIDTH 144U
#define APP_LCD_VIEWPORT_HEIGHT 144U
#define APP_LCD_CENTER_X 120
#define APP_LCD_CENTER_Y 160
#define APP_LCD_SCALE_PIXELS 48
#define APP_LCD_FPS_X 168U
#define APP_LCD_FPS_Y 8U
#define APP_LCD_COLOR_BLACK 0x0000U
#define APP_LCD_COLOR_YELLOW 0xFFE0U
#define APP_LCD_GRADIENT_LEVELS 64U
#define APP_LCD_DEPTH_LIMIT 1664

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} app_lcd_vertex_t;

typedef struct {
    int16_t x;
    int16_t y;
} app_lcd_point_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t end_x;
    int16_t end_y;
    int16_t delta_x;
    int16_t delta_y;
    int8_t step_x;
    int8_t step_y;
    int32_t error;
    int32_t color_q16;
    int32_t color_step_q16;
    bool active;
} app_lcd_line_iterator_t;

enum {
    APP_LCD_CLIP_LEFT = 1U,
    APP_LCD_CLIP_RIGHT = 2U,
    APP_LCD_CLIP_TOP = 4U,
    APP_LCD_CLIP_BOTTOM = 8U,
};

static app_lcd_line_iterator_t app_lcd_line_iterators[12];
static app_lcd_3d_diagnostics_t app_lcd_diagnostics;

static const app_lcd_vertex_t app_lcd_cuboid_vertices[8] = {
    {-1229, -922, -666}, {1229, -922, -666},
    {1229, 922, -666}, {-1229, 922, -666},
    {-1229, -922, 666}, {1229, -922, 666},
    {1229, 922, 666}, {-1229, 922, 666},
};

static const uint8_t app_lcd_cuboid_edges[12][2] = {
    {0U, 1U}, {1U, 2U}, {2U, 3U}, {3U, 0U},
    {4U, 5U}, {5U, 6U}, {6U, 7U}, {7U, 4U},
    {0U, 4U}, {1U, 5U}, {2U, 6U}, {3U, 7U},
};

static const uint16_t app_lcd_depth_gradient[APP_LCD_GRADIENT_LEVELS] = {
    0x001FU, 0x009FU, 0x011FU, 0x019FU, 0x021FU, 0x029FU, 0x031FU, 0x039FU,
    0x041FU, 0x049FU, 0x051FU, 0x059FU, 0x061FU, 0x069FU, 0x071FU, 0x079FU,
    0x07FFU, 0x07FDU, 0x07FBU, 0x07F9U, 0x07F7U, 0x07F5U, 0x07F3U, 0x07F1U,
    0x07EFU, 0x07EDU, 0x07EBU, 0x07E9U, 0x07E7U, 0x07E5U, 0x07E3U, 0x07E1U,
    0x0FE0U, 0x1FE0U, 0x2FE0U, 0x3FE0U, 0x4FE0U, 0x5FE0U, 0x6FE0U, 0x7FE0U,
    0x8FE0U, 0x9FE0U, 0xAFE0U, 0xBFE0U, 0xCFE0U, 0xDFE0U, 0xEFE0U, 0xFFE0U,
    0xFF80U, 0xFF00U, 0xFE80U, 0xFE00U, 0xFD80U, 0xFD00U, 0xFC80U, 0xFC00U,
    0xFB80U, 0xFB00U, 0xFA80U, 0xFA00U, 0xF980U, 0xF900U, 0xF880U, 0xF800U,
};

static const int16_t app_lcd_sine_q10_0_to_90[19] = {
    0, 89, 178, 265, 350, 433, 512, 587, 658, 724,
    784, 839, 887, 928, 962, 989, 1008, 1020, 1024,
};

static int16_t app_lcd_sine_q10(uint16_t angle) {
    angle %= 360U;
    if (angle <= 90U) {
        return app_lcd_sine_q10_0_to_90[angle / 5U];
    }
    if (angle <= 180U) {
        return app_lcd_sine_q10_0_to_90[(180U - angle) / 5U];
    }
    if (angle <= 270U) {
        return (int16_t)-app_lcd_sine_q10_0_to_90[(angle - 180U) / 5U];
    }
    return (int16_t)-app_lcd_sine_q10_0_to_90[(360U - angle) / 5U];
}

static int16_t app_lcd_cosine_q10(uint16_t angle) {
    return app_lcd_sine_q10((uint16_t)((angle + 90U) % 360U));
}

static app_lcd_point_t app_lcd_project_vertex(
    const app_lcd_vertex_t *vertex,
    uint16_t angle,
    int16_t *depth) {
    int32_t sin_x = app_lcd_sine_q10(APP_LCD_TILT_DEGREES);
    int32_t cos_x = app_lcd_cosine_q10(APP_LCD_TILT_DEGREES);
    int32_t sin_y = app_lcd_sine_q10(angle);
    int32_t cos_y = app_lcd_cosine_q10(angle);
    int32_t rotated_x;
    int32_t rotated_y;
    int32_t rotated_z;
    app_lcd_point_t point;

    rotated_x = ((int32_t)vertex->x * cos_y + (int32_t)vertex->z * sin_y) >> 10;
    rotated_z = (-(int32_t)vertex->x * sin_y + (int32_t)vertex->z * cos_y) >> 10;
    rotated_y = ((int32_t)vertex->y * cos_x - rotated_z * sin_x) >> 10;
    *depth = (int16_t)(((int32_t)vertex->y * sin_x + rotated_z * cos_x) >> 10);
    point.x = (int16_t)(APP_LCD_CENTER_X +
                        rotated_x * APP_LCD_SCALE_PIXELS / APP_LCD_Q10_ONE);
    point.y = (int16_t)(APP_LCD_CENTER_Y -
                        rotated_y * APP_LCD_SCALE_PIXELS / APP_LCD_Q10_ONE);
    return point;
}

static uint8_t app_lcd_depth_color_index(int16_t depth) {
    int32_t clamped_depth = depth;

    if (clamped_depth < -APP_LCD_DEPTH_LIMIT) {
        clamped_depth = -APP_LCD_DEPTH_LIMIT;
    } else if (clamped_depth > APP_LCD_DEPTH_LIMIT) {
        clamped_depth = APP_LCD_DEPTH_LIMIT;
    }
    return (uint8_t)((clamped_depth + APP_LCD_DEPTH_LIMIT) *
                     (APP_LCD_GRADIENT_LEVELS - 1U) /
                     (2 * APP_LCD_DEPTH_LIMIT));
}

static uint8_t app_lcd_line_outcode(int32_t x, int32_t y) {
    uint8_t code = 0U;

    if (x < APP_LCD_VIEWPORT_X) {
        code |= APP_LCD_CLIP_LEFT;
    } else if (x >= APP_LCD_VIEWPORT_X + APP_LCD_VIEWPORT_WIDTH) {
        code |= APP_LCD_CLIP_RIGHT;
    }
    if (y < APP_LCD_VIEWPORT_Y) {
        code |= APP_LCD_CLIP_TOP;
    } else if (y >= APP_LCD_VIEWPORT_Y + APP_LCD_VIEWPORT_HEIGHT) {
        code |= APP_LCD_CLIP_BOTTOM;
    }
    return code;
}

static bool app_lcd_clip_line(app_lcd_point_t *first, app_lcd_point_t *second) {
    const int32_t left = APP_LCD_VIEWPORT_X;
    const int32_t right = APP_LCD_VIEWPORT_X + APP_LCD_VIEWPORT_WIDTH - 1;
    const int32_t top = APP_LCD_VIEWPORT_Y;
    const int32_t bottom = APP_LCD_VIEWPORT_Y + APP_LCD_VIEWPORT_HEIGHT - 1;
    int32_t x0 = first->x;
    int32_t y0 = first->y;
    int32_t x1 = second->x;
    int32_t y1 = second->y;
    uint8_t code0 = app_lcd_line_outcode(x0, y0);
    uint8_t code1 = app_lcd_line_outcode(x1, y1);

    for (;;) {
        uint8_t outside;
        int32_t x;
        int32_t y;

        if ((code0 | code1) == 0U) {
            first->x = (int16_t)x0;
            first->y = (int16_t)y0;
            second->x = (int16_t)x1;
            second->y = (int16_t)y1;
            return true;
        }
        if ((code0 & code1) != 0U) {
            return false;
        }
        outside = (code0 != 0U) ? code0 : code1;
        if ((outside & APP_LCD_CLIP_TOP) != 0U) {
            if (y1 == y0) {
                return false;
            }
            x = x0 + (x1 - x0) * (top - y0) / (y1 - y0);
            y = top;
        } else if ((outside & APP_LCD_CLIP_BOTTOM) != 0U) {
            if (y1 == y0) {
                return false;
            }
            x = x0 + (x1 - x0) * (bottom - y0) / (y1 - y0);
            y = bottom;
        } else if ((outside & APP_LCD_CLIP_RIGHT) != 0U) {
            if (x1 == x0) {
                return false;
            }
            y = y0 + (y1 - y0) * (right - x0) / (x1 - x0);
            x = right;
        } else {
            if (x1 == x0) {
                return false;
            }
            y = y0 + (y1 - y0) * (left - x0) / (x1 - x0);
            x = left;
        }
        if (outside == code0) {
            x0 = x;
            y0 = y;
            code0 = app_lcd_line_outcode(x0, y0);
        } else {
            x1 = x;
            y1 = y;
            code1 = app_lcd_line_outcode(x1, y1);
        }
    }
}

static bool app_lcd_line_iterator_init(
    app_lcd_line_iterator_t *iterator,
    app_lcd_point_t first,
    app_lcd_point_t second,
    uint8_t first_color,
    uint8_t second_color) {
    app_lcd_point_t point_swap;
    uint8_t color_swap;
    int32_t total_steps;

    memset(iterator, 0, sizeof(*iterator));
    if (!app_lcd_clip_line(&first, &second)) {
        return false;
    }
    if ((first.y > second.y) ||
        ((first.y == second.y) && (first.x > second.x))) {
        point_swap = first;
        first = second;
        second = point_swap;
        color_swap = first_color;
        first_color = second_color;
        second_color = color_swap;
    }
    iterator->x = first.x;
    iterator->y = first.y;
    iterator->end_x = second.x;
    iterator->end_y = second.y;
    iterator->delta_x = (int16_t)((first.x < second.x) ?
        (second.x - first.x) : (first.x - second.x));
    iterator->step_x = (first.x < second.x) ? 1 : -1;
    iterator->delta_y = (int16_t)((first.y < second.y) ?
        (first.y - second.y) : (second.y - first.y));
    iterator->step_y = (first.y < second.y) ? 1 : -1;
    iterator->error = iterator->delta_x + iterator->delta_y;
    total_steps = (iterator->delta_x > -iterator->delta_y) ?
                  iterator->delta_x : -iterator->delta_y;
    iterator->color_q16 = (int32_t)first_color << 16;
    iterator->color_step_q16 = (total_steps > 0) ?
        (((int32_t)second_color - first_color) * 65536L) / total_steps : 0;
    iterator->active = true;
    return true;
}

static bool app_lcd_render_stripe(lcd_surface_t *surface, void *context) {
    size_t index;
    int32_t stripe_bottom = surface->y + surface->height - 1U;

    (void)context;
    lcd_surface_clear(surface, APP_LCD_COLOR_BLACK);
    for (index = 0U; index < 12U; ++index) {
        app_lcd_line_iterator_t *iterator = &app_lcd_line_iterators[index];

        while (iterator->active && (iterator->y <= stripe_bottom)) {
            int32_t color_index = (iterator->color_q16 + 0x8000) >> 16;
            uint16_t color;
            int32_t doubled_error;

            if (color_index < 0) {
                color_index = 0;
            } else if (color_index >= APP_LCD_GRADIENT_LEVELS) {
                color_index = APP_LCD_GRADIENT_LEVELS - 1U;
            }
            color = app_lcd_depth_gradient[color_index];
            if (iterator->y >= surface->y) {
                lcd_surface_draw_pixel(surface, iterator->x, iterator->y, color);
                lcd_surface_draw_pixel(surface, (int16_t)(iterator->x + 1), iterator->y, color);
            }
            if ((iterator->x == iterator->end_x) &&
                (iterator->y == iterator->end_y)) {
                iterator->active = false;
                break;
            }
            doubled_error = iterator->error * 2;
            if (doubled_error >= iterator->delta_y) {
                iterator->error += iterator->delta_y;
                iterator->x += iterator->step_x;
            }
            if (doubled_error <= iterator->delta_x) {
                iterator->error += iterator->delta_x;
                iterator->y += iterator->step_y;
            }
            iterator->color_q16 += iterator->color_step_q16;
        }
    }
    return true;
}

bool app_lcd_3d_render(uint16_t angle, uint32_t fps) {
    app_lcd_point_t points[8];
    int16_t depths[8];
    uint8_t color_indices[8];
    size_t index;

    for (index = 0U; index < 8U; ++index) {
        points[index] = app_lcd_project_vertex(
            &app_lcd_cuboid_vertices[index], angle, &depths[index]);
        color_indices[index] = app_lcd_depth_color_index(depths[index]);
    }
    for (index = 0U; index < 12U; ++index) {
        app_lcd_line_iterator_init(
            &app_lcd_line_iterators[index],
            points[app_lcd_cuboid_edges[index][0]],
            points[app_lcd_cuboid_edges[index][1]],
            color_indices[app_lcd_cuboid_edges[index][0]],
            color_indices[app_lcd_cuboid_edges[index][1]]);
    }
    if (!lcd_refresh_region(
            APP_LCD_VIEWPORT_X, APP_LCD_VIEWPORT_Y,
            APP_LCD_VIEWPORT_WIDTH, APP_LCD_VIEWPORT_HEIGHT,
            app_lcd_render_stripe, NULL) ||
        (lcd_printf(
            APP_LCD_FPS_X, APP_LCD_FPS_Y, 2U,
            APP_LCD_COLOR_YELLOW, APP_LCD_COLOR_BLACK,
            "FPS:%2lu", (unsigned long)fps) < 0)) {
        return false;
    }

    taskENTER_CRITICAL();
    ++app_lcd_diagnostics.frame_sequence;
    app_lcd_diagnostics.last_angle = angle;
    app_lcd_diagnostics.last_fps = fps;
    for (index = 0U; index < 8U; ++index) {
        app_lcd_diagnostics.vertices[index][0] = points[index].x;
        app_lcd_diagnostics.vertices[index][1] = points[index].y;
    }
    taskEXIT_CRITICAL();
    return true;
}

bool app_lcd_3d_get_diagnostics(app_lcd_3d_diagnostics_t *diagnostics) {
    if (diagnostics == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    *diagnostics = app_lcd_diagnostics;
    taskEXIT_CRITICAL();
    return true;
}

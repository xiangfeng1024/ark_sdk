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

#include "app_main.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "app_sys_config.h"
#include "led.h"
#include "oled.h"
#include "stream.h"
#include "uart_cli.h"
#include "watchdog.h"
#include "ark_component.h"

#define APP_LED_TASK_STACK_DEPTH 128U
#define APP_CUBE_TASK_STACK_DEPTH 384U
#define APP_CUBE_FRAME_PERIOD_MS 33U
#define APP_CUBE_FPS_WINDOW_MS 500U
#define APP_CUBE_ANGLE_STEP_DEGREES 5U
#define APP_CUBE_INITIAL_ANGLE_DEGREES 25U
#define APP_CUBE_TILT_DEGREES 20U
#define APP_CUBE_Q10_ONE 1024
#define APP_CUBE_ORTHOGRAPHIC_SCALE_PIXELS 20
#define APP_CUBE_CENTER_X 50
#define APP_CUBE_CENTER_Y 35
#define APP_CUBE_FPS_X 80U

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} app_cube_vertex_t;

typedef struct {
    int16_t x;
    int16_t y;
} app_screen_point_t;

static const app_cube_vertex_t app_cube_vertices[8] = {
    {-APP_CUBE_Q10_ONE, -APP_CUBE_Q10_ONE, -APP_CUBE_Q10_ONE},
    { APP_CUBE_Q10_ONE, -APP_CUBE_Q10_ONE, -APP_CUBE_Q10_ONE},
    { APP_CUBE_Q10_ONE,  APP_CUBE_Q10_ONE, -APP_CUBE_Q10_ONE},
    {-APP_CUBE_Q10_ONE,  APP_CUBE_Q10_ONE, -APP_CUBE_Q10_ONE},
    {-APP_CUBE_Q10_ONE, -APP_CUBE_Q10_ONE,  APP_CUBE_Q10_ONE},
    { APP_CUBE_Q10_ONE, -APP_CUBE_Q10_ONE,  APP_CUBE_Q10_ONE},
    { APP_CUBE_Q10_ONE,  APP_CUBE_Q10_ONE,  APP_CUBE_Q10_ONE},
    {-APP_CUBE_Q10_ONE,  APP_CUBE_Q10_ONE,  APP_CUBE_Q10_ONE},
};

static const uint8_t app_cube_edges[12][2] = {
    {0U, 1U}, {1U, 2U}, {2U, 3U}, {3U, 0U},
    {4U, 5U}, {5U, 6U}, {6U, 7U}, {7U, 4U},
    {0U, 4U}, {1U, 5U}, {2U, 6U}, {3U, 7U},
};

static const int16_t app_sine_q10_0_to_90[19] = {
    0, 89, 178, 265, 350, 433, 512, 587, 658, 724,
    784, 839, 887, 928, 962, 989, 1008, 1020, 1024,
};

static int16_t app_sine_q10(uint16_t angle) {
    angle %= 360U;
    if (angle <= 90U) {
        return app_sine_q10_0_to_90[angle / 5U];
    }
    if (angle <= 180U) {
        return app_sine_q10_0_to_90[(180U - angle) / 5U];
    }
    if (angle <= 270U) {
        return (int16_t)-app_sine_q10_0_to_90[(angle - 180U) / 5U];
    }
    return (int16_t)-app_sine_q10_0_to_90[(360U - angle) / 5U];
}

static int16_t app_cosine_q10(uint16_t angle) {
    return app_sine_q10((uint16_t)((angle + 90U) % 360U));
}

static app_screen_point_t app_project_vertex(
    const app_cube_vertex_t *vertex,
    uint16_t angle_x,
    uint16_t angle_y) {
    int32_t sin_x = app_sine_q10(angle_x);
    int32_t cos_x = app_cosine_q10(angle_x);
    int32_t sin_y = app_sine_q10(angle_y);
    int32_t cos_y = app_cosine_q10(angle_y);
    int32_t rotated_x;
    int32_t rotated_y;
    int32_t rotated_z_y;
    app_screen_point_t point;

    rotated_x = ((int32_t)vertex->x * cos_y + (int32_t)vertex->z * sin_y) >> 10;
    rotated_z_y = (-(int32_t)vertex->x * sin_y + (int32_t)vertex->z * cos_y) >> 10;
    rotated_y = ((int32_t)vertex->y * cos_x - rotated_z_y * sin_x) >> 10;
    point.x = (int16_t)(APP_CUBE_CENTER_X +
                        rotated_x * APP_CUBE_ORTHOGRAPHIC_SCALE_PIXELS /
                            APP_CUBE_Q10_ONE);
    point.y = (int16_t)(APP_CUBE_CENTER_Y -
                        rotated_y * APP_CUBE_ORTHOGRAPHIC_SCALE_PIXELS /
                            APP_CUBE_Q10_ONE);
    return point;
}

static bool app_render_cube(uint16_t angle, uint32_t fps, bool fps_valid) {
    app_screen_point_t points[8];
    size_t index;

    oled_canvas_clear();
    for (index = 0U; index < 8U; ++index) {
        points[index] = app_project_vertex(
            &app_cube_vertices[index],
            APP_CUBE_TILT_DEGREES,
            angle);
    }
    for (index = 0U; index < 12U; ++index) {
        uint8_t first = app_cube_edges[index][0];
        uint8_t second = app_cube_edges[index][1];

        oled_draw_line(
            points[first].x,
            points[first].y,
            points[second].x,
            points[second].y);
    }
    if (fps_valid) {
        oled_draw_printf(APP_CUBE_FPS_X, 0U, 8U, "FPS:%2lu", (unsigned long)fps);
    } else {
        oled_draw_printf(APP_CUBE_FPS_X, 0U, 8U, "FPS:--");
    }
    return oled_present();
}

static void app_led_task(void *argument) {
    TickType_t next_wakeup = xTaskGetTickCount();

    (void)argument;
    for (;;) {
        vTaskDelayUntil(&next_wakeup, pdMS_TO_TICKS(1000U));
        led_toggle_by_id(ARK_APP_STATUS_LED_ID);
    }
}

static void app_cube_task(void *argument) {
    TickType_t next_wakeup = xTaskGetTickCount();
    TickType_t fps_window_started = next_wakeup;
    uint32_t presented_frames = 0U;
    uint32_t measured_fps = 0U;
    uint32_t present_failures = 0U;
    uint16_t angle = APP_CUBE_INITIAL_ANGLE_DEGREES;
    bool fps_valid = false;

    (void)argument;
    printf("[cube3d] render task started\r\n");

    for (;;) {
        TickType_t now;
        TickType_t elapsed;

        if (app_render_cube(angle, measured_fps, fps_valid)) {
            ++presented_frames;
        } else {
            ++present_failures;
        }
        angle = (uint16_t)((angle + APP_CUBE_ANGLE_STEP_DEGREES) % 360U);
        now = xTaskGetTickCount();
        elapsed = now - fps_window_started;
        if (elapsed >= pdMS_TO_TICKS(APP_CUBE_FPS_WINDOW_MS)) {
            measured_fps = presented_frames * configTICK_RATE_HZ / (uint32_t)elapsed;
            fps_valid = true;
            if (present_failures != 0U) {
                printf(
                    "[cube3d] OLED present failed %lu time(s)\r\n",
                    (unsigned long)present_failures);
            }
            presented_frames = 0U;
            present_failures = 0U;
            fps_window_started = now;
        }
        vTaskDelayUntil(&next_wakeup, pdMS_TO_TICKS(APP_CUBE_FRAME_PERIOD_MS));
    }
}

static void app_self_test_report(
    const ark_component_t *component,
    ark_component_result_t result,
    uint32_t elapsed_ms) {
    const char *status = (result == ARK_COMPONENT_OK) ? "PASS" :
                         (result == ARK_COMPONENT_TIMEOUT) ? "TIMEOUT" : "FAIL";

    printf(
        "[self-test] %-12s %-7s %lu ms\r\n",
        component->name,
        status,
        (unsigned long)elapsed_ms);
}

void components_init(void) {
    watchdog_register();
    stream_register();
    uart_cli_register();
    led_register();
    oled_register();

    if (ark_component_init_all(NULL) != ARK_COMPONENT_OK) {
        printf("[app] component initialization failed\r\n");
    }
    ark_component_self_test_all(app_self_test_report, NULL);
}

void appStartTask(void *argument) {
    BaseType_t led_task_result;
    BaseType_t cube_task_result;

    (void)argument;
    printf("[app] cube3d selected\r\n");
    components_init();
    taskENTER_CRITICAL();
    led_task_result = xTaskCreate(
        app_led_task,
        "appLed",
        APP_LED_TASK_STACK_DEPTH,
        NULL,
        tskIDLE_PRIORITY + 1U,
        NULL);
    cube_task_result = xTaskCreate(
        app_cube_task,
        "appCube",
        APP_CUBE_TASK_STACK_DEPTH,
        NULL,
        tskIDLE_PRIORITY + 1U,
        NULL);
    taskEXIT_CRITICAL();

    printf(
        "[app] tasks led=%ld cube=%ld free_heap=%lu\r\n",
        (long)led_task_result,
        (long)cube_task_result,
        (unsigned long)xPortGetFreeHeapSize());

    if (led_task_result != pdPASS) {
        printf("[app] appLed create failed\r\n");
    }
    if (cube_task_result != pdPASS) {
        printf("[app] appCube create failed\r\n");
    }
    vTaskDelete(NULL);
}

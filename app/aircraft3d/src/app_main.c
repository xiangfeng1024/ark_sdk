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
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "app_aircraft_3d.h"
#include "app_sys_config.h"
#include "icm20602.h"
#include "led.h"
#include "oled.h"
#include "stream.h"
#include "uart_cli.h"
#include "watchdog.h"
#include "ark_cli.h"
#include "ark_component.h"

#define APP_LED_TASK_STACK_DEPTH 128U
#define APP_AIRCRAFT_TASK_STACK_DEPTH 512U
#define APP_DISPLAY_FPS 30U

static TaskHandle_t app_aircraft_task_handle;
static uint32_t app_display_fps;
static uint32_t app_present_failures;

static void app_advance_frame_deadline(TickType_t *deadline, uint32_t *remainder) {
    *deadline += configTICK_RATE_HZ / APP_DISPLAY_FPS;
    *remainder += configTICK_RATE_HZ % APP_DISPLAY_FPS;
    if (*remainder >= APP_DISPLAY_FPS) {
        ++(*deadline);
        *remainder -= APP_DISPLAY_FPS;
    }
}

static void app_led_task(void *argument) {
    TickType_t next_wakeup = xTaskGetTickCount();

    (void)argument;
    for (;;) {
        vTaskDelayUntil(&next_wakeup, pdMS_TO_TICKS(1000U));
        led_toggle_by_id(ARK_APP_STATUS_LED_ID);
    }
}

static void app_aircraft_task(void *argument) {
    TickType_t next_frame = xTaskGetTickCount();
    TickType_t fps_window = next_frame;
    uint32_t frame_remainder = 0U;
    uint32_t frames = 0U;

    (void)argument;
    app_display_fps = 0U;
    app_present_failures = 0U;
    for (;;) {
        icm20602_motion_t motion;
        TickType_t now;

        now = xTaskGetTickCount();
        if ((int32_t)(now - next_frame) >= 0) {
            icm20602_get_motion(&motion);
            if (app_aircraft_3d_render(motion.euler_cdeg)) {
                ++frames;
            } else {
                ++app_present_failures;
            }
            do {
                app_advance_frame_deadline(&next_frame, &frame_remainder);
            } while ((int32_t)(now - next_frame) >= 0);
        }
        if ((now - fps_window) >= configTICK_RATE_HZ) {
            uint32_t elapsed_ticks = (uint32_t)(now - fps_window);

            app_display_fps =
                (frames * configTICK_RATE_HZ + elapsed_ticks / 2U) / elapsed_ticks;
            frames = 0U;
            fps_window = now;
        }
    }
}

static void app_self_test_report(
    const ark_component_t *component,
    ark_component_result_t result,
    uint32_t elapsed_ms) {
    const char *status = (result == ARK_COMPONENT_OK) ? "PASS" :
                         (result == ARK_COMPONENT_TIMEOUT) ? "TIMEOUT" : "FAIL";

    printf("[self-test] %-12s %-7s %lu ms\r\n",
           component->name, status, (unsigned long)elapsed_ms);
}

static int app_aircraft_cli(int argc, char *argv[]) {
    icm20602_motion_t motion;
    icm20602_diagnostics_t diagnostics;

    if (argc != 1) {
        printf("usage: %s\r\n", argv[0]);
        return -1;
    }
    icm20602_get_motion(&motion);
    icm20602_get_diagnostics(&diagnostics);
    printf("AIRCRAFT fps=%lu failures=%lu frames=%lu irq=%lu read_fail=%lu recover=%lu\r\n",
           (unsigned long)app_display_fps,
           (unsigned long)app_present_failures,
           (unsigned long)motion.update_count,
           (unsigned long)diagnostics.irq_count,
           (unsigned long)diagnostics.read_failures,
           (unsigned long)diagnostics.bus_recoveries);
    printf("  euler_cdeg=%ld,%ld,%ld stationary=%u heap=%lu stack_hwm_words=%u\r\n",
           (long)motion.euler_cdeg[0],
           (long)motion.euler_cdeg[1],
           (long)motion.euler_cdeg[2],
           motion.stationary ? 1U : 0U,
           (unsigned long)xPortGetFreeHeapSize(),
           (unsigned int)((app_aircraft_task_handle != NULL) ?
               uxTaskGetStackHighWaterMark(app_aircraft_task_handle) : 0U));
    return 0;
}

static ark_cli_command_t app_aircraft_command = {
    .name = "aircraft",
    .usage = "aircraft",
    .description = "show aircraft renderer and IMU diagnostics",
    .handler = app_aircraft_cli,
};

void components_init(void) {
    watchdog_register();
    stream_register();
    uart_cli_register();
    (void)ark_cli_register(&app_aircraft_command);
    led_register();
    oled_register();
    icm20602_register();

    if (ark_component_init_all(NULL) != ARK_COMPONENT_OK) {
        printf("[aircraft3d] component initialization failed\r\n");
    }
    ark_component_self_test_all(app_self_test_report, NULL);
}

void appStartTask(void *argument) {
    BaseType_t led_result;
    BaseType_t aircraft_result;

    (void)argument;
    components_init();
    printf("[aircraft3d] app selected\r\n");
    taskENTER_CRITICAL();
    led_result = xTaskCreate(
        app_led_task, "appLed", APP_LED_TASK_STACK_DEPTH,
        NULL, tskIDLE_PRIORITY + 1U, NULL);
    aircraft_result = xTaskCreate(
        app_aircraft_task, "appAircraft", APP_AIRCRAFT_TASK_STACK_DEPTH,
        NULL, tskIDLE_PRIORITY + 1U, &app_aircraft_task_handle);
    taskEXIT_CRITICAL();
    printf("[aircraft3d] tasks led=%ld aircraft=%ld free_heap=%lu\r\n",
           (long)led_result,
           (long)aircraft_result,
           (unsigned long)xPortGetFreeHeapSize());
    if ((led_result != pdPASS) || (aircraft_result != pdPASS)) {
        printf("[aircraft3d] one or more task creations failed\r\n");
    }
    vTaskDelete(NULL);
}

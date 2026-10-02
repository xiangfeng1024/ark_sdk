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
#include "app_component_boot.h"
#include "app_sys_config.h"
#include "flash.h"
#include "flash_w25q16.h"
#include "icm20602.h"
#include "led.h"
#include "motor.h"
#include "motor_can.h"
#include "oled.h"
#include "stream.h"
#include "uart_cli.h"
#include "watchdog.h"
#include "ws2812b.h"

#define APP_LED_TASK_STACK_DEPTH 128U
#define APP_IMU_DISPLAY_ENABLED 0U
#define APP_IMU_DISPLAY_TASK_STACK_DEPTH 320U

static TaskHandle_t app_led_task_handle;
#if APP_IMU_DISPLAY_ENABLED
static TaskHandle_t app_motion_task_handle;
#endif

static void app_led_task(void *argument) {
    TickType_t next_wakeup = xTaskGetTickCount();

    (void)argument;
    for (;;) {
        vTaskDelayUntil(&next_wakeup, pdMS_TO_TICKS(1000U));
        led_toggle_by_id(ARK_APP_STATUS_LED_ID);
    }
}

#if APP_IMU_DISPLAY_ENABLED
static void app_motion_task(void *argument) {
    TickType_t next_frame = xTaskGetTickCount();

    (void)argument;
    for (;;) {
        icm20602_motion_t motion;
        static const uint8_t pages[3] = {1U, 3U, 5U};
        static const char *const names[3] = {"Roll", "Pitch", "Yaw"};
        size_t axis;

        if (icm20602_get_motion(&motion)) {
            oled_canvas_clear();
            for (axis = 0U; axis < 3U; ++axis) {
                int32_t angle = motion.euler_cdeg[axis];
                int32_t magnitude = (angle < 0L) ? -angle : angle;

                oled_draw_printf(
                    8U, pages[axis], 16U, "%-5s:%c%3ld.%02ld",
                    names[axis], (angle < 0L) ? '-' : '+',
                    (long)(magnitude / 100L), (long)(magnitude % 100L));
            }
            oled_present();
        }
        vTaskDelayUntil(&next_frame, pdMS_TO_TICKS(33U));
    }
}
#endif

void components_init(void) {
    oled_register();
    watchdog_register();
    stream_register();
    uart_cli_register();
    led_register();
    flash_register();
    flash_w25q16_register();
    ws2812b_register();
    motor_register();
    motor_can_register();
    icm20602_register();
}

void appStartTask(void *argument) {
    BaseType_t led_result;
#if APP_IMU_DISPLAY_ENABLED
    BaseType_t motion_result;
#endif

    (void)argument;
    components_init();
    app_component_boot_run();
    printf("[microcar] app selected\r\n");
    {
        icm20602_diagnostics_t diagnostics;

        if (icm20602_get_diagnostics(&diagnostics) &&
            (diagnostics.who_am_i != 0x12U)) {
            printf("[microcar] warning: compatible IMU WHO_AM_I=0x%02X, official ICM20602=0x12\r\n",
                   diagnostics.who_am_i);
        }
    }
    taskENTER_CRITICAL();
    led_result = xTaskCreate(
        app_led_task, "appLed", APP_LED_TASK_STACK_DEPTH,
        NULL, tskIDLE_PRIORITY + 1U, &app_led_task_handle);
#if APP_IMU_DISPLAY_ENABLED
    motion_result = xTaskCreate(
        app_motion_task, "appMotion", APP_IMU_DISPLAY_TASK_STACK_DEPTH,
        NULL, tskIDLE_PRIORITY + 1U, &app_motion_task_handle);
#endif
    taskEXIT_CRITICAL();
    printf("[microcar] tasks led=%ld free_heap=%lu\r\n",
           (long)led_result,
           (unsigned long)xPortGetFreeHeapSize());
    if (led_result != pdPASS) {
        printf("[microcar] task creation failed\r\n");
    }
#if APP_IMU_DISPLAY_ENABLED
    if (motion_result != pdPASS) {
        printf("[microcar] IMU display task creation failed\r\n");
    }
#endif
    vTaskDelete(NULL);
}

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

#include "FreeRTOS.h"
#include "task.h"
#include "dht11.h"
#include "lcd.h"
#include "ark_net.h"
#include "uart_cli.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_dts_generated.h"

#define APP_TELEMETRY_TASK_STACK_WORDS 384U
#define APP_DISPLAY_PERIOD_MS 500U
#define APP_SENSOR_PERIOD_MS 2000U

static uint32_t app_display_period_ms = APP_DISPLAY_PERIOD_MS;

static void app_telemetry_task(void *argument) {
    TickType_t previous_tick = xTaskGetTickCount();
    TickType_t next_sensor_tick = previous_tick;
    dht11_sample_t sample;
    ark_net_command_t command;
    ark_net_telemetry_t telemetry;

    (void)argument;
    for (;;) {
        TickType_t now = xTaskGetTickCount();

        if ((uint32_t)((now - next_sensor_tick) * portTICK_PERIOD_MS) >=
            APP_SENSOR_PERIOD_MS) {
            next_sensor_tick = now;
            if (dht11_read(&sample)) {
                ark_net_set_telemetry(
                    (int32_t)sample.temperature_deci_c,
                    (int32_t)sample.humidity_deci_percent);
            }
        }
        if (ark_net_get_command(&command)) {
            lcd_clear(0x0000U);
            lcd_printf(12U, 32U, 3U, 0x7D7CU, 0x0000U, "ARK NET");
            if (ark_net_get_telemetry(&telemetry)) {
                lcd_printf(12U, 92U, 2U, 0xFFFFU, 0x0000U,
                           "TEMP %ld.%ldC",
                           (long)(telemetry.temperature_x10 / 10),
                           (long)(telemetry.temperature_x10 % 10));
                lcd_printf(12U, 124U, 2U, 0xFFFFU, 0x0000U,
                           "HUMI %ld.%ld%%",
                           (long)(telemetry.humidity_x10 / 10),
                           (long)(telemetry.humidity_x10 % 10));
            } else {
                lcd_printf(12U, 92U, 2U, 0xFFE0U, 0x0000U, "DHT11 WAITING");
            }
            lcd_printf(12U, 172U, 2U, command.led_on ? 0x07E0U : 0xF800U,
                       0x0000U, "LED %s", command.led_on ? "ON" : "OFF");
            lcd_printf(12U, 208U, 1U, 0x07FFU, 0x0000U, "REV %lu",
                       (unsigned long)command.revision);
        }
        vTaskDelayUntil(&previous_tick, pdMS_TO_TICKS(app_display_period_ms));
    }
}

bool components_init(void) {
    return ark_dts_register_components();
}

static void app_load_config(void) {
    const ark_of_node_t *node = ark_of_find_node_by_path("/software/display");

    if (node != NULL) {
        ark_of_property_read_u32(node, "ark,refresh-period-ms", &app_display_period_ms);
    }
    if (app_display_period_ms == 0U) {
        app_display_period_ms = APP_DISPLAY_PERIOD_MS;
    }
}

void appStartTask(void *argument) {
    (void)argument;
    app_load_config();
    if (!components_init()) {
        vTaskDelete(NULL);
        return;
    }

    /* Keep the generated startup task within its fixed CubeMX stack budget. */
    ark_component_init_all(NULL);
    uart_cli_start();

    taskENTER_CRITICAL();
    xTaskCreate(app_telemetry_task, "netApp", APP_TELEMETRY_TASK_STACK_WORDS,
                NULL, tskIDLE_PRIORITY + 1U, NULL);
    taskEXIT_CRITICAL();
    vTaskDelete(NULL);
}

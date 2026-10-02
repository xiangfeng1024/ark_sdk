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
#include "adc38_tracking.h"
#include "adc_sensor.h"
#include "app_component_boot.h"
#include "app_dts_config.h"
#include "app_line_follow.h"
#include "app_soil_probe.h"
#include "bh1750.h"
#include "buzzer.h"
#include "control.h"
#include "dht11.h"
#include "encoder.h"
#include "oled.h"
#include "servo.h"
#include "beidou.h"
#include "bluetooth.h"
#include "maixcam.h"
#include "ws2812b.h"
#include "ark_dts_generated.h"

typedef struct {
    dht11_sample_t dht11;
    adc_sensor_sample_t mq7;
    adc_sensor_sample_t mq135;
    adc_sensor_sample_t soil;
    bh1750_sample_t light;
    bool dht11_ok;
    bool mq7_ok;
    bool mq135_ok;
    bool soil_ok;
    bool soil_inserted;
    bool light_ok;
} app_sensor_snapshot_t;

static TaskHandle_t app_control_task_handle;
static TaskHandle_t app_sensor_task_handle;
static TaskHandle_t app_oled_task_handle;
static TaskHandle_t app_business_task_handle;
static app_sensor_snapshot_t app_sensor_snapshot;
static uint8_t app_light_level = UINT8_MAX;

static uint16_t app_adc_raw_percent(uint16_t raw) {
    return (uint16_t)(((uint32_t)raw * 100U + 2047U) / 4095U);
}

static uint8_t app_light_to_level(uint32_t lux_x10) {
    uint32_t lux = (lux_x10 + 5U) / 10U;
    uint32_t ambient_level;

    if (lux >= APP_LIGHT_MAX_LUX) {
        return 0U;
    }
    ambient_level = (lux * 8U) / APP_LIGHT_MAX_LUX;
    if (ambient_level >= APP_WS2812B_MAX_LEVEL) {
        return 0U;
    }
    return (uint8_t)(APP_WS2812B_MAX_LEVEL - ambient_level);
}

static void app_update_headlight(const bh1750_sample_t *light) {
    uint8_t level = app_light_to_level(light->lux_x10);
    uint8_t white;

    if (level == app_light_level) {
        return;
    }
    white = (uint8_t)(((uint16_t)level * APP_WS2812B_MAX_WHITE) /
                      APP_WS2812B_MAX_LEVEL);
    if (ws2812b_fill(white, white, white)) {
        app_light_level = level;
    }
}

static void app_sensor_publish(const app_sensor_snapshot_t *snapshot) {
    taskENTER_CRITICAL();
    app_sensor_snapshot = *snapshot;
    taskEXIT_CRITICAL();
}

static void app_sensor_get(app_sensor_snapshot_t *snapshot) {
    taskENTER_CRITICAL();
    *snapshot = app_sensor_snapshot;
    taskEXIT_CRITICAL();
}

static void app_publish_bluetooth(
    const app_sensor_snapshot_t *sensor,
    const app_line_follow_snapshot_t *line) {
    beidou_position_t position = {0};
    bluetooth_packet_builder_t packet;

    (void)beidou_get_position(&position);
    bluetooth_packet_reset(&packet);
    if (!bluetooth_packet_add_uint8(&packet, (uint8_t)line->corner_count) ||
        !bluetooth_packet_add_float(
            &packet,
            sensor->dht11_ok ?
                (float)sensor->dht11.temperature_deci_c * 0.1f : 0.0f) ||
        !bluetooth_packet_add_float(
            &packet,
            sensor->dht11_ok ?
                (float)sensor->dht11.humidity_deci_percent * 0.1f : 0.0f) ||
        !bluetooth_packet_add_float(
            &packet,
            sensor->mq7_ok ?
                (float)app_adc_raw_percent(sensor->mq7.raw) : 0.0f) ||
        !bluetooth_packet_add_float(
            &packet,
            sensor->mq135_ok ?
                (float)app_adc_raw_percent(sensor->mq135.raw) : 0.0f) ||
        !bluetooth_packet_add_float(
            &packet,
            line->soil_sample_valid ?
                (float)app_adc_raw_percent(line->soil_raw) :
                (sensor->soil_ok && sensor->soil_inserted) ?
                (float)app_adc_raw_percent(sensor->soil.raw) : 0.0f) ||
        !bluetooth_packet_add_float(
            &packet,
            sensor->light_ok ?
                (float)sensor->light.lux_x10 * 0.1f : 0.0f) ||
        !bluetooth_packet_add_float(
            &packet,
            position.valid ?
                (float)position.latitude_e7 * 0.0000001f : 0.0f) ||
        !bluetooth_packet_add_float(
            &packet,
            position.valid ?
                (float)position.longitude_e7 * 0.0000001f : 0.0f)) {
        return;
    }
    (void)bluetooth_packet_send(&packet);
}

static void app_draw_label2(
    uint8_t page,
    oled_zh_glyph_t first,
    oled_zh_glyph_t second) {
    oled_draw_zh16(0U, page, first);
    oled_draw_zh16(16U, page, second);
    oled_draw_printf(32U, page, 16U, ":");
}

static void app_draw_label4(
    uint8_t page,
    oled_zh_glyph_t first,
    oled_zh_glyph_t second,
    oled_zh_glyph_t third,
    oled_zh_glyph_t fourth) {
    oled_draw_zh16(0U, page, first);
    oled_draw_zh16(16U, page, second);
    oled_draw_zh16(32U, page, third);
    oled_draw_zh16(48U, page, fourth);
    oled_draw_printf(64U, page, 16U, ":");
}

static void app_draw_camera(const app_line_follow_snapshot_t *line) {
    if (line->vision_enabled && line->flower_detected) {
        oled_draw_printf(0U, 6U, 16U, "X:%3u Y:%3u", line->camera_x, line->camera_y);
    } else if (line->vision_enabled) {
        oled_draw_printf(0U, 6U, 16U, "X: -- Y: --");
    }
}

static void app_draw_environment_page(
    const app_sensor_snapshot_t *sensor,
    const app_line_follow_snapshot_t *line) {
    app_draw_label2(0U, OLED_ZH_WEN, OLED_ZH_DU);
    if (sensor->dht11_ok) {
        uint16_t magnitude = (uint16_t)((sensor->dht11.temperature_deci_c < 0) ?
            -sensor->dht11.temperature_deci_c : sensor->dht11.temperature_deci_c);

        oled_draw_printf(
            48U, 0U, 16U, "%c%2u.%u",
            (sensor->dht11.temperature_deci_c < 0) ? '-' : ' ',
            magnitude / 10U,
            magnitude % 10U);
    } else {
        oled_draw_printf(48U, 0U, 16U, "   --");
    }
    oled_draw_zh16(104U, 0U, OLED_ZH_CELSIUS);

    app_draw_label2(2U, OLED_ZH_SHI, OLED_ZH_DU);
    if (sensor->dht11_ok) {
        oled_draw_printf(
            48U, 2U, 16U, "%3u.%u",
            sensor->dht11.humidity_deci_percent / 10U,
            sensor->dht11.humidity_deci_percent % 10U);
    } else {
        oled_draw_printf(48U, 2U, 16U, "   --");
    }
    oled_draw_printf(112U, 2U, 16U, "%%");

    app_draw_label2(4U, OLED_ZH_GUANG, OLED_ZH_ZHAO);
    if (sensor->light_ok) {
        oled_draw_printf(
            48U, 4U, 16U, "%5lu",
            (unsigned long)((sensor->light.lux_x10 + 5U) / 10U));
    } else {
        oled_draw_printf(48U, 4U, 16U, "   --");
    }
    oled_draw_printf(104U, 4U, 16U, "lx");
    app_draw_camera(line);
}

static void app_draw_sensor_page(
    const app_sensor_snapshot_t *sensor,
    const app_line_follow_snapshot_t *line) {
    app_draw_label4(0U, OLED_ZH_YI, OLED_ZH_YANG, OLED_ZH_HUA, OLED_ZH_TAN);
    if (sensor->mq7_ok) {
        oled_draw_printf(80U, 0U, 16U, "%3u", app_adc_raw_percent(sensor->mq7.raw));
    } else {
        oled_draw_printf(80U, 0U, 16U, " --");
    }
    oled_draw_printf(112U, 0U, 16U, "%%");

    app_draw_label4(2U, OLED_ZH_KONG, OLED_ZH_QI, OLED_ZH_ZHI, OLED_ZH_LIANG);
    if (sensor->mq135_ok) {
        oled_draw_printf(80U, 2U, 16U, "%3u", app_adc_raw_percent(sensor->mq135.raw));
    } else {
        oled_draw_printf(80U, 2U, 16U, " --");
    }
    oled_draw_printf(112U, 2U, 16U, "%%");

    app_draw_label4(4U, OLED_ZH_TU, OLED_ZH_RANG, OLED_ZH_SHI, OLED_ZH_DU);
    if (line->soil_sample_valid) {
        oled_draw_printf(80U, 4U, 16U, "%3u", app_adc_raw_percent(line->soil_raw));
    } else if (sensor->soil_ok && sensor->soil_inserted) {
        oled_draw_printf(80U, 4U, 16U, "%3u", app_adc_raw_percent(sensor->soil.raw));
    } else {
        oled_draw_printf(80U, 4U, 16U, " --");
    }
    oled_draw_printf(112U, 4U, 16U, "%%");
    app_draw_camera(line);
}

static void app_draw_snapshot(
    const app_sensor_snapshot_t *sensor,
    const app_line_follow_snapshot_t *line) {
    oled_canvas_clear();
    if (line->state == APP_LINE_STATE_FLOWER_DONE) {
        if (line->completion_blink_on) {
            oled_draw_zh16(48U, 3U, OLED_ZH_WAN);
            oled_draw_zh16(64U, 3U, OLED_ZH_CHENG);
        }
        oled_present();
        return;
    }
    if (((xTaskGetTickCount() / pdMS_TO_TICKS(APP_OLED_PAGE_PERIOD_MS)) & 1U) == 0U) {
        app_draw_environment_page(sensor, line);
    } else {
        app_draw_sensor_page(sensor, line);
    }
    oled_present();
}

static void app_control_task(void *argument) {
    TickType_t next_wakeup = xTaskGetTickCount();

    (void)argument;
    for (;;) {
        (void)control_update();
        vTaskDelayUntil(&next_wakeup, pdMS_TO_TICKS(APP_CONTROL_PERIOD_MS));
    }
}

static void app_sensor_task(void *argument) {
    TickType_t next_wakeup = xTaskGetTickCount();
    TickType_t last_bluetooth_tick = next_wakeup;

    (void)argument;
    for (;;) {
        app_sensor_snapshot_t next = {0};

        next.dht11_ok = dht11_read(&next.dht11);
        next.mq7_ok = adc_sensor_read(ADC_SENSOR_MQ7, &next.mq7);
        next.mq135_ok = adc_sensor_read(ADC_SENSOR_MQ135, &next.mq135);
        next.soil_ok = adc_sensor_read(ADC_SENSOR_SOIL, &next.soil);
        if (next.soil_ok) {
            app_soil_probe_observe_idle(next.soil.raw);
            next.soil_inserted =
                app_soil_probe_sample_is_inserted(next.soil.raw);
        }
        next.light_ok = bh1750_read(&next.light);
        if (next.light_ok) {
            app_update_headlight(&next.light);
        }
        app_sensor_publish(&next);
        if ((xTaskGetTickCount() - last_bluetooth_tick) >=
            pdMS_TO_TICKS(APP_BLUETOOTH_PERIOD_MS)) {
            app_line_follow_snapshot_t line;

            if (app_line_follow_get_snapshot(&line)) {
                app_publish_bluetooth(&next, &line);
            }
            last_bluetooth_tick = xTaskGetTickCount();
        }
        vTaskDelayUntil(&next_wakeup, pdMS_TO_TICKS(APP_SENSOR_PERIOD_MS));
    }
}

static void app_oled_task(void *argument) {
    TickType_t next_wakeup = xTaskGetTickCount();

    (void)argument;
    for (;;) {
        app_line_follow_snapshot_t snapshot;
        app_sensor_snapshot_t sensors;

        if (app_line_follow_get_snapshot(&snapshot)) {
            app_sensor_get(&sensors);
            app_draw_snapshot(&sensors, &snapshot);
        }
        vTaskDelayUntil(&next_wakeup, pdMS_TO_TICKS(APP_OLED_PERIOD_MS));
    }
}

static void app_business_task(void *argument) {
    TickType_t next_wakeup = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(APP_BUSINESS_PERIOD_MS);

    (void)argument;
    for (;;) {
        TickType_t now;

        app_line_follow_step();
        now = xTaskGetTickCount();
        if ((now - next_wakeup) >= period) {
            next_wakeup = now;
        }
        vTaskDelayUntil(&next_wakeup, period);
    }
}

bool components_init(void) {
    return ark_dts_register_components();
}

void appStartTask(void *argument) {
    BaseType_t result;

    (void)argument;
    if (!app_dts_config_load()) {
        vTaskDelete(NULL);
        return;
    }
    if (!components_init()) {
        vTaskDelete(NULL);
        return;
    }
    app_component_boot_run();
    app_line_follow_init();
    taskENTER_CRITICAL();
    result = xTaskCreate(app_control_task, "controlTask", APP_CONTROL_TASK_STACK_DEPTH,
                         NULL, tskIDLE_PRIORITY + app_dts_config_get()->control_priority,
                         &app_control_task_handle);
    result &= xTaskCreate(app_sensor_task, "sensorTask", APP_SENSOR_TASK_STACK_DEPTH,
                          NULL, tskIDLE_PRIORITY + app_dts_config_get()->sensor_priority,
                          &app_sensor_task_handle);
    result &= xTaskCreate(app_oled_task, "oledTask", APP_OLED_TASK_STACK_DEPTH,
                          NULL, tskIDLE_PRIORITY + app_dts_config_get()->oled_priority,
                          &app_oled_task_handle);
    result &= xTaskCreate(app_business_task, "businessTask", APP_BUSINESS_TASK_STACK_DEPTH,
                          NULL, tskIDLE_PRIORITY + app_dts_config_get()->business_priority,
                          &app_business_task_handle);
    taskEXIT_CRITICAL();
    if (result != pdPASS) {
        control_stop();
    }
    vTaskDelete(NULL);
}

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

#include "app_component_boot.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "oled.h"
#include "ark_component.h"

#define APP_BOOT_TASK_STACK_DEPTH 256U
#define APP_BOOT_BAR_LEFT 16
#define APP_BOOT_BAR_RIGHT 111
#define APP_BOOT_BAR_TOP 24
#define APP_BOOT_BAR_BOTTOM 35
#define APP_BOOT_FAILURE_HALF_PERIOD_MS 300U
#define APP_BOOT_COMPLETE_HOLD_MS 500U

static TaskHandle_t app_boot_task_handle;
static SemaphoreHandle_t app_boot_ack;
static ark_component_progress_info_t app_boot_progress;
static volatile bool app_boot_enter_running;
static size_t app_boot_init_count;
static size_t app_boot_self_test_count;
static size_t app_boot_init_success_count;

static void app_self_test_report(
    const ark_component_t *component,
    ark_component_result_t result,
    uint32_t elapsed_ms) {
    const char *status = (result == ARK_COMPONENT_OK) ? "PASS" :
                         (result == ARK_COMPONENT_TIMEOUT) ? "TIMEOUT" : "FAIL";

    printf("[self-test] %-12s %-7s %lu ms\r\n",
           component->name, status, (unsigned long)elapsed_ms);
}

static uint8_t app_centered_text_x(const char *text) {
    size_t width = strlen(text) * 6U;

    return (width < OLED_WIDTH) ? (uint8_t)((OLED_WIDTH - width) / 2U) : 0U;
}

static void app_draw_boot_progress(uint8_t percent, const char *status) {
    char percent_text[5];
    int16_t y;
    int16_t fill_right;

    if (percent > 100U) {
        percent = 100U;
    }
    snprintf(percent_text, sizeof(percent_text), "%u%%", (unsigned int)percent);
    oled_canvas_clear();
    oled_draw_printf(app_centered_text_x(percent_text), 1U, 8U, "%s", percent_text);
    oled_draw_line(APP_BOOT_BAR_LEFT + 1, APP_BOOT_BAR_TOP,
                   APP_BOOT_BAR_RIGHT - 1, APP_BOOT_BAR_TOP);
    oled_draw_line(APP_BOOT_BAR_LEFT + 1, APP_BOOT_BAR_BOTTOM,
                   APP_BOOT_BAR_RIGHT - 1, APP_BOOT_BAR_BOTTOM);
    oled_draw_line(APP_BOOT_BAR_LEFT, APP_BOOT_BAR_TOP + 1,
                   APP_BOOT_BAR_LEFT, APP_BOOT_BAR_BOTTOM - 1);
    oled_draw_line(APP_BOOT_BAR_RIGHT, APP_BOOT_BAR_TOP + 1,
                   APP_BOOT_BAR_RIGHT, APP_BOOT_BAR_BOTTOM - 1);
    fill_right = APP_BOOT_BAR_LEFT + 2 +
        (int16_t)((uint16_t)percent * 91U / 100U);
    for (y = APP_BOOT_BAR_TOP + 2; y <= APP_BOOT_BAR_BOTTOM - 2; ++y) {
        oled_draw_line(APP_BOOT_BAR_LEFT + 2, y, fill_right, y);
    }
    if (status != NULL) {
        oled_draw_printf(app_centered_text_x(status), 6U, 8U, "%s", status);
    }
    oled_present();
}

static void app_draw_running(uint8_t dots) {
    char text[10] = "Runing";
    uint8_t index;

    for (index = 0U; index < dots; ++index) {
        text[6U + index] = '.';
    }
    text[6U + dots] = '\0';
    oled_canvas_clear();
    oled_draw_printf(app_centered_text_x(text), 3U, 8U, "%s", text);
    oled_present();
}

static void app_boot_task(void *argument) {
    (void)argument;

    for (;;) {
        ark_component_progress_info_t progress;
        char status[22];

        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        taskENTER_CRITICAL();
        progress = app_boot_progress;
        taskEXIT_CRITICAL();

        if (progress.state == ARK_COMPONENT_PROGRESS_BEGIN) {
            (void)xSemaphoreGive(app_boot_ack);
        } else if ((progress.state == ARK_COMPONENT_PROGRESS_RESULT) &&
                   (progress.result != ARK_COMPONENT_OK)) {
            uint8_t flash;

            snprintf(status, sizeof(status), "FAIL:%s", progress.component->name);
            for (flash = 0U; flash < 3U; ++flash) {
                app_draw_boot_progress(progress.percent, status);
                vTaskDelay(pdMS_TO_TICKS(APP_BOOT_FAILURE_HALF_PERIOD_MS));
                app_draw_boot_progress(progress.percent, NULL);
                vTaskDelay(pdMS_TO_TICKS(APP_BOOT_FAILURE_HALF_PERIOD_MS));
            }
            (void)xSemaphoreGive(app_boot_ack);
        } else if (progress.state == ARK_COMPONENT_PROGRESS_RESULT) {
            app_draw_boot_progress(progress.percent, progress.component->name);
            (void)xSemaphoreGive(app_boot_ack);
        } else if (progress.state == ARK_COMPONENT_PROGRESS_FINISHED) {
            uint8_t dots = 0U;

            snprintf(status, sizeof(status), "%u/%u",
                     (unsigned int)progress.success_count,
                     (unsigned int)progress.total_count);
            app_draw_boot_progress(progress.percent, status);
            (void)xSemaphoreGive(app_boot_ack);
            if (!app_boot_enter_running) {
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(APP_BOOT_COMPLETE_HOLD_MS));
            for (;;) {
                app_draw_running(dots);
                dots = (uint8_t)((dots + 1U) % 4U);
                vTaskDelay(pdMS_TO_TICKS(500U));
            }
        }
    }
}

static void app_publish_boot_progress(const ark_component_progress_info_t *progress) {
    taskENTER_CRITICAL();
    app_boot_progress = *progress;
    taskEXIT_CRITICAL();
    if (app_boot_task_handle != NULL) {
        (void)xTaskNotifyGive(app_boot_task_handle);
    }
    if ((app_boot_ack != NULL) &&
        (progress->state == ARK_COMPONENT_PROGRESS_RESULT) &&
        (progress->result != ARK_COMPONENT_OK)) {
        (void)xSemaphoreTake(app_boot_ack, pdMS_TO_TICKS(2000U));
    } else if ((app_boot_ack != NULL) &&
               ((progress->state == ARK_COMPONENT_PROGRESS_BEGIN) ||
                (progress->state == ARK_COMPONENT_PROGRESS_RESULT) ||
                (progress->state == ARK_COMPONENT_PROGRESS_FINISHED))) {
        (void)xSemaphoreTake(app_boot_ack, pdMS_TO_TICKS(250U));
    }
}

static void app_init_progress(const ark_component_progress_info_t *progress) {
    ark_component_progress_info_t combined = *progress;

    if (progress->state == ARK_COMPONENT_PROGRESS_FINISHED) {
        app_boot_init_success_count = progress->success_count;
        return;
    }
    combined.total_count = app_boot_init_count + app_boot_self_test_count;
    combined.completed_count = progress->completed_count;
    combined.success_count = progress->success_count;
    combined.percent = (combined.total_count == 0U) ? 100U :
        (uint8_t)((combined.completed_count * 100U) / combined.total_count);
    app_publish_boot_progress(&combined);
}

static void app_self_test_progress(const ark_component_progress_info_t *progress) {
    ark_component_progress_info_t combined = *progress;

    if (progress->state == ARK_COMPONENT_PROGRESS_BEGIN) {
        return;
    }
    combined.total_count = app_boot_init_count + app_boot_self_test_count;
    combined.completed_count = app_boot_init_count + progress->completed_count;
    combined.success_count = app_boot_init_success_count + progress->success_count;
    combined.percent = (combined.total_count == 0U) ? 100U :
        (uint8_t)((combined.completed_count * 100U) / combined.total_count);
    app_publish_boot_progress(&combined);
}

void app_component_boot_run(void) {
    app_boot_init_count = ark_component_init_count();
    app_boot_self_test_count = ark_component_self_test_count();
    app_boot_init_success_count = 0U;
    app_boot_enter_running = false;
    app_boot_ack = xSemaphoreCreateBinary();
    if ((app_boot_ack == NULL) || (xTaskCreate(
            app_boot_task,
            "appBoot",
            APP_BOOT_TASK_STACK_DEPTH,
            NULL,
            tskIDLE_PRIORITY + 1U,
            &app_boot_task_handle) != pdPASS)) {
        app_boot_task_handle = NULL;
        printf("[microcar] boot display task creation failed\r\n");
    }
    if (ark_component_init_all(app_init_progress) != ARK_COMPONENT_OK) {
        printf("[microcar] component initialization failed\r\n");
    }
    ark_component_self_test_all(app_self_test_report, app_self_test_progress);
    if (app_boot_task_handle != NULL) {
        app_boot_enter_running = true;
        (void)xTaskNotifyGive(app_boot_task_handle);
        (void)xSemaphoreTake(app_boot_ack, pdMS_TO_TICKS(1000U));
        vSemaphoreDelete(app_boot_ack);
        app_boot_ack = NULL;
    }
}

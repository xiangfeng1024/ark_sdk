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
#include "app_lcd_3d.h"
#include "app_sys_config.h"
#include "lcd.h"
#include "led.h"
#include "oled.h"
#include "spi.h"
#include "stm32f1xx_hal.h"
#include "stream.h"
#include "uart_cli.h"
#include "watchdog.h"
#include "ark_cli.h"
#include "ark_component.h"

#define APP_LED_TASK_STACK_DEPTH 128U
#define APP_OLED_TASK_STACK_DEPTH 384U
#define APP_LCD_TASK_STACK_DEPTH 384U
#define APP_FRAME_PERIOD_MS 33U
#define APP_FPS_WINDOW_MS 500U
#define APP_ANGLE_STEP_DEGREES 5U
#define APP_INITIAL_ANGLE_DEGREES 25U
#define APP_TILT_DEGREES 20U
#define APP_Q10_ONE 1024
#define APP_OLED_SCALE_PIXELS 20
#define APP_OLED_CENTER_X 50
#define APP_OLED_CENTER_Y 35
#define APP_OLED_FPS_X 80U

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} app_vertex_t;

typedef struct {
    int16_t x;
    int16_t y;
} app_point_t;

static const app_vertex_t app_cube_vertices[8] = {
    {-APP_Q10_ONE, -APP_Q10_ONE, -APP_Q10_ONE},
    { APP_Q10_ONE, -APP_Q10_ONE, -APP_Q10_ONE},
    { APP_Q10_ONE,  APP_Q10_ONE, -APP_Q10_ONE},
    {-APP_Q10_ONE,  APP_Q10_ONE, -APP_Q10_ONE},
    {-APP_Q10_ONE, -APP_Q10_ONE,  APP_Q10_ONE},
    { APP_Q10_ONE, -APP_Q10_ONE,  APP_Q10_ONE},
    { APP_Q10_ONE,  APP_Q10_ONE,  APP_Q10_ONE},
    {-APP_Q10_ONE,  APP_Q10_ONE,  APP_Q10_ONE},
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

static TaskHandle_t app_led_task_handle;
static TaskHandle_t app_oled_task_handle;
static TaskHandle_t app_lcd_task_handle;

static uint32_t app_spi_clock_divider(uint32_t cr1) {
    return 2UL << ((cr1 & SPI_CR1_BR) >> 3U);
}

static uint32_t app_irq_active(IRQn_Type irqn) {
    uint32_t number = (uint32_t)irqn;

    return (NVIC->IABR[number >> 5U] >> (number & 0x1FU)) & 1UL;
}

static void app_print_lcd_hardware_diagnostics(void) {
    uint32_t spi_cr1 = SPI1->CR1;
    uint32_t spi_clock = HAL_RCC_GetPCLK2Freq() / app_spi_clock_divider(spi_cr1);
    uint32_t dma_ccr = DMA1_Channel3->CCR;
    uint32_t dma_count = DMA1_Channel3->CNDTR;
    uint32_t dma_peripheral = DMA1_Channel3->CPAR;
    uint32_t dma_memory = DMA1_Channel3->CMAR;
    unsigned int dma_hal_state = (hspi1.hdmatx != NULL) ?
                                 (unsigned int)hspi1.hdmatx->State : 0xFFU;
    unsigned long dma_hal_error = (hspi1.hdmatx != NULL) ?
                                  (unsigned long)hspi1.hdmatx->ErrorCode : 0xFFFFFFFFUL;

    printf(
        "LCD_HW cpuid=%08lX dbgmcu=%08lX dev=%03lX rev=%04lX\r\n",
        (unsigned long)SCB->CPUID,
        (unsigned long)DBGMCU->IDCODE,
        (unsigned long)(DBGMCU->IDCODE & 0x0FFFUL),
        (unsigned long)(DBGMCU->IDCODE >> 16U));
    printf(
        "  clock sys=%lu hclk=%lu pclk2=%lu spi1=%lu RCC_CR=%08lX CFGR=%08lX FLASH_ACR=%08lX\r\n",
        (unsigned long)HAL_RCC_GetSysClockFreq(),
        (unsigned long)HAL_RCC_GetHCLKFreq(),
        (unsigned long)HAL_RCC_GetPCLK2Freq(),
        (unsigned long)spi_clock,
        (unsigned long)RCC->CR,
        (unsigned long)RCC->CFGR,
        (unsigned long)FLASH->ACR);
    printf(
        "  clocks AHBENR=%08lX APB2ENR=%08lX AFIO_MAPR=%08lX\r\n",
        (unsigned long)RCC->AHBENR,
        (unsigned long)RCC->APB2ENR,
        (unsigned long)AFIO->MAPR);
    printf(
        "  gpioa CRL=%08lX IDR=%04lX ODR=%04lX BSRR=%08lX\r\n",
        (unsigned long)GPIOA->CRL,
        (unsigned long)GPIOA->IDR,
        (unsigned long)GPIOA->ODR,
        (unsigned long)GPIOA->BSRR);
    printf(
        "  spi1 CR1=%04lX CR2=%04lX SR=%04lX hal_state=%u hal_error=%08lX\r\n",
        (unsigned long)spi_cr1,
        (unsigned long)SPI1->CR2,
        (unsigned long)SPI1->SR,
        (unsigned int)hspi1.State,
        (unsigned long)hspi1.ErrorCode);
    printf(
        "  dma1_ch3 CCR=%04lX CNDTR=%lu CPAR=%08lX CMAR=%08lX "
        "ISR=%08lX hal_state=%u hal_error=%08lX\r\n",
        (unsigned long)dma_ccr,
        (unsigned long)dma_count,
        (unsigned long)dma_peripheral,
        (unsigned long)dma_memory,
        (unsigned long)DMA1->ISR,
        dma_hal_state,
        dma_hal_error);
    printf(
        "  irq dma3 en=%lu pending=%lu active=%lu prio=%lu "
        "spi1 en=%lu pending=%lu active=%lu prio=%lu\r\n",
        (unsigned long)NVIC_GetEnableIRQ(DMA1_Channel3_IRQn),
        (unsigned long)NVIC_GetPendingIRQ(DMA1_Channel3_IRQn),
        (unsigned long)app_irq_active(DMA1_Channel3_IRQn),
        (unsigned long)NVIC_GetPriority(DMA1_Channel3_IRQn),
        (unsigned long)NVIC_GetEnableIRQ(SPI1_IRQn),
        (unsigned long)NVIC_GetPendingIRQ(SPI1_IRQn),
        (unsigned long)app_irq_active(SPI1_IRQn),
        (unsigned long)NVIC_GetPriority(SPI1_IRQn));
    printf(
        "  cpu primask=%lu basepri=%lu ipsr=%lu\r\n",
        (unsigned long)__get_PRIMASK(),
        (unsigned long)__get_BASEPRI(),
        (unsigned long)__get_IPSR());
}

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

static app_point_t app_project_oled_vertex(const app_vertex_t *vertex, uint16_t angle) {
    int32_t sin_x = app_sine_q10(APP_TILT_DEGREES);
    int32_t cos_x = app_cosine_q10(APP_TILT_DEGREES);
    int32_t sin_y = app_sine_q10(angle);
    int32_t cos_y = app_cosine_q10(angle);
    int32_t rotated_x;
    int32_t rotated_y;
    int32_t rotated_z;
    app_point_t point;

    rotated_x = ((int32_t)vertex->x * cos_y + (int32_t)vertex->z * sin_y) >> 10;
    rotated_z = (-(int32_t)vertex->x * sin_y + (int32_t)vertex->z * cos_y) >> 10;
    rotated_y = ((int32_t)vertex->y * cos_x - rotated_z * sin_x) >> 10;
    point.x = (int16_t)(APP_OLED_CENTER_X +
                        rotated_x * APP_OLED_SCALE_PIXELS / APP_Q10_ONE);
    point.y = (int16_t)(APP_OLED_CENTER_Y -
                        rotated_y * APP_OLED_SCALE_PIXELS / APP_Q10_ONE);
    return point;
}

static bool app_render_oled(uint16_t angle, uint32_t fps, bool fps_valid) {
    app_point_t points[8];
    size_t index;

    oled_canvas_clear();
    for (index = 0U; index < 8U; ++index) {
        points[index] = app_project_oled_vertex(&app_cube_vertices[index], angle);
    }
    for (index = 0U; index < 12U; ++index) {
        oled_draw_line(
            points[app_cube_edges[index][0]].x,
            points[app_cube_edges[index][0]].y,
            points[app_cube_edges[index][1]].x,
            points[app_cube_edges[index][1]].y);
    }
    if (fps_valid) {
        oled_draw_printf(APP_OLED_FPS_X, 0U, 8U, "FPS:%2lu", (unsigned long)fps);
    } else {
        oled_draw_printf(APP_OLED_FPS_X, 0U, 8U, "FPS:--");
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

static void app_oled_task(void *argument) {
    TickType_t next_wakeup = xTaskGetTickCount();
    TickType_t window_started = next_wakeup;
    uint32_t frames = 0U;
    uint32_t fps = 0U;
    uint16_t angle = APP_INITIAL_ANGLE_DEGREES;
    bool fps_valid = false;

    (void)argument;
    for (;;) {
        TickType_t now;
        TickType_t elapsed;

        if (app_render_oled(angle, fps, fps_valid)) {
            ++frames;
        }
        angle = (uint16_t)((angle + APP_ANGLE_STEP_DEGREES) % 360U);
        now = xTaskGetTickCount();
        elapsed = now - window_started;
        if (elapsed >= pdMS_TO_TICKS(APP_FPS_WINDOW_MS)) {
            fps = frames * configTICK_RATE_HZ / (uint32_t)elapsed;
            frames = 0U;
            window_started = now;
            fps_valid = true;
        }
        vTaskDelayUntil(&next_wakeup, pdMS_TO_TICKS(APP_FRAME_PERIOD_MS));
    }
}

static void app_lcd_task(void *argument) {
    TickType_t next_wakeup = xTaskGetTickCount();
    TickType_t window_started = next_wakeup;
    uint32_t frames = 0U;
    uint32_t fps = 0U;
    uint16_t angle = APP_INITIAL_ANGLE_DEGREES;

    (void)argument;
    for (;;) {
        TickType_t now;
        TickType_t elapsed;

        if (app_lcd_3d_render(angle, fps)) {
            ++frames;
        }
        angle = (uint16_t)((angle + APP_ANGLE_STEP_DEGREES) % 360U);
        now = xTaskGetTickCount();
        elapsed = now - window_started;
        if (elapsed >= pdMS_TO_TICKS(APP_FPS_WINDOW_MS)) {
            fps = frames * configTICK_RATE_HZ / (uint32_t)elapsed;
            frames = 0U;
            window_started = now;
        }
        vTaskDelayUntil(&next_wakeup, pdMS_TO_TICKS(APP_FRAME_PERIOD_MS));
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

static int app_display_cli(int argc, char *argv[]) {
    const char *command = argv[0];
    const char *argument = (argc == 1) ? NULL : argv[1];
    lcd_diagnostics_t lcd_diagnostics;
    app_lcd_3d_diagnostics_t render_diagnostics;
    size_t index;

    if (strcmp(command, "oled_pause") == 0) {
        if ((argument != NULL) || (app_oled_task_handle == NULL)) {
            printf("usage: oled_pause\r\n");
        } else {
            vTaskSuspend(app_oled_task_handle);
            printf("OLED render task suspended\r\n");
        }
        return 0;
    }
    if (strcmp(command, "oled_resume") == 0) {
        if ((argument != NULL) || (app_oled_task_handle == NULL)) {
            printf("usage: oled_resume\r\n");
        } else {
            vTaskResume(app_oled_task_handle);
            printf("OLED render task resumed\r\n");
        }
        return 0;
    }
    if (strcmp(command, "lcd_hw") == 0) {
        if (argument != NULL) {
            printf("usage: lcd_hw\r\n");
        } else {
            app_print_lcd_hardware_diagnostics();
        }
        return 0;
    }
    if (strcmp(command, "lcd_diag") != 0) {
        return -1;
    }
    if (argument != NULL) {
        printf("usage: lcd_diag\r\n");
        return 0;
    }
    if (!lcd_get_diagnostics(&lcd_diagnostics) ||
        !app_lcd_3d_get_diagnostics(&render_diagnostics)) {
        printf("LCD_DIAG_ERROR snapshot_failed\r\n");
        return -1;
    }
    printf(
        "LCD_DIAG init=%u sequence=%lu fps=%lu angle=%u failures=%lu dma_bytes=%lu regions=%lu\r\n",
        lcd_diagnostics.initialized ? 1U : 0U,
        (unsigned long)render_diagnostics.frame_sequence,
        (unsigned long)render_diagnostics.last_fps,
        render_diagnostics.last_angle,
        (unsigned long)lcd_diagnostics.transfer_failures,
        (unsigned long)lcd_diagnostics.dma_bytes,
        (unsigned long)lcd_diagnostics.region_sequence);
    printf(
        "  heap_free=%lu stack_hwm_words: led=%u oled=%u lcd=%u\r\n",
        (unsigned long)xPortGetFreeHeapSize(),
        (unsigned int)((app_led_task_handle != NULL) ?
            uxTaskGetStackHighWaterMark(app_led_task_handle) : 0U),
        (unsigned int)((app_oled_task_handle != NULL) ?
            uxTaskGetStackHighWaterMark(app_oled_task_handle) : 0U),
        (unsigned int)((app_lcd_task_handle != NULL) ?
            uxTaskGetStackHighWaterMark(app_lcd_task_handle) : 0U));
    for (index = 0U; index < 8U; ++index) {
        printf(
            "  v%lu=(%d,%d)%s",
            (unsigned long)index,
            render_diagnostics.vertices[index][0],
            render_diagnostics.vertices[index][1],
            ((index & 3U) == 3U) ? "\r\n" : " ");
    }
    return 0;
}

static ark_cli_command_t app_lcd_diag_command = {
    .name = "lcd_diag", .usage = "lcd_diag",
    .description = "show ST7789/DMA/render diagnostics", .handler = app_display_cli,
};
static ark_cli_command_t app_lcd_hw_command = {
    .name = "lcd_hw", .usage = "lcd_hw",
    .description = "show MCU/SPI1/DMA/GPIO registers", .handler = app_display_cli,
};
static ark_cli_command_t app_oled_pause_command = {
    .name = "oled_pause", .usage = "oled_pause",
    .description = "suspend OLED render task", .handler = app_display_cli,
};
static ark_cli_command_t app_oled_resume_command = {
    .name = "oled_resume", .usage = "oled_resume",
    .description = "resume OLED render task", .handler = app_display_cli,
};

void components_init(void) {
    watchdog_register();
    stream_register();
    uart_cli_register();
    (void)ark_cli_register(&app_lcd_diag_command);
    (void)ark_cli_register(&app_lcd_hw_command);
    (void)ark_cli_register(&app_oled_pause_command);
    (void)ark_cli_register(&app_oled_resume_command);
    led_register();
    oled_register();
    lcd_register();

    if (ark_component_init_all(NULL) != ARK_COMPONENT_OK) {
        printf("[app] component initialization failed\r\n");
    }
    ark_component_self_test_all(app_self_test_report, NULL);
}

void appStartTask(void *argument) {
    BaseType_t led_result;
    BaseType_t oled_result;
    BaseType_t lcd_result;

    (void)argument;
    components_init();
    printf("[app] dual_display selected\r\n");
    taskENTER_CRITICAL();
    led_result = xTaskCreate(
        app_led_task, "appLed", APP_LED_TASK_STACK_DEPTH,
        NULL, tskIDLE_PRIORITY + 1U, &app_led_task_handle);
    oled_result = xTaskCreate(
        app_oled_task, "appOled3D", APP_OLED_TASK_STACK_DEPTH,
        NULL, tskIDLE_PRIORITY + 1U, &app_oled_task_handle);
    lcd_result = xTaskCreate(
        app_lcd_task, "appLcd3D", APP_LCD_TASK_STACK_DEPTH,
        NULL, tskIDLE_PRIORITY + 2U, &app_lcd_task_handle);
    taskEXIT_CRITICAL();

    printf(
        "[app] tasks led=%ld oled=%ld lcd=%ld free_heap=%lu\r\n",
        (long)led_result,
        (long)oled_result,
        (long)lcd_result,
        (unsigned long)xPortGetFreeHeapSize());
    if ((led_result != pdPASS) || (oled_result != pdPASS) || (lcd_result != pdPASS)) {
        printf("[app] one or more task creations failed\r\n");
    }
    vTaskDelete(NULL);
}

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

#include "ws2812b.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"
#include "ark_dts_generated.h"
#include "ark_hal_gpio.h"
#include "ark_hal_pwm.h"
#include "ark_hal_time.h"

#define WS2812B_BITS_PER_LED 24U
#define WS2812B_RESET_SLOTS 48U
#define WS2812B_TRANSFER_TIMEOUT_MS 20U
#define WS2812B_MAX_WHITE_LEVEL 88U
#define WS2812B_SELF_TEST_MS 200U
#define WS2812B_PERIOD_NS 1250U
#define WS2812B_ZERO_HIGH_NS 350U
#define WS2812B_ONE_HIGH_NS 700U

typedef enum {
    WS2812B_TRANSPORT_PWM_DMA = 0,
    WS2812B_TRANSPORT_SOFTWARE
} ws2812b_transport_t;

typedef struct {
    ws2812b_transport_t transport;
    ark_hal_pwm_id_t pwm_id;
    ark_hal_gpio_config_t DATA;
    size_t count;
} ws2812b_config_t;

static ws2812b_config_t ws2812b_config;

static bool ws2812b_load_config(void) {
    const ark_of_node_t *node = ark_of_find_compatible_node(NULL, "ws2812b");
    uint32_t count;

    if ((node == NULL) ||
        (ark_of_property_read_u32(node, "chain-length", &count) != 0) ||
        (count == 0U) || (count > WS2812B_MAX_COUNT)) {
        return false;
    }
    ws2812b_config.count = count;
#if ARK_DTS_WS2812B_SOFTWARE_ENABLED
    ws2812b_config.transport = WS2812B_TRANSPORT_SOFTWARE;
    return ark_of_get_named_gpio(node, "data-gpios", 0U, &ws2812b_config.DATA) == 0;
#elif ARK_DTS_WS2812B_PWM_ENABLED
    {
        uint8_t pwm_id;
        if (ark_of_hal_get(node, "pwms", 0U, ARK_OF_HAL_PWM, &pwm_id, NULL) != 0) {
            return false;
        }
        ws2812b_config.transport = WS2812B_TRANSPORT_PWM_DMA;
        ws2812b_config.pwm_id = (ark_hal_pwm_id_t)pwm_id;
        return true;
    }
#else
    return false;
#endif
}

size_t ws2812b_count(void) {
    return ws2812b_config.count;
}

#if ARK_DTS_WS2812B_PWM_ENABLED
static ws2812b_color_t ws2812b_colors[WS2812B_MAX_COUNT];
static uint16_t ws2812b_waveform[
    WS2812B_MAX_COUNT * WS2812B_BITS_PER_LED + WS2812B_RESET_SLOTS];

bool ws2812b_set(size_t index, uint8_t red, uint8_t green, uint8_t blue) {
    if (index >= ws2812b_count()) {
        return false;
    }
    ws2812b_colors[index] = (ws2812b_color_t){
        .red = red,
        .green = green,
        .blue = blue
    };
    return true;
}

void ws2812b_clear(void) {
    memset(ws2812b_colors, 0, sizeof(ws2812b_colors));
}

bool ws2812b_show(void) {
    uint32_t period = ark_hal_pwm.period_ticks(ws2812b_config.pwm_id);
    uint16_t zero_ticks;
    uint16_t one_ticks;
    size_t led;
    size_t output = 0U;

    if ((period < 10U) || (ws2812b_count() == 0U)) {
        return false;
    }
    zero_ticks = (uint16_t)((period * 28U) / 100U);
    one_ticks = (uint16_t)((period * 56U) / 100U);
    for (led = 0U; led < ws2812b_count(); ++led) {
        uint32_t grb = ((uint32_t)ws2812b_colors[led].green << 16U) |
                       ((uint32_t)ws2812b_colors[led].red << 8U) |
                       ws2812b_colors[led].blue;
        uint32_t mask;

        for (mask = 0x800000UL; mask != 0U; mask >>= 1U) {
            ws2812b_waveform[output++] = (grb & mask) ? one_ticks : zero_ticks;
        }
    }
    memset(&ws2812b_waveform[output], 0,
           WS2812B_RESET_SLOTS * sizeof(ws2812b_waveform[0]));
    output += WS2812B_RESET_SLOTS;
    return ark_hal_pwm.transmit_dma(
               ws2812b_config.pwm_id,
               ws2812b_waveform,
               output,
               WS2812B_TRANSFER_TIMEOUT_MS) == (int32_t)output;
}
#endif

#if ARK_DTS_WS2812B_SOFTWARE_ENABLED
static bool ws2812b_software_send(
    const ws2812b_color_t *colors,
    size_t count,
    const ws2812b_color_t *fill) {
    uint8_t grb[WS2812B_MAX_COUNT * 3U];
    size_t index;

    if (!ark_hal_time.is_ready() || (ark_hal_gpio.transmit_pulse_bytes == NULL) ||
        (count == 0U) || (count > ws2812b_count()) ||
        ((colors == NULL) && (fill == NULL))) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        const ws2812b_color_t *color = (colors != NULL) ? &colors[index] : fill;

        grb[index * 3U] = color->green;
        grb[index * 3U + 1U] = color->red;
        grb[index * 3U + 2U] = color->blue;
    }
    taskENTER_CRITICAL();
    if (!ark_hal_gpio.transmit_pulse_bytes(
            ws2812b_config.DATA.port,
            ws2812b_config.DATA.pin,
            grb,
            count * 3U,
            WS2812B_PERIOD_NS,
            WS2812B_ZERO_HIGH_NS,
            WS2812B_ONE_HIGH_NS)) {
        taskEXIT_CRITICAL();
        return false;
    }
    taskEXIT_CRITICAL();
    ark_hal_time.delay_us(80U);
    return true;
}
#endif

bool ws2812b_write(const ws2812b_color_t *colors, size_t count) {
#if ARK_DTS_WS2812B_SOFTWARE_ENABLED
    return ws2812b_software_send(colors, count, NULL);
#elif ARK_DTS_WS2812B_PWM_ENABLED
    size_t index;

    if ((colors == NULL) || (count != ws2812b_count())) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        ws2812b_set(index, colors[index].red, colors[index].green, colors[index].blue);
    }
    return ws2812b_show();
#else
    return false;
#endif
}

bool ws2812b_fill(uint8_t red, uint8_t green, uint8_t blue) {
#if ARK_DTS_WS2812B_SOFTWARE_ENABLED
    ws2812b_color_t color = {
        .red = red,
        .green = green,
        .blue = blue
    };

    return ws2812b_software_send(NULL, ws2812b_count(), &color);
#elif ARK_DTS_WS2812B_PWM_ENABLED
    size_t index;

    for (index = 0U; index < ws2812b_count(); ++index) {
        ws2812b_set(index, red, green, blue);
    }
    return ws2812b_show();
#else
    return false;
#endif
}

bool ws2812b_off(void) {
    return ws2812b_fill(0U, 0U, 0U);
}

ark_component_result_t ws2812b_init(void) {
    if (!ws2812b_load_config() || (ws2812b_count() == 0U) ||
        (ws2812b_count() > WS2812B_MAX_COUNT)) {
        return ARK_COMPONENT_ERROR;
    }
#if ARK_DTS_WS2812B_SOFTWARE_ENABLED
    if (!ark_hal_time.is_ready()) {
        return ARK_COMPONENT_ERROR;
    }
    ark_hal_gpio.set(ws2812b_config.DATA.port, ws2812b_config.DATA.pin, false);
#elif ARK_DTS_WS2812B_PWM_ENABLED
    if (!ark_hal_pwm.is_ready(ws2812b_config.pwm_id)) {
        return ARK_COMPONENT_ERROR;
    }
#endif
    if (!ws2812b_off()) {
        return ARK_COMPONENT_ERROR;
    }
    return ARK_COMPONENT_OK;
}

ark_component_result_t ws2812b_self_test(void) {
    ark_component_result_t result = ARK_COMPONENT_OK;

    if (!ws2812b_fill(WS2812B_MAX_WHITE_LEVEL,
                      WS2812B_MAX_WHITE_LEVEL,
                      WS2812B_MAX_WHITE_LEVEL)) {
        result = ARK_COMPONENT_ERROR;
    } else {
        vTaskDelay(pdMS_TO_TICKS(WS2812B_SELF_TEST_MS));
    }
    if (!ws2812b_off()) {
        result = ARK_COMPONENT_ERROR;
    }
    return result;
}

#if ARK_DTS_HAS_UART_CLI
static int ws2812b_cli(int argc, char *argv[]) {
    char *end;
    unsigned long level;

    if (argc != 2) {
        printf("usage: %s <0..7>\r\n", argv[0]);
        return -1;
    }
    level = strtoul(argv[1], &end, 10);
    if ((*end != '\0') || (level > 7U)) {
        return -1;
    }
    return ws2812b_fill(
               (uint8_t)((level * WS2812B_MAX_WHITE_LEVEL) / 7U),
               (uint8_t)((level * WS2812B_MAX_WHITE_LEVEL) / 7U),
               (uint8_t)((level * WS2812B_MAX_WHITE_LEVEL) / 7U)) ? 0 : -1;
}

static ark_cli_command_t ws2812b_command = {
    .name = "ws2812",
    .usage = "ws2812 <0..7>",
    .description = "set all WS2812B pixels to white level",
    .handler = ws2812b_cli,
};
#endif

static const ark_component_t ws2812b_component = {
    .name = "ws2812b",
#if ARK_DTS_WS2812B_SOFTWARE_ENABLED
    .init = ws2812b_init,
#else
    .init = NULL,
#endif
    .self_test = ws2812b_self_test,
    .self_test_expected_ms = WS2812B_SELF_TEST_MS,
    .init_level = ARK_COMPONENT_INIT_LEVEL_3,
};

bool ws2812b_register(void) {
#if ARK_DTS_HAS_UART_CLI
    (void)ark_cli_register(&ws2812b_command);
#endif
    return ark_component_register(&ws2812b_component);
}

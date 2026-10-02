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

#include "oled.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ssd1306.h"
#include "ark_dts_generated.h"
#include "ark_cli.h"
#include "ark_component.h"
#include "ark_dts.h"

#define OLED_FORMAT_BUFFER_SIZE 128U
#define OLED_I2C_ADDRESS 0x3CU
#define OLED_SELF_TEST_TIMEOUT_MS 100U
#define OLED_FRAME_CHECKSUM_INITIAL 2166136261UL
#define OLED_FRAME_CHECKSUM_PRIME 16777619UL
#define OLED_CLI_BYTES_PER_LINE 16U

static oled_config_t oled_config;

const oled_config_t *oled_config_get(void) {
    return &oled_config;
}

static bool oled_load_config(void) {
    const ark_of_node_t *node = ark_of_find_compatible_node(NULL, "oled");
    uint8_t i2c_id;

    if ((node == NULL) ||
        (ark_of_hal_get_parent(node, ARK_OF_HAL_I2C, &i2c_id) != 0)) {
        return false;
    }
    oled_config.i2c_id = (ark_hal_i2c_id_t)i2c_id;
    oled_config.dirty_refresh = ARK_DTS_OLED_DIRTY_REFRESH != 0;
    return true;
}

static ark_component_result_t oled_init(void) {
    if (!oled_load_config() || !ssd1306_init()) {
        return ARK_COMPONENT_ERROR;
    }
    return ARK_COMPONENT_OK;
}

static ark_component_result_t oled_self_test(void) {
    return ark_hal_i2c.is_device_ready(
               ark_oled_config.i2c_id,
               OLED_I2C_ADDRESS,
               OLED_SELF_TEST_TIMEOUT_MS) ? ARK_COMPONENT_OK : ARK_COMPONENT_ERROR;
}

static const ark_component_t oled_component = {
    .name = "oled_sh1106",
    .init = oled_init,
    .self_test = oled_self_test,
    .self_test_expected_ms = OLED_SELF_TEST_TIMEOUT_MS,
    .init_level = ARK_COMPONENT_INIT_LEVEL_1,
};

#if ARK_DTS_HAS_UART_CLI && ARK_DTS_OLED_DIRTY_REFRESH
static uint8_t oled_cli_frame[OLED_FRAME_BUFFER_SIZE];

static int oled_frame_cli(int argc, char *argv[]) {
    size_t offset;
    uint32_t checksum = OLED_FRAME_CHECKSUM_INITIAL;
    uint32_t sequence;

    if (argc != 1) {
        printf("usage: %s\r\n", argv[0]);
        return -1;
    }
    if (!oled_copy_frame(oled_cli_frame, sizeof(oled_cli_frame), &sequence)) {
        printf("OLED_FB_ERROR snapshot_failed\r\n");
        return -1;
    }
    for (offset = 0U; offset < sizeof(oled_cli_frame); ++offset) {
        checksum ^= oled_cli_frame[offset];
        checksum *= OLED_FRAME_CHECKSUM_PRIME;
    }
    printf("OLED_FB_BEGIN width=128 height=64 format=page-lsb size=1024 sequence=%lu\r\n",
           (unsigned long)sequence);
    for (offset = 0U;
         offset < sizeof(oled_cli_frame);
         offset += OLED_CLI_BYTES_PER_LINE) {
        size_t index;

        printf("%04lX:", (unsigned long)offset);
        for (index = 0U; index < OLED_CLI_BYTES_PER_LINE; ++index) {
            printf(" %02X", oled_cli_frame[offset + index]);
        }
        printf("\r\n");
    }
    printf("OLED_FB_END fnv1a32=%08lX\r\n", (unsigned long)checksum);
    return 0;
}

static ark_cli_command_t oled_frame_command = {
    .name = "oled_fb",
    .usage = "oled_fb",
    .description = "dump the last OLED framebuffer",
    .handler = oled_frame_cli,
};
#endif

bool oled_register(void) {
#if ARK_DTS_HAS_UART_CLI && ARK_DTS_OLED_DIRTY_REFRESH
    (void)ark_cli_register(&oled_frame_command);
#endif
    return ark_component_register(&oled_component);
}

int oled_printf(uint8_t x, uint8_t y, uint8_t font_size, const char *format, ...) {
    char buffer[OLED_FORMAT_BUFFER_SIZE];
    va_list arguments;
    int length;

    if (format == NULL) {
        return -1;
    }
    va_start(arguments, format);
    length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if ((length < 0) || !ssd1306_write_string(x, y, font_size, buffer)) {
        return -1;
    }
    return (int)strlen(buffer);
}

void oled_canvas_clear(void) {
    ssd1306_canvas_clear();
}

void oled_draw_pixel(int16_t x, int16_t y, bool enabled) {
    ssd1306_draw_pixel(x, y, enabled);
}

void oled_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    ssd1306_draw_line(x0, y0, x1, y1);
}

bool oled_draw_zh16(uint8_t x, uint8_t page, oled_zh_glyph_t glyph) {
    return ssd1306_draw_zh16(x, page, (uint8_t)glyph);
}

int oled_draw_printf(uint8_t x, uint8_t y, uint8_t font_size, const char *format, ...) {
    char buffer[OLED_FORMAT_BUFFER_SIZE];
    va_list arguments;
    int length;

    if (format == NULL) {
        return -1;
    }
    va_start(arguments, format);
    length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if ((length < 0) || !ssd1306_draw_string(x, y, font_size, buffer)) {
        return -1;
    }
    return (int)strlen(buffer);
}

bool oled_present(void) {
    return ssd1306_present();
}

bool oled_copy_frame(uint8_t *buffer, size_t size, uint32_t *sequence) {
    return ssd1306_copy_frame(buffer, size, sequence);
}

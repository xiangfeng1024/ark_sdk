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

#include "ssd1306.h"

#include "oled.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "ark_dts_generated.h"
#include "oled_font.h"
#include "oled_font_zh.h"
#include "ark_hal_i2c.h"

#define SSD1306_WIDTH 128U
#define SSD1306_HEIGHT 64U
#define SSD1306_PAGE_COUNT (SSD1306_HEIGHT / 8U)
#define SSD1306_FRAME_SIZE (SSD1306_WIDTH * SSD1306_PAGE_COUNT)
#define SSD1306_PAGE_PACKET_SIZE (SSD1306_WIDTH + 1U)
#define SSD1306_I2C_ADDRESS 0x3CU
#define SH1106_COLUMN_OFFSET 0U
#define SSD1306_POWER_ON_DELAY_MS 200U
#define SSD1306_TRANSFER_TIMEOUT_MS 100U
#define SSD1306_COMMAND_CONTROL_BYTE 0x00U
#define SSD1306_DATA_CONTROL_BYTE 0x40U
#define SSD1306_FONT_6X8_WIDTH 6U
#define SSD1306_FONT_8X16_WIDTH 8U

static uint8_t ssd1306_frame[SSD1306_FRAME_SIZE];
static uint8_t ssd1306_page_packet[SSD1306_PAGE_PACKET_SIZE];
static uint32_t ssd1306_present_sequence;
#if ARK_DTS_OLED_DIRTY_REFRESH
static uint8_t ssd1306_presented_frame[SSD1306_FRAME_SIZE];
#endif

static bool ssd1306_write_command(uint8_t command) {
    uint8_t packet[2] = {SSD1306_COMMAND_CONTROL_BYTE, command};

    return ark_hal_i2c.transmit(
               ark_oled_config.i2c_id,
               SSD1306_I2C_ADDRESS,
               packet,
               sizeof(packet),
               SSD1306_TRANSFER_TIMEOUT_MS) == (int32_t)sizeof(packet);
}

static bool ssd1306_set_position(uint8_t x, uint8_t page) {
    uint8_t column = (uint8_t)(x + SH1106_COLUMN_OFFSET);

    return ssd1306_write_command((uint8_t)(0xB0U + page)) &&
           ssd1306_write_command((uint8_t)(0x10U | (column >> 4U))) &&
           ssd1306_write_command((uint8_t)(column & 0x0FU));
}

static bool ssd1306_update_region(
    uint8_t x_start,
    uint8_t x_end,
    uint8_t page_start,
    uint8_t page_end) {
    uint8_t page;

    if ((x_start > x_end) || (x_end >= SSD1306_WIDTH) ||
        (page_start > page_end) || (page_end >= SSD1306_PAGE_COUNT)) {
        return false;
    }
    for (page = page_start; page <= page_end; ++page) {
        size_t width;

        if (!ssd1306_set_position(x_start, page)) {
            return false;
        }
        width = (size_t)x_end - x_start + 1U;
        ssd1306_page_packet[0] = SSD1306_DATA_CONTROL_BYTE;
        memcpy(
            &ssd1306_page_packet[1],
            &ssd1306_frame[(size_t)page * SSD1306_WIDTH + x_start],
            width);
        if (ark_hal_i2c.transmit(
                ark_oled_config.i2c_id,
                SSD1306_I2C_ADDRESS,
                ssd1306_page_packet,
                width + 1U,
                SSD1306_TRANSFER_TIMEOUT_MS) != (int32_t)(width + 1U)) {
            return false;
        }
    }
    return true;
}

static void ssd1306_capture_presented_frame(void) {
#if ARK_DTS_OLED_DIRTY_REFRESH
    taskENTER_CRITICAL();
    memcpy(ssd1306_presented_frame, ssd1306_frame, sizeof(ssd1306_frame));
    taskEXIT_CRITICAL();
#endif
    ++ssd1306_present_sequence;
}

bool ssd1306_init(void) {
    static const uint8_t commands[] = {
        0xAEU,
        0x00U,
        0x10U,
        0x40U,
        0xB0U,
        0x81U, 0xFFU,
        0xA1U,
        0xA6U,
        0xA8U, 0x3FU,
        0xC8U,
        0xD3U, 0x00U,
        0xD5U, 0x80U,
        0xD8U, 0x05U,
        0xD9U, 0xF1U,
        0xDAU, 0x12U,
        0xDBU, 0x30U,
        0x8DU, 0x14U,
        0xAFU,
    };
    size_t index;

    vTaskDelay(pdMS_TO_TICKS(SSD1306_POWER_ON_DELAY_MS));
    if (!ark_hal_i2c.is_ready(ark_oled_config.i2c_id) ||
        !ark_hal_i2c.is_device_ready(
            ark_oled_config.i2c_id,
            SSD1306_I2C_ADDRESS,
            SSD1306_TRANSFER_TIMEOUT_MS)) {
        return false;
    }
    for (index = 0U; index < sizeof(commands); ++index) {
        if (!ssd1306_write_command(commands[index])) {
            return false;
        }
    }
    return ssd1306_clear();
}

bool ssd1306_clear(void) {
    bool result;

    memset(ssd1306_frame, 0, sizeof(ssd1306_frame));
    result = ssd1306_update_region(0U, SSD1306_WIDTH - 1U, 0U, SSD1306_PAGE_COUNT - 1U);
    if (result) {
        ssd1306_capture_presented_frame();
    }
    return result;
}

bool ssd1306_set_all_pixels(bool enabled) {
    return ssd1306_write_command(enabled ? 0xA5U : 0xA4U);
}

static void ssd1306_draw_char(
    uint8_t x,
    uint8_t page,
    uint8_t font_size,
    char character) {
    size_t glyph;
    size_t column;

    if ((character < ' ') || (character > '~')) {
        character = '?';
    }
    glyph = (size_t)(character - ' ');
    if (font_size == 16U) {
        for (column = 0U;
             (column < SSD1306_FONT_8X16_WIDTH) &&
             ((size_t)x + column < SSD1306_WIDTH);
             ++column) {
            ssd1306_frame[(size_t)page * SSD1306_WIDTH + x + column] =
                oled_font_8x16[glyph * 16U + column];
            ssd1306_frame[((size_t)page + 1U) * SSD1306_WIDTH + x + column] =
                oled_font_8x16[
                    glyph * 16U + SSD1306_FONT_8X16_WIDTH + column];
        }
    } else {
        for (column = 0U;
             (column < SSD1306_FONT_8X16_WIDTH) &&
             ((size_t)x + column < SSD1306_WIDTH);
             ++column) {
            ssd1306_frame[(size_t)page * SSD1306_WIDTH + x + column] =
                (column < SSD1306_FONT_6X8_WIDTH) ?
                    oled_font_6x8[glyph][column] : 0U;
        }
    }
}

void ssd1306_canvas_clear(void) {
    memset(ssd1306_frame, 0, sizeof(ssd1306_frame));
}

void ssd1306_draw_pixel(int16_t x, int16_t y, bool enabled) {
    size_t index;
    uint8_t mask;

    if ((x < 0) || (x >= (int16_t)SSD1306_WIDTH) ||
        (y < 0) || (y >= (int16_t)SSD1306_HEIGHT)) {
        return;
    }
    index = (size_t)(y / 8) * SSD1306_WIDTH + (size_t)x;
    mask = (uint8_t)(1U << ((uint16_t)y & 7U));
    if (enabled) {
        ssd1306_frame[index] |= mask;
    } else {
        ssd1306_frame[index] &= (uint8_t)~mask;
    }
}

void ssd1306_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    int32_t delta_x = (x0 < x1) ? (int32_t)x1 - x0 : (int32_t)x0 - x1;
    int32_t step_x = (x0 < x1) ? 1 : -1;
    int32_t delta_y = (y0 < y1) ? (int32_t)y0 - y1 : (int32_t)y1 - y0;
    int32_t step_y = (y0 < y1) ? 1 : -1;
    int32_t error = delta_x + delta_y;

    for (;;) {
        int32_t doubled_error;

        ssd1306_draw_pixel(x0, y0, true);
        if ((x0 == x1) && (y0 == y1)) {
            break;
        }
        doubled_error = 2 * error;
        if (doubled_error >= delta_y) {
            error += delta_y;
            x0 = (int16_t)(x0 + step_x);
        }
        if (doubled_error <= delta_x) {
            error += delta_x;
            y0 = (int16_t)(y0 + step_y);
        }
    }
}

bool ssd1306_draw_zh16(uint8_t x, uint8_t page, uint8_t glyph) {
    size_t column;

    if ((glyph >= OLED_ZH_COUNT) || (x > (SSD1306_WIDTH - 16U)) ||
        (page >= (SSD1306_PAGE_COUNT - 1U))) {
        return false;
    }
    for (column = 0U; column < 16U; ++column) {
        ssd1306_frame[(size_t)page * SSD1306_WIDTH + x + column] =
            oled_font_zh_16x16[glyph][column];
        ssd1306_frame[((size_t)page + 1U) * SSD1306_WIDTH + x + column] =
            oled_font_zh_16x16[glyph][16U + column];
    }
    return true;
}

bool ssd1306_draw_string(uint8_t x, uint8_t y, uint8_t font_size, const char *text) {
    uint8_t cursor_x = x;
    uint8_t cursor_page = y;
    uint8_t page_step;

    if ((x >= SSD1306_WIDTH) || (y >= SSD1306_PAGE_COUNT) || (text == NULL) ||
        ((font_size != 8U) && (font_size != 16U)) ||
        ((font_size == 16U) && (y >= (SSD1306_PAGE_COUNT - 1U)))) {
        return false;
    }
    page_step = (font_size == 16U) ? 2U : 1U;
    while (*text != '\0') {
        if (*text == '\n') {
            cursor_x = 0U;
            cursor_page = (uint8_t)(cursor_page + page_step);
            ++text;
            continue;
        }
        if (cursor_x > (SSD1306_WIDTH - SSD1306_FONT_8X16_WIDTH)) {
            cursor_x = 0U;
            cursor_page = (uint8_t)(cursor_page + page_step);
        }
        if ((cursor_page >= SSD1306_PAGE_COUNT) ||
            ((font_size == 16U) && (cursor_page >= (SSD1306_PAGE_COUNT - 1U)))) {
            break;
        }
        ssd1306_draw_char(cursor_x, cursor_page, font_size, *text);
        cursor_x = (uint8_t)(cursor_x + SSD1306_FONT_8X16_WIDTH);
        ++text;
    }
    return true;
}

bool ssd1306_draw_string_compact(uint8_t x, uint8_t y, const char *text) {
    size_t length;
    size_t index;

    if ((x >= SSD1306_WIDTH) || (y >= SSD1306_PAGE_COUNT) || (text == NULL)) {
        return false;
    }
    length = strlen(text);
    if ((length * SSD1306_FONT_6X8_WIDTH) > (SSD1306_WIDTH - x)) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        char character = text[index];
        size_t glyph;
        size_t column;

        if ((character < ' ') || (character > '~')) {
            character = '?';
        }
        glyph = (size_t)(character - ' ');
        for (column = 0U; column < SSD1306_FONT_6X8_WIDTH; ++column) {
            ssd1306_frame[
                (size_t)y * SSD1306_WIDTH +
                x +
                index * SSD1306_FONT_6X8_WIDTH +
                column] = oled_font_6x8[glyph][column];
        }
    }
    return true;
}

bool ssd1306_present(void) {
#if ARK_DTS_OLED_DIRTY_REFRESH
    uint8_t page;
    bool result = true;

    for (page = 0U; page < SSD1306_PAGE_COUNT; ++page) {
        size_t page_offset = (size_t)page * SSD1306_WIDTH;
        uint8_t first = SSD1306_WIDTH;
        uint8_t last = 0U;
        uint8_t x;

        for (x = 0U; x < SSD1306_WIDTH; ++x) {
            if (ssd1306_frame[page_offset + x] !=
                ssd1306_presented_frame[page_offset + x]) {
                if (first == SSD1306_WIDTH) {
                    first = x;
                }
                last = x;
            }
        }
        if ((first != SSD1306_WIDTH) &&
            !ssd1306_update_region(first, last, page, page)) {
            result = false;
            break;
        }
    }

    if (result) {
        ssd1306_capture_presented_frame();
    }
    return result;
#else
    bool result = ssd1306_update_region(
        0U,
        SSD1306_WIDTH - 1U,
        0U,
        SSD1306_PAGE_COUNT - 1U);

    if (result) {
        ssd1306_capture_presented_frame();
    }
    return result;
#endif
}

bool ssd1306_copy_frame(uint8_t *buffer, size_t size, uint32_t *sequence) {
    if ((buffer == NULL) || (size < sizeof(ssd1306_frame))) {
        return false;
    }
    taskENTER_CRITICAL();
    memcpy(buffer, ssd1306_frame, sizeof(ssd1306_frame));
    if (sequence != NULL) {
        *sequence = ssd1306_present_sequence;
    }
    taskEXIT_CRITICAL();
    return true;
}

bool ssd1306_write_string(uint8_t x, uint8_t y, uint8_t font_size, const char *text) {
    uint8_t cursor_x = x;
    uint8_t cursor_page = y;
    uint8_t page_step;

    if ((x >= SSD1306_WIDTH) || (y >= SSD1306_PAGE_COUNT) || (text == NULL) ||
        ((font_size != 8U) && (font_size != 16U)) ||
        ((font_size == 16U) && (y >= (SSD1306_PAGE_COUNT - 1U)))) {
        return false;
    }
    page_step = (font_size == 16U) ? 2U : 1U;
    while (*text != '\0') {
        if (*text == '\n') {
            cursor_x = 0U;
            cursor_page = (uint8_t)(cursor_page + page_step);
            ++text;
            continue;
        }
        if (cursor_x > (SSD1306_WIDTH - SSD1306_FONT_8X16_WIDTH)) {
            cursor_x = 0U;
            cursor_page = (uint8_t)(cursor_page + page_step);
        }
        if ((cursor_page >= SSD1306_PAGE_COUNT) ||
            ((font_size == 16U) && (cursor_page >= (SSD1306_PAGE_COUNT - 1U)))) {
            break;
        }
        ssd1306_draw_char(cursor_x, cursor_page, font_size, *text);
        if (!ssd1306_update_region(
                cursor_x,
                (uint8_t)(cursor_x + SSD1306_FONT_8X16_WIDTH - 1U),
                cursor_page,
                (uint8_t)(cursor_page + page_step - 1U))) {
            return false;
        }
        cursor_x = (uint8_t)(cursor_x + SSD1306_FONT_8X16_WIDTH);
        ++text;
    }
    ssd1306_capture_presented_frame();
    return true;
}

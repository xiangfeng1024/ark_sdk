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

#include "lcd.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "st7789.h"
#include "ark_component.h"
#include "ark_dts.h"

#define LCD_TRANSFER_BUFFER_SIZE 1152U
#define LCD_TEXT_BUFFER_SIZE 64U
#define LCD_FONT_WIDTH 5U
#define LCD_FONT_HEIGHT 7U
#define LCD_BACKLIGHT_ENABLE_DELAY_MS 20U
#define LCD_COLOR_BLACK 0x0000U
#define LCD_SELF_TEST_TEXT_X 84U
#define LCD_SELF_TEST_TEXT_Y 152U
#define LCD_SELF_TEST_TEXT_SCALE 2U
#define LCD_SELF_TEST_TEXT_COLOR 0xFFE0U
#define LCD_SELF_TEST_EXPECTED_MS 2000U

typedef struct {
    uint16_t color;
} lcd_fill_context_t;

typedef struct {
    int16_t x0;
    int16_t y0;
    int16_t x1;
    int16_t y1;
    uint16_t foreground;
    uint16_t background;
} lcd_line_context_t;

typedef struct {
    const char *text;
    uint16_t x;
    uint16_t y;
    uint8_t scale;
    uint16_t foreground;
    uint16_t background;
} lcd_text_context_t;

static uint8_t lcd_transfer_buffer[LCD_TRANSFER_BUFFER_SIZE];
static lcd_diagnostics_t lcd_diagnostics;
static lcd_config_t lcd_config;

const lcd_config_t *lcd_config_get(void) {
    return &lcd_config;
}

static bool lcd_load_config(void) {
    const ark_of_node_t *node =
        ark_of_find_compatible_node(NULL, "lcd");
    uint8_t spi_id;

    if ((node == NULL) ||
        (ark_of_hal_get_parent(node, ARK_OF_HAL_SPI, &spi_id) != 0) ||
        (ark_of_get_named_gpio(node, "bl-gpios", 0U, &lcd_config.BL) != 0) ||
        (ark_of_get_named_gpio(node, "cs-gpios", 0U, &lcd_config.CS) != 0) ||
        (ark_of_get_named_gpio(node, "dc-gpios", 0U, &lcd_config.DC) != 0) ||
        (ark_of_get_named_gpio(node, "reset-gpios", 0U, &lcd_config.RST) != 0)) {
        return false;
    }
    lcd_config.spi_id = (ark_hal_spi_id_t)spi_id;
    return true;
}

static const uint8_t lcd_font_digits[10][5] = {
    {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU},
    {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U},
    {0x42U, 0x61U, 0x51U, 0x49U, 0x46U},
    {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U},
    {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U},
    {0x27U, 0x45U, 0x45U, 0x45U, 0x39U},
    {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U},
    {0x01U, 0x71U, 0x09U, 0x05U, 0x03U},
    {0x36U, 0x49U, 0x49U, 0x49U, 0x36U},
    {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU},
};

static const uint8_t lcd_font_letters[26][5] = {
    {0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU},
    {0x7FU, 0x49U, 0x49U, 0x49U, 0x36U},
    {0x3EU, 0x41U, 0x41U, 0x41U, 0x22U},
    {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU},
    {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U},
    {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U},
    {0x3EU, 0x41U, 0x49U, 0x49U, 0x7AU},
    {0x7FU, 0x08U, 0x08U, 0x08U, 0x7FU},
    {0x00U, 0x41U, 0x7FU, 0x41U, 0x00U},
    {0x20U, 0x40U, 0x41U, 0x3FU, 0x01U},
    {0x7FU, 0x08U, 0x14U, 0x22U, 0x41U},
    {0x7FU, 0x40U, 0x40U, 0x40U, 0x40U},
    {0x7FU, 0x02U, 0x0CU, 0x02U, 0x7FU},
    {0x7FU, 0x04U, 0x08U, 0x10U, 0x7FU},
    {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU},
    {0x7FU, 0x09U, 0x09U, 0x09U, 0x06U},
    {0x3EU, 0x41U, 0x51U, 0x21U, 0x5EU},
    {0x7FU, 0x09U, 0x19U, 0x29U, 0x46U},
    {0x46U, 0x49U, 0x49U, 0x49U, 0x31U},
    {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U},
    {0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU},
    {0x1FU, 0x20U, 0x40U, 0x20U, 0x1FU},
    {0x3FU, 0x40U, 0x38U, 0x40U, 0x3FU},
    {0x63U, 0x14U, 0x08U, 0x14U, 0x63U},
    {0x07U, 0x08U, 0x70U, 0x08U, 0x07U},
    {0x61U, 0x51U, 0x49U, 0x45U, 0x43U},
};

static const uint8_t *lcd_glyph(char character) {
    static const uint8_t blank[5] = {0U, 0U, 0U, 0U, 0U};
    static const uint8_t colon[5] = {0U, 0x36U, 0x36U, 0U, 0U};
    static const uint8_t dash[5] = {0x08U, 0x08U, 0x08U, 0x08U, 0x08U};
    static const uint8_t dot[5] = {0U, 0x60U, 0x60U, 0U, 0U};

    if ((character >= 'a') && (character <= 'z')) {
        character = (char)(character - 'a' + 'A');
    }
    if ((character >= '0') && (character <= '9')) {
        return lcd_font_digits[character - '0'];
    }
    if ((character >= 'A') && (character <= 'Z')) {
        return lcd_font_letters[character - 'A'];
    }
    if (character == ':') {
        return colon;
    }
    if (character == '-') {
        return dash;
    }
    if (character == '.') {
        return dot;
    }
    return blank;
}

void lcd_surface_draw_pixel(
    lcd_surface_t *surface,
    int16_t x,
    int16_t y,
    uint16_t color) {
    size_t pixel;

    if ((surface == NULL) || (x < (int16_t)surface->x) ||
        (y < (int16_t)surface->y) ||
        (x >= (int16_t)(surface->x + surface->width)) ||
        (y >= (int16_t)(surface->y + surface->height))) {
        return;
    }
    pixel = (size_t)(y - (int16_t)surface->y) * surface->width +
            (size_t)(x - (int16_t)surface->x);
    surface->rgb565_be[pixel * 2U] = (uint8_t)(color >> 8U);
    surface->rgb565_be[pixel * 2U + 1U] = (uint8_t)color;
}

void lcd_surface_clear(lcd_surface_t *surface, uint16_t color) {
    size_t pixel;

    if (surface == NULL) {
        return;
    }
    for (pixel = 0U; pixel < (size_t)surface->width * surface->height; ++pixel) {
        surface->rgb565_be[pixel * 2U] = (uint8_t)(color >> 8U);
        surface->rgb565_be[pixel * 2U + 1U] = (uint8_t)color;
    }
}

void lcd_surface_draw_line(
    lcd_surface_t *surface,
    int16_t x0,
    int16_t y0,
    int16_t x1,
    int16_t y1,
    uint16_t color) {
    int16_t delta_x = (int16_t)((x0 < x1) ? (x1 - x0) : (x0 - x1));
    int16_t delta_y = (int16_t)-((y0 < y1) ? (y1 - y0) : (y0 - y1));
    int16_t step_x = (x0 < x1) ? 1 : -1;
    int16_t step_y = (y0 < y1) ? 1 : -1;
    int32_t error = (int32_t)delta_x + delta_y;

    for (;;) {
        int32_t doubled_error;

        lcd_surface_draw_pixel(surface, x0, y0, color);
        if ((x0 == x1) && (y0 == y1)) {
            break;
        }
        doubled_error = error * 2;
        if (doubled_error >= delta_y) {
            error += delta_y;
            x0 += step_x;
        }
        if (doubled_error <= delta_x) {
            error += delta_x;
            y0 += step_y;
        }
    }
}

bool lcd_refresh_region(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    lcd_region_renderer_t renderer,
    void *context) {
    uint16_t stripe_y;
    uint16_t rows_per_stripe;

    if (!lcd_diagnostics.initialized || (renderer == NULL) ||
        (width == 0U) || (height == 0U) ||
        ((uint32_t)x + width > LCD_WIDTH) ||
        ((uint32_t)y + height > LCD_HEIGHT) ||
        ((size_t)width * 2U > sizeof(lcd_transfer_buffer))) {
        return false;
    }
    rows_per_stripe = (uint16_t)(sizeof(lcd_transfer_buffer) / ((size_t)width * 2U));
    if (!st7789_begin_region(x, y, width, height)) {
        ++lcd_diagnostics.transfer_failures;
        return false;
    }
    for (stripe_y = y; stripe_y < (uint16_t)(y + height); stripe_y += rows_per_stripe) {
        lcd_surface_t surface;
        size_t bytes;

        surface.x = x;
        surface.y = stripe_y;
        surface.width = width;
        surface.height = rows_per_stripe;
        if ((uint32_t)stripe_y + surface.height > (uint32_t)y + height) {
            surface.height = (uint16_t)(y + height - stripe_y);
        }
        surface.rgb565_be = lcd_transfer_buffer;
        bytes = (size_t)surface.width * surface.height * 2U;
        if (!renderer(&surface, context) ||
            !st7789_write_pixels(lcd_transfer_buffer, bytes)) {
            st7789_end_region();
            ++lcd_diagnostics.transfer_failures;
            return false;
        }
        lcd_diagnostics.dma_bytes += bytes;
    }
    st7789_end_region();
    ++lcd_diagnostics.region_sequence;
    return true;
}

static bool lcd_render_fill(lcd_surface_t *surface, void *context) {
    const lcd_fill_context_t *fill_context = context;

    lcd_surface_clear(surface, fill_context->color);
    return true;
}

bool lcd_fill_rect(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    uint16_t color) {
    lcd_fill_context_t fill_context = {color};

    return lcd_refresh_region(
        x,
        y,
        width,
        height,
        lcd_render_fill,
        &fill_context);
}

bool lcd_clear(uint16_t color) {
    return lcd_fill_rect(0U, 0U, LCD_WIDTH, LCD_HEIGHT, color);
}

bool lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color) {
    return lcd_fill_rect(x, y, 1U, 1U, color);
}

static bool lcd_render_line(lcd_surface_t *surface, void *context) {
    const lcd_line_context_t *line_context = context;

    lcd_surface_clear(surface, line_context->background);
    lcd_surface_draw_line(
        surface,
        line_context->x0,
        line_context->y0,
        line_context->x1,
        line_context->y1,
        line_context->foreground);
    return true;
}

bool lcd_draw_line(
    int16_t x0,
    int16_t y0,
    int16_t x1,
    int16_t y1,
    uint16_t color,
    uint16_t background) {
    int16_t left = (x0 < x1) ? x0 : x1;
    int16_t right = (x0 > x1) ? x0 : x1;
    int16_t top = (y0 < y1) ? y0 : y1;
    int16_t bottom = (y0 > y1) ? y0 : y1;
    lcd_line_context_t line_context = {x0, y0, x1, y1, color, background};

    if ((left < 0) || (top < 0) || (right >= (int16_t)LCD_WIDTH) ||
        (bottom >= (int16_t)LCD_HEIGHT)) {
        return false;
    }
    return lcd_refresh_region(
        (uint16_t)left, (uint16_t)top,
        (uint16_t)(right - left + 1), (uint16_t)(bottom - top + 1),
        lcd_render_line, &line_context);
}

static bool lcd_render_text(lcd_surface_t *surface, void *context) {
    const lcd_text_context_t *text_context = context;
    size_t character_index;

    lcd_surface_clear(surface, text_context->background);
    for (character_index = 0U;
         text_context->text[character_index] != '\0';
         ++character_index) {
        const uint8_t *glyph = lcd_glyph(text_context->text[character_index]);
        uint16_t column;

        for (column = 0U; column < LCD_FONT_WIDTH; ++column) {
            uint16_t row;

            for (row = 0U; row < LCD_FONT_HEIGHT; ++row) {
                if ((glyph[column] & (1U << row)) != 0U) {
                    uint8_t scale_x;

                    for (scale_x = 0U; scale_x < text_context->scale; ++scale_x) {
                        uint8_t scale_y;

                        for (scale_y = 0U; scale_y < text_context->scale; ++scale_y) {
                            lcd_surface_draw_pixel(
                                surface,
                                (int16_t)(text_context->x + character_index *
                                    (LCD_FONT_WIDTH + 1U) * text_context->scale +
                                    column * text_context->scale + scale_x),
                                (int16_t)(text_context->y +
                                    row * text_context->scale + scale_y),
                                text_context->foreground);
                        }
                    }
                }
            }
        }
    }
    return true;
}

int lcd_printf(
    uint16_t x,
    uint16_t y,
    uint8_t scale,
    uint16_t foreground,
    uint16_t background,
    const char *format,
    ...) {
    char buffer[LCD_TEXT_BUFFER_SIZE];
    lcd_text_context_t text_context;
    va_list arguments;
    int length;
    size_t visible_length;
    uint16_t width;
    uint16_t height;

    if ((format == NULL) || (scale == 0U)) {
        return -1;
    }
    va_start(arguments, format);
    length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length < 0) {
        return -1;
    }
    visible_length = strlen(buffer);
    width = (uint16_t)(visible_length * (LCD_FONT_WIDTH + 1U) * scale);
    height = (uint16_t)(LCD_FONT_HEIGHT * scale);
    if ((visible_length == 0U) || ((uint32_t)x + width > LCD_WIDTH) ||
        ((uint32_t)y + height > LCD_HEIGHT)) {
        return -1;
    }
    text_context.text = buffer;
    text_context.x = x;
    text_context.y = y;
    text_context.scale = scale;
    text_context.foreground = foreground;
    text_context.background = background;
    if (!lcd_refresh_region(
            x,
            y,
            width,
            height,
            lcd_render_text,
            &text_context)) {
        return -1;
    }
    return (int)visible_length;
}

static ark_component_result_t lcd_init(void) {
    if (!st7789_init()) {
        return ARK_COMPONENT_ERROR;
    }
    lcd_diagnostics.initialized = true;
    if (!lcd_clear(LCD_COLOR_BLACK)) {
        lcd_diagnostics.initialized = false;
        return ARK_COMPONENT_ERROR;
    }
    vTaskDelay(pdMS_TO_TICKS(LCD_BACKLIGHT_ENABLE_DELAY_MS));
    st7789_set_backlight(true);
    return ARK_COMPONENT_OK;
}

static ark_component_result_t lcd_self_test(void) {
    if (!lcd_clear(LCD_COLOR_BLACK) ||
        (lcd_printf(
             LCD_SELF_TEST_TEXT_X,
             LCD_SELF_TEST_TEXT_Y,
             LCD_SELF_TEST_TEXT_SCALE,
             LCD_SELF_TEST_TEXT_COLOR,
             LCD_COLOR_BLACK,
             "LCD OK") < 0)) {
        return ARK_COMPONENT_ERROR;
    }
    return ARK_COMPONENT_OK;
}

static const ark_component_t lcd_component = {
    .name = "lcd_st7789",
    .init = lcd_init,
    .self_test = lcd_self_test,
    .self_test_expected_ms = LCD_SELF_TEST_EXPECTED_MS,
    .init_level = ARK_COMPONENT_INIT_LEVEL_1,
};

bool lcd_register(void) {
    return lcd_load_config() && ark_component_register(&lcd_component);
}

bool lcd_get_diagnostics(lcd_diagnostics_t *diagnostics) {
    if (diagnostics == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    *diagnostics = lcd_diagnostics;
    taskEXIT_CRITICAL();
    return true;
}

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

#include "st7789.h"

#include "lcd.h"

#include "FreeRTOS.h"
#include "task.h"
#include "ark_hal_gpio.h"
#include "ark_hal_spi.h"

#define ST7789_COMMAND_TIMEOUT_MS 100U
#define ST7789_DMA_TIMEOUT_MS 100U
#define ST7789_RESET_LOW_MS 20U
#define ST7789_RESET_RECOVERY_MS 120U
#define ST7789_SOFTWARE_RESET_DELAY_MS 150U
#define ST7789_SLEEP_EXIT_DELAY_MS 120U
#define ST7789_DISPLAY_ON_DELAY_MS 20U
#define ST7789_MAX_DMA_TRANSFER_SIZE 65535U

static void st7789_gpio_set(const ark_hal_gpio_config_t *gpio, bool active) {
    ark_hal_gpio.set(
        gpio->port,
        gpio->pin,
        active ? (gpio->active_level != 0U) : (gpio->active_level == 0U));
}

static void st7789_select(bool selected) {
    st7789_gpio_set(&ark_lcd_config.CS, selected);
}

static bool st7789_write_command(uint8_t command, const uint8_t *data, size_t size) {
    bool result;

    st7789_select(true);
    st7789_gpio_set(&ark_lcd_config.DC, false);
    result = ark_hal_spi.transmit(
                 ark_lcd_config.spi_id,
                 &command,
                 1U,
                 ST7789_COMMAND_TIMEOUT_MS) == 1;
    if (result && (size > 0U)) {
        st7789_gpio_set(&ark_lcd_config.DC, true);
        result = ark_hal_spi.transmit(
                     ark_lcd_config.spi_id,
                     data,
                     size,
                     ST7789_COMMAND_TIMEOUT_MS) == (int32_t)size;
    }
    st7789_select(false);
    return result;
}

static bool st7789_set_window(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height) {
    uint16_t x_end = (uint16_t)(x + width - 1U);
    uint16_t y_end = (uint16_t)(y + height - 1U);
    uint8_t columns[4] = {
        (uint8_t)(x >> 8U),
        (uint8_t)x,
        (uint8_t)(x_end >> 8U),
        (uint8_t)x_end,
    };
    uint8_t rows[4] = {
        (uint8_t)(y >> 8U),
        (uint8_t)y,
        (uint8_t)(y_end >> 8U),
        (uint8_t)y_end,
    };

    return st7789_write_command(0x2AU, columns, sizeof(columns)) &&
           st7789_write_command(0x2BU, rows, sizeof(rows));
}

bool st7789_init(void) {
    static const uint8_t color_mode = 0x55U;
    static const uint8_t memory_access = 0x00U;

    st7789_gpio_set(&ark_lcd_config.BL, false);
    st7789_select(false);
    st7789_gpio_set(&ark_lcd_config.DC, false);
    st7789_gpio_set(&ark_lcd_config.RST, true);
    vTaskDelay(pdMS_TO_TICKS(ST7789_RESET_LOW_MS));
    st7789_gpio_set(&ark_lcd_config.RST, false);
    vTaskDelay(pdMS_TO_TICKS(ST7789_RESET_RECOVERY_MS));

    if (!ark_hal_spi.is_ready(ark_lcd_config.spi_id) ||
        !st7789_write_command(0x01U, NULL, 0U)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(ST7789_SOFTWARE_RESET_DELAY_MS));
    if (!st7789_write_command(0x11U, NULL, 0U)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(ST7789_SLEEP_EXIT_DELAY_MS));
    if (!st7789_write_command(0x3AU, &color_mode, 1U) ||
        !st7789_write_command(0x36U, &memory_access, 1U) ||
        !st7789_write_command(0x21U, NULL, 0U) ||
        !st7789_write_command(0x13U, NULL, 0U) ||
        !st7789_write_command(0x29U, NULL, 0U)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(ST7789_DISPLAY_ON_DELAY_MS));
    return true;
}

void st7789_set_backlight(bool enabled) {
    st7789_gpio_set(&ark_lcd_config.BL, enabled);
}

bool st7789_write_region(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    const uint8_t *rgb565_be,
    size_t size) {
    if ((width == 0U) || (height == 0U) || (rgb565_be == NULL) ||
        ((uint32_t)x + width > ST7789_WIDTH) ||
        ((uint32_t)y + height > ST7789_HEIGHT) ||
        (size != (size_t)width * height * 2U)) {
        return false;
    }
    if (!st7789_begin_region(x, y, width, height)) {
        return false;
    }
    {
        bool result = st7789_write_pixels(rgb565_be, size);

        st7789_end_region();
        return result;
    }
}

bool st7789_begin_region(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    uint8_t memory_write = 0x2CU;

    if ((width == 0U) || (height == 0U) ||
        ((uint32_t)x + width > ST7789_WIDTH) ||
        ((uint32_t)y + height > ST7789_HEIGHT) ||
        !st7789_set_window(x, y, width, height)) {
        return false;
    }
    st7789_select(true);
    st7789_gpio_set(&ark_lcd_config.DC, false);
    if (ark_hal_spi.transmit(
            ark_lcd_config.spi_id,
            &memory_write,
            1U,
            ST7789_COMMAND_TIMEOUT_MS) != 1) {
        st7789_select(false);
        return false;
    }
    st7789_gpio_set(&ark_lcd_config.DC, true);
    return true;
}

bool st7789_write_pixels(const uint8_t *rgb565_be, size_t size) {
    if ((rgb565_be == NULL) ||
        (size == 0U) ||
        (size > ST7789_MAX_DMA_TRANSFER_SIZE)) {
        return false;
    }
    return ark_hal_spi.transmit_dma(
               ark_lcd_config.spi_id,
               rgb565_be,
               size,
               ST7789_DMA_TIMEOUT_MS) == (int32_t)size;
}

void st7789_end_region(void) {
    st7789_select(false);
}

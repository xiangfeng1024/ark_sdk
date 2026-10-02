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

#ifndef ARK_COMPONENT_LCD_H
#define ARK_COMPONENT_LCD_H

#include <stdbool.h>
#include <stdint.h>

#define LCD_WIDTH 240U
#define LCD_HEIGHT 320U

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint8_t *rgb565_be;
} lcd_surface_t;

typedef bool (*lcd_region_renderer_t)(lcd_surface_t *surface, void *context);

typedef struct {
    bool initialized;
    uint32_t region_sequence;
    uint32_t transfer_failures;
    uint32_t dma_bytes;
} lcd_diagnostics_t;

bool lcd_register(void);
bool lcd_clear(uint16_t color);
bool lcd_fill_rect(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    uint16_t color);
bool lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color);
bool lcd_draw_line(
    int16_t x0,
    int16_t y0,
    int16_t x1,
    int16_t y1,
    uint16_t color,
    uint16_t background);
int lcd_printf(
    uint16_t x,
    uint16_t y,
    uint8_t scale,
    uint16_t foreground,
    uint16_t background,
    const char *format,
    ...);
bool lcd_refresh_region(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    lcd_region_renderer_t renderer,
    void *context);
void lcd_surface_clear(lcd_surface_t *surface, uint16_t color);
void lcd_surface_draw_pixel(
    lcd_surface_t *surface,
    int16_t x,
    int16_t y,
    uint16_t color);
void lcd_surface_draw_line(
    lcd_surface_t *surface,
    int16_t x0,
    int16_t y0,
    int16_t x1,
    int16_t y1,
    uint16_t color);
bool lcd_get_diagnostics(lcd_diagnostics_t *diagnostics);

#endif /* ARK_COMPONENT_LCD_H */

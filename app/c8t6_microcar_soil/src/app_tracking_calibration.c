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

#include "app_tracking_calibration.h"

#include <stddef.h>
#include <string.h>

#include "adc38_tracking.h"
#include "flash.h"
#include "maixcam.h"

#define APP_TRACKING_CALIBRATION_MAGIC 0x314B5254UL
#define APP_TRACKING_CALIBRATION_VERSION 1U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    adc38_tracking_calibration_t calibration;
    uint32_t crc32;
} app_tracking_calibration_record_t;

static const flash_device_t *app_tracking_calibration_flash;
static uint32_t app_tracking_calibration_last_sequence;
static bool app_tracking_calibration_white_captured;
static bool app_tracking_calibration_black_captured;
static app_tracking_calibration_record_t app_tracking_calibration_record;
static adc38_tracking_calibration_t app_tracking_calibration_buffer;
static uint16_t app_tracking_calibration_values[ADC38_TRACKING_CHANNEL_COUNT];

static uint32_t app_tracking_calibration_crc32(
    const void *data,
    size_t size) {
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFUL;
    size_t index;

    for (index = 0U; index < size; ++index) {
        uint8_t bit;

        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^
                  ((crc & 1U) ? 0xEDB88320UL : 0U);
        }
    }
    return ~crc;
}

static void app_tracking_calibration_defaults(
    adc38_tracking_calibration_t *calibration) {
    if (!adc38_tracking_get_calibration(calibration)) {
        memset(calibration, 0, sizeof(*calibration));
    }
}

static bool app_tracking_calibration_read_record(
    adc38_tracking_calibration_t *calibration) {
    uint32_t expected_crc;

    if ((app_tracking_calibration_flash == NULL) ||
        !flash_read(
            app_tracking_calibration_flash,
            0U,
            &app_tracking_calibration_record,
            sizeof(app_tracking_calibration_record)) ||
        (app_tracking_calibration_record.magic != APP_TRACKING_CALIBRATION_MAGIC) ||
        (app_tracking_calibration_record.version != APP_TRACKING_CALIBRATION_VERSION) ||
        (app_tracking_calibration_record.size !=
         sizeof(app_tracking_calibration_record.calibration))) {
        return false;
    }
    expected_crc = app_tracking_calibration_crc32(
        &app_tracking_calibration_record,
        offsetof(app_tracking_calibration_record_t, crc32));
    if ((app_tracking_calibration_record.crc32 != expected_crc) ||
        !adc38_tracking_set_calibration(
            &app_tracking_calibration_record.calibration)) {
        return false;
    }
    *calibration = app_tracking_calibration_record.calibration;
    return true;
}

static bool app_tracking_calibration_write_record(void) {
    memset(&app_tracking_calibration_record, 0xFF,
           sizeof(app_tracking_calibration_record));
    app_tracking_calibration_record.magic = APP_TRACKING_CALIBRATION_MAGIC;
    app_tracking_calibration_record.version = APP_TRACKING_CALIBRATION_VERSION;
    app_tracking_calibration_record.size =
        sizeof(app_tracking_calibration_record.calibration);
    if (!adc38_tracking_get_calibration(
            &app_tracking_calibration_record.calibration)) {
        return false;
    }
    app_tracking_calibration_record.crc32 = app_tracking_calibration_crc32(
        &app_tracking_calibration_record,
        offsetof(app_tracking_calibration_record_t, crc32));
    return (app_tracking_calibration_flash != NULL) &&
           flash_write(
        app_tracking_calibration_flash,
        0U,
        &app_tracking_calibration_record,
        sizeof(app_tracking_calibration_record));
}

static void app_tracking_calibration_send_values(
    uint8_t command,
    uint8_t status,
    const uint16_t values[ADC38_TRACKING_CHANNEL_COUNT]) {
    (void)maixcam_send_response(
        command,
        status,
        values,
        sizeof(uint16_t) * ADC38_TRACKING_CHANNEL_COUNT);
}

void app_tracking_calibration_init(void) {
    app_tracking_calibration_flash = flash_find("internal");
    if (app_tracking_calibration_flash == NULL) {
        app_tracking_calibration_flash = flash_default();
    }
    if (!app_tracking_calibration_read_record(&app_tracking_calibration_buffer)) {
        app_tracking_calibration_defaults(&app_tracking_calibration_buffer);
        (void)adc38_tracking_set_calibration(&app_tracking_calibration_buffer);
    }
}

void app_tracking_calibration_step(bool commands_enabled) {
    maixcam_command_request_t request;
    uint8_t status = MAIXCAM_STATUS_OK;

    if (!maixcam_get_command(&request) ||
        (request.sequence == app_tracking_calibration_last_sequence)) {
        return;
    }
    app_tracking_calibration_last_sequence = request.sequence;
    if ((request.command < MAIXCAM_COMMAND_CAL_WHITE) ||
        (request.command > MAIXCAM_COMMAND_FLASH_READ)) {
        return;
    }
    if (!commands_enabled) {
        (void)maixcam_send_response(
            request.command, MAIXCAM_STATUS_ERROR, NULL, 0U);
        return;
    }
    switch (request.command) {
    case MAIXCAM_COMMAND_CAL_WHITE:
        memset(app_tracking_calibration_values, 0,
               sizeof(app_tracking_calibration_values));
        status = adc38_tracking_capture_white(app_tracking_calibration_values) ?
            MAIXCAM_STATUS_OK : MAIXCAM_STATUS_ERROR;
        if (status == MAIXCAM_STATUS_OK) {
            app_tracking_calibration_white_captured = true;
        }
        app_tracking_calibration_send_values(
            request.command, status, app_tracking_calibration_values);
        break;
    case MAIXCAM_COMMAND_CAL_BLACK:
        memset(app_tracking_calibration_values, 0,
               sizeof(app_tracking_calibration_values));
        status = adc38_tracking_capture_black(app_tracking_calibration_values) ?
            MAIXCAM_STATUS_OK : MAIXCAM_STATUS_ERROR;
        if (status == MAIXCAM_STATUS_OK) {
            app_tracking_calibration_black_captured = true;
        }
        app_tracking_calibration_send_values(
            request.command, status, app_tracking_calibration_values);
        break;
    case MAIXCAM_COMMAND_FLASH_WRITE:
        status = (app_tracking_calibration_white_captured &&
                  app_tracking_calibration_black_captured &&
                  app_tracking_calibration_write_record()) ?
            MAIXCAM_STATUS_OK : MAIXCAM_STATUS_ERROR;
        (void)maixcam_send_response(request.command, status, NULL, 0U);
        break;
    case MAIXCAM_COMMAND_FLASH_READ:
        if (!app_tracking_calibration_read_record(&app_tracking_calibration_buffer)) {
            app_tracking_calibration_defaults(&app_tracking_calibration_buffer);
            (void)adc38_tracking_set_calibration(&app_tracking_calibration_buffer);
            status = MAIXCAM_STATUS_DEFAULTS;
        }
        (void)maixcam_send_response(
            request.command,
            status,
            &app_tracking_calibration_buffer,
            sizeof(app_tracking_calibration_buffer));
        break;
    default:
        break;
    }
}

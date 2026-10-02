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

#ifndef ARK_COMPONENT_MAIXCAM_H
#define ARK_COMPONENT_MAIXCAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAIXCAM_COMMAND_FLOWER 1U
#define MAIXCAM_COMMAND_CAL_WHITE 2U
#define MAIXCAM_COMMAND_CAL_BLACK 3U
#define MAIXCAM_COMMAND_FLASH_WRITE 4U
#define MAIXCAM_COMMAND_FLASH_READ 5U
#define MAIXCAM_COMMAND_START_TASK 6U
#define MAIXCAM_COMMAND_TASK_READY 7U
#define MAIXCAM_STATUS_OK 0U
#define MAIXCAM_STATUS_DEFAULTS 1U
#define MAIXCAM_STATUS_ERROR 2U
#define MAIXCAM_MAX_PAYLOAD_SIZE 32U

typedef struct {
    bool detected;
    uint16_t center_x;
    uint16_t center_y;
    uint16_t width;
    uint16_t height;
    uint32_t tick;
} maixcam_result_t;

typedef struct {
    uint8_t command;
    uint32_t sequence;
    uint32_t tick;
} maixcam_command_request_t;

bool maixcam_register(void);
bool maixcam_get_result(maixcam_result_t *result);
bool maixcam_get_command(maixcam_command_request_t *request);
bool maixcam_send_response(
    uint8_t command,
    uint8_t status,
    const void *payload,
    size_t payload_size);
bool maixcam_notify_task_ready(void);
int32_t maixcam_read_raw(uint8_t *data, size_t size);

#endif /* ARK_COMPONENT_MAIXCAM_H */

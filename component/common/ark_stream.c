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

#include "ark_stream.h"

#include <string.h>

#define ARK_STREAM_MAX_COUNT 4U

static ark_stream_t *ark_streams[ARK_STREAM_MAX_COUNT];

bool ark_stream_attach(ark_stream_t *stream) {
    size_t index;

    if ((stream == NULL) || (stream->name == NULL) ||
        (stream->name[0] == '\0') || (stream->operations == NULL) ||
        (stream->operations->read == NULL) ||
        (stream->operations->write == NULL)) {
        return false;
    }

    for (index = 0U; index < ARK_STREAM_MAX_COUNT; ++index) {
        if ((ark_streams[index] == stream) ||
            ((ark_streams[index] != NULL) &&
             (strcmp(ark_streams[index]->name, stream->name) == 0))) {
            return false;
        }
    }

    for (index = 0U; index < ARK_STREAM_MAX_COUNT; ++index) {
        if (ark_streams[index] == NULL) {
            ark_streams[index] = stream;
            return true;
        }
    }

    return false;
}

bool ark_stream_detach(ark_stream_t *stream) {
    size_t index;

    for (index = 0U; index < ARK_STREAM_MAX_COUNT; ++index) {
        if (ark_streams[index] == stream) {
            ark_streams[index] = NULL;
            return true;
        }
    }

    return false;
}

ark_stream_t *ark_stream_find(const char *name) {
    size_t index;

    if (name == NULL) {
        return NULL;
    }

    for (index = 0U; index < ARK_STREAM_MAX_COUNT; ++index) {
        if ((ark_streams[index] != NULL) &&
            (strcmp(ark_streams[index]->name, name) == 0)) {
            return ark_streams[index];
        }
    }

    return NULL;
}

bool ark_stream_is_ready(const ark_stream_t *stream) {
    if ((stream == NULL) || (stream->operations == NULL)) {
        return false;
    }
    if (stream->operations->is_ready == NULL) {
        return true;
    }
    return stream->operations->is_ready(stream->context);
}

int32_t ark_stream_read(
    const ark_stream_t *stream,
    uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    if ((stream == NULL) || (stream->operations == NULL) ||
        (stream->operations->read == NULL) || (data == NULL) || (size == 0U)) {
        return -1;
    }
    return stream->operations->read(stream->context, data, size, timeout_ms);
}

int32_t ark_stream_write(
    const ark_stream_t *stream,
    const uint8_t *data,
    size_t size,
    uint32_t timeout_ms) {
    if ((stream == NULL) || (stream->operations == NULL) ||
        (stream->operations->write == NULL) || (data == NULL) || (size == 0U)) {
        return -1;
    }
    return stream->operations->write(stream->context, data, size, timeout_ms);
}

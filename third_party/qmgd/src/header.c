/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "qmgd/decode.h"
#include <string.h>

static uint16_t rd_be16(const uint8_t *p) { return (p[0] << 8) | p[1]; }
static uint16_t rd_le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int qmage_parse_header(const uint8_t *data, size_t size, QmageHeader *h) {
    size_t pos = 0;
    int flags4, flags5, flags10, flags11;

    memset(h, 0, sizeof(*h));

    if (size < 12) return -1;

    if (rd_be16(data + pos) != QMAGE_MAGIC) return -1;
    pos += 2;

    h->qversion = data[pos++];
    h->raw_type = data[pos++];
    switch (h->raw_type) {
        case 0: h->transparency = 0; break; /* RGB565 */
        case 3: case 6: h->transparency = 1; break; /* RGBA5658 / RGBA */
        default: return -1;
    }

    flags4 = data[pos++];
    h->qp = flags4 & 0x1f;
    h->not_comp = !!(flags4 & 0x20);
    h->use_chroma_key = !!(flags4 & 0x40);
    h->mode = !!(flags4 & 0x80);

    flags5 = data[pos++];
    if (h->qversion == QVERSION_1_43_LESS)
        h->encoder_mode = flags5 & 0x7;
    else if (h->qversion > QVERSION_1_43_LESS)
        h->encoder_mode = flags5 & 0xf;
    else
        h->encoder_mode = 0;
    h->is_dynamic_table = (h->qversion > QVERSION_1_43_LESS) ? !!(flags5 & 0x10) : 0;
    h->alpha_depth = (flags5 & 0x20) ? 2 : 1;
    h->depth = (flags5 & 0x40) ? 2 : 1;
    h->use_extra_exception = !!(flags5 & 0x80);

    if (pos + 4 > size) return -1;
    h->width = rd_le16(data + pos); pos += 2;
    h->height = rd_le16(data + pos); pos += 2;

    if (pos + 2 > size) return -1;
    flags10 = data[pos++];
    h->near_lossless = !!(flags10 & 0x40);

    flags11 = data[pos++];
    h->android_support = !!(flags11 & 0x4);
    h->is_gray_type = !!(flags11 & 0x4);
    h->use_index_color = !!(flags11 & 0x8);
    h->pre_multiplied = !!(flags11 & 0x10);
    h->not_alpha_comp = !!(flags11 & 0x40);
    h->is_opaque = !!(flags11 & 0x20);
    h->nine_patched = !!(flags11 & 0x80);

    h->alpha_position = -1;
    if (h->qversion == QVERSION_1_43_LESS) {
        if (h->transparency || h->mode) {
            if (pos + 4 > size) return -1;
            h->alpha_position = (int)rd_le32(data + pos); pos += 4;
        }
        h->alpha_encoder_mode = h->encoder_mode;
    } else if (h->qversion > QVERSION_1_43_LESS) {
        int flags14;
        if (pos + 4 > size) return -1;
        h->alpha_position = rd_le16(data + pos); pos += 2;
        flags14 = data[pos++];
        h->alpha_encoder_mode = flags14 & 0xf;
        pos += 1;
    }

    h->total_frame_number = h->current_frame_number = 1;
    if (h->mode) {
        if (pos + 8 > size) return -1;
        h->total_frame_number = rd_le16(data + pos); pos += 2;
        h->current_frame_number = rd_le16(data + pos); pos += 2;
        h->animation_delay_time = rd_le16(data + pos); pos += 2;
        h->animation_no_repeat = data[pos++];
        pos += 1;
    }

    if (h->qversion > QVERSION_1_43_LESS) {
        if (!h->mode || h->current_frame_number <= 1) {
            if (h->alpha_position >= 0)
                h->alpha_position *= 4;
        }
    }

    h->header_size = h->mode ? 24 : (h->transparency ? 16 : 12);

    if (h->use_index_color) {
        if (h->nine_patched) pos += 4;
        if (pos + 4 > size) return -1;
        h->color_count = (int)rd_le32(data + pos); pos += 4;
    }

    h->parsed_bytes = (int)pos;
    return 0;
}

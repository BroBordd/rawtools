/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "qmgd/decode.h"
#include "qmgd/bitreader.h"
#include "tables.h"
#include <stdlib.h>
#include <string.h>

static int read_value(ByteReader *gb) {
    int v = 0;
    while (yr_peek_byte(gb) == 0xff) {
        yr_get_byte(gb);
        v += 0xff;
    }
    return v + yr_get_byte(gb);
}

static int decode_w2_aligned(int width, int height, ByteReader *gb1, ByteReader *gb2, ByteReader *gb3,
                              const uint8_t *data, size_t size, uint8_t *dst) {
    int counter = 0;
    int dim = width * height * 2;
    while (1) {
        int idx = read_value(gb1);
        if (idx == 0) {
            uint32_t val = yr_get_le32(gb3);
            wr32(dst + counter, val);
            counter += 4;
        } else {
            uint32_t val;
            int run, n, i;
            idx--;
            if ((size_t)idx * 4 + 4 > size - 16) return -1;
            val = rd32(data + 16 + idx * 4);
            run = read_value(gb2) + 1;
            n = run;
            if (n > (dim - counter) / 4) n = (dim - counter) / 4;
            for (i = 0; i < n; i++) wr32(dst + counter + i * 4, val);
            counter += 4 * run;
        }
        if (counter >= dim) break;
    }
    return 0;
}

int qmage_decode_w2_pass_depth1(const QmageHeader *h, const uint8_t *data, size_t size, uint8_t *dst) {
    uint32_t cnt_table, size_idx, size_run;
    size_t start1, start2, start3;
    ByteReader gb1, gb2, gb3;

    if (size < 16) return -1;
    cnt_table = rd32(data);
    size_idx = rd32(data + 4);
    size_run = rd32(data + 8);

    start1 = 16 + (size_t)cnt_table * 4;
    start2 = start1 + size_idx;
    start3 = start2 + size_run;

    if (start1 >= size || start2 >= size || start3 > size) return -1;

    yr_init(&gb1, data + start1, size - start1);
    yr_init(&gb2, data + start2, size - start2);
    yr_init(&gb3, data + start3, size - start3);

    return decode_w2_aligned(h->width, h->height, &gb1, &gb2, &gb3, data, size, dst);
}

static int strip1(BitReader *gb1, ByteReader *gb2, ByteReader *gb3, int *rel, uint8_t *dst, int d_pos) {
    int i;
    uint32_t val = yr_get_le32(gb3);
    wr32(dst + d_pos, val);
    d_pos += 4;
    for (i = 0; i < 6; i++) {
        uint16_t v;
        if (!(i & 1)) {
            if (!br_get_bits1(gb1))
                *rel = br_get_bits1(gb1) ? yr_get_byte(gb2) : yr_get_le16(gb3);
        }
        if (!br_get_bits1(gb1)) {
            if (!br_get_bits1(gb1)) {
                int pos = d_pos - *rel * 2;
                if (pos < 0) return -1;
                v = rd16(dst + pos) ^ qmage_diff[yr_get_byte(gb2)];
            } else {
                v = yr_get_le16(gb3);
            }
        } else {
            int pos = d_pos - *rel * 2;
            if (pos < 0) return -1;
            v = rd16(dst + pos);
        }
        wr16(dst + d_pos, v);
        d_pos += 2;
    }
    return d_pos;
}

static int strip2(BitReader *gb1, ByteReader *gb2, ByteReader *gb3, int *rel, uint8_t *dst, int d_pos) {
    int i;
    int mask = yr_get_byte(gb2);
    for (i = 0; i < 8; i++) {
        uint16_t v;
        if (!(i & 1)) {
            if (!br_get_bits1(gb1))
                *rel = br_get_bits1(gb1) ? yr_get_byte(gb2) : yr_get_le16(gb3);
        }
        if (!(mask & (1 << (7 - i)))) {
            if (!br_get_bits1(gb1)) {
                int pos = d_pos - *rel * 2;
                if (pos < 0) return -1;
                v = rd16(dst + pos) ^ qmage_diff[yr_get_byte(gb2)];
            } else {
                v = yr_get_le16(gb3);
            }
        } else {
            int pos = d_pos - *rel * 2;
            if (pos < 0) return -1;
            v = rd16(dst + pos);
        }
        wr16(dst + d_pos, v);
        d_pos += 2;
    }
    return d_pos;
}

int qmage_decode_w2_pass_depth2(const QmageHeader *h, const uint8_t *data, size_t size, uint8_t *dst) {
    uint32_t bsize, len1, len2;
    uint8_t *bdata;
    int rel = 1, d_pos, limit, ret;
    BitReader gb1;
    ByteReader gb2, gb3;

    if (size < 12) return -1;
    bsize = rd32(data);
    if (bsize < 16) return -1;
    len1 = rd32(data + 4);
    len2 = rd32(data + 8);

    bdata = (uint8_t *)calloc(1, bsize);
    if (!bdata) return -1;

    br_init(&gb1, data + 12, size - 12);
    yr_init(&gb2, data + 12 + len1, size - 12 - len1);
    yr_init(&gb3, data + 12 + len1 + len2, size - 12 - len1 - len2);

    d_pos = strip1(&gb1, &gb2, &gb3, &rel, bdata, 0);
    if (d_pos < 0) { free(bdata); return -1; }

    limit = (int)(bsize & ~15u);
    while (d_pos < limit) {
        if (!br_get_bits1(&gb1)) {
            if (!br_get_bits1(&gb1)) {
                yr_get_buffer(&gb3, bdata + d_pos, 16);
            } else {
                int j;
                if (d_pos - rel * 2 < 0) { free(bdata); return -1; }
                for (j = 0; j < 8; j++) {
                    uint16_t v = rd16(bdata + d_pos - rel * 2 + j * 2);
                    wr16(bdata + d_pos + j * 2, v);
                }
            }
            d_pos += 16;
        } else {
            d_pos = strip2(&gb1, &gb2, &gb3, &rel, bdata, d_pos);
            if (d_pos < 0) { free(bdata); return -1; }
        }
    }

    if (bsize & 15) {
        int rem = bsize & 15;
        yr_get_buffer(&gb2, bdata + d_pos, rem);
    }

    ret = qmage_decode_w2_pass_depth1(h, bdata, bsize, dst);
    free(bdata);
    return ret;
}

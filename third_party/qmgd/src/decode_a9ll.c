/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "qmgd/decode.h"
#include "qmgd/bitreader.h"
#include "tables.h"
#include <string.h>
#include <stdio.h>

#ifdef QMGD_DEBUG
#define QMGD_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define QMGD_LOG(...) ((void)0)
#endif

static uint16_t get_pixel(const uint8_t *src, int linesize, int width, int height, int x, int y) {
    if (x >= 0 && x < width && y >= 0 && y < height)
        return rd16(src + (size_t)y * linesize + (size_t)x * 2);
    return 0;
}

static void copy_edge(uint8_t *dst, int linesize, int x0, int y0, int w, int h) {
    int i, j;
    for (j = 0; j < h; j++) {
        uint16_t src = rd16(dst + (size_t)(y0 + j) * linesize + (size_t)x0 * 2 - 2);
        for (i = 0; i < w; i++)
            wr16(dst + (size_t)(y0 + j) * linesize + (size_t)(x0 + i) * 2, src);
    }
}

static void copy_block_generic(uint8_t *dst, int dst_linesize, const uint8_t *ref, int ref_linesize, int n) {
    int i, j;
    for (j = 0; j < n; j++)
        for (i = 0; i < n; i++)
            wr16(dst + (size_t)j * dst_linesize + (size_t)i * 2, rd16(ref + (size_t)j * ref_linesize + (size_t)i * 2));
}

/* ===================== KEYFRAME (decode_a9ll) =====================
 * gb1, gb2 = BitReader (both bit contexts); gb3 = ByteReader.
 * Matches upstream decode_a9ll + its decode_pixel_inter exactly. */

static void decode_pixel_inter_kf(int copy, const uint16_t *ori_delta, BitReader *gb1, BitReader *gb2, ByteReader *gb3,
                                   uint8_t *dst_px, const uint8_t *ref, int ref_linesize, int width, int height,
                                   int ref_x, int ref_y) {
    if (copy) {
        wr16(dst_px, get_pixel(ref, ref_linesize, width, height, ref_x, ref_y));
    } else {
        int nb_bits = (int)br_get_bits(gb2, 3);
        if (nb_bits == 7) {
            wr16(dst_px, yr_get_le16(gb3));
        } else {
            int idx = (int)br_get_bits(gb1, nb_bits + 1);
            uint16_t delta = ori_delta[idx + (2 << nb_bits) - 2];
            uint16_t base = get_pixel(ref, ref_linesize, width, height, ref_x, ref_y);
            wr16(dst_px, (uint16_t)(base + delta));
        }
    }
}

int qmage_decode_a9ll(const QmageHeader *h, const uint8_t *data, size_t size, uint8_t *dst) {
    int header_size = h->header_size;
    uint32_t gb1_start, gb3_start;
    BitReader gb1, gb2;
    ByteReader gb3;
    const uint16_t *ori_delta;
    uint16_t ori_delta_local[512];
    int linesize = h->width * 2;
    int x, y;

    if (size < (size_t)header_size + 8) { QMGD_LOG("[a9ll] size too small (%zu < %d)\n", size, header_size + 8); return -1; }
    gb1_start = rd32(data + header_size);
    gb3_start = rd32(data + header_size + 4);
    if (gb1_start < (uint32_t)header_size + 8 || gb1_start > size) { QMGD_LOG("[a9ll] gb1_start OOB: %u (size=%zu)\n", gb1_start, size); return -1; }
    if (gb3_start < (uint32_t)header_size + 8 || gb3_start > size) { QMGD_LOG("[a9ll] gb3_start OOB: %u (size=%zu)\n", gb3_start, size); return -1; }

    br_init(&gb1, data + header_size + 8, size - header_size - 8);
    br_init(&gb2, data + gb1_start, size - gb1_start);
    yr_init(&gb3, data + gb3_start, size - gb3_start);

    if (h->is_dynamic_table) {
        uint8_t sign[512];
        int i;
        for (i = 0; i < 512; i++) sign[i] = yr_get_byte(&gb3);
        for (i = 0; i < 512; i++) {
            uint16_t v = yr_get_le16(&gb3);
            ori_delta_local[i] = sign[i] ? v : (uint16_t)(-(int)v);
        }
        ori_delta = ori_delta_local + 1;
    } else {
        ori_delta = qmage_ori_delta[h->qversion != QVERSION_1_43_LESS];
    }

    if (h->use_extra_exception) { QMGD_LOG("[a9ll] use_extra_exception not implemented\n"); return -1; }

    for (y = 0; y < h->height; y += 4) {
        for (x = 0; x < h->width; x += 4) {
            int mode = (int)br_get_bits(&gb1, 2);
            if (mode < 3) {
                uint16_t cbp = yr_get_le16(&gb3);
                int k = 0, i, j;
                for (j = 0; j < 4; j++) {
                    for (i = 0; i < 4; i++) {
                        if (x + i < h->width && y + j < h->height) {
                            int copy = !!(cbp & (1 << k));
                            int ref_x = x + i + qmage_dir[mode].x;
                            int ref_y = y + j + qmage_dir[mode].y;
                            size_t off = (size_t)(y + j) * linesize + (size_t)(x + i) * 2;
                            decode_pixel_inter_kf(copy, ori_delta, &gb1, &gb2, &gb3,
                                                   dst + off, dst, linesize, h->width, h->height, ref_x, ref_y);
                            k++;
                        }
                    }
                }
            } else {
                if (x > 0) {
                    int cw = h->width - x; if (cw > 4) cw = 4;
                    int ch = h->height - y; if (ch > 4) ch = 4;
                    copy_edge(dst, linesize, x, y, cw, ch);
                }
            }
        }
    }
    return 0;
}

/* ===================== INTER-FRAME (decode_a9ll_ani) =====================
 * gb1 = BitReader, gb2 = ByteReader (this differs from the keyframe path --
 * decode_pixel() upstream takes GetBitContext *gb1, GetByteContext *gb2,
 * using gb1 for the skip flag AND the nb_bits/index, gb2 only for literal
 * 16-bit reads). */

static void decode_pixel(const uint16_t *ori_delta, BitReader *gb1, ByteReader *gb2,
                          uint8_t *dst_px, const uint8_t *ref, int ref_linesize, int width, int height,
                          int ref_x, int ref_y) {
    int skip = br_get_bits1(gb1);
    if (skip) {
        wr16(dst_px, get_pixel(ref, ref_linesize, width, height, ref_x, ref_y));
    } else {
        int nb_bits = (int)br_get_bits(gb1, 3);
        if (nb_bits == 7) {
            wr16(dst_px, yr_get_le16(gb2));
        } else {
            int idx = (int)br_get_bits(gb1, nb_bits + 1);
            uint16_t delta = ori_delta[idx + (2 << nb_bits) - 2];
            uint16_t base = get_pixel(ref, ref_linesize, width, height, ref_x, ref_y);
            wr16(dst_px, (uint16_t)(base + delta));
        }
    }
}

static void decode_block3_ani(const QmageHeader *h, BitReader *gb1, ByteReader *gb2, int x, int y,
                               uint8_t *dst, int linesize, const uint8_t *ref, int ref_linesize,
                               int mv_x, int mv_y, const uint16_t *ori_delta) {
    int mode = (int)br_get_bits(gb1, 3);
    if (h->qp == 0 || br_get_bits1(gb1)) {
        int i, j;
        if (mode < 3) {
            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++)
                    decode_pixel(ori_delta, gb1, gb2,
                                 dst + (size_t)(y + j) * linesize + (size_t)(x + i) * 2,
                                 dst, linesize, h->width, h->height,
                                 x + i + qmage_dir[mode].x, y + j + qmage_dir[mode].y);
        } else if (mode == 3) {
            if (x > 0) copy_edge(dst, linesize, x, y, 4, 4);
        } else if (mode == 4) {
            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++)
                    decode_pixel(ori_delta, gb1, gb2,
                                 dst + (size_t)(y + j) * linesize + (size_t)(x + i) * 2,
                                 ref, ref_linesize, h->width, h->height, x + i, y + j);
        } else if (mode == 5) {
            copy_block_generic(dst + (size_t)y * linesize + (size_t)x * 2, linesize,
                                ref + (size_t)y * ref_linesize + (size_t)x * 2, ref_linesize, 4);
        } else if (mode == 6) {
            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++)
                    decode_pixel(ori_delta, gb1, gb2,
                                 dst + (size_t)(y + j) * linesize + (size_t)(x + i) * 2,
                                 ref, ref_linesize, h->width, h->height, x + i + mv_x, y + j + mv_y);
        } else {
            if (x + mv_x < 0 || x + mv_x + 4 > h->width || y + mv_y < 0 || y + mv_y + 4 > h->height)
                return;
            copy_block_generic(dst + (size_t)y * linesize + (size_t)x * 2, linesize,
                                ref + (size_t)(y + mv_y) * ref_linesize + (size_t)(x + mv_x) * 2, ref_linesize, 4);
        }
    }
}

static void decode_block2_ani(const QmageHeader *h, BitReader *gb1, ByteReader *gb2, int x, int y,
                               uint8_t *dst, int linesize, const uint16_t *ori_delta) {
    int mode = (int)br_get_bits(gb1, 2);
    if (h->qp == 0 || br_get_bits1(gb1)) {
        if (mode < 3) {
            int i, j;
            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++)
                    decode_pixel(ori_delta, gb1, gb2,
                                 dst + (size_t)(y + j) * linesize + (size_t)(x + i) * 2,
                                 dst, linesize, h->width, h->height,
                                 x + i + qmage_dir[mode].x, y + j + qmage_dir[mode].y);
        } else {
            if (x > 0) copy_edge(dst, linesize, x, y, 4, 4);
        }
    }
}

static int decode_mb_ani(const QmageHeader *h, BitReader *gb1, ByteReader *gb2, int x, int y,
                          uint8_t *dst, int linesize, const uint8_t *ref, int ref_linesize,
                          const uint16_t *ori_delta) {
    if (br_get_bits1(gb1)) {
        if (br_get_bits1(gb1)) {
            copy_block_generic(dst + (size_t)y * linesize + (size_t)x * 2, linesize,
                                ref + (size_t)y * ref_linesize + (size_t)x * 2, ref_linesize, 16);
        } else {
            int mv_x, mv_y, i, j;
            if (!br_get_bits1(gb1)) {
                mv_x = (int)br_get_bits(gb1, 8) - 0x7f;
                mv_y = (int)br_get_bits(gb1, 7) - 0x3f;
                if (x + mv_x < 0 || x + mv_x + 16 > h->width || y + mv_y < 0 || y + mv_y + 16 > h->height)
                    return -1;
                if (br_get_bits1(gb1)) {
                    copy_block_generic(dst + (size_t)y * linesize + (size_t)x * 2, linesize,
                                        ref + (size_t)(y + mv_y) * ref_linesize + (size_t)(x + mv_x) * 2,
                                        ref_linesize, 16);
                    return 0;
                }
            } else {
                mv_x = mv_y = 0;
            }
            for (j = 0; j < 16; j += 4)
                for (i = 0; i < 16; i += 4)
                    decode_block3_ani(h, gb1, gb2, x + i, y + j, dst, linesize, ref, ref_linesize, mv_x, mv_y, ori_delta);
        }
    } else {
        int i, j;
        for (j = 0; j < 16; j += 4)
            for (i = 0; i < 16; i += 4)
                decode_block2_ani(h, gb1, gb2, x + i, y + j, dst, linesize, ori_delta);
    }
    return 0;
}

static int decode_mbedge_ani(const QmageHeader *h, BitReader *gb1, ByteReader *gb2, int xpos, int ypos,
                              uint8_t *dst, int linesize, const uint8_t *ref, int ref_linesize,
                              const uint16_t *ori_delta) {
    int x, y;
    if (br_get_bits1(gb1)) return -1;

    for (y = ypos; y < ypos + 16 && y < h->height; y += 4) {
        for (x = xpos; x < xpos + 16 && x < h->width; x += 4) {
            if (x + 4 <= h->width && y + 4 <= h->height) {
                int mode = (int)br_get_bits(gb1, 2);
                if (mode < 3) {
                    int i, j;
                    for (j = 0; j < 4; j++)
                        for (i = 0; i < 4; i++)
                            if (x + i < h->width && y + j < h->height)
                                decode_pixel(ori_delta, gb1, gb2,
                                             dst + (size_t)(y + j) * linesize + (size_t)(x + i) * 2,
                                             dst, linesize, h->width, h->height,
                                             x + i + qmage_dir[mode].x, y + j + qmage_dir[mode].y);
                } else {
                    if (x > 0) {
                        int cw = h->width - x; if (cw > 4) cw = 4;
                        int ch = h->height - y; if (ch > 4) ch = 4;
                        copy_edge(dst, linesize, x, y, cw, ch);
                    }
                }
            } else {
                int i, j;
                for (j = 0; j < 4; j++)
                    for (i = 0; i < 4; i++)
                        if (x + i < h->width && y + j < h->height)
                            wr16(dst + (size_t)(y + j) * linesize + (size_t)(x + i) * 2, yr_get_le16(gb2));
            }
        }
    }
    return 0;
}

int qmage_decode_a9ll_ani(const QmageHeader *h, const uint8_t *data, size_t size, uint8_t *dst,
                          const uint8_t *ref, int ref_linesize) {
    int header_size = h->header_size;
    uint32_t gb1_start;
    BitReader gb1;
    ByteReader gb2;
    const uint16_t *ori_delta;
    int linesize = h->width * 2;
    int x, y;

    if (size < (size_t)header_size + 8) return -1;
    gb1_start = rd32(data + header_size);
    if (gb1_start < (uint32_t)header_size + 8 || gb1_start > size) return -1;

    br_init(&gb1, data + header_size + 8, size - header_size - 8);
    yr_init(&gb2, data + gb1_start, size - gb1_start);

    ori_delta = qmage_ori_delta[h->qversion != QVERSION_1_43_LESS];

    for (y = 0; y < h->height; y += 16) {
        for (x = 0; x < h->width; x += 16) {
            int ret;
            if (h->width - x >= 16 && h->height - y >= 16) {
                ret = decode_mb_ani(h, &gb1, &gb2, x, y, dst, linesize, ref, ref_linesize, ori_delta);
            } else {
                ret = decode_mbedge_ani(h, &gb1, &gb2, x, y, dst, linesize, ref, ref_linesize, ori_delta);
            }
            if (ret < 0) return -1;
        }
    }
    return 0;
}

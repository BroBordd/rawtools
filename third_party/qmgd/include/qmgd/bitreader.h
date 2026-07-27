/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef QMGD_BITREADER_H
#define QMGD_BITREADER_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    const uint8_t *data;
    size_t size;   /* bytes */
    size_t bitpos; /* bit position */
} BitReader;

static inline void br_init(BitReader *br, const uint8_t *data, size_t size) {
    br->data = data;
    br->size = size;
    br->bitpos = 0;
}

static inline uint32_t br_get_bits(BitReader *br, int n) {
    uint32_t val = 0;
    int i;
    for (i = 0; i < n; i++) {
        size_t byte_idx = br->bitpos >> 3;
        int bit_idx = 7 - (int)(br->bitpos & 7);
        int bit = (br->data[byte_idx] >> bit_idx) & 1;
        val = (val << 1) | (uint32_t)bit;
        br->bitpos++;
    }
    return val;
}

static inline int br_get_bits1(BitReader *br) {
    return (int)br_get_bits(br, 1);
}

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} ByteReader;

static inline void yr_init(ByteReader *yr, const uint8_t *data, size_t size) {
    yr->data = data;
    yr->size = size;
    yr->pos = 0;
}

static inline uint8_t yr_get_byte(ByteReader *yr) {
    return yr->data[yr->pos++];
}

static inline uint8_t yr_peek_byte(ByteReader *yr) {
    return yr->data[yr->pos];
}

static inline uint16_t yr_get_le16(ByteReader *yr) {
    uint16_t v = (uint16_t)(yr->data[yr->pos] | (yr->data[yr->pos + 1] << 8));
    yr->pos += 2;
    return v;
}

static inline uint32_t yr_get_le32(ByteReader *yr) {
    uint32_t v = (uint32_t)yr->data[yr->pos] |
                 ((uint32_t)yr->data[yr->pos + 1] << 8) |
                 ((uint32_t)yr->data[yr->pos + 2] << 16) |
                 ((uint32_t)yr->data[yr->pos + 3] << 24);
    yr->pos += 4;
    return v;
}

static inline void yr_get_buffer(ByteReader *yr, uint8_t *out, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) out[i] = yr->data[yr->pos + i];
    yr->pos += n;
}

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline void wr16(uint8_t *p, uint16_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; }
static inline uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void wr32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}

#endif

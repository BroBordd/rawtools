/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef QMGD_DECODE_H
#define QMGD_DECODE_H

#include <stdint.h>
#include <stddef.h>

#define QMAGE_MAGIC 0x514d
#define QVERSION_1_43_LESS 0xb
#define QCODEC_V16_SHORT_INDEX 0
#define QCODEC_W2_PASS 1

typedef struct {
    int qversion;
    int raw_type;
    int transparency;

    int qp;
    int not_comp;
    int use_chroma_key;
    int mode;

    int encoder_mode;
    int is_dynamic_table;
    int alpha_depth;
    int depth;
    int use_extra_exception;

    int width;
    int height;

    int near_lossless;

    int android_support;
    int is_gray_type;
    int use_index_color;
    int pre_multiplied;
    int not_alpha_comp;
    int is_opaque;
    int nine_patched;

    int alpha_position;
    int alpha_encoder_mode;

    int total_frame_number;
    int current_frame_number;
    int animation_delay_time;
    int animation_no_repeat;

    int header_size;
    int color_count;

    int parsed_bytes; /* how many header bytes we consumed */
} QmageHeader;

/* Returns 0 on success, -1 on error. data/size = the full frame record. */
int qmage_parse_header(const uint8_t *data, size_t size, QmageHeader *h);

/* Decoders. All write RGB565 (2 bytes/pixel), row-major, tightly packed
 * (linesize == width*2), into a caller-allocated buffer of width*height*2 bytes.
 *
 * decode_w2_pass_depth1/2: for encoder_mode == QCODEC_W2_PASS, non-animation
 *   or used as the "keyframe passthrough" for depth1/2 static frames.
 * decode_a9ll: keyframe decoder (encoder_mode == QCODEC_V16_SHORT_INDEX),
 *   used for animation frame 1 and static V16_SHORT_INDEX images.
 * decode_a9ll_ani: inter-frame decoder for animation frames 2..N, referencing
 *   the previously decoded frame.
 *
 * data/size for the decode_* functions = the FULL packet (avpkt->data/size
 * equivalent), matching the real decoder's calling convention exactly.
 */
int qmage_decode_w2_pass_depth1(const QmageHeader *h, const uint8_t *data, size_t size, uint8_t *dst);
int qmage_decode_w2_pass_depth2(const QmageHeader *h, const uint8_t *data, size_t size, uint8_t *dst);
int qmage_decode_a9ll(const QmageHeader *h, const uint8_t *data, size_t size, uint8_t *dst);
int qmage_decode_a9ll_ani(const QmageHeader *h, const uint8_t *data, size_t size, uint8_t *dst,
                          const uint8_t *ref, int ref_linesize);

/* High-level: decode one frame record (data/size = exactly one QM...record's
 * bytes, e.g. one slice of a concatenated multi-frame file). ref may be NULL
 * for keyframes. dst must be width*height*2 bytes. */
int qmage_decode_frame(const uint8_t *data, size_t size, QmageHeader *h_out, uint8_t *dst,
                       const uint8_t *ref, int ref_linesize);

#endif

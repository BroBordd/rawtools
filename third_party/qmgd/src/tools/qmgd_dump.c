/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "qmgd/decode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long size;
    if (!f) { perror("fopen"); return NULL; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (uint8_t *)malloc(size);
    if (fread(buf, 1, size, f) != (size_t)size) { fclose(f); free(buf); return NULL; }
    fclose(f);
    *size_out = (size_t)size;
    return buf;
}

static void write_ppm(const char *path, const uint8_t *rgb565, int width, int height) {
    FILE *f = fopen(path, "wb");
    int x, y;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            uint16_t v = rgb565[(y * width + x) * 2] | (rgb565[(y * width + x) * 2 + 1] << 8);
            int r = (v >> 11) & 0x1f;
            int g = (v >> 5) & 0x3f;
            int b = v & 0x1f;
            uint8_t r8 = (uint8_t)((r * 255 + 15) / 31);
            uint8_t g8 = (uint8_t)((g * 255 + 31) / 63);
            uint8_t b8 = (uint8_t)((b * 255 + 15) / 31);
            fputc(r8, f); fputc(g8, f); fputc(b8, f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *path;
    uint8_t *data;
    size_t size, pos = 0;
    int frame_no = 0;
    uint8_t *ref = NULL;
    int ref_linesize = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s file.qmg [out_prefix]\n", argv[0]);
        return 1;
    }
    path = argv[1];
    const char *out_prefix = argc > 2 ? argv[2] : "frame";

    data = read_file(path, &size);
    if (!data) return 1;

    printf("file: %s, size: %zu\n", path, size);

    while (pos < size) {
        QmageHeader h;
        uint8_t *dst = NULL;
        char outpath[256];
        int ret;

        if (qmage_parse_header(data + pos, size - pos, &h) < 0) {
            fprintf(stderr, "header parse failed at offset %zu\n", pos);
            break;
        }

        printf("frame %d @ %zu: %dx%d qversion=%d encoder_mode=%d depth=%d mode=%d "
               "total_frames=%d cur_frame=%d record_size=?\n",
               frame_no, pos, h.width, h.height, h.qversion, h.encoder_mode, h.depth,
               h.mode, h.total_frame_number, h.current_frame_number);
        printf("  raw_type=%d transparency=%d qp=%d not_comp=%d use_chroma_key=%d\n",
               h.raw_type, h.transparency, h.qp, h.not_comp, h.use_chroma_key);
        printf("  is_dynamic_table=%d alpha_depth=%d use_extra_exception=%d\n",
               h.is_dynamic_table, h.alpha_depth, h.use_extra_exception);
        printf("  near_lossless=%d use_index_color=%d pre_multiplied=%d not_alpha_comp=%d "
               "is_opaque=%d nine_patched=%d\n",
               h.near_lossless, h.use_index_color, h.pre_multiplied, h.not_alpha_comp,
               h.is_opaque, h.nine_patched);
        printf("  alpha_position=%d alpha_encoder_mode=%d animation_delay_time=%d "
               "animation_no_repeat=%d header_size=%d color_count=%d parsed_bytes=%d\n",
               h.alpha_position, h.alpha_encoder_mode, h.animation_delay_time,
               h.animation_no_repeat, h.header_size, h.color_count, h.parsed_bytes);

        /* Frame size, per the real demuxer's read_header() logic:
         * - non-mode (single-frame) files: rest of file
         * - mode (animation) files:
         *     if transparency: alpha_position + alpha_size (needs bitstream
         *       parse for keyframes -- not implemented here yet)
         *     else: alpha_position directly (this is NOT the same as the
         *       raw offset-0x0c field we print above for diagnostics --
         *       that field is something else and must not be used as the
         *       record length)
         */
        uint32_t reclen;
        if (h.mode) {
            if (h.transparency) {
                fprintf(stderr, "  transparency frame size calc not implemented yet\n");
                free(dst);
                break;
            }
            if (h.alpha_position <= h.header_size) {
                fprintf(stderr, "  invalid alpha_position (%d <= header_size %d)\n",
                        h.alpha_position, h.header_size);
                free(dst);
                break;
            }
            reclen = (uint32_t)h.alpha_position;
        } else {
            reclen = (uint32_t)(size - pos);
        }
        printf("  reclen=%u (raw 0x0c field was %u -- not used for framing)\n",
               reclen,
               (uint32_t)(data[pos+0x0c] | (data[pos+0x0d]<<8) | (data[pos+0x0e]<<16) | (data[pos+0x0f]<<24)));

        dst = (uint8_t *)malloc((size_t)h.width * h.height * 2);
        ret = qmage_decode_frame(data + pos, reclen, &h, dst, ref, ref_linesize);
        if (ret < 0) {
            fprintf(stderr, "  decode FAILED (encoder_mode=%d depth=%d cur_frame=%d)\n",
                    h.encoder_mode, h.depth, h.current_frame_number);
            free(dst);
            free(ref);
            free(data);
            return 1;
        } else {
            snprintf(outpath, sizeof(outpath), "%s_%d.ppm", out_prefix, frame_no);
            write_ppm(outpath, dst, h.width, h.height);
            printf("  decoded OK -> %s\n", outpath);
        }

        free(ref);
        ref = dst;
        ref_linesize = h.width * 2;

        pos += reclen;
        frame_no++;
        if (!h.mode) break; /* single-frame file, no more records */
    }

    free(ref);
    free(data);
    return 0;
}

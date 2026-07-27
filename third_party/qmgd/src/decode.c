/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "qmgd/decode.h"
#include <string.h>

int qmage_decode_frame(const uint8_t *data, size_t size, QmageHeader *h_out, uint8_t *dst,
                       const uint8_t *ref, int ref_linesize) {
    QmageHeader h;
    int ret;

    if (qmage_parse_header(data, size, &h) < 0) return -1;
    if (h_out) *h_out = h;

    memset(dst, 0, (size_t)h.width * h.height * 2);

    if (h.encoder_mode == QCODEC_W2_PASS) {
        if (h.depth == 1)
            ret = qmage_decode_w2_pass_depth1(&h, data + h.header_size, size - h.header_size, dst);
        else
            ret = qmage_decode_w2_pass_depth2(&h, data + h.header_size, size - h.header_size, dst);
    } else if (h.encoder_mode == QCODEC_V16_SHORT_INDEX) {
        if (h.mode && h.current_frame_number > 1) {
            if (!ref) return -1;
            ret = qmage_decode_a9ll_ani(&h, data, size, dst, ref, ref_linesize ? ref_linesize : h.width * 2);
        } else {
            ret = qmage_decode_a9ll(&h, data, size, dst);
        }
    } else {
        return -1; /* other encoder_mode values (Huffman variants etc.) not implemented yet */
    }

    return ret;
}

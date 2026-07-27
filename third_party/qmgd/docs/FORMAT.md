# QMG (Quram Qmage) file format notes

This documents what `qmgd` currently knows about the format, split into
confirmed (validated against real files) and unimplemented. If you're
extending the decoder, this is the reference.

## Provenance

The core codec logic here is a C port of Peter Ross's Qmage decoder patch
submitted to FFmpeg (`ffmpeg-devel`, Nov 2024):
`libavcodec/qmagedec.c` + `libavcodec/qmagedata.h`. That patch was the first
public, working reverse-engineering of this format; prior public knowledge
was limited to Google Project Zero's partial writeup during their 2020
Qmage/Skia security research (CVE-2020-8899), which covered the container
format but not the full pixel codec.

## Container / header (confirmed)

Every frame record starts with a header. Field layout depends on `qversion`
(byte offset 2): versions `<= 0xb` (`QVERSION_1_43_LESS`) use a slightly
different layout than versions `> 0xb`. See `qmgd_parse_header()` in
`src/header.c` for the exact bit-level breakdown -- it's a faithful port of
`decode_header()` from the FFmpeg source and has been validated against
real sample files at both qversion 11 and qversion 15.

Key gotcha we hit and fixed: for animation files (`mode == 1`), the length
of each frame record is **not** a simple fixed field you can read at a
constant offset. It's derived from `alpha_position` (itself qversion- and
mode-dependent) following the exact logic in the real demuxer
(`libavformat/qmagedec.c`'s `read_header()`):

- non-animation file: rest of the file
- animation frame, opaque (`raw_type == 0`, no alpha): `alpha_position`
  directly
- animation frame, with alpha (`raw_type == 3` or `6`): `alpha_position +
  alpha_size`, where `alpha_size` for keyframes has to be discovered by
  actually running the bitstream decoder partway (`parse_a9ll_alpha_size()`
  upstream) -- **this case is not implemented in qmgd yet**, see below.

## Pixel codecs (partially confirmed)

Dispatch is by `encoder_mode` (a header bitfield), not a marker byte inline
in the stream:

- `QCODEC_W2_PASS` (1): non-animation frames, or "flat"/simple images.
  - `depth == 1`: `qmage_decode_w2_pass_depth1()` -- confirmed correct
    against solid-color and single-pixel-exception test files.
  - `depth == 2`: `qmage_decode_w2_pass_depth2()` -- confirmed correct
    against a checkerboard test file.
- `QCODEC_V16_SHORT_INDEX` (0): used by animations and some static images.
  - keyframe (`current_frame_number <= 1`, or non-animation):
    `qmage_decode_a9ll()` -- confirmed correct against a real 9-frame
    gradient animation's first frame, pixel-for-pixel against the source
    PNG (mean/max error consistent with RGB565 quantization, not a bug).
  - inter-frame (`current_frame_number > 1`): `qmage_decode_a9ll_ani()` --
    confirmed correct against all 8 remaining frames of the same real
    animation.

## Known-unimplemented paths

These will currently return an error (`decode FAILED` from `qmgd_dump`)
rather than silently producing garbage:

- Any `encoder_mode` other than 0 or 1 (there may be pure-Huffman or other
  codec paths in real-world files we haven't needed yet).
- `use_extra_exception` (a header flag, currently rejected explicitly).
- Alpha/transparency data (`raw_type == 3` or `6`). The RGB plane decode
  logic likely works unchanged; the alpha plane itself, and the frame-length
  math needed to find where a keyframe's alpha data ends, are not ported.
- `is_dynamic_table` is implemented but has not been exercised against a
  real sample yet (lightly tested only).

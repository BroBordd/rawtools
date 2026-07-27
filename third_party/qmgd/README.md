# qmgd

A decoder for Samsung's Qmage (`.qmg`) image and boot-animation format,
built from scratch by reverse-engineering the format and cross-checking
against a real FFmpeg decoder patch.

Qmage is a proprietary Korean image codec (by Quram) that Samsung uses for
boot/shutdown animations and some UI assets. There is no official public
spec. This project's decoder is a C port of the format logic from
[Peter Ross's Qmage decoder patch](https://ffmpeg.org/pipermail/ffmpeg-devel/2024-November/336379.html)
submitted to FFmpeg in November 2024, validated against real sample files
including a real device boot animation.

See [`docs/FORMAT.md`](docs/FORMAT.md) for what's confirmed vs. not yet
implemented.

## Status

- **Decoding: working.** Header parsing, both `W2_PASS` codec paths (flat
  colors, simple palettes), and both `V16_SHORT_INDEX` codec paths (keyframe
  + animation inter-frame) are implemented and validated pixel-for-pixel
  against real files.
- Not implemented: alpha/transparency channel, a couple of rarer header
  flag combinations. See `docs/FORMAT.md` for the exact list.

## Building

With CMake (preferred):

```sh
cmake -B build
cmake --build build
./build/qmgd_dump samples/9frame_gradient_animation.qmg out
```

Without CMake:

```sh
sh build.sh
./qmgd_dump samples/9frame_gradient_animation.qmg out
```

`qmgd_dump` is a diagnostic CLI: it decodes every frame in a `.qmg` file and
writes each one out as a `.ppm` image (`out_0.ppm`, `out_1.ppm`, ...), while
printing the parsed header fields for each frame to stdout/stderr. PPM is a
trivial uncompressed format -- convert with ImageMagick
(`convert out_0.ppm out_0.png`) or open directly in GIMP.

## Repo layout

```
include/qmgd/     public headers (decode.h, bitreader.h)
src/              library implementation (header parsing, both codec paths)
src/tools/        qmgd_dump: CLI decode/dump tool built on top of the library
docs/             format notes
samples/          sample .qmg files used for validation
```

## License

GPLv3 (or later) -- see [`LICENSE`](LICENSE). Portions of the codec logic
are derived from FFmpeg's LGPL-2.1-or-later `qmagedec.c`/`qmagedata.h`,
which is license-compatible.

## Acknowledgments

- Peter Ross, for the original FFmpeg Qmage decoder patch this project's
  codec logic is built from.
- Google Project Zero / Mateusz Jurczyk, whose 2020 Qmage security research
  (CVE-2020-8899) was the first public documentation of the container
  format's existence, even though it predates the full pixel-codec
  reverse-engineering.

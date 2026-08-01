# zip2qmg

Converts an Android `bootanimation.zip` into Samsung's Qmage (`.qmg`)
boot-animation format, using `qmgc` as the encoder library.

## Usage

```sh
zip2qmg bootanimation.zip out.qmg [--delay N] [--fps N] [--resolution WxH] \
    [--fit stretch|pad] [--pad-color RRGGBB]
```

Unpacks `bootanimation.zip`, reads `desc.txt`, loads every PNG frame
across all parts in playback order, and encodes them as a single
`.qmg` animation (or a still image if there's only one frame total).

- `--delay N` sets the per-frame delay directly, in centiseconds.
- `--fps N` sets it from a frame rate instead (`delay = 100 / fps`).
  Pass only one of `--delay`/`--fps`; if neither is given, falls back
  to the fps in `desc.txt`'s first line, or 10cs if that's unset.
- `--resolution WxH` (e.g. `--resolution 1080x2400`) resizes every
  frame to that exact size before encoding. Unlike a bootanimation
  zip, a `.qmg` has no independent "fit to screen" behavior -- it's
  rendered at whatever width/height is baked into the file, so if the
  source PNGs don't already match the target device's resolution the
  animation will appear off-scale (cropped, letterboxed, or not
  filling the display) instead of stretching to fit. Pass the
  device's actual screen resolution here to bake in a full-screen fit
  and avoid that mismatch.
- `--fit` picks how `--resolution` resizes frames (requires
  `--resolution` to also be passed):
  - `stretch` (default): resize straight to the target WxH, ignoring
    aspect ratio. Distorts the image if the aspect ratio doesn't match.
  - `pad`: scale preserving aspect ratio to fit inside the target WxH,
    then center it on a full-size canvas, filling the leftover
    top/bottom or left/right margin with a solid color instead of
    stretching.
- `--pad-color RRGGBB` (e.g. `--pad-color 000000`) sets the fill color
  used by `--fit pad`. Defaults to black. Ignored with `--fit stretch`.

## Format notes: desc.txt -> qmg

`bootanimation.zip`'s `desc.txt` supports multiple parts (`p`/`c` lines,
loop counts, pauses, per-part background colors) — see the upstream
[bootanimation FORMAT.md](https://android.googlesource.com/platform/frameworks/base/+/master/cmds/bootanimation/FORMAT.md)
for the full spec.

A `.qmg` animation is flatter: one ordered sequence of frames with a
single per-frame delay (`animation_delay_time`) and a single repeat
flag (`animation_no_repeat`). There's no native concept of multiple
parts, loop counts per part, or pauses.

All parts are flattened into one frame sequence, in the order
`desc.txt` plays them (accounting for each part's own loop count).
Only the FPS from `desc.txt`'s first line and `--delay` (if passed)
map onto the qmg animation delay; per-part loop/pause/color fields
have no `.qmg` equivalent and are dropped. This is lossy for
multi-part animations — documented here rather than silently
discovered later.

## Status

Working, round-trip tested against qmgd's sample files via `qmg2zip`.
Known gap: multi-part/looping `desc.txt` is flattened (lossy).

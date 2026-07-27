# zip2qmg

Converts an Android `bootanimation.zip` into Samsung's Qmage (`.qmg`)
boot-animation format, using `qmgc` as the encoder library.

## Usage

```sh
zip2qmg bootanimation.zip out.qmg [--delay N]
```

Unpacks `bootanimation.zip`, reads `desc.txt`, loads every PNG frame
across all parts in playback order, and encodes them as a single
`.qmg` animation (or a still image if there's only one frame total).

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

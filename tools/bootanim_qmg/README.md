# bootanim_qmg

Converts between Android's stock `bootanimation.zip` format and Samsung's
Qmage (`.qmg`) boot-animation format, using `qmgc` (encode) and `qmgd`
(decode) as libraries.

## Usage

```sh
bootanim_qmg encode bootanimation.zip out.qmg [--delay N]
bootanim_qmg decode in.qmg bootanimation.zip [--fps N]
```

- `encode`: unpacks `bootanimation.zip`, reads `desc.txt`, loads every PNG
  frame across all parts in playback order, and encodes them as a single
  `.qmg` animation (or a still image if there's only one frame total).
- `decode`: decodes every frame of a `.qmg` file, writes them out as PNGs
  under `part0/`, generates a `desc.txt` (single part, infinite loop by
  default), and repacks everything into an uncompressed `bootanimation.zip`.

## Format notes: desc.txt <-> qmg

`bootanimation.zip`'s `desc.txt` supports multiple parts (`p`/`c` lines,
loop counts, pauses, per-part background colors) — see
[`../../third_party/qmgd/docs/FORMAT.md`](../../third_party/qmgd/docs/FORMAT.md)
and the upstream
[bootanimation FORMAT.md](https://android.googlesource.com/platform/frameworks/base/+/master/cmds/bootanimation/FORMAT.md)
for the full spec.

A `.qmg` animation is flatter: one ordered sequence of frames with a single
per-frame delay (`animation_delay_time`) and a single repeat flag
(`animation_no_repeat`). There's no native concept of multiple parts, loop
counts per part, or pauses.

**encode direction:** all parts are flattened into one frame sequence, in
the order `desc.txt` plays them (accounting for each part's own loop count).
Only the FPS from `desc.txt`'s first line and `--delay` (if passed) map onto
the qmg animation delay; per-part loop/pause/color fields have no `.qmg`
equivalent and are dropped. This is lossy for multi-part animations —
documented here rather than silently discovered later.

**decode direction:** the reverse mapping is exact (single part, single
loop), since a `.qmg` animation only ever describes one linear frame
sequence.

## Status

Scaffolding only — CLI arg parsing, zip I/O, PNG writing on the decode
side, and the desc.txt reader/writer still need implementing. See inline
`TODO`s in `main.cpp`.

# qmg2zip

Converts a Samsung Qmage (`.qmg`) boot-animation file into an Android
`bootanimation.zip`, using `qmgd` as the decoder library.

## Usage

```sh
qmg2zip in.qmg bootanimation.zip [--fps N]
```

Decodes every frame of a `.qmg` file, writes them out as PNGs under
`part0/`, generates a `desc.txt` (single part, infinite loop by
default), and repacks everything into an uncompressed
`bootanimation.zip`.

## Format notes: qmg -> desc.txt

The reverse mapping (qmg -> desc.txt) is exact — single part, single
loop — since a `.qmg` animation only ever describes one linear frame
sequence. See [`zip2qmg`'s README](../zip2qmg/README.md) for the
lossy encode-direction mapping.

## Status

Working, round-trip tested against qmgd's sample files via `zip2qmg`.
Known gap: transparency frames aren't supported yet (matches qmgd's
current decode limitations).

## Memory use

qmg2zip memory-maps the input and decodes one frame at a time, writing each
frame straight to a PNG in `$TMPDIR`; only the current and reference frames are
held in RAM, so peak memory does not grow with frame count. Temp disk space
must hold the decoded PNGs until the zip is assembled.

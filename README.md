# rawtools

Low-level Android tooling. First tools in here: converting between Android
`bootanimation.zip` archives and Samsung Qmage (`.qmg`) boot-animation files,
built on top of [qmgc](https://github.com/BroBordd/qmgc) (encoder) and
[qmgd](https://github.com/BroBordd/qmgd) (decoder).

## Layout

```
third_party/qmgc/  qmgc encoder library + CLI (git submodule)
third_party/qmgd/  qmgd decoder library + CLI (git submodule)
tools/common/      shared desc.txt + bootanimation.zip handling (both tools)
tools/zip2qmg/     bootanimation.zip -> .qmg (encode)
tools/qmg2zip/     .qmg -> bootanimation.zip (decode)
```

## Getting the code

```sh
git clone --recurse-submodules https://github.com/BroBordd/rawtools
# or, if already cloned without --recurse-submodules:
git submodule update --init --recursive
```

## zip2qmg / qmg2zip

Convert a stock Android `bootanimation.zip` into a single `.qmg` file
(Samsung's on-device boot animation format), and back.

```sh
zip2qmg bootanimation.zip out.qmg
qmg2zip in.qmg bootanimation.zip
```

See [`tools/zip2qmg/README.md`](tools/zip2qmg/README.md) and
[`tools/qmg2zip/README.md`](tools/qmg2zip/README.md) for details,
current status, and format-mapping notes (desc.txt <-> qmg header
fields).

## License

GPLv3 (or later), matching `qmgc`/`qmgd`. See [`LICENSE`](LICENSE).

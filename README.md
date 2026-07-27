# rawtools

Low-level Android tooling. First tool in here: converting between Android
`bootanimation.zip` archives and Samsung Qmage (`.qmg`) boot-animation files,
built on top of [qmgc](https://github.com/BroBordd/qmgc) (encoder) and
[qmgd](https://github.com/BroBordd/qmgd) (decoder).

## Layout

```
third_party/qmgc/   qmgc encoder library + CLI (git submodule)
third_party/qmgd/   qmgd decoder library + CLI (git submodule)
tools/bootanim_qmg/ bootanimation.zip <-> .qmg converter (this repo's tool)
```

## Getting the code

```sh
git clone --recurse-submodules https://github.com/BroBordd/rawtools
# or, if already cloned without --recurse-submodules:
git submodule update --init --recursive
```

## bootanim_qmg

Converts a stock Android `bootanimation.zip` into a single `.qmg` file
(Samsung's on-device boot animation format), and back.

```sh
bootanim_qmg encode bootanimation.zip out.qmg
bootanim_qmg decode in.qmg bootanimation.zip
```

See [`tools/bootanim_qmg/README.md`](tools/bootanim_qmg/README.md) for
details, current status, and format-mapping notes (desc.txt <-> qmg header
fields).

## License

GPLv3 (or later), matching `qmgc`/`qmgd`. See [`LICENSE`](LICENSE).

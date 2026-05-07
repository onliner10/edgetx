# Edge16 HIL MVP

This directory contains the MVP hardware-in-the-loop harness for a real TX16S,
an Arduino Uno R4 Minima, the trainer jack, and the external module bay signal
path.

Typical host run:

```sh
nix develop -c tools/hil/edge16-hil run --suite mvp --device auto
```

One-time SD-card install:

```sh
nix develop -c tools/hil/edge16-hil prepare-sd --mount /path/to/sdcard
```

`prepare-sd` installs `SCRIPTS/FUNCTIONS/hil.lua` and these model assets:

- `MODELS/model97.yml`: `HIL PPM8`, fixed external PPM output for debugging
- `MODELS/model98.yml`: `HIL SBUS16`, fixed external SBUS output for debugging
- `MODELS/model99.yml`: `HIL AUTO`, Lua-supervised autonomous profile switching

It also keeps readable copies as `hil-ppm8.yml`, `hil-sbus16.yml`, and
`hil.yml`, then appends the radio-visible files to `MODELS/models.yml` unless
`--skip-models-list-update` is used. Select `HIL AUTO` before running the PC
suite.

Electrical assumptions:

- Uno `D2` drives trainer PPM through the trainer jack signal path.
- Uno `D3` captures the module-bay CPPM/PPM signal and can decode SBUS without
  an external inverter.
- Uno `Serial1 RX` may also capture SBUS if you add external inversion and
  level shifting.
- The Uno must never be connected to module-bay battery voltage.

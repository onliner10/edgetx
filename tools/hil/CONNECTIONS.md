# Edge16 HIL Uno R4 Minima Connections

This wiring matches `tools/hil/uno/edge16_hil_uno/edge16_hil_uno.ino`.

Do not connect the TX16S module-bay battery/VMAIN/BATT pin to the Uno. Power
the Uno from USB only, and share ground with the radio.

## Uno Pins

| Uno R4 Minima pin | Direction | Harness signal |
| --- | --- | --- |
| USB-C | PC to Uno | Serial control and Uno power |
| GND | common | TX16S trainer sleeve and module-bay GND |
| D2 | output | Trainer PPM into TX16S trainer jack |
| D3 | input | Module-bay OUT/CPPM direct capture for PPM and software-inverted SBUS |
| D0 / RX1 | optional input | Module-bay SBUS capture after hardware inverter |
| D1 / TX1 | unused | Leave unconnected |

## Trainer Jack Input

Use a 3.5 mm trainer plug into the TX16S trainer jack.

| Trainer plug contact | Connect to |
| --- | --- |
| Sleeve | Uno GND |
| Tip | Uno D2 through the divider below |
| Ring | Leave unconnected unless verified for your radio/cable |

Recommended D2 level/protection network:

```text
Uno D2 ---- 4.7 kOhm ----+---- trainer plug tip
                         |
                       10 kOhm
                         |
Uno GND -----------------+---- trainer plug sleeve
```

This turns the Uno 5 V PPM output into about 3.4 V at the trainer input and
limits fault current.

## Module-Bay Output Capture

Use a JR module-bay breakout or sacrificial module shell. Identify pins by
function, not by wire color:

| Module-bay function | Use in MVP |
| --- | --- |
| GND | Connect to Uno GND |
| OUT / CPPM / PPM | Capture directly on D3; optional inverted SBUS copy to D0/RX1 |
| BATT / VMAIN | Do not connect to Uno |
| SPORT | Leave unconnected for MVP |
| HEARTBEAT / SBUS-CPPM input | Leave unconnected for MVP |

Direct MVP capture path, no inverter:

```text
Module OUT/CPPM ---- 1 kOhm ---- Uno D3
Module GND --------------------- Uno GND
```

This one wire handles module-bay PPM and SBUS. The firmware uses pulse timing on
D3 in PPM mode and a software-inverted SBUS decoder on D3 in SBUS mode.

Optional SBUS hardware-UART capture path with a 74HC14/74HCT14-style inverter
powered from Uno 5 V:

```text
Module OUT/CPPM ---- 4.7 kOhm ---- inverter input
Uno 5V --------------------------- inverter VCC
Uno GND -------------------------- inverter GND
inverter output ---- 1 kOhm ------ Uno D0 / RX1
```

Optional SBUS hardware-UART capture path with a discrete NPN inverter:

```text
Module OUT/CPPM ---- 10 kOhm ---- NPN base
NPN emitter ---------------------- Uno GND
Uno 5V -------- 10 kOhm ----------+---- Uno D0 / RX1
                                  |
NPN collector --------------------+
```

Use only one SBUS inverter option. If you do not fit an inverter, leave D0/RX1
unconnected.

## First Power-Up Checks

1. With the radio off, verify continuity from Uno GND to trainer sleeve and
   module-bay GND.
2. Verify no continuity from module-bay BATT/VMAIN to Uno 5V, VIN, D2, D3, or
   D0.
3. Power the Uno from USB, leave the radio off, and confirm D2 at the trainer
   tip is below 3.6 V when high.
4. Power the radio, select `HIL AUTO`, and run `edge16-hil list-devices` before
   running the full suite.

# Ford F-100 Digital Gauge Cluster

STM32H753ZI-based digital instrument cluster for a Ford F-100. Replaces the analog gauge cluster with five round displays (four peripheral gauges plus a larger center display) driven from CAN/ADC/pulse inputs.

## Hardware

- **MCU:** STM32H753ZI on Nucleo-144 dev board
- **Displays:**
  - 4× ST77916 round 360×360 LCDs (peripheral gauges: fuel, oil pressure, coolant temp, charge voltage) — driven via shared QSPI, individual CS lines
  - 1× NV3052CGRB-based 4" 720×720 round LCD (center display: speedometer, tachometer, high-beam) — 18-bit RGB666 panel driven from LTDC (RGB565 framebuffer → 24-bit LTDC bus, panel reads top 6 bits of each channel) + 3-wire SPI for init. The pixel matrix is 720×720; the visible aperture is circular so the corners are not rendered.
- **Inputs:**
  - Holley Sniper 2 EFI over CAN (FDCAN1, 1 Mbps)
  - ADC: oil pressure, coolant temp, battery voltage, fuel level, headlight dimmer rheostat
  - Pulse: VSS, tachometer (timer input capture)
  - Discrete: high beam, left/right turn signals
- **Other:** I²C EEPROM for calibration storage, USART for Bluetooth phone config UI, USART for debug

## Build

CubeIDE-CMake project. The `.ioc` file is the source of truth for peripheral config; pin assignments and HAL init code are regenerated from CubeMX.

```
cmake --preset Release
cmake --build --preset Release
```

Flashing/debug is via the STM32 VS Code Extension (`.vscode/launch.json` is committed).

> **Regeneration caution:** `DEPolarity = LTDC_DEPOLARITY_AL` in `MX_LTDC_Init` is a required fix (see NV3052C post-mortem) that lives in the CubeMX-generated region of `main.c` and is **not** captured in the `.ioc`. Set the LTDC DE polarity accordingly in CubeMX before regenerating, or the fix silently reverts and the center display goes dark.

## Project Layout

```
Core/           CubeMX-generated HAL init + main.c
Drivers/        STM32 HAL + CMSIS (vendored)
Display/        Display drivers (st77916.c/h, nv3052c.c/h)
CAN/            Holley Sniper 2 protocol parser (in progress)
Sensors/        ADC sensor reading (in progress)
Inputs/         Pulse input handling (in progress)
Config/         Persistent calibration / settings
cmake/          Toolchain + STM32H7 cmake helpers
```

## Design Notes

[`docs/design.md`](docs/design.md) — living document of architecture decisions (input handling, calibration UI, hardware choices).

## Current Status

- **ST77916 driver:** working — solid color test confirmed
- **NV3052C driver:** working — half-screen issue resolved (see post-mortem below); currently runs a background-color cycling diagnostic, real gauge rendering not yet implemented
- **CAN / ADC / pulse inputs:** implemented, not yet validated against real vehicle signals

## NV3052C half-screen issue — resolved (2026-07-24)

For a long stretch of this project the center display showed a perfect 50/50 vertical split: one half displayed the LTDC background color (washed out), the other a frozen "barcode" of vertical stripes. The split was invariant to PCLK frequency, porches, sync mode, and register `0x23`, moved only with MADCTL `ss`, and survived a Nucleo board swap. Root cause was two stacked firmware bugs:

1. **SPI chip-select framing silently discarded the entire vendor init.** The bit-bang 9-bit SPI raised CS after every word, but the NV3052C page-select command (`0xFF`) takes its three parameters (`0x30`, `0x52`, page) in a *single* CS frame. The page never switched, so every page 1–3 write (source timing, gamma, VCOM) landed nowhere and the panel ran on power-on defaults — which load only ~360 of 720 columns per line. The half receiving each line's *late* pixels was never written (hence the frozen barcode of unlatched garbage, and why the dead side tracked `ss`), and the working half's colors were washed out by default gamma/VCOM. Fixed in [Display/nv3052c.c](Display/nv3052c.c): `nv_write()` holds CS low across a command and all its parameters; page selects are one three-parameter frame.
2. **LTDC data-enable polarity was inverted.** Once the init actually applied, the panel obeyed strict DE-only mode (`0x23=0xA0`) and sampled during blanking — where the bus carries black — because `DEPolarity` was `AH`. Fixed to `AL` in `MX_LTDC_Init` (see the regeneration caution in Build).

The decisive diagnostic: frame-by-frame analysis of phone video showed the garbled half was statistically frozen across background-color changes — those columns never latched *any* data, which eliminated signal-integrity and defective-panel theories (a constant input can't produce static multicolor stripes, and a dead bank wouldn't move with `ss`) and pointed at init delivery.

`0x23` and MADCTL are back at the manufacturer's golden values (`0xA0` / `0x0A`). Verified on hardware: full-screen saturated RGB cycling, uniform edge to edge, correct hues.

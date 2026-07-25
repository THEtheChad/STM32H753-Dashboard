# VSS Bring-Up — Status & Pickup Point (paused 2026-07-25)

**Goal:** the speedometer needle shows real measured speed. Firmware is DONE and
flashed; we are blocked on ONE physical jumper wire that does not conduct.

## Where we stopped — resume here

A wire-continuity test was set up and **not yet executed**:

1. The suspect jumper should be placed with BOTH ends on the CN11 (left-edge
   morpho) strip: one end in **CN11 pin 28 (PA0)** — right column, row 14, three
   holes above the green harness wire — and the other end in **CN11 pin 20
   (GND)** — same column, four rows up.
2. PA0 had an internal pull-up armed over SWD. **A reset/power-cycle clears it** —
   re-arm before reading (commands below).
3. Read PA0: **0 = wire conducts** (wire fine → suspect the CN12 pin 1 end/seating
   next). **1 = wire is broken internally** → discard it AND its bundle-mates, use
   a visibly different jumper, wire CN12 pin 1 → CN11 pin 28, done.

```sh
CLI="$HOME/Library/Application Support/stm32cube/bundles/programmer/2.23.0/bin/STM32_Programmer_CLI"
# re-arm PA0 pull-up (PUPDR[1:0] = 01). Read current, OR in low bit:
"$CLI" -c port=SWD mode=HOTPLUG -r32 0x5802000C 4          # note value V
"$CLI" -c port=SWD mode=HOTPLUG -w32 0x5802000C <V|1>      # set pull-up
# read PA0 level (bit 0 of GPIOA IDR):
"$CLI" -c port=SWD mode=HOTPLUG -r32 0x58020010 4          # bit0: 1=open, 0=wire OK
```

## The intended final jumper (bench only)

**CN12 pin 1 (PC9, test PWM out) → CN11 pin 28 (PA0, VSS capture in).**
Both are male morpho pins; a female-female DuPont works. See
[`vss-jumper.png`](vss-jumper.png) (yellow wire; colored dots are the verified
display harness for orientation). Do NOT use Zio CN10-29 for PA0 — Zio sockets
are female and need a male-tipped wire (first two failed attempts).

## What is already PROVEN working (all verified over SWD)

- **Firmware end-to-end**: during wire insertion, contact bounce produced a pulse
  burst — the capture ISR ate it, the SMA buffer filled (`vssBufCount=16`), and
  the needle jumped to 100 before timing out to 0. The entire
  capture → smoothing → source-selection → needle pipeline works.
- **Test PWM generator**: TIM3_CH4 on PC9 verified live and toggling (~50% duty,
  frequency tracking the 0-100-0 MPH triangle profile). `VSS_TEST_SIGNAL 1` in
  `Inputs/inputs.c` — set to 0 for the real vehicle.
- **Pin muxes**: PA0 = TIM2_CH1 (AF1) confirmed; PC9 = TIM3_CH4 (AF2) confirmed.
- **Calibration**: defaults loaded, `vssPulsesPerMile = 8000`, SMA window 5.
- **The failure is purely physical**: driving each pin and scanning every GPIO on
  the chip found NO follower and NO electrical load on either pin — the jumper
  (or its seating) is an open circuit. Prime suspect: broken crimp.

## After the wire finally conducts

- No reflash needed: needle immediately tracks a 24 s 0-100-0 MPH triangle,
  genuinely measured (`inputs.speedMph`, symbol `inputs` — check with SWD or
  just watch the gauge).
- Update the wiring map: the last export (`wiring-map-2026-07-25 (4).json`)
  still records the jumper at CN10-29 — re-record to CN11-28 and re-export;
  `docs/wiring-map.json` + `pinout.md` only carry the permanent 34-wire harness
  and stay as-is (bench jumper is temporary).
- For the truck: set `VSS_TEST_SIGNAL` to 0, remove the jumper, feed real VSS
  into PA0 per docs/design.md.

## Debug tooling worth remembering

- `tools/fbshot.py` — pixel-perfect screenshot of the gauge over SWD.
- `NV3052C_NeedleCycles` / `NV3052C_FrameIntervalMs` — draw-cost / frame-rate
  telemetry (find addresses with `arm-none-eabi-nm`).
- STM32_Programmer_CLI `-r32/-w32` beware: writes to write-only registers
  (BSRR, ICR) fail readback-verify and may abort — use read-modify-write on
  ODR/MODER/PUPDR and verify every step (unverified writes poisoned one round
  of continuity testing).

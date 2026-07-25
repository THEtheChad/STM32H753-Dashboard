# Bench Harness Pinout — Center Display (NV3052C)

Verified 2026-07-25 against the expected netlist derived from the `.ioc`, the
driver code, and the SF-TO400XC-8996A2-N panel spec — 35/35 connections correct.
Recorded with [`docs/wiring-map.html`](docs/wiring-map.html); machine-readable
source of truth is the exported JSON from that tool.

## Panel (FPC-40P breakout J1) → Nucleo / DC-DC

| FPC pin | Signal | Lands on | MCU pin | Wire color |
|---|---|---|---|---|
| 1 | LEDA (backlight +) | DC-DC OUT+ |  | red |
| 2 | LEDK (backlight −) | CN9 pin 12 | GND | purple |
| 3 | LEDK (backlight −) | CN7 pin 8 | GND | black |
| 4 | GND | GNDR pin 2 | GND | black |
| 5 | VCI 3.3 V | CN8 pin 7 | 3V3 | yellow |
| 6 | RESET | CN12 pin 68 | PG5 | brown |
| 9 | SPI SDA | CN12 pin 15 | PA7 | orange |
| 10 | SPI SCK | CN11 pin 70 | PG11 | orange |
| 11 | SPI CS | CN12 pin 69 | PG4 | orange |
| 12 | PCLK | CN10 pin 8 | PE14 | brown |
| 13 | DE | CN9 pin 11 | PF10 | orange |
| 14 | VSYNC | CN7 pin 17 | PA4 | orange |
| 15 | HSYNC | CN7 pin 1 | PC6 | orange |
| 16 | DB0 / B2 (unused, tied low) | CN10 pin 5 | GND | black |
| 17 | DB1 / B3 | CN12 pin 65 | PD10 | blue |
| 18 | DB2 / B4 | CN12 pin 49 | PE12 | blue |
| 19 | DB3 / B5 | CN12 pin 37 | PA3 | blue |
| 20 | DB4 / B6 | CN7 pin 2 | PB8 | blue |
| 21 | DB5 / B7 | CN7 pin 4 | PB9 | blue |
| 22 | DB6 / G2 | CN7 pin 12 | PA6 | green |
| 23 | DB7 / G3 | CN10 pin 6 | PE11 | green |
| 24 | DB8 / G4 | CN10 pin 32 | PB10 | green |
| 25 | DB9 / G5 | CN10 pin 34 | PB11 | green |
| 26 | DB10 / G6 | CN7 pin 11 | PC7 | white |
| 27 | DB11 / G7 | CN12 pin 66 | PG8 | green |
| 28 | DB12 / R2 (unused, tied low) | CN10 pin 27 | GND | black |
| 29 | DB13 / R3 | CN11 pin 34 | PB0 | green |
| 30 | DB14 / R4 | CN12 pin 14 | PA11 | red |
| 31 | DB15 / R5 | CN9 pin 3 | PC0 | red |
| 32 | DB16 / R6 | CN9 pin 7 | PB1 | red |
| 33 | DB17 / R7 | CN10 pin 30 | PE15 | red |
| 34 | GND | CN9 pin 23 | GND | black |

Panel pins 7, 8 (NC) and 35–40 (touch, unpopulated) are unconnected by design.

## Backlight power (DC-DC module)

| From | To | Wire color |
|---|---|---|
| CN8 pin 9 (5V) | DC-DC IN+ | red |
| DC-DC IN- | CN8 pin 11 (GND) | purple |
| DC-DC OUT- | CN8 pin 13 (GND) | purple |

## Notes

- The panel exposes only two logic grounds (FPC 4 and 34); both are wired.
- Known improvement: the backlight return (LEDK, FPC 2/3) currently lands on logic
  ground rather than the DC-DC OUT− pad, so LED current shares the logic-ground
  jumpers. Fine on the bench; reroute before the analog senders come online.
- 2026-07-25: FPC-16 (B2) was found on CN10 pin 7 (PF4, floating) and moved to
  CN10 pin 5 (GND) — a one-row slip present since original assembly.

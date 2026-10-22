# Changelog

## [1.1.0] - 2026-10-22

### Added
- Optional 128x64 I2C OLED on the GPS bus showing UTC, the 6-character Maidenhead grid,
  satellite count and Bluetooth link state. SSD1306 or SH1106 is detected at boot from the
  status byte (Meshtastic's probe), at 0x3C or 0x3D. A screen plugged in later is picked up
  within 5 seconds.
- Burn-in care: contrast 96 of 255, and the layout shifts by up to 2 px each minute.
- Redraws happen after each RMC, at the end of the NMEA burst, so the ~95 ms I2C transfer never
  delays a sentence.

### Changed
- CPU runs at 80 MHz instead of 240, the lowest Bluetooth allows, for battery life.
- The LED stays off when a screen is fitted.
- The time is tagged "UTC" on screen so nobody mistakes it for local time.

## [1.0.1] - 2026-10-22

### Added
- MIT licence.
- README rewritten for anyone building one: other ESP32 boards, Linux pairing, the timing
  limits of NMEA, troubleshooting (including OscarWatch not reconnecting after a reboot).
- Prebuilt release images: a full image for `0x0` and an app-only image for `0x10000`.

### Changed
- `strip_paths.py` pre-script maps local build paths out of `__FILE__`, so the binary carries
  no home directory from the machine it was built on.
- I2C pins are now `I2C_SDA` / `I2C_SCL` defines at the top of `main.cpp`.
- `platformio.ini` no longer pins a COM port; PlatformIO auto-detects it.

## [1.0.0] - 2026-10-22

### Added
- Reads NMEA from an Adafruit Mini GPS PA1010D over I2C (0x10, SDA 21, SCL 22) and relays it
  over Bluetooth Classic SPP as `OscarGPS` and over USB serial at 115200.
- Checksum check on every sentence before it goes out; the PA1010D's bare-LF buffer padding is
  dropped, so it can't split a sentence.
- GPS configured at boot for RMC, GGA and GSA every second and GSV every fifth.
- Status lines on USB prefixed `#` (NMEA parsers skip them): boot info, I2C scan, fix gained and
  lost, Bluetooth client connect and disconnect.
- GPS re-probed every 5 s if it was missing at boot.
- LED: fast blink with no GPS on the bus, one flash a second while searching, solid with a fix.

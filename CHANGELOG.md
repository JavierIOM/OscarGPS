# Changelog

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

# OscarGPS

A Bluetooth GPS for satellite tracking software, made from an ESP32 and a small I2C GPS breakout.

The ESP32 reads an Adafruit Mini GPS PA1010D over I2C and relays standard NMEA over Bluetooth
Classic serial (SPP). Pair it with a PC and you get a COM port that any program expecting a
serial NMEA GPS can open. It was built for
[OscarWatch](https://github.com/magicbug/OscarWatch-Tracker), which uses it for station position,
altitude and UTC, but nothing in it is specific to OscarWatch.

The GPS sentences go out over USB serial as well, so it also works as a wired GPS.

## Hardware

| Part | Notes |
|------|-------|
| DFRobot FireBeetle ESP32 (DFR0478) | Any original ESP32 board should do. **Not** an ESP32-S3, C3 or C6: they have no Bluetooth Classic, and Windows only turns Bluetooth Classic SPP into a COM port, not BLE |
| Adafruit Mini GPS PA1010D (product 4415) | MediaTek MT3333, GPS + GLONASS, I2C address 0x10, STEMMA QT connector |
| STEMMA QT to male header cable | Or four jumper wires |

### Wiring

| GPS (STEMMA QT) | ESP32 |
|-----------------|-------|
| VIN (red) | 3V3 |
| GND (black) | GND |
| SDA (blue) | GPIO21 |
| SCL (yellow) | GPIO22 |

GPIO21 and GPIO22 are the default I2C pins on most ESP32 boards. If yours differ, change
`I2C_SDA` and `I2C_SCL` at the top of `src/main.cpp`.

## Flash a prebuilt binary

Each [release](https://github.com/JavierIOM/OscarGPS/releases) has two images for an original
ESP32 with 4 MB of flash or more, with SHA-256 checksums:

| File | Flash at | Use it for |
|------|----------|-----------|
| `OscarGPS-x.y.z-esp32-full.bin` | `0x0` | A new board. Bootloader, partition table and app in one |
| `OscarGPS-x.y.z-esp32-app.bin` | `0x10000` | Updating a board already running OscarGPS. Keeps its Bluetooth pairing |

The full image overwrites the area where the ESP32 stores its Bluetooth pairings, so after
flashing it, remove `OscarGPS` from your PC's Bluetooth devices and pair it again.

In Chrome or Edge, the [esptool-js web flasher](https://espressif.github.io/esptool-js/) needs
nothing installed: Connect, set the address, pick the file, Program. Or with
[esptool](https://github.com/espressif/esptool):

```
esptool.py --chip esp32 write_flash 0x0 OscarGPS-1.1.0-esp32-full.bin
```

## Build and flash

You need [PlatformIO](https://platformio.org/), either the VS Code extension or the CLI.

```
pio run -t upload
pio device monitor
```

The upload port is auto-detected. To set it, add `upload_port = COM5` (or `/dev/ttyUSB0`) to
`platformio.ini`. For a board other than the FireBeetle, change `board =` to match it, for example
`esp32dev`.

On boot the serial monitor (115200 baud) shows something like:

```
# OscarGPS 1.0.1
# I2C device at 0x10
# PA1010D found at 0x10
# Bluetooth up as OscarGPS, 88:57:21:xx:xx:xx
# GPS fix, 7 satellites
$GNGGA,...
```

## Pairing

### Windows

1. Settings, Devices, Bluetooth & other devices, Add Bluetooth or other device, Bluetooth.
2. Pick `OscarGPS`. It may show as its address for a few seconds before the name appears. There
   is no PIN.
3. Go to More Bluetooth options and open the COM Ports tab. Windows adds two ports. Use the
   **Outgoing** one, named `OscarGPS 'ESP32SPP'`. The Incoming one never carries any data.

The baud rate setting on a Bluetooth COM port is ignored, so any value works.

### Linux

Not tested by me, but the standard route is:

```
bluetoothctl pair <address>
bluetoothctl trust <address>
sudo rfcomm bind 0 <address> 1
```

That gives you `/dev/rfcomm0`, which you can open directly or hand to gpsd.

## OscarWatch

Settings, GPS:

- Enable GPS
- Connection: Serial (COM port)
- Serial port: the Outgoing Bluetooth port (or the USB port for a wired setup at 115200)
- Tick "Automatically update station position from GPS fix", or press Apply fix to station

## What it sends

RMC, GGA, GSA and GSV once a second. The talker IDs are the ones the module
uses (`$GN` for combined fixes, `$GP` and `$GL` for each constellation). Every sentence's checksum
is checked before it goes out.

The USB output also carries status lines that start with `#`: boot info, fix gained or lost, and
Bluetooth client connected or disconnected. NMEA parsers skip lines that don't start with `$`.

To change the Bluetooth name, the I2C address or the sentence mix, edit the defines and the
`PMTK314` command near the top of `src/main.cpp`.

## Display (optional)

A 128x64 I2C OLED (the common 0.96" SSD1306 or 1.3" SH1106 modules with four pins) shows:

```
   19:42:07 UTC
      IO74re
7 sats  Oscar linked
```

That's the time in UTC (not local time), the 6-character grid locator (or "no fix"), the
satellites in use and the Bluetooth link.

Wire its VCC, GND, SDA and SCL to the same four lines as the GPS. It sits at 0x3C (or 0x3D)
and the GPS at 0x10, so they share the bus. Use one screen: two modules on the same address
will fight. The controller type is detected at boot. Without a screen everything else works
as normal, and a screen plugged in later is picked up within 5 seconds.

"Oscar linked" means a Bluetooth client has the serial port open ("not linked" otherwise). The
board can't tell which program that is. To slow burn-in, the contrast is turned down and the
text moves a pixel or two each minute.

### Pages

With satellites in view the screen cycles every 20 seconds:

| Page | Shows | Time on screen |
|------|-------|----------------|
| Main | UTC, grid, satellites in use, link, battery | 10 s |
| Sky plot | Each satellite at its azimuth and elevation, labelled with its PRN. Reversed (white box) means it's in the fix. Outer ring is the horizon, inner ring 45 degrees, north up. Used/in-view per system, HDOP and battery volts on the right | 5 s |
| Signal | A bar per satellite, height from its SNR (0 to 50 dB-Hz), PRN underneath. Solid bars are in the fix, hollow ones are tracked but unused. Up to 12, those in the fix first | 5 s |

The GPS is set to report its satellite list every second so the pages update live.

## Battery gauge (optional)

The FireBeetle has a LiPo socket and charger but no battery sense on its analogue pins (the
on-board divider on the V4.0 board isn't connected to A0 as shipped). Two resistors fix that:

```
   VCC o──────┐
              │
             [ ] R1 100k
              │
A0/IO36 o─────┤
              │
             [ ] R2 100k
              │
   GND o──────┘
```

VCC carries the battery voltage on battery and the USB voltage on USB, so the screen shows a
percentage on battery and "USB" when plugged in. The divider draws about 20 µA. On the
FireBeetle, GND, AREF, 3V3 and VCC are the last four pins of one header row, so R1 and R2 can
run in series from VCC to GND with a wire from their join to A0/IO36 at the far end of the other
row. Sleeve the legs so they don't touch AREF or 3V3. A 100 nF capacitor across R2 steadies the
reading but isn't required.

Without the resistors the firmware sees nothing on A0 and leaves the battery off the screen. If
the gauge disagrees with a meter, change `BATT_RATIO` in `src/main.cpp`.

## LED

On the FireBeetle the on-board LED is GPIO2. It stays off when a screen is fitted, to save
battery.

| Pattern | Meaning |
|---------|---------|
| Fast blink | No GPS on I2C. Check the cable. It retries every 5 seconds |
| One flash a second | GPS found, searching for a fix |
| Solid | Fix |

## A note on timing

NMEA has no precise time edge. The sentences reach the PC roughly 0.2 to 0.3 s after the second
they describe, and Bluetooth adds a little more. That is plenty for orbit prediction, where 0.2 s
is about 1.5 km along a low orbit. It is not good enough to discipline a PC clock for FT8 or FT4.
Keep NTP for that.

## Troubleshooting

- **"Access is denied" opening the port:** another program already has it. Only one program can
  open a COM port at a time.
- **Port opens but no data:** you picked the Incoming Bluetooth port. Use the Outgoing one.
- **"The system cannot find the file specified" opening the Bluetooth port**, while Device
  Manager says the port is fine: Windows' Bluetooth serial layer has wedged. The board isn't at
  fault (the screen will show "not linked"). Turn Bluetooth off and on in Windows and reopen
  the port.
- **Data stopped after the board rebooted or lost power:** the PC opens the Bluetooth link, so
  the board can't restart it. OscarWatch doesn't reopen the port by itself, so turn GPS off and
  on in its settings.
- **Not in the Bluetooth device list:** check that the LED is doing something. Bluetooth only
  starts after the firmware boots.
- **No fix indoors:** the PA1010D's patch antenna needs a view of the sky. Try a windowsill. From
  cold it can take a minute or two.

## Licence

MIT. Built by 2D0PEY on the Isle of Man.

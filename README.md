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
esptool.py --chip esp32 write_flash 0x0 OscarGPS-1.5.0-esp32-full.bin
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

Once paired, **the board dials the PC**, not the other way round. It connects to the PC's
**Incoming** Bluetooth COM port, and redials by itself after a restart, a flat battery or a
trip out of range. The app just keeps the Incoming port open and never has to reconnect.

That's deliberate. With the usual arrangement (the app opens an Outgoing port to the device),
neither Windows nor OscarWatch recovers when the device restarts. OscarWatch retries the dead
port about 30 times a second, and that can knock Windows' Bluetooth serial driver over
altogether.

### Windows

1. Settings, Devices, Bluetooth & other devices, Add Bluetooth or other device, Bluetooth.
2. Pick `OscarGPS`. It may show as its address for a few seconds before the name appears. There
   is no PIN.
3. Go to More Bluetooth options and open the COM Ports tab. If there's no **Incoming** port, click
   Add, choose Incoming, OK.
4. Restart the board. It finds the PC in its list of paired devices and dials it within a few
   seconds.

The screen shows which port to use: when the PC is there but nothing has the port open it reads
`COM11 closed` (or whatever number Windows gave the Incoming port), learned from the PC itself,
so it follows Windows if the number ever changes. The baud rate setting on a Bluetooth COM port
is ignored.

A board that has never been paired offers its serial port the old way until it is, so step 2
works on a fresh board. To go back to the PC dialling the board, set `BT_DIAL_HOME` to 0 in
`src/main.cpp`.

### Linux

Not tested by me. The simplest route is `BT_DIAL_HOME` 0 and the standard binding:

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
- Serial port: the **Incoming** Bluetooth port, the one the screen names (or the USB port for a
  wired setup at 115200)
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

The screen is drawn rotated 180 degrees to suit the enclosure. If yours reads upside down, set
`OLED_ROTATION` to `U8G2_R0` in `src/main.cpp`.

The link label says what to do about it:

| Label | Meaning |
|-------|---------|
| `Oscar linked` | A program on the PC has the serial port open and is getting a position. The board can't tell which program |
| `awaiting sats` | Linked, but the GPS has no fix yet, so the program has nothing to plot. Give it more sky |
| `COM11 closed` | The PC answered, but nothing has its Incoming port open. Start OscarWatch, or check it's set to that port |
| `PC not found` | The PC didn't answer: off, asleep, out of range or Bluetooth turned off |
| `dialling` | The first dial since a restart or a dropout is still going |
| `not linked` | Nothing paired yet |

The board tells the two failures apart by how long the PC takes to answer, so allow one dial
(up to 15 s) after a change for the label to catch up. To slow burn-in, the contrast is turned
down and the text moves a pixel or two each minute.

### Boot screen

At power-on the screen runs a retro BIOS POST: firmware version, CPU, flash, the Bluetooth
address and I2C pins, then a check of each part with a dot leader and its result:

```
GPS PA1010D 0x10 ......... OK
Display SH1106 0x3C ...... OK
Battery sense A0 ....... 4.05V
Bluetooth SPP ............ OK
NMEA stream .............. OK
```

The checks are real. A missing GPS shows NONE, a GPS that hasn't sent a sentence yet shows
WAIT, and so on. It takes about 5 seconds, and is skipped when no screen is fitted.

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

## Enclosure

A layered 3.6 mm plywood box and a laser-cut 3 mm acrylic lid, with clips for the screen and GPS and a glue jig to place them: [enclosure/](enclosure/README.md).

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
- **Port opens but no data:** you picked the Outgoing Bluetooth port. Use the Incoming one, the
  port the screen names.
- **"The system cannot find the file specified" opening a Bluetooth port:** Windows' Bluetooth
  serial driver has failed. Device Manager shows "Bluetooth Device (RFCOMM Protocol TDI)" with
  an error (code 10). Close every program that has a Bluetooth COM port open, then turn
  Bluetooth off and on. With the program still holding the port, the driver may not come back.
- **Stopped working after removing and re-pairing `OscarGPS`:** removing the device in Windows
  deletes the Incoming port as well, and pairing again doesn't bring it back. The old COM
  numbers may still be listed but won't open. Add an Incoming port again (Pairing, step 3) and
  point the app at it. Until the first link the screen shows the old number, so go by the COM
  Ports tab this time. You rarely need to re-pair: if the link drops, check the app has the
  Incoming port open first.
- **Screen says "PC not found":** the PC is off, asleep, out of range or has Bluetooth off. The
  USB log has the same (`PC didn't answer`).
- **Screen says "COM11 closed" with OscarWatch running:** OscarWatch is set to a different port,
  or it gave up on the port. Check Settings, GPS, and restart OscarWatch if it's set right.
- **Not in the Bluetooth device list:** check that the LED is doing something. Bluetooth only
  starts after the firmware boots.
- **No fix indoors:** the PA1010D's patch antenna needs a view of the sky. Try a windowsill. From
  cold it can take a minute or two.

## Licence

MIT. Built by 2D0PEY on the Isle of Man.

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

RMC, GGA and GSA once a second, and GSV every fifth second. The talker IDs are the ones the module
uses (`$GN` for combined fixes, `$GP` and `$GL` for each constellation). Every sentence's checksum
is checked before it goes out.

The USB output also carries status lines that start with `#`: boot info, fix gained or lost, and
Bluetooth client connected or disconnected. NMEA parsers skip lines that don't start with `$`.

To change the Bluetooth name, the I2C address or the sentence mix, edit the defines and the
`PMTK314` command near the top of `src/main.cpp`.

## LED

On the FireBeetle the on-board LED is GPIO2.

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
- **Data stopped after the board rebooted or lost power:** the PC opens the Bluetooth link, so
  the board can't restart it. OscarWatch doesn't reopen the port by itself, so turn GPS off and
  on in its settings.
- **Not in the Bluetooth device list:** check that the LED is doing something. Bluetooth only
  starts after the firmware boots.
- **No fix indoors:** the PA1010D's patch antenna needs a view of the sky. Try a windowsill. From
  cold it can take a minute or two.

## Licence

MIT. Built by 2D0PEY on the Isle of Man.

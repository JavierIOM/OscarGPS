# OscarGPS

Bluetooth GPS for [OscarWatch](https://github.com/magicbug/OscarWatch-Tracker). A FireBeetle
ESP32 reads an Adafruit Mini GPS PA1010D over I2C and relays standard NMEA over Bluetooth
Classic serial (SPP). Windows turns the pairing into a COM port, and OscarWatch reads that as a
serial NMEA GPS for station position, altitude and UTC.

## Hardware

| Part | Notes |
|------|-------|
| DFRobot FireBeetle ESP32 (ESP32-D0WD) | Needs the original ESP32: the S3 and C3 have no Bluetooth Classic, and Windows only makes COM ports from Classic SPP |
| Adafruit Mini GPS PA1010D (product 4415) | MediaTek MT3333, GPS + GLONASS, I2C address 0x10 |

| GPS (STEMMA QT) | FireBeetle |
|-----------------|------------|
| VIN (red) | 3V3 |
| GND (black) | GND |
| SDA (blue) | GPIO21 |
| SCL (yellow) | GPIO22 |

## Build and flash

PlatformIO, run through the penv binary on Javier's machine:

```
C:\Users\Gavin\.platformio\penv\Scripts\pio.exe run -t upload
```

`platformio.ini` points at COM10. Change `upload_port` if Windows has moved it.

## Pairing with Windows

1. Settings, Devices, Bluetooth & other devices, Add Bluetooth or other device, Bluetooth.
2. Pick `OscarGPS`. No PIN.
3. Settings, Devices, More Bluetooth options, COM Ports tab. Windows adds two ports for the
   device. The **Outgoing** one, named `OscarGPS 'ESP32SPP'`, is the one to use.

## OscarWatch

Settings, GPS:

- Enable GPS
- Connection: Serial (COM port)
- Serial port: the Outgoing Bluetooth port from above (or the USB port for a wired test)
- Baud: anything. Bluetooth serial ignores it. Over USB it is 115200.

Then turn on "Automatically update station position from GPS fix", or press Apply fix to station.

## What it sends

RMC, GGA and GSA once a second, GSV every fifth second, with `GN`/`GP`/`GL` talker IDs as the
module produces them. Every sentence's checksum is checked before it goes out.

USB also carries status lines that start with `#` (boot info, fix gained or lost, Bluetooth
client connected). NMEA parsers ignore lines that don't start with `$`.

## LED (GPIO2)

| Pattern | Meaning |
|---------|---------|
| Fast blink | No GPS found on I2C. Check the cable. It retries every 5 s |
| One flash a second | GPS found, searching for a fix |
| Solid | Fix |

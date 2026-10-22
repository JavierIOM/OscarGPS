// OscarGPS: Adafruit Mini GPS PA1010D (I2C, STEMMA QT) on a FireBeetle ESP32,
// relayed as NMEA over Bluetooth Classic SPP and USB serial.
// OscarWatch reads it as a "Serial (COM port)" GPS: pair the board in Windows
// and pick the outgoing Bluetooth COM port it creates.
// Optional 128x64 I2C OLED (SSD1306 or SH1106) on the same bus shows UTC, grid,
// satellites and the Bluetooth link.

#include <Arduino.h>
#include <Wire.h>
#include <BluetoothSerial.h>
#include <esp_bt_device.h>
#include <U8g2lib.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED) || !defined(CONFIG_BT_SPP_ENABLED)
#error Bluetooth Classic SPP is not enabled in this core
#endif

#define BT_NAME         "OscarGPS"
#define I2C_SDA         21
#define I2C_SCL         22
#define GPS_I2C_ADDR    0x10
#define I2C_CHUNK       64     // bytes per read; the PA1010D pads with 0x0A once its buffer is empty
#define I2C_MAX_CHUNKS  16     // cap on one drain so the loop never stalls
#define IDLE_POLL_MS    10     // nap between drains; keeps sentence latency low for GPS time
#define FIX_TIMEOUT_MS  5000   // no GGA for this long counts as no fix
#define LED_PIN         LED_BUILTIN
#define OLED_CONTRAST   96     // of 255; dimmer is kinder to an always-on OLED
#define OLED_REDRAW_MS  1000   // fallback redraw when no RMC arrives to trigger one

BluetoothSerial SerialBT;
static U8G2 *oled = nullptr;

// NMEA line assembly. Room for 82 chars plus the CRLF we put back on.
static char line[128];
static size_t lineLen = 0;
static bool sawCR = false;

static bool gpsPresent = false;
static bool btConnected = false;
static uint8_t fixQuality = 0;
static uint8_t satellites = 0;
static uint32_t lastGgaMs = 0;
static char utcText[9] = "";      // HH:MM:SS from the last GGA
static char gridText[7] = "";     // 6-character Maidenhead locator
static bool redrawDue = false;    // set at the end of each NMEA burst (RMC)

// Status lines go to USB only and start with '#', so NMEA parsers skip them.
static void status(const char *fmt, ...) {
  char buf[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print("# ");
  Serial.println(buf);
}

static void sendPmtk(const char *body) {
  uint8_t cs = 0;
  for (const char *p = body; *p; p++) cs ^= (uint8_t)*p;
  char buf[96];
  int n = snprintf(buf, sizeof(buf), "$%s*%02X\r\n", body, cs);
  Wire.beginTransmission(GPS_I2C_ADDR);
  Wire.write((const uint8_t *)buf, n);
  Wire.endTransmission();
  delay(20);
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Expects "$...*HH" with nothing after the checksum.
static bool checksumOk(const char *s, size_t len) {
  const char *star = (const char *)memchr(s, '*', len);
  if (!star || star + 3 != s + len) return false;
  uint8_t cs = 0;
  for (const char *p = s + 1; p < star; p++) cs ^= (uint8_t)*p;
  int hi = hexVal(star[1]), lo = hexVal(star[2]);
  return hi >= 0 && lo >= 0 && cs == ((hi << 4) | lo);
}

// Field n of a sentence, counting the "$GNGGA" id as field 0. Stops at '*'.
static void nmeaField(const char *s, int n, char *out, size_t outLen) {
  while (n > 0 && *s && *s != '*') {
    if (*s++ == ',') n--;
  }
  size_t i = 0;
  while (*s && *s != ',' && *s != '*' && i < outLen - 1) out[i++] = *s++;
  out[i] = '\0';
}

// "ddmm.mmmm" or "dddmm.mmmm" plus hemisphere to signed decimal degrees.
static double nmeaDegrees(const char *v, const char *hemi) {
  double raw = atof(v);
  int deg = (int)(raw / 100);
  double d = deg + (raw - deg * 100) / 60.0;
  return (*hemi == 'S' || *hemi == 'W') ? -d : d;
}

static void maidenhead(double lat, double lon, char *out) {
  lon += 180.0;
  lat += 90.0;
  out[0] = 'A' + (int)(lon / 20);
  out[1] = 'A' + (int)(lat / 10);
  out[2] = '0' + (int)(fmod(lon, 20) / 2);
  out[3] = '0' + (int)fmod(lat, 10);
  out[4] = 'a' + (int)(fmod(lon, 2) * 12);
  out[5] = 'a' + (int)(fmod(lat, 1) * 24);
  out[6] = '\0';
}

static void parseGga(const char *s) {
  char f[16];
  nmeaField(s, 1, f, sizeof(f));
  if (strlen(f) >= 6) {
    snprintf(utcText, sizeof(utcText), "%.2s:%.2s:%.2s", f, f + 2, f + 4);
  }
  nmeaField(s, 6, f, sizeof(f));
  uint8_t q = (uint8_t)atoi(f);
  nmeaField(s, 7, f, sizeof(f));
  satellites = (uint8_t)atoi(f);
  lastGgaMs = millis();
  if (q > 0) {
    char lat[16], lon[16], ns[2], ew[2];
    nmeaField(s, 2, lat, sizeof(lat));
    nmeaField(s, 3, ns, sizeof(ns));
    nmeaField(s, 4, lon, sizeof(lon));
    nmeaField(s, 5, ew, sizeof(ew));
    if (*lat && *lon) maidenhead(nmeaDegrees(lat, ns), nmeaDegrees(lon, ew), gridText);
  }
  if ((q > 0) != (fixQuality > 0)) {
    status(q > 0 ? "GPS fix, %u satellites" : "GPS fix lost, %u satellites", satellites);
  }
  fixQuality = q;
}

static void handleLine() {
  line[lineLen] = '\0';
  if (!checksumOk(line, lineLen)) return;

  // Only talker sentences ($GP, $GN, $GL, $GA...) go out; $PMTK acks are ours.
  if (line[1] != 'G') {
    status("%s", line);
    return;
  }

  if (lineLen > 7 && memcmp(line + 3, "GGA,", 4) == 0) parseGga(line);
  if (lineLen > 7 && memcmp(line + 3, "RMC,", 4) == 0) redrawDue = true;  // last of the burst

  line[lineLen] = '\r';
  line[lineLen + 1] = '\n';
  Serial.write((const uint8_t *)line, lineLen + 2);
  if (btConnected) SerialBT.write((const uint8_t *)line, lineLen + 2);
}

// Real sentences end in CRLF. A bare LF is the PA1010D's empty-buffer padding,
// which can land mid-sentence, so it is dropped rather than ending the line.
static void feed(char c) {
  if (c == '$') {
    lineLen = 0;
    line[lineLen++] = c;
    sawCR = false;
    return;
  }
  if (lineLen == 0) return;
  if (c == '\n') {
    if (sawCR) {
      handleLine();
      lineLen = 0;
    }
    sawCR = false;
    return;
  }
  sawCR = (c == '\r');
  if (sawCR) return;
  if (lineLen < sizeof(line) - 3) {
    line[lineLen++] = c;
  } else {
    lineLen = 0;  // runaway line, wait for the next '$'
  }
}

// Drains the GPS buffer. Returns once a read comes back as nothing but padding.
static void pollGps() {
  for (int i = 0; i < I2C_MAX_CHUNKS; i++) {
    size_t n = Wire.requestFrom((uint8_t)GPS_I2C_ADDR, (uint8_t)I2C_CHUNK);
    if (n == 0) return;
    bool gotData = false;
    while (Wire.available()) {
      char c = (char)Wire.read();
      if (c != '\n') gotData = true;
      feed(c);
    }
    if (!gotData) return;
  }
}

static void scanI2c() {
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) status("I2C device at 0x%02X", addr);
  }
}

static bool probeGps() {
  Wire.beginTransmission(GPS_I2C_ADDR);
  gpsPresent = (Wire.endTransmission() == 0);
  if (gpsPresent) {
    status("PA1010D found at 0x%02X", GPS_I2C_ADDR);
    // RMC, GGA and GSA every fix, GSV every fifth: position, altitude, time, DOP and sky view
    sendPmtk("PMTK314,0,1,0,1,1,5,0,0,0,0,0,0,0,0,0,0,0,0,0");
    sendPmtk("PMTK220,1000");
  }
  return gpsPresent;
}

// Same probe Meshtastic uses: the low nibble of the controller's status byte is
// 0x8 (or 0x0) on an SH1106 and 0x3, 0x4, 0x6 or 0x7 on an SSD1306.
static bool probeOled() {
  const uint8_t addrs[] = {0x3C, 0x3D};
  for (uint8_t addr : addrs) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)0x00);
    Wire.endTransmission();
    Wire.requestFrom(addr, (uint8_t)1);
    uint8_t st = Wire.available() ? Wire.read() : 0xFF;
    bool sh1106 = ((st & 0x0F) == 0x08 || (st & 0x0F) == 0x00);
    if (sh1106) {
      oled = new U8G2_SH1106_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
    } else {
      oled = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
    }
    oled->setI2CAddress(addr << 1);
    oled->setBusClock(100000);  // the GPS shares the bus; a redraw is ~95 ms, done between bursts
    oled->begin();
    oled->setContrast(OLED_CONTRAST);
    status("OLED at 0x%02X (status 0x%02X), driving it as %s", addr, st, sh1106 ? "SH1106" : "SSD1306");

    oled->clearBuffer();
    oled->setFont(u8g2_font_helvB14_tr);
    oled->drawStr((128 - oled->getStrWidth("OscarGPS")) / 2, 30, "OscarGPS");
    oled->setFont(u8g2_font_6x10_tr);
    oled->drawStr((128 - oled->getStrWidth(FW_VERSION)) / 2, 48, FW_VERSION);
    oled->sendBuffer();
    return true;
  }
  return false;
}

static void drawCentred(const char *s, int y, int dx) {
  oled->drawStr((128 - oled->getStrWidth(s)) / 2 - 1 + dx, y, s);
}

// UTC, grid and a status line. Everything drifts by up to 2 px a minute against burn-in.
static void drawOled() {
  uint32_t minute = millis() / 60000;
  int dx = minute % 3;
  int dy = (minute / 3) % 2;

  oled->clearBuffer();
  if (!gpsPresent) {
    oled->setFont(u8g2_font_helvB14_tr);
    drawCentred("No GPS", 38 + dy, dx);
  } else {
    oled->setFont(u8g2_font_logisoso20_tn);
    drawCentred(*utcText ? utcText : "--:--:--", 22 + dy, dx);

    oled->setFont(u8g2_font_helvB14_tr);
    drawCentred(fixQuality > 0 && *gridText ? gridText : "no fix", 44 + dy, dx);

    char sats[12];
    snprintf(sats, sizeof(sats), "%u sats", satellites);
    oled->setFont(u8g2_font_6x10_tr);
    oled->drawStr(dx, 62 + dy - 1, sats);
    const char *bt = btConnected ? "BT linked" : "BT idle";
    oled->drawStr(126 - oled->getStrWidth(bt) + dx, 62 + dy - 1, bt);
  }
  oled->sendBuffer();
}

// Fast blink: no GPS on the bus. One flash a second: searching. Solid: fix.
// With a screen fitted the screen says it all, so the LED stays off to save battery.
static void updateLed() {
  uint32_t now = millis();
  bool on;
  if (oled) {
    on = false;
  } else if (!gpsPresent) {
    on = (now / 100) % 2;
  } else if (fixQuality > 0) {
    on = true;
  } else {
    on = (now % 1000) < 60;
  }
  digitalWrite(LED_PIN, on ? HIGH : LOW);
}

void setup() {
  setCpuFrequencyMhz(80);  // lowest clock Bluetooth runs at; saves battery
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  delay(200);
  status("OscarGPS %s, CPU %u MHz", FW_VERSION, getCpuFrequencyMhz());

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  scanI2c();

  if (!probeGps()) {
    status("No GPS at 0x%02X, check the STEMMA cable (SDA %d, SCL %d)", GPS_I2C_ADDR, I2C_SDA, I2C_SCL);
  }
  if (!probeOled()) status("No OLED at 0x3C or 0x3D, carrying on without one");

  if (SerialBT.begin(BT_NAME)) {
    const uint8_t *mac = esp_bt_dev_get_address();
    status("Bluetooth up as %s, %02X:%02X:%02X:%02X:%02X:%02X", BT_NAME,
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  } else {
    status("Bluetooth failed to start");
  }
}

void loop() {
  static uint32_t lastProbeMs = 0;
  static uint32_t lastDrawMs = 0;
  if (gpsPresent) pollGps();
  if ((!gpsPresent || !oled) && millis() - lastProbeMs > 5000) {
    lastProbeMs = millis();  // cable plugged in after boot
    if (!gpsPresent) probeGps();
    if (!oled) probeOled();
  }

  bool connected = SerialBT.hasClient();
  if (connected != btConnected) {
    btConnected = connected;
    redrawDue = true;
    status(connected ? "Bluetooth client connected" : "Bluetooth client disconnected");
  }

  if (oled && (redrawDue || millis() - lastDrawMs > OLED_REDRAW_MS + 500)) {
    redrawDue = false;
    lastDrawMs = millis();
    drawOled();
  }

  if (fixQuality > 0 && millis() - lastGgaMs > FIX_TIMEOUT_MS) {
    fixQuality = 0;
    status("GPS silent for %d s, treating as no fix", FIX_TIMEOUT_MS / 1000);
  }

  updateLed();
  delay(IDLE_POLL_MS);
}

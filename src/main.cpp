// OscarGPS: Adafruit Mini GPS PA1010D (I2C, STEMMA QT) on a FireBeetle ESP32,
// relayed as NMEA over Bluetooth Classic SPP and USB serial.
// OscarWatch reads it as a "Serial (COM port)" GPS: pair the board in Windows
// and pick the outgoing Bluetooth COM port it creates.
// Optional 128x64 I2C OLED (SSD1306 or SH1106) on the same bus cycles through
// UTC/grid/link, an ID-52 style sky plot and per-satellite signal bars.
// Optional battery gauge: VCC pin to A0 through a 100k/100k divider.

#include <Arduino.h>
#include <Wire.h>
#include <BluetoothSerial.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <esp_spp_api.h>
#include <esp_system.h>
#include <Preferences.h>
#include <U8g2lib.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED) || !defined(CONFIG_BT_SPP_ENABLED)
#error Bluetooth Classic SPP is not enabled in this core
#endif

#define BT_NAME         "OscarGPS"
// 1: once paired, the board dials the PC's Incoming COM port itself and redials after any
// drop or restart, so the app never has to reconnect. 0: the PC dials the board's Outgoing
// port, which Windows and OscarWatch don't recover from when the board restarts.
#define BT_DIAL_HOME    1
#define DIAL_RETRY_MS   3000   // pause after a failed dial
#define MAX_HOMES       4      // paired PCs to try in turn
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
#define PAGE_MAIN_MS    10000  // time on the UTC/grid page before the satellite pages
#define PAGE_SAT_MS     5000   // time on each satellite page
#define MAX_SATS        32
#define SAT_EXPIRE_MS   4000   // drop a satellite not reported in this long
#define BATT_PIN        36     // A0, fed from VCC through the divider
#define BATT_RATIO      2.0f   // 100k/100k; tweak to agree with a meter
#define BATT_USB_V      4.45f  // VCC above this is the USB rail, not the battery
#define BATT_READ_MS    2000

BluetoothSerial SerialBT;
static U8G2 *oled = nullptr;
static const char *oledName = "";
static uint8_t oledAddr = 0;

// Satellites in view, from GSV. sys is the talker's second letter: P GPS, L GLONASS,
// A Galileo, B BeiDou.
struct Sat {
  char sys;
  uint8_t prn;
  int8_t elev;      // degrees, -1 unknown
  int16_t az;       // degrees, -1 unknown
  uint8_t snr;      // dB-Hz, 0 not tracked
  uint32_t seenMs;  // 0 marks an empty slot
};
static Sat sats[MAX_SATS];

// PRNs used in the fix, from the last GSA of each system
struct UsedList {
  char sys;
  uint8_t prn[12];
  uint8_t n;
  uint32_t ms;
};
static UsedList used[4] = {{'P'}, {'L'}, {'A'}, {'B'}};
static char hdopText[6] = "";
static float battVolts = 0;  // smoothed VCC; 0 means no divider fitted

// Paired PCs, from the board's own bond list, and the dialler's state
static esp_bd_addr_t homes[MAX_HOMES];
static int homeCount = 0;
static volatile bool dialling = false;
static volatile uint32_t dialFailures = 0;
// What the PC calls its serial service, from SDP. Windows only offers the service while
// an app has the Incoming port open, so the last name seen is kept in NVS: after a restart
// the screen can still say which port to open.
static char pcServiceName[32] = "";
static Preferences prefs;

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
  nmeaField(s, 8, hdopText, sizeof(hdopText));
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

static int nmeaFieldCount(const char *s) {
  int n = 1;
  for (; *s && *s != '*'; s++) {
    if (*s == ',') n++;
  }
  return n;
}

static Sat *satSlot(char sys, uint8_t prn) {
  Sat *oldest = &sats[0];
  for (Sat &st : sats) {
    if (st.seenMs && st.sys == sys && st.prn == prn) return &st;
  }
  for (Sat &st : sats) {
    if (!st.seenMs) return &st;
    if (st.seenMs < oldest->seenMs) oldest = &st;
  }
  return oldest;
}

// Groups of PRN, elevation, azimuth, SNR after 4 header fields. An NMEA 4.1 signal ID
// at the end is a lone field, which the whole-group count leaves out.
static void parseGsv(const char *s, char sys) {
  char f[8];
  int groups = (nmeaFieldCount(s) - 4) / 4;
  for (int g = 0; g < groups && g < 4; g++) {
    int base = 4 + g * 4;
    nmeaField(s, base, f, sizeof(f));
    uint8_t prn = (uint8_t)atoi(f);
    if (!prn) continue;
    Sat *st = satSlot(sys, prn);
    st->sys = sys;
    st->prn = prn;
    nmeaField(s, base + 1, f, sizeof(f));
    st->elev = *f ? (int8_t)atoi(f) : -1;
    nmeaField(s, base + 2, f, sizeof(f));
    st->az = *f ? (int16_t)atoi(f) : -1;
    nmeaField(s, base + 3, f, sizeof(f));
    st->snr = (uint8_t)atoi(f);
    st->seenMs = millis();
  }
}

static UsedList *usedFor(char sys) {
  for (UsedList &u : used) {
    if (u.sys == sys) return &u;
  }
  return nullptr;
}

static void parseGsa(const char *s, char sys) {
  char f[8];
  if (sys == 'N') {  // combined $GNGSA: NMEA 4.1 puts the system in field 18
    nmeaField(s, 18, f, sizeof(f));
    sys = *f == '2' ? 'L' : *f == '3' ? 'A' : *f == '4' ? 'B' : 'P';
  }
  UsedList *u = usedFor(sys);
  if (!u) return;
  u->n = 0;
  for (int i = 3; i <= 14; i++) {
    nmeaField(s, i, f, sizeof(f));
    if (*f && u->n < 12) u->prn[u->n++] = (uint8_t)atoi(f);
  }
  u->ms = millis();
}

static bool satFresh(const Sat &st) {
  return st.seenMs && millis() - st.seenMs < SAT_EXPIRE_MS;
}

static bool satUsed(const Sat &st) {
  const UsedList *u = usedFor(st.sys);
  if (!u || millis() - u->ms > SAT_EXPIRE_MS) return false;
  for (uint8_t i = 0; i < u->n; i++) {
    if (u->prn[i] == st.prn) return true;
  }
  return false;
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
  if (lineLen > 7 && memcmp(line + 3, "GSV,", 4) == 0) parseGsv(line, line[2]);
  if (lineLen > 7 && memcmp(line + 3, "GSA,", 4) == 0) parseGsa(line, line[2]);
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
    // RMC, GGA, GSA and GSV every fix: position, altitude, time, DOP and a live sky view
    sendPmtk("PMTK314,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0");
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
    oledName = sh1106 ? "SH1106" : "SSD1306";
    oledAddr = addr;
    status("OLED at 0x%02X (status 0x%02X), driving it as %s", addr, st, oledName);
    return true;
  }
  return false;
}

static void drawCentred(const char *s, int y, int dx) {
  oled->drawStr((128 - oled->getStrWidth(s)) / 2 - 1 + dx, y, s);
}

// Resting LiPo voltage to charge; rough, but plenty for a gauge.
static int battPercent(float v) {
  static const float curve[][2] = {
    {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.92f, 70}, {3.86f, 60}, {3.82f, 50},
    {3.79f, 40},  {3.76f, 30}, {3.73f, 20}, {3.68f, 10}, {3.50f, 0},
  };
  const int n = sizeof(curve) / sizeof(curve[0]);
  if (v >= curve[0][0]) return 100;
  for (int i = 1; i < n; i++) {
    if (v >= curve[i][0]) {
      float t = (v - curve[i][0]) / (curve[i - 1][0] - curve[i][0]);
      return (int)(curve[i][1] + t * (curve[i - 1][1] - curve[i][1]) + 0.5f);
    }
  }
  return 0;
}

static void readBattery() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(BATT_PIN);
  float v = mv / 16.0f / 1000.0f * BATT_RATIO;
  if (v < 1.0f) {  // an unconnected A0 sits near 0 V: no divider fitted
    battVolts = 0;
    return;
  }
  battVolts = (battVolts > 0 && fabsf(v - battVolts) < 0.3f) ? battVolts * 0.8f + v * 0.2f : v;
}

// Battery icon with the percentage under it, or "USB" when VCC is the USB rail.
static void drawBattery(int cx, int top) {
  oled->setFont(u8g2_font_4x6_tr);
  if (battVolts > BATT_USB_V) {
    oled->setFont(u8g2_font_5x7_tr);
    oled->drawStr(cx - oled->getStrWidth("USB") / 2, top + 10, "USB");
    return;
  }
  int pct = battPercent(battVolts);
  oled->drawFrame(cx - 7, top, 13, 7);
  oled->drawBox(cx + 6, top + 2, 2, 3);
  oled->drawBox(cx - 5, top + 2, (9 * pct + 50) / 100, 3);
  char t[6];
  if (pct <= 10) {
    strcpy(t, "LOW");
  } else {
    snprintf(t, sizeof(t), "%d%%", pct);
  }
  oled->drawStr(cx - oled->getStrWidth(t) / 2, top + 14, t);
}

// UTC, grid, satellites in use and the link.
static void drawMainPage(int dx, int dy) {
  // Time plus a small "UTC" tag on the same baseline, centred as a pair
  const char *t = *utcText ? utcText : "--:--:--";
  oled->setFont(u8g2_font_6x10_tr);
  int tagW = oled->getStrWidth("UTC");
  oled->setFont(u8g2_font_logisoso20_tn);
  int timeW = oled->getStrWidth(t);
  int x = (128 - (timeW + 3 + tagW)) / 2 - 1 + dx;
  oled->drawStr(x, 22 + dy, t);
  oled->setFont(u8g2_font_6x10_tr);
  oled->drawStr(x + timeW + 3, 22 + dy, "UTC");

  // Grid, shifted left to make room for the battery when a divider is fitted
  const char *g = fixQuality > 0 && *gridText ? gridText : "no fix";
  oled->setFont(u8g2_font_helvB14_tr);
  if (battVolts > 0) {
    oled->drawStr((100 - oled->getStrWidth(g)) / 2 + dx, 44 + dy, g);
    drawBattery(113 + dx, 30 + dy);
  } else {
    drawCentred(g, 44 + dy, dx);
  }

  char n[12];
  snprintf(n, sizeof(n), "%u sats", satellites);
  oled->setFont(u8g2_font_6x10_tr);
  oled->drawStr(dx, 61 + dy, n);
  // Not linked but the PC has told us its port (Windows names the service "COM11" etc):
  // that's the port to pick in OscarWatch, so say so.
  char bt[24];
  if (btConnected) {
    strcpy(bt, "Oscar linked");
  } else if (strncmp(pcServiceName, "COM", 3) == 0) {
    snprintf(bt, sizeof(bt), "use %s", pcServiceName);
  } else {
    strcpy(bt, dialling ? "dialling" : "not linked");
  }
  oled->drawStr(126 - oled->getStrWidth(bt) + dx, 61 + dy, bt);
}

static const char *sysName(char sys) {
  switch (sys) {
    case 'P': return "GPS";
    case 'L': return "GLO";
    case 'A': return "GAL";
    case 'B': return "BDS";
    default:  return "GNS";
  }
}

// ID-52 style sky plot: horizon ring, 45 degree ring, north up. Satellites are drawn
// as their PRN, boxed in reverse when they're in the fix. Counts and DOP on the right.
static void drawSkyPage(int dx, int dy) {
  const int cx = 31 + dx, cy = 32 + dy, r = 30;
  oled->drawCircle(cx, cy, r);
  oled->drawCircle(cx, cy, r / 2);
  oled->drawPixel(cx, cy);
  oled->drawVLine(cx, cy - r, 3);
  oled->setFont(u8g2_font_4x6_tr);
  oled->drawStr(cx + 2, cy - r + 6, "N");

  for (const Sat &st : sats) {
    if (!satFresh(st) || st.elev < 0 || st.az < 0) continue;
    float rad = r * (90 - st.elev) / 90.0f;
    float a = st.az * DEG_TO_RAD;
    int x = cx + (int)lroundf(rad * sinf(a));
    int y = cy - (int)lroundf(rad * cosf(a));
    char p[4];
    snprintf(p, sizeof(p), "%u", st.prn);
    int w = oled->getStrWidth(p);
    if (satUsed(st)) {
      oled->drawBox(x - w / 2 - 1, y - 4, w + 2, 7);
      oled->setDrawColor(0);
      oled->drawStr(x - w / 2, y + 2, p);
      oled->setDrawColor(1);
    } else {
      oled->drawStr(x - w / 2, y + 2, p);
    }
  }

  // Per-system used/in-view, then fix and HDOP, then battery volts for calibration
  oled->setFont(u8g2_font_5x7_tr);
  int y = 9 + dy;
  const char systems[] = {'P', 'L', 'A', 'B'};
  for (char sys : systems) {
    int view = 0, inFix = 0;
    for (const Sat &st : sats) {
      if (st.sys != sys || !satFresh(st)) continue;
      view++;
      if (satUsed(st)) inFix++;
    }
    if (!view) continue;
    char t[16];
    snprintf(t, sizeof(t), "%s %d/%d", sysName(sys), inFix, view);
    oled->drawStr(68 + dx, y, t);
    y += 9;
  }
  char t[16];
  snprintf(t, sizeof(t), "%s", fixQuality > 0 ? "fix" : "no fix");
  oled->drawStr(68 + dx, y, t);
  y += 9;
  if (*hdopText && fixQuality > 0) {
    snprintf(t, sizeof(t), "HDOP %s", hdopText);
    oled->drawStr(68 + dx, y, t);
    y += 9;
  }
  if (battVolts > 0) {
    snprintf(t, sizeof(t), "%s %.2fV", battVolts > BATT_USB_V ? "USB" : "Bat", battVolts);
    oled->drawStr(68 + dx, y, t);
  }
}

// Signal bars for up to 12 satellites, PRN underneath. Solid bars are in the fix.
// Satellites in the fix come first, then the strongest; shown in PRN order so bars
// keep their place from one second to the next.
static void drawSignalPage(int dx, int dy) {
  const Sat *pick[12];
  int n = 0;
  for (int pass = 0; pass < 2 && n < 12; pass++) {
    while (n < 12) {
      const Sat *best = nullptr;
      for (const Sat &st : sats) {
        if (!satFresh(st) || satUsed(st) != (pass == 0)) continue;
        bool taken = false;
        for (int i = 0; i < n; i++) taken |= (pick[i] == &st);
        if (!taken && (!best || st.snr > best->snr)) best = &st;
      }
      if (!best) break;
      pick[n++] = best;
    }
  }
  for (int i = 1; i < n; i++) {  // insertion sort by system, then PRN
    const Sat *k = pick[i];
    int j = i - 1;
    while (j >= 0 && (pick[j]->sys > k->sys || (pick[j]->sys == k->sys && pick[j]->prn > k->prn))) {
      pick[j + 1] = pick[j];
      j--;
    }
    pick[j + 1] = k;
  }

  oled->setFont(u8g2_font_4x6_tr);
  if (!n) {
    drawCentred("no satellites", 34 + dy, dx);
    return;
  }
  const int base = 54 + dy, maxH = 48, pitch = 10;
  int x0 = (128 - n * pitch) / 2 + 1 + dx;
  for (int i = 0; i < n; i++) {
    const Sat *st = pick[i];
    int h = st->snr * maxH / 50;
    if (h > maxH) h = maxH;
    int x = x0 + i * pitch;
    if (h < 1) {
      oled->drawHLine(x, base - 1, 8);
    } else if (satUsed(*st)) {
      oled->drawBox(x, base - h, 8, h);
    } else {
      oled->drawFrame(x, base - h, 8, h);
    }
    char p[4];
    snprintf(p, sizeof(p), "%u", st->prn);
    oled->drawStr(x + 4 - oled->getStrWidth(p) / 2, base + 8, p);
  }
}

// Cycles main, sky, signal. Everything drifts by up to 2 px a minute against burn-in.
static void drawOled() {
  static uint8_t page = 0;
  static uint32_t pageStartMs = 0;
  uint32_t now = millis();

  bool anySats = false;
  for (const Sat &st : sats) anySats |= satFresh(st);
  if (!anySats) {
    page = 0;
  } else if (now - pageStartMs > (page == 0 ? PAGE_MAIN_MS : PAGE_SAT_MS)) {
    page = (page + 1) % 3;
    pageStartMs = now;
  }

  uint32_t minute = now / 60000;
  int dx = minute % 3;
  int dy = (minute / 3) % 2;

  oled->clearBuffer();
  if (!gpsPresent) {
    oled->setFont(u8g2_font_helvB14_tr);
    drawCentred("No GPS", 38 + dy, dx);
  } else if (page == 1) {
    drawSkyPage(dx, dy);
  } else if (page == 2) {
    drawSignalPage(dx, dy);
  } else {
    drawMainPage(dx, dy);
  }
  oled->sendBuffer();
}

// ── Retro BIOS boot screen ────────────────────────────────────────────────
// Same idea as the RAPT and weather displays' POST, cut down to 128x64 and mono.
// Fixed-width 4x6 font on purpose: the dot leaders line up on its 4 px advance, 32
// columns. Title bar stays put; the rows below scroll like a real POST. Unlike theirs,
// the checks are real: each result is what the probe actually found.
// Does nothing without a screen, so a headless board boots at full speed.
#define POST_ROWS    7
#define POST_COLS    32
#define POST_DOT_COL 26   // dots run to here, then " RESULT" (up to 5 chars)
static char postRows[POST_ROWS][POST_COLS + 1];
static int postUsed = 0;
static uint32_t postCheckMs = 0;

static void postRedraw() {
  oled->clearBuffer();
  oled->setFont(u8g2_font_4x6_tr);
  oled->drawStr(0, 5, "OSCARGPS BIOS v" FW_VERSION);
  oled->drawStr(0, 11, "(C) 2026 2D0PEY");
  oled->drawHLine(0, 13, 128);
  for (int i = 0; i < postUsed; i++) oled->drawStr(0, 21 + i * 7, postRows[i]);
  oled->sendBuffer();
}

static void postLine(int pauseMs, const char *fmt, ...) {
  if (!oled) return;
  if (postUsed == POST_ROWS) {
    memmove(postRows[0], postRows[1], sizeof(postRows[0]) * (POST_ROWS - 1));
    postUsed--;
  }
  va_list args;
  va_start(args, fmt);
  vsnprintf(postRows[postUsed++], POST_COLS + 1, fmt, args);
  va_end(args);
  postRedraw();
  delay(pauseMs);
}

// Label and dot leader now; the result lands on the same row once the check is done.
static void postBegin(const char *label) {
  if (!oled) return;
  char buf[POST_COLS + 1];
  int n = snprintf(buf, sizeof(buf), "%s ", label);
  while (n < POST_DOT_COL) buf[n++] = '.';
  buf[n] = '\0';
  postLine(0, "%s", buf);
  postCheckMs = millis();
}

static void postResult(const char *result) {
  if (!oled) return;
  int32_t wait = 350 - (int32_t)(millis() - postCheckMs);  // a check always takes a beat
  if (wait > 0) delay(wait);
  char *row = postRows[postUsed - 1];
  snprintf(row + strlen(row), POST_COLS + 1 - strlen(row), " %s", result);
  postRedraw();
  delay(60);
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

// Keeps a link to a paired PC up. connect() does an SDP lookup for the PC's serial port
// service and then opens RFCOMM, blocking for up to ~10 s when the PC isn't there, so it
// lives in its own task and the GPS, screen and USB output never wait on it. Paired PCs
// are tried in turn. Nothing is printed from here: loop() owns the USB serial port.
static void dialTask(void *) {
  int next = 0;
  for (;;) {
    if (!SerialBT.hasClient()) {
      dialling = true;
      bool ok = SerialBT.connect(homes[next % homeCount]);
      dialling = false;
      if (!ok) {
        dialFailures++;
        next++;
        vTaskDelay(pdMS_TO_TICKS(DIAL_RETRY_MS));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// Runs in the Bluetooth task after the library's own handler. Keeps the name the PC gives
// its serial port service when the dialler looks it up.
static void sppEvent(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (event == ESP_SPP_DISCOVERY_COMP_EVT && param->disc_comp.status == ESP_SPP_SUCCESS &&
      param->disc_comp.scn_num > 0 && param->disc_comp.service_name[0]) {
    strlcpy(pcServiceName, param->disc_comp.service_name[0], sizeof(pcServiceName));
  }
}

// Starts Bluetooth. In dial-home mode with a paired PC, starts the dialler; with nothing
// paired yet, offers the serial port service instead so the PC can pair and connect.
static bool startBluetooth() {
  prefs.begin("oscargps", false);
  prefs.getString("pcport", pcServiceName, sizeof(pcServiceName));
  SerialBT.register_callback(sppEvent);
  if (!SerialBT.begin(BT_NAME, BT_DIAL_HOME)) return false;
  const uint8_t *mac = esp_bt_dev_get_address();
  status("Bluetooth up as %s, %02X:%02X:%02X:%02X:%02X:%02X", BT_NAME,
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  if (!BT_DIAL_HOME) return true;

  SerialBT.isReady(true, 5000);
  int n = MAX_HOMES;
  if (esp_bt_gap_get_bond_device_list(&n, homes) != ESP_OK) n = 0;
  homeCount = n;
  if (homeCount == 0) {
    esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "ESP32SPP");
    status("Nothing paired yet: pair OscarGPS from the PC, then restart the board");
    return true;
  }
  for (int i = 0; i < homeCount; i++) {
    status("Dialling paired PC %02X:%02X:%02X:%02X:%02X:%02X", homes[i][0], homes[i][1],
           homes[i][2], homes[i][3], homes[i][4], homes[i][5]);
  }
  xTaskCreatePinnedToCore(dialTask, "dial", 4096, nullptr, 1, nullptr, 1);
  return true;
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

  // Screen first, so the POST has somewhere to draw
  if (!probeOled()) status("No OLED at 0x3C or 0x3D, carrying on without one");

  uint8_t btMac[6];
  esp_read_mac(btMac, ESP_MAC_BT);
  postLine(90, "CPU  : %s r%d %uMHz", ESP.getChipModel(), ESP.getChipRevision(), getCpuFrequencyMhz());
  postLine(90, "Flash: %uMB  SRAM: 520KB", ESP.getFlashChipSize() / (1024 * 1024));
  postLine(90, "BT   : %02X:%02X:%02X:%02X:%02X:%02X",
           btMac[0], btMac[1], btMac[2], btMac[3], btMac[4], btMac[5]);
  postLine(90, "I2C  : SDA%d SCL%d 100kHz", I2C_SDA, I2C_SCL);
  postLine(250, "Detecting peripherals...");

  postBegin("GPS PA1010D 0x10");
  if (!probeGps()) {
    status("No GPS at 0x%02X, check the STEMMA cable (SDA %d, SCL %d)", GPS_I2C_ADDR, I2C_SDA, I2C_SCL);
  }
  postResult(gpsPresent ? "OK" : "NONE");

  if (oled) {
    char label[24];
    snprintf(label, sizeof(label), "Display %s 0x%02X", oledName, oledAddr);
    postBegin(label);
    postResult("OK");
  }

  postBegin("Battery sense A0");
  analogSetPinAttenuation(BATT_PIN, ADC_11db);
  readBattery();
  char batt[8] = "NONE";
  if (battVolts > BATT_USB_V) {
    strcpy(batt, "USB");
  } else if (battVolts > 0) {
    snprintf(batt, sizeof(batt), "%.2fV", battVolts);
  }
  if (battVolts > 0) {
    status("Battery sense on A0: %.2f V%s", battVolts, battVolts > BATT_USB_V ? " (USB)" : "");
  } else {
    status("No battery divider on A0 (reads %u mV; a fitted divider reads 1700 to 2400)", analogReadMilliVolts(BATT_PIN));
  }
  postResult(batt);

  postBegin("Bluetooth SPP");
  bool btOk = startBluetooth();
  if (!btOk) status("Bluetooth failed to start");
  postResult(btOk ? "OK" : "FAIL");
  if (btOk && BT_DIAL_HOME) {
    postBegin("Paired PC");
    postResult(homeCount ? "OK" : "NONE");
  }

  // First valid GGA from the GPS proves the whole I2C and NMEA path
  if (gpsPresent && oled) {
    postBegin("NMEA stream");
    uint32_t start = millis();
    while (!lastGgaMs && millis() - start < 2500) {
      pollGps();
      delay(IDLE_POLL_MS);
    }
    postResult(lastGgaMs ? "OK" : "WAIT");
  }

  postLine(150, "Press DEL to enter SETUP");
  postLine(2000, "Starting OscarGPS...");
}

void loop() {
  static uint32_t lastProbeMs = 0;
  static uint32_t lastDrawMs = 0;
  static uint32_t lastBattMs = 0;
  if (gpsPresent) pollGps();
  if (millis() - lastBattMs > BATT_READ_MS) {
    lastBattMs = millis();
    readBattery();
  }
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

  // Reports a new name and stores it; the first pass reports the one loaded from NVS
  static char reportedService[32] = "";
  if (strcmp(reportedService, pcServiceName) != 0) {
    strlcpy(reportedService, pcServiceName, sizeof(reportedService));
    status("PC serial service is called \"%s\"", reportedService);
    if (prefs.getString("pcport", "") != reportedService) prefs.putString("pcport", reportedService);
  }

  // The first failed dial, then every 20th, so a PC that's off doesn't flood the log
  static uint32_t reportedFailures = 0;
  uint32_t failures = dialFailures;
  if (failures != reportedFailures && (failures == 1 || failures % 20 == 0)) {
    reportedFailures = failures;
    status("Dial failed (%u so far): is the PC on, with its Incoming port open?", failures);
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

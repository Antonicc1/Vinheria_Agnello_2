#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <EEPROM.h>


#define PIN_DHT       2
#define PIN_BTN_OK    5
#define PIN_BTN_CAN   6
#define PIN_BUZZER    8
#define PIN_LED_R     9
#define PIN_LED_G     10
#define PIN_LED_B     7
#define PIN_LDR       A0

#define PIN_JOY_Y     A1
#define PIN_RTC_CLK   11
#define PIN_RTC_DAT   12
#define PIN_RTC_RST   13



#define LCD_ADDR 0x27

const uint8_t  LDR_GREEN_MIN   = 0;
const uint8_t  LDR_GREEN_MAX   = 50;
const uint16_t LDR_YELLOW_MAX  = 200;

const uint8_t  UMID_GREEN_MIN  = 30;
const uint8_t  UMID_GREEN_MAX  = 45;
const uint8_t  UMID_YELLOW_MAX = 60;

const uint8_t  TEMP_GREEN_MIN  = 20;
const uint8_t  TEMP_GREEN_MAX  = 25;
const uint8_t  TEMP_YELLOW_MAX = 30;

const uint16_t BUZZ_AMARELO  = 500;
const uint16_t BUZZ_VERMELHO = 1000;

const uint16_t JOY_CENTER     = 512;
const uint16_t JOY_DEADZONE   = 200;

const uint8_t  EEP_MAGIC      = 0xB1;
const uint16_t EEP_ADDR_CFG   = 0;    
const uint16_t EEP_ADDR_LOGN  = 16;
const uint16_t EEP_ADDR_HEAD  = 18;
const uint16_t EEP_ADDR_LOGS  = 32;
const uint8_t  LOG_ENTRY_SIZE = 8;
const uint16_t LOG_MAX        = (1024 - EEP_ADDR_LOGS) / LOG_ENTRY_SIZE;

struct Settings {
  uint8_t  magic;
  int8_t   utcOffset;
  uint8_t  tempUnit;
  uint8_t  language;
  uint8_t  logIntervalS;
  uint16_t ldrMin;
  uint16_t ldrMax;
  uint8_t  buzzerOn;
};

struct LogEntry {
  uint32_t ts;
  int8_t   tempC;
  uint8_t  humid;
  uint8_t  ldr;
  uint8_t  status;
};

LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);
DHT               dht(PIN_DHT, DHT11);
ThreeWire         rtcWire(PIN_RTC_DAT, PIN_RTC_CLK, PIN_RTC_RST);
RtcDS1302<ThreeWire> rtc(rtcWire);
Settings          cfg;

enum AppMode { MODE_BOOT, MODE_NORMAL, MODE_MENU, MODE_LOGS};
AppMode appMode = MODE_BOOT;

bool lcdOk = false;
bool rtcOk = false;
bool freshConfig = true;

const uint8_t WINDOW_S = 5;
float    sumT = 0, sumH = 0;
uint32_t sumL = 0;
uint8_t  nSamples = 0;

float   curT = 0, curH = 0;
uint8_t curL = 0;
uint8_t curStatus = 0;
bool homeShowValues = false;

uint16_t ldrRawMin = 1023;
uint16_t ldrRawMax = 0;

unsigned long tLastSample = 0;
unsigned long tLastWindow = 0;
unsigned long tLastLog    = 0;
unsigned long tLastBtn    = 0;
unsigned long tLastFrame  = 0;
unsigned long bootMillis  = 0;

uint8_t menuIdx = 0;
const uint8_t MENU_ITEMS = 6;
uint16_t logViewIdx = 0;

byte chBottle1[8] = { 0b00100,0b01110,0b01010,0b01010,0b11111,0b11111,0b11111,0b11111 };
byte chBottle2[8] = { 0b00100,0b01110,0b01010,0b11111,0b11111,0b11111,0b11111,0b11111 };
byte chGrape  [8] = { 0b00000,0b01110,0b11111,0b11111,0b01110,0b00100,0b00100,0b00000 };
byte chThermo [8] = { 0b00100,0b01010,0b01010,0b01110,0b01110,0b11111,0b11111,0b01110 };
byte chDrop   [8] = { 0b00100,0b00100,0b01010,0b10001,0b10001,0b10001,0b01110,0b00000 };
byte chSun    [8] = { 0b00100,0b10101,0b01110,0b11011,0b01110,0b10101,0b00100,0b00000 };
byte chOk     [8] = { 0b00000,0b00001,0b00011,0b10110,0b11100,0b01000,0b00000,0b00000 };
byte chAlerta [8] = { 0b00000,0b00100,0b01110,0b01110,0b01010,0b11111,0b00100,0b00000 };
byte chCritico[8] = { 0b01110,0b11111,0b10101,0b11011,0b11111,0b01110,0b01010,0b00000 };

#define ICO_BOTTLE   0
#define ICO_BOTTLE2  1
#define ICO_CRITICO  1
#define ICO_GRAPE    2
#define ICO_THERMO   3
#define ICO_DROP     4
#define ICO_SUN      5
#define ICO_OK       6
#define ICO_ALERTA   7

void cfgSave() {
  cfg.ldrMin = ldrRawMin;
  cfg.ldrMax = ldrRawMax;
  EEPROM.put(EEP_ADDR_CFG, cfg);
}

void cfgLoad() {
  EEPROM.get(EEP_ADDR_CFG, cfg);
  cfg.utcOffset = -3;
  if (cfg.magic != EEP_MAGIC) {
    freshConfig = true;
    cfg.magic        = EEP_MAGIC;
    cfg.utcOffset    = -3;
    cfg.tempUnit     = 0;
    cfg.language     = 0;
    cfg.logIntervalS = 60;
    cfg.ldrMin       = 1023;
    cfg.ldrMax       = 0;
    cfg.buzzerOn     = 1;
    cfgSave();
    uint16_t z = 0;
    EEPROM.put(EEP_ADDR_LOGN, z);
    EEPROM.put(EEP_ADDR_HEAD, z);
  }
  if (cfg.ldrMin <= 1023) ldrRawMin = cfg.ldrMin;
  if (cfg.ldrMax <= 1023) ldrRawMax = cfg.ldrMax;
}

void logWrite(const LogEntry &e) {
  uint16_t head, count;
  EEPROM.get(EEP_ADDR_HEAD, head);
  EEPROM.get(EEP_ADDR_LOGN, count);
  if (head >= LOG_MAX) head = 0;
  EEPROM.put(EEP_ADDR_LOGS + head * LOG_ENTRY_SIZE, e);
  head = (head + 1) % LOG_MAX;
  if (count < LOG_MAX) count++;
  EEPROM.put(EEP_ADDR_HEAD, head);
  EEPROM.put(EEP_ADDR_LOGN, count);
}

LogEntry logRead(uint16_t idx) {
  LogEntry e;
  EEPROM.get(EEP_ADDR_LOGS + idx * LOG_ENTRY_SIZE, e);
  return e;
}

void logClearAll() {
  uint16_t z = 0;
  EEPROM.put(EEP_ADDR_LOGN, z);
  EEPROM.put(EEP_ADDR_HEAD, z);
}

uint8_t readLDR() {
  uint16_t raw = analogRead(PIN_LDR);
  if (raw < ldrRawMin) ldrRawMin = raw;
  if (raw > ldrRawMax) ldrRawMax = raw;
  if ((ldrRawMax - ldrRawMin) < 50) {
    return constrain(map(raw, 0, 1023, 0, 255), 0, 255);
  }
  return constrain(map(raw, ldrRawMin, ldrRawMax, 0, 255), 0, 255);
}

void sensorSample() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  uint8_t l = readLDR();
  if (!isnan(t) && !isnan(h)) {
    sumT += t; sumH += h; sumL += l; nSamples++;
  } else {
    Serial.println(F("[WARN] DHT11 retornou NaN"));
  }
}

uint8_t evalZone(float v, float gMin, float gMax, float yMax) {
  if (v >= gMin && v <= gMax) return 0;
  if (v <= yMax)              return 1;
  return 2;
}

void windowFinalize() {
  if (nSamples == 0) {
    Serial.println(F("[WARN] Janela 10s sem amostras validas"));
    return;
  }
  curT = sumT / nSamples;
  curH = sumH / nSamples;
  curL = sumL / nSamples;

  uint8_t zT = evalZone(curT, TEMP_GREEN_MIN, TEMP_GREEN_MAX, TEMP_YELLOW_MAX);
  uint8_t zH = evalZone(curH, UMID_GREEN_MIN, UMID_GREEN_MAX, UMID_YELLOW_MAX);
  uint8_t zL = evalZone(curL, LDR_GREEN_MIN,  LDR_GREEN_MAX,  LDR_YELLOW_MAX);
  curStatus = max(max(zT, zH), zL);

  Serial.print(F("[10s] T=")); Serial.print(curT, 1);
  Serial.print(F("C  U=")); Serial.print(curH, 1);
  Serial.print(F("%  L=")); Serial.print(curL);
  Serial.print(F("/255  zT=")); Serial.print(zT);
  Serial.print(F(" zH="));      Serial.print(zH);
  Serial.print(F(" zL="));      Serial.print(zL);
  Serial.print(F(" status=")); Serial.println(curStatus);

  sumT = sumH = 0; sumL = 0; nSamples = 0;
  cfgSave();
}

void setRGB(uint8_t r, uint8_t g) {
  analogWrite(PIN_LED_R, r);
  analogWrite(PIN_LED_G, g);
}

void setBlue(bool on) {
  digitalWrite(PIN_LED_B, on ? HIGH : LOW);
}

void alertApply() {
  switch (curStatus) {
    case 0: setRGB(0, 255); noTone(PIN_BUZZER); break;
    case 1:
      setRGB(255, 80);
      cfg.buzzerOn ? tone(PIN_BUZZER, BUZZ_AMARELO) : noTone(PIN_BUZZER);
      break;
    case 2:
      setRGB(255, 0);
      cfg.buzzerOn ? tone(PIN_BUZZER, BUZZ_VERMELHO) : noTone(PIN_BUZZER);
      break;
  }
}

const char* zoneText(uint8_t z) {
  if (cfg.language == 0) {
    if (z == 0) return "OK     ";
    if (z == 1) return "ALERTA ";
    return            "CRITICO";
  } else if (cfg.language == 1) {
    if (z == 0) return "OK     ";
    if (z == 1) return "ALERT  ";
    return            "DANGER ";
  } else {
    if (z == 0) return "OK     ";
    if (z == 1) return "ALERTA ";
    return            "CRITICO";
  }
}

float tempForDisplay(float c) {
  return (cfg.tempUnit == 1) ? (c * 9.0 / 5.0 + 32.0) : c;
}
char tempUnitChar() { return cfg.tempUnit == 1 ? 'F' : 'C'; }

uint32_t nowUnix() {
  if (rtcOk) {
    RtcDateTime t = rtc.GetDateTime();
    return t.TotalSeconds();
  }
  return (millis() - bootMillis) / 1000UL;
}

RtcDateTime nowLocal() {
  return RtcDateTime(nowUnix() + (int32_t)cfg.utcOffset * 3600L);
}

void bootAnimation() {
  if (!lcdOk) return;
  lcd.clear();

  for (uint8_t i = 0; i < 10; i++) {
    lcd.setCursor(9 - i, 1);  lcd.write(ICO_GRAPE);
    lcd.setCursor(10 + i, 1); lcd.write(ICO_GRAPE);
    delay(40);
  }
  delay(200);

  lcd.setCursor(0, 1); lcd.print(F("                    "));
  const char* t1 = "VINHERIA AGNELLO";
  uint8_t col = (20 - 16) / 2;
  for (uint8_t i = 0; i < 16; i++) {
    lcd.setCursor(col + i, 1);
    lcd.print(t1[i]);
    delay(60);
  }

  const char* t2 = "Sistema de Monit.";
  for (uint8_t i = 0; i < 17; i++) {
    lcd.setCursor(2 + i, 2);
    lcd.print(t2[i]);
    delay(30);
  }
  for (uint8_t f = 0; f < 8; f++) {
    lcd.setCursor(0, 3);  lcd.write(ICO_GRAPE);
    lcd.setCursor(19, 3); lcd.write(ICO_GRAPE);
    lcd.setCursor(9, 3);  lcd.write((f % 2) ? ICO_BOTTLE : ICO_BOTTLE2);
    delay(180);
  }
  delay(400);
  lcd.clear();
}

uint8_t zoneEmoji(uint8_t z) {
  if (z == 0) return ICO_OK;
  if (z == 1) return ICO_ALERTA;
  return ICO_CRITICO;
}

void drawSensorRow(uint8_t row, uint8_t lineIcon, uint8_t pct, uint8_t zone) {
  lcd.setCursor(0, row);
  lcd.write(lineIcon);
  lcd.print(' ');

  uint8_t filled = (uint16_t)pct * 8 / 100;
  if (filled > 8) filled = 8;
  for (uint8_t i = 0; i < 8; i++) {
    lcd.write(i < filled ? (uint8_t)0xFF : (uint8_t)' ');
  }

  lcd.print(' ');
  lcd.print(zoneText(zone));
  lcd.print(' ');
  lcd.write(zoneEmoji(zone));
}

void drawValueRow(uint8_t row, uint8_t lineIcon,
                  const char* valStr, uint8_t zone) {
  lcd.setCursor(0, row);
  lcd.write(lineIcon);
  lcd.print(' ');
  char buf[9];
  snprintf(buf, sizeof(buf), "%-8s", valStr);
  lcd.print(buf);
  lcd.print(' ');
  lcd.print(zoneText(zone));
  lcd.print(' ');
  lcd.write(zoneEmoji(zone));
}

void renderHome() {
  if (!lcdOk) return;

  RtcDateTime dt = nowLocal();
  char buf[21];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d ",
           dt.Day(), dt.Month(), dt.Year(),
           dt.Hour(), dt.Minute(), dt.Second());
  lcd.setCursor(0, 0);
  lcd.print(buf);

  uint8_t zT = evalZone(curT, TEMP_GREEN_MIN, TEMP_GREEN_MAX, TEMP_YELLOW_MAX);
  uint8_t zH = evalZone(curH, UMID_GREEN_MIN, UMID_GREEN_MAX, UMID_YELLOW_MAX);
  uint8_t zL = evalZone(curL, LDR_GREEN_MIN,  LDR_GREEN_MAX,  LDR_YELLOW_MAX);

  uint8_t pctT = constrain((int16_t)(curT * 2.0f), 0, 100);
  uint8_t pctH = constrain((int16_t)curH, 0, 100);
  uint8_t pctL = constrain((int16_t)((uint16_t)curL * 100 / 255), 0, 100);

  if (homeShowValues) {
    char tbuf[9], hbuf[9], lbuf[9];

    char tStr[7];
    dtostrf(tempForDisplay(curT), 1, 1, tStr);
    snprintf(tbuf, sizeof(tbuf), "%s%c", tStr, tempUnitChar());

    char hStr[7];
    dtostrf(curH, 1, 1, hStr);
    snprintf(hbuf, sizeof(hbuf), "%s%%", hStr);

    uint8_t lPct = (uint16_t)curL * 100 / 255;
    char lStr[5];
    itoa(lPct, lStr, 10);
    snprintf(lbuf, sizeof(lbuf), "%s%%", lStr);

    drawValueRow(1, ICO_THERMO, tbuf, zT);
    drawValueRow(2, ICO_DROP,   hbuf, zH);
    drawValueRow(3, ICO_SUN,    lbuf, zL);

  } else {
    drawSensorRow(1, ICO_THERMO, pctT, zT);
    drawSensorRow(2, ICO_DROP,   pctH, zH);
    drawSensorRow(3, ICO_SUN,    pctL, zL);
  }
}

const char* menuLabel(uint8_t i) {
  if (cfg.language == 0) {
    switch (i) {
      case 0: return "Unidade temp";
      case 1: return "Idioma";
      case 2: return "Buzzer";
      case 3: return "Ver logs";
      case 4: return "Limpar logs";
      case 5: return "Sair";
    }
  } else if (cfg.language == 1) {
    switch (i) {
      case 0: return "Temp unit";
      case 1: return "Language";
      case 2: return "Buzzer";
      case 3: return "View logs";
      case 4: return "View logs";
      case 5: return "Exit";
    }
  } else {
    switch (i) {
      case 0: return "Unidad temp";
      case 1: return "Idioma";
      case 2: return "Buzzer";
      case 3: return "Ver logs";
      case 4: return "Borrar logs";
      case 5: return "Salir";
    }
  }
  return "";
}

void menuValueAt(uint8_t i, char* out, uint8_t len) {
  switch (i) {
    case 0: snprintf(out, len, "%c", cfg.tempUnit == 0 ? 'C' : 'F'); break;
    case 1: {
      const char* langs[] = {"PT", "EN", "ES"};
      snprintf(out, len, "%s", langs[cfg.language]);
      break;
    }
    case 2: snprintf(out, len, "%s", cfg.buzzerOn ? "ON" : "OFF"); break;
    case 3: snprintf(out, len, ">"); break;
    case 4: snprintf(out, len, ">"); break;
    case 5: snprintf(out, len, ">"); break;
  }
}

void renderMenu() {
  if (!lcdOk) return;
  lcd.setCursor(0, 0);
  if (cfg.language == 0)      lcd.print(F("=== CONFIGURACAO ==="));
  else if (cfg.language == 1) lcd.print(F("===   SETUP    ====="));
  else                        lcd.print(F("=== CONFIGURACION =="));

  for (uint8_t row = 0; row < 3; row++) {
    int8_t idx = (int8_t)menuIdx + row - 1;
    lcd.setCursor(0, row + 1);
    if (idx < 0 || idx >= MENU_ITEMS) {
      lcd.print(F("                    ")); continue;
    }
    lcd.print(idx == menuIdx ? '>' : ' ');
    char val[6];
    menuValueAt(idx, val, sizeof(val));
    char line[21];
    snprintf(line, sizeof(line), "%-13s%5s", menuLabel(idx), val);
    lcd.print(line);
  }
}

void menuAction(int8_t delta) {
  if (delta != 0) return;
  switch (menuIdx) {
    case 0:
      cfg.tempUnit = !cfg.tempUnit;
      break;
    case 1:
      cfg.language = (cfg.language + 1) % 3;
      break;
    case 2:
      cfg.buzzerOn = !cfg.buzzerOn;
      break;
    case 3:
      appMode = MODE_LOGS;
      break;
    case 4:
      logClearAll();
      logViewIdx = 0;
      if (lcdOk) {
        lcd.clear();
        lcd.setCursor(0, 1);
        if (cfg.language == 0)      lcd.print(F("   Logs apagados!   "));
        else if (cfg.language == 1) lcd.print(F("   Logs cleared!    "));
        else                        lcd.print(F(" Logs eliminados!   "));
        delay(1500);
        lcd.clear();
      }
      Serial.println(F("[EEPROM] Todos os logs apagados"));
      break;
    case 5:
      cfgSave();
      appMode = MODE_NORMAL;
      if (lcdOk) lcd.clear();
      break;
  }
  cfgSave();
}

void renderLogs() {
  if (!lcdOk) return;
  uint16_t count;
  EEPROM.get(EEP_ADDR_LOGN, count);
  lcd.setCursor(0, 0);
  if (count == 0) {
    lcd.print(F("=== LOGS (vazio) ==="));
    lcd.setCursor(0, 1); lcd.print(F("                    "));
    lcd.setCursor(0, 2); lcd.print(F("CANCEL = voltar     "));
    lcd.setCursor(0, 3); lcd.print(F("                    "));
    return;
  }
  if (logViewIdx >= count) logViewIdx = count - 1;

  uint16_t head;
  EEPROM.get(EEP_ADDR_HEAD, head);
  uint16_t firstPhys = (head + LOG_MAX - count) % LOG_MAX;
  uint16_t phys = (firstPhys + logViewIdx) % LOG_MAX;
  LogEntry e = logRead(phys);

  char buf[21];
  snprintf(buf, sizeof(buf), "LOG %3u/%3u",
           (unsigned)(logViewIdx + 1), (unsigned)count);
  lcd.setCursor(0, 0); lcd.print(buf); lcd.print(F("         "));

  RtcDateTime d(e.ts + (int32_t)cfg.utcOffset * 3600L);
  snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d:%02d",
           d.Day(), d.Month(), d.Hour(), d.Minute(), d.Second());
  lcd.setCursor(0, 1); lcd.print(buf); lcd.print(F("    "));

  float tDisp = tempForDisplay(e.tempC);
  char tb[6]; dtostrf(tDisp, 4, 1, tb);
  snprintf(buf, sizeof(buf), "T:%s%c U:%2u%% L:%3u",
           tb, tempUnitChar(), e.humid, e.ldr);
  lcd.setCursor(0, 2); lcd.print(buf);

  lcd.setCursor(0, 3); lcd.print(F("Status: "));
  lcd.print(zoneText(e.status));
  lcd.print(F("     "));
}

bool btnPressed(uint8_t pin) { return digitalRead(pin) == LOW; }

int8_t joyDir() {
  uint16_t v = analogRead(PIN_JOY_Y);
  if (v < JOY_CENTER - JOY_DEADZONE) return -1;
  if (v > JOY_CENTER + JOY_DEADZONE) return +1;
  return 0;
}

void handleButtons() {
  if (millis() - tLastBtn < 180) return;

  if (btnPressed(PIN_BTN_OK)) {
    tLastBtn = millis();
    Serial.println(F("[BTN] OK"));
    if (appMode == MODE_MENU) {
      menuAction(0);
    } else if (appMode == MODE_NORMAL) {
      homeShowValues = !homeShowValues;
      if (lcdOk) lcd.clear();
      Serial.println(homeShowValues
        ? F("[HOME] Modo: valores") : F("[HOME] Modo: barras"));
    }
  }
  else if (btnPressed(PIN_BTN_CAN)) {
    tLastBtn = millis();
    Serial.println(F("[BTN] CANCEL"));
    if (appMode == MODE_NORMAL) {
      appMode = MODE_MENU; menuIdx = 0;
      if (lcdOk) lcd.clear();
    } else if (appMode == MODE_MENU) {
      cfgSave(); appMode = MODE_NORMAL;
      if (lcdOk) lcd.clear();
    } else if (appMode == MODE_LOGS) {
      appMode = MODE_MENU;
      if (lcdOk) lcd.clear();
    }
  }
  else {
    int8_t dir = joyDir();
    if (dir == -1) {
      tLastBtn = millis();
      Serial.println(F("[JOY] UP"));
      if (appMode == MODE_MENU) {
        menuIdx = (menuIdx == 0) ? MENU_ITEMS - 1 : menuIdx - 1;
      } else if (appMode == MODE_LOGS) {
        if (logViewIdx > 0) logViewIdx--;
      }
    } else if (dir == +1) {
      tLastBtn = millis();
      Serial.println(F("[JOY] DOWN"));
      if (appMode == MODE_MENU) {
        menuIdx = (menuIdx + 1) % MENU_ITEMS;
      } else if (appMode == MODE_LOGS) {
        logViewIdx++;
      }
    }
  }
}

void maybeLog() {
  if (millis() - tLastLog < (unsigned long)cfg.logIntervalS * 1000UL) return;
  tLastLog = millis();
  LogEntry e;
  e.ts     = nowUnix();
  e.tempC  = (int8_t)curT;
  e.humid  = (uint8_t)curH;
  e.ldr    = curL;
  e.status = curStatus;
  logWrite(e);
  Serial.print(F("[EEPROM] log gravado @ts="));
  Serial.println(e.ts);
}

void setup() {
  pinMode(PIN_BTN_OK,  INPUT_PULLUP);
  pinMode(PIN_BTN_CAN, INPUT_PULLUP);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  setRGB(0, 0);
  setBlue(false);

  Serial.begin(9600);
  delay(500);

  Wire.begin();

  Serial.print(F("Inicializando LCD em 0x")); Serial.print(LCD_ADDR, HEX);
  Serial.print(F("... "));
  lcd.init();
  lcd.backlight();
  lcd.createChar(ICO_BOTTLE,  chBottle1);
  lcd.createChar(ICO_BOTTLE2, chBottle2);
  lcd.createChar(ICO_GRAPE,   chGrape);
  lcd.createChar(ICO_THERMO,  chThermo);
  lcd.createChar(ICO_DROP,    chDrop);
  lcd.createChar(ICO_SUN,     chSun);
  lcd.createChar(ICO_OK,      chOk);
  lcd.createChar(ICO_ALERTA,  chAlerta);
  lcdOk = true;
  Serial.println(F("OK"));

  Serial.print(F("Carregando config da EEPROM... "));
  cfgLoad();
  Serial.println(F("OK"));

  Serial.print(F("Joystick Y em repouso: "));
  Serial.println(analogRead(PIN_JOY_Y));

  Serial.print(F("Inicializando DHT11... "));
  dht.begin();
  Serial.println(F("OK"));

  Serial.print(F("Inicializando RTC DS1302... "));
  rtc.Begin();

  RtcDateTime compileLocal = RtcDateTime(__DATE__, __TIME__);
  RtcDateTime compileTime(compileLocal.TotalSeconds() - (int32_t)cfg.utcOffset * 3600L);

  rtc.SetIsWriteProtected(false);
  rtc.SetIsRunning(true);

  if (!rtc.IsDateTimeValid()) {
    rtc.SetDateTime(compileTime);
  }

  RtcDateTime now = rtc.GetDateTime();
  if (now < compileTime || now.Year() > 2030) {
    Serial.println(F("  hora antiga, ajustando pelo build"));
    rtc.SetDateTime(compileTime);
  }

  if (rtc.IsDateTimeValid()) {
    rtcOk = true;
    char hb[24];
    now = rtc.GetDateTime();
    snprintf(hb, sizeof(hb), "OK [%02d/%02d/%04d %02d:%02d:%02d]",
             now.Day(), now.Month(), now.Year(),
             now.Hour(), now.Minute(), now.Second());
    Serial.println(hb);
  } else {
    rtcOk = false;
    Serial.println(F("FALHA - usando millis() como timestamp"));
  }
  bootMillis = millis();

  Serial.println(F("\nIniciando animacao de boot...\n"));
  bootAnimation();

  lcd.createChar(ICO_CRITICO, chCritico);

  appMode = MODE_MENU;
  menuIdx = 0;
  if (lcdOk) lcd.clear();

  tLastSample = tLastWindow = tLastLog = millis();
  Serial.println(F("Sistema pronto. Loop principal iniciado.\n"));
}

void loop() {
  static AppMode lastAppMode = MODE_BOOT;

  handleButtons();

  if (appMode == MODE_MENU) {
    setRGB(255, 0);
    setBlue(true);
    noTone(PIN_BUZZER);
  } else {
    setBlue(false);
    if (lastAppMode == MODE_MENU) {
      alertApply();
    }
  }
  lastAppMode = appMode;

  unsigned long now = millis();

  if (now - tLastSample >= 1000) {
    tLastSample = now;
    sensorSample();
  }

  if (now - tLastWindow >= (unsigned long)WINDOW_S * 1000UL) {
    tLastWindow = now;
    windowFinalize();
    alertApply();
  }

  maybeLog();

  if (now - tLastFrame >= 300) {
    tLastFrame = now;
    switch (appMode) {
      case MODE_NORMAL: renderHome(); break;
      case MODE_MENU:   renderMenu(); break;
      case MODE_LOGS:   renderLogs(); break;
      default: break;
    }
  }
}

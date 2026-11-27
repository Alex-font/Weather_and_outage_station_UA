// Погодна станція + монітор відключень світла на ESP32-C3 Super Mini + TFT 2.8" 240x320 і трьох кнопках.
//
// Годинник, день тижня і дата; графік відключень світла на 24 години (джерела: див. sources.h);
// поточна погода й прогноз опадів (OpenWeatherMap). Wi-Fi, часовий пояс, населений пункт, черга
// і яскравість налаштовуються кнопками на екрані й запам'ятовуються.
//
// Докладніше: README.md. Піни й ключ OpenWeatherMap: config.h.

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <time.h>
#include <esp_system.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#include "config.h"
#include "types.h"
#include "colors.h"

#if TFT_DRIVER_ILI9341
#include <Adafruit_ILI9341.h>
#else
#include <Adafruit_ST7789.h>
#endif

// ---------- Екран ----------
#if TFT_DRIVER_ILI9341
Adafruit_ILI9341 tft(&SPI, PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_RST);
#else
Adafruit_ST7789 tft(&SPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
#endif

#include "ukr.h"       // українська мова: шрифти з кирилицею, UTF-8 <-> CP1251
#include "wizard.h"    // кнопки, списки, введення тексту, майстри Wi-Fi і часового поясу
#include "schedule.h"  // графіки відключень: завантаження й розбір
#include "places.h"    // вибір населеного пункту (область, район, пошук) і черги
#include "weather.h"   // погода

// ---------- Стан ----------
Preferences prefs;
String gTz;
PlaceCfg gPlace;

SemaphoreHandle_t gMutex = nullptr;

// Графік відключень (під gMutex)
Schedule gSched;
uint32_t gOkMs = 0;              // millis() останнього успішного запиту графіка
bool gEverOk = false;
bool gFetchFailed = false;
char gErr[48] = "";
uint32_t gChangedMs = 0;         // коли графік востаннє змінився
uint32_t gVer = 0;               // лічильник оновлень (для перемальовування графіка)

// Погода (під gMutex)
Weather gWeather = {};
Precip gPrecip = {};
bool gWFailed = false;           // остання спроба оновити погоду була невдалою

volatile bool gPauseNet = false;  // меню відкрите: фонове завантаження не чіпає Wi-Fi
volatile bool gNetBusy = false;
volatile bool gForce = false;     // оновити зараз
uint32_t gLastManual = 0;
bool ntpStarted = false;

// ---------- Яскравість ----------
constexpr int BL_LEVELS = 10;
constexpr uint8_t BL_DUTY[BL_LEVELS] = {8, 20, 36, 56, 80, 108, 140, 175, 213, 255};   // ШІМ підсвітки (якщо є пін BL)
constexpr uint16_t BL_SCALE[BL_LEVELS] = {40, 56, 74, 96, 120, 146, 174, 204, 232, 256};  // програмне затемнення (з 256)
int gBlLevel = BL_LEVELS;
bool gOverlay = false;  // показується панель яскравості: нижні рядки погоди не перемальовуємо

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#define BL_PWM_V3 1  // нове API ledcAttach(pin, ...)
#endif
constexpr uint8_t BL_PWM_CHANNEL = 0;  // потрібен лише для ядра 2.x

void backlightBegin() {
  if (PIN_TFT_BL < 0) return;
#ifdef BL_PWM_V3
  ledcAttach(PIN_TFT_BL, 5000, 8);
#else
  ledcSetup(BL_PWM_CHANNEL, 5000, 8);
  ledcAttachPin(PIN_TFT_BL, BL_PWM_CHANNEL);
#endif
}

// Справжня підсвітка (ШІМ), якщо є пін BL; інакше затемнення всіх кольорів
void applyBrightness() {
  if (gBlLevel < 1) gBlLevel = 1;
  if (gBlLevel > BL_LEVELS) gBlLevel = BL_LEVELS;
  if (PIN_TFT_BL >= 0) {
    gDim = 256;
    int duty = BL_DUTY[gBlLevel - 1];
    if (!TFT_BL_ACTIVE_HIGH) duty = 255 - duty;
#ifdef BL_PWM_V3
    ledcWrite(PIN_TFT_BL, duty);
#else
    ledcWrite(BL_PWM_CHANNEL, duty);
#endif
  } else {
    gDim = BL_SCALE[gBlLevel - 1];
  }
}

// ---------- Поля тексту ----------
// Усі координати розраховані на екран 240x320 (портрет, кнопки під екраном); базова лінія тексту = y.
//   0..62     годинник: час, потім день тижня (зліва) і дата (справа)
//   68        лінія
//   71..206   графік відключень: назва пункту, стан світла й черга, наступна подія, дві 12-годинні смуги
//   208       лінія
//   211..320  погода: температура, стан, вологість, вітер, опади
Field fTime    = {8,   36,  &FreeSansBold24pt7b};
Field fSec     = {150, 36,  &FreeSans12ptCyr};
Field fDay     = {8,   62,  &FreeSans12ptCyr};
Field fDate    = {0,   62,  &FreeSans12ptCyr};   // x рахується (по правому краю)
Field fSt1     = {8,   112, &FreeSans12ptCyr};
Field fSt2     = {8,   129, &FreeSans9ptCyr};
Field fQueue   = {0,   111, &FreeSans9ptCyr};    // справа в рядку стану світла (або "Оновлено"/"Помилка")
Field fTempLbl = {8,   236, &FreeSans12ptCyr};
Field fTemp    = {80,  236, &FreeSansBold18pt7b};
Field fNow     = {8,   255, &FreeSans12ptCyr};
Field fHum     = {8,   272, &FreeSans9ptCyr};
Field fWUpd    = {0,   272, &FreeSans9ptCyr};
Field fWind    = {8,   289, &FreeSans9ptCyr};
Field fAz      = {0,   289, &FreeSans9ptCyr};
Field fPrecip  = {8,   312, &FreeSans12ptCyr};
Field* const ALL_FIELDS[] = {&fTime, &fSec, &fDay, &fDate, &fQueue, &fSt1, &fSt2,
                             &fTempLbl, &fTemp, &fNow, &fHum, &fWUpd, &fWind, &fAz, &fPrecip};

constexpr int X_L = 8, X_R = 232;     // поля зліва й справа
constexpr int Y_LINE1 = 68, Y_LINE2 = 208;
constexpr int Y_PLACE = 71;           // верх полотна з назвою пункту

// Перемальовує поле лише якщо змінились текст або колір; стирає тільки свою область.
// '^' у тексті малюється як знак градуса (у шрифтах Adafruit його немає).
void setField(Field& f, const char* s, uint16_t color) {
  if (f.drawn && f.lastColor == color && strcmp(f.last, s) == 0) return;
  if (f.drawn || f.fixed) tft.fillRect(f.bx, f.by, f.bw, f.bh, COL_BG);  // стираємо попередній текст
  tft.setFont(f.font);
  tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  const char* deg = strchr(s, '^');
  if (!deg) {
    boundsU(s, f.x, f.y, &x1, &y1, &w, &h);
    tft.setCursor(f.x, f.y);
    printU(s);
    if (!f.fixed) {
      f.bx = x1 - 1;
      f.by = y1 - 1;
      f.bw = w + 2;
      f.bh = h + 2;
    }
  } else {
    char left[48];
    size_t n = (size_t)(deg - s);
    if (n >= sizeof left) n = sizeof left - 1;
    memcpy(left, s, n);
    left[n] = 0;
    boundsU(left, f.x, f.y, &x1, &y1, &w, &h);
    tft.setCursor(f.x, f.y);
    printU(left);
    int r = f.font->yAdvance / 12;  // радіус кільця залежить від розміру шрифту
    if (r < 2) r = 2;
    int cx = tft.getCursorX();
    int16_t dx, dy;  // верхня межа цифр береться від рядка "0": у заглушки "--" верх це короткий дефіс
    uint16_t dw, dh;
    tft.getTextBounds("0", f.x, f.y, &dx, &dy, &dw, &dh);
    int cy = dy + r + 1;
    tft.drawCircle(cx + r + 1, cy, r, color);
    tft.drawCircle(cx + r + 1, cy, r - 1, color);
    tft.setCursor(cx + 2 * r + 4, f.y);
    printU(deg + 1);
    if (!f.fixed) {  // висота завжди як у цифр, а не як у короткого "--": інакше від "°C" лишаються залишки
      f.bx = x1 - 2;
      f.by = dy - 3;
      f.bw = tft.getCursorX() - f.bx + 6;
      f.bh = (f.y - dy) + 8;
    }
  }
  strlcpy(f.last, s, sizeof(f.last));
  f.lastColor = color;
  f.drawn = true;
}

// Текст, вирівняний по правому краю (xRight)
void setFieldRight(Field& f, int xRight, const char* s, uint16_t color) {
  tft.setFont(f.font);
  int16_t x1, y1;
  uint16_t w, h;
  boundsU(s, 0, 0, &x1, &y1, &w, &h);
  f.x = xRight - (int)w - x1;
  setField(f, s, color);
}

// Розташування годинника й підпису температури
void layoutAll() {
  int16_t x1, y1;
  uint16_t w, h;
  tft.setFont(&FreeSansBold24pt7b);
  boundsU("00:00", 0, 0, &x1, &y1, &w, &h);
  fTime.x = (SCR_W - (int)w) / 2 - x1;   // HH:MM рівно посередині рядка, секунди праворуч від нього
  fSec.x = (SCR_W + (int)w) / 2 + 8;
  tft.setFont(&FreeSans12ptCyr);
  boundsU("Темп:", 0, 0, &x1, &y1, &w, &h);
  fTemp.x = X_L + (int)w + 10;
}

// ---------- Малювання ----------
const char* const DAYS_UA[7] = {"Неділя", "Понеділок", "Вівторок", "Середа", "Четвер", "П'ятниця", "Субота"};
const char* const DAYS_UA_SHORT[7] = {"Нд", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};

void drawStaticUi() {
  tft.fillScreen(COL_BG);
  tft.drawFastHLine(X_L, Y_LINE1, X_R - X_L, COL_LINE);
  tft.drawFastHLine(X_L, Y_LINE2, X_R - X_L, COL_LINE);
}

// Графік: одна смуга на 24 години (48 клітинок по пів-години)
// Два рядки по 12 годин (24 клітинки по пів-години в рядку): кожна година має мітку з підписом, пів години мітку без підпису.
constexpr int CH_X0 = 8, CH_W = 224, CH_BAR_H = 12, CH_Y = 138, CH_ROW_H = 35;
constexpr int CH_COLS = 24;     // клітинок у рядку
constexpr int CH_CELLS = 48;    // усього (24 години)

time_t gChartBase = 0;
int gChartEl = -1;
uint32_t gChartVer = 0xFFFFFFFF;
char gPlaceShown[48] = "";
Marq gPlaceMq;
uint16_t gLastDot = 0xFFFF;

void resetUi() {
  for (Field* f : ALL_FIELDS) {
    f->drawn = false;
    f->last[0] = 0;
  }
  gChartBase = 0;
  gChartEl = -1;
  gChartVer = 0xFFFFFFFF;
  gPlaceShown[0] = 0;
  gLastDot = 0xFFFF;
  drawStaticUi();
}

void drawClock() {
  struct tm t;
  char buf[24];
  if (getLocalTime(&t, 0)) {
    snprintf(buf, sizeof buf, "%02d:%02d", t.tm_hour, t.tm_min);
    setField(fTime, buf, COL_TIME);
    snprintf(buf, sizeof buf, "%02d", t.tm_sec);
    setField(fSec, buf, COL_DIM);
    setField(fDay, DAYS_UA[t.tm_wday], COL_DAY);
    snprintf(buf, sizeof buf, "%02d.%02d.%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    setFieldRight(fDate, X_R, buf, COL_TIME);
  } else {
    setField(fTime, "--:--", COL_DIM);
    setField(fSec, "", COL_DIM);
    setField(fDay, "", COL_DAY);
    setFieldRight(fDate, X_R, "", COL_TIME);
  }
}

// Назва населеного пункту: якщо не вміщується, біжить рядком
void renderPlace(int off, bool fits) {
  if (!gCvP) {
    tft.fillRect(4, Y_PLACE, CVP_W, CVP_H, COL_BG);
    char fit[64];
    tft.setFont(&FreeSans12ptCyr);
    fitText(gPlace.name, fit, sizeof fit, CVP_W - 8);
    tft.setTextColor(COL_DAY);
    tft.setCursor(8, Y_PLACE + 18);
    printU(fit);
    return;
  }
  gCvP->fillScreen(COL_BG);
  gCvP->setFont(&FreeSans12ptCyr);
  gCvP->setTextColor(COL_DAY);
  int x = 4 - off;
  if (fits) {  // коротка назва: по центру рядка
    tft.setFont(&FreeSans12ptCyr);
    x = (CVP_W - textWidthU(gPlace.name)) / 2;
    if (x < 4) x = 4;
  }
  gCvP->setCursor(x, 18);
  printUTo(*gCvP, gPlace.name);
  gCvP->fillRect(0, 0, 4, CVP_H, COL_BG);
  gCvP->fillRect(CVP_W - 4, 0, 4, CVP_H, COL_BG);
  cvBlit(gCvP, 4, Y_PLACE);
}

void drawPlaceLine() {
  if (strcmp(gPlaceShown, gPlace.name) != 0) {
    strlcpy(gPlaceShown, gPlace.name, sizeof gPlaceShown);
    tft.setFont(&FreeSans12ptCyr);
    marqStart(gPlaceMq, textWidthU(gPlace.name), CVP_W - 8);
    renderPlace(0, !gPlaceMq.active);
  } else if (marqTick(gPlaceMq)) {
    renderPlace(gPlaceMq.off, false);
  }
}

uint16_t slotColor(uint8_t st) {
  switch (st) {
    case SL_ON: return COL_S_ON;
    case SL_OFF: return COL_S_OFF;
    case SL_MAYBE: return COL_S_MAYBE;
  }
  return COL_S_NONE;
}

uint16_t dimColor(uint16_t c) { return (c & 0xF7DE) >> 1; }

// ---------- Графік на 24 години ----------
// Дві смуги по 12 годин. Кожна смуга починається й закінчується позначкою ПІВ-ГОДИНИ (21:30, 09:30 ...),
// тому позначки годин (із підписом) потрапляють у середину між краями смуги, а підписи стоять рівномірно.
// Під кожною годиною мітка з підписом, під пів-годиною коротка мітка без підпису.
// Початок графіка: остання позначка :30, що не пізніше за "зараз" (тож зліва видно до години минулого).
// Білий маркер: поточний момент; минуле притемнене.
static int chX(int col) { return CH_X0 + (col * CH_W) / CH_COLS; }
static int chY(int k) { return CH_Y + (k / CH_COLS) * CH_ROW_H; }

static time_t chartBase(time_t now) {
  return ((now - SLOT_SEC) / 3600) * 3600 + SLOT_SEC;  // SLOT_SEC = 1800 (пів години)
}

static int chartMarkerX(time_t base, time_t now) {
  long d = (long)(now - base);
  if (d < 0) d = 0;
  return CH_X0 + (int)((d * CH_W) / ((long)CH_COLS * SLOT_SEC));
}

// full = true: стерти область і перемалювати з підписами (зміна даних або години);
// full = false: лише зсув маркера: без стирання області, тому без мерехтіння
void drawChart(const Schedule& s, time_t now, bool full) {
  time_t base = chartBase(now);
  int xm = chartMarkerX(base, now);
  if (full) {
    tft.fillRect(0, CH_Y - 4, SCR_W, CH_ROW_H + CH_BAR_H + 22, COL_BG);  // до y=206: лінія на 208 лишається
  } else if (gChartEl >= 0) {  // прибираємо лише стару мітку маркера над і під смугою
    tft.fillRect(gChartEl, CH_Y - 3, 2, 3, COL_BG);
    tft.fillRect(gChartEl, CH_Y + CH_BAR_H, 2, 3, COL_BG);
  }
  for (int k = 0; k < CH_CELLS; k++) {
    time_t t = base + (time_t)k * SLOT_SEC;
    int col = k % CH_COLS, y = chY(k);
    int x0 = chX(col), x1 = chX(col + 1);
    uint16_t c = slotColor(slotAt(s, t));
    if (t + SLOT_SEC <= now) c = dimColor(c);  // клітинка повністю в минулому
    tft.fillRect(x0, y, x1 - x0, CH_BAR_H, c);
    if (t <= now && now < t + SLOT_SEC) {      // поточна клітинка: минула частина притемнена
      int el = xm - x0;
      if (el > 0) tft.fillRect(x0, y, el, CH_BAR_H, dimColor(slotColor(slotAt(s, t))));
    }
    struct tm lt;
    localtime_r(&t, &lt);
    if (lt.tm_min == 0) {
      if (col > 0) tft.drawFastVLine(x0, y, CH_BAR_H, COL_BG);  // межа годин
      tft.drawFastVLine(x0, y + CH_BAR_H, 3, COL_DIM);          // мітка години
      if (full) {
        char lb[4];
        snprintf(lb, sizeof lb, "%02d", lt.tm_hour);
        tft.setFont(nullptr);
        tft.setTextSize(1, 2);  // цифри підписів витягнуті по висоті (6x16 пікселів), ширина та сама
        tft.setTextColor(lt.tm_hour == 0 ? COL_FEELS : COL_DIM);
        tft.setCursor(x0 - 5, y + CH_BAR_H + 5);  // підпис по центру мітки (дві цифри: 12 пікселів)
        tft.print(lb);
        tft.setTextSize(1);
      }
    } else {
      tft.drawFastVLine(x0, y + CH_BAR_H, 2, COL_LINE);         // мітка пів-години, без підпису
    }
  }
  tft.fillRect(xm, CH_Y - 3, 2, CH_BAR_H + 6, COL_DAY);
  gChartEl = xm;
}

// ---------- Текстові підписи ----------
void fmtCountdown(long sec, char* out, size_t n) {
  long m = (sec + 59) / 60;
  if (m >= 1440) snprintf(out, n, "%ldд %ldг", m / 1440, (m % 1440) / 60);
  else if (m >= 60) snprintf(out, n, "%ldг %02ldхв", m / 60, m % 60);
  else snprintf(out, n, "%ldхв", m < 1 ? 1 : m);
}

// "о 14:00" / "завтра 04:00" / "Нд 04:00" (isEnd: 00:00 показуємо як 24:00 цієї ж доби)
void whenStr(time_t t, time_t now, bool atWord, bool isEnd, char* out, size_t n) {
  struct tm a, b;
  time_t td = t;
  bool mid = false;
  localtime_r(&t, &a);
  if (isEnd && a.tm_hour == 0 && a.tm_min == 0) { td = t - 60; mid = true; }
  localtime_r(&td, &a);
  localtime_r(&now, &b);
  struct tm m = b;  // різниця в календарних днях
  m.tm_hour = m.tm_min = m.tm_sec = 0;
  struct tm d2 = a;
  d2.tm_hour = d2.tm_min = d2.tm_sec = 0;
  long days = (long)((mktime(&d2) - mktime(&m) + 43200L) / 86400L);  // +12 год: стійко до переходу на літній/зимовий час
  struct tm tt;
  localtime_r(&t, &tt);
  int hh = mid ? 24 : tt.tm_hour;
  int mm = mid ? 0 : tt.tm_min;
  if (days == 0) snprintf(out, n, atWord ? "о %02d:%02d" : "%02d:%02d", hh, mm);
  else if (days == 1) snprintf(out, n, "завтра %02d:%02d", hh, mm);
  else snprintf(out, n, "%s %02d:%02d", DAYS_UA_SHORT[a.tm_wday], hh, mm);
}

bool isStale(uint32_t okMs) { return (uint32_t)(millis() - okMs) > STALE_AFTER_MS; }

// ---------- Блок відключень ----------
void drawOutage(bool wifiOk, bool* outageOk) {
  *outageOk = true;
  if (!gPlace.outage) {  // для області немає відкритих даних
    setFieldRight(fQueue, X_R, "", COL_FEELS);
    setField(fSt1, "Графіка відключень немає", COL_DIM);
    setField(fSt2, "Для цієї області немає даних", COL_DIM);
    return;
  }

  Schedule s;
  bool failed, everOk;
  uint32_t okMs, ver, changedMs;
  char err[48];
  time_t now = time(nullptr);
  xSemaphoreTake(gMutex, portMAX_DELAY);
  if (timeIsValid(now)) pruneSchedule(gSched, now);  // прибираємо застарілі доби
  s = gSched;
  failed = gFetchFailed;
  everOk = gEverOk;
  okMs = gOkMs;
  ver = gVer;
  changedMs = gChangedMs;
  strlcpy(err, gErr, sizeof err);
  xSemaphoreGive(gMutex);

  bool stale = everOk && (millis() - okMs) > STALE_AFTER_MS;
  *outageOk = everOk && !stale && !failed;

  char buf[64], l2[64] = "", w[32];

  if (!timeIsValid(now)) {
    setFieldRight(fQueue, X_R, "", COL_FEELS);
    setField(fSt1, "Синхронізація часу...", COL_DIM);
    setField(fSt2, "", COL_TEXT);
    return;
  }

  const char* l1 = "";
  uint16_t c1 = COL_DIM, c2 = COL_TEXT;

  if (!s.valid) {
    if (!everOk && !failed) {
      l1 = wifiOk ? "Завантаження..." : "Немає Wi-Fi";
    } else if (failed) {
      l1 = "Немає даних";
      strlcpy(l2, err, sizeof l2);
      c2 = COL_BAD;
    } else {
      l1 = "Немає даних";
      strlcpy(l2, "Графік не опубліковано", sizeof l2);
    }
  } else {
    uint8_t cur = slotAt(s, now);
    NextEvent ev = findNextChange(s, now, cur);
    if (cur == SL_NONE) {
      l1 = "Немає даних";
      strlcpy(l2, "Для цього часу графіка нема", sizeof l2);
    } else {
      if (cur == SL_ON) { l1 = "Світло є"; c1 = COL_OK; }
      else if (cur == SL_OFF) { l1 = "Світла немає"; c1 = COL_BAD; }
      else { l1 = "Можливе вимкнення"; c1 = COL_WARN; }

      if (ev.found) {
        uint8_t nx = slotAt(s, ev.t);
        whenStr(ev.t, now, true, false, w, sizeof w);
        if (cur == SL_ON) snprintf(l2, sizeof l2, nx == SL_MAYBE ? "Можливо %s" : "Вимкнення %s", w);
        else if (cur == SL_OFF) snprintf(l2, sizeof l2, "Увімкнення %s", w);
        else snprintf(l2, sizeof l2, "Далі зміна %s", w);
        // Скільки лишилось: у кінці цього ж рядка, якщо вміщується (щоб не займати окремий рядок)
        long m = ((long)(ev.t - now) + 59) / 60;
        char cdc[16] = "";
        if (m < 60) snprintf(cdc, sizeof cdc, "%ldхв", m < 1 ? 1 : m);
        else if (m < 600) snprintf(cdc, sizeof cdc, "%ldг %02ldхв", m / 60, m % 60);
        else if (m < 1440) snprintf(cdc, sizeof cdc, "%ldг", m / 60);
        if (cdc[0]) {
          char tmp[80];
          snprintf(tmp, sizeof tmp, "%s, %s", l2, cdc);
          tft.setFont(&FreeSans9ptCyr);
          if (textWidthU(tmp) <= X_R - X_L) strlcpy(l2, tmp, sizeof l2);
        }
      } else if (ev.unknown) {
        whenStr(ev.t, now, false, true, w, sizeof w);
        if (cur == SL_ON) snprintf(l2, sizeof l2, "Без відключень до %s", w);
        else if (cur == SL_OFF) snprintf(l2, sizeof l2, "Вимкнено до %s", w);
        else snprintf(l2, sizeof l2, "Можливо до %s", w);
      } else {
        strlcpy(l2, cur == SL_ON ? "Відключень не заплановано" : "Без змін понад 48 год", sizeof l2);
      }
    }
  }
  // Праворуч від стану світла: черга; тимчасово "Оновлено" або "Помилка". Якщо не вміщується, ховаємо
  const char* qtxt = buf;
  uint16_t qc = COL_FEELS;
  snprintf(buf, sizeof buf, "Черга %s", queueShort(gPlace.queue));
  {
    uint32_t ms = millis();
    if (failed && err[0]) { qtxt = "Помилка"; qc = COL_BAD; }
    else if (everOk && changedMs && (ms - changedMs) < CHANGED_SHOW_MS) { qtxt = "Оновлено"; qc = COL_OK; }
  }
  tft.setFont(&FreeSans12ptCyr);
  int w1 = textWidthU(l1);
  tft.setFont(&FreeSans9ptCyr);
  int wq = textWidthU(qtxt);
  if (w1 + 10 + wq > X_R - X_L) {  // не вміщується: пробуємо коротший стан
    if (!strcmp(l1, "Можливе вимкнення")) {
      l1 = "Можливо вимк.";
      tft.setFont(&FreeSans12ptCyr);
      w1 = textWidthU(l1);
    }
  }
  setField(fSt1, l1, c1);
  setField(fSt2, l2, c2);
  setFieldRight(fQueue, X_R, (w1 + 10 + wq <= X_R - X_L) ? qtxt : "", qc);

  // Графік: повністю при зміні даних чи години; лише маркер "зараз" (без стирання) при його зсуві
  if (s.valid || everOk) {
    time_t base = chartBase(now);
    bool full = (gChartBase != base || gChartVer != ver || gChartEl < 0);
    if (full || chartMarkerX(base, now) != gChartEl) {
      drawChart(s, now, full);
      gChartBase = base;
      gChartVer = ver;
    }
  }
}

// ---------- Блок погоди ----------
// Рядок стану: кілька станів через слеш, скільки вміститься (шрифт 12 pt, а якщо перший не вміщується, то 9 pt)
static void buildNow(const Weather& w, char* out, size_t n, uint16_t& color, bool& small) {
  out[0] = 0;
  color = COL_TEXT;
  small = false;
  const int maxW = X_R - X_L;
  const char* used[3];
  int nu = 0;
  for (int i = 0; i < w.nCond && i < 3; i++) {
    uint16_t c = COL_TEXT;
    const char* txt = condText(w.condId[i], w.night, c);
    bool dup = false;
    for (int j = 0; j < nu; j++) {
      if (!strcmp(used[j], txt)) dup = true;
    }
    if (dup) continue;
    char tmp[64];
    if (nu == 0) {
      snprintf(tmp, sizeof tmp, "%s", txt);
      tft.setFont(&FreeSans12ptCyr);
      small = textWidthU(tmp) > maxW;
    } else {
      snprintf(tmp, sizeof tmp, "%s / %s", out, txt);
    }
    tft.setFont(small ? &FreeSans9ptCyr : &FreeSans12ptCyr);
    if (nu > 0 && textWidthU(tmp) > maxW) break;
    strlcpy(out, tmp, n);
    if (nu == 0) color = c;
    used[nu++] = txt;
  }
  if (nu == 0) strlcpy(out, "--", n);
  if (nu == 1) {  // єдиний стан, що не вміщується навіть дрібним шрифтом: скорочуємо
    char fit[64];
    tft.setFont(small ? &FreeSans9ptCyr : &FreeSans12ptCyr);
    fitText(out, fit, sizeof fit, maxW);
    strlcpy(out, fit, n);
  }
}

void drawWeather(bool wifiOk, bool* weatherOk) {
  Weather w;
  Precip p;
  bool wFailed;
  xSemaphoreTake(gMutex, portMAX_DELAY);
  w = gWeather;
  p = gPrecip;
  wFailed = gWFailed;
  xSemaphoreGive(gMutex);

  bool wFresh = w.valid && !isStale(w.okMs);
  bool pFresh = p.valid && !isStale(p.okMs);
  *weatherOk = wFresh && pFresh && !wFailed;
  char buf[64];

  // --- Поточна погода ---
  setField(fTempLbl, "Темп:", COL_FEELS);  // жовтий підпис
  if (w.valid) {
    long t = lroundf(w.temp);
    uint16_t tc = (t > 0) ? COL_TEMP : (t < 0 ? COL_RAIN : COL_DAY);  // плюс червоний, мінус блакитний
    if (t > 0) snprintf(buf, sizeof buf, "+%ld^C", t);
    else if (t < 0) snprintf(buf, sizeof buf, "-%ld^C", -t);
    else snprintf(buf, sizeof buf, "0^C");
    setField(fTemp, buf, wFresh ? tc : COL_DIM);

    uint16_t nc = COL_TEXT;
    bool small = false;
    buildNow(w, buf, sizeof buf, nc, small);
    fNow.font = small ? &FreeSans9ptCyr : &FreeSans12ptCyr;
    fNow.y = small ? 253 : 255;
    setField(fNow, buf, wFresh ? nc : COL_DIM);

    // Час останнього успішного оновлення (рахується від millis, тому не залежить від моменту NTP)
    time_t tNow = time(nullptr);
    if (tNow > 1700000000L) {
      time_t u = tNow - (time_t)((uint32_t)(millis() - w.okMs) / 1000UL);
      struct tm tu;
      localtime_r(&u, &tu);
      snprintf(buf, sizeof buf, "upd %02d:%02d", tu.tm_hour, tu.tm_min);
    } else {
      snprintf(buf, sizeof buf, "upd --:--");
    }
    if (!gOverlay) {
      setFieldRight(fWUpd, X_R, buf, COL_DIM);
      snprintf(buf, sizeof buf, "Вологість %d%%", w.humidity);
      setField(fHum, buf, wFresh ? COL_HUM : COL_DIM);

      // Вітер: швидкість зеленим, азимут (звідки дме) оранжевим і з проміжком, щоб не зливалось
      tft.setFont(fWind.font);
      snprintf(buf, sizeof buf, "Вітер %.1f км/год", w.windMs * 3.6f);
      setField(fWind, buf, wFresh ? COL_WIND : COL_DIM);
      if (w.windDeg >= 0) {
        fAz.x = fWind.x + textWidthU(buf) + 18;
        snprintf(buf, sizeof buf, "А-%03d", w.windDeg);
        setField(fAz, buf, wFresh ? COL_AZ : COL_DIM);
      } else {
        setField(fAz, "штиль", wFresh ? COL_AZ : COL_DIM);  // напрямку немає
      }
    }
  } else {
    setField(fTemp, "--^C", COL_DIM);
    fNow.font = &FreeSans12ptCyr;
    fNow.y = 255;
    setField(fNow, "--", COL_DIM);
    if (!gOverlay) {
      setFieldRight(fWUpd, X_R, "upd --:--", COL_DIM);
      setField(fHum, "Вологість --", COL_DIM);
      setField(fWind, "Вітер --", COL_DIM);
      setField(fAz, "", COL_DIM);
    }
  }

  if (gOverlay) return;  // нижній рядок зайнятий панеллю яскравості

  // --- Прогноз опадів (рахується від поточного часу, тому оновлюється вчасно) ---
  if (!w.valid) {
    const char* msg;
    uint16_t mc = COL_DIM;
    if (USE_OPENWEATHERMAP && !strcmp(OWM_API_KEY, "YOUR_API_KEY")) { msg = "Впишіть ключ API у config.h"; mc = COL_BAD; }
    else if (!wifiOk) msg = "Підключення Wi-Fi...";
    else if (USE_OPENWEATHERMAP && gWeatherHttp == 401) { msg = "Хибний ключ API погоди"; mc = COL_BAD; }
    else msg = "Завантаження погоди...";
    setField(fPrecip, msg, mc);
  } else if (!p.valid) {
    setField(fPrecip, "Завантаження прогнозу...", COL_DIM);
  } else {
    const char* typeName[] = {"", "Дощ", "Сніг", "Дощ/сніг"};
    const uint16_t typeColor[] = {COL_OK, COL_RAIN, COL_SNOW, COL_SLEET};
    time_t nowT = time(nullptr);
    FcSummary f = summarizeForecast(p, nowT);

    if (!f.any) {
      setField(fPrecip, "Без опадів 24 год", pFresh ? COL_DAY : COL_DIM);  // не зелений: інакше зливається з вітром
    } else {
      struct tm tw, tn;
      localtime_r(&f.when, &tw);
      localtime_r(&nowT, &tn);
      char base[64];
      if (f.when <= nowT) {  // опади в поточній годині
        snprintf(base, sizeof base, "%s зараз", typeName[f.type]);
      } else if (tw.tm_yday == tn.tm_yday) {
        snprintf(base, sizeof base, "%s ~%02d:%02d", typeName[f.type], tw.tm_hour, tw.tm_min);
      } else {
        snprintf(base, sizeof base, "%s ~%s %02d:%02d", typeName[f.type], DAYS_UA_SHORT[tw.tm_wday],
                 tw.tm_hour, tw.tm_min);
      }
      snprintf(buf, sizeof buf, "%s %d%%", base, f.popAt);
      int16_t bx, by;
      uint16_t bw, bh;
      tft.setFont(fPrecip.font);
      boundsU(buf, fPrecip.x, fPrecip.y, &bx, &by, &bw, &bh);
      if (bx + (int)bw > X_R + 4) strlcpy(buf, base, sizeof buf);  // довгий рядок: без відсотків
      setField(fPrecip, buf, pFresh ? typeColor[f.type] : COL_DIM);
    }
  }
}

// ---------- Індикатор стану й загальне перемальовування ----------
// Кружок у куті: зелений усе гаразд, жовтий дані застаріли або помилка, червоний немає Wi-Fi
void drawAll() {
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  bool outageOk = true, weatherOk = true;
  drawPlaceLine();
  drawClock();
  drawOutage(wifiOk, &outageOk);
  drawWeather(wifiOk, &weatherOk);
  uint16_t dot = !wifiOk ? COL_BAD : ((outageOk && weatherOk) ? COL_OK : COL_WARN);
  if (dot != gLastDot) {
    tft.fillCircle(230, 10, 5, dot);
    gLastDot = dot;
  }
}

// ---------- Wi-Fi: перепідключення у фоні ----------
enum KState : uint8_t { K_IDLE, K_SCANNING, K_CONNECTING };
KState kState = K_IDLE;
uint32_t kLostMs = 0, kStateMs = 0;
bool kBoot = true;
int kCand[MAX_SAVED];
int kCandN = 0, kCandPos = 0;

void wifiKeeper() {
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    kState = K_IDLE;
    kLostMs = 0;
    kBoot = false;
    return;
  }
  if (gPauseNet) return;  // зараз працює майстер Wi-Fi
  if (kState == K_IDLE) {
    if (!kBoot) {
      if (kLostMs == 0) { kLostMs = now; return; }
      if (now - kLostMs < 30000) return;  // даємо драйверу самому відновити зв'язок
    }
    WiFi.scanNetworks(true);
    kState = K_SCANNING;
    kStateMs = now;
  } else if (kState == K_SCANNING) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING && now - kStateMs < 15000) return;
    kCandN = 0;
    if (n > 0) {
      // за силою сигналу: спершу найсильніша відома мережа
      int rssi[MAX_SAVED];
      for (int i = 0; i < n && kCandN < MAX_SAVED; i++) {
        String ss = WiFi.SSID(i);
        int slot = wifiStoreFind(ss.c_str());
        if (slot < 0) continue;
        int pos = kCandN++;
        kCand[pos] = slot;
        rssi[pos] = WiFi.RSSI(i);
        while (pos > 0 && rssi[pos] > rssi[pos - 1]) {
          int ts = kCand[pos]; kCand[pos] = kCand[pos - 1]; kCand[pos - 1] = ts;
          int tr = rssi[pos]; rssi[pos] = rssi[pos - 1]; rssi[pos - 1] = tr;
          pos--;
        }
      }
    }
    WiFi.scanDelete();
    kCandPos = 0;
    if (kCandN == 0) { kState = K_IDLE; kLostMs = now; kBoot = false; return; }
    SavedNet sn;
    if (wifiStoreLoad(kCand[0], sn)) WiFi.begin(sn.ssid, sn.pass[0] ? sn.pass : nullptr);
    kState = K_CONNECTING;
    kStateMs = now;
  } else if (kState == K_CONNECTING) {
    if (now - kStateMs < 15000) return;
    kCandPos++;
    if (kCandPos < kCandN) {
      SavedNet sn;
      if (wifiStoreLoad(kCand[kCandPos], sn)) WiFi.begin(sn.ssid, sn.pass[0] ? sn.pass : nullptr);
      kStateMs = now;
    } else {
      WiFi.disconnect();
      kState = K_IDLE;
      kLostMs = now;
      kBoot = false;
    }
  }
}

// ---------- Фонове завантаження: погода й графік ----------
void resetData() {
  xSemaphoreTake(gMutex, portMAX_DELAY);
  memset(&gSched, 0, sizeof gSched);
  gEverOk = false;
  gFetchFailed = false;
  gErr[0] = 0;
  gChangedMs = 0;
  gVer++;
  gWeather = {};
  gPrecip = {};
  gWFailed = false;
  xSemaphoreGive(gMutex);
  gWeatherHttp = 0;
}

void netTask(void*) {
  uint32_t nextW = 0, nextS = 0;
  bool first = true;
  for (;;) {
    if (gPauseNet || WiFi.status() != WL_CONNECTED || !gPlace.valid) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    uint32_t now = millis();
    bool force = gForce;
    bool dueW = first || force || (int32_t)(now - nextW) >= 0;
    bool dueS = gPlace.outage && (first || force || (int32_t)(now - nextS) >= 0);
    if (!dueW && !dueS) {
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    first = false;
    gForce = false;
    gNetBusy = true;
    PlaceCfg pc = gPlace;

    if (dueW) {
      Weather w = {};
      Precip p = {};
      bool a, b;
      if (USE_OPENWEATHERMAP) {  // поточна погода з OpenWeatherMap, опади по годинах з Open-Meteo
        a = fetchCurrent(pc.lat, pc.lon, w);
        b = fetchOpenMeteo(pc.lat, pc.lon, nullptr, &p);
      } else {  // усе одним запитом до Open-Meteo
        a = b = fetchOpenMeteo(pc.lat, pc.lon, &w, &p);
      }
      xSemaphoreTake(gMutex, portMAX_DELAY);
      if (a) gWeather = w;
      if (b) gPrecip = p;
      gWFailed = !(a && b);
      xSemaphoreGive(gMutex);
      nextW = millis() + ((a && b) ? WEATHER_INTERVAL_MS : RETRY_INTERVAL_MS);
    }

    if (dueS) {
      Schedule s;
      char err[48] = "";
      FetchResult r = fetchSchedule(pc, s, err, sizeof err);
      xSemaphoreTake(gMutex, portMAX_DELAY);
      if (r == FETCH_FAIL) {
        gFetchFailed = true;
        strlcpy(gErr, err[0] ? err : "Помилка завантаження", sizeof gErr);
      } else {
        gFetchFailed = false;
        gErr[0] = 0;
        gOkMs = millis();
        bool changed = (r == FETCH_OK) && gEverOk && (s.hash != gSched.hash);
        if (r == FETCH_OK) {
          gSched = s;
        } else {
          memset(&gSched, 0, sizeof gSched);  // джерело працює, але для черги графіка немає
        }
        if (changed) gChangedMs = millis();
        bool firstOk = !gEverOk;
        gEverOk = true;
        if (changed || firstOk || r != FETCH_OK) gVer++;  // графік перемальовується лише якщо дані справді змінились
      }
      xSemaphoreGive(gMutex);
      Serial.printf("fetch schedule: %d %s\n", (int)r, err);
      nextS = millis() + (r == FETCH_FAIL ? RETRY_INTERVAL_MS : SCHEDULE_INTERVAL_MS);
    }
    gNetBusy = false;
    Serial.printf("Пам'ять: вільно %u, мінімум %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  }
}

// ---------- Кнопки ----------
void drainButtons() {  // чекаємо, поки всі кнопки відпустять
  uint32_t t = millis();
  while (millis() - t < 300) {
    pollButton(btnL);
    pollButton(btnC);
    pollButton(btnR);
    if (btnL.down || btnC.down || btnR.down) t = millis();
    delay(5);
  }
}

// ---------- Яскравість ----------
// Показується основний екран, унизу панель: ліва кнопка темніше, права яскравіше, центральна підтверджує.
void drawBrightnessPanel() {
  tft.fillRect(0, 258, SCR_W, 62, COL_BG);  // закриває вологість, вітер і опади
  tft.drawFastHLine(X_L, 262, X_R - X_L, COL_LINE);
  char b[32];
  snprintf(b, sizeof b, "Яскравість %d/%d", gBlLevel, BL_LEVELS);
  tft.setFont(&FreeSans12ptCyr);
  tft.setTextColor(COL_DAY);
  tft.setCursor(X_L, 284);
  printU(b);
  for (int i = 0; i < BL_LEVELS; i++) {
    tft.fillRect(X_L + i * 22, 291, 20, 12, i < gBlLevel ? COL_FEELS : COL_LINE);
  }
  tft.setFont(&FreeSans9ptCyr);
  tft.setTextColor(COL_DIM);
  tft.setCursor(X_L, 316);
  printU("Л: -      Ц: ОК      П: +");
}

void brightnessMode() {
  drainButtons();
  gOverlay = true;
  int startLevel = gBlLevel;
  uint32_t last = millis(), lastDraw = 0;
  drawAll();
  drawBrightnessPanel();
  for (;;) {
    Nav nv = pollNav();
    if (nv.step) {
      int nl = gBlLevel + (nv.step > 0 ? 1 : -1);
      if (nl < 1) nl = 1;
      if (nl > BL_LEVELS) nl = BL_LEVELS;
      last = millis();
      if (nl != gBlLevel) {
        gBlLevel = nl;
        applyBrightness();
        if (PIN_TFT_BL < 0) {  // програмне затемнення: усі кольори змінились, перемальовуємо екран
          resetUi();
          drawAll();
        }
        drawBrightnessPanel();
      }
    }
    if (nv.ok) break;
    if (millis() - last > BL_ADJUST_TIMEOUT_MS) break;
    uint32_t ms = millis();
    if (ms - lastDraw >= 200) {  // основний екран живе: годинник іде
      lastDraw = ms;
      drawAll();
    }
    wifiKeeper();
    delay(5);
  }
  if (gBlLevel != startLevel) prefs.putUChar("blv", (uint8_t)gBlLevel);
  gOverlay = false;
  drainButtons();
  resetUi();
}

// ---------- Налаштування ----------
void applyTz() {
  setenv("TZ", gTz.c_str(), 1);
  tzset();
}

void menuLabel(int i, char* out, size_t n) {
  static const char* const L[] = {"Населений пункт і черга", "Мережа Wi-Fi", "Часовий пояс", "Яскравість екрана",
                                  "Оновити дані зараз", "Назад"};
  strlcpy(out, L[i], n);
}

// Повертає номер вибраного пункту (3 = яскравість: її регулювання йде вже на основному екрані)
int settingsMenu() {
  drainButtons();
  gPauseNet = true;
  uint32_t t0 = millis();
  while (gNetBusy && millis() - t0 < 20000) delay(50);

  int s = listMenu("Налаштування", 6, menuLabel, 0);
  if (s == 0) {
    PlaceCfg pc;
    if (runPlaceWizard(prefs, pc, true)) {
      gPlace = pc;
      resetData();
      gForce = true;
    }
  } else if (s == 1) {
    if (!wizardWifi(true)) {  // користувач передумав: Wi-Fi вже від'єднано пошуком, підключаємось знову
      kState = K_IDLE;
      kBoot = true;
    }
  } else if (s == 2) {
    wizardTz(prefs);
    gTz = prefs.getString("tz", DEFAULT_TZ_INFO);
    applyTz();
  } else if (s == 4) {
    gForce = true;
  }
  drainButtons();
  resetUi();
  gPauseNet = false;
  return s;
}

// Очікування підключення до Wi-Fi (для першого налаштування, коли мережі вже відомі)
bool waitWifi(uint32_t timeoutMs) {
  showMessage("Підключення до Wi-Fi...", nullptr, COL_TEXT);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    wifiKeeper();
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

// ---------- Запуск ----------
void setup() {
  Serial.begin(115200);
  Serial.printf("Запуск, причина скидання: %d (1 живлення, 3 програмний, 4 паніка, 5-7 вотчдог, 15 просідання живлення)\n",
                (int)esp_reset_reason());
  backlightBegin();

  SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, -1);  // MISO не потрібен, CS керує бібліотека
#if TFT_DRIVER_ILI9341
  tft.begin(TFT_SPI_HZ);
#else
  tft.init(240, 320, TFT_SPI_MODE);
  tft.setSPISpeed(TFT_SPI_HZ);
#endif
  if (TFT_INVERT >= 0) tft.invertDisplay(TFT_INVERT != 0);
  tft.setRotation(TFT_ROTATION);
  tft.setTextWrap(false);
  tft.fillScreen(COL_BG);
  layoutAll();
  cvBegin();
  buttonsBegin();
  gMutex = xSemaphoreCreateMutex();

  prefs.begin("station", false);
  gBlLevel = prefs.getUChar("blv", BL_LEVELS);
  if (gBlLevel < 1 || gBlLevel > BL_LEVELS) gBlLevel = BL_LEVELS;
  applyBrightness();

  bool forceSetup = bothButtonsHeld();  // ліва і права кнопки при вмиканні: повне налаштування
  wifiStoreBegin();
  gTz = prefs.isKey("tz") ? prefs.getString("tz", "") : String("");
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  if (WIFI_LOW_TX_POWER) WiFi.setTxPower(WIFI_POWER_8_5dBm);

  if (forceSetup || gTz.length() == 0 || wifiStoreCount() == 0) {
    runSetupWizard(prefs);
    gTz = prefs.getString("tz", DEFAULT_TZ_INFO);
  }
  applyTz();

  bool havePlace = loadPlace(prefs, gPlace);
  if (forceSetup || !havePlace) {
    if (WiFi.status() != WL_CONNECTED) waitWifi(25000);  // потрібен для запиту списку черг
    runPlaceWizard(prefs, gPlace, false);
  }

  resetUi();
  memset(&gSched, 0, sizeof gSched);
  xTaskCreate(netTask, "net", 20480, nullptr, 1, nullptr);
}

void loop() {
  if (!ntpStarted && WiFi.status() == WL_CONNECTED) {
    configTzTime(gTz.c_str(), "pool.ntp.org", "time.google.com", "ua.pool.ntp.org");
    ntpStarted = true;
  }
  wifiKeeper();

  pollButton(btnL);
  BtnEvent er = pollButton(btnR);
  BtnEvent ec = pollButton(btnC);
  if (ec == EV_PRESS) {  // центральна: меню (одразу, без утримання)
    int act = settingsMenu();
    if (act == 3) brightnessMode();
  } else if (er == EV_PRESS && millis() - gLastManual > MANUAL_REFRESH_GAP_MS) {
    gLastManual = millis();  // права кнопка: оновити погоду й графік зараз
    gForce = true;
  }

  static uint32_t lastDraw = 0;
  uint32_t ms = millis();
  drawPlaceLine();
  if (ms - lastDraw >= 200) {
    lastDraw = ms;
    drawAll();
  }
  delay(10);
}

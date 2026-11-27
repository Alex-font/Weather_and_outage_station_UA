#pragma once
// Інтерфейс на трьох кнопках: кнопки, меню-списки, введення тексту (пароль, пошук) зі смугою літер,
// майстри Wi-Fi і часового поясу.
// Підключається з основного скетча ПІСЛЯ оголошення tft, colors.h і ukr.h.
//
// Ліва кнопка: вгору / попередній символ.   Права кнопка: вниз / наступний символ.
// Центральна кнопка: підтвердження (спрацьовує одразу, без утримання).
// Ліву й праву можна утримувати: вони повторюються самі (швидке гортання довгих списків).

#include <Preferences.h>
#include "timezones.h"
#include "wifistore.h"

constexpr int SCR_W = 240;  // екран стоїть вертикально (портрет), кнопки під ним
constexpr int SCR_H = 320;

// ---------- Кнопки ----------
enum BtnEvent : uint8_t { EV_NONE, EV_PRESS, EV_REPEAT };

struct Button {
  uint8_t pin;
  bool raw;              // останній "сирий" стан піна (true = натиснуто)
  bool down;             // стабільний стан після усунення брязкоту
  uint32_t changedMs;
  uint32_t pressMs;
  uint32_t nextRepeatMs;
  bool longFired;        // довге утримання вже спрацювало (введення тексту)
};

Button btnL = {(uint8_t)PIN_BTN_L, false, false, 0, 0, 0};
Button btnC = {(uint8_t)PIN_BTN_C, false, false, 0, 0, 0};
Button btnR = {(uint8_t)PIN_BTN_R, false, false, 0, 0, 0};

void buttonsBegin() {
  pinMode(PIN_BTN_L, INPUT_PULLUP);
  pinMode(PIN_BTN_C, INPUT_PULLUP);
  pinMode(PIN_BTN_R, INPUT_PULLUP);
}

// Натискання повертається одразу (EV_PRESS). Якщо repeat, під час утримання йдуть EV_REPEAT.
static BtnEvent pollButton(Button& b, bool repeat = false) {
  uint32_t now = millis();
  bool raw = (digitalRead(b.pin) == LOW);
  if (raw != b.raw) {
    b.raw = raw;
    b.changedMs = now;
  }
  if (raw != b.down && (now - b.changedMs) >= BTN_DEBOUNCE_MS) {
    b.down = raw;
    if (raw) {
      b.pressMs = now;
      b.nextRepeatMs = now + BTN_REPEAT_DELAY_MS;
      return EV_PRESS;
    }
    return EV_NONE;
  }
  if (repeat && b.down && (int32_t)(now - b.nextRepeatMs) >= 0) {
    b.nextRepeatMs = now + ((now - b.pressMs) > 2000 ? BTN_REPEAT_FAST_MS : BTN_REPEAT_MS);
    return EV_REPEAT;
  }
  return EV_NONE;
}

// Навігація: step = -1 (ліва) або +1 (права), ok = центральна
struct Nav {
  int8_t step;
  bool ok;
};

static Nav pollNav() {
  Nav n = {0, false};
  BtnEvent el = pollButton(btnL, true);
  BtnEvent er = pollButton(btnR, true);
  BtnEvent ec = pollButton(btnC, false);
  if (el != EV_NONE) n.step = -1;
  else if (er != EV_NONE) n.step = 1;
  if (ec == EV_PRESS) n.ok = true;
  return n;
}

static bool anyButtonDown() { return btnL.down || btnC.down || btnR.down; }

// Ліва і права утримуються під час вмикання (перевіряємо ~300 мс): повне налаштування спочатку
bool bothButtonsHeld() {
  for (int i = 0; i < 6; i++) {
    if (digitalRead(PIN_BTN_L) != LOW || digitalRead(PIN_BTN_R) != LOW) return false;
    delay(50);
  }
  return true;
}

// ---------- Допоміжне малювання ----------
static void centerText(const char* sIn, int y) {  // шрифт встановлюється перед викликом
  char fitBuf[96];
  fitText(sIn, fitBuf, sizeof fitBuf, SCR_W - 10);  // довгий рядок обрізається, а не виходить за екран
  const char* s = fitBuf;
  int16_t x1, y1;
  uint16_t w, h;
  boundsU(s, 0, y, &x1, &y1, &w, &h);
  tft.setCursor((SCR_W - (int)w) / 2, y);
  printU(s);
}

static void rightText(const char* s, int xRight, int y) {  // шрифт встановлюється перед викликом
  int16_t x1, y1;
  uint16_t w, h;
  boundsU(s, 0, y, &x1, &y1, &w, &h);
  tft.setCursor(xRight - (int)w, y);
  printU(s);
}

static void showMessage(const char* l1, const char* l2, uint16_t color) {
  tft.fillScreen(COL_BG);
  tft.setFont(&FreeSans12ptCyr);
  tft.setTextColor(color);
  centerText(l1, 150);
  if (l2) {
    tft.setFont(&FreeSans9ptCyr);
    tft.setTextColor(COL_TEXT);
    centerText(l2, 180);
  }
}

// Повідомлення з заголовком і двома рядками дрібним шрифтом
static void showMessage3(const char* title, const char* l1, const char* l2, uint16_t color) {
  tft.fillScreen(COL_BG);
  tft.setFont(&FreeSans12ptCyr);
  tft.setTextColor(color);
  centerText(title, 110);
  tft.setFont(&FreeSans9ptCyr);
  tft.setTextColor(COL_TEXT);
  if (l1) centerText(l1, 150);
  if (l2) centerText(l2, 174);
}

// Чекає, поки всі кнопки відпустять (і ще трохи), нічого не повертаючи
static void waitButtonsReleased() {
  if (digitalRead(PIN_BTN_L) == LOW || digitalRead(PIN_BTN_C) == LOW || digitalRead(PIN_BTN_R) == LOW) {
    showMessage("Відпустіть кнопки", nullptr, COL_WARN);
  }
  uint32_t t = millis();
  while (millis() - t < 250) {
    pollButton(btnL);
    pollButton(btnC);
    pollButton(btnR);
    if (digitalRead(PIN_BTN_L) == LOW || digitalRead(PIN_BTN_C) == LOW || digitalRead(PIN_BTN_R) == LOW) t = millis();
    delay(5);
  }
}

// Після повідомлення з паузою: чекаємо натискання центральної (або таймаут)
static void waitOk(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    Nav n = pollNav();
    if (n.ok) break;
    delay(10);
  }
}

// ---------- Полотна для плавного рядка, що біжить ----------
// Текст малюється в оперативній пам'яті й виводиться на екран одним блоком, тому не мерехтить.
constexpr int CV_W = 232;       // полотно рядка списку
constexpr int CV_H = 26;
constexpr int CVP_W = 232;      // полотно назви населеного пункту на головному екрані
constexpr int CVP_H = 24;
GFXcanvas16* gCv = nullptr;     // рядок списку
GFXcanvas16* gCvP = nullptr;    // назва пункту на головному екрані

static void cvBegin() {
  gCv = new GFXcanvas16(CV_W, CV_H);
  if (gCv && gCv->getBuffer()) {
    gCv->setTextWrap(false);
  } else {
    delete gCv;
    gCv = nullptr;  // без полотна довгі назви просто обрізаються
  }
  gCvP = new GFXcanvas16(CVP_W, CVP_H);
  if (gCvP && gCvP->getBuffer()) {
    gCvP->setTextWrap(false);
  } else {
    delete gCvP;
    gCvP = nullptr;
  }
}

static void cvBlit(GFXcanvas16* c, int16_t x, int16_t y) {
  if (!c) return;
  tft.startWrite();
  tft.setAddrWindow(x, y, c->width(), c->height());
  tft.writePixels(c->getBuffer(), (uint32_t)c->width() * c->height());
  tft.endWrite();
}

struct Marq {
  bool active;    // текст ширший за доступне місце
  int textW, availW, off, maxOff;
  uint8_t ph;     // 0 пауза на початку, 1 прокрутка, 2 пауза наприкінці
  uint32_t t;
};

static void marqStart(Marq& m, int textW, int availW) {
  m.textW = textW;
  m.availW = availW;
  m.off = 0;
  m.ph = 0;
  m.t = millis();
  m.active = (textW > availW);
  m.maxOff = textW - availW + 6;
}

// Повертає true, коли зсув змінився і рядок треба перемалювати
static bool marqTick(Marq& m) {
  if (!m.active) return false;
  uint32_t now = millis();
  if (m.ph == 0) {
    if (now - m.t >= MARQUEE_HOLD_MS) { m.ph = 1; m.t = now; }
    return false;
  }
  if (m.ph == 1) {
    if (now - m.t < MARQUEE_STEP_MS) return false;
    m.t = now;
    m.off += 2;
    if (m.off >= m.maxOff) { m.off = m.maxOff; m.ph = 2; }
    return true;
  }
  if (now - m.t >= MARQUEE_HOLD_MS) {  // кінець паузи: починаємо спочатку
    m.off = 0;
    m.ph = 0;
    m.t = now;
    return true;
  }
  return false;
}

// ---------- Універсальний список ----------
// Повністю екран малюється один раз, далі перемальовуються лише змінені рядки.
// Вибраний рядок виводиться через полотно: якщо текст не вміщується, він біжить.
constexpr int LIST_ROWS = 10;
constexpr int LIST_ROW_H = 26;
constexpr int LIST_TOP = 34;
constexpr int LIST_TEXT_W = 216;  // ширина області тексту в рядку
typedef void (*LabelFn)(int idx, char* out, size_t n);

static int listFirst(int sel, int count) {
  int first = sel - LIST_ROWS / 2;
  if (first > count - LIST_ROWS) first = count - LIST_ROWS;
  if (first < 0) first = 0;
  return first;
}

static void listLabel(int idx, bool selected, LabelFn fn, LabelFn detail, char* out, size_t n) {
  if (selected && detail) detail(idx, out, n);
  else fn(idx, out, n);
}

static void drawSelRow(const char* label, int y, int off) {
  tft.setFont(&FreeSans9ptCyr);
  if (!gCv) {  // запасний варіант без полотна
    char buf[128];
    tft.fillRect(0, y, SCR_W, LIST_ROW_H, COL_BG);
    tft.fillRoundRect(6, y, SCR_W - 12, LIST_ROW_H - 2, 4, COL_SEL);
    fitText(label, buf, sizeof buf, LIST_TEXT_W - 6);
    tft.setTextColor(COL_DAY);
    tft.setCursor(12, y + 18);
    printU(buf);
    return;
  }
  gCv->fillScreen(COL_BG);
  gCv->fillRect(2, 0, CV_W - 4, LIST_ROW_H - 2, COL_SEL);
  gCv->setFont(&FreeSans9ptCyr);
  gCv->setTextColor(COL_DAY);
  gCv->setCursor(8 - off, 18);
  printUTo(*gCv, label);
  gCv->fillRect(2, 0, 6, LIST_ROW_H - 2, COL_SEL);              // ліве поле: ховаємо текст, що виїхав
  gCv->fillRect(CV_W - 8, 0, 6, LIST_ROW_H - 2, COL_SEL);       // праве поле
  gCv->fillRect(0, 0, 2, LIST_ROW_H, COL_BG);
  gCv->fillRect(CV_W - 2, 0, 2, LIST_ROW_H, COL_BG);
  cvBlit(gCv, 4, y);
}

static void drawListRow(int idx, int first, int sel, int count, LabelFn fn, LabelFn detail, const Marq& mq) {
  int r = idx - first;
  if (idx < 0 || idx >= count || r < 0 || r >= LIST_ROWS) return;
  int y = LIST_TOP + r * LIST_ROW_H;
  bool s = (idx == sel);
  char buf[128];
  listLabel(idx, s, fn, detail, buf, sizeof buf);
  if (s) {
    drawSelRow(buf, y, mq.active ? mq.off : 0);
    return;
  }
  char fit[128];
  tft.setFont(&FreeSans9ptCyr);
  fitText(buf, fit, sizeof fit, LIST_TEXT_W - 6);
  tft.fillRect(0, y, SCR_W, LIST_ROW_H, COL_BG);
  tft.setTextColor(COL_TEXT);
  tft.setCursor(12, y + 18);
  printU(fit);
}

static void drawListCounter(int sel, int count) {
  char buf[16];
  tft.fillRect(172, 4, 68, 22, COL_BG);
  snprintf(buf, sizeof buf, "%d/%d", sel + 1, count);
  tft.setFont(&FreeSans9ptCyr);
  tft.setTextColor(COL_DIM);
  rightText(buf, SCR_W - 8, 22);
}

static void drawListFull(const char* title, int count, LabelFn fn, LabelFn detail, int sel, const Marq& mq) {
  tft.fillScreen(COL_BG);
  tft.setFont(&FreeSans12ptCyr);
  tft.setTextColor(COL_DAY);
  tft.setCursor(8, 22);
  char t[64];
  fitText(title, t, sizeof t, 160);
  printU(t);
  tft.drawFastHLine(8, 28, SCR_W - 16, COL_LINE);

  int first = listFirst(sel, count);
  for (int r = 0; r < LIST_ROWS; r++) drawListRow(first + r, first, sel, count, fn, detail, mq);
  drawListCounter(sel, count);

  tft.setFont(&FreeSans9ptCyr);
  tft.setTextColor(COL_DIM);
  tft.setCursor(8, 314);
  printU("Л: вгору    Ц: ОК    П: вниз");
}

static void startMarqFor(Marq& mq, int sel, LabelFn fn, LabelFn detail) {
  char buf[128];
  listLabel(sel, true, fn, detail, buf, sizeof buf);
  tft.setFont(&FreeSans9ptCyr);
  marqStart(mq, textWidthU(buf), LIST_TEXT_W);
}

// Ліва: вгору, права: вниз, центральна: вибір. Повертає індекс.
// detail (необов'язково): довший текст для вибраного рядка; якщо він не вміщується, то біжить.
static int listMenu(const char* title, int count, LabelFn fn, int sel, LabelFn detail = nullptr) {
  if (sel < 0 || sel >= count) sel = 0;
  Marq mq = {};
  startMarqFor(mq, sel, fn, detail);
  drawListFull(title, count, fn, detail, sel, mq);
  int first = listFirst(sel, count);
  for (;;) {
    Nav nv = pollNav();
    if (nv.ok) return sel;

    int ns = sel;
    if (nv.step > 0) ns = (sel + 1) % count;
    if (nv.step < 0) ns = (sel + count - 1) % count;
    if (ns != sel) {
      startMarqFor(mq, ns, fn, detail);
      int nf = listFirst(ns, count);
      if (nf == first) {  // вікно не зсунулось: міняємо лише два рядки
        drawListRow(sel, first, ns, count, fn, detail, mq);
        drawListRow(ns, first, ns, count, fn, detail, mq);
      } else {
        for (int r = 0; r < LIST_ROWS; r++) drawListRow(nf + r, nf, ns, count, fn, detail, mq);
      }
      sel = ns;
      first = nf;
      drawListCounter(sel, count);
    } else if (marqTick(mq)) {
      drawListRow(sel, first, sel, count, fn, detail, mq);
    }
    delay(5);
  }
}

// ---------- Мережі Wi-Fi ----------
constexpr int MAX_NETS = 20;
struct NetItem {
  char ssid[33];   // справжня назва (для підключення)
  char label[34];  // назва для екрана (UTF-8)
  int rssi;
  bool secured;
  bool saved;      // пароль уже збережено
};
NetItem nets[MAX_NETS];
int netCount = 0;

static void scanNetworks() {
  showMessage("Пошук мереж...", "Wi-Fi", COL_TEXT);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  netCount = 0;
  for (int i = 0; i < n && netCount < MAX_NETS; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0 || s.length() > 32) continue;  // приховані мережі пропускаємо
    int dup = -1;
    for (int j = 0; j < netCount; j++) {
      if (s == nets[j].ssid) { dup = j; break; }
    }
    if (dup >= 0) {
      if (WiFi.RSSI(i) > nets[dup].rssi) nets[dup].rssi = WiFi.RSSI(i);
      continue;
    }
    NetItem& it = nets[netCount++];
    strlcpy(it.ssid, s.c_str(), sizeof it.ssid);
    strlcpy(it.label, it.ssid, sizeof it.label);  // UTF-8: українські назви теж виводяться
    it.rssi = WiFi.RSSI(i);
    it.secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    it.saved = (wifiStoreFind(it.ssid) >= 0);
  }
  WiFi.scanDelete();
  for (int i = 1; i < netCount; i++) {  // за силою сигналу
    NetItem t = nets[i];
    int j = i - 1;
    while (j >= 0 && nets[j].rssi < t.rssi) { nets[j + 1] = nets[j]; j--; }
    nets[j + 1] = t;
  }
}

// "+" збережена мережа, "*" мережа з паролем. Після мереж: "Шукати знову" і (за потреби) "Назад".
static void netLabel(int i, char* out, size_t n) {
  if (i == netCount) {
    strlcpy(out, "[ Шукати знову ]", n);
  } else if (i > netCount) {
    strlcpy(out, "[ Назад ]", n);
  } else {
    char nm[48];
    u8trunc(nets[i].label, nm, sizeof nm, 40);  // довге ім'я біжить у вибраному рядку
    snprintf(out, n, "%c%c %s", nets[i].saved ? '+' : ' ', nets[i].secured ? '*' : ' ', nm);
  }
}

static const char* tzName(int i) { return TIMEZONES[i].nameUa; }

static void tzLabel(int i, char* out, size_t n) { strlcpy(out, tzName(i), n); }

static void failLabel(int i, char* out, size_t n) {
  strlcpy(out, i == 0 ? "Виправити пароль" : "Обрати іншу мережу", n);
}

static int findTz(const String& posix) {
  for (int i = 0; i < TZ_COUNT; i++) {
    if (posix == TIMEZONES[i].posix) return i;
  }
  return 0;
}

static bool tryConnect(const char* ssid, const char* pass, const char* label) {
  char lb[64];
  u8trunc(label, lb, sizeof lb, 30);
  showMessage("Підключення...", lb, COL_TEXT);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, (pass && pass[0]) ? pass : nullptr);
  uint32_t t0 = millis();
  while (millis() - t0 < 20000) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) return true;
    if (st == WL_CONNECT_FAILED) break;  // зазвичай хибний пароль
    delay(200);
  }
  WiFi.disconnect();
  return false;
}

// ---------- Введення тексту (пароль, пошук за назвою) ----------
// Знизу екрана смуга символів: обраний посередині великий, а ліворуч і праворуч видно сусідні (наступні й попередні).
// Ліва/права (коротко): листати смугу. Утримання лівої або правої: обрана літера стає ВЕЛИКОЮ (ще раз: знову малою).
// Центральна (коротко): ввести обраний символ. Утримання центральної: ГОТОВО (підтвердити введене).
// Службові пункти на початку смуги: <- (стерти), X (вийти).
enum TokKind : uint8_t { TK_CHAR, TK_DONE, TK_BACK, TK_EXIT };
struct Tok {
  const char* s;   // UTF-8: підпис і (для TK_CHAR) сам символ
  uint8_t kind;
};

// Пароль: малі латинські літери, цифри та лише символи @ _ - #
static const Tok PASS_TOKS[] = {
  {"X", TK_EXIT}, {"<-", TK_BACK},
  {"a", TK_CHAR}, {"b", TK_CHAR}, {"c", TK_CHAR}, {"d", TK_CHAR}, {"e", TK_CHAR}, {"f", TK_CHAR}, {"g", TK_CHAR},
  {"h", TK_CHAR}, {"i", TK_CHAR}, {"j", TK_CHAR}, {"k", TK_CHAR}, {"l", TK_CHAR}, {"m", TK_CHAR}, {"n", TK_CHAR},
  {"o", TK_CHAR}, {"p", TK_CHAR}, {"q", TK_CHAR}, {"r", TK_CHAR}, {"s", TK_CHAR}, {"t", TK_CHAR}, {"u", TK_CHAR},
  {"v", TK_CHAR}, {"w", TK_CHAR}, {"x", TK_CHAR}, {"y", TK_CHAR}, {"z", TK_CHAR},
  {"0", TK_CHAR}, {"1", TK_CHAR}, {"2", TK_CHAR}, {"3", TK_CHAR}, {"4", TK_CHAR}, {"5", TK_CHAR}, {"6", TK_CHAR},
  {"7", TK_CHAR}, {"8", TK_CHAR}, {"9", TK_CHAR},
  {"@", TK_CHAR}, {"_", TK_CHAR}, {"-", TK_CHAR}, {"#", TK_CHAR},
};
constexpr int PASS_TOKS_N = sizeof(PASS_TOKS) / sizeof(PASS_TOKS[0]);
constexpr int PASS_TOKS_START = 2;  // починаємо з "a"

// Пошук населеного пункту: українська абетка, апостроф, дефіс, пробіл
static const Tok SEARCH_TOKS[] = {
  {"X", TK_EXIT}, {"<-", TK_BACK},
  {"а", TK_CHAR}, {"б", TK_CHAR}, {"в", TK_CHAR}, {"г", TK_CHAR}, {"ґ", TK_CHAR}, {"д", TK_CHAR}, {"е", TK_CHAR},
  {"є", TK_CHAR}, {"ж", TK_CHAR}, {"з", TK_CHAR}, {"и", TK_CHAR}, {"і", TK_CHAR}, {"ї", TK_CHAR}, {"й", TK_CHAR},
  {"к", TK_CHAR}, {"л", TK_CHAR}, {"м", TK_CHAR}, {"н", TK_CHAR}, {"о", TK_CHAR}, {"п", TK_CHAR}, {"р", TK_CHAR},
  {"с", TK_CHAR}, {"т", TK_CHAR}, {"у", TK_CHAR}, {"ф", TK_CHAR}, {"х", TK_CHAR}, {"ц", TK_CHAR}, {"ч", TK_CHAR},
  {"ш", TK_CHAR}, {"щ", TK_CHAR}, {"ь", TK_CHAR}, {"ю", TK_CHAR}, {"я", TK_CHAR},
  {"'", TK_CHAR}, {"-", TK_CHAR}, {" ", TK_CHAR},
};
constexpr int SEARCH_TOKS_N = sizeof(SEARCH_TOKS) / sizeof(SEARCH_TOKS[0]);
constexpr int SEARCH_TOKS_START = 2;  // починаємо з "а"

struct TextCfg {
  const char* title;     // заголовок (UTF-8)
  const char* sub;       // рядок під заголовком (UTF-8): назва мережі, підказка
  const Tok* toks;
  int nToks;
  int startIdx;
  int maxBytes;          // максимум байтів тексту (без нуля)
  int minChars;          // мінімум символів
  const char* minMsg;    // що показати, якщо тексту менше
  const char* doneDesc;  // опис пункту OK
  bool mono;             // моноширинний шрифт поля (пароль)
};

// Літера, яку можна зробити великою: латинська a-z або українська (UTF-8, два байти)
static bool tokIsLetter(const Tok& t) {
  if (t.kind != TK_CHAR) return false;
  uint8_t c = (uint8_t)t.s[0];
  return (c >= 'a' && c <= 'z') || c == 0xD0 || c == 0xD1 || c == 0xD2;
}

// Велика літера: a-z -> A-Z, а-я і є і ї ґ -> А-Я Є І Ї Ґ
static void u8Upper(const char* in, char* out, size_t n) {
  strlcpy(out, in, n);
  uint8_t c0 = (uint8_t)out[0], c1 = (uint8_t)out[1];
  if (c0 >= 'a' && c0 <= 'z') out[0] = (char)(c0 - 'a' + 'A');
  else if (c0 == 0xD0 && c1 >= 0xB0 && c1 <= 0xBF) out[1] = (char)(c1 - 0x20);  // а-п
  else if (c0 == 0xD1 && c1 >= 0x80 && c1 <= 0x8F) { out[0] = (char)0xD0; out[1] = (char)(c1 + 0x20); }  // р-я
  else if (c0 == 0xD1 && c1 == 0x94) { out[0] = (char)0xD0; out[1] = (char)0x84; }  // є
  else if (c0 == 0xD1 && c1 == 0x96) { out[0] = (char)0xD0; out[1] = (char)0x86; }  // і
  else if (c0 == 0xD1 && c1 == 0x97) { out[0] = (char)0xD0; out[1] = (char)0x87; }  // ї
  else if (c0 == 0xD2 && c1 == 0x91) out[1] = (char)0x90;                          // ґ
}

// Усе, що ввели, у малі літери (для пошуку за назвою регістр не важливий)
static void u8LowerInPlace(char* s) {
  for (uint8_t* p = (uint8_t*)s; *p; p++) {
    if (*p >= 'A' && *p <= 'Z') *p = *p - 'A' + 'a';
    else if (*p == 0xD0 && p[1] >= 0x90 && p[1] <= 0x9F) { p[1] += 0x20; p++; }          // А-П
    else if (*p == 0xD0 && p[1] >= 0xA0 && p[1] <= 0xAF) { *p = 0xD1; p[1] -= 0x20; p++; }  // Р-Я
    else if (*p == 0xD0 && p[1] == 0x84) { *p = 0xD1; p[1] = 0x94; p++; }                // Є
    else if (*p == 0xD0 && p[1] == 0x86) { *p = 0xD1; p[1] = 0x96; p++; }                // І
    else if (*p == 0xD0 && p[1] == 0x87) { *p = 0xD1; p[1] = 0x97; p++; }                // Ї
    else if (*p == 0xD2 && p[1] == 0x90) { p[1] = 0x91; p++; }                            // Ґ
    else if (*p >= 0xC0) p++;  // інший двобайтовий символ: пропускаємо обидва байти
  }
}

// Підпис символу: з урахуванням того, що обрану літеру зробили великою
static void tokLabel(const Tok& t, bool upper, char* out, size_t n) {
  if (upper && tokIsLetter(t)) u8Upper(t.s, out, n);
  else strlcpy(out, t.s, n);
}

static int utf8Len(const char* s) {
  int n = 0;
  for (const uint8_t* p = (const uint8_t*)s; *p; p++) {
    if ((*p & 0xC0) != 0x80) n++;
  }
  return n;
}

static void drawEntryStatic(const TextCfg& c) {
  tft.fillScreen(COL_BG);
  tft.setFont(&FreeSans12ptCyr);
  tft.setTextColor(COL_DAY);
  tft.setCursor(8, 22);
  printU(c.title);
  tft.drawFastHLine(8, 28, SCR_W - 16, COL_LINE);

  char buf[64];
  tft.setFont(&FreeSans9ptCyr);
  tft.setTextColor(COL_DIM);
  fitText(c.sub, buf, sizeof buf, SCR_W - 16);
  tft.setCursor(8, 50);
  printU(buf);

  tft.drawRoundRect(4, 58, SCR_W - 8, 44, 6, COL_LINE);

  // Підказки внизу: як вводити, і де великі літери
  tft.setFont(&FreeSans9ptCyr);
  tft.setTextColor(COL_TEXT);
  tft.setCursor(8, 248);
  printU("Л / П: обрати,  Ц: ввести");
  tft.setTextColor(COL_FEELS);
  tft.setCursor(8, 266);
  printU("Утримуйте Л або П: ВЕЛИКА");
  tft.setTextColor(COL_OK);
  tft.setCursor(8, 284);
  printU("Утримуйте Ц: ГОТОВО");
  tft.setTextColor(COL_DIM);
  tft.setCursor(8, 304);
  printU("<- стерти,  X вихід без змін");
}

// Поле тексту і лічильник
static void drawEntryText(const TextCfg& c, const char* text, bool upper) {
  char buf[24];
  tft.fillRect(176, 4, SCR_W - 180, 22, COL_BG);
  snprintf(buf, sizeof buf, "%d сим.", utf8Len(text));
  tft.setFont(&FreeSans9ptCyr);
  tft.setTextColor(COL_DIM);
  rightText(buf, SCR_W - 8, 22);

  tft.fillRect(6, 60, SCR_W - 12, 40, COL_BG);
  if (c.mono) {
    tft.setFont(&FreeMonoBold12ptCyr);
    int len = strlen(text);
    int vis = 15;  // скільки символів вміщується (14 пікселів на символ)
    int start = len > vis ? len - vis : 0;
    tft.setTextColor(COL_DAY);
    tft.setCursor(10, 87);
    for (int i = start; i < len; i++) tft.write((uint8_t)text[i]);
    int cx = tft.getCursorX();
    tft.fillRect(cx + 1, 67, 3, 26, COL_FEELS);
  } else {
    tft.setFont(&FreeSans12ptCyr);
    const char* s = text;
    while (*s && textWidthU(s) > SCR_W - 34) {  // довгий запит: показуємо кінець
      s++;
      while (((uint8_t)*s & 0xC0) == 0x80) s++;
    }
    tft.setTextColor(COL_DAY);
    tft.setCursor(10, 87);
    printU(s);
    int cx = tft.getCursorX();
    tft.fillRect(cx + 2, 67, 3, 26, COL_FEELS);
  }
}

// Смуга символів: 9 клітинок, обраний посередині
static void drawEntryStrip(const TextCfg& c, int sel, bool upper) {
  tft.fillRect(0, 124, SCR_W, 110, COL_BG);
  const int CW = 24;
  char lab[16];
  for (int j = -4; j <= 4; j++) {
    int idx = ((sel + j) % c.nToks + c.nToks) % c.nToks;
    const Tok& t = c.toks[idx];
    tokLabel(t, j == 0 && upper, lab, sizeof lab);  // велика лише обрана літера
    const char* show = (t.kind == TK_CHAR && t.s[0] == ' ') ? "_" : lab;
    int cx = SCR_W / 2 + j * CW + (j > 0 ? 8 : (j < 0 ? -8 : 0));
    if (j == 0) {
      bool ctrl = (t.kind != TK_CHAR);
      tft.fillRoundRect(cx - 22, 128, 44, 58, 7, ctrl ? COL_SEL : COL_FEELS);
      tft.drawRoundRect(cx - 22, 128, 44, 58, 7, COL_DAY);
      if (ctrl) {
        tft.setFont(&FreeSans12ptCyr);
        tft.setTextColor(COL_DAY);
        tft.setCursor(cx - textWidthU(show) / 2, 164);
      } else {
        tft.setFont(&FreeSansBold24ptCyr);
        tft.setTextColor(COL_BG);
        tft.setCursor(cx - textWidthU(show) / 2, 172);
      }
      printU(show);
    } else {
      int a = j < 0 ? -j : j;
      tft.setFont(t.kind == TK_CHAR ? &FreeSans12ptCyr : &FreeSans9ptCyr);
      tft.setTextColor(a >= 3 ? COL_DIM : COL_TEXT);
      tft.setCursor(cx - textWidthU(show) / 2, t.kind == TK_CHAR ? 164 : 162);
      printU(show);
    }
  }
  // Опис обраного пункту
  const Tok& cur = c.toks[sel];
  const char* d = "";
  switch (cur.kind) {
    case TK_DONE: d = c.doneDesc; break;
    case TK_BACK: d = "Стерти останній символ"; break;
    case TK_EXIT: d = "Вийти без змін"; break;
    default:
      if (cur.s[0] == ' ') d = "Пробіл";
      else if (tokIsLetter(cur)) d = upper ? "Буде введено ВЕЛИКУ" : "";
      break;
  }
  tft.setFont(&FreeSans9ptCyr);
  tft.setTextColor(COL_FEELS);
  centerText(d, 214);
}

// Кнопки введення тексту: коротке натискання діє при відпусканні, утримання (BTN_LONG_MS) спрацьовує під час утримання
enum : uint8_t { TN_NONE, TN_SHORT, TN_LONG };

static uint8_t pollTextButton(Button& b) {
  uint32_t now = millis();
  bool raw = (digitalRead(b.pin) == LOW);
  uint8_t ev = TN_NONE;
  if (raw != b.raw) {
    b.raw = raw;
    b.changedMs = now;
  }
  if (raw != b.down && (now - b.changedMs) >= BTN_DEBOUNCE_MS) {
    b.down = raw;
    if (raw) {
      b.pressMs = now;
      b.longFired = false;
    } else if (!b.longFired) {
      ev = TN_SHORT;
    }
  }
  if (b.down && !b.longFired && (now - b.pressMs) >= BTN_LONG_MS) {
    b.longFired = true;
    ev = TN_LONG;
  }
  return ev;
}

// Смужка прогресу утримання під смугою символів (показується після ~15% часу, щоб не миготіла)
static int gHoldBarW = -1;
static void drawHoldBar() {
  float f = 0;
  Button* bs[3] = {&btnL, &btnC, &btnR};
  for (Button* b : bs) {
    if (b->down && !b->longFired) {
      float x = (float)(millis() - b->pressMs) / (float)BTN_LONG_MS;
      if (x > f) f = x;
    }
  }
  int w = (f > 0.15f) ? (int)(f * (SCR_W - 16)) : 0;
  if (w > SCR_W - 16) w = SCR_W - 16;
  if (w == gHoldBarW) return;
  gHoldBarW = w;
  tft.fillRect(8, 232, SCR_W - 16, 4, COL_BG);
  if (w > 0) tft.fillRect(8, 232, w, 4, COL_FEELS);
}

// Повертає true, якщо введення підтверджено (OK), false, якщо вийшли (X)
static bool enterText(const TextCfg& cfg, char* buf, size_t cap) {
  waitButtonsReleased();  // кнопка, якою відкрили екран, ще може бути натиснута
  int sel = cfg.startIdx;
  bool upper = false;  // обрана літера велика (скидається при переході до іншого символу й після вводу)
  gHoldBarW = -1;
  drawEntryStatic(cfg);
  drawEntryText(cfg, buf, upper);
  drawEntryStrip(cfg, sel, upper);

  for (;;) {
    uint8_t el = pollTextButton(btnL);
    uint8_t er = pollTextButton(btnR);
    uint8_t ec = pollTextButton(btnC);
    drawHoldBar();

    int step = 0;
    if (el == TN_SHORT) step = -1;
    else if (er == TN_SHORT) step = 1;
    if (step) {
      sel = (sel + step + cfg.nToks) % cfg.nToks;
      upper = false;
      drawEntryStrip(cfg, sel, upper);
    }
    if ((el == TN_LONG || er == TN_LONG) && tokIsLetter(cfg.toks[sel])) {
      upper = !upper;  // утримання кнопки вибору літери: ВЕЛИКА (ще раз: мала)
      drawEntryStrip(cfg, sel, upper);
    }
    if (ec == TN_LONG) {  // утримання центральної: готово
      if (utf8Len(buf) < cfg.minChars) {
        showMessage(cfg.minMsg, nullptr, COL_BAD);
        delay(1500);
        gHoldBarW = -1;
        drawEntryStatic(cfg);  // після повного повідомлення екран треба відновити
        drawEntryText(cfg, buf, upper);
        drawEntryStrip(cfg, sel, upper);
      } else {
        return true;
      }
    }
    if (ec == TN_SHORT) {
      const Tok& t = cfg.toks[sel];
      if (t.kind == TK_CHAR) {
        char lab[16];
        tokLabel(t, upper, lab, sizeof lab);
        if ((int)strlen(buf) + (int)strlen(lab) <= cfg.maxBytes && strlen(buf) + strlen(lab) < cap) {
          strlcat(buf, lab, cap);
          upper = false;
          drawEntryText(cfg, buf, upper);
          drawEntryStrip(cfg, sel, upper);
        }
      } else if (t.kind == TK_BACK) {
        size_t l = strlen(buf);
        while (l > 0) {  // стираємо один символ UTF-8
          l--;
          if (((uint8_t)buf[l] & 0xC0) != 0x80) break;
        }
        buf[l] = 0;
        drawEntryText(cfg, buf, upper);
      } else if (t.kind == TK_EXIT) {
        return false;
      }
    }
    delay(5);
  }
}

constexpr int PASS_MAX = 63;

// Повертає false, якщо користувач вийшов (X)
static bool enterPassword(const char* netName, char* pass, size_t cap) {
  TextCfg cfg = {"Пароль Wi-Fi", netName, PASS_TOKS, PASS_TOKS_N, PASS_TOKS_START, PASS_MAX, 8,
                 "Мінімум 8 символів", "Готово: підключитись", true};
  return enterText(cfg, pass, cap);
}

// ---------- Майстер Wi-Fi ----------
// Вибір мережі, пароль, підключення. Зберігає мережу у список відомих і лишає Wi-Fi підключеним.
// allowCancel: додає пункт "Назад" (для виклику з меню); тоді повертає false, якщо користувач передумав.
bool wizardWifi(bool allowCancel) {
  waitButtonsReleased();
  WiFi.mode(WIFI_STA);

  char pass[PASS_MAX + 1] = "";
  char ssidSel[33] = "";
  bool securedSel = false;
  bool connected = false;

  while (!connected) {
    // 1. Вибір мережі
    int sel = -1;
    for (;;) {
      scanNetworks();
      int extra = allowCancel ? 2 : 1;
      int s = listMenu("Мережа Wi-Fi", netCount + extra, netLabel, 0);
      if (s < netCount) { sel = s; break; }
      if (s > netCount) return false;  // "Назад"
    }
    strlcpy(ssidSel, nets[sel].ssid, sizeof ssidSel);
    securedSel = nets[sel].secured;
    bool savedSel = nets[sel].saved;
    char label[34];
    strlcpy(label, nets[sel].label, sizeof label);
    pass[0] = 0;

    // 2a. Відома мережа: пробуємо збережений пароль, вводити його знову не потрібно
    if (savedSel) {
      int slot = wifiStoreFind(ssidSel);
      SavedNet sn;
      if (slot >= 0 && wifiStoreLoad(slot, sn)) {
        strlcpy(pass, sn.pass, sizeof pass);
        if (tryConnect(ssidSel, pass, label)) {
          connected = true;
          break;
        }
        showMessage("Пароль не підійшов", "Введіть пароль знову", COL_BAD);
        delay(1800);
        pass[0] = 0;
      }
    }

    // 2b. Нова мережа (або пароль змінився): введення пароля і підключення
    bool cancelled = false;
    for (;;) {
      if (securedSel && !enterPassword(label, pass, sizeof pass)) { cancelled = true; break; }
      if (tryConnect(ssidSel, securedSel ? pass : "", label)) {
        connected = true;
        break;
      }
      showMessage("Не підключено", securedSel ? "Перевірте пароль" : "Спробуйте ще раз", COL_BAD);
      delay(2000);
      if (!securedSel) break;
      if (listMenu("Що далі?", 2, failLabel, 0) == 1) break;  // обрати іншу мережу
    }
    if (cancelled) continue;
  }

  wifiStoreSave(ssidSel, securedSel ? pass : "");
  return true;
}

// ---------- Майстер часового поясу ----------
void wizardTz(Preferences& prefs) {
  waitButtonsReleased();
  int start = findTz(prefs.isKey("tz") ? prefs.getString("tz", DEFAULT_TZ_INFO) : String(DEFAULT_TZ_INFO));
  int tz = listMenu("Часовий пояс", TZ_COUNT, tzLabel, start);
  prefs.putString("tz", TIMEZONES[tz].posix);
  showMessage("Збережено", tzName(tz), COL_OK);
  delay(1200);
}

// Перше налаштування: мережа і часовий пояс (населений пункт вибирається окремо, див. places.h)
void runSetupWizard(Preferences& prefs) {
  wizardWifi(false);
  wizardTz(prefs);
}

#pragma once
// Вибір населеного пункту: область -> район -> (літера) -> населений пункт -> (оператор) -> черга.
// Із бази береться й широта/довгота пункту, за якими запитується погода. Для областей без відкритих
// даних про графік відключень пристрій працює лише як погодна станція.
// Підключається ПІСЛЯ wizard.h, sources.h і schedule.h.
// База населених пунктів (places_data.h) зберігається у флеш-пам'яті й читається без копіювання в ОЗП.

#include "places_data.h"

constexpr int LETTER_MIN_PLACES = 30;  // якщо в районі більше пунктів, спершу вибирається перша літера

// ---------- Доступ до бази ----------
struct LetGrp {
  uint8_t c;       // перша літера (CP1251)
  uint16_t first;  // номер першого пункту групи в районі
  uint16_t count;
};

static uint32_t* gOff = nullptr;  // зсуви записів поточного району в PLACES
static int gOffN = 0;
static LetGrp gLet[40];
static int gLetN = 0;
static int gGrpFirst = 0, gGrpCount = 0;  // діапазон пунктів, що показується (весь район або літера)
static int gCurObl = 0;                   // вибрана область (для підписів)

static const char* recName(int placeIdxInRaion) { return (const char*)(PLACES + gOff[placeIdxInRaion] + PL_HDR); }
// Координати запису (див. tools/gen_places.py)
static void recLatLon(const uint8_t* r, float& lat, float& lon) {
  lat = PL_LAT0 + (float)(r[2] | (r[3] << 8)) / PL_LAT_K;
  lon = PL_LON0 + (float)(r[4] | (r[5] << 8)) / PL_LON_K;
}
static int recHrom(int placeIdxInRaion) {
  const uint8_t* r = (const uint8_t*)(PLACES + gOff[placeIdxInRaion]);
  return (int)(r[0] - 1) + 255 * (int)(r[1] - 1);
}

// Будує таблицю зсувів і групи за першою літерою для району
static bool loadRaion(int ri) {
  if (gOff) { free(gOff); gOff = nullptr; }
  gOffN = RAIONS[ri].count;
  gOff = (uint32_t*)malloc(sizeof(uint32_t) * (gOffN > 0 ? gOffN : 1));
  if (!gOff) { gOffN = 0; return false; }
  const uint8_t* p = PLACES + RAIONS[ri].off;
  gLetN = 0;
  for (int i = 0; i < gOffN; i++) {
    gOff[i] = (uint32_t)(p - PLACES);
    uint8_t c = p[PL_HDR];
    if (gLetN == 0 || gLet[gLetN - 1].c != c) {
      if (gLetN < 40) {
        gLet[gLetN].c = c;
        gLet[gLetN].first = i;
        gLet[gLetN].count = 0;
        gLetN++;
      }
    }
    gLet[gLetN - 1].count++;
    p += PL_HDR;
    p += strlen((const char*)p) + 1;
  }
  return true;
}

// ---------- Підписи для списків ----------
static bool oblHasSource(int i) {
  const OblSource* s = findSource(OBLS[i].pcode);
  return s && s->nOps > 0;
}

static void oblLabel(int i, char* out, size_t n) {
  if (i == 0) { strlcpy(out, "[ Пошук за назвою ]", n); return; }
  if (i > OBL_COUNT) { strlcpy(out, "[ Скасувати ]", n); return; }
  snprintf(out, n, "%s%s", oblHasSource(i - 1) ? "  " : "- ", OBLS[i - 1].name);
}

static void oblDetail(int i, char* out, size_t n) {
  oblLabel(i, out, n);
  if (i >= 1 && i <= OBL_COUNT && !oblHasSource(i - 1)) strlcat(out, " (лише погода)", n);
}

static void raionLabel(int i, char* out, size_t n) {
  if (i == 0) { strlcpy(out, "[ Назад ]", n); return; }
  snprintf(out, n, "%s р-н", RAIONS[OBLS[gCurObl].raion0 + i - 1].name);
}

static void letterLabel(int i, char* out, size_t n) {
  if (i == 0) { strlcpy(out, "[ Назад ]", n); return; }
  const LetGrp& g = gLet[i - 1];
  char c1[2] = {(char)g.c, 0}, u[8];
  cp1251ToUtf8(c1, u, sizeof u);
  snprintf(out, n, "%s   (%d)", u, (int)g.count);
}

static void placeLabel(int i, char* out, size_t n) {
  if (i == 0) { strlcpy(out, "[ Назад ]", n); return; }
  cp1251ToUtf8(recName(gGrpFirst + i - 1), out, n);
}

// Для вибраного рядка: назва і громада; якщо не вміщується, біжить
static void placeDetail(int i, char* out, size_t n) {
  if (i == 0) { strlcpy(out, "[ Назад ]", n); return; }
  char nm[64];
  cp1251ToUtf8(recName(gGrpFirst + i - 1), nm, sizeof nm);
  snprintf(out, n, "%s (%s громада)", nm, HROMS[recHrom(gGrpFirst + i - 1)]);
}

static const OblSource* gOpSrc = nullptr;
static void opLabel(int i, char* out, size_t n) {
  if (i == 0) { strlcpy(out, "[ Назад ]", n); return; }
  strlcpy(out, gOpSrc->ops[i - 1].name, n);
}

static QueueList gQl;
static void queueLabel(int i, char* out, size_t n) {
  if (i == 0) { strlcpy(out, "[ Назад ]", n); return; }
  snprintf(out, n, "Черга %s", queueShort(gQl.key[i - 1]));
}

// Запит черг іде в окремому завданні з великим стеком: TLS не поміщається в стандартний стек loop()
struct QJob {
  const OblSource* src;
  int op;
  volatile bool done;
  volatile bool ok;
};
static QJob gQJob;

static void qListTask(void*) {
  gQJob.ok = fetchQueueList(gQJob.src, gQJob.op, gQl);
  gQJob.done = true;
  vTaskDelete(nullptr);
}

static bool fetchQueueListTask(const OblSource* src, int op) {
  gQJob.src = src;
  gQJob.op = op;
  gQJob.done = false;
  gQJob.ok = false;
  gQl.n = 0;
  if (xTaskCreate(qListTask, "qlist", 16384, nullptr, 1, nullptr) != pdPASS) return fetchQueueList(src, op, gQl);
  uint32_t t0 = millis();
  while (!gQJob.done && millis() - t0 < 60000) delay(20);
  if (!gQJob.done) {  // дуже довго: беремо стандартні черги
    gQl.n = 0;
    return false;
  }
  return gQJob.ok;
}

// ---------- Пошук за назвою ----------
// Введення української назви кнопками, пошук по всій базі (спершу назви, що починаються з введеного,
// потім ті, що містять його). У результатах біля назви показано область і район, щоб не переплутати однойменні села.
constexpr int SEARCH_MAXLEN = 24;  // символів у запиті (байт UTF-8 до 2 на символ)

static uint8_t lc1251(uint8_t c) {
  if (c >= 0xC0 && c <= 0xDF) return c + 0x20;
  switch (c) {
    case 0xA5: return 0xB4;  // Ґ
    case 0xAA: return 0xBA;  // Є
    case 0xB2: return 0xB3;  // І
    case 0xAF: return 0xBF;  // Ї
  }
  return c;
}

// Порядок літер для сортування результатів (а б в г ґ д е є ж з и і ї й к ...)
static int uaRank(uint8_t c) {
  static const uint8_t ORDER[] = {0xE0,0xE1,0xE2,0xE3,0xB4,0xE4,0xE5,0xBA,0xE6,0xE7,0xE8,0xB3,0xBF,0xE9,0xEA,0xEB,0xEC,
                                  0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFC,0xFE,0xFF};
  c = lc1251(c);
  for (int i = 0; i < (int)sizeof ORDER; i++) if (ORDER[i] == c) return 128 + i;
  return c < 128 ? c : 200;
}

struct SRes {
  uint32_t off;   // зсув запису в PLACES
  uint8_t raion;
  uint8_t obl;
  uint8_t grp;    // 0 починається з запиту, 1 містить
};
constexpr int SRES_MAX = 300;
static SRes gSr[SRES_MAX];
static int gSrN = 0;

static int oblOfRaion(int r) {
  for (int i = 0; i < OBL_COUNT; i++) {
    if (r >= OBLS[i].raion0 && r < OBLS[i].raion0 + OBLS[i].nRaion) return i;
  }
  return 0;
}

// 0 не збігається, 1 починається з запиту, 2 містить
static int matchName(const uint8_t* name, const uint8_t* q, int ql) {
  int pos = 0;
  for (const uint8_t* s = name; *s; s++, pos++) {
    int k = 0;
    while (k < ql && s[k] && lc1251(s[k]) == q[k]) k++;
    if (k == ql) return pos == 0 ? 1 : 2;
  }
  return 0;
}

static bool srLess(const SRes& a, const SRes& b) {
  if (a.grp != b.grp) return a.grp < b.grp;
  const uint8_t* x = (const uint8_t*)(PLACES + a.off + PL_HDR);
  const uint8_t* y = (const uint8_t*)(PLACES + b.off + PL_HDR);
  while (*x && *y) {
    int rx = uaRank(*x), ry = uaRank(*y);
    if (rx != ry) return rx < ry;
    x++; y++;
  }
  if (*x != *y) return *x == 0;
  return a.off < b.off;
}

// Слово-фільтр (початок слова області, району чи громади) для запитів виду "іванівка київська"
static bool ctxHasPrefix(const uint8_t* ctx, const uint8_t* tok, int tl) {
  for (const uint8_t* s = ctx; *s; s++) {
    if (s != ctx && s[-1] != ' ' && s[-1] != '-') continue;
    int k = 0;
    while (k < tl && s[k] && lc1251(s[k]) == tok[k]) k++;
    if (k == tl) return true;
  }
  return false;
}

// Запит із пробілами: ліва частина має початися з назви пункту, а кожне слово праворуч (область, район, громада)
// має бути початком слова в описі місця. Так серед сотні однойменних сіл знаходиться потрібне.
static bool matchWithFilter(const uint8_t* name, const uint8_t* q, int ql, int rIdx, const uint8_t* rec) {
  char u[200], c[200];
  int hrom = (int)(rec[0] - 1) + 255 * (int)(rec[1] - 1);
  snprintf(u, sizeof u, "%s %s %s", OBLS[oblOfRaion(rIdx)].name, RAIONS[rIdx].name, HROMS[hrom]);
  toCp1251(u, c, sizeof c);
  for (int sp = 1; sp < ql - 1; sp++) {
    if (q[sp] != ' ') continue;
    if (matchName(name, q, sp) != 1) continue;  // назва має починатися з лівої частини
    bool all = true;
    int i = sp + 1;
    while (i < ql && all) {
      while (i < ql && q[i] == ' ') i++;
      int j = i;
      while (j < ql && q[j] != ' ') j++;
      if (j > i && !ctxHasPrefix((const uint8_t*)c, q + i, j - i)) all = false;
      i = j;
    }
    if (all) return true;
  }
  return false;
}

static void runSearch(const char* utf8) {
  char q1251[64];
  toCp1251(utf8, q1251, sizeof q1251);
  uint8_t q[64];
  int ql = 0;
  for (const uint8_t* p = (const uint8_t*)q1251; *p && ql < 60; p++) q[ql++] = lc1251(*p);
  gSrN = 0;
  if (ql == 0) return;
  bool hasSpace = false;
  for (int i = 1; i < ql - 1; i++) if (q[i] == ' ') hasSpace = true;
  for (int r = 0; r < RAION_COUNT; r++) {
    const uint8_t* p = PLACES + RAIONS[r].off;
    for (int i = 0; i < RAIONS[r].count; i++) {
      int m = matchName(p + PL_HDR, q, ql);
      if (!m && hasSpace && matchWithFilter(p + PL_HDR, q, ql, r, p)) m = 1;
      if (m && gSrN < SRES_MAX) {
        // коли місця немає, "починається з" витісняє "містить" з кінця
        gSr[gSrN++] = {(uint32_t)(p - PLACES), (uint8_t)r, (uint8_t)oblOfRaion(r), (uint8_t)(m - 1)};
      } else if (m == 1 && gSrN >= SRES_MAX) {
        for (int k = SRES_MAX - 1; k >= 0; k--) {
          if (gSr[k].grp == 1) { gSr[k] = {(uint32_t)(p - PLACES), (uint8_t)r, (uint8_t)oblOfRaion(r), 0}; break; }
        }
      }
      p += PL_HDR;
      p += strlen((const char*)p) + 1;
    }
  }
  for (int i = 1; i < gSrN; i++) {  // вставками: максимум 80 елементів
    SRes t = gSr[i];
    int j = i - 1;
    while (j >= 0 && srLess(t, gSr[j])) { gSr[j + 1] = gSr[j]; j--; }
    gSr[j + 1] = t;
  }
}

static void srText(int i, bool withHromada, char* out, size_t n) {
  if (i == 0) { strlcpy(out, "[ Назад ]", n); return; }
  const SRes& r = gSr[i - 1];
  char nm[48], where[96];
  cp1251ToUtf8((const char*)(PLACES + r.off + PL_HDR), nm, sizeof nm);
  if (OBLS[r.obl].nRaion == 1) snprintf(where, sizeof where, "%s", OBLS[r.obl].name);
  else snprintf(where, sizeof where, "%s, %s р-н", OBLS[r.obl].name, RAIONS[r.raion].name);
  if (withHromada) {
    const uint8_t* rec = (const uint8_t*)(PLACES + r.off);
    int h = (int)(rec[0] - 1) + 255 * (int)(rec[1] - 1);
    snprintf(out, n, "%s (%s, %s громада)", nm, where, HROMS[h]);
  } else {
    snprintf(out, n, "%s (%s)", nm, where);
  }
}
static void srLabel(int i, char* out, size_t n) { srText(i, false, out, n); }
static void srDetail(int i, char* out, size_t n) { srText(i, true, out, n); }

// Введення назви: смуга літер (див. enterText у wizard.h). Повертає true для пошуку, false для виходу.
static bool enterSearch(char* txt, size_t n) {
  TextCfg cfg = {"Пошук пункту", "Мінімум 2 літери", SEARCH_TOKS, SEARCH_TOKS_N, SEARCH_TOKS_START,
                 SEARCH_MAXLEN * 2, 2, "Мінімум 2 літери", "Готово: шукати", false};
  if (!enterText(cfg, txt, n)) return false;
  u8LowerInPlace(txt);  // регістр у пошуку не важливий: далі шукаємо малими літерами
  return true;
}

// ---------- Збереження ----------
// Пункт вважається налаштованим, якщо збережено назву й координати. Черга потрібна лише для графіка відключень.
static bool loadPlace(Preferences& prefs, PlaceCfg& pc) {
  memset(&pc, 0, sizeof pc);
  if (!prefs.isKey("pname") || !prefs.isKey("lat") || !prefs.isKey("lon")) return false;
  String n = prefs.getString("pname", "");
  if (n.length() == 0) return false;
  strlcpy(pc.name, n.c_str(), sizeof pc.name);
  pc.lat = prefs.getFloat("lat", 0.0f);
  pc.lon = prefs.getFloat("lon", 0.0f);
  if (pc.lat < 40.0f || pc.lat > 53.0f || pc.lon < 21.0f || pc.lon > 41.0f) return false;  // хибні координати: налаштувати знову
  pc.valid = true;
  // Графік відключень: є область, оператор і черга, а джерело даних існує
  String o = prefs.isKey("obl") ? prefs.getString("obl", "") : String("");
  String q = prefs.isKey("q") ? prefs.getString("q", "") : String("");
  uint8_t op = prefs.getUChar("op", 0);
  const OblSource* s = o.length() ? findSource(o.c_str()) : nullptr;
  if (s && op < s->nOps && q.length() > 0) {
    strlcpy(pc.obl, o.c_str(), sizeof pc.obl);
    strlcpy(pc.queue, q.c_str(), sizeof pc.queue);
    pc.op = op;
    pc.outage = true;
  }
  return true;
}

static void savePlace(Preferences& prefs, const PlaceCfg& pc) {
  prefs.putString("pname", pc.name);
  prefs.putFloat("lat", pc.lat);
  prefs.putFloat("lon", pc.lon);
  prefs.putString("obl", pc.outage ? pc.obl : "");
  prefs.putUChar("op", pc.op);
  prefs.putString("q", pc.outage ? pc.queue : "");
}

static void noOutageLabel(int i, char* out, size_t n) {
  strlcpy(out, i == 0 ? "Зберегти: лише погода" : "[ Назад ]", n);
}

// ---------- Майстер вибору ----------
// Повертає true, якщо користувач дійшов до кінця (результат у out і в prefs).
// "Назад" у кожному списку повертає на попередній крок. allowCancel додає на першому кроці
// пункт "Скасувати" (для виклику з меню налаштувань); тоді повертає false і нічого не змінює.
static bool runPlaceWizard(Preferences& prefs, PlaceCfg& out, bool allowCancel) {
  waitButtonsReleased();
  enum { ST_OBL, ST_RAION, ST_LETTER, ST_PLACE, ST_OP, ST_QUEUE, ST_SEARCH, ST_SRES, ST_NOOUT };
  int st = ST_OBL;
  int oi = 0, ri = 0, opi = 0;
  int selMem[9] = {0, 1, 1, 1, 1, 1, 0, 1, 0};
  bool useLetters = false, skipRaion = false, fromSearch = false;
  char sq[SEARCH_MAXLEN * 2 + 2] = "";
  const OblSource* src = nullptr;
  char placeName[48] = "";
  float pLat = 0, pLon = 0;
  bool hasData = false;  // для вибраної області є графік відключень

  for (;;) {
    switch (st) {
      case ST_OBL: {
        int s = listMenu("Область", OBL_COUNT + 1 + (allowCancel ? 1 : 0), oblLabel, selMem[ST_OBL], oblDetail);
        if (s > OBL_COUNT) { if (gOff) { free(gOff); gOff = nullptr; } return false; }
        selMem[ST_OBL] = s;
        if (s == 0) { st = ST_SEARCH; break; }
        s -= 1;  // далі s: номер області в OBLS
        fromSearch = false;
        src = findSource(OBLS[s].pcode);
        hasData = (src && src->nOps > 0);
        oi = s;
        gCurObl = oi;
        skipRaion = (OBLS[oi].nRaion == 1);
        if (skipRaion) {
          ri = OBLS[oi].raion0;
          if (!loadRaion(ri)) { showMessage("Недостатньо пам'яті", nullptr, COL_BAD); delay(2000); break; }
          useLetters = (RAIONS[ri].count > LETTER_MIN_PLACES);
          if (useLetters) { selMem[ST_LETTER] = 1; st = ST_LETTER; }
          else { gGrpFirst = 0; gGrpCount = RAIONS[ri].count; selMem[ST_PLACE] = 1; st = ST_PLACE; }
        } else {
          selMem[ST_RAION] = 1;
          st = ST_RAION;
        }
        break;
      }

      case ST_RAION: {
        int n = OBLS[oi].nRaion;
        int s = listMenu("Район", n + 1, raionLabel, selMem[ST_RAION]);
        selMem[ST_RAION] = s;
        if (s == 0) { st = ST_OBL; break; }
        ri = OBLS[oi].raion0 + s - 1;
        if (!loadRaion(ri)) { showMessage("Недостатньо пам'яті", nullptr, COL_BAD); delay(2000); break; }
        useLetters = (RAIONS[ri].count > LETTER_MIN_PLACES);
        if (useLetters) { selMem[ST_LETTER] = 1; st = ST_LETTER; }
        else { gGrpFirst = 0; gGrpCount = RAIONS[ri].count; selMem[ST_PLACE] = 1; st = ST_PLACE; }
        break;
      }

      case ST_LETTER: {
        int s = listMenu("Перша літера", gLetN + 1, letterLabel, selMem[ST_LETTER]);
        selMem[ST_LETTER] = s;
        if (s == 0) { st = skipRaion ? ST_OBL : ST_RAION; break; }
        gGrpFirst = gLet[s - 1].first;
        gGrpCount = gLet[s - 1].count;
        selMem[ST_PLACE] = 1;
        st = ST_PLACE;
        break;
      }

      case ST_PLACE: {
        int s = listMenu("Населений пункт", gGrpCount + 1, placeLabel, selMem[ST_PLACE], placeDetail);
        selMem[ST_PLACE] = s;
        if (s == 0) {
          st = useLetters ? ST_LETTER : (skipRaion ? ST_OBL : ST_RAION);
          break;
        }
        cp1251ToUtf8(recName(gGrpFirst + s - 1), placeName, sizeof placeName);
        recLatLon(PLACES + gOff[gGrpFirst + s - 1], pLat, pLon);
        fromSearch = false;
        if (!hasData) { st = ST_NOOUT; break; }
        if (src->nOps > 1) { gOpSrc = src; selMem[ST_OP] = 1; st = ST_OP; }
        else { opi = 0; st = ST_QUEUE; }
        break;
      }

      case ST_SEARCH: {
        if (!enterSearch(sq, sizeof sq)) { st = ST_OBL; break; }
        showMessage("Пошук...", nullptr, COL_TEXT);
        runSearch(sq);
        if (gSrN == 0) {
          showMessage3("Нічого не знайдено", sq, "Спробуйте іншу назву", COL_WARN);
          delay(2000);
          waitButtonsReleased();
          break;  // лишаємось у введенні, текст зберігається
        }
        selMem[ST_SRES] = 1;
        st = ST_SRES;
        break;
      }

      case ST_SRES: {
        char title[40];
        snprintf(title, sizeof title, "Знайдено: %d%s", gSrN, gSrN >= SRES_MAX ? "+" : "");
        int s = listMenu(title, gSrN + 1, srLabel, selMem[ST_SRES], srDetail);
        selMem[ST_SRES] = s;
        if (s == 0) { st = ST_SEARCH; break; }
        const SRes& r = gSr[s - 1];
        src = findSource(OBLS[r.obl].pcode);
        hasData = (src && src->nOps > 0);
        oi = r.obl;
        cp1251ToUtf8((const char*)(PLACES + r.off + PL_HDR), placeName, sizeof placeName);
        recLatLon(PLACES + r.off, pLat, pLon);
        fromSearch = true;
        if (!hasData) { st = ST_NOOUT; break; }
        if (src->nOps > 1) { gOpSrc = src; selMem[ST_OP] = 1; st = ST_OP; }
        else { opi = 0; st = ST_QUEUE; }
        break;
      }

      case ST_NOOUT: {  // для області немає відкритих даних: пристрій працюватиме як погодна станція
        showMessage3("Графіка відключень немає", src ? src->why1 : "Немає відкритого",
                     src ? src->why2 : "джерела даних", COL_WARN);
        delay(2200);
        waitButtonsReleased();
        int s = listMenu("Лише погода", 2, noOutageLabel, selMem[ST_NOOUT]);
        selMem[ST_NOOUT] = s;
        if (s == 1) { st = fromSearch ? ST_SRES : ST_PLACE; break; }
        memset(&out, 0, sizeof out);
        strlcpy(out.name, placeName, sizeof out.name);
        out.lat = pLat;
        out.lon = pLon;
        out.outage = false;
        out.valid = true;
        savePlace(prefs, out);
        if (gOff) { free(gOff); gOff = nullptr; }
        showMessage3("Збережено", placeName, "Показуватиму лише погоду", COL_OK);
        delay(1500);
        return true;
      }

      case ST_OP: {
        int s = listMenu("Оператор", src->nOps + 1, opLabel, selMem[ST_OP]);
        selMem[ST_OP] = s;
        if (s == 0) { st = fromSearch ? ST_SRES : ST_PLACE; break; }
        opi = s - 1;
        st = ST_QUEUE;
        break;
      }

      case ST_QUEUE: {
        showMessage("Завантаження черг...", placeName, COL_TEXT);
        bool fromSrc = fetchQueueListTask(src, opi);
        if (gQl.n == 0) {  // запасний варіант, якщо завдання не відповіло
          for (int a = 1; a <= 6; a++) for (int b = 1; b <= 2; b++) {
            char k[12];
            snprintf(k, sizeof k, (src->ops[opi].ep[0].fmt != FMT_GPV) ? "%d.%d" : "GPV%d.%d", a, b);
            queueAdd(gQl, k);
          }
        }
        if (!fromSrc) {
          showMessage3("Черги не отримано", "Показую стандартні черги", "1.1 ... 6.2", COL_WARN);
          delay(1800);
        }
        selMem[ST_QUEUE] = 1;
        int s = listMenu("Черга відключень", gQl.n + 1, queueLabel, selMem[ST_QUEUE]);
        if (s == 0) { st = (src->nOps > 1) ? ST_OP : (fromSearch ? ST_SRES : ST_PLACE); break; }
        memset(&out, 0, sizeof out);
        strlcpy(out.obl, OBLS[oi].pcode, sizeof out.obl);
        out.op = (uint8_t)opi;
        strlcpy(out.queue, gQl.key[s - 1], sizeof out.queue);
        strlcpy(out.name, placeName, sizeof out.name);
        out.lat = pLat;
        out.lon = pLon;
        out.outage = true;
        out.valid = true;
        savePlace(prefs, out);
        if (gOff) { free(gOff); gOff = nullptr; }
        showMessage3("Збережено", placeName, nullptr, COL_OK);
        delay(1500);
        return true;
      }
    }
  }
}

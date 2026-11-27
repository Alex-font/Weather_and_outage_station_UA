#pragma once
// Завантаження й розбір графіків відключень (два формати, див. sources.h), стан "зараз" та наступна подія.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "sources.h"

// ---------- Робота з клітинками графіка ----------
constexpr int SLOT_SEC = 1800;  // пів-години

// Стан у момент t (SL_NONE, якщо даних немає)
static uint8_t slotAt(const Schedule& s, time_t t) {
  for (int d = 0; d < s.nDays && d < SCH_DAYS; d++) {
    if (t >= s.dayTs[d] && t < s.dayTs[d] + 86400) {
      int i = (int)((t - s.dayTs[d]) / SLOT_SEC);
      if (i < 0 || i > 47) return SL_NONE;
      return s.slot[d][i];
    }
  }
  return SL_NONE;
}

static bool timeIsValid(time_t t) { return t > 1700000000L; }

// Прибирає доби, які цілком минули (застарілі дані). Повертає true, якщо щось видалено.
static bool pruneSchedule(Schedule& s, time_t now) {
  if (!timeIsValid(now)) return false;
  bool changed = false;
  int w = 0;
  for (int d = 0; d < s.nDays; d++) {
    if (s.dayTs[d] + 86400 <= now) {
      changed = true;
      continue;
    }
    if (w != d) {
      s.dayTs[w] = s.dayTs[d];
      memcpy(s.slot[w], s.slot[d], 48);
    }
    w++;
  }
  s.nDays = w;
  s.valid = (w > 0);
  return changed;
}

static uint32_t hashSchedule(const Schedule& s) {
  uint32_t h = 2166136261UL;
  for (int d = 0; d < s.nDays; d++) {
    uint32_t ts = (uint32_t)s.dayTs[d];
    for (int k = 0; k < 4; k++) { h ^= (ts >> (8 * k)) & 0xFF; h *= 16777619UL; }
    for (int i = 0; i < 48; i++) { h ^= s.slot[d][i]; h *= 16777619UL; }
  }
  return h;
}

// Додає добу (ts = початок доби), сортуючи за часом; зберігає лише останні SCH_DAYS діб.
static uint8_t* schedAddDay(Schedule& s, time_t ts) {
  for (int d = 0; d < s.nDays; d++) {  // така доба вже є: перезаписуємо
    if (s.dayTs[d] == ts) { memset(s.slot[d], SL_NONE, 48); return s.slot[d]; }
  }
  if (s.nDays >= SCH_DAYS) {
    // місця немає: прибираємо найстарішу, якщо нова новіша
    int oldest = 0;
    for (int d = 1; d < s.nDays; d++) if (s.dayTs[d] < s.dayTs[oldest]) oldest = d;
    if (ts < s.dayTs[oldest]) return nullptr;
    s.dayTs[oldest] = ts;
    memset(s.slot[oldest], SL_NONE, 48);
    // впорядкування нижче
    uint8_t* r = s.slot[oldest];
    for (int i = 0; i < s.nDays - 1; i++)
      for (int j = 0; j < s.nDays - 1 - i; j++)
        if (s.dayTs[j] > s.dayTs[j + 1]) {
          time_t tt = s.dayTs[j]; s.dayTs[j] = s.dayTs[j + 1]; s.dayTs[j + 1] = tt;
          uint8_t tmp[48];
          memcpy(tmp, s.slot[j], 48); memcpy(s.slot[j], s.slot[j + 1], 48); memcpy(s.slot[j + 1], tmp, 48);
        }
    for (int d = 0; d < s.nDays; d++) if (s.dayTs[d] == ts) return s.slot[d];
    return r;
  }
  int pos = s.nDays;
  while (pos > 0 && s.dayTs[pos - 1] > ts) {
    s.dayTs[pos] = s.dayTs[pos - 1];
    memcpy(s.slot[pos], s.slot[pos - 1], 48);
    pos--;
  }
  s.dayTs[pos] = ts;
  memset(s.slot[pos], SL_NONE, 48);
  s.nDays++;
  return s.slot[pos];
}

// ---------- HTTP ----------
// Повертає true, якщо JSON отримано й розібрано. У err лишається короткий опис помилки (UTF-8).
// Буферизоване читання відповіді: чекає на дані з мережі (TLS віддає їх шматками), пропускає BOM.
// Без цього ArduinoJson міг прочитати «кінець» посеред файлу й повернути IncompleteInput.
struct NetReader {
  Stream* c;
  uint8_t buf[512];
  size_t pos = 0, len = 0;
  bool first = true;
  uint32_t waitMs;
  NetReader(Stream* s, uint32_t w) : c(s), waitMs(w) {}
  bool refill() {
    uint32_t t0 = millis();
    for (;;) {
      int av = c->available();
      if (av > 0) {
        int n = c->readBytes(buf, av < (int)sizeof buf ? av : (int)sizeof buf);
        if (n > 0) {
          pos = 0; len = n;
          if (first) {
            first = false;
            if (len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) pos = 3;
          }
          return pos < len ? true : refill();
        }
      }
      if (millis() - t0 > waitMs) return false;
      delay(2);
    }
  }
  int read() {
    if (pos >= len && !refill()) return -1;
    return buf[pos++];
  }
  size_t readBytes(char* d, size_t n) {
    size_t k = 0;
    while (k < n) {
      int ch = read();
      if (ch < 0) break;
      d[k++] = (char)ch;
    }
    return k;
  }
};

static bool httpGetJson(const char* url, JsonDocument& doc, const JsonDocument& filter, char* err, size_t errN) {
  WiFiClientSecure client;
  client.setInsecure();  // шифрування є, але сертифікат сервера не перевіряється
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.useHTTP10(true);  // без chunked, щоб потік JSON читався коректно
  http.setUserAgent("Mozilla/5.0 (ESP32-C3 outage-monitor)");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    strlcpy(err, "Помилка запиту", errN);
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    if (code > 0) snprintf(err, errN, "Помилка HTTP %d", code);
    else strlcpy(err, "Немає зв'язку з сервером", errN);
    Serial.printf("HTTP error: %d (%s)\n", code, url);
    http.end();
    return false;
  }
  NetReader rd(http.getStreamPtr(), 8000);
  DeserializationError e = deserializeJson(doc, rd, DeserializationOption::Filter(filter));
  http.end();
  if (e) {
    snprintf(err, errN, "JSON: %s", e.c_str());
    Serial.printf("JSON error: %s\n", e.c_str());
    return false;
  }
  return true;
}

// ---------- Формат GPV (outage-data-ua) ----------
// Значення години: yes світло є, no немає, maybe можливо, first/second немає перші/другі 30 хв, mfirst/msecond можливо.
static void gpvHalves(const char* v, uint8_t& a, uint8_t& b) {
  a = b = SL_NONE;
  if (!v) return;
  if (!strcmp(v, "yes")) { a = SL_ON; b = SL_ON; }
  else if (!strcmp(v, "no")) { a = SL_OFF; b = SL_OFF; }
  else if (!strcmp(v, "maybe")) { a = SL_MAYBE; b = SL_MAYBE; }
  else if (!strcmp(v, "first")) { a = SL_OFF; b = SL_ON; }
  else if (!strcmp(v, "second")) { a = SL_ON; b = SL_OFF; }
  else if (!strcmp(v, "mfirst")) { a = SL_MAYBE; b = SL_ON; }
  else if (!strcmp(v, "msecond")) { a = SL_ON; b = SL_MAYBE; }
}

static bool parseGpv(const char* url, const char* queue, Schedule& out, char* err, size_t errN) {
  JsonDocument filter;
  filter["fact"]["data"]["*"][queue] = true;  // "*": будь-яка доба, далі лише потрібна черга
  JsonDocument doc;
  if (!httpGetJson(url, doc, filter, err, errN)) return false;

  JsonObject data = doc["fact"]["data"];
  if (data.isNull()) return true;  // дані відсутні (порожній графік)
  char hk[4];
  for (JsonPair kv : data) {
    time_t ts = (time_t)strtoul(kv.key().c_str(), nullptr, 10);
    if (ts < 1700000000L) continue;
    JsonObject q = kv.value()[queue];
    if (q.isNull()) continue;
    uint8_t* sl = schedAddDay(out, ts);
    if (!sl) continue;
    for (int h = 1; h <= 24; h++) {
      snprintf(hk, sizeof hk, "%d", h);
      uint8_t a, b;
      gpvHalves(q[hk] | (const char*)nullptr, a, b);
      sl[(h - 1) * 2] = a;
      sl[(h - 1) * 2 + 1] = b;
    }
  }
  return true;
}

// ---------- Формат YASNO ----------
// Дата "2026-11-21T00:00:00+02:00" -> unix-час
static long daysFromCivil(long y, unsigned m, unsigned d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned mp = (m > 2) ? (m - 3) : (m + 9);
  unsigned doy = (153 * mp + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

// Зсув часового поясу з рядка виду "...T00:00:00+02:00" (секунди; 0, якщо зсуву немає)
static long isoOffset(const char* s) {
  const char* p = s ? strchr(s, 'T') : nullptr;
  if (!p) return 0;
  p++;
  for (const char* q = p; *q; q++) {
    if (*q == 'Z') return 0;
    if (*q == '+' || (*q == '-' && q > p)) {
      int oh = 0, om = 0;
      sscanf(q + 1, "%d:%d", &oh, &om);
      long off = (long)(oh * 3600 + om * 60);
      return *q == '-' ? -off : off;
    }
  }
  return 0;
}

static time_t parseIsoDate(const char* s) {
  int Y, M, D, h = 0, mi = 0, se = 0;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &se) < 3) return 0;
  long off = isoOffset(s);
  return (time_t)(daysFromCivil(Y, M, D) * 86400L + h * 3600L + mi * 60L + se - off);
}

static bool parseYasno(const char* url, const char* queue, Schedule& out, char* err, size_t errN) {
  JsonDocument filter;
  filter[queue]["today"] = true;
  filter[queue]["tomorrow"] = true;
  JsonDocument doc;
  if (!httpGetJson(url, doc, filter, err, errN)) return false;

  JsonObject g = doc[queue];
  if (g.isNull()) return true;
  static const char* const DAYS[2] = {"today", "tomorrow"};
  for (int k = 0; k < 2; k++) {
    JsonObject d = g[DAYS[k]];
    if (d.isNull()) continue;
    const char* status = d["status"] | "ScheduleApplies";
    time_t ts = parseIsoDate(d["date"] | "");
    if (ts < 1700000000L) continue;
    if (strcmp(status, "ScheduleApplies") != 0) continue;  // графіка на цю добу ще немає або аварійні відключення
    uint8_t* sl = schedAddDay(out, ts);
    if (!sl) continue;
    memset(sl, SL_ON, 48);  // графік діє: поза відключеннями світло є
    JsonArray slots = d["slots"];
    for (JsonObject o : slots) {
      int st = o["start"] | 0, en = o["end"] | 0;
      const char* type = o["type"] | "Definite";
      uint8_t v = SL_OFF;
      if (!strcmp(type, "NotPlanned")) v = SL_ON;
      else if (strstr(type, "ossible") || strstr(type, "aybe")) v = SL_MAYBE;
      int c0 = st / 30, c1 = (en + 29) / 30;
      if (c0 < 0) c0 = 0;
      if (c1 > 48) c1 = 48;
      for (int c = c0; c < c1; c++) sl[c] = v;
    }
  }
  return true;
}

// ---------- Формат INTERVALS (mrkaktuz/outages-data) ----------
// Відрізки [s, e) з Unix-часом переносяться в клітинки двох діб (початки діб у dts[0], dts[1]).
static void intervalsApply(uint8_t slot[2][48], const time_t dts[2], time_t s, time_t e, uint8_t v, bool touched[2]) {
  for (int d = 0; d < 2; d++) {
    time_t a = s > dts[d] ? s : dts[d];
    time_t b = e < dts[d] + 86400 ? e : dts[d] + 86400;
    if (b <= a) continue;
    touched[d] = true;
    int c0 = (int)((a - dts[d]) / SLOT_SEC);
    int c1 = (int)((b - dts[d] + SLOT_SEC - 1) / SLOT_SEC);
    if (c1 > 48) c1 = 48;
    for (int c = c0; c < c1; c++) slot[d][c] = v;
  }
}

static bool parseIntervals(const char* url, const char* queue, Schedule& out, char* err, size_t errN) {
  JsonDocument filter;
  filter["updatedAt"] = true;
  filter["schedules"][queue]["intervals"] = true;
  JsonDocument doc;
  if (!httpGetJson(url, doc, filter, err, errN)) return false;

  time_t now = time(nullptr);
  if (!timeIsValid(now)) return true;  // без точного часу не знаємо, до якої доби належать відрізки
  JsonArray iv = doc["schedules"][queue]["intervals"];
  if (iv.isNull() || iv.size() == 0) return true;

  long off = isoOffset(doc["updatedAt"] | "");
  time_t d0 = (time_t)((((long)now + off) / 86400L) * 86400L - off);  // початок сьогоднішньої доби за часом даних
  time_t dts[2] = {d0, d0 + 86400};
  uint8_t slot[2][48];
  memset(slot, SL_ON, sizeof slot);
  bool touched[2] = {false, false};
  for (JsonObject o : iv) {
    time_t s = parseIsoDate(o["start"] | "");
    time_t e = parseIsoDate(o["end"] | "");
    if (s == 0 || e == 0) continue;
    const char* kind = o["kind"] | "off";
    uint8_t v = strcmp(kind, "possible") == 0 ? SL_MAYBE : SL_OFF;
    intervalsApply(slot, dts, s, e, v, touched);
  }
  // Доба вважається опублікованою, якщо в ній є відрізки (або вона між опублікованими)
  bool useToday = touched[0] || touched[1];
  for (int d = 0; d < 2; d++) {
    if (d == 0 ? !useToday : !touched[1]) continue;
    uint8_t* sl = schedAddDay(out, dts[d]);
    if (sl) memcpy(sl, slot[d], 48);
  }
  return true;
}

// ---------- Завантаження для вибраного населеного пункту ----------
enum FetchResult : uint8_t { FETCH_FAIL = 0, FETCH_OK = 1, FETCH_NODATA = 2 };

static FetchResult fetchSchedule(const PlaceCfg& pc, Schedule& out, char* err, size_t errN) {
  memset(&out, 0, sizeof out);
  err[0] = 0;
  const OblSource* src = findSource(pc.obl);
  if (!src || src->nOps == 0 || pc.op >= src->nOps) {
    strlcpy(err, "Немає джерела", errN);
    return FETCH_FAIL;
  }
  const Operator& op = src->ops[pc.op];
  bool anyOk = false;
  char e1[40] = "";
  for (int i = 0; i < EP_MAX; i++) {
    const Endpoint& ep = op.ep[i];
    if (!ep.url) continue;
    Schedule tmp;
    memset(&tmp, 0, sizeof tmp);
    bool ok = (ep.fmt == FMT_YASNO)      ? parseYasno(ep.url, pc.queue, tmp, e1, sizeof e1)
              : (ep.fmt == FMT_INTERVALS) ? parseIntervals(ep.url, pc.queue, tmp, e1, sizeof e1)
                                          : parseGpv(ep.url, pc.queue, tmp, e1, sizeof e1);
    if (!ok) {
      strlcpy(err, e1, errN);
      continue;  // пробуємо запасну адресу
    }
    anyOk = true;
    if (tmp.nDays > 0) {
      tmp.valid = true;
      out = tmp;
      out.hash = hashSchedule(out);
      err[0] = 0;
      return FETCH_OK;
    }
  }
  if (anyOk) {
    err[0] = 0;
    return FETCH_NODATA;
  }
  return FETCH_FAIL;
}

// ---------- Перелік черг для вибору ----------
constexpr int MAXQ = 32;
struct QueueList {
  int n;
  char key[MAXQ][12];
};

static void queueNum(const char* key, int& a, int& b) {
  const char* p = key;
  while (*p && !(*p >= '0' && *p <= '9')) p++;
  a = b = 0;
  sscanf(p, "%d.%d", &a, &b);
}

static void queueAdd(QueueList& ql, const char* key) {
  if (ql.n >= MAXQ || !key || !key[0] || strlen(key) >= sizeof ql.key[0]) return;
  if (!(strpbrk(key, "0123456789"))) return;
  for (int i = 0; i < ql.n; i++) if (!strcmp(ql.key[i], key)) return;
  strlcpy(ql.key[ql.n++], key, sizeof ql.key[0]);
}

static void queueSort(QueueList& ql) {
  for (int i = 1; i < ql.n; i++) {
    char t[12];
    strlcpy(t, ql.key[i], sizeof t);
    int ta, tb;
    queueNum(t, ta, tb);
    int j = i - 1;
    while (j >= 0) {
      int ja, jb;
      queueNum(ql.key[j], ja, jb);
      if (ja > ta || (ja == ta && jb > tb)) { strlcpy(ql.key[j + 1], ql.key[j], sizeof ql.key[0]); j--; }
      else break;
    }
    strlcpy(ql.key[j + 1], t, sizeof ql.key[0]);
  }
}

// Запитує в джерела, які черги в ньому є. Якщо не вдалось, пропонує стандартні 1.1 ... 6.2.
// Повертає true, якщо список отримано з джерела.
static bool fetchQueueList(const OblSource* src, int opIdx, QueueList& ql) {
  ql.n = 0;
  bool fromSource = false;
  char err[40];
  if (src && opIdx < src->nOps) {
    const Operator& op = src->ops[opIdx];
    for (int i = 0; i < EP_MAX && !fromSource; i++) {
      const Endpoint& ep = op.ep[i];
      if (!ep.url) continue;
      JsonDocument filter, doc;
      if (ep.fmt == FMT_YASNO) {
        filter["*"]["today"]["date"] = true;
        if (!httpGetJson(ep.url, doc, filter, err, sizeof err)) continue;
        for (JsonPair kv : doc.as<JsonObject>()) {
          if (kv.value().is<JsonObject>()) queueAdd(ql, kv.key().c_str());
        }
      } else if (ep.fmt == FMT_INTERVALS) {
        filter["groups"] = true;
        if (!httpGetJson(ep.url, doc, filter, err, sizeof err)) continue;
        for (JsonVariant g : doc["groups"].as<JsonArray>()) queueAdd(ql, g.as<const char*>());
      } else {
        filter["fact"]["data"]["*"]["*"]["1"] = true;  // достатньо назв черг: з кожної лише першу годину
        if (!httpGetJson(ep.url, doc, filter, err, sizeof err)) continue;
        JsonObject data = doc["fact"]["data"];
        for (JsonPair day : data) {
          for (JsonPair g : day.value().as<JsonObject>()) queueAdd(ql, g.key().c_str());
        }
      }
      fromSource = (ql.n > 0);
    }
  }
  if (!fromSource) {
    ql.n = 0;
    bool yasno = src && opIdx < src->nOps && src->ops[opIdx].ep[0].fmt != FMT_GPV;
    char k[12];
    for (int a = 1; a <= 6; a++) {
      for (int b = 1; b <= 2; b++) {
        snprintf(k, sizeof k, yasno ? "%d.%d" : "GPV%d.%d", a, b);
        queueAdd(ql, k);
      }
    }
  }
  queueSort(ql);
  return fromSource;
}

// "GPV3.1" -> "3.1"
static const char* queueShort(const char* key) {
  while (*key && !(*key >= '0' && *key <= '9')) key++;
  return key;
}

// ---------- Наступна подія ----------
struct NextEvent {
  bool found;    // знайдено зміну стану
  bool unknown;  // далі даних немає (t = початок невідомої ділянки)
  time_t t;
};

static NextEvent findNextChange(const Schedule& s, time_t now, uint8_t cur) {
  NextEvent e = {};
  time_t t = now - (now % SLOT_SEC) + SLOT_SEC;
  for (int k = 0; k < 96; k++, t += SLOT_SEC) {
    uint8_t st = slotAt(s, t);
    if (st == SL_NONE) {
      e.unknown = true;
      e.t = t;
      return e;
    }
    if (st != cur) {
      e.found = true;
      e.t = t;
      return e;
    }
  }
  return e;
}

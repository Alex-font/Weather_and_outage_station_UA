#pragma once
// Погода (OpenWeatherMap): поточний стан, прогноз опадів на 24 години, назви станів українською.
// Підключається з основного скетча після types.h, config.h, colors.h, lang.h.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static volatile int gWeatherHttp = 0;  // код останньої відповіді (200 ок, 401 хибний ключ, 0 немає зв'язку)

// ---------- Мережа ----------
static bool fetchJson(const char* url, JsonDocument& doc, const JsonDocument& filter) {
  WiFiClientSecure client;
  client.setInsecure();  // шифрування є, але сертифікат сервера не перевіряється
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.useHTTP10(true);  // без chunked, щоб потік JSON читався коректно
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  gWeatherHttp = code;
  if (code != HTTP_CODE_OK) {
    Serial.printf("Weather HTTP error: %d\n", code);  // URL не друкуємо: у ньому ключ
    http.end();
    return false;
  }
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("JSON error: %s\n", err.c_str());
    return false;
  }
  return true;
}

static bool fetchCurrent(double lat, double lon, Weather& w) {
  char url[220];
  snprintf(url, sizeof url,
           "https://api.openweathermap.org/data/2.5/weather?lat=%.4f&lon=%.4f&units=metric&appid=%s",
           lat, lon, OWM_API_KEY);

  JsonDocument filter;
  filter["main"]["temp"] = true;
  filter["main"]["humidity"] = true;
  filter["wind"]["speed"] = true;
  filter["wind"]["deg"] = true;
  filter["weather"][0]["id"] = true;
  filter["weather"][0]["icon"] = true;

  JsonDocument doc;
  if (!fetchJson(url, doc, filter)) return false;
  if (doc["main"]["temp"].isNull()) return false;

  w = {};
  w.temp     = doc["main"]["temp"].as<float>();
  w.humidity = doc["main"]["humidity"].as<int>();
  w.windMs   = doc["wind"]["speed"] | 0.0f;
  w.windDeg  = doc["wind"]["deg"] | -1;  // при штилі напрямку може не бути

  JsonArray conds = doc["weather"];  // може бути кілька станів одночасно (дощ і туман)
  for (JsonObject c : conds) {
    if (w.nCond >= 3) break;
    int id = c["id"] | 0;
    if (id <= 0) continue;
    if (w.nCond == 0) {
      const char* icon = c["icon"] | "";
      w.night = (strlen(icon) >= 3 && icon[2] == 'n');
    }
    w.condId[w.nCond++] = (uint16_t)id;
  }

  w.valid = true;
  w.okMs = millis();
  Serial.printf("Weather OK: %.1f C, humidity %d%%\n", w.temp, w.humidity);
  return true;
}

// Код WMO (Open-Meteo) у код стану OpenWeatherMap, за яким працюють condText і решта програми
static uint16_t wmoToId(int c) {
  switch (c) {
    case 0: return 800;
    case 1: return 801;
    case 2: return 802;
    case 3: return 804;
    case 45: case 48: return 741;
    case 51: case 53: case 55: case 56: case 57: return 300;
    case 61: return 500;
    case 63: return 501;
    case 65: return 502;
    case 66: case 67: return 511;
    case 71: case 77: return 600;
    case 73: return 601;
    case 75: return 602;
    case 80: return 520;
    case 81: return 521;
    case 82: return 522;
    case 85: return 620;
    case 86: return 622;
    case 95: return 211;
    case 96: case 99: return 202;
  }
  return 804;
}

// Open-Meteo (ключ не потрібен): один запит дає і поточну погоду (w), і прогноз опадів по ГОДИНАХ (p).
// Безкоштовний прогноз OpenWeatherMap має лише 3-годинний крок. Будь-який з w, p можна передати nullptr.
// Ймовірність у записі прогнозу стосується попередньої години, тому dt це кінець години.
static bool fetchOpenMeteo(double lat, double lon, Weather* w, Precip* p) {
  char url[420];
  int n = snprintf(url, sizeof url, "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f", lat, lon);
  if (w) {
    n += snprintf(url + n, sizeof url - n,
                  "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m,is_day"
                  "&wind_speed_unit=ms");
  }
  if (p) {
    n += snprintf(url + n, sizeof url - n,
                  "&hourly=precipitation_probability,precipitation,snowfall,temperature_2m&forecast_hours=%d", FC_MAX);
  }
  snprintf(url + n, sizeof url - n, "&timeformat=unixtime&timezone=GMT");

  JsonDocument filter;
  if (w) {
    JsonObject c = filter["current"].to<JsonObject>();
    c["temperature_2m"] = true;
    c["relative_humidity_2m"] = true;
    c["weather_code"] = true;
    c["wind_speed_10m"] = true;
    c["wind_direction_10m"] = true;
    c["is_day"] = true;
  }
  if (p) {
    JsonObject h = filter["hourly"].to<JsonObject>();
    h["time"] = true;
    h["precipitation_probability"] = true;
    h["precipitation"] = true;
    h["snowfall"] = true;
    h["temperature_2m"] = true;
  }

  JsonDocument doc;
  if (!fetchJson(url, doc, filter)) return false;

  if (w) {
    JsonObject c = doc["current"];
    if (c.isNull() || c["temperature_2m"].isNull()) return false;
    *w = {};
    w->temp = c["temperature_2m"].as<float>();
    w->humidity = c["relative_humidity_2m"] | 0;
    w->windMs = c["wind_speed_10m"] | 0.0f;
    w->windDeg = (w->windMs < 0.5f) ? -1 : (int)(c["wind_direction_10m"] | -1);  // штиль: напрямку немає
    w->nCond = 1;
    w->condId[0] = wmoToId(c["weather_code"] | 3);
    w->night = ((c["is_day"] | 1) == 0);
    w->valid = true;
    w->okMs = millis();
    Serial.printf("Weather OK: %.1f C, humidity %d%%\n", w->temp, w->humidity);
  }

  if (p) {
    JsonArray tm = doc["hourly"]["time"];
    JsonArray pr = doc["hourly"]["precipitation_probability"];
    JsonArray am = doc["hourly"]["precipitation"];
    JsonArray sn = doc["hourly"]["snowfall"];
    JsonArray tp = doc["hourly"]["temperature_2m"];
    if (tm.isNull() || tm.size() == 0) return false;
    *p = {};
    for (size_t i = 0; i < tm.size() && p->n < FC_MAX; i++) {
      int pop = pr[i] | 0;  // null означає "немає даних"
      float rain = am[i] | 0.0f;
      float snow = sn[i] | 0.0f;
      float tmp = tp[i] | 10.0f;

      bool isSnow = snow > 0;
      bool isRain = rain > 0 && tmp > 0.5f;  // кількість опадів включає й сніг, тому дощ лише при плюсовій температурі
      if (!isSnow && !isRain) {  // ймовірність є, а кількості ще нема: тип за температурою
        if (tmp <= 0.5f) isSnow = true; else isRain = true;
      }
      p->dt[p->n] = (time_t)tm[i].as<uint32_t>();
      p->pop[p->n] = (uint8_t)(pop < 0 ? 0 : (pop > 100 ? 100 : pop));
      p->type[p->n] = (isRain && isSnow) ? 3 : (isSnow ? 2 : 1);
      p->n++;
    }
    p->valid = (p->n > 0);
    p->okMs = millis();
    if (!p->valid) return false;
  }
  return true;
}

// ---------- Назви станів ----------
static const char* condText(int id, bool night, uint16_t& color) {
  if (id >= 200 && id < 300) { color = COL_STORM; return "Гроза"; }
  if (id >= 300 && id < 400) { color = COL_RAIN; return "Мряка"; }
  if (id >= 500 && id < 600) {
    color = COL_RAIN;
    switch (id) {
      case 500: return "Слабкий дощ";
      case 502: case 503: case 504: return "Сильний дощ";
      case 511: return "Крижаний дощ";
      case 520: return "Дощик";  // слабка злива
      case 522: return "Сильна злива";
      case 521: case 531: return "Злива";
      default: return "Дощ";
    }
  }
  if (id >= 600 && id < 700) {
    color = COL_SNOW;
    switch (id) {
      case 600: return "Слабкий сніг";
      case 602: return "Сильний сніг";
      case 611: case 612: case 613: return "Мокрий сніг";
      case 615: case 616: return "Дощ зі снігом";
      case 620: case 621: case 622: return "Снігопад";
      default: return "Сніг";
    }
  }
  if (id >= 700 && id < 800) {
    color = COL_FOG;
    switch (id) {
      case 701: return "Димка";
      case 711: return "Дим";
      case 721: return "Імла";
      case 731: case 761: return "Пил";
      case 741: return "Туман";
      case 751: return "Пісок";
      case 762: return "Попіл";
      case 771: return "Шквал";
      case 781: return "Торнадо";
      default: return "Імла";
    }
  }
  if (id == 800) {
    color = night ? COL_TEXT : COL_SUN;
    return night ? "Ясно" : "Сонячно";
  }
  color = COL_CLOUD;
  if (id == 801 || id == 802) return "Мінлива хмарність";
  if (id == 803) return "Хмарно";
  return "Пасмурно";
}

// Підсумок прогнозу рахується від поточного часу: години, що вже минули, ігноруються
static FcSummary summarizeForecast(const Precip& p, time_t now) {
  FcSummary s = {};
  for (int i = 0; i < p.n; i++) {
    if (p.dt[i] <= now) continue;  // година закінчилась
    if (!s.any && p.pop[i] >= POP_THRESHOLD) {
      s.any = true;
      s.type = p.type[i];
      s.when = p.dt[i] - 3600;  // початок години
      s.popAt = p.pop[i];
    }
  }
  return s;
}

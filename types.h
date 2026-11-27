#pragma once
// Типи даних винесені в окремий файл, бо автоматичні прототипи Arduino IDE
// вставляються вище за оголошення struct у .ino і ламали б компіляцію.

#include <Arduino.h>
#include <time.h>
#include <Adafruit_GFX.h>

// Поле тексту: перемальовується тільки при зміні тексту/кольору,
// стирає лише свою область (без мерехтіння всього екрана).
struct Field {
  int16_t x, y;            // позиція (базова лінія тексту)
  const GFXfont* font;
  char last[96];
  uint16_t lastColor;
  int16_t bx, by, bw, bh;  // область, яку займав попередній текст
  bool drawn;
  bool fixed;              // true: стирати завжди заздалегідь задану область (bx,by,bw,bh)
};

// ---------- Графік відключень ----------
// Графік ділиться на пів-години: 48 клітинок на добу. Зберігаємо до трьох діб.
enum SlotState : uint8_t { SL_NONE = 0, SL_ON = 1, SL_OFF = 2, SL_MAYBE = 3 };
// SL_NONE  немає даних
// SL_ON    світло є
// SL_OFF   світла немає
// SL_MAYBE можливе відключення

constexpr int SCH_DAYS = 3;
struct Schedule {
  bool valid;                  // є хоча б одна доба
  uint8_t nDays;
  time_t dayTs[SCH_DAYS];      // початок доби (unix, 00:00 за Києвом)
  uint8_t slot[SCH_DAYS][48];  // SlotState для кожної пів-години
  uint32_t hash;               // для виявлення змін графіка
};

// Вибраний користувачем населений пункт: координати для погоди, область і черга для графіка
struct PlaceCfg {
  bool valid;
  bool outage;      // для області є графік відключень і вибрано чергу
  char obl[8];      // код області (UA12)
  uint8_t op;       // номер оператора в області
  char queue[12];   // ключ черги в даних (GPV3.1 або 3.1)
  char name[48];    // назва населеного пункту (UTF-8) для екрана
  float lat, lon;   // координати для погоди
};

// ---------- Дані погоди ----------
struct Weather {
  bool valid;
  float temp, windMs;
  int humidity, windDeg;
  uint8_t nCond;        // скільки кодів стану погоди (1..3)
  uint16_t condId[3];   // коди OpenWeather: 800 ясно, 5xx дощ, 6xx сніг, 7xx туман...
  bool night;           // за піктограмою: вночі "Clear", вдень "Sunny"
  uint32_t okMs;        // millis() останнього успішного оновлення
};

// Прогноз опадів зберігається по годинах (24 години вперед), а підсумок рахується під час малювання,
// щоб година, яка вже минула, одразу зникала з екрана.
constexpr int FC_MAX = 24;
struct Precip {
  bool valid;
  int n;
  time_t dt[FC_MAX];     // кінець години, до якої належить ймовірність
  uint8_t pop[FC_MAX];   // ймовірність опадів, %
  uint8_t type[FC_MAX];  // 1 дощ, 2 сніг, 3 мокрий сніг
  uint32_t okMs;
};

struct FcSummary {
  bool any;        // є опади вище порогу
  uint8_t type;
  time_t when;     // початок години, з якої очікуються опади (може бути в минулому: це "зараз")
  int popAt;       // ймовірність у цьому кроці, %
};

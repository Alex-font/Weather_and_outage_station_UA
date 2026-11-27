#pragma once
// Джерела графіків відключень для кожної області.
//
// Кожна область має одного або кількох операторів (наприклад, на Дніпропетровщині це ДТЕК і ЦЕК),
// а в кожного оператора до трьох адрес: основна й запасні (пробуються по черзі, якщо попередня не відповіла).
//
// Формати даних:
//   FMT_GPV    JSON у форматі проєкту outage-data-ua: fact.data[<доба>]["GPV3.1"]["1".."24"] = yes/no/maybe/first/second/mfirst/msecond
//   FMT_YASNO  API YASNO: {"3.1": {"today": {"slots": [{"start":мин,"end":мин,"type":"Definite"}], "date": "...", "status": "..."}, "tomorrow": {...}}}
//   FMT_INTERVALS  репозиторій mrkaktuz/outages-data: {"groups": ["1.1",...], "schedules": {"1.1": {"intervals": [{"start": ISO, "end": ISO, "kind": "off"|"possible"}]}}}
//
// Щоб додати область чи змінити адресу, достатньо змінити таблицю SOURCES нижче.
// Області без запису (або з nOps = 0) у меню позначені, а після вибору показують причину.
//
// ВАЖЛИВО: це відкриті дзеркала на GitHub, які підтримують сторонні люди. Адреси можуть змінитись,
// тому при помилці на екрані з'являється код (наприклад "Помилка HTTP 404"): це ознака, що адресу треба оновити.

#include <stdint.h>

enum FmtType : uint8_t { FMT_NONE = 0, FMT_GPV = 1, FMT_YASNO = 2, FMT_INTERVALS = 3 };

constexpr int EP_MAX = 3;  // адрес на оператора

struct Endpoint {
  const char* url;
  uint8_t fmt;
};

struct Operator {
  const char* name;      // для екрана (UTF-8)
  Endpoint ep[EP_MAX];   // основна й запасні адреси
};

struct OblSource {
  const char* pcode;  // код області з бази населених пунктів (UA12)
  uint8_t nOps;       // скільки операторів (0 = джерела немає)
  Operator ops[2];
  const char* why1;   // причина відсутності (два рядки на екран)
  const char* why2;
};

// Бази адрес
#define SRC_BASKERVILLE "https://raw.githubusercontent.com/Baskerville42/outage-data-ua/main/data/"
#define SRC_OE "https://raw.githubusercontent.com/yaroslav2901/OE_OUTAGE_DATA/main/data/"
#define SRC_IF "https://raw.githubusercontent.com/samokisha/outage-data-if/main/data/"
#define SRC_VOE "https://raw.githubusercontent.com/olnet93/gpv-voe-vinnytsia/main/data/"
#define SRC_VOE2 "https://raw.githubusercontent.com/vn-progr/gpv-voe-vinnytsia/main/data/"
#define SRC_POLTAVA "https://raw.githubusercontent.com/findme88/poltava-outage-data/main/data/"
#define SRC_ZOE "https://raw.githubusercontent.com/milkmann/gpv-zoe-zaporizhzhia/main/data/"
#define SRC_MRKAKTUZ "https://raw.githubusercontent.com/mrkaktuz/outages-data/data/"
#define SRC_YASNO "https://app.yasno.ua/api/blackout-service/public/shutdowns/regions/"

#define NO_EP {nullptr, FMT_NONE}

static const OblSource SOURCES[] = {
  // --- Є відкриті дані ---
  // Київ: YASNO, запасні дзеркала ДТЕК
  {"UA80", 1, {{"ДТЕК Київські ЕМ", {{SRC_YASNO "25/dsos/902/planned-outages", FMT_YASNO},
                                       {SRC_BASKERVILLE "kyiv.json", FMT_GPV},
                                       {SRC_MRKAKTUZ "dtek-kem.json", FMT_INTERVALS}}}}, nullptr, nullptr},
  {"UA32", 1, {{"ДТЕК Київські регіон. ЕМ", {{SRC_BASKERVILLE "kyiv-region.json", FMT_GPV},
                                              {SRC_MRKAKTUZ "dtek-krem.json", FMT_INTERVALS}, NO_EP}}}, nullptr, nullptr},
  {"UA12", 2, {{"ДТЕК Дніпровські ЕМ", {{SRC_YASNO "3/dsos/301/planned-outages", FMT_YASNO},
                                         {SRC_BASKERVILLE "dnipro.json", FMT_GPV},
                                         {SRC_MRKAKTUZ "dtek-dnem.json", FMT_INTERVALS}}},
               {"ЦЕК (Центральна енергетична)", {{SRC_YASNO "3/dsos/303/planned-outages", FMT_YASNO}, NO_EP, NO_EP}}}, nullptr, nullptr},
  {"UA51", 1, {{"ДТЕК Одеські ЕМ", {{SRC_BASKERVILLE "odesa.json", FMT_GPV},
                                     {SRC_MRKAKTUZ "dtek-oem.json", FMT_INTERVALS}, NO_EP}}}, nullptr, nullptr},
  {"UA26", 1, {{"Прикарпаттяобленерго", {{SRC_IF "ivano-frankivsk.json", FMT_GPV},
                                          {SRC_OE "Prykarpattiaoblenerho.json", FMT_GPV}, NO_EP}}}, nullptr, nullptr},
  {"UA68", 1, {{"Хмельницькобленерго", {{SRC_OE "Khmelnytskoblenerho.json", FMT_GPV}, NO_EP, NO_EP}}}, nullptr, nullptr},
  {"UA21", 1, {{"Закарпаттяобленерго", {{SRC_OE "Zakarpattiaoblenerho.json", FMT_GPV}, NO_EP, NO_EP}}}, nullptr, nullptr},
  {"UA46", 1, {{"Львівобленерго", {{SRC_OE "Lvivoblenerho.json", FMT_GPV}, NO_EP, NO_EP}}}, nullptr, nullptr},
  {"UA61", 1, {{"Тернопільобленерго", {{SRC_OE "Ternopiloblenerho.json", FMT_GPV}, NO_EP, NO_EP}}}, nullptr, nullptr},
  {"UA74", 1, {{"Чернігівобленерго", {{SRC_OE "Chernihivoblenergo.json", FMT_GPV}, NO_EP, NO_EP}}}, nullptr, nullptr},
  {"UA23", 1, {{"Запоріжжяобленерго", {{SRC_OE "Zaporizhzhiaoblenergo.json", FMT_GPV},
                                        {SRC_ZOE "Zaporizhzhiaoblenergo.json", FMT_GPV}, NO_EP}}}, nullptr, nullptr},
  {"UA18", 1, {{"Житомиробленерго", {{SRC_OE "Zhytomyroblenergo.json", FMT_GPV},
                                      {SRC_MRKAKTUZ "ztoe.json", FMT_INTERVALS}, NO_EP}}}, nullptr, nullptr},
  {"UA53", 1, {{"Полтаваобленерго", {{SRC_POLTAVA "Poltavaoblenergo.json", FMT_GPV},
                                      {SRC_OE "Poltavaoblenergo.json", FMT_GPV}, NO_EP}}}, nullptr, nullptr},
  {"UA56", 1, {{"Рівнеобленерго", {{SRC_OE "Rivneoblenergo.json", FMT_GPV}, NO_EP, NO_EP}}}, nullptr, nullptr},
  {"UA05", 1, {{"Вінницяобленерго", {{SRC_VOE "Vinnytsiaoblenerho.json", FMT_GPV},
                                           {SRC_VOE2 "Vinnytsiaoblenerho.json", FMT_GPV}, NO_EP}}}, nullptr, nullptr},
  {"UA48", 1, {{"Миколаївобленерго", {{SRC_MRKAKTUZ "mykolaiv.json", FMT_INTERVALS}, NO_EP, NO_EP}}}, nullptr, nullptr},
  // Імена файлів Черкащини й Харківщини звірено з проєктами ha-svitlo-yeah та UA-power-outages-monitor (Харківщина: "oblenerho")
  {"UA71", 1, {{"Черкасиобленерго", {{SRC_OE "Cherkasyoblenergo.json", FMT_GPV}, NO_EP, NO_EP}}}, nullptr, nullptr},
  {"UA63", 1, {{"Харківобленерго", {{SRC_OE "Kharkivoblenerho.json", FMT_GPV}, NO_EP, NO_EP}}}, nullptr, nullptr},

  // --- Немає відкритих машиночитних даних ---
  {"UA01", 0, {}, "Тимчасово окупована", "територія"},
  {"UA85", 0, {}, "Тимчасово окупована", "територія"},
  {"UA44", 0, {}, "Оператор не публікує", "погодинних графіків"},
  {"UA14", 0, {}, "Оператор не публікує", "погодинних графіків"},
  {"UA65", 0, {}, "Графік лише в кабінеті", "клієнта оператора"},
  {"UA07", 0, {}, "Графік лише картинкою", "на сайті оператора"},
  {"UA35", 0, {}, "Графік лише в Telegram", "оператора"},
  {"UA59", 0, {}, "Графік лише на сайті", "оператора (потрібен вхід)"},
  {"UA73", 0, {}, "Немає надійного", "джерела даних"},
};
constexpr int SOURCES_N = sizeof(SOURCES) / sizeof(SOURCES[0]);

static const OblSource* findSource(const char* pcode) {
  for (int i = 0; i < SOURCES_N; i++) {
    if (!strcmp(SOURCES[i].pcode, pcode)) return &SOURCES[i];
  }
  return nullptr;
}

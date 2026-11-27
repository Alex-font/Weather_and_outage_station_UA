#pragma once
// Список часових поясів для майстра налаштування (від заходу до сходу).
// name/nameUa: те, що видно на екрані (до ~22 символів), posix: рядок TZ з правилами літнього часу.
// Щоб додати свій пояс, допишіть рядок у масив.

struct TzItem {
  const char* name;    // англійською
  const char* nameUa;  // українською
  const char* posix;
};

static const TzItem TIMEZONES[] = {
  {"Honolulu (UTC-10)", "Гонолулу (UTC-10)", "HST10"},
  {"Anchorage (UTC-9)", "Анкоридж (UTC-9)", "AKST9AKDT,M3.2.0,M11.1.0"},
  {"Los Angeles (UTC-8)", "Лос-Анджелес (UTC-8)", "PST8PDT,M3.2.0,M11.1.0"},
  {"Denver (UTC-7)", "Денвер (UTC-7)", "MST7MDT,M3.2.0,M11.1.0"},
  {"Phoenix (UTC-7)", "Фінікс (UTC-7)", "MST7"},
  {"Chicago (UTC-6)", "Чикаго (UTC-6)", "CST6CDT,M3.2.0,M11.1.0"},
  {"Mexico City (UTC-6)", "Мехіко (UTC-6)", "CST6"},
  {"New York (UTC-5)", "Нью-Йорк (UTC-5)", "EST5EDT,M3.2.0,M11.1.0"},
  {"Halifax (UTC-4)", "Галіфакс (UTC-4)", "AST4ADT,M3.2.0,M11.1.0"},
  {"St. John's (UTC-3:30)", "Сент-Джонс (UTC-3:30)", "NST3:30NDT,M3.2.0,M11.1.0"},
  {"Sao Paulo (UTC-3)", "Сан-Паулу (UTC-3)", "<-03>3"},
  {"Reykjavik (UTC+0)", "Рейк'явік (UTC+0)", "GMT0"},
  {"UTC", "UTC", "UTC0"},
  {"London/Dublin (UTC+0)", "Лондон/Дублін (UTC+0)", "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Lisbon (UTC+0)", "Лісабон (UTC+0)", "WET0WEST,M3.5.0/1,M10.5.0"},
  {"Casablanca (UTC+1)", "Касабланка (UTC+1)", "<+01>-1"},
  {"Lagos (UTC+1)", "Лагос (UTC+1)", "WAT-1"},
  {"Central Europe (UTC+1)", "Центр. Європа (UTC+1)", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Kyiv/Helsinki (UTC+2)", "Київ/Гельсінкі (UTC+2)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"Chisinau (UTC+2)", "Кишинів (UTC+2)", "EET-2EEST,M3.5.0,M10.5.0/3"},
  {"Cairo (UTC+2)", "Каїр (UTC+2)", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
  {"Jerusalem (UTC+2)", "Єрусалим (UTC+2)", "IST-2IDT,M3.4.4/26,M10.5.0"},
  {"Johannesburg (UTC+2)", "Йоганнесбург (UTC+2)", "SAST-2"},
  {"Istanbul (UTC+3)", "Стамбул (UTC+3)", "<+03>-3"},
  {"Moscow/Minsk (UTC+3)", "Москва/Мінськ (UTC+3)", "MSK-3"},
  {"Riyadh/Baghdad (UTC+3)", "Ер-Ріяд/Багдад (UTC+3)", "<+03>-3"},
  {"Nairobi (UTC+3)", "Найробі (UTC+3)", "EAT-3"},
  {"Tehran (UTC+3:30)", "Тегеран (UTC+3:30)", "<+0330>-3:30"},
  {"Dubai/Baku (UTC+4)", "Дубай/Баку (UTC+4)", "<+04>-4"},
  {"Tashkent/Astana (UTC+5)", "Ташкент/Астана (UTC+5)", "<+05>-5"},
  {"Karachi (UTC+5)", "Карачі (UTC+5)", "PKT-5"},
  {"Delhi (UTC+5:30)", "Делі (UTC+5:30)", "IST-5:30"},
  {"Kathmandu (UTC+5:45)", "Катманду (UTC+5:45)", "<+0545>-5:45"},
  {"Dhaka (UTC+6)", "Дака (UTC+6)", "<+06>-6"},
  {"Bangkok/Jakarta (UTC+7)", "Бангкок/Джакарта (+7)", "<+07>-7"},
  {"Beijing/Singapore (+8)", "Пекін/Сінгапур (+8)", "CST-8"},
  {"Tokyo/Seoul (UTC+9)", "Токіо/Сеул (UTC+9)", "JST-9"},
  {"Brisbane (UTC+10)", "Брисбен (UTC+10)", "AEST-10"},
  {"Sydney (UTC+10/+11)", "Сідней (UTC+10/+11)", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {"Auckland (UTC+12/+13)", "Окленд (UTC+12/+13)", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};

constexpr int TZ_COUNT = sizeof(TIMEZONES) / sizeof(TIMEZONES[0]);

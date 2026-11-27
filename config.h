#pragma once
// Налаштування пристрою: піни, інтервали, джерело погоди.
// Wi-Fi, часовий пояс, населений пункт і черга вводяться кнопками на екрані
// й зберігаються в пам'яті плати, тому тут їх вказувати не потрібно.

#include <Arduino.h>
#include <SPI.h>

// ===== Джерело погоди =====
// За замовчуванням і поточна погода, і прогноз опадів по годинах беруться з Open-Meteo: ключ не потрібен.
// За бажання поточну погоду можна брати з OpenWeatherMap (https://openweathermap.org, безкоштовний ключ):
// поставте true і впишіть ключ. Прогноз опадів по годинах усе одно йде з Open-Meteo.
constexpr bool USE_OPENWEATHERMAP = false;
constexpr const char* OWM_API_KEY = "YOUR_API_KEY";

// ===== Wi-Fi =====
// Якщо плата C3 Super Mini погано підключається до Wi-Fi, поставте true
// (знижує потужність передавача, це відомий фікс для деяких плат).
constexpr bool WIFI_LOW_TX_POWER = false;

// ===== Часовий пояс =====
// Лише підсвічується у списку за замовчуванням (Київ).
constexpr const char* DEFAULT_TZ_INFO = "EET-2EEST,M3.5.0/3,M10.5.0/4";

// ===== Прогноз опадів =====
// Показувати опади, якщо ймовірність у прогнозі на годину >= цього значення (%)
constexpr int POP_THRESHOLD = 20;

// ===== Інтервали =====
constexpr uint32_t WEATHER_INTERVAL_MS  = 10UL * 60UL * 1000UL;  // оновлення погоди
constexpr uint32_t SCHEDULE_INTERVAL_MS = 10UL * 60UL * 1000UL;  // перевірка змін графіка відключень
constexpr uint32_t RETRY_INTERVAL_MS    = 30UL * 1000UL;         // повтор після помилки
constexpr uint32_t STALE_AFTER_MS       = 30UL * 60UL * 1000UL;  // дані "застаріли" (сірі / жовтий індикатор)
constexpr uint32_t CHANGED_SHOW_MS      = 3UL * 60UL * 1000UL;   // скільки показувати "Графік оновлено"
constexpr uint32_t MANUAL_REFRESH_GAP_MS = 15UL * 1000UL;        // не частіше за раз на 15 с вручну

// ===== Екран 2.8" TFT SPI 240x320 (GMT028-05), ESP32-C3 Super Mini =====
// Уникайте GPIO2, 8, 9 (strapping) та 20, 21 (UART).
constexpr int8_t PIN_TFT_SCK  = 4;   // SCK (на екрані підписано SCK)
constexpr int8_t PIN_TFT_MOSI = 6;   // SDA (це лінія даних SPI, не I2C!)
constexpr int8_t PIN_TFT_DC   = 5;   // DC
constexpr int8_t PIN_TFT_RST  = 3;   // RST
constexpr int8_t PIN_TFT_CS   = 7;   // CS
// На модулі з 7 виводами піна підсвітки немає (підсвітка завжди ввімкнена), тому -1:
// тоді яскравість регулюється програмно (затемнення кольорів). Якщо ви допаяли керування
// підсвіткою через транзистор до якогось піна (наприклад, GPIO20), впишіть його номер сюди:
// тоді яскравість регулюватиметься справжнім ШІМ підсвітки.
constexpr int8_t PIN_TFT_BL   = -1;
constexpr bool TFT_BL_ACTIVE_HIGH = true;  // false, якщо підсвітка вмикається низьким рівнем (рідко)

// Контролер екрана: 0 = ST7789 (найчастіше для 7-контактних 2.8"), 1 = ILI9341.
// Якщо екран білий або показує сміття, спробуйте змінити.
#define TFT_DRIVER_ILI9341 0

// Модулі з CS працюють у SPI_MODE0. Якщо екран білий/порожній, спробуйте SPI_MODE2 або SPI_MODE3.
constexpr uint8_t TFT_SPI_MODE = SPI_MODE0;
constexpr uint32_t TFT_SPI_HZ = 20000000;  // швидкість SPI; на довгих дротах 40 МГц може давати збої й миготіння всього екрана
constexpr uint8_t TFT_ROTATION = 2;        // 2 = портрет, роз'єм екрана вгорі (перевірено на вашому екрані); догори дном: поставте 0
// Інверсія кольорів: -1 залишити як ініціалізує бібліотека, 0 вимкнути, 1 увімкнути.
// Якщо кольори "негативні" (білий чорний, червоний блакитний), змініть 0 на 1 або навпаки.
constexpr int8_t TFT_INVERT = 0;

// ===== Кнопки (одним виводом на GND, використовується внутрішня підтяжка) =====
// Не використовуйте GPIO9 (кнопка BOOT) і GPIO2, 8 (strapping).
constexpr int8_t PIN_BTN_L = 0;    // ліва: вгору / назад / попередній символ
constexpr int8_t PIN_BTN_C = 1;    // центральна: підтвердження / меню
constexpr int8_t PIN_BTN_R = 10;   // права: вниз / далі / наступний символ
constexpr uint32_t BTN_DEBOUNCE_MS = 30;   // усунення брязкоту
constexpr uint32_t BTN_REPEAT_DELAY_MS = 450;  // скільки утримувати ліву/праву, щоб почалось автоповторення
constexpr uint32_t BTN_REPEAT_MS = 110;        // період автоповторення
constexpr uint32_t BTN_REPEAT_FAST_MS = 45;    // період після 2 с утримання
constexpr uint32_t BTN_LONG_MS = 1000;         // утримання лівої/правої під час введення тексту: ВЕЛИКА літера
constexpr uint32_t BL_ADJUST_TIMEOUT_MS = 30000;  // вихід з регулювання яскравості, якщо кнопки не чіпають

// ===== Довгі назви (рядок, що біжить) =====
constexpr uint32_t MARQUEE_HOLD_MS = 900;   // пауза на початку й наприкінці
constexpr uint32_t MARQUEE_STEP_MS = 45;    // крок прокрутки (2 пікселі)

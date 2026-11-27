#pragma once
// Кольори (RGB565) і програмне регулювання яскравості.
// Модуль екрана без піна підсвітки: яскравість змінюється затемненням усіх кольорів (gDim 0..256).
// Усі COL_* розкриваються у вираз dim565(...), тому зміна яскравості діє на весь інтерфейс одразу.

#include <Arduino.h>

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static int gDim = 256;  // 256 = повна яскравість

static inline uint16_t dim565(uint16_t c) {
  if (gDim >= 256 || c == 0) return c;
  uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  r = (r * (uint32_t)gDim) >> 8;
  g = (g * (uint32_t)gDim) >> 8;
  b = (b * (uint32_t)gDim) >> 8;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

constexpr uint16_t BASE_LINE  = rgb565(70, 70, 80);
constexpr uint16_t BASE_TIME  = rgb565(255, 90, 200);
constexpr uint16_t BASE_DAY   = rgb565(255, 255, 255);
constexpr uint16_t BASE_TEXT  = rgb565(200, 200, 210);
constexpr uint16_t BASE_DIM   = rgb565(130, 130, 140);
constexpr uint16_t BASE_FEELS = rgb565(255, 220, 40);   // жовтий
constexpr uint16_t BASE_TEMP  = rgb565(255, 60, 60);
constexpr uint16_t BASE_HUM   = rgb565(0, 220, 255);
constexpr uint16_t BASE_WIND  = rgb565(60, 235, 90);
constexpr uint16_t BASE_AZ    = rgb565(255, 150, 30);   // азимут вітру: інший колір, щоб не зливався зі швидкістю
constexpr uint16_t BASE_RAIN  = rgb565(80, 160, 255);
constexpr uint16_t BASE_SNOW  = rgb565(200, 240, 255);
constexpr uint16_t BASE_SLEET = rgb565(170, 150, 255);
constexpr uint16_t BASE_OK    = rgb565(60, 225, 90);
constexpr uint16_t BASE_WARN  = rgb565(255, 175, 0);
constexpr uint16_t BASE_BAD   = rgb565(255, 70, 70);
constexpr uint16_t BASE_SUN   = rgb565(255, 200, 0);
constexpr uint16_t BASE_CLOUD = rgb565(190, 195, 210);
constexpr uint16_t BASE_FOG   = rgb565(150, 165, 175);
constexpr uint16_t BASE_STORM = rgb565(200, 120, 255);
constexpr uint16_t BASE_SEL   = rgb565(0, 70, 140);
// клітинки графіка відключень
constexpr uint16_t BASE_S_ON    = rgb565(0, 170, 60);
constexpr uint16_t BASE_S_OFF   = rgb565(215, 40, 40);
constexpr uint16_t BASE_S_MAYBE = rgb565(230, 170, 0);
constexpr uint16_t BASE_S_NONE  = rgb565(50, 50, 55);

#define COL_BG     ((uint16_t)0x0000)
#define COL_LINE   dim565(BASE_LINE)
#define COL_TIME   dim565(BASE_TIME)
#define COL_DAY    dim565(BASE_DAY)
#define COL_TEXT   dim565(BASE_TEXT)
#define COL_DIM    dim565(BASE_DIM)
#define COL_FEELS  dim565(BASE_FEELS)
#define COL_TEMP   dim565(BASE_TEMP)
#define COL_HUM    dim565(BASE_HUM)
#define COL_WIND   dim565(BASE_WIND)
#define COL_AZ     dim565(BASE_AZ)
#define COL_RAIN   dim565(BASE_RAIN)
#define COL_SNOW   dim565(BASE_SNOW)
#define COL_SLEET  dim565(BASE_SLEET)
#define COL_OK     dim565(BASE_OK)
#define COL_WARN   dim565(BASE_WARN)
#define COL_BAD    dim565(BASE_BAD)
#define COL_SUN    dim565(BASE_SUN)
#define COL_CLOUD  dim565(BASE_CLOUD)
#define COL_FOG    dim565(BASE_FOG)
#define COL_STORM  dim565(BASE_STORM)
#define COL_SEL    dim565(BASE_SEL)
#define COL_S_ON    dim565(BASE_S_ON)
#define COL_S_OFF   dim565(BASE_S_OFF)
#define COL_S_MAYBE dim565(BASE_S_MAYBE)
#define COL_S_NONE  dim565(BASE_S_NONE)

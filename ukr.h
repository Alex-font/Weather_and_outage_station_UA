#pragma once
// Підтримка української мови на екрані.
//
// Шрифти Adafruit GFX однобайтові, тому кирилиця зберігається в кодуванні CP1251
// (див. fonts_cyr.h). У програмі всі тексти пишуться звичайним UTF-8, а перед виведенням
// на екран перетворюються функціями printU() і boundsU().
// Увага: у шрифтах немає символів, яких не було в CP1251 (вони виводяться як '?').

#include "fonts_cyr.h"
#include "lang.h"

static char cp1251Of(uint16_t cp) {
  if (cp >= 0x0410 && cp <= 0x044F) return (char)(cp - 0x0410 + 0xC0);  // А-я
  switch (cp) {
    case 0x0401: return (char)0xA8;  // Ё
    case 0x0451: return (char)0xB8;  // ё
    case 0x0404: return (char)0xAA;  // Є
    case 0x0454: return (char)0xBA;  // є
    case 0x0406: return (char)0xB2;  // І
    case 0x0456: return (char)0xB3;  // і
    case 0x0407: return (char)0xAF;  // Ї
    case 0x0457: return (char)0xBF;  // ї
    case 0x0490: return (char)0xA5;  // Ґ
    case 0x0491: return (char)0xB4;  // ґ
    case 0x00B0: return (char)0xB0;  // °
    case 0x00A0: return ' ';
    default: return '?';
  }
}

// UTF-8 -> CP1251
static void toCp1251(const char* in, char* out, size_t n) {
  size_t o = 0;
  const uint8_t* p = (const uint8_t*)in;
  while (*p && o + 1 < n) {
    uint8_t c = *p;
    if (c < 0x80) {
      out[o++] = (char)c;
      p++;
    } else if ((c & 0xE0) == 0xC0 && p[1]) {
      uint16_t cp = (uint16_t)(((c & 0x1F) << 6) | (p[1] & 0x3F));
      out[o++] = cp1251Of(cp);
      p += 2;
    } else {  // довші послідовності не підтримуються
      out[o++] = '?';
      p++;
      while ((*p & 0xC0) == 0x80) p++;
    }
  }
  out[o] = 0;
}

static void printU(const char* utf8) {
  char b[160];
  toCp1251(utf8, b, sizeof b);
  tft.print(b);
}

static void boundsU(const char* utf8, int16_t x, int16_t y, int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h) {
  char b[160];
  toCp1251(utf8, b, sizeof b);
  tft.getTextBounds(b, x, y, x1, y1, w, h);
}

// Скорочує UTF-8 рядок до maxChars символів (не розриваючи багатобайтові символи)
static void u8trunc(const char* in, char* out, size_t n, int maxChars) {
  size_t o = 0;
  int chars = 0;
  const uint8_t* p = (const uint8_t*)in;
  while (*p && chars < maxChars) {
    size_t len = (*p < 0x80) ? 1 : ((*p & 0xE0) == 0xC0) ? 2 : ((*p & 0xF0) == 0xE0) ? 3 : 4;
    size_t avail = strnlen((const char*)p, len);
    if (o + avail + 1 > n) break;
    for (size_t k = 0; k < avail; k++) out[o++] = (char)p[k];
    p += avail;
    chars++;
  }
  out[o] = 0;
}

// ---------- Додатково для монітора відключень ----------

// CP1251 -> UTF-8 (назви населених пунктів у базі зберігаються в CP1251, щоб заощадити флеш-пам'ять)
static void cp1251ToUtf8(const char* in, char* out, size_t n) {
  size_t o = 0;
  const uint8_t* p = (const uint8_t*)in;
  while (*p && o + 3 < n) {
    uint8_t c = *p++;
    uint16_t cp = 0;
    if (c < 0x80) {
      out[o++] = (char)c;
      continue;
    } else if (c >= 0xC0) cp = 0x0410 + (c - 0xC0);
    else switch (c) {
      case 0xA5: cp = 0x0490; break;  // Ґ
      case 0xB4: cp = 0x0491; break;  // ґ
      case 0xAA: cp = 0x0404; break;  // Є
      case 0xBA: cp = 0x0454; break;  // є
      case 0xB2: cp = 0x0406; break;  // І
      case 0xB3: cp = 0x0456; break;  // і
      case 0xAF: cp = 0x0407; break;  // Ї
      case 0xBF: cp = 0x0457; break;  // ї
      default: cp = '?'; break;
    }
    if (cp < 0x80) {
      out[o++] = (char)cp;
    } else {
      out[o++] = (char)(0xC0 | (cp >> 6));
      out[o++] = (char)(0x80 | (cp & 0x3F));
    }
  }
  out[o] = 0;
}

// Друк UTF-8 на довільне полотно (GFXcanvas16 тощо), шрифт і колір задаються на полотні
static void printUTo(Adafruit_GFX& g, const char* utf8) {
  char b[160];
  toCp1251(utf8, b, sizeof b);
  g.print(b);
}

// Ширина тексту в пікселях для шрифту, встановленого на tft
static int textWidthU(const char* utf8) {
  int16_t x1, y1;
  uint16_t w, h;
  boundsU(utf8, 0, 0, &x1, &y1, &w, &h);
  return (int)w;
}

// Вкорочує рядок так, щоб він вміщувався в maxW пікселів (шрифт встановлено на tft), у кінці ".."
static void fitText(const char* in, char* out, size_t n, int maxW) {
  strlcpy(out, in, n);
  if (textWidthU(out) <= maxW) return;
  int chars = 0;
  for (const uint8_t* p = (const uint8_t*)in; *p; p++) {
    if ((*p & 0xC0) != 0x80) chars++;
  }
  while (chars > 1) {
    chars--;
    u8trunc(in, out, n, chars);
    size_t l = strlen(out);
    if (l + 3 < n) strlcat(out, "..", n);
    if (textWidthU(out) <= maxW) return;
  }
}

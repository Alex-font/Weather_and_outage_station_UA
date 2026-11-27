#pragma once
// Мова написів на екрані: true українська, false англійська.
// Змініть значення й перепрошийте плату.
constexpr bool UI_UKRAINIAN = true;

// Повертає український або англійський варіант тексту залежно від налаштування
static inline const char* T(const char* en, const char* ua) {
  return UI_UKRAINIAN ? ua : en;
}

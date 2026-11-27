#pragma once
// Збережені мережі Wi-Fi (пам'ять NVS): до MAX_SAVED мереж.
// Коли місця немає, найстарішу заміщає нова.

#include <Arduino.h>
#include <Preferences.h>

constexpr int MAX_SAVED = 8;

struct SavedNet {
  char ssid[33];
  char pass[64];
};

static Preferences wstore;

static void wifiStoreBegin() {
  wstore.begin("wifinets", false);
}

static void slotKey(char* out, char kind, int slot) {  // out: щонайменше 4 байти
  snprintf(out, 4, "%c%d", kind, slot);
}

static bool wifiStoreLoad(int slot, SavedNet& n) {
  char key[4];
  slotKey(key, 's', slot);
  if (!wstore.isKey(key)) return false;
  String s = wstore.getString(key, "");
  if (s.length() == 0) return false;
  strlcpy(n.ssid, s.c_str(), sizeof n.ssid);
  slotKey(key, 'p', slot);
  String p = wstore.isKey(key) ? wstore.getString(key, "") : String("");
  strlcpy(n.pass, p.c_str(), sizeof n.pass);
  return true;
}

// Повертає номер комірки або -1
static int wifiStoreFind(const char* ssid) {
  char key[4];
  for (int i = 0; i < MAX_SAVED; i++) {
    slotKey(key, 's', i);
    if (wstore.isKey(key) && wstore.getString(key, "") == ssid) return i;
  }
  return -1;
}

static int wifiStoreCount() {
  char key[4];
  int n = 0;
  for (int i = 0; i < MAX_SAVED; i++) {
    slotKey(key, 's', i);
    if (wstore.isKey(key)) n++;
  }
  return n;
}

// Додає мережу або оновлює пароль, якщо така вже є
static void wifiStoreSave(const char* ssid, const char* pass) {
  int slot = wifiStoreFind(ssid);
  char key[4];
  if (slot < 0) {
    for (int i = 0; i < MAX_SAVED; i++) {  // перша вільна комірка
      slotKey(key, 's', i);
      if (!wstore.isKey(key)) { slot = i; break; }
    }
  }
  if (slot < 0) {  // усі зайняті: заміщаємо по колу
    uint8_t next = wstore.isKey("next") ? wstore.getUChar("next", 0) : 0;
    slot = next % MAX_SAVED;
    wstore.putUChar("next", (slot + 1) % MAX_SAVED);
  }
  slotKey(key, 's', slot);
  wstore.putString(key, ssid);
  slotKey(key, 'p', slot);
  wstore.putString(key, pass);
}

#!/usr/bin/env python3
"""Генератор places_data.h з файлу ukr-populated-places.xlsx (COD-AB Україна, OCHA / HDX).

Використання:   python gen_places.py ukr-populated-places.xlsx ../places_data.h
Потрібна бібліотека openpyxl (pip install openpyxl).

Єдина база для двох задач: графіка відключень (область, район, громада) і погоди (координати).

Що робить:
  * групує населені пункти: область -> район -> населений пункт (громаду зберігає окремим довідником);
  * сортує за українським алфавітом (А Б В Г Ґ Д Е Є Ж З И І Ї Й К ...);
  * назви пише в CP1251 (1 байт на літеру), координати стискає до 2 байтів на вісь.

Формат запису населеного пункту в масиві PLACES (заголовок PL_HDR = 6 байтів):
  [громада lo+1][громада hi+1][широта lo][широта hi][довгота lo][довгота hi][назва CP1251]\\0
  номер громади в HROMS = (b0-1) + 255*(b1-1)
  широта  = (lat - 44.0) * 6000   (крок ~18 м)
  довгота = (lon - 22.0) * 3500   (крок ~31 м)
"""
import sys, collections

LAT0, LAT_K = 44.0, 6000.0
LON0, LON_K = 22.0, 3500.0
ALPHA = "абвгґдеєжзиіїйклмнопрстуфхцчшщьюя"


def ukey(s):
    out = []
    for ch in s.lower():
        if ch in ALPHA: out.append(10 + ALPHA.index(ch))
        elif ch == "'": out.append(1)
        elif ch == "-": out.append(2)
        elif ch == " ": out.append(3)
        else: out.append(100 + ord(ch) % 100)
    return out


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def q_lat(v): return int(round((v - LAT0) * LAT_K))
def q_lon(v): return int(round((v - LON0) * LON_K))


def emit(out_path, obl_rows, raion_rows, hroms, places, total, valid_on):
    """obl_rows: (pcode, display, raion0, nRaion); raion_rows: (name, offset, count); places: bytearray."""
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("#pragma once\n")
        f.write("// ЗГЕНЕРОВАНО tools/gen_places.py з ukr-populated-places.xlsx (COD-AB Україна, OCHA / HDX, станом на %s). НЕ РЕДАГУВАТИ ВРУЧНУ.\n" % valid_on)
        f.write("// Умови використання даних: https://data.humdata.org/dataset/cod-ab-ukr (CC BY-IGO).\n")
        f.write("// Населених пунктів: %d, районів: %d, громад: %d, розмір бази: %d байт.\n" % (total, len(raion_rows), len(hroms), len(places)))
        f.write("#include <Arduino.h>\n#include <stdint.h>\n\n")
        f.write("struct OblInfo { const char* pcode; const char* name; uint8_t raion0; uint8_t nRaion; };\n")
        f.write("struct RaionInfo { const char* name; uint32_t off; uint16_t count; };\n\n")
        f.write("constexpr int OBL_COUNT = %d;\nconstexpr int RAION_COUNT = %d;\nconstexpr int HROM_COUNT = %d;\n" % (len(obl_rows), len(raion_rows), len(hroms)))
        f.write("constexpr int PL_HDR = 6;  // байтів перед назвою в кожному записі\n")
        f.write("constexpr float PL_LAT0 = %.1ff, PL_LAT_K = %.1ff, PL_LON0 = %.1ff, PL_LON_K = %.1ff;\n\n" % (LAT0, LAT_K, LON0, LON_K))
        f.write("static const OblInfo OBLS[OBL_COUNT] = {\n")
        for pc, nm, r0, n in obl_rows:
            f.write("  {%s, %s, %d, %d},\n" % (cstr(pc), cstr(nm), r0, n))
        f.write("};\n\n")
        f.write("static const RaionInfo RAIONS[RAION_COUNT] = {\n")
        for nm, off, n in raion_rows:
            f.write("  {%s, %d, %d},\n" % (cstr(nm), off, n))
        f.write("};\n\n")
        f.write("static const char* const HROMS[HROM_COUNT] = {\n")
        for nm in hroms:
            f.write("  %s,\n" % cstr(nm))
        f.write("};\n\n")
        f.write("// Записи по районах підряд (порядок і зсуви: RAIONS).\n")
        f.write("static const uint8_t PLACES[%d] PROGMEM = {\n" % len(places))
        line = []
        for b in places:
            line.append(str(b))
            if len(line) == 40:
                f.write(",".join(line) + ",\n")
                line = []
        if line:
            f.write(",".join(line) + "\n")
        f.write("};\n")


def pack(hrom_idx, lat_q, lon_q, name):
    assert 0 <= lat_q < 65536 and 0 <= lon_q < 65536, (name, lat_q, lon_q)
    return bytes([(hrom_idx % 255) + 1, (hrom_idx // 255) + 1,
                  lat_q & 255, lat_q >> 8, lon_q & 255, lon_q >> 8]) + name.encode("cp1251") + b"\0"


def main(xlsx, out):
    import openpyxl
    wb = openpyxl.load_workbook(xlsx, read_only=True)
    ws = wb["Populated Places"]
    rows = list(ws.iter_rows(values_only=True))
    ix = {k: i for i, k in enumerate(rows[0])}
    R = rows[1:]
    valid_on = ""
    try:
        for r in wb["Metadata"].iter_rows(values_only=True):
            if r and str(r[0]).strip() == "Valid on":
                valid_on = str(r[1]).strip()
    except Exception:
        pass

    def g(r, k): return r[ix[k]]

    def obl_display(code, name):
        if code == "UA01": return "АР Крим", "Крим"
        if code in ("UA80", "UA85"): return "м. " + name, name
        return name + " обл.", name
    obls = {}
    for r in R:
        code, name = g(r, "adm1_pcode"), g(r, "adm1_name1")
        if code not in obls:
            obls[code] = obl_display(code, name)
    obl_order = sorted(obls, key=lambda c: ukey(obls[c][1]))

    hrom_names = collections.OrderedDict()
    for r in sorted(R, key=lambda r: (g(r, "adm3_pcode"))):
        hrom_names.setdefault(g(r, "adm3_pcode"), g(r, "adm3_name1"))
    hrom_idx = {c: i for i, c in enumerate(hrom_names)}
    assert len(hrom_idx) < 255 * 255

    by_raion = collections.defaultdict(list)
    raion_name, raion_obl = {}, {}
    for r in R:
        rc = g(r, "adm2_pcode")
        by_raion[rc].append(r)
        raion_name[rc] = g(r, "adm2_name1")
        raion_obl[rc] = g(r, "adm1_pcode")

    places = bytearray()
    raion_rows, obl_rows = [], []
    for oc in obl_order:
        rcs = sorted([rc for rc in raion_name if raion_obl[rc] == oc], key=lambda rc: ukey(raion_name[rc]))
        obl_rows.append((oc, obls[oc][0], len(raion_rows), len(rcs)))
        for rc in rcs:
            recs = sorted(by_raion[rc], key=lambda r: (ukey(g(r, "adm4_name1")), g(r, "adm3_name1"), g(r, "adm4_pcode")))
            off = len(places)
            for r in recs:
                places += pack(hrom_idx[g(r, "adm3_pcode")], q_lat(g(r, "y_coord")), q_lon(g(r, "x_coord")), g(r, "adm4_name1"))
            raion_rows.append((raion_name[rc], off, len(recs)))
    assert len(obl_rows) < 256 and len(raion_rows) < 256
    emit(out, obl_rows, raion_rows, list(hrom_names.values()), places, len(R), valid_on)
    print("OK:", len(R), "places,", len(raion_rows), "raions,", len(places), "bytes")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])

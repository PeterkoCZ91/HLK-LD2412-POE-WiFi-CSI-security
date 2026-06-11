# Návrhy vylepšení — poe-2412-wifi

> Stav při sepsání: **v5.0.6**, ~15,7k LOC, ESP32 PoE (radar LD2412 + WiFi CSI fusion).
> Security audit z 2026-06-05 je z velké části vyřešen (F-01, F-02, F-06, F-09 hotové ve v5.0.4–5.0.6).
> Datum: 2026-06-11.

Projekt je vyzrálý a dobře udržovaný. Nejde o opravu zjevných děr, ale o technický dluh
a smysluplné přidání hodnoty. Tasky jsou rozdělené podle náročnosti a self-contained
(určené k implementaci v samostatných session nezávisle na sobě).

---

## 🟢 SNADNÉ (hodiny, nízké riziko)

### T1 — `/metrics` endpoint pro Prometheus
Data už všechna existují (heap, uptime, RSSI, packet rate, fusion confidence, alarm state,
chip temp). Stačí je vypsat v Prometheus text formátu na novém endpointu (unauth nebo basic-auth).
Otevře Grafana dashboardy uživatelům mimo Home Assistant.
- **Hodnota/práce:** velmi dobrá.
- **Soubory:** `src/WebRoutes.cpp` (nová route), data z `CSIService` / `SecurityMonitor` / system.

### T2 — CI: `cppcheck` + `clang-format --dry-run`
`.github/workflows/build.yml` teď dělá jen build dvou envů. Statická analýza chytí třídu chyb
v safety-critical alarm logice. Přidat jako matrix krok.
- **Soubory:** `.github/workflows/build.yml`, příp. `.clang-format`.

### T3 — Auth lockout / rate-limit
`WebRoutes.cpp:1105` už trackuje „age since last failed auth", ale není žádný lockout.
Po N neúspěšných pokusech v okně → `429` + exponenciální backoff. Levná obrana proti brute-force
na Basic/Digest. (Zbytek auditu hotový, tohle je logický doplněk.)
- **Soubory:** `src/WebRoutes.cpp`.

### T4 — `web_interface.h` má `lang="cs"` natvrdo
V `<head>` je `lang="cs"` přesto, že default jazyk je teď EN (v5.0.4) a běží i18n.
Ovlivňuje screen readery a lang detekci. Drobnost.
- **Soubory:** `include/web_interface.h`.

---

## 🟡 STŘEDNÍ (dny, střední riziko)

### T5 — ⭐ Nativní unit testy (`env:native`) — NEJVYŠŠÍ HODNOTA
Projekt má **nula** automatických testů na 15,7k LOC bezpečnostní logiky; CI jen builduje.
Tyto části jsou čisté funkce / deterministické stavové automaty a jdou testovat bez hardwaru:
- `include/services/ml_features.h` — 17-feature extraktor (median, skewness, kurtosis, entropy…).
  Regression testy proti známým vektorům.
- `MQTTOfflineBuffer` — ring buffer s drop-on-OTA logikou.
- **`SecurityMonitor` stavový automat** — DISARMED→ARMING→ARMED→PENDING→TRIGGERED, entry/exit
  delays, auto-rearm, scheduled arm. Ve v5.0.1 byla reálná bugfix ve scheduled arm logice —
  přesně to, co testy chrání. **Začít tady, má největší blast radius.**
- `ConfigSnapshot` round-trip.

Doporučení: TDD přístup (skill `test-driven-development`).

### T6 — Refaktor `main.cpp` (2043 ř.) — vyextrahovat MQTT command router
God-file: mesh supervision, OTA runtime FSM, zóny, ETH eventy, MQTT callback dispatch.
Vytáhnout dispatch příkazů (ARM/DISARM/PIN — audit F-01) do vlastní jednotky → stane se
**testovatelnou** (navazuje na T5) a PIN guard dostane unit testy.
- **Soubory:** `src/main.cpp` → nová jednotka (např. `src/services/MQTTCommandRouter.cpp`).

### T7 — Build pipeline pro `web_interface.h`
Teď je to jeden 2416řádkový `PROGMEM` raw-string blob (HTML+CSS+JS+i18n inline). Žádný lint,
žádná minifikace, špatně se diffuje. Návrh: zdroje rozdělit do `web/src/*.{html,css,js}`,
build skript zminifikuje a vygeneruje header (PROGMEM serving zůstává). Ušetří flash i údržbu.
- **Soubory:** nový `web/src/`, build skript v `tools/`, `include/web_interface.h` (generovaný).

### T8 — Skutečné MQTTS/TLS (audit F-04, zatím neřešeno)
`WebRoutes.cpp:261` jen *detekuje* port 8883 pro zobrazení — žádný TLS MQTT klient neexistuje.
Pro produkci: `PubSubClient` přes `WiFiClientSecure` s CA certem. Alarm stav a presence teď
jdou po síti plaintextem.
- **Soubory:** `src/services/MQTTService.cpp`, `include/secrets.h`.

---

## 🔴 NÁROČNÉ (týdny / vyžadují výzkum)

### T9 — On-device ML feedback loop
Model (`ml_weights.h`, F1=0.852) se retrénuje ručně offline v sister projektu. Návrh: tlačítko
v UI „falešný poplach / skutečný pohyb" → uloží feature vektor + label do LittleFS → export přes
API → zkrácení retrénovací smyčky. Uzavírá ML cyklus přímo na zařízení.

### T10 — Sonoff/dveřní korroborační okno (roadmap v5.1.x)
U low-confidence alarmů počkat X sekund na potvrzení z dveřního/okenního kontaktu (MQTT) než se
spustí siréna. Snižuje false positives. Plánováno v roadmapě.

### T11 — Platform/library bump na ESP32Async/platform-espressif32 (roadmap v5.1.x)
Větší riziko regresí — LAN8720A + AsyncTCP race podmínky byly historicky bolavé (proto pinnuté
commity). Vyžaduje pečlivé bench testování OTA pod zátěží.

### T12 — People counting / occupancy estimace z fúze radar+CSI
Výzkumný směr — fúzní confidence + DSER/PLCR features by mohly dát hrubý odhad počtu osob.
Nejistý výsledek, ale diferenciátor.

### T13 — OTA HTTPS cert pinning (audit F-05)
`setInsecure()` v Pull OTA i Telegram. Povinné MD5 (v5.0.6) integritu už řeší → priorita nízká,
ale pro úplnost: CA cert / fingerprint pinning.

---

## Doporučené pořadí

Maximální hodnota: **T5 (testy SecurityMonitoru) → T6 (extrakce MQTT routeru, zpřístupní ho
testům) → T2 (cppcheck do CI)**. Trojice dramaticky zvedne důvěru v safety-critical cestu a chytí
regrese dřív, než dojedou na hardware. Z přidaných funkcí má nejlepší poměr hodnota/práce **T1
(/metrics)**.

> Pozn.: Před implementací T5/T6 se vyplatí rozepsat detailní plán (skill `writing-plans`).

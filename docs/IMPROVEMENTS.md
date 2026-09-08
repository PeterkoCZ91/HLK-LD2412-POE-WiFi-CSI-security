# Návrhy vylepšení — poe-2412-wifi

> **Release checkpoint 2026-09-06 (aktualizováno):** stav 5.7.1 a plný
> průběh vyšetřování viz [RELEASE_5.7.1_VALIDATION.md](RELEASE_5.7.1_VALIDATION.md).
> Bench HTTP/SSE zátěž opakovaně padala (dev4-dev9); skutečná příčina byla
> zastaralý pin AsyncTCP knihovny se známou, upstream opravenou
> use-after-free chybou (ESP32Async/AsyncTCP#118) — ne chyba v tomto
> firmware. Bump na v3.5.0 (dev10) vyřešil všechny reprodukce, 5 různých
> zátěžových kombinací přežito bez restartu. Zbývá před vydáním: delší
> soak (dnešní ověření byly jen krátké ohraničené testy), reálný ML sběr
> z provozu, hardware test MQTT alarm routeru — viz dokument pro detail.
> Historické „HOTOVO“ níže neznamená, že prošlo aktuální hardwarové
> předrelease ověření.

> Stav při sepsání: **v5.0.6**, ~15,7k LOC, ESP32 PoE (radar LD2412 + WiFi CSI fusion).
> Security audit z 2026-06-05 je z velké části vyřešen (F-01, F-02, F-06, F-09 hotové ve v5.0.4–5.0.6).
> Datum sepsání: 2026-06-11. **Poslední audit stavu: 2026-09-05** (v5.7.1-dev3) — viz "Stav" na konci souboru.

Projekt je vyzrálý a dobře udržovaný. Nejde o opravu zjevných děr, ale o technický dluh
a smysluplné přidání hodnoty. Tasky jsou rozdělené podle náročnosti a self-contained
(určené k implementaci v samostatných session nezávisle na sobě).

---

## 🟢 SNADNÉ (hodiny, nízké riziko)

### T1 — ✅ HOTOVO (v5.0.7) — `/metrics` endpoint pro Prometheus
`GET /metrics` v `src/WebRoutes.cpp:716`, Prometheus text formát. Otevřelo Grafana dashboardy
uživatelům mimo Home Assistant.

### T2 — ✅ HOTOVO (v5.0.7) — CI: `cppcheck` + `clang-format --dry-run`
`cppcheck` job v `.github/workflows/build.yml` (`tools/run_cppcheck.sh`), matrix krok vedle buildu.

### T3 — ✅ HOTOVO (v5.0.7) — Auth lockout / rate-limit
`AuthLockout` (`src/WebRoutes.cpp:153`) + `429` brána po N neúspěšných pokusech v okně.

### T4 — ✅ HOTOVO — `web_interface.h` mělo `lang="cs"` natvrdo
`web/src/index.html:2` má teď `lang="en"` a běží i18n toggle.

---

## 🟡 STŘEDNÍ (dny, střední riziko)

### T5 — ✅ HOTOVO (v5.0.7, průběžně rozšiřováno) — Nativní unit testy (`env:native`)
Od nuly testů k **405 test cases** v `pio test -e native` (k 2026-09-05), pokrývá
`SecurityMonitor` FSM, `ml_features.h`, `MQTTOfflineBuffer`, `ConfigSnapshot` a všechno nově
extrahované (T6/T9). Základ, na kterém stojí T6/T7/T9.

### T6 — ✅ HOTOVO (2026-09-04) — Refaktor `main.cpp` — vyextrahovat MQTT command router
`MqttCommandRouter::evaluateArmCommand` (`include/services/MqttCommandRouter.h`) — čistá
rozhodovací logika ARM/DISARM/PIN vyextrahovaná z main.cpp lambdy, 9 native testů.

### T7 — ✅ HOTOVO (2026-09-04) — Build pipeline pro `web_interface.h`
Zdroje v `web/src/{index.html,style.css,i18n.js,app.js}`, `tools/build_web.py` minifikuje a
generuje header, CI hlídá drift (`build_web.py --check`).

### T8 — ✅ HOTOVO — Skutečné MQTTS/TLS (audit F-04)
`PubSubClient` přes `WiFiClientSecure` s CA certem (`MQTTService.cpp:146`, `setCACert`).

---

## 🔴 NÁROČNÉ (týdny / vyžadují výzkum)

### T9 — ✅ HOTOVO (2026-09-04) — On-device ML feedback loop
`MlFeedbackStore` + `POST /api/csi/feedback` (label motion/no_motion) + `GET
/api/csi/feedback/export` (stránkovaný export pro sesterský retrain pipeline).

### T10 — ⏸️ ODLOŽENO (2026-09-04) — Sonoff/dveřní korroborační okno
U low-confidence alarmů počkat X sekund na potvrzení z dveřního/okenního kontaktu (MQTT) než se
spustí siréna. Snižuje false positives. Ne vyřešeno kódem, ne hardwarovým výzkumem jako T12 —
prostě doma není MQTT dveřní/okenní senzor (Sonoff/Zigbee2MQTT), na kterém by se to dalo postavit
a otestovat. Architektura na to existuje jako hotový vzor (mesh verify_request/verify_confirm v
`main.cpp` — MQTT subscribe na topic jiného zařízení + časové okno), `CorroborationWindow.h`
(`include/services/CorroborationWindow.h`) řeší jen interní cross-modal case (radar↔CSI), ne
externí kontakt. Až bude senzor k dispozici, rozšíření by mělo být menší task.

### T11 — ✅ HOTOVO (v5.1.0, 2026-07-08) — ESP-IDF 5.5 / Arduino 3.x migrace
Vydáno jako `esp32_poe_csi_idf5` / `esp32_poe_csi_idf5_8mb` na pioarduino platformě, běží
**vedle** starého espressif32@6.9.0 stacku (kód guardovaný `ESP_ARDUINO_VERSION_MAJOR`), ne
místo něj — viz `docs/superpowers/specs/2026-07-02-idf5-arduino3-migration.md` T10 rozhodovací
bod (produkce zůstává na 6.9.0, migrují se jen nové/testovací uzly). Detaily v CHANGELOG.md.

### T12 — ⏸️ ODLOŽENO (2026-09-04) — People counting / occupancy estimace z fúze radar+CSI
Původní záměr: fúzní confidence + DSER/PLCR features (`ml_features.h:12-13`) → hrubý odhad
počtu osob. Rešerše (GitHub/arXiv) ukázala, že s naším senzorovým setupem je to pravděpodobně
nedosažitelné na použitelné úrovni, ne jen "nejistý výzkum":

- `LD2412Service::RadarData` (`include/services/LD2412Service.h:29-38`) je single-target —
  jedna `distance_cm`/`moving_energy`, ne seznam objektů.
- CSI je agregovaný SISO signál jedné WiFi linky — žádné prostorové rozlišení bez více antén.
- Referenční dataset na téměř identickém HW (`RS2002/WiCount` — ESP32-S3, 1 anténa, 52
  subnosných, 0–3 lidi) ukazuje, že modely velikostí srovnatelné s naší on-device MLP dopadnou
  špatně (MLP 56,77 %, baseline náhody 25 %). Použitelných 94,32 % dosáhl jen CSI-BERT2 —
  5,45M parametrů, GPU inference — mimo náš "vše na zařízení" architektonický rámec.
- Reálné nasazení na skoro stejném HW (`ruvnet/RuView` issue #803) hlásí, že ESP32-S3 trvale
  ukazuje 1 osobu bez ohledu na skutečný počet — nevyřešeno, otevřené.
- Espressifův vlastní `esp-csi` toolkit people counting vůbec nenabízí, jen single-person
  presence/activity.

Bez připojeného radaru navíc chybí i ta druhá polovina fúze. Neotvírat bez nového HW
(vícero přijímačů/antén) nebo výrazně jiného přístupu.

### T13 — ✅ HOTOVO — OTA HTTPS cert pinning (audit F-05)
Žádné `setInsecure()` v repu — Pull OTA i Telegram jdou přes ověřený CA řetězec.

---

## Stav k 2026-09-05

**T1–T9, T11, T13 hotovo.** T10 a T12 vědomě odložené — chybí hardware (MQTT dveřní senzor,
resp. MW radar), ne nedostatek hodnoty, viz jejich sekce výše. Původní "doporučené pořadí"
(T5→T6→T2) proběhlo přesně v tomhle sledu napříč v5.0.7–dnes.

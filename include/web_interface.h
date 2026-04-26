#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="cs">
<head>
  <title>LD2412 Zabezpečení</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
    :root { --bg: #0a0a0a; --card: #161616; --text: #e0e0e0; --accent: #03dac6; --warn: #cf6679; --sec: #333; }
    * { box-sizing: border-box; }
    body { font-family: 'Segoe UI', sans-serif; background: var(--bg); color: var(--text); margin: 0; padding: 10px; padding-bottom: 50px; }
    h2 { color: var(--accent); margin: 5px 0; font-size: 1.4rem; display: flex; align-items: center; justify-content: center; gap: 10px; }
    
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 10px; max-width: 1200px; margin: 0 auto; }
    .card { background: var(--card); padding: 15px; border-radius: 12px; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }
    
    .stat-row { display: flex; justify-content: space-between; margin-bottom: 8px; font-size: 0.9rem; border-bottom: 1px solid #222; padding-bottom: 4px; }
    .stat-val { font-weight: bold; color: #fff; }
    
    .gauge { text-align: center; margin-bottom: 15px; }
    .big-val { font-size: 2.5rem; font-weight: bold; line-height: 1; }
    .unit { font-size: 0.8rem; color: #888; }
    
    /* Sparkline */
    svg.spark { width: 100%; height: 50px; stroke-width: 2; fill: none; margin-top: 5px; }
    
    /* Icons */
    .icon { width: 16px; height: 16px; display: inline-block; vertical-align: middle; border-radius: 50%; }
    .icon.ok { background: #00ff00; box-shadow: 0 0 5px #00ff00; }
    .icon.warn { background: orange; }
    .icon.err { background: #ff0000; }
    
    /* Inputs */
    input[type=range] { width: 100%; accent-color: var(--accent); }
    input[type=text], input[type=password], input[type=number], select { background: #222; border: 1px solid #444; color: white; padding: 8px; border-radius: 4px; width: 100%; margin-top:2px; }
    .row-input { display:flex; justify-content:space-between; align-items:center; margin-bottom:5px; gap:10px; }
    
    button { width: 100%; padding: 10px; border: none; border-radius: 6px; background: #3700b3; color: white; cursor: pointer; margin-top: 5px; }
    button:hover { opacity: 0.9; }
    button.sec { background: var(--sec); }
    button.warn { background: var(--warn); color: black; font-weight: bold; }

    /* Per-gate & Bars */
    .gate-wrapper { display: flex; align-items: center; gap: 5px; margin-bottom: 4px; font-size: 0.75rem; }
    .gate-label { width: 25px; flex-shrink:0; }
    input[type=range].mov-slider { accent-color: #03dac6; }
    input[type=range].stat-slider { accent-color: #bb86fc; }
    .gate-dimmed { opacity: 0.55; background: #1a1a1a; border-radius: 4px; padding: 2px 4px; }
    .gate-dimmed .gate-label { color: #444; text-decoration: line-through; }
    .gate-dimmed input[type=range] { filter: grayscale(1) brightness(0.6); }
    .gate-dimmed span[id^="lm_"], .gate-dimmed span[id^="ls_"] { color: #444 !important; text-decoration: line-through; }
    
    .tabs { display: flex; gap: 5px; margin-bottom: 10px; flex-wrap: wrap; }
    .tab { flex: 1; min-width: 80px; padding: 8px; background: #222; text-align: center; cursor: pointer; border-radius: 6px; font-size:0.9rem; }
    .tab.active { background: var(--accent); color: black; font-weight: bold; }
    
    .hidden { display: none; }
    
    .section-title { color:#888; font-size:0.8rem; margin:15px 0 5px 0; text-transform:uppercase; border-bottom:1px solid #333; }
    
    #toast { position: fixed; bottom: 20px; left: 50%; transform: translateX(-50%); background: #333; padding: 10px 20px; border-radius: 20px; opacity: 0; transition: opacity 0.3s; pointer-events: none; }

    /* Timeline scroll */
    #evt_timeline_list::-webkit-scrollbar { width: 6px; }
    #evt_timeline_list::-webkit-scrollbar-thumb { background: #444; border-radius: 3px; }
    #evt_timeline_list::-webkit-scrollbar-track { background: #111; }

    /* Mobile Responsive */
    @media (max-width: 480px) {
        .grid { grid-template-columns: 1fr; gap: 8px; }
        body { padding: 5px; padding-bottom: 60px; }
        .big-val { font-size: 2rem; }
        .tabs { flex-wrap: wrap; gap: 4px; }
        .tab { min-width: 60px; font-size: 0.8rem; padding: 10px 6px; flex-grow: 1; }
        button { padding: 12px; min-height: 44px; font-size: 1rem; }
        input[type=range] { height: 30px; }
        .gate-wrapper { flex-wrap: wrap; }
        .row-input { flex-direction: column; align-items: stretch; gap: 2px; }
        .row-input span { margin-bottom: 2px; }
        input[type=text], input[type=number], select { padding: 10px; font-size: 1rem; }
    }
  </style>
  <script>
  // i18n — CZ/EN language support
  const I18N = {
    cs: {
      title: "LD2412 Zabezpečení", loading: "NAČÍTÁM...", arm: "STŘEŽIT", disarm: "ZRUŠIT",
      disarmed: "🔓 NESTŘEŽENO", arming: "⏳ AKTIVUJI...", armed: "🔒 STŘEŽENO",
      pending: "⚠️ ČEKÁNÍ", triggered: "🚨 POPLACH",
      sensor_health: "Zdraví senzoru", uart_state: "UART Stav", frame_rate: "Snímková frekvence",
      ram: "RAM (Volná/Min)", chip_temp: "Teplota čipu", uptime: "Doba běhu",
      factory_reset_confirm: "Opravdu provést tovární reset radaru?",
      tab_basic: "Základní", tab_security: "Bezpečnost", tab_gates: "Hradla",
      tab_network: "Síť & Cloud", tab_zones: "Zóny", tab_events: "Události", tab_csi: "CSI",
      device_name: "Jméno zařízení (mDNS)", hold_time: "Doba držení (ms)",
      move_sens: "Citlivost Pohyb (%)", enable_diag: "Povolit Diagnostiku",
      radar_bt: "Bluetooth radaru (párování HLK aplikace)",
      radar_bt_warn: "⚠️ Zapnout jen na dobu potřebnou pro diagnostiku HLK aplikací. Doporučeno vypnuto — kdokoli v dosahu BT se jinak může spárovat s radarem.",
      radar_bt_apply: "Radar se restartuje pro aplikaci změny.",
      antimask_title: "ANTI-MASKING (Sabotáž zakrytím)", antimask_enable: "Povolit alarm při tichu",
      timeout_sec: "Časový limit (sec)",
      antimask_hint: "Pro sklady, chaty, serverovny <b>VYPNĚTE</b> - ticho je tam normální.<br>Pro obývané prostory ZAPNĚTE - detekuje zakrytí sensoru.",
      loiter_title: "LOITERING (Podezřelé postávání)", loiter_enable: "Notifikace při postávání",
      loiter_hint: "Alarm když někdo stojí &lt;2m od sensoru déle než timeout.",
      hb_title: "HEARTBEAT (Pravidelný report)",
      hb_hint: "0 = vypnuto, 4 = každé 4 hodiny zpráva \"jsem OK\".",
      pet_hint: "Filtruje malé objekty (kočky, psi) s nízkou energií &lt;2m.",
      entry_delay: "Zpoždění vstupu (sec)", exit_delay: "Zpoždění odchodu (sec)",
      disarm_reminder: "Připomínka \"Stále NESTŘEŽENO\"",
      hold_hint: "Doba, po které radar hlásí \"nepřítomnost\" bez detekce.",
      light_func: "Funkce světla", light_night: "Noční režim (pod práh)", light_day: "Denní režim (nad práh)",
      light_thr: "Práh světla (0-255)", light_cur: "Aktuální světlo",
      light_hint: "OUT pin aktivní jen když je světlo pod/nad prahem.<br>Ideální pro noční zabezpečení (režim \"pod práh\").",
      movement: "Pohyb", static_: "Statika",
      gate_legend: "Higher = more sensitive &middot; <span style='opacity:0.4'>gray = outside min/max range</span>",
      set_all: "Nastavit vše:", indoor: "Interiér", outdoor: "Exteriér", pets: "Zvířata",
      save_gates: "Uložit Hradla", save_mqtt: "Uložit MQTT", save: "Uložit",
      username: "Uživatel", new_pass: "Nové heslo", change_pass: "Změnit heslo",
      cfg_backup: "ZÁLOHA KONFIGURACE", cfg_export: "💾 Exportovat", cfg_import: "📂 Importovat",
      cfg_import_confirm: "Nahradit všechna nastavení? Zařízení se restartuje.",
      cfg_import_ok: "Konfigurace importována, restartování...",
      cfg_import_err: "Import selhal: {err}",
      add_zone: "+ Přidat Zónu", save_zones: "💾 Uložit Zóny", learn_static: "📡 Naučit statiku",
      filter_all: "Vše", filter_alarm: "Alarm", filter_move: "Pohyb",
      filter_tamper: "Tamper", filter_hb: "Heartbeat", filter_sys: "Systém", filter_net: "Síť",
      delete: "Smazat", now: "nyní", load_more: "Načíst další...",
      csi_warn: "Tento firmware nebyl zkompilován s podporou WiFi CSI (chybí <code>-D USE_CSI=1</code>). Pro povolení nahraj variantu <b>esp32_poe_csi</b>.",
      csi_active: "Aktivní", csi_idle: "Idle baseline připraven",
      ap_compat: "AP kompatibilita (HT LTF)", ap_ok: "OK", ap_incompat: "NEKOMPATIBILNÍ", ap_checking: "čekám na HT LTF…",
      ap_incompat_hint: "AP nevysílá 802.11n HT LTF rámce (pravděpodobně WiFi 6 HW v n-módu). CSI je pasivní, ale detekce pohybu nebude fungovat. Použij pre-WiFi 6 AP.",
      wifi_ap: "WIFI AP (CSI)", wifi_ssid: "SSID", wifi_pass: "Heslo",
      wifi_ap_hint: "Přepnutí AP pro CSI senzor. Uložení vyžaduje reboot. Prázdné = fallback na compile-time default ze <code>secrets.h</code>.",
      save_reboot: "💾 Uložit a rebootovat", wifi_use_default: "↩️ Compile-time default",
      wifi_ssid_empty: "SSID nesmí být prázdné",
      wifi_save_confirm: "Uložit SSID '{ssid}' a rebootovat? Pokud je heslo špatné, ESP se nepřipojí a bude třeba factory reset (GPIO0 5s).",
      wifi_saved_reboot: "Uloženo. Rebootuji…",
      wifi_save_failed: "Uložení selhalo",
      wifi_reset_confirm: "Obnovit compile-time default SSID ze secrets.h a rebootovat?",
      wifi_reset_reboot: "Reset. Rebootuji…",
      motion_state: "Stav pohybu", composite: "Composite skóre",
      threshold_label: "Práh detekce (variance threshold):",
      save_config: "Uložit konfiguraci", upload_fw: "Nahrát Firmware", saved: "Uloženo",
      fw_update_title: "Aktualizace FW",
      pull_ota_title: "Pull OTA z URL",
      pull_ota_help: "ESP si stáhne firmware sám z dané URL. Funguje s HTTPS i přesměrováním. Auth header je volitelný (např. <code>Bearer &lt;token&gt;</code>).",
      pull_ota_btn: "⤓ Stáhnout a flashnout",
      pull_ota_running: "Stahuji & flashuji…",
      pull_ota_phase_idle: "—",
      pull_ota_phase_downloading: "Stahuji…",
      pull_ota_phase_writing: "Zapisuji firmware…",
      pull_ota_phase_success: "✅ Hotovo, restartuji",
      pull_ota_phase_error: "❌ Chyba",
      pull_ota_no_url: "Zadej URL k firmware .bin",
      ota_cold_label: "Před OTA restartovat (doporučeno pro 100% úspěšnost)",
      ota_cold_restart: "Restartuji zařízení...", ota_waiting_reboot: "Čekám na zařízení",
      ota_ready_uploading: "Zařízení naběhlo, nahrávám...", ota_uploading: "Nahrávám firmware...",
      ota_cold_failed: "Zařízení nenaběhlo do 25 s — zkontroluj napájení a zkus znovu",
      yes: "ANO", no: "NE", no_collecting: "NE — sbírá vzorky", motion: "POHYB", idle: "KLID",
      coverage: "Pokrytí", resolution: "Rozlišení", gate: "hradlo",
      hold_state: "DRŽENÍ", tamper_state: "SABOTÁŽ!",
      csi_restart: "Změna povolení CSI vyžaduje restart ESP. Restartovat teď?",
      calib_started: "Kalibrace zahájena (10s, neobývej místnost)",
      reset_confirm: "Resetovat idle baseline? CSI bude N sekund znovu sbírat vzorky.",
      no_events: "Žádné události", del_history: "Smazat celou historii?",
      noise_calib: "Spustit kalibraci šumu? (60s, nepohybujte se před senzorem)",
      tg_error: "Chyba", tg_unknown: "Neznámá",
      zone_name: "Název", zone_immediate: "🚨 Okamžité", zone_delay: "Zpoždění (ms)",
      zone_behavior: "Chování alarmu v zóně",
      zone_default: "Zóna", zones_saved: "Zóny uloženy", save_error: "Chyba při ukládání",
      starting: "Spouštím...", apply: "Použít",
      zone_added: "→ Zóna přidána, nezapomeň uložit!",
      gates_saved: "Hradla uložena", gates_error: "Chyba při ukládání hradel",
      preset_applied: "Předvolba nastavena", restarting: "Restartování...",
      enter_creds: "Zadejte uživatelské jméno a heslo", pass_mismatch: "Hesla se neshodují",
      pass_changed: "Heslo změněno", creds_changed: "Přihlašovací údaje změněny. Zařízení se restartuje.",
      conn_lost: "Spojení ztraceno",
      default_pass_warn: "⚠️ Výchozí heslo admin/admin — změňte v sekci Síť &amp; Cloud",
      net_section: "Ethernet síť", net_mode: "Režim",
      net_dhcp: "DHCP (automaticky)", net_static: "Statická IP",
      net_ip: "IP adresa", net_subnet: "Maska sítě",
      net_gateway: "Brána", net_dns: "DNS server",
      net_mac: "MAC", net_link: "Link",
      net_save: "💾 Uložit a restartovat",
      net_confirm: "Uložit síťové nastavení a restartovat zařízení? Ztratíš spojení pokud se IP změní.",
      net_ip_invalid: "Neplatná IP, brána nebo maska.",
      timeline_title: "TIMELINE UDÁLOSTÍ", total: "celkem",
      no_timeline: "Nedostatek dat pro timeline",
      tgen_mode: "Režim", tgen_port: "Cílový port", tgen_pps: "Paketů/s (PPS):",
      actions: "AKCE", config: "KONFIGURACE",
      not_enough: "Nedostatek dat.", static_label: "Statika",
      auto_calib: "📐 Auto-kalibrace prahu (10s)", reset_baseline: "♻️ Reset idle baseline",
      reconnect_wifi: "📶 Reconnect WiFi",
      csi_help: "<b>Auto-kalibrace:</b> 10s vzorkuje variance v klidu, nastaví práh = mean×1.5. Použij v prázdné místnosti.<br><b>Reset baseline:</b> vyčistí idle hodnoty. Po přesunu senzoru.<br><b>Reconnect WiFi:</b> restart asociace při RSSI dropech.",
      site_learning: "SITE LEARNING (dlouhodobé)",
      learn_status: "Stav učení", learn_elapsed: "Uplynulo / cíl",
      learn_samples: "Přijaté / zamítnuté (pohyb / radar)", learn_bssid_resets: "BSSID resety",
      learn_thr_est: "Odhad prahu", learn_duration: "Délka učení:",
      learn_start: "▶️ Spustit učení", learn_stop: "⏹ Zastavit", learn_clear: "🗑 Smazat model",
      learn_confirm_start: "Spustit site learning na {h}? Místnost by měla být prázdná.",
      learn_confirm_stop: "Zastavit probíhající učení?",
      learn_confirm_clear: "Smazat naučený model? Detekce se vrátí na tovární práh.",
      learn_idle: "Neaktivní", learn_running: "Běží", learn_done: "Dokončeno",
      learned_model: "NAUČENÝ MODEL", learned_ready: "Model připraven",
      learned_thr: "Naučený práh", learned_mean: "Mean variance",
      learned_std: "Std variance", learned_max: "Max variance", learned_samples: "Vzorků",
      learn_refresh: "EMA refresh samples",
      learn_help: "<b>Site learning:</b> dlouhodobé vzorkování variance v prázdné místnosti (doporučeno 24–72 h). Radar-gate (LD2412) odfiltruje statické lidi. Po dokončení se automaticky nastaví <code>threshold = mean + 3×std</code>. <b>Učení přežívá OTA flash</b> (uloženo v NVS). Smazat model = reset na tovární hodnoty.",
      ml_mlp: "ML (MLP 17→18→9→1)", ml_enabled_lbl: "ML povoleno",
      ml_motion_lbl: "ML stav", ml_prob_lbl: "Pravděpodobnost",
      ml_threshold_lbl: "ML práh (enter):",
      ml_help: "<b>MLP klasifikátor:</b> 17 featur (statistiky turbulence + fáze + DSER/PLCR) → 18→9→1 sigmoid. Trénováno na espectre datasetu (F1 = 0.852). Enter ≥ threshold, exit = threshold × 0.70, N/M smoothing 4/5 z 6 oken. Výstup jde do fusion jako 3. signál.",
      detection_src: "Zdroj detekce", fusion_enabled: "Fusion povoleno",
      enabled: "Povoleno",
      hysteresis: "Hystereze (exit multiplier):", window_size: "Velikost okna (vzorky):",
      pub_interval: "Interval publikace (ms):",
      gate_out_of_range: "mimo rozsah",
      tz_section: "ČAS &amp; ZÓNA", tz_preset: "Předvolba",
      tz_custom_std: "Standardní offset (s)", tz_custom_dst: "Letní čas (s)",
      tz_save: "💾 Uložit časovou zónu", tz_saved: "Časová zóna uložena",
      sched_section: "NAPLÁNOVÁNÍ", sched_arm: "Čas auto-armování",
      sched_disarm: "Čas auto-odarmování", sched_days: "Dny",
      sched_save: "💾 Uložit naplánování", sched_saved: "Naplánování uloženo",
      sched_hint: "Zařízení se sám (od)armuje dle nastavené časové zóny. Formát HH:MM, prázdné = vypnuto.",
      sched_auto_arm: "Auto-arm po nečinnosti (min, 0 = vyp)",
      dow_mo: "Po", dow_tu: "Út", dow_we: "St", dow_th: "Čt",
      dow_fr: "Pá", dow_sa: "So", dow_su: "Ne",
    },
    en: {
      title: "LD2412 Security", loading: "LOADING...", arm: "ARM", disarm: "DISARM",
      disarmed: "🔓 DISARMED", arming: "⏳ ARMING...", armed: "🔒 ARMED",
      pending: "⚠️ PENDING", triggered: "🚨 TRIGGERED",
      sensor_health: "Sensor Health", uart_state: "UART State", frame_rate: "Frame Rate",
      ram: "RAM (Free/Min)", chip_temp: "Chip Temperature", uptime: "Uptime",
      factory_reset_confirm: "Really perform radar factory reset?",
      tab_basic: "Basic", tab_security: "Security", tab_gates: "Gates",
      tab_network: "Network & Cloud", tab_zones: "Zones", tab_events: "Events", tab_csi: "CSI",
      device_name: "Device Name (mDNS)", hold_time: "Hold Time (ms)",
      move_sens: "Movement Sensitivity (%)", enable_diag: "Enable Diagnostics",
      radar_bt: "Radar Bluetooth (HLK app pairing)",
      radar_bt_warn: "⚠️ Leave OFF in deployed units — anyone in BT range can otherwise pair with the radar via the HLK mobile app. Enable only for diagnostics.",
      radar_bt_apply: "Radar will restart to apply the change.",
      antimask_title: "ANTI-MASKING (Tamper by Covering)", antimask_enable: "Enable silence alarm",
      timeout_sec: "Timeout (sec)",
      antimask_hint: "For warehouses, cabins, server rooms <b>DISABLE</b> — silence is normal.<br>For occupied spaces ENABLE — detects sensor covering.",
      loiter_title: "LOITERING (Suspicious Lingering)", loiter_enable: "Loitering notification",
      loiter_hint: "Alarm when someone stands &lt;2m from sensor longer than timeout.",
      hb_title: "HEARTBEAT (Periodic Report)",
      hb_hint: "0 = disabled, 4 = every 4 hours 'I'm OK' message.",
      pet_hint: "Filters small objects (cats, dogs) with low energy &lt;2m.",
      entry_delay: "Entry Delay (sec)", exit_delay: "Exit Delay (sec)",
      disarm_reminder: "Reminder \"Still DISARMED\"",
      hold_hint: "Duration after which radar reports 'no presence' without detection.",
      light_func: "Light Function", light_night: "Night mode (below threshold)", light_day: "Day mode (above threshold)",
      light_thr: "Light Threshold (0-255)", light_cur: "Current Light",
      light_hint: "OUT pin active only when light is below/above threshold.<br>Ideal for night security ('below threshold' mode).",
      movement: "Movement", static_: "Static",
      gate_legend: "Higher = more sensitive &middot; <span style='opacity:0.4'>gray = outside min/max range</span>",
      set_all: "Set all:", indoor: "Indoor", outdoor: "Outdoor", pets: "Pets",
      save_gates: "Save Gates", save_mqtt: "Save MQTT", save: "Save",
      username: "Username", new_pass: "New Password", change_pass: "Change Password",
      cfg_backup: "CONFIG BACKUP", cfg_export: "💾 Export", cfg_import: "📂 Import",
      cfg_import_confirm: "Replace all settings? Device will reboot.",
      cfg_import_ok: "Config imported, rebooting...",
      cfg_import_err: "Import failed: {err}",
      add_zone: "+ Add Zone", save_zones: "💾 Save Zones", learn_static: "📡 Learn Static",
      filter_all: "All", filter_alarm: "Alarm", filter_move: "Movement",
      filter_tamper: "Tamper", filter_hb: "Heartbeat", filter_sys: "System", filter_net: "Network",
      delete: "Delete", now: "now", load_more: "Load more...",
      csi_warn: "This firmware was not compiled with WiFi CSI support (missing <code>-D USE_CSI=1</code>). To enable, upload the <b>esp32_poe_csi</b> variant.",
      csi_active: "Active", csi_idle: "Idle baseline ready",
      ap_compat: "AP compatibility (HT LTF)", ap_ok: "OK", ap_incompat: "INCOMPATIBLE", ap_checking: "waiting for HT LTF…",
      ap_incompat_hint: "AP is not emitting 802.11n HT LTF frames (likely WiFi 6 hardware in n-mode). CSI is still passive but motion detection won't work. Use a pre-WiFi 6 AP.",
      wifi_ap: "WIFI AP (CSI)", wifi_ssid: "SSID", wifi_pass: "Password",
      wifi_ap_hint: "Switch AP for the CSI sensor. Saving requires reboot. Empty = fall back to compile-time default from <code>secrets.h</code>.",
      save_reboot: "💾 Save and reboot", wifi_use_default: "↩️ Compile-time default",
      wifi_ssid_empty: "SSID cannot be empty",
      wifi_save_confirm: "Save SSID '{ssid}' and reboot? If the password is wrong the ESP won't associate and a factory reset (GPIO0 5s) will be required.",
      wifi_saved_reboot: "Saved. Rebooting…",
      wifi_save_failed: "Save failed",
      wifi_reset_confirm: "Restore compile-time default SSID from secrets.h and reboot?",
      wifi_reset_reboot: "Reset. Rebooting…",
      motion_state: "Motion State", composite: "Composite Score",
      threshold_label: "Detection threshold (variance):",
      save_config: "Save Configuration", upload_fw: "Upload Firmware", saved: "Saved",
      fw_update_title: "Firmware Update",
      pull_ota_title: "Pull OTA from URL",
      pull_ota_help: "Device fetches the firmware itself from the given URL. HTTPS + redirects supported. Auth header optional (e.g. <code>Bearer &lt;token&gt;</code>).",
      pull_ota_btn: "⤓ Fetch & flash",
      pull_ota_running: "Fetching & flashing…",
      pull_ota_phase_idle: "—",
      pull_ota_phase_downloading: "Downloading…",
      pull_ota_phase_writing: "Writing firmware…",
      pull_ota_phase_success: "✅ Done, rebooting",
      pull_ota_phase_error: "❌ Error",
      pull_ota_no_url: "Enter URL to firmware .bin",
      ota_cold_label: "Reboot device before OTA (recommended for 100% success)",
      ota_cold_restart: "Rebooting device...", ota_waiting_reboot: "Waiting for device",
      ota_ready_uploading: "Device up, uploading...", ota_uploading: "Uploading firmware...",
      ota_cold_failed: "Device did not come back within 25 s — check power and retry",
      yes: "YES", no: "NO", no_collecting: "NO — collecting samples", motion: "MOTION", idle: "IDLE",
      coverage: "Coverage", resolution: "Resolution", gate: "gate",
      hold_state: "HOLD", tamper_state: "TAMPER!",
      csi_restart: "Changing CSI requires ESP restart. Restart now?",
      calib_started: "Calibration started (10s, keep room empty)",
      reset_confirm: "Reset idle baseline? CSI will re-collect samples for a few seconds.",
      no_events: "No events", del_history: "Delete entire history?",
      noise_calib: "Start noise calibration? (60s, do not move in front of sensor)",
      tg_error: "Error", tg_unknown: "Unknown",
      zone_name: "Name", zone_immediate: "🚨 Immediate", zone_delay: "Delay (ms)",
      zone_behavior: "Zone alarm behavior",
      zone_default: "Zone", zones_saved: "Zones saved", save_error: "Save error",
      starting: "Starting...", apply: "Apply",
      zone_added: "→ Zone added, don't forget to save!",
      gates_saved: "Gates saved", gates_error: "Error saving gates",
      preset_applied: "Preset applied", restarting: "Restarting...",
      enter_creds: "Enter username and password", pass_mismatch: "Passwords don't match",
      pass_changed: "Password changed", creds_changed: "Credentials changed. Device will restart.",
      conn_lost: "Connection lost",
      default_pass_warn: "⚠️ Default password admin/admin — change in Network &amp; Cloud",
      net_section: "Ethernet network", net_mode: "Mode",
      net_dhcp: "DHCP (automatic)", net_static: "Static IP",
      net_ip: "IP address", net_subnet: "Subnet mask",
      net_gateway: "Gateway", net_dns: "DNS server",
      net_mac: "MAC", net_link: "Link",
      net_save: "💾 Save & reboot",
      net_confirm: "Save network config and reboot the device? You'll lose connection if the IP changes.",
      net_ip_invalid: "Invalid IP, gateway or subnet.",
      timeline_title: "EVENT TIMELINE", total: "total",
      no_timeline: "Not enough data for timeline",
      tgen_mode: "Mode", tgen_port: "Target Port", tgen_pps: "Packets/s (PPS):",
      actions: "ACTIONS", config: "CONFIGURATION",
      not_enough: "Not enough data.", static_label: "Static",
      auto_calib: "📐 Auto-calibrate threshold (10s)", reset_baseline: "♻️ Reset idle baseline",
      reconnect_wifi: "📶 Reconnect WiFi",
      csi_help: "<b>Auto-calibration:</b> 10s variance sampling in idle, sets threshold = mean×1.5. Use in empty room.<br><b>Reset baseline:</b> clears idle values. After moving sensor.<br><b>Reconnect WiFi:</b> restarts association on RSSI drops.",
      site_learning: "SITE LEARNING (long-term)",
      learn_status: "Learning state", learn_elapsed: "Elapsed / target",
      learn_samples: "Accepted / rejected (motion / radar)", learn_bssid_resets: "BSSID resets",
      learn_thr_est: "Threshold estimate", learn_duration: "Learning duration:",
      learn_start: "▶️ Start learning", learn_stop: "⏹ Stop", learn_clear: "🗑 Clear model",
      learn_confirm_start: "Start site learning for {h}? Room should be empty.",
      learn_confirm_stop: "Stop ongoing learning?",
      learn_confirm_clear: "Clear learned model? Detection falls back to factory threshold.",
      learn_idle: "Idle", learn_running: "Running", learn_done: "Done",
      learned_model: "LEARNED MODEL", learned_ready: "Model ready",
      learned_thr: "Learned threshold", learned_mean: "Mean variance",
      learned_std: "Std variance", learned_max: "Max variance", learned_samples: "Samples",
      learn_refresh: "EMA refresh samples",
      learn_help: "<b>Site learning:</b> long-term variance sampling in an empty room (24–72 h recommended). LD2412 radar-gate rejects stationary humans. On completion, <code>threshold = mean + 3×std</code> is applied automatically. <b>Learned model survives OTA flash</b> (stored in NVS). Clearing the model resets to factory threshold.",
      ml_mlp: "ML (MLP 17→18→9→1)", ml_enabled_lbl: "ML enabled",
      ml_motion_lbl: "ML state", ml_prob_lbl: "Probability",
      ml_threshold_lbl: "ML threshold (enter):",
      ml_help: "<b>MLP classifier:</b> 17 features (turbulence stats + phase + DSER/PLCR) → 18→9→1 sigmoid. Trained on espectre dataset (F1 = 0.852). Enter ≥ threshold, exit = threshold × 0.70, N/M smoothing 4/5 of 6 windows. Output is fed into fusion as 3rd signal.",
      detection_src: "Detection source", fusion_enabled: "Fusion enabled",
      enabled: "Enabled",
      hysteresis: "Hysteresis (exit multiplier):", window_size: "Window size (samples):",
      pub_interval: "Publish interval (ms):",
      gate_out_of_range: "out of range",
      tz_section: "TIME &amp; TIMEZONE", tz_preset: "Preset",
      tz_custom_std: "Standard offset (s)", tz_custom_dst: "DST offset (s)",
      tz_save: "💾 Save timezone", tz_saved: "Timezone saved",
      sched_section: "SCHEDULE", sched_arm: "Auto-arm time",
      sched_disarm: "Auto-disarm time", sched_days: "Days",
      sched_save: "💾 Save schedule", sched_saved: "Schedule saved",
      sched_hint: "Device auto-arms/disarms according to the configured timezone. Format HH:MM, empty = disabled.",
      sched_auto_arm: "Auto-arm after idle (min, 0 = off)",
      dow_mo: "Mo", dow_tu: "Tu", dow_we: "We", dow_th: "Th",
      dow_fr: "Fr", dow_sa: "Sa", dow_su: "Su",
    }
  };
  let LANG = localStorage.getItem('lang') || 'cs';
  function t(k) { return (I18N[LANG] && I18N[LANG][k]) || (I18N.en[k]) || k; }
  function setLang(l) { LANG = l; localStorage.setItem('lang', l); applyLang(); }
  function applyLang() {
    document.querySelectorAll('[data-i18n]').forEach(el => {
      let k = el.getAttribute('data-i18n');
      if (el.tagName === 'INPUT') el.placeholder = t(k);
      else if (el.tagName === 'OPTION') el.textContent = t(k);
      else el.innerHTML = t(k);
    });
    document.querySelector('#lang_btn').textContent = LANG === 'cs' ? '🇬🇧 EN' : '🇨🇿 CZ';
    document.title = t('title');
  }
  </script>
</head>
<body onload="applyLang()">

  <h2>
    LD2412 <span style="font-size:0.6em; color:#666" id="fw_ver">...</span>
    <span id="sse_icon" class="icon" title="Realtime"></span>
    <span id="wifi_icon" class="icon" title="ETH"></span>
    <span id="mqtt_icon" class="icon" title="MQTT"></span>
    <button id="lang_btn" onclick="setLang(LANG==='cs'?'en':'cs')" style="width:auto; padding:2px 8px; font-size:0.7rem; background:#333; border-radius:4px; margin:0; min-height:auto">🇬🇧 EN</button>
  </h2>

  <div id="security_warning" style="background:#cf6679; color:black; padding:10px; border-radius:8px; margin-bottom:10px; display:none; text-align:center; font-weight:bold;">
    <span data-i18n="default_pass_warn">⚠️ Výchozí heslo admin/admin — změňte v sekci Síť &amp; Cloud</span>
  </div>

  <div class="grid">
    <!-- MAIN STATUS -->
    <div class="card">
        <div class="gauge">
            <div id="state_text" style="color:#888; font-weight:bold; letter-spacing:2px; margin-bottom:5px" data-i18n="loading">NAČÍTÁM...</div>
            <div id="alarm_badge" style="margin-bottom:8px; font-size:0.9rem; font-weight:bold; color:#888">---</div>
            <button id="btn_arm" onclick="toggleArm()" style="width:auto; padding:8px 20px; margin-bottom:10px; background:#3700b3" data-i18n="arm">STŘEŽIT</button>
            <div class="big-val" id="dist_val" style="color:var(--accent)">---</div>
            <div class="unit">DISTANCE (cm)</div>
            <svg class="spark" id="graph_dist"></svg>
        </div>
        <div style="display:flex; gap:10px">
            <div style="flex:1; text-align:center">
                <div style="color:#03dac6; font-weight:bold" id="mov_val">0%</div>
                <div class="unit" data-i18n="movement">POHYB</div>
                <svg class="spark" id="graph_mov" style="height:30px; stroke:#03dac6"></svg>
            </div>
            <div style="flex:1; text-align:center">
                <div style="color:#bb86fc; font-weight:bold" id="stat_val">0%</div>
                <div class="unit" data-i18n="static_">STATIKA</div>
                <svg class="spark" id="graph_stat" style="height:30px; stroke:#bb86fc"></svg>
            </div>
        </div>
    </div>

    <!-- HEALTH & STATS -->
    <div class="card">
        <div class="stat-row"><span data-i18n="sensor_health">Zdraví senzoru</span><span id="h_score" class="stat-val">---%</span></div>
        <div class="stat-row"><span data-i18n="uart_state">UART Stav</span><span id="h_uart">---</span></div>
        <div class="stat-row"><span data-i18n="frame_rate">Snímková frekvence</span><span id="h_fps">--- FPS</span></div>
        <div class="stat-row"><span>Comm Errors</span><span id="h_err" style="color:var(--warn)">0</span></div>
        <div class="stat-row"><span data-i18n="ram">RAM (Volná/Min)</span><span id="h_heap">--- / --- KB</span></div>
        <div class="stat-row"><span data-i18n="chip_temp">Teplota čipu</span><span id="h_temp">--- °C</span></div>
        <div class="stat-row"><span data-i18n="uptime">Doba běhu</span><span id="h_uptime">---</span></div>
        <div style="display:flex; gap:5px; margin-top:10px; flex-wrap: wrap;">
            <button class="sec" style="flex:1; min-width:80px;" onclick="api('radar/restart', {method:'POST'})">Restart Radar</button>
            <button class="sec" style="flex:1; min-width:80px;" onclick="if(confirm('Restart ESP?')) api('restart', {method:'POST'})">Restart ESP</button>
            <button class="warn" style="flex:1; min-width:80px;" onclick="if(confirm(t('factory_reset_confirm'))) api('radar/factory_reset', {method:'POST'})">Reset MW</button>
        </div>
    </div>

    <!-- CONTROLS -->
    <div class="card">
        <div class="tabs">
            <div class="tab active" onclick="tab(0)"data-i18n="tab_basic">Základní</div>
            <div class="tab" onclick="tab(1)"data-i18n="tab_security">Bezpečnost</div>
            <div class="tab" onclick="tab(2)"data-i18n="tab_gates">Hradla</div>
            <div class="tab" onclick="tab(3)"data-i18n="tab_network">Síť & Cloud</div>
            <div class="tab" onclick="tab(4)"data-i18n="tab_zones">Zóny</div>
            <div class="tab" onclick="tab(5)"data-i18n="tab_events">Historie</div>
            <div class="tab" onclick="tab(6)"data-i18n="tab_csi">WiFi CSI</div>
        </div>

        <!-- TAB 0: BASIC -->
        <div id="tab0">
            <div class="stat-row"><span data-i18n="device_name">Jméno zařízení (mDNS)</span></div>
            <div style="display:flex; gap:5px; margin-bottom:10px">
                <input type="text" id="txt_hostname" placeholder="e.g. sensor-room1">
                <button class="sec" style="width:auto; margin:0" onclick="saveHostname()">OK</button>
            </div>

            <div class="row-input">
                <span style="flex:1">Min Range (Gate)</span>
                <input type="number" id="i_min" min="0" max="13" style="width:60px" onchange="saveBasic()">
            </div>
            <div class="row-input">
                <span style="flex:1">Max Range (Gate)</span>
                <input type="number" id="i_max" min="1" max="13" style="width:60px" onchange="saveBasic()">
            </div>
            
            <div class="stat-row" style="margin-top:10px"><span data-i18n="hold_time">Doba držení (ms)</span></div>
            <input type="number" id="i_hold" step="1000" onchange="saveBasic()">
            
            <div class="stat-row" style="margin-top:10px"><span data-i18n="move_sens">Citlivost Pohyb (%)</span></div>
            <input type="number" id="i_sens" min="0" max="100" onchange="saveBasic()">

            <div style="display:flex; align-items:center; gap:8px; margin-top:15px; margin-bottom:5px">
                <input type="checkbox" id="chk_led" style="width:auto" onchange="saveBasic()">
                <label for="chk_led">Enable LED (Indicator)</label>
            </div>
            <div style="display:flex; align-items:center; gap:8px; margin-bottom:10px">
                <input type="checkbox" id="chk_eng" style="width:auto" onchange="toggleEng()">
                <label for="chk_eng" title="Enable detailed gate data (14 zones) and faster communication"><span data-i18n="enable_diag">Povolit Diagnostiku</span></label>
            </div>

            <div style="display:flex; align-items:center; gap:8px; margin-bottom:5px">
                <input type="checkbox" id="chk_radar_bt" style="width:auto" onchange="toggleRadarBt()">
                <label for="chk_radar_bt"><span data-i18n="radar_bt">Bluetooth radaru</span></label>
                <span id="radar_mac_val" style="margin-left:8px; font-size:0.75rem; color:#888; font-family:monospace"></span>
            </div>
            <p style="font-size:0.7rem; color:#c80; margin:2px 0 15px 0">
                <span data-i18n="radar_bt_warn">⚠️ Zapnout jen na dobu potřebnou pro diagnostiku HLK aplikací. Doporučeno vypnuto.</span>
            </p>

            <button id="btn_calib" onclick="startCalib()" style="margin-top:15px">Calibrate Noise (60s)</button>
        </div>

        <!-- TAB 1: SECURITY -->
        <div id="tab1" class="hidden">
            <div class="section-title"><span data-i18n="antimask_title">ANTI-MASKING (Sabotáž zakrytím)</span></div>
            <div style="display:flex; align-items:center; gap:8px; margin-bottom:5px">
                <input type="checkbox" id="chk_am_en" style="width:auto" onchange="saveSec()">
                <label for="chk_am_en"><span data-i18n="antimask_enable">Povolit alarm při tichu</span></label>
            </div>
            <div class="row-input">
                <span data-i18n="timeout_sec">Časový limit (sec)</span>
                <input type="number" id="i_am" placeholder="300" min="0" max="3600" step="5" title="0 = disabled / vypnuto (max 3600 s)" style="width:80px" onchange="saveSec()">
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0 15px 0">
                ⚠️ <span data-i18n="antimask_hint">Pro sklady, chaty, serverovny <b>VYPNĚTE</b> - ticho je tam normální.<br>
                Pro obývané prostory ZAPNĚTE - detekuje zakrytí sensoru.</span>
            </p>

            <div class="section-title"><span data-i18n="loiter_title">LOITERING (Podezřelé postávání)</span></div>
            <div style="display:flex; align-items:center; gap:8px; margin-bottom:5px">
                <input type="checkbox" id="chk_loit_en" style="width:auto" onchange="saveSec()">
                <label for="chk_loit_en"><span data-i18n="loiter_enable">Notifikace při postávání</span></label>
            </div>
            <div class="row-input">
                <span data-i18n="timeout_sec">Časový limit (sec)</span>
                <input type="number" id="i_loit" placeholder="15" min="0" max="3600" step="5" title="0 = disabled / vypnuto (max 3600 s)" style="width:80px" onchange="saveSec()">
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0 15px 0">
                <span data-i18n="loiter_hint">Alarm když někdo stojí &lt;2m od sensoru déle než timeout.</span>
            </p>

            <div class="section-title"><span data-i18n="hb_title">HEARTBEAT (Pravidelný report)</span></div>
            <div class="row-input">
                <span>Interval (h)</span>
                <input type="number" id="i_hb" placeholder="4" min="0" max="24" style="width:80px" onchange="saveSec()">
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0 15px 0">
                <span data-i18n="hb_hint">0 = vypnuto, 4 = každé 4 hodiny zpráva "jsem OK".</span>
            </p>

            <div class="section-title">PET IMMUNITY</div>
            <div class="row-input">
                <span>Min Move Energy</span>
                <input type="number" id="i_pet" placeholder="10" min="0" max="50" style="width:80px" onchange="saveSec()">
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0">
                <span data-i18n="pet_hint">Filtruje malé objekty (kočky, psi) s nízkou energií</span &lt;2m.
            </p>

            <div class="section-title">ALARM DELAY</div>
            <div class="row-input">
                <span data-i18n="entry_delay">Zpoždění vstupu (sec)</span>
                <input type="number" id="i_entry_dl" placeholder="30" min="0" max="300" style="width:80px" onchange="saveAlarmConfig()">
            </div>
            <div class="row-input">
                <span data-i18n="exit_delay">Zpoždění odchodu (sec)</span>
                <input type="number" id="i_exit_dl" placeholder="30" min="0" max="300" style="width:80px" onchange="saveAlarmConfig()">
            </div>
            <div style="display:flex; align-items:center; gap:8px; margin-bottom:5px">
                <input type="checkbox" id="chk_dis_rem" style="width:auto" onchange="saveAlarmConfig()">
                <label for="chk_dis_rem"><span data-i18n="disarm_reminder">Připomínka "Stále NESTŘEŽENO"</span></label>
            </div>

            <div class="section-title">ABSENCE TIMEOUT</div>
            <div class="row-input">
                <span>Unmanned Duration (sec)</span>
                <input type="number" id="i_timeout" placeholder="10" min="0" max="255" style="width:80px" onchange="saveTimeout()">
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0 15px 0">
                <span data-i18n="hold_hint">Doba, po které radar hlásí "nepřítomnost" bez detekce.</span>
            </p>

            <div class="section-title">LIGHT SENSOR (OUT pin)</div>
            <div class="row-input">
                <span data-i18n="light_func">Funkce světla</span>
                <select id="sel_light_func" style="width:140px" onchange="saveLightConfig()">
                    <option value="0">Off</option>
                    <option value="1"data-i18n="light_night">Noční režim (pod práh)</option>
                    <option value="2"data-i18n="light_day">Denní režim (nad práh)</option>
                </select>
            </div>
            <div class="row-input">
                <span data-i18n="light_thr">Práh světla (0-255)</span>
                <input type="number" id="i_light_thresh" placeholder="128" min="0" max="255" style="width:80px" onchange="saveLightConfig()">
            </div>
            <div class="row-input">
                <span data-i18n="light_cur">Aktuální světlo</span>
                <span id="cur_light_val" style="font-weight:bold">---</span>
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0 0 0">
                <span data-i18n="light_hint">OUT pin aktivní jen když je světlo pod/nad prahem.<br>
                Ideální pro noční zabezpečení (režim "pod práh").</span>
            </p>
        </div>

        <!-- TAB 2: GATES -->
        <div id="tab2" class="hidden">
            <div id="range_summary" style="font-size:0.8rem; color:#888; margin-bottom:8px; text-align:center"></div>

            <div style="font-size:0.75rem; color:#888; margin-bottom:8px; line-height:1.5">
                <span style="color:#03dac6; font-weight:bold">&#9632; Pohyb</span> = citlivost na pohybující se objekty#9632; <span data-i18n="movement">Pohyb</span></span> = move sensitivity &middot;
                <span style="color:#bb86fc; font-weight:bold">&#9632; Statika</span> = citlivost na nehybné objekty#9632; <span data-i18n="static_">Statika</span></span> = static sensitivity<br>
                Higher = more sensitive &middot; <span style="opacity:0.4">gray = outside min/max range</span>
            </div>

            <div style="background:#111; border-radius:8px; padding:8px; margin-bottom:8px">
                <div style="font-size:0.75rem; color:#888; margin-bottom:4px"data-i18n="set_all">Nastavit vše:</div>
                <div style="display:flex; gap:6px; align-items:center; flex-wrap:wrap">
                    <span style="color:#03dac6; font-size:0.75rem; width:12px">P</span>
                    <input type="range" class="mov-slider" id="g_m_all" value="50" min="0" max="100" style="flex:1; min-width:60px" oninput="$('lm_all').innerText=this.value">
                    <span id="lm_all" style="width:22px; color:#03dac6; font-size:0.75rem; text-align:right">50</span>
                    <span style="color:#bb86fc; font-size:0.75rem; width:12px; margin-left:4px">S</span>
                    <input type="range" class="stat-slider" id="g_s_all" value="30" min="0" max="100" style="flex:1; min-width:60px" oninput="$('ls_all').innerText=this.value">
                    <span id="ls_all" style="width:22px; color:#bb86fc; font-size:0.75rem; text-align:right">30</span>
                    <button class="sec" style="width:auto; padding:4px 10px; margin:0; font-size:0.75rem" onclick="setAllGates()">OK</button>
                </div>
            </div>

            <div style="display:flex; justify-content:space-between; margin-bottom:8px; gap:5px">
                <button class="sec" style="flex:1; padding:5px; font-size:0.8rem" onclick="setPreset('indoor')"data-i18n="indoor">Interiér</button>
                <button class="sec" style="flex:1; padding:5px; font-size:0.8rem" onclick="setPreset('outdoor')"data-i18n="outdoor">Exteriér</button>
                <button class="sec" style="flex:1; padding:5px; font-size:0.8rem" onclick="setPreset('pet')"data-i18n="pets">Zvířata</button>
            </div>

            <div id="gates_container" style="max-height:400px; overflow-y:auto"></div>

            <button onclick="saveGates()" style="margin-top:8px"data-i18n="save_gates">Uložit Hradla</button>
        </div>

        <!-- TAB 3: NETWORK & CLOUD -->
        <div id="tab3" class="hidden">
            <div class="section-title" data-i18n="net_section">Ethernet síť</div>
            <label style="font-size:0.85rem; color:#aaa" data-i18n="net_mode">Režim</label>
            <select id="net_mode_sel" onchange="onNetModeChange()" style="width:100%">
                <option value="dhcp" data-i18n="net_dhcp">DHCP (automaticky)</option>
                <option value="static" data-i18n="net_static">Statická IP</option>
            </select>
            <div id="net_static_fields" style="display:none">
                <input type="text" id="net_ip" placeholder="192.168.1.50" data-i18n="net_ip">
                <input type="text" id="net_subnet" placeholder="255.255.255.0" data-i18n="net_subnet">
                <input type="text" id="net_gateway" placeholder="192.168.1.1" data-i18n="net_gateway">
                <input type="text" id="net_dns" placeholder="8.8.8.8" data-i18n="net_dns">
            </div>
            <div style="font-size:0.75rem; color:#777; margin:6px 0">
                <span data-i18n="net_mac">MAC</span>: <span id="net_mac_lbl">—</span>
                &nbsp;·&nbsp; <span data-i18n="net_link">Link</span>: <span id="net_link_lbl">—</span>
            </div>
            <button onclick="saveNetworkConfig()" class="warn" data-i18n="net_save">💾 Uložit a restartovat</button>

            <div class="section-title" data-i18n="tz_section">ČAS &amp; ZÓNA</div>
            <label style="font-size:0.85rem; color:#aaa" data-i18n="tz_preset">Předvolba</label>
            <select id="tz_sel" onchange="onTzChange()" style="width:100%">
                <option value="0,0">UTC (0,0)</option>
                <option value="3600,3600">Europe/Prague (CET/CEST)</option>
                <option value="0,3600">Europe/London (GMT/BST)</option>
                <option value="3600,3600">Europe/Berlin (CET/CEST)</option>
                <option value="7200,3600">Europe/Athens (EET/EEST)</option>
                <option value="-18000,3600">America/New_York (EST/EDT)</option>
                <option value="-21600,3600">America/Chicago (CST/CDT)</option>
                <option value="-25200,3600">America/Denver (MST/MDT)</option>
                <option value="-28800,3600">America/Los_Angeles (PST/PDT)</option>
                <option value="32400,0">Asia/Tokyo (JST)</option>
                <option value="28800,0">Asia/Shanghai (CST)</option>
                <option value="19800,0">Asia/Kolkata (IST)</option>
                <option value="36000,3600">Australia/Sydney (AEST/AEDT)</option>
                <option value="custom">Custom…</option>
            </select>
            <div id="tz_custom_fields" style="display:none">
                <input type="number" id="tz_std_in" placeholder="Standard offset (s)" oninput="updateTzLabel()">
                <input type="number" id="tz_dst_in" placeholder="DST offset (s)" oninput="updateTzLabel()">
            </div>
            <div style="font-size:0.75rem; color:#777; margin:6px 0">
                UTC offset: <span id="tz_device_time">—</span>
            </div>
            <button onclick="saveTimezone()" class="sec" data-i18n="tz_save">💾 Uložit časovou zónu</button>

            <div class="section-title" data-i18n="sched_section">NAPLÁNOVÁNÍ</div>
            <div style="display:flex; gap:5px; align-items:center">
                <label style="flex:1; font-size:0.85rem; color:#aaa" data-i18n="sched_arm">Čas auto-armování</label>
                <input type="time" id="sched_arm_in" style="flex:1" placeholder="HH:MM">
            </div>
            <div style="display:flex; gap:5px; align-items:center">
                <label style="flex:1; font-size:0.85rem; color:#aaa" data-i18n="sched_disarm">Čas auto-odarmování</label>
                <input type="time" id="sched_disarm_in" style="flex:1" placeholder="HH:MM">
            </div>
            <div style="display:flex; gap:5px; align-items:center">
                <label style="flex:1; font-size:0.85rem; color:#aaa" data-i18n="sched_auto_arm">Auto-arm po nečinnosti (min, 0 = vyp)</label>
                <input type="number" id="sched_auto_arm_in" min="0" max="1440" style="flex:1" placeholder="0">
            </div>
            <div style="font-size:0.75rem; color:#777; margin:6px 0" data-i18n="sched_hint">Zařízení se sám (od)armuje dle nastavené časové zóny. Formát HH:MM, prázdné = vypnuto.</div>
            <button onclick="saveSchedule()" class="sec" data-i18n="sched_save">💾 Uložit naplánování</button>

            <div class="section-title">MQTT Broker</div>
            <div style="display:flex; align-items:center; gap:8px;">
                <input type="checkbox" id="chk_mqtt_en" style="width:auto">
                <label for="chk_mqtt_en">Enable MQTT</label>
            </div>
            <input type="text" id="txt_mqtt_server" placeholder="Server IP">
            <div style="display:flex; gap:5px">
                <input type="text" id="txt_mqtt_port" placeholder="Port (1883)">
                <input type="text" id="txt_mqtt_user" placeholder="Username">
            </div>
            <input type="password" id="txt_mqtt_pass" placeholder="Password">
            <button onclick="saveMQTTConfig()" class="sec"data-i18n="save_mqtt">Uložit MQTT</button>

            <div class="section-title">Telegram Notifications</div>
            <div style="display:flex; align-items:center; gap:8px;">
                <input type="checkbox" id="chk_tg_en" style="width:auto">
                <label for="chk_tg_en">Enable Bot</label>
            </div>
            <input type="text" id="txt_tg_token" placeholder="Bot Token">
            <input type="text" id="txt_tg_chat" placeholder="Chat ID">
            <div style="display:flex; gap:5px">
                <button onclick="saveTelegram()" class="sec"data-i18n="save">Uložit</button>
                <button onclick="testTelegram()" class="sec">Test</button>
            </div>
            <div class="section-title">CREDENTIALS</div>
            <input type="text" id="txt_auth_user" placeholder="Username">
            <input type="password" id="txt_auth_pass" placeholder="New Password">
            <input type="password" id="txt_auth_pass2" placeholder="Confirm Password">
            <button onclick="saveAuth()" class="warn"data-i18n="change_pass">Změnit heslo</button>

            <div class="section-title" data-i18n="cfg_backup">ZÁLOHA KONFIGURACE</div>
            <div style="display:flex; gap:5px">
              <button onclick="exportConfig()" class="sec" data-i18n="cfg_export">💾 Exportovat</button>
              <button onclick="importConfigPrompt()" class="sec" data-i18n="cfg_import">📂 Importovat</button>
            </div>
            <input type="file" id="cfg_import_file" accept="application/json" style="display:none" onchange="importConfig(this.files[0])">
        </div>

        <!-- TAB 4: ZONES -->
        <div id="tab4" class="hidden">
            <div class="label" style="margin-bottom:10px; font-size:0.8rem; color:#888">ZONE DEFINITIONS (cm)</div>
            <!-- SVG Zone Map -->
            <div style="position:relative; margin-bottom:10px">
                <svg id="zone_map" width="100%" height="48" style="display:block"></svg>
                <div style="position:absolute; bottom:2px; left:4px; font-size:0.65rem; color:#555" id="zone_map_scale"></div>
            </div>
            <div id="zones_list"></div>
            <button onclick="addZone()" class="sec" style="margin-top:10px"data-i18n="add_zone">+ Přidat Zónu</button>
            <button onclick="saveZones()"data-i18n="save_zones">💾 Uložit Zóny</button>
            <!-- Auto-learn -->
            <div style="background:#1a1a2e; border-radius:6px; padding:8px; margin-top:10px">
                <div style="display:flex; gap:6px; align-items:center; flex-wrap:wrap">
                    <select id="learn_dur" style="flex:1; min-width:120px">
                        <option value="60">1 min</option>
                        <option value="180" selected>3 min</option>
                        <option value="300">5 min</option>
                        <option value="600">10 min</option>
                        <option value="1800">30 min</option>
                        <option value="3600">60 min</option>
                        <option value="14400">4h</option>
                        <option value="28800">8h</option>
                    </select>
                    <button onclick="startLearn()" id="btn_learn" class="sec" style="flex:1"data-i18n="learn_static">📡 Naučit statiku</button>
                </div>
                <div id="learn_status" style="margin-top:6px; font-size:0.8rem; color:#888; display:none"></div>
            </div>
        </div>

        <!-- TAB 5: EVENT TIMELINE -->
        <div id="tab5" class="hidden">
            <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:8px; flex-wrap:wrap; gap:6px">
                <div class="section-title" style="margin:0; border:none"><span data-i18n="timeline_title">TIMELINE UDÁLOSTÍ</span></div>
                <div style="display:flex; gap:4px; align-items:center; flex-wrap:wrap">
                    <span id="evt_total" style="font-size:0.75rem; color:#666"></span>
                    <select id="evt_filter" onchange="loadEvents()" style="width:auto; padding:4px 8px; font-size:0.8rem">
                        <option value="-1"data-i18n="filter_all">Vše</option>
                        <option value="5"data-i18n="filter_alarm">Alarm</option>
                        <option value="1"data-i18n="filter_move">Pohyb</option>
                        <option value="2"data-i18n="filter_tamper">Tamper</option>
                        <option value="4"data-i18n="filter_hb">Heartbeat</option>
                        <option value="0"data-i18n="filter_sys">Systém</option>
                        <option value="3"data-i18n="filter_net">Síť</option>
                    </select>
                    <button onclick="exportEvents()" class="sec" style="width:auto; padding:4px 8px; margin:0; font-size:0.8rem">CSV</button>
                    <button onclick="clearEvents()" class="warn" style="width:auto; padding:4px 8px; margin:0; font-size:0.8rem"data-i18n="delete">Smazat</button>
                </div>
            </div>
            <!-- Timeline visual bar (last 24h density) -->
            <svg id="evt_timeline" viewBox="0 0 288 32" preserveAspectRatio="none"
                 style="width:100%; height:32px; background:#111; border-radius:4px; margin-bottom:8px">
            </svg>
            <div style="display:flex; justify-content:space-between; font-size:0.65rem; color:#555; margin:-4px 0 8px 0">
                <span>-24h</span><span>-12h</span><span data-i18n="now">nyní</span>
            </div>
            <!-- Event list -->
            <div id="evt_timeline_list" style="max-height:400px; overflow-y:auto"></div>
            <button id="evt_load_more" onclick="loadMoreEvents()" class="sec" style="display:none; margin-top:8px; font-size:0.85rem"data-i18n="load_more">Načíst další...</button>
        </div>

        <!-- TAB 6: WIFI CSI -->
        <div id="tab6" class="hidden">
            <div id="csi_compiled_warn" style="display:none; padding:10px; background:#3a1010; border-left:3px solid var(--warn); margin-bottom:12px; font-size:0.85rem">
                ⚠️ <span data-i18n="csi_warn">Tento firmware nebyl zkompilován s podporou WiFi CSI.</span>
            </div>

            <div class="section-title">STAV CSI</div>
            <div class="stat-row"><span data-i18n="csi_active">Aktivní</span><span id="csi_active_val" style="font-weight:bold">—</span></div>
            <div class="stat-row"><span>WiFi SSID</span><span id="csi_ssid_val">—</span></div>
            <div class="stat-row"><span>WiFi RSSI</span><span id="csi_rssi_val">—</span></div>
            <div class="stat-row"><span>Pakety/s</span><span id="csi_pps_val">—</span></div>
            <div class="stat-row"><span data-i18n="csi_idle">Idle baseline připraven</span><span id="csi_idle_val">—</span></div>
            <div class="stat-row"><span data-i18n="ap_compat">AP kompatibilita (HT LTF)</span><span id="csi_ap_compat" style="font-weight:bold">—</span></div>

            <div class="section-title">DETEKCE POHYBU</div>
            <div class="stat-row">
                <span data-i18n="motion_state">Stav pohybu</span>
                <span id="csi_motion_val" style="font-weight:bold; font-size:1.2rem">—</span>
            </div>
            <div class="stat-row"><span data-i18n="composite">Composite skóre</span><span id="csi_comp_val">—</span></div>
            <div class="stat-row"><span>Variance (window)</span><span id="csi_var_val">—</span></div>
            <svg id="csi_graph" viewBox="0 0 100 50" style="width:100%; height:60px; background:#1a1a1a; margin-top:5px; stroke:#03dac6"></svg>

            <div class="section-title">FUSION (Radar + CSI)</div>
            <div class="stat-row">
                <span>Fusion stav</span>
                <span id="fus_presence_val" style="font-weight:bold; font-size:1.2rem">—</span>
            </div>
            <div class="stat-row"><span>Confidence</span><span id="fus_conf_val">—</span></div>
            <div class="stat-row"><span data-i18n="detection_src">Zdroj detekce</span><span id="fus_source_val">—</span></div>
            <div class="stat-row">
                <span data-i18n="fusion_enabled">Fusion povoleno</span>
                <label class="switch"><input type="checkbox" id="fus_en" onchange="toggleFusion(this.checked)"><span class="slider"></span></label>
            </div>

            <div class="section-title"><span data-i18n="config">KONFIGURACE</span></div>

            <div class="stat-row">
                <span data-i18n="enabled">Povoleno</span>
                <label class="switch"><input type="checkbox" id="csi_en"><span class="slider"></span></label>
            </div>

            <label style="display:block; margin-top:10px; font-size:0.85rem; color:#aaa">
                <span data-i18n="threshold_label">Práh detekce (variance threshold):</span> <span id="csi_thr_lbl">0.50</span>
            </label>
            <input type="range" id="csi_thr" min="0.01" max="3.0" step="0.01" value="0.5"
                   oninput="$('csi_thr_lbl').innerText=parseFloat(this.value).toFixed(2)">

            <label style="display:block; margin-top:10px; font-size:0.85rem; color:#aaa">
                <span data-i18n="hysteresis">Hystereze (exit multiplier):</span> <span id="csi_hyst_lbl">0.70</span>
            </label>
            <input type="range" id="csi_hyst" min="0.30" max="0.95" step="0.01" value="0.7"
                   oninput="$('csi_hyst_lbl').innerText=parseFloat(this.value).toFixed(2)">

            <label style="display:block; margin-top:10px; font-size:0.85rem; color:#aaa">
                <span data-i18n="window_size">Velikost okna (vzorky):</span> <span id="csi_win_lbl">75</span>
            </label>
            <input type="range" id="csi_win" min="10" max="200" step="5" value="75"
                   oninput="$('csi_win_lbl').innerText=this.value">

            <label style="display:block; margin-top:10px; font-size:0.85rem; color:#aaa">
                <span data-i18n="pub_interval">Interval publikace (ms):</span> <span id="csi_pub_lbl">1000</span>
            </label>
            <input type="range" id="csi_pub" min="200" max="5000" step="100" value="1000"
                   oninput="$('csi_pub_lbl').innerText=this.value">

            <div class="section-title" style="margin-top:14px">TRAFFIC GENERATOR</div>
            <div class="stat-row">
                <span data-i18n="tgen_mode">Režim</span>
                <select id="csi_tmode" style="width:auto; padding:4px 8px" onchange="$('csi_udp_port_row').style.display=this.value==='udp'?'flex':'none'">
                    <option value="udp">UDP</option>
                    <option value="icmp">ICMP Ping</option>
                </select>
            </div>
            <div id="csi_udp_port_row" class="stat-row">
                <span data-i18n="tgen_port">Cílový port</span>
                <input type="number" id="csi_tport" value="7" min="1" max="65535" style="width:80px">
            </div>
            <label style="display:block; margin-top:8px; font-size:0.85rem; color:#aaa">
                <span data-i18n="tgen_pps">Paketů/s (PPS):</span> <span id="csi_pps_lbl">100</span>
            </label>
            <input type="range" id="csi_tpps" min="10" max="500" step="10" value="100"
                   oninput="$('csi_pps_lbl').innerText=this.value">

            <button onclick="saveCSIConfig()" style="margin-top:12px"data-i18n="save_config">Uložit konfiguraci</button>

            <div class="section-title" data-i18n="wifi_ap">WIFI AP (CSI)</div>
            <div style="font-size:0.75rem; color:#888; margin-bottom:6px" data-i18n="wifi_ap_hint">
                Přepnutí AP pro CSI senzor. Uložení vyžaduje reboot. Prázdné = fallback na compile-time default ze <code>secrets.h</code>.
            </div>
            <label style="font-size:0.85rem" data-i18n="wifi_ssid">SSID</label>
            <input type="text" id="csi_wifi_ssid" maxlength="32" placeholder="—" style="margin-bottom:6px">
            <label style="font-size:0.85rem" data-i18n="wifi_pass">Heslo</label>
            <input type="password" id="csi_wifi_pass" maxlength="64" placeholder="(nemění se pokud prázdné při edit)" style="margin-bottom:6px">
            <div style="display:flex; gap:6px; flex-wrap:wrap">
                <button onclick="saveCsiWifi()" class="warn" style="flex:2; min-width:140px" data-i18n="save_reboot">💾 Uložit a rebootovat</button>
                <button onclick="resetCsiWifi()" class="sec" style="flex:1; min-width:120px" data-i18n="wifi_use_default">↩️ Compile-time default</button>
            </div>

            <div class="section-title" data-i18n="actions">AKCE</div>
            <div style="display:flex; flex-wrap:wrap; gap:6px">
                <button class="sec" style="flex:1; min-width:120px" onclick="csiCalibrate()"data-i18n="auto_calib">📐 Auto-kalibrace prahu (10s)</button>
                <button class="sec" style="flex:1; min-width:120px" onclick="csiResetBaseline()"data-i18n="reset_baseline">♻️ Reset idle baseline</button>
                <button class="sec" style="flex:1; min-width:120px" onclick="csiReconnect()"data-i18n="reconnect_wifi">📶 Reconnect WiFi</button>
            </div>
            <div id="csi_calib_bar" style="display:none; height:6px; background:#333; margin-top:8px; border-radius:3px; overflow:hidden">
                <div id="csi_calib_fill" style="height:100%; width:0%; background:var(--accent); transition:width 0.3s"></div>
            </div>
            <div style="font-size:0.75rem; color:#777; margin-top:8px">
                <span data-i18n="csi_help"><b>Auto-kalibrace:</b> 10 sekund vzorkuje variance v klidu, nastaví práh = mean × 1.5. Použij když je v místnosti nikdo.<br>
                <b>Reset baseline:</b> vyčistí naučené idle hodnoty (turbulence, fáze). Po přesunu senzoru.<br>
                <b>Reconnect WiFi:</b> přerušení / RSSI dropy řeší restart asociace.</span>
            </div>

            <div class="section-title" style="margin-top:14px" data-i18n="site_learning">SITE LEARNING (dlouhodobé)</div>
            <div class="stat-row">
                <span data-i18n="learn_status">Stav učení</span>
                <span id="csi_learn_status" style="font-weight:bold">—</span>
            </div>
            <div class="stat-row">
                <span data-i18n="learn_elapsed">Uplynulo / cíl</span>
                <span id="csi_learn_elapsed">—</span>
            </div>
            <div style="height:6px; background:#333; margin-top:4px; border-radius:3px; overflow:hidden">
                <div id="csi_learn_fill" style="height:100%; width:0%; background:var(--accent); transition:width 0.3s"></div>
            </div>
            <div id="csi_learn_progress_lbl" style="display:none; text-align:right; font-size:0.75rem; color:#aaa; margin-top:3px">—</div>
            <div class="stat-row" style="margin-top:6px">
                <span data-i18n="learn_samples">Přijaté / zamítnuté (pohyb / radar)</span>
                <span id="csi_learn_samples">—</span>
            </div>
            <div class="stat-row">
                <span data-i18n="learn_bssid_resets">BSSID resety</span>
                <span id="csi_learn_bssid">—</span>
            </div>
            <div class="stat-row">
                <span data-i18n="learn_thr_est">Odhad prahu</span>
                <span id="csi_learn_thr_est">—</span>
            </div>

            <label style="display:block; margin-top:10px; font-size:0.85rem; color:#aaa">
                <span data-i18n="learn_duration">Délka učení:</span> <span id="csi_learn_dur_lbl">48 h</span>
            </label>
            <input type="range" id="csi_learn_dur" min="0.5" max="168" step="0.5" value="48"
                   oninput="$('csi_learn_dur_lbl').innerText=fmtLearnDur(this.value)">

            <div style="display:flex; flex-wrap:wrap; gap:6px; margin-top:8px">
                <button class="sec" style="flex:1; min-width:120px" onclick="csiStartLearning()" data-i18n="learn_start">▶️ Spustit učení</button>
                <button class="sec" style="flex:1; min-width:120px" onclick="csiStopLearning()" data-i18n="learn_stop">⏹ Zastavit</button>
                <button class="sec" style="flex:1; min-width:120px" onclick="csiClearLearned()" data-i18n="learn_clear">🗑 Smazat model</button>
            </div>

            <div class="section-title" style="margin-top:14px" data-i18n="learned_model">NAUČENÝ MODEL</div>
            <div class="stat-row"><span data-i18n="learned_ready">Model připraven</span><span id="csi_lm_ready">—</span></div>
            <div class="stat-row"><span data-i18n="learned_thr">Naučený práh</span><span id="csi_lm_thr">—</span></div>
            <div class="stat-row"><span data-i18n="learned_mean">Mean variance</span><span id="csi_lm_mean">—</span></div>
            <div class="stat-row"><span data-i18n="learned_std">Std variance</span><span id="csi_lm_std">—</span></div>
            <div class="stat-row"><span data-i18n="learned_max">Max variance</span><span id="csi_lm_max">—</span></div>
            <div class="stat-row"><span data-i18n="learned_samples">Vzorků</span><span id="csi_lm_samples">—</span></div>
            <div class="stat-row"><span data-i18n="learn_refresh">EMA refresh samples</span><span id="csi_lm_refresh">—</span></div>
            <div style="font-size:0.75rem; color:#777; margin-top:8px">
                <span data-i18n="learn_help"><b>Site learning:</b> dlouhodobé vzorkování variance v prázdné místnosti (doporučeno 24–72 h). Radar-gate (LD2412) odfiltruje statické lidi. Po dokončení se automaticky nastaví <code>threshold = mean + 3×std</code>. <b>Učení přežívá OTA flash</b> (uloženo v NVS). Smazat model = reset na tovární hodnoty.</span>
            </div>

            <div class="section-title" style="margin-top:14px" data-i18n="ml_mlp">ML (MLP 17→18→9→1)</div>
            <div class="stat-row">
                <span data-i18n="ml_enabled_lbl">ML povoleno</span>
                <label class="switch"><input type="checkbox" id="csi_ml_en" onchange="saveMLConfig()"><span class="slider"></span></label>
            </div>
            <div class="stat-row">
                <span data-i18n="ml_motion_lbl">ML stav</span>
                <span id="csi_ml_motion_val" style="font-weight:bold">—</span>
            </div>
            <div class="stat-row">
                <span data-i18n="ml_prob_lbl">Pravděpodobnost</span>
                <span id="csi_ml_prob_val">—</span>
            </div>
            <label style="display:block; margin-top:10px; font-size:0.85rem; color:#aaa">
                <span data-i18n="ml_threshold_lbl">ML práh (enter):</span> <span id="csi_ml_thr_lbl">0.50</span>
            </label>
            <input type="range" id="csi_ml_thr" min="0.05" max="0.95" step="0.01" value="0.50"
                   oninput="$('csi_ml_thr_lbl').innerText=parseFloat(this.value).toFixed(2)"
                   onchange="saveMLConfig()">
            <div style="font-size:0.75rem; color:#777; margin-top:8px">
                <span data-i18n="ml_help"><b>MLP klasifikátor:</b> 17 featur (statistiky turbulence + fáze + DSER/PLCR) → 18→9→1 sigmoid. Trénováno na espectre datasetu (F1 = 0.852). Enter ≥ threshold, exit = threshold × 0.70, N/M smoothing 4/5 z 6 oken. Výstup jde do fusion jako 3. signál.</span>
            </div>
        </div>
    </div>

    <!-- OTA -->
    <div class="card">
        <div class="stat-row"><span data-i18n="fw_update_title">Aktualizace FW</span></div>
        <input type="file" id="fw_file" accept=".bin">
        <label style="display:block; margin-top:6px; font-size:0.9em">
            <input type="checkbox" id="ota_cold_reboot" checked>
            <span data-i18n="ota_cold_label">Před OTA restartovat (doporučeno)</span>
        </label>
        <div id="ota_status" style="margin-top:4px; font-size:0.85em; color:#aaa; min-height:1.1em"></div>
        <div id="ota_bar" style="height:5px; background:#333; margin-top:5px; width:0%; transition:width 0.2s; background:var(--accent)"></div>
        <button id="btn_ota" onclick="uploadFW()" data-i18n="upload_fw">Nahrát Firmware</button>

        <hr style="border:none; border-top:1px solid #333; margin:14px 0 10px">
        <div class="stat-row"><span data-i18n="pull_ota_title">Pull OTA z URL</span></div>
        <div style="font-size:0.75rem; color:#777; margin-bottom:6px">
            <span data-i18n="pull_ota_help">ESP si stáhne firmware sám z dané URL. Funguje s HTTPS i přesměrováním. Auth header je volitelný (např. <code>Bearer &lt;token&gt;</code>).</span>
        </div>
        <input type="text" id="pull_url" placeholder="https://example.com/firmware.bin" style="width:100%; box-sizing:border-box; margin-bottom:6px">
        <input type="text" id="pull_auth" placeholder="Bearer …" style="width:100%; box-sizing:border-box; margin-bottom:6px">
        <div id="pull_status" style="margin-top:4px; font-size:0.85em; color:#aaa; min-height:1.1em"></div>
        <button id="btn_pull" onclick="pullFW()" class="sec" data-i18n="pull_ota_btn">⤓ Stáhnout a flashnout</button>
    </div>
  </div>

  <div id="toast" data-i18n="saved">Uloženo</div>

<script>
// --- CORE ---
const $ = id => document.getElementById(id);
const api = (ep, opts={}) => {
    // ESPAsyncWebServer hasParam() only checks query params, not POST body
    // Convert URLSearchParams body to query string automatically
    if(opts.body instanceof URLSearchParams) {
        ep += (ep.includes('?') ? '&' : '?') + opts.body.toString();
        delete opts.body;
    }
    return fetch('/api/'+ep, opts).then(r => {
        if(r.ok) showToast("OK"); else showToast("Chyba");
        return r;
    });
};
function showToast(msg) { $('toast').innerText=msg; $('toast').style.opacity=1; setTimeout(()=>$('toast').style.opacity=0, 2000); }

// --- DATA STREAM ---
let histDist = new Array(60).fill(0);
let histMov = new Array(60).fill(0);
let histStat = new Array(60).fill(0);
let histCsiComp = new Array(60).fill(0);
let zones = [];
let gateResolution = 0.75, cfgMinGate = 0, cfgMaxGate = 13;

let evtSource = null;
let reconnectTimeout = null;

function connectSSE() {
    if (evtSource) {
        evtSource.close();
    }

    evtSource = new EventSource('/events');

    evtSource.addEventListener('telemetry', e => {
        const d = JSON.parse(e.data);
        updateUI(d);
        if(d.alarm_state) { alarmArmed = d.armed; updateAlarmUI(d.alarm_state); }
        if(d.gate_move && !$('tab2').classList.contains('hidden')) updateGatesUI(d);
        if(d.csi) updateCSIUI(d.csi);
    });

    evtSource.onerror = () => {
        console.log('SSE connection lost, reconnecting in 3s...');
        $('sse_icon').className = 'icon err';
        $('sse_icon').title = t('conn_lost');
        evtSource.close();
        if (reconnectTimeout) clearTimeout(reconnectTimeout);
        reconnectTimeout = setTimeout(connectSSE, 3000);
    };

    evtSource.onopen = () => {
        console.log('SSE connected');
        $('sse_icon').className = 'icon ok';
        $('sse_icon').title = 'Realtime OK';
    };
}

function init() {
    // SSE Connection with auto-reconnect
    connectSSE();
    
    // Initial Load
    fetch('/api/version').then(r=>r.text()).then(v => $('fw_ver').innerText = v);
    
    fetch('/api/health').then(r=>r.json()).then(d => {
        if(d.is_default_pass) $('security_warning').style.display = 'block';
        if(d.auth_user) $('txt_auth_user').value = d.auth_user;
        if(d.hostname) $('txt_hostname').value = d.hostname;
        updateHealth(d);
    });
    
    // Load Configs
    loadMainConfig();
    loadSecurityConfig();
    loadMQTTConfig();
    loadTelegramConfig();
    loadZones();
    loadAlarmStatus();

    initCollapsible();
    
    setInterval(() => fetch('/api/health').then(r=>r.json()).then(updateHealth), 5000);
}

function loadMainConfig() {
    fetch('/api/config').then(r=>r.json()).then(d => {
        $('i_max').value = d.max_gate;
        if(d.min_gate !== undefined) $('i_min').value = d.min_gate;
        $('i_hold').value = d.hold_time;
        if(d.led_en !== undefined) $('chk_led').checked = d.led_en;
        if(d.eng_mode !== undefined) $('chk_eng').checked = d.eng_mode;
        if(d.mov_sens && d.mov_sens.length > 0) $('i_sens').value = d.mov_sens[0]; // Display first gate sens as general
        if(d.resolution) gateResolution = d.resolution;
        if(d.min_gate !== undefined) cfgMinGate = d.min_gate;
        if(d.max_gate !== undefined) cfgMaxGate = d.max_gate;

        // Range summary
        let minDist = (cfgMinGate * gateResolution * 100).toFixed(0);
        let maxDist = (cfgMaxGate * gateResolution * 100).toFixed(0);
        $('range_summary').innerHTML = `${t('coverage')}: <b>${minDist}cm – ${maxDist}cm</b> &middot; ${t('resolution')}: ${gateResolution}m/${t('gate')}`;

        // Gate Sliders (no energy bars — eng mode broken on V1.26)
        renderGateSliders(d.mov_sens, d.stat_sens);
    });
}

function updateUI(d) {
    // Sparklines
    histDist.push(d.distance_mm/10); histDist.shift();
    histMov.push(d.moving_energy); histMov.shift();
    histStat.push(d.static_energy); histStat.shift();
    
    drawSpark('graph_dist', histDist, 400); // max 400cm
    drawSpark('graph_mov', histMov, 100);
    drawSpark('graph_stat', histStat, 100);
    
    // Values
    $('dist_val').innerText = (d.distance_mm/10).toFixed(0);
    $('mov_val').innerText = d.moving_energy + '%';
    $('stat_val').innerText = d.static_energy + '%';
    
    let st = "KLID";
    let stColor = "#888";
    if(d.state === "detected") { st = "DETEKCE"; stColor = "var(--accent)"; }
    else if(d.state === "hold") { st = t('hold_state'); stColor = "#bb86fc"; }
    if(d.tamper) { st = t('tamper_state'); stColor = "var(--warn)"; }
    $('state_text').innerText = st;
    $('state_text').style.color = stColor;
    drawZoneMap(d.raw_stat_dist, d.raw_mov_dist);
}

function renderGateSliders(mov, stat) {
    let h = '';
    let oorTitle = t('gate_out_of_range');
    for(let i=0; i<14; i++) {
        let dist = Math.round(i * gateResolution * 100);
        let active = (i >= cfgMinGate && i <= cfgMaxGate);
        let dimClass = active ? '' : ' gate-dimmed';
        let titleAttr = active ? '' : ` title="${oorTitle}"`;
        let m = mov ? mov[i] : 50;
        let s = stat ? stat[i] : 30;
        h += `<div class="gate-wrapper${dimClass}"${titleAttr}>
            <div class="gate-label" style="width:65px; white-space:nowrap">G${i} <span style="color:#666">(${dist}cm)</span></div>
            <input type="range" class="mov-slider" id="g_m_${i}" value="${m}" min="0" max="100" title="Pohyb G${i}" oninput="$('lm_${i}').innerText=this.value" style="flex:1">
            <span id="lm_${i}" style="width:22px; text-align:right; color:#03dac6; font-size:0.75rem">${m}</span>
            <input type="range" class="stat-slider" id="g_s_${i}" value="${s}" min="0" max="100" title="Statika G${i}" oninput="$('ls_${i}').innerText=this.value" style="flex:1">
            <span id="ls_${i}" style="width:22px; text-align:right; color:#bb86fc; font-size:0.75rem">${s}</span>
        </div>`;
    }
    $('gates_container').innerHTML = h;
}

function setAllGates() {
    let m = $('g_m_all').value, s = $('g_s_all').value;
    for(let i=0; i<14; i++) {
        let el_m = $(`g_m_${i}`), el_s = $(`g_s_${i}`);
        if(el_m) { el_m.value = m; $(`lm_${i}`).innerText = m; }
        if(el_s) { el_s.value = s; $(`ls_${i}`).innerText = s; }
    }
}

function updateGatesUI(d) {
    // No-op: energy bars removed (eng mode broken on V1.26 FW)
}

function updateHealth(d) {
    let ethOk = d.eth_link || (d.ethernet && d.ethernet.link_up);
    $('wifi_icon').className = "icon " + (ethOk ? "ok" : "err");
    $('mqtt_icon').className = "icon " + (d.mqtt && d.mqtt.connected ? "ok" : "err");
    $('h_score').innerText = d.health_score + "%";
    $('h_uart').innerText = d.uart_state;
    $('h_fps').innerText = d.frame_rate.toFixed(1) + " FPS";
    $('h_err').innerText = d.error_count;
    $('h_heap').innerText = (d.free_heap/1024).toFixed(1) + " / " + (d.min_heap/1024).toFixed(1) + " KB";
    if (d.chip_temp != null) $('h_temp').innerText = d.chip_temp.toFixed(1) + " °C";
    let u = d.uptime;
    $('h_uptime').innerText = Math.floor(u/3600) + "h " + Math.floor((u%3600)/60) + "m";
}

// --- GRAPHS ---
function drawSpark(id, data, max) {
    const el = $(id);
    let pts = "";
    const w = 100 / (data.length - 1);
    data.forEach((v, i) => {
        const y = 50 - (Math.min(v, max) / max * 50);
        pts += `${i * w},${y} `;
    });
    el.innerHTML = `<polyline points="${pts}" style="fill:none;stroke:inherit;stroke-width:2" />`;
}

// --- ACTIONS ---
function initCollapsible() {
    document.querySelectorAll('.section-title').forEach(el => {
        el.style.cursor = 'pointer';
        // Add icon/indicator
        el.innerHTML += ' <span style="font-size:0.8em; float:right">▼</span>';
        
        el.onclick = () => {
            let next = el.nextElementSibling;
            while(next && !next.classList.contains('section-title')) {
                next.style.display = next.style.display === 'none' ? '' : 'none';
                next = next.nextElementSibling;
            }
        };
    });
}

function tab(n) {
    ['tab0','tab1','tab2','tab3','tab4','tab5','tab6'].forEach((id, i) => {
        $(id).classList.toggle('hidden', i !== n);
        document.querySelectorAll('.tab')[i].classList.toggle('active', i === n);
    });
    // Refresh config when switching tabs
    if(n===1) { loadSecurityConfig(); loadAlarmStatus(); }
    if(n===2) loadMainConfig();
    if(n===3) { loadNetworkConfig(); loadTimezoneConfig(); loadScheduleConfig(); loadMQTTConfig(); loadTelegramConfig(); }
    if(n===5) loadEvents();
    if(n===6) loadCSIConfig();
}

// --- WIFI CSI ---
function loadCSIConfig() {
    loadCsiWifi();
    fetch('/api/csi').then(r=>r.json()).then(d => {
        // Compiled-in check
        $('csi_compiled_warn').style.display = d.compiled ? 'none' : 'block';
        if (!d.compiled) return;

        // Config sliders
        $('csi_en').checked = !!d.enabled;
        if (d.threshold !== undefined) {
            $('csi_thr').value = d.threshold;
            $('csi_thr_lbl').innerText = parseFloat(d.threshold).toFixed(2);
        }
        if (d.hysteresis !== undefined) {
            $('csi_hyst').value = d.hysteresis;
            $('csi_hyst_lbl').innerText = parseFloat(d.hysteresis).toFixed(2);
        }
        if (d.window !== undefined) {
            $('csi_win').value = d.window;
            $('csi_win_lbl').innerText = d.window;
        }
        if (d.publish_ms !== undefined) {
            $('csi_pub').value = d.publish_ms;
            $('csi_pub_lbl').innerText = d.publish_ms;
        }

        // Traffic gen config
        if (d.traffic_icmp !== undefined) {
            $('csi_tmode').value = d.traffic_icmp ? 'icmp' : 'udp';
            $('csi_udp_port_row').style.display = d.traffic_icmp ? 'none' : 'flex';
        }
        if (d.traffic_port !== undefined) $('csi_tport').value = d.traffic_port;
        if (d.traffic_pps !== undefined) {
            $('csi_tpps').value = d.traffic_pps;
            $('csi_pps_lbl').innerText = d.traffic_pps;
        }

        // Live status
        $('csi_active_val').innerText = d.active ? 'ANO' : 'NE';
        $('csi_active_val').style.color = d.active ? 'var(--accent)' : '#888';
        $('csi_ssid_val').innerText  = d.wifi_ssid || '—';
        $('csi_rssi_val').innerText  = (d.wifi_rssi !== undefined && d.wifi_rssi !== 0) ? (d.wifi_rssi + ' dBm') : '—';
        $('csi_pps_val').innerText   = (d.pps !== undefined) ? d.pps.toFixed(1) : '—';
        $('csi_idle_val').innerText  = d.idle_ready ? t('yes') : t('no_collecting');
        if (d.ht_ltf_seen !== undefined) {
            const el = $('csi_ap_compat');
            el.title = '';
            if (!d.active) {
                el.innerText = '—'; el.style.color = '';
            } else if (d.ht_ltf_seen) {
                el.innerText = t('ap_ok'); el.style.color = 'var(--accent)';
            } else if ((d.packets || 0) === 0) {
                el.innerText = t('ap_checking'); el.style.color = '#888';
            } else {
                el.innerText = t('ap_incompat');
                el.style.color = '#e05252';
                el.title = t('ap_incompat_hint');
            }
        }

        if (d.motion !== undefined) {
            $('csi_motion_val').innerText = d.motion ? t('motion') : t('idle');
            $('csi_motion_val').style.color = d.motion ? 'var(--accent)' : '#888';
        }
        if (d.composite !== undefined) $('csi_comp_val').innerText = d.composite.toFixed(4);
        if (d.variance !== undefined) $('csi_var_val').innerText = d.variance.toFixed(4);

        // Fusion
        $('fus_en').checked = !!d.fusion_enabled;
        if (d.fusion) {
            updateFusionUI(d.fusion);
        }

        // Site learning + learned model
        updateLearningUI(d);

        // ML MLP
        if (d.ml_enabled !== undefined) $('csi_ml_en').checked = !!d.ml_enabled;
        if (d.ml_threshold !== undefined) {
            $('csi_ml_thr').value = d.ml_threshold;
            $('csi_ml_thr_lbl').innerText = parseFloat(d.ml_threshold).toFixed(2);
        }
        updateMLUI(d);
    });
}

function fmtDurationSec(s) {
    if (!s || s < 0) return '—';
    let h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), ss = s % 60;
    if (h > 0) return h + 'h ' + m + 'm';
    if (m > 0) return m + 'm ' + ss + 's';
    return ss + 's';
}

function updateLearningUI(d) {
    if (d.learning_active === undefined) return;
    let active = !!d.learning_active;
    let done = !active && !!d.model_ready;
    $('csi_learn_status').innerText = active ? t('learn_running') : (done ? t('learn_done') : t('learn_idle'));
    $('csi_learn_status').style.color = active ? 'var(--accent)' : (done ? '#4caf50' : '#888');
    let elapsed = d.learning_elapsed_s || 0;
    let target  = d.learning_duration_s || 0;
    $('csi_learn_elapsed').innerText = fmtDurationSec(elapsed) + ' / ' + fmtDurationSec(target);
    let pct = (d.learning_progress !== undefined) ? d.learning_progress : 0;
    if (pct > 100) pct = 100;
    $('csi_learn_fill').style.width = pct + '%';
    let progLbl = $('csi_learn_progress_lbl');
    if (active && target > 0) {
        progLbl.innerText = fmtLearnDur(elapsed / 3600) + ' / ' + fmtLearnDur(target / 3600);
        progLbl.style.display = 'block';
    } else {
        progLbl.style.display = 'none';
    }
    let acc = d.learning_samples || 0;
    let rm  = d.learning_rejected_motion || 0;
    let rr  = d.learning_rejected_radar  || 0;
    $('csi_learn_samples').innerText = acc + ' / ' + rm + ' / ' + rr;
    $('csi_learn_bssid').innerText   = (d.learning_bssid_resets !== undefined) ? d.learning_bssid_resets : '—';
    $('csi_learn_thr_est').innerText = (d.learning_threshold_estimate !== undefined) ? parseFloat(d.learning_threshold_estimate).toFixed(4) : '—';

    $('csi_lm_ready').innerText   = d.model_ready ? t('yes') : t('no');
    $('csi_lm_ready').style.color = d.model_ready ? 'var(--accent)' : '#888';
    $('csi_lm_thr').innerText     = (d.learned_threshold     !== undefined) ? parseFloat(d.learned_threshold).toFixed(4)     : '—';
    $('csi_lm_mean').innerText    = (d.learned_mean_variance !== undefined) ? parseFloat(d.learned_mean_variance).toFixed(4) : '—';
    $('csi_lm_std').innerText     = (d.learned_std_variance  !== undefined) ? parseFloat(d.learned_std_variance).toFixed(4)  : '—';
    $('csi_lm_max').innerText     = (d.learned_max_variance  !== undefined) ? parseFloat(d.learned_max_variance).toFixed(4)  : '—';
    $('csi_lm_samples').innerText = (d.learned_samples       !== undefined) ? d.learned_samples : '—';
    $('csi_lm_refresh').innerText = (d.learn_refresh_count   !== undefined) ? d.learn_refresh_count : '—';
}

function updateMLUI(d) {
    if (d.ml_motion !== undefined) {
        $('csi_ml_motion_val').innerText = d.ml_motion ? t('motion') : t('idle');
        $('csi_ml_motion_val').style.color = d.ml_motion ? 'var(--accent)' : '#888';
    }
    if (d.ml_probability !== undefined) {
        $('csi_ml_prob_val').innerText = (d.ml_probability * 100).toFixed(1) + '%';
    }
}

function updateCSIUI(csi) {
    // Called from SSE telemetry handler with the `csi` sub-object
    if (!csi) return;
    if ($('tab6').classList.contains('hidden')) {
        // Tab not visible — only push history so sparkline keeps animating
        histCsiComp.push(csi.composite || 0); histCsiComp.shift();
        return;
    }
    histCsiComp.push(csi.composite || 0); histCsiComp.shift();
    drawSpark('csi_graph', histCsiComp, 2.0);

    if (csi.motion !== undefined) {
        $('csi_motion_val').innerText = csi.motion ? 'POHYB' : 'KLID';
        $('csi_motion_val').style.color = csi.motion ? 'var(--accent)' : '#888';
    }
    if (csi.composite !== undefined) $('csi_comp_val').innerText = csi.composite.toFixed(4);
    if (csi.variance !== undefined)  $('csi_var_val').innerText  = csi.variance.toFixed(4);
    if (csi.pps !== undefined)       $('csi_pps_val').innerText  = csi.pps.toFixed(1);
    if (csi.rssi !== undefined && csi.rssi !== 0) $('csi_rssi_val').innerText = csi.rssi + ' dBm';

    // Calibration progress bar
    if (csi.calibrating) {
        $('csi_calib_bar').style.display = 'block';
        $('csi_calib_fill').style.width = ((csi.calib_pct || 0) * 100) + '%';
    } else {
        $('csi_calib_bar').style.display = 'none';
    }

    // ML live update
    updateMLUI(csi);

    // Site learning live update (progress bar + elapsed/samples while running)
    if (csi.learning_active) {
        updateLearningUI(csi);
    }

    // Fusion live update (nested in SSE csi object)
    if (csi.fusion) updateFusionUI(csi.fusion);
}

function updateFusionUI(f) {
    if (!f) return;
    let srcLabels = {none:'—', radar:'Radar', csi:'CSI', both:'Radar + CSI'};
    $('fus_presence_val').innerText = f.presence ? 'DETEKCE' : 'KLID';
    $('fus_presence_val').style.color = f.presence ? 'var(--warn)' : '#888';
    $('fus_conf_val').innerText = (f.confidence !== undefined) ? (f.confidence * 100).toFixed(0) + '%' : '—';
    $('fus_source_val').innerText = srcLabels[f.source] || f.source || '—';
}

function toggleFusion(en) {
    let p = new URLSearchParams();
    p.append('fusion_enabled', en ? '1' : '0');
    api('csi', {method:'POST', body:p}).then(r=>r.json()).then(d => {
        showToast(en ? 'Fusion zapnut' : 'Fusion vypnut');
        if (!en) {
            $('fus_presence_val').innerText = '—';
            $('fus_conf_val').innerText = '—';
            $('fus_source_val').innerText = '—';
        }
    });
}

function saveCSIConfig() {
    let p = new URLSearchParams();
    p.append('enabled',    $('csi_en').checked ? '1' : '0');
    p.append('threshold',  $('csi_thr').value);
    p.append('hysteresis', $('csi_hyst').value);
    p.append('window',     $('csi_win').value);
    p.append('publish_ms', $('csi_pub').value);
    p.append('traffic_icmp', $('csi_tmode').value === 'icmp' ? '1' : '0');
    p.append('traffic_port', $('csi_tport').value);
    p.append('traffic_pps', $('csi_tpps').value);
    api('csi', {method:'POST', body:p}).then(r=>r.json()).then(d => {
        if (d.needs_restart) {
            if (confirm(t('csi_restart'))) {
                api('restart', {method:'POST'});
            }
        }
    });
}

function csiCalibrate() {
    api('csi/calibrate', {method:'POST'}).then(r => {
        if (r.ok) showToast(t('calib_started'));
    });
}

function csiResetBaseline() {
    if (!confirm(t('reset_confirm'))) return;
    api('csi/reset_baseline', {method:'POST'});
}

function csiReconnect() {
    api('csi/reconnect', {method:'POST'});
}

function loadCsiWifi() {
    api('csi/wifi').then(r => r.json()).then(d => {
        if (d && d.ssid) $('csi_wifi_ssid').value = d.ssid;
        $('csi_wifi_pass').value = '';
    }).catch(() => {});
}

function saveCsiWifi() {
    let ssid = $('csi_wifi_ssid').value.trim();
    let pass = $('csi_wifi_pass').value;
    if (!ssid) { showToast(t('wifi_ssid_empty')); return; }
    if (!confirm(t('wifi_save_confirm').replace('{ssid}', ssid))) return;
    let p = new URLSearchParams();
    p.append('ssid', ssid);
    p.append('pass', pass);
    api('csi/wifi', {method:'POST', body:p}).then(r => r.json()).then(d => {
        if (d && d.saved) {
            showToast(t('wifi_saved_reboot'));
            setTimeout(() => api('restart', {method:'POST'}), 1500);
        } else {
            showToast(t('wifi_save_failed'));
        }
    }).catch(() => showToast(t('wifi_save_failed')));
}

function resetCsiWifi() {
    if (!confirm(t('wifi_reset_confirm'))) return;
    let p = new URLSearchParams();
    p.append('reset', '1');
    api('csi/wifi', {method:'POST', body:p}).then(r => r.json()).then(d => {
        if (d && d.saved) {
            showToast(t('wifi_reset_reboot'));
            setTimeout(() => api('restart', {method:'POST'}), 1500);
        }
    });
}

function fmtLearnDur(h) {
    h = parseFloat(h);
    if (h < 1) return Math.round(h * 60) + ' min';
    let whole = Math.floor(h);
    let mins = Math.round((h - whole) * 60);
    return mins ? (whole + ' h ' + mins + ' min') : (whole + ' h');
}
function csiStartLearning() {
    let h = $('csi_learn_dur').value;
    let label = fmtLearnDur(h);
    if (!confirm(t('learn_confirm_start').replace('{h}', label))) return;
    let p = new URLSearchParams();
    p.append('duration_h', h);
    api('csi/site_learning', {method:'POST', body:p}).then(() => {
        setTimeout(loadCSIConfig, 500);
    });
}

function csiStopLearning() {
    if (!confirm(t('learn_confirm_stop'))) return;
    let p = new URLSearchParams();
    p.append('stop', '1');
    api('csi/site_learning', {method:'POST', body:p}).then(() => {
        setTimeout(loadCSIConfig, 500);
    });
}

function csiClearLearned() {
    if (!confirm(t('learn_confirm_clear'))) return;
    let p = new URLSearchParams();
    p.append('clear_model', '1');
    api('csi/site_learning', {method:'POST', body:p}).then(() => {
        setTimeout(loadCSIConfig, 500);
    });
}

function saveMLConfig() {
    let p = new URLSearchParams();
    p.append('ml_enabled',   $('csi_ml_en').checked ? '1' : '0');
    p.append('ml_threshold', $('csi_ml_thr').value);
    api('csi', {method:'POST', body:p});
}

// --- Event Timeline ---
const EVT_TYPES = {
    0: {name:'SYS', color:'#888', icon:'⚙️'},
    1: {name:'MOV', color:'#03dac6', icon:'👤'},
    2: {name:'TMP', color:'#cf6679', icon:'🚨'},
    3: {name:'NET', color:'#bb86fc', icon:'🌐'},
    4: {name:'HB',  color:'#4caf50', icon:'💚'},
    5: {name:'SEC', color:'#ff9800', icon:'🔒'}
};
let evtOffset = 0;
let evtAllLoaded = false;

function evtTimeStr(ts) {
    if (ts > 1700000000) {
        let d = new Date(ts * 1000);
        return d.toLocaleString('cs-CZ', {day:'numeric',month:'numeric', hour:'2-digit',minute:'2-digit',second:'2-digit'});
    }
    return Math.floor(ts/3600) + "h " + Math.floor((ts%3600)/60) + "m " + (ts%60) + "s";
}

function renderTimelineBar(events) {
    // 24h density heatmap — 288 bins (5 min each)
    let now = Math.floor(Date.now() / 1000);
    let bins = new Array(288).fill(0);
    let maxBin = 1;
    let hasBins = false;
    events.forEach(e => {
        if (e.ts > 1700000000) {
            let age = now - e.ts;
            if (age >= 0 && age < 86400) {
                let bin = 287 - Math.floor(age / 300);
                if (bin >= 0 && bin < 288) { bins[bin]++; hasBins = true; }
            }
        }
    });
    if (!hasBins) { $('evt_timeline').innerHTML = '<text x="144" y="20" text-anchor="middle" fill="#444" font-size="10"><tspan data-i18n="no_timeline">Nedostatek dat pro timeline</tspan></text>'; return; }
    for (let i = 0; i < 288; i++) if (bins[i] > maxBin) maxBin = bins[i];

    let svg = '';
    for (let i = 0; i < 288; i++) {
        if (bins[i] === 0) continue;
        let h = Math.max(2, (bins[i] / maxBin) * 28);
        let a = 0.3 + 0.7 * (bins[i] / maxBin);
        svg += `<rect x="${i}" y="${32-h}" width="1" height="${h}" fill="var(--accent)" opacity="${a.toFixed(2)}"/>`;
    }
    // SEC events highlighted in red
    events.forEach(e => {
        if (e.type === 5 && e.ts > 1700000000) {
            let age = now - e.ts;
            if (age >= 0 && age < 86400) {
                let bin = 287 - Math.floor(age / 300);
                svg += `<rect x="${bin}" y="0" width="1" height="32" fill="#ff9800" opacity="0.6"/>`;
            }
        }
    });
    $('evt_timeline').innerHTML = svg;
}

function renderEventList(events, append) {
    let container = $('evt_timeline_list');
    let h = append ? '' : '';
    events.forEach(e => {
        let t = EVT_TYPES[e.type] || EVT_TYPES[0];
        let timeStr = evtTimeStr(e.ts);
        let distStr = e.dist > 0 ? `<span style="color:#888">${e.dist}cm</span>` : '';
        h += `<div style="display:flex; gap:8px; padding:6px 4px; border-left:3px solid ${t.color}; margin-bottom:2px; background:#111; border-radius:0 4px 4px 0; align-items:flex-start">
            <div style="flex-shrink:0; width:20px; text-align:center">${t.icon}</div>
            <div style="flex:1; min-width:0">
                <div style="display:flex; justify-content:space-between; gap:8px; flex-wrap:wrap">
                    <span style="font-size:0.75rem; color:#666; white-space:nowrap">${timeStr}</span>
                    <span style="font-size:0.7rem; color:${t.color}; font-weight:bold">${t.name} ${distStr}</span>
                </div>
                <div style="font-size:0.82rem; margin-top:2px; word-break:break-word">${e.msg}</div>
            </div>
        </div>`;
    });
    if (append) { container.innerHTML += h; }
    else { container.innerHTML = h || '<div style="text-align:center; padding:20px; color:#555">No events</div>'; }
}

function loadEvents() {
    evtOffset = 0;
    evtAllLoaded = false;
    let typeFilter = $('evt_filter').value;
    let url = '/api/events?limit=50&type=' + typeFilter;
    fetch(url).then(r=>r.json()).then(d => {
        let events = d.events || [];
        let total = d.total || 0;
        $('evt_total').textContent = total + ' ' + t('total');
        renderTimelineBar(events);
        renderEventList(events, false);
        evtOffset = events.length;
        evtAllLoaded = events.length >= (d.count !== undefined ? total : events.length);
        $('evt_load_more').style.display = (events.length >= 50 && !evtAllLoaded) ? 'block' : 'none';
    });
}

function loadMoreEvents() {
    let typeFilter = $('evt_filter').value;
    fetch('/api/events?limit=50&offset=' + evtOffset + '&type=' + typeFilter).then(r=>r.json()).then(d => {
        let events = d.events || [];
        renderEventList(events, true);
        evtOffset += events.length;
        if (events.length < 50) evtAllLoaded = true;
        $('evt_load_more').style.display = evtAllLoaded ? 'none' : 'block';
    });
}

function exportEvents() {
    window.open('/api/events/csv', '_blank');
}

function clearEvents() {
    if(confirm(t('del_history'))) {
        api('events/clear', {method:'POST'}).then(() => loadEvents());
    }
}

function startCalib() {
    if(confirm(t('noise_calib'))) {
        api('radar/calibrate', {method:'POST'});
    }
}

function saveBasic() {
    let m = $('i_max').value;
    let min = $('i_min').value;
    let h = $('i_hold').value;
    let s = $('i_sens').value;
    let l = $('chk_led').checked ? 1 : 0;
    api(`config`, {
        method: 'POST',
        body: new URLSearchParams({
            'gate': m,
            'min_gate': min,
            'hold': h,
            'mov': s,
            'led_en': l
        })
    }); 
}

function toggleEng() {
    let en = $('chk_eng').checked ? 1 : 0;
    api(`engineering`, {
        method: 'POST',
        body: new URLSearchParams({ 'enable': en })
    });
}

function loadRadarBt() {
    fetch('/api/radar/bluetooth').then(r=>r.json()).then(d => {
        if (d.readable && d.enabled !== undefined) {
            $('chk_radar_bt').checked = !!d.enabled;
        } else {
            $('chk_radar_bt').checked = !!d.configured;
        }
        $('radar_mac_val').innerText = d.mac ? ('MAC: ' + d.mac) : '';
    }).catch(e => console.log('Radar BT status not loaded'));
}

function toggleRadarBt() {
    let en = $('chk_radar_bt').checked ? 1 : 0;
    if (en && !confirm(t('radar_bt_warn') + '\n\n' + t('radar_bt_apply'))) {
        $('chk_radar_bt').checked = false;
        return;
    }
    if (!en && !confirm(t('radar_bt_apply'))) {
        $('chk_radar_bt').checked = true;
        return;
    }
    api('radar/bluetooth', {
        method: 'POST',
        body: new URLSearchParams({ 'enable': en })
    }).then(() => setTimeout(loadRadarBt, 3000));
}

function loadSecurityConfig() {
    fetch('/api/security/config').then(r=>r.json()).then(d => {
        $('i_am').value = d.antimask_time || 300;
        $('chk_am_en').checked = d.antimask_enabled || false;
        $('i_loit').value = d.loiter_time || 15;
        $('chk_loit_en').checked = d.loiter_alert !== false;
        $('i_hb').value = d.heartbeat || 4;
        $('i_pet').value = d.pet_immunity || 0;
        $('i_rssi_thresh').value = d.rssi_threshold || -80;
        $('i_rssi_drop').value = d.rssi_drop || 20;
    }).catch(e => console.log('Security config not loaded'));

    // Load Light Config
    fetch('/api/radar/light').then(r=>r.json()).then(d => {
        if(d.function !== undefined) $('sel_light_func').value = d.function;
        if(d.threshold !== undefined) $('i_light_thresh').value = d.threshold;
        if(d.current_level !== undefined) $('cur_light_val').innerText = d.current_level;
    }).catch(e => console.log('Light config not loaded'));

    // Load Timeout (unmanned duration)
    fetch('/api/radar/timeout').then(r=>r.json()).then(d => {
        if(d.duration !== undefined) $('i_timeout').value = d.duration;
    }).catch(e => console.log('Timeout config not loaded'));

    // Load Radar Bluetooth state
    loadRadarBt();
}

function saveLightConfig() {
    let func = $('sel_light_func').value;
    let thresh = $('i_light_thresh').value;
    api('radar/light', {
        method: 'POST',
        body: new URLSearchParams({ 'function': func, 'threshold': thresh })
    });
}

function saveTimeout() {
    let dur = $('i_timeout').value;
    api('radar/timeout', {
        method: 'POST',
        body: new URLSearchParams({ 'duration': dur })
    });
}

function saveSec() {
    let am = $('i_am').value;
    let am_en = $('chk_am_en').checked ? 1 : 0;
    let lo = $('i_loit').value;
    let lo_en = $('chk_loit_en').checked ? 1 : 0;
    let hb = $('i_hb').value;
    let pt = $('i_pet').value;
    let rt = $('i_rssi_thresh').value;
    let rd = $('i_rssi_drop').value;
    
    api(`security/config`, {
        method: 'POST',
        body: new URLSearchParams({
            'antimask': am,
            'antimask_en': am_en,
            'loiter': lo,
            'loiter_alert': lo_en,
            'heartbeat': hb,
            'pet': pt,
            'rssi_threshold': rt,
            'rssi_drop': rd
        })
    });
}

// MQTT Config
function loadMQTTConfig() {
    fetch('/api/health').then(r=>r.json()).then(d=>{
        if(d.mqtt) {
            $('chk_mqtt_en').checked = (d.mqtt.enabled !== false);
            $('txt_mqtt_server').value = d.mqtt.server || '';
            $('txt_mqtt_port').value = d.mqtt.port || '';
            $('txt_mqtt_user').value = d.mqtt.user || '';
        }
    });
}
function saveMQTTConfig() {
    let en = $('chk_mqtt_en').checked ? 1 : 0;
    let s = $('txt_mqtt_server').value;
    let p = $('txt_mqtt_port').value;
    let u = $('txt_mqtt_user').value;
    let pw = $('txt_mqtt_pass').value;

    api(`mqtt/config`, {
        method: 'POST',
        body: new URLSearchParams({
            'enabled': en,
            'server': s,
            'port': p,
            'user': u,
            'pass': pw
        })
    });
}

// --- Network (Ethernet static/DHCP) ---
function onNetModeChange() {
    let mode = $('net_mode_sel').value;
    $('net_static_fields').style.display = (mode === 'static') ? 'block' : 'none';
}
function isValidIPv4(s) {
    if (!s) return false;
    let parts = s.split('.');
    if (parts.length !== 4) return false;
    return parts.every(p => /^\d+$/.test(p) && +p >= 0 && +p <= 255);
}
function loadNetworkConfig() {
    fetch('/api/network/config').then(r => r.ok ? r.json() : null).then(d => {
        if (!d) return;
        $('net_mode_sel').value = d.mode || 'dhcp';
        $('net_ip').value = d.ip || '';
        $('net_subnet').value = d.subnet || '255.255.255.0';
        $('net_gateway').value = d.gateway || '';
        $('net_dns').value = d.dns || '';
        $('net_mac_lbl').innerText = d.mac || '—';
        $('net_link_lbl').innerText = (d.link_speed ? d.link_speed + ' Mbps ' : '') +
            (d.full_duplex ? 'FD' : (d.link_speed ? 'HD' : ''));
        onNetModeChange();
    });
}
function saveNetworkConfig() {
    let mode = $('net_mode_sel').value;
    let body = new URLSearchParams();
    body.append('mode', mode);
    if (mode === 'static') {
        let ip = $('net_ip').value.trim();
        let sn = $('net_subnet').value.trim();
        let gw = $('net_gateway').value.trim();
        let dns = $('net_dns').value.trim();
        if (!isValidIPv4(ip) || !isValidIPv4(gw) || (sn && !isValidIPv4(sn)) || (dns && !isValidIPv4(dns))) {
            showToast(t('net_ip_invalid'));
            return;
        }
        body.append('ip', ip);
        body.append('gateway', gw);
        if (sn) body.append('subnet', sn);
        if (dns) body.append('dns', dns);
    }
    if (!confirm(t('net_confirm'))) return;
    fetch('/api/network/config', {method:'POST', body: body}).then(r => {
        showToast(r.ok ? t('restarting') : t('save_error'));
    });
}

// Telegram
function loadTelegramConfig() {
    fetch('/api/telegram/config').then(r=>r.json()).then(d => {
        $('chk_tg_en').checked = d.enabled;
        $('txt_tg_token').value = d.token || '';
        $('txt_tg_chat').value = d.chat_id || '';
    });
}
function saveTelegram() {
    let en = $('chk_tg_en').checked ? 1 : 0;
    let t = $('txt_tg_token').value;
    let c = $('txt_tg_chat').value;
    api('telegram/config', {
        method: 'POST',
        body: new URLSearchParams({ 'enabled': en, 'token': t, 'chat_id': c })
    });
}
function testTelegram() {
    fetch('/api/telegram/test', {method:'POST'})
    .then(r => r.json())
    .then(d => {
        showToast(d.success ? "Telegram OK!" : t('tg_error') + ": " + (d.error || t('tg_unknown')));
    })
    .catch(e => showToast("Chyba komunikace"));
}

// --- Timezone ---
function onTzChange() {
    let v = $('tz_sel').value;
    $('tz_custom_fields').style.display = (v === 'custom') ? 'block' : 'none';
    updateTzLabel();
}
function updateTzLabel() {
    let v = $('tz_sel').value;
    let std, dst;
    if (v === 'custom') {
        std = parseInt($('tz_std_in').value || '0', 10);
        dst = parseInt($('tz_dst_in').value || '0', 10);
    } else {
        let parts = v.split(',');
        std = parseInt(parts[0], 10);
        dst = parseInt(parts[1] || '0', 10);
    }
    let hours = (std / 3600);
    let sign = hours >= 0 ? '+' : '';
    let dstLbl = dst ? (' (DST +' + (dst/3600) + 'h)') : '';
    $('tz_device_time').innerText = 'UTC' + sign + hours + 'h' + dstLbl;
}
function loadTimezoneConfig() {
    fetch('/api/timezone').then(r => r.ok ? r.json() : null).then(d => {
        if (!d) return;
        let std = d.tz_offset || 0;
        let dst = d.dst_offset || 0;
        let target = std + ',' + dst;
        let sel = $('tz_sel');
        let found = false;
        for (let i = 0; i < sel.options.length; i++) {
            if (sel.options[i].value === target) {
                sel.selectedIndex = i;
                found = true;
                break;
            }
        }
        if (!found) {
            sel.value = 'custom';
            $('tz_std_in').value = std;
            $('tz_dst_in').value = dst;
        }
        onTzChange();
    });
}
function loadScheduleConfig() {
    fetch('/api/schedule').then(r => r.ok ? r.json() : null).then(d => {
        if (!d) return;
        $('sched_arm_in').value = d.arm_time || '';
        $('sched_disarm_in').value = d.disarm_time || '';
        $('sched_auto_arm_in').value = (d.auto_arm_minutes != null) ? d.auto_arm_minutes : 0;
    });
}
function saveSchedule() {
    let arm = $('sched_arm_in').value || '';
    let disarm = $('sched_disarm_in').value || '';
    let autoArm = parseInt($('sched_auto_arm_in').value || '0', 10);
    if (isNaN(autoArm) || autoArm < 0) autoArm = 0;
    if (autoArm > 1440) autoArm = 1440;
    // NOTE: backend /api/schedule POST reads params from query string (hasParam w/o post=true),
    // so we send as URL query instead of form body.
    let qs = new URLSearchParams({
        'arm_time': arm,
        'disarm_time': disarm,
        'auto_arm_minutes': autoArm
    }).toString();
    fetch('/api/schedule?' + qs, { method: 'POST' }).then(r => {
        showToast(r.ok ? t('sched_saved') : t('save_error'));
    });
}
function saveTimezone() {
    let v = $('tz_sel').value;
    let std, dst;
    if (v === 'custom') {
        std = parseInt($('tz_std_in').value || '0', 10);
        dst = parseInt($('tz_dst_in').value || '0', 10);
    } else {
        let parts = v.split(',');
        std = parseInt(parts[0], 10);
        dst = parseInt(parts[1] || '0', 10);
    }
    fetch('/api/timezone', {
        method: 'POST',
        body: new URLSearchParams({ 'tz_offset': std, 'dst_offset': dst })
    }).then(r => {
        showToast(r.ok ? t('tz_saved') : t('save_error'));
    });
}

// Zones Implementation
function loadZones() {
    fetch('/api/zones').then(r=>r.json()).then(d => { zones = d; renderZones(); }).catch(e=>zones=[]);
}
const ZONE_COLORS = ['#1a6b3a','#1a4a6b','#6b1a1a','#4a1a6b'];
function renderZones() {
    let h = '';
    zones.forEach((z, i) => {
        const ab = z.alarm_behavior ?? 0;
        h += `<div style="margin-bottom:5px; background:#222; padding:5px; border-radius:5px; border-left:3px solid ${ZONE_COLORS[ab]||'#444'}">
            <div style="display:flex; gap:5px; margin-bottom:5px">
                <input type="text" value="${z.name}" id="z_name_${i}" style="flex:2" placeholder="" data-i18n="zone_name">
                <input type="number" value="${z.min}" id="z_min_${i}" style="flex:1" placeholder="Od (cm)">
                <input type="number" value="${z.max}" id="z_max_${i}" style="flex:1" placeholder="Do (cm)">
            </div>
            <div style="display:flex; gap:5px; align-items:center">
                <select id="z_lvl_${i}" style="flex:1">
                    <option value="0" ${z.level==0?'selected':''}>Log</option>
                    <option value="1" ${z.level==1?'selected':''}>Info</option>
                    <option value="2" ${z.level==2?'selected':''}>Warn</option>
                    <option value="3" ${z.level==3?'selected':''}>ALARM</option>
                </select>
                <select id="z_ab_${i}" style="flex:2" title="" data-i18n="zone_behavior">
                    <option value="0" ${ab==0?'selected':''}>⏱ Entry delay</option>
                    <option value="1" ${ab==1?'selected':''}data-i18n="zone_immediate">🚨 Okamžité</option>
                    <option value="2" ${ab==2?'selected':''}>🔕 Ignorovat</option>
                    <option value="3" ${ab==3?'selected':''}>📡 Ignorovat statiku</option>
                </select>
                <input type="number" value="${z.delay||0}" id="z_del_${i}" style="flex:1" placeholder="" data-i18n="zone_delay">
                <input type="checkbox" id="z_en_${i}" ${z.enabled!==false?'checked':''} style="width:auto">
                <button onclick="delZone(${i})" class="warn" style="width:auto; margin:0; padding:5px 10px">×</button>
            </div>
        </div>`;
    });
    $('zones_list').innerHTML = h;
    drawZoneMap();
}
function addZone() {
    zones.push({name: t('zone_default') + " " + (zones.length+1), min: 0, max: 100, level: 0, alarm_behavior: 0, delay: 0, enabled: true});
    renderZones();
}
function delZone(i) {
    zones.splice(i, 1);
    renderZones();
}
function saveZones() {
    let newZones = [];
    zones.forEach((_, i) => {
        newZones.push({
            name: document.getElementById(`z_name_${i}`).value,
            min: parseInt(document.getElementById(`z_min_${i}`).value),
            max: parseInt(document.getElementById(`z_max_${i}`).value),
            level: parseInt(document.getElementById(`z_lvl_${i}`).value),
            alarm_behavior: parseInt(document.getElementById(`z_ab_${i}`).value),
            delay: parseInt(document.getElementById(`z_del_${i}`).value),
            enabled: document.getElementById(`z_en_${i}`).checked
        });
    });
    zones = newZones;
    fetch('/api/zones', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(zones)
    }).then(r => {
        if(r.ok) showToast(t('zones_saved'));
        else showToast(t('save_error'));
    });
}

// ── Zone Map ───────────────────────────────────────────────────────────────────
function drawZoneMap(statDist, movDist) {
    const svg   = $('zone_map');
    const scale = $('zone_map_scale');
    if (!svg) return;
    const W = svg.clientWidth || 300, H = 48, MAX = 1050; // 14 gates × 75cm
    let html = '';
    zones.forEach(z => {
        if (!z.enabled) return;
        const ab  = z.alarm_behavior ?? 0;
        const col = ZONE_COLORS[ab] || '#444';
        const x1  = Math.round(z.min / MAX * W);
        const x2  = Math.round(z.max / MAX * W);
        html += `<rect x="${x1}" y="4" width="${x2-x1}" height="${H-8}" fill="${col}" opacity="0.5" rx="3"/>`;
        html += `<text x="${x1+3}" y="16" font-size="9" fill="#aaa">${z.name}</text>`;
    });
    if (statDist > 0) {
        const sx = Math.round(statDist / MAX * W);
        html += `<line x1="${sx}" y1="0" y2="${H}" x2="${sx}" stroke="#bb86fc" stroke-width="2"/>`;
    }
    if (movDist > 0) {
        const mx = Math.round(movDist / MAX * W);
        html += `<line x1="${mx}" y1="0" y2="${H}" x2="${mx}" stroke="#03dac6" stroke-width="2"/>`;
    }
    svg.innerHTML = html;
    if (scale) scale.innerText = '0cm' + ' '.repeat(10) + '525cm' + ' '.repeat(10) + '1050cm';
}

// ── Auto-learn ────────────────────────────────────────────────────────────────
let learnPollTimer = null;
function startLearn() {
    const dur = $('learn_dur').value;
    api(`radar/learn-static?duration=${dur}`, { method: 'POST' }).then(r => {
        if (r.ok) {
            $('btn_learn').disabled = true;
            $('learn_status').style.display = 'block';
            $('learn_status').innerText = t('starting');
            learnPollTimer = setInterval(pollLearn, 3000);
        }
    });
}
function pollLearn() {
    fetch('/api/radar/learn-static').then(r=>r.json()).then(d => {
        const stat = $('learn_status');
        if (!d.active && d.progress === 100) {
            clearInterval(learnPollTimer);
            $('btn_learn').disabled = false;
            let txt = `✅ Hotovo! Top gate: ${d.top_gate} (~${d.top_cm}cm), confidence: ${d.confidence}%`;
            if (d.suggest_ready) {
                txt += ` <button onclick="applyLearnZone(${d.suggest_min_cm},${d.suggest_max_cm})" class="sec" style="padding:2px 8px; margin-left:6px">${t('apply')}</button>`;
            } else {
                txt += ' ⚠️ ' + t('not_enough');
            }
            stat.innerHTML = txt;
        } else {
            stat.innerText = `⏳ ${d.progress}% | ${t('static_label')}: ${d.static_freq_pct}% | Top gate: ${d.top_gate} (~${d.top_cm}cm)`;
        }
    });
}
function applyLearnZone(minCm, maxCm) {
    zones.push({name: t('static_label')+'-auto', min: minCm, max: maxCm, level: 0, alarm_behavior: 3, delay: 0, enabled: true});
    renderZones();
    $('learn_status').innerHTML += ' &nbsp;<b>' + t('zone_added') + '</b>';
}

function saveGates() {
    let mov = [], stat = [];
    for(let i=0; i<14; i++) {
        mov.push(parseInt($(`g_m_${i}`).value));
        stat.push(parseInt($(`g_s_${i}`).value));
    }
    fetch('/api/radar/gates', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({mov, stat})
    }).then(r => {
        if(r.ok) showToast(t('gates_saved'));
        else showToast(t('gates_error'));
    });
}
function setPreset(t) {
    fetch('/api/preset?name=' + t, {method:'POST'}).then(r => {
        if(!r.ok) { showToast("Chyba presetu"); return; }
        showToast(t('preset_applied') + " (" + t + ")");
        // Re-fetch config and update sliders in-place (no reload)
        fetch('/api/config').then(r=>r.json()).then(d => {
            if(d.mov_sens && d.stat_sens) renderGateSliders(d.mov_sens, d.stat_sens);
        });
    });
}
function saveHostname() { 
    let hn = $('txt_hostname').value;
    api(`config`, {
        method: 'POST',
        body: new URLSearchParams({ 'hostname': hn })
    });
}

function _otaStatus(msg) { $('ota_status').innerText = msg || ''; }

function _otaSendUpload(f) {
    let fd = new FormData();
    fd.append('firmware', f);
    let xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/update');
    xhr.timeout = 180000;
    xhr.withCredentials = true;
    xhr.upload.onprogress = e => $('ota_bar').style.width = (e.loaded/e.total*100) + "%";
    xhr.onload = () => {
        $('btn_ota').disabled = false;
        if (xhr.status >= 200 && xhr.status < 300 && xhr.responseText.indexOf("OK") === 0) {
            _otaStatus('');
            alert(t('restarting'));
        } else {
            _otaStatus('❌ HTTP ' + xhr.status);
            alert('OTA failed: HTTP ' + xhr.status + '\n' + (xhr.responseText || '(no body)'));
        }
    };
    xhr.onerror = () => { $('btn_ota').disabled = false; _otaStatus('❌ network'); alert('OTA failed: network error (device may still be rebooting)'); };
    xhr.ontimeout = () => { $('btn_ota').disabled = false; _otaStatus('❌ timeout'); alert('OTA failed: timeout (3 min).'); };
    _otaStatus(t('ota_uploading'));
    xhr.send(fd);
}

function _otaWaitReboot(f, attempt) {
    attempt = attempt || 0;
    if (attempt > 25) {
        $('btn_ota').disabled = false;
        _otaStatus('❌ ' + t('ota_cold_failed'));
        alert(t('ota_cold_failed'));
        return;
    }
    _otaStatus(t('ota_waiting_reboot') + ' (' + attempt + 's)');
    fetch('/api/health', {cache: 'no-store', credentials: 'include'})
        .then(r => r.ok ? r.json() : Promise.reject(r.status))
        .then(d => {
            // Confirm fresh boot: uptime < 30 s (server returns seconds)
            let up = (typeof d.uptime === 'number') ? d.uptime : 99999;
            if (up < 30) {
                _otaStatus('✅ ' + t('ota_ready_uploading'));
                setTimeout(() => _otaSendUpload(f), 500);
            } else {
                setTimeout(() => _otaWaitReboot(f, attempt + 1), 1000);
            }
        })
        .catch(() => setTimeout(() => _otaWaitReboot(f, attempt + 1), 1000));
}

function uploadFW() {
    let f = $('fw_file').files[0];
    if(!f) return;
    $('ota_bar').style.width = '0%';
    $('btn_ota').disabled = true;
    if ($('ota_cold_reboot').checked) {
        _otaStatus(t('ota_cold_restart'));
        fetch('/api/restart', {method:'POST', credentials:'include'})
            .catch(()=>{})
            .finally(() => setTimeout(() => _otaWaitReboot(f, 0), 3000));
    } else {
        _otaSendUpload(f);
    }
}

// --- PULL OTA ---
function _pullStatus(msg) { $('pull_status').innerText = msg || ''; }
function _pullPhaseLabel(phase) {
    if (phase === 'downloading' || phase === 'fetching') return t('pull_ota_phase_downloading');
    if (phase === 'writing' || phase === 'flashing')     return t('pull_ota_phase_writing');
    if (phase === 'success')                              return t('pull_ota_phase_success');
    if (phase === 'error')                                return t('pull_ota_phase_error');
    return t('pull_ota_phase_idle');
}
function _pullPoll(tries) {
    if (tries > 60) { $('btn_pull').disabled = false; return; }
    fetch('/api/update/pull/status', {credentials:'include'})
        .then(r => r.json())
        .then(d => {
            const phase = (d.phase || 'idle').toLowerCase();
            const lbl = _pullPhaseLabel(phase);
            const err = d.last_error ? (' — ' + d.last_error) : '';
            _pullStatus(lbl + err);
            if (phase === 'success') {
                setTimeout(() => location.reload(), 4000);
                return;
            }
            if (phase === 'error') {
                $('btn_pull').disabled = false;
                return;
            }
            setTimeout(() => _pullPoll(tries + 1), 2000);
        })
        .catch(() => setTimeout(() => _pullPoll(tries + 1), 2000));
}
function pullFW() {
    const url = ($('pull_url').value || '').trim();
    if (!url) { _pullStatus(t('pull_ota_no_url')); return; }
    const auth = ($('pull_auth').value || '').trim();
    const body = auth ? {url, auth} : {url};
    $('btn_pull').disabled = true;
    _pullStatus(t('pull_ota_running'));
    fetch('/api/update/pull', {
        method: 'POST',
        credentials: 'include',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(body)
    })
    .then(r => r.json().catch(() => ({})))
    .then(d => {
        if (d && d.error) {
            _pullStatus(t('pull_ota_phase_error') + ' — ' + d.error);
            $('btn_pull').disabled = false;
            return;
        }
        // accepted; start polling
        setTimeout(() => _pullPoll(0), 1500);
    })
    .catch(e => {
        _pullStatus(t('pull_ota_phase_error') + ' — ' + e);
        $('btn_pull').disabled = false;
    });
}

// --- ALARM ---
let alarmArmed = false;
function loadAlarmStatus() {
    fetch('/api/alarm/status').then(r=>r.json()).then(d => {
        alarmArmed = d.armed;
        updateAlarmUI(d.state);
        $('i_entry_dl').value = d.entry_delay || 30;
        $('i_exit_dl').value = d.exit_delay || 30;
        $('chk_dis_rem').checked = d.disarm_reminder !== false;
    }).catch(()=>{});
}
function updateAlarmUI(state) {
    let badge = $('alarm_badge');
    let btn = $('btn_arm');
    if(state === 'disarmed') { badge.innerText = t('disarmed'); badge.style.color='#888'; btn.innerText=t('arm'); btn.style.background='#b00020'; }
    else if(state === 'arming') { badge.innerText = t('arming'); badge.style.color='orange'; btn.innerText=t('disarm'); btn.style.background='#3700b3'; }
    else if(state === 'armed_away') { badge.innerText = t('armed'); badge.style.color='#00ff00'; btn.innerText=t('disarm'); btn.style.background='#3700b3'; }
    else if(state === 'pending') { badge.innerText = t('pending'); badge.style.color='orange'; btn.innerText=t('disarm'); btn.style.background='#3700b3'; }
    else if(state === 'triggered') { badge.innerText = t('triggered'); badge.style.color='red'; btn.innerText=t('disarm'); btn.style.background='#3700b3'; }
}
function toggleArm() {
    if(alarmArmed) {
        api('alarm/disarm', {method:'POST'}).then(()=>{ alarmArmed=false; loadAlarmStatus(); });
    } else {
        api('alarm/arm', {method:'POST'}).then(()=>{ alarmArmed=true; loadAlarmStatus(); });
    }
}
function saveAlarmConfig() {
    let ed = $('i_entry_dl').value;
    let xd = $('i_exit_dl').value;
    let dr = $('chk_dis_rem').checked ? 1 : 0;
    api('alarm/config', {
        method: 'POST',
        body: new URLSearchParams({ 'entry_delay': ed, 'exit_delay': xd, 'disarm_reminder': dr })
    });
}

function saveAuth() {
    let u = $('txt_auth_user').value;
    let p = $('txt_auth_pass').value;
    let p2 = $('txt_auth_pass2').value;
    if(!u || !p) { showToast(t('enter_creds')); return; }
    if(p !== p2) { showToast(t('pass_mismatch')); return; }
    if(u.length < 4 || p.length < 4) { showToast("Min. 4 znaky"); return; }

    fetch(`/api/auth/config?user=${encodeURIComponent(u)}&pass=${encodeURIComponent(p)}`, {
        method: 'POST'
    }).then(r => {
        if(r.ok) { showToast(t('pass_changed')); alert(t('creds_changed')); }
        else r.text().then(t => showToast(t || "Chyba"));
    });
}

// --- Config Export/Import ---
function exportConfig() {
    let d = new Date();
    let ymd = d.getFullYear().toString()
        + String(d.getMonth()+1).padStart(2,'0')
        + String(d.getDate()).padStart(2,'0');
    let a = document.createElement('a');
    a.href = '/api/config/export';
    a.download = 'poe2412-config-' + ymd + '.json';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
}

function importConfigPrompt() {
    $('cfg_import_file').click();
}

function importConfig(file) {
    if(!file) return;
    if(!confirm(t('cfg_import_confirm'))) { $('cfg_import_file').value = ''; return; }
    file.text().then(txt => {
        return fetch('/api/config/import', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: txt
        });
    }).then(r => {
        if(r.ok) showToast(t('cfg_import_ok'));
        else r.text().then(err => showToast(t('cfg_import_err').replace('{err}', err || r.status)));
    }).catch(e => {
        showToast(t('cfg_import_err').replace('{err}', e.message || e));
    }).finally(() => {
        $('cfg_import_file').value = '';
    });
}

window.onload = init;
</script>
</body>
</html>
)rawliteral";

#endif

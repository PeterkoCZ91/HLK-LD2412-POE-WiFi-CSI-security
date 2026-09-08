#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="en">
<head>
  <title>LD2412 Zabezpečení</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
:root{--bg:#0a0a0a;--card:#161616;--text:#e0e0e0;--accent:#03dac6;--warn:#cf6679;--sec:#333}*{box-sizing:border-box}body{font-family:"Segoe UI",sans-serif;background:var(--bg);color:var(--text);margin:0;padding:10px 10px 50px}h2{color:var(--accent);margin:5px 0;font-size:1.4rem;display:flex;align-items:center;justify-content:center;gap:10px}.grid{column-width:360px;column-gap:10px;max-width:1200px;margin:0 auto}.card{background:var(--card);padding:15px;border-radius:12px;box-shadow:0 4px 10px rgba(0,0,0,.5);break-inside:avoid;page-break-inside:avoid;margin:0 0 10px}.stat-row{display:flex;justify-content:space-between;margin-bottom:8px;font-size:.9rem;border-bottom:1px solid #222;padding-bottom:4px}.stat-val{font-weight:700;color:#fff}.gauge{text-align:center;margin-bottom:10px}.big-val{font-size:2.5rem;font-weight:700;line-height:1}.unit{font-size:.8rem;color:#888}button,svg.spark{width:100%;margin-top:5px}svg.spark{height:50px;stroke-width:2;fill:none}.icon{width:16px;height:16px;display:inline-block;vertical-align:middle;border-radius:50%}.icon.ok{background:#0f0;box-shadow:0 0 5px #0f0}.icon.warn{background:orange}.icon.err{background:red}input[type=range]{width:100%;accent-color:var(--accent)}input[type=number],input[type=password],input[type=text],select{background:#222;border:1px solid #444;color:#fff;padding:8px;border-radius:4px;width:100%;margin-top:2px}.row-input{display:flex;justify-content:space-between;align-items:center;margin-bottom:5px;gap:10px}button{padding:10px;border:0;border-radius:6px;background:#3700b3;color:#fff;cursor:pointer}button:hover{opacity:.9}button.sec{background:var(--sec)}button.warn{background:var(--warn);color:#000;font-weight:700}button:disabled{opacity:.5;cursor:wait}.wifi-scan-list{display:flex;flex-direction:column;gap:4px;margin:6px 0}button.wifi-scan-row{display:flex;justify-content:space-between;align-items:center;gap:8px;background:#222;border:1px solid #383838;padding:8px;margin:0;text-align:left}.wifi-scan-name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:700}.wifi-scan-meta{flex-shrink:0;color:#999;font-size:.75rem}.gate-wrapper{display:flex;align-items:center;gap:5px;margin-bottom:4px;font-size:.75rem}.gate-label{width:25px;flex-shrink:0}input[type=range].mov-slider{accent-color:#03dac6}input[type=range].stat-slider{accent-color:#bb86fc}.gate-dimmed{opacity:.55;background:#1a1a1a;border-radius:4px;padding:2px 4px}.gate-dimmed .gate-label{color:#444;text-decoration:line-through}.gate-dimmed input[type=range]{filter:grayscale(1) brightness(.6)}.gate-dimmed span[id^=lm_],.gate-dimmed span[id^=ls_]{color:#444!important;text-decoration:line-through}.tabs{display:flex;gap:5px;margin-bottom:10px;flex-wrap:wrap}.tab{flex:1;min-width:80px;padding:8px;background:#222;text-align:center;cursor:pointer;border-radius:6px;font-size:.9rem}.tab.active{background:var(--accent);color:#000;font-weight:700}.hidden{display:none}.section-title{color:#888;font-size:.8rem;margin:10px 0 5px;text-transform:uppercase;border-bottom:1px solid #333}.section-title.collapsible{cursor:pointer;user-select:none;display:flex;justify-content:space-between;align-items:center}.section-title.collapsible::after{content:"▾";font-size:.9em;opacity:.55;margin-left:8px}.section-title.collapsed::after{content:"▸"}.radar-only.r-hidden,.sec-collapsed{display:none!important}.radar-reveal{font-size:.75rem;color:#666;cursor:pointer;text-align:center;margin:4px 0;user-select:none}.radar-reveal:hover{color:#888}#csi_main_block.promoted #csi_main_state{font-size:2.5rem;line-height:1}#csi_main_block.promoted{margin-top:4px}#toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);background:#333;padding:10px 20px;border-radius:20px;opacity:0;transition:opacity .3s;pointer-events:none}#evt_timeline_list::-webkit-scrollbar{width:6px}#evt_timeline_list::-webkit-scrollbar-thumb{background:#444;border-radius:3px}#evt_timeline_list::-webkit-scrollbar-track{background:#111}@media (max-width:480px){.grid{column-width:auto;column-count:1}body{padding:5px 5px 60px}.big-val{font-size:2rem}.tabs{flex-wrap:wrap;gap:4px}.tab{min-width:60px;font-size:.8rem;padding:10px 6px;flex-grow:1}button{padding:12px;min-height:44px;font-size:1rem}input[type=range]{height:30px}.gate-wrapper{flex-wrap:wrap}.row-input{flex-direction:column;align-items:stretch;gap:2px}.row-input span{margin-bottom:2px}input[type=number],input[type=text],select{padding:10px;font-size:1rem}}.fbars{display:flex;gap:14px;justify-content:center;align-items:flex-end;margin:8px 0 10px}.fbar{display:flex;flex-direction:column;align-items:center;gap:4px;width:48px}.fbar-track{width:24px;height:70px;background:var(--sec);border-radius:5px;display:flex;align-items:flex-end;overflow:hidden}.fbar-fill{width:100%;height:0%;background:#555;border-radius:5px;transition:height .3s,background .3s}.fbar-fill.on{background:var(--accent)}.fbar span{font-size:.72rem;color:#aaa;white-space:nowrap}.fbar.na .fbar-track{opacity:.35}.fbar.na .fbar-fill{height:100%;background:repeating-linear-gradient(45deg,#444,#444 4px,#333 4px,#333 8px)}.fbar.na span::after{content:" N/A";color:#777}.fgauge{display:flex;align-items:center;gap:8px;margin:6px 0}.fgauge-track{flex:1;height:10px;background:var(--sec);border-radius:5px;overflow:hidden}.fgauge-fill{height:100%;width:0%;background:linear-gradient(90deg,var(--warn),#e0c000,var(--accent));transition:width .3s}.fgauge span{font-size:.85rem;font-weight:700;color:var(--text);min-width:40px;text-align:right}.freason{font-size:.8rem;color:#bbb;text-align:center;margin-top:6px;min-height:1.1em}
  </style>
  <script>
const I18N={cs:{title:"LD2412 Zabezpečení",loading:"NAČÍTÁM...",arm:"STŘEŽIT",disarm:"ZRUŠIT",disarmed:"🔓 NESTŘEŽENO",arming:"⏳ AKTIVUJI...",armed:"🔒 STŘEŽENO",pending:"⚠️ ČEKÁNÍ",triggered:"🚨 POPLACH",sensor_health:"Zdraví senzoru",uart_state:"UART Stav",frame_rate:"Snímková frekvence",ram:"RAM (Volná/Min)",chip_temp:"Teplota čipu",uptime:"Doba běhu",factory_reset_confirm:"Opravdu provést tovární reset radaru?",tab_basic:"Základní",tab_security:"Bezpečnost",tab_gates:"Citlivost pásem",tab_network:"Síť & Cloud",tab_zones:"Zóny",tab_events:"Události",tab_csi:"CSI",device_name:"Jméno zařízení (mDNS)",hold_time:"Doba držení (s)",move_sens:"Citlivost Pohyb (%)",enable_diag:"Povolit Diagnostiku",radar_bt:"Bluetooth radaru (párování HLK aplikace)",radar_bt_warn:"⚠️ Zapnout jen na dobu potřebnou pro diagnostiku HLK aplikací. Doporučeno vypnuto — kdokoli v dosahu BT se jinak může spárovat s radarem.",radar_bt_apply:"Radar se restartuje pro aplikaci změny.",antimask_title:"ANTI-MASKING (Sabotáž zakrytím)",antimask_enable:"Povolit alarm při tichu",timeout_sec:"Časový limit (sec)",antimask_hint:"Pro sklady, chaty, serverovny <b>VYPNĚTE</b> - ticho je tam normální.<br>Pro obývané prostory ZAPNĚTE - detekuje zakrytí sensoru.",loiter_title:"LOITERING (Podezřelé postávání)",loiter_enable:"Notifikace při postávání",loiter_hint:"Alarm když někdo stojí &lt;2m od sensoru déle než timeout.",hb_title:"HEARTBEAT (Pravidelný report)",hb_hint:'0 = vypnuto, 4 = každé 4 hodiny zpráva "jsem OK".',pet_hint:"Filtruje malé objekty (kočky, psi) s nízkou energií &lt;2m.",entry_delay:"Zpoždění vstupu (sec)",exit_delay:"Zpoždění odchodu (sec)",disarm_reminder:'Připomínka "Stále NESTŘEŽENO"',hold_hint:'Doba, po které radar hlásí "nepřítomnost" bez detekce.',light_func:"Funkce světla",light_night:"Noční režim (pod práh)",light_day:"Denní režim (nad práh)",light_thr:"Práh světla (0-255)",light_cur:"Aktuální světlo",light_hint:'OUT pin aktivní jen když je světlo pod/nad prahem.<br>Ideální pro noční zabezpečení (režim "pod práh").',movement:"Pohyb",static_:"Statika",gate_legend:"Vyšší = citlivější &middot; <span style='opacity:0.4'>šedá = mimo min/max rozsah</span>",set_all:"Nastavit vše:",indoor:"Interiér",outdoor:"Exteriér",pets:"Zvířata",save_gates:"Uložit citlivost",save_mqtt:"Uložit MQTT",save:"Uložit",username:"Uživatel",new_pass:"Nové heslo",change_pass:"Změnit heslo",cfg_backup:"ZÁLOHA KONFIGURACE",cfg_export:"💾 Exportovat",cfg_import:"📂 Importovat",cfg_import_confirm:"Nahradit všechna nastavení? Zařízení se restartuje.",cfg_import_ok:"Konfigurace importována, restartování...",cfg_import_err:"Import selhal: {err}",add_zone:"+ Přidat Zónu",save_zones:"💾 Uložit Zóny",learn_static:"📡 Naučit statiku",filter_all:"Vše",filter_alarm:"Alarm",filter_move:"Pohyb",filter_tamper:"Tamper",filter_hb:"Heartbeat",filter_sys:"Systém",filter_net:"Síť",delete:"Smazat",now:"nyní",load_more:"Načíst další...",csi_warn:"Tento firmware nebyl zkompilován s podporou WiFi CSI (chybí <code>-D USE_CSI=1</code>). Pro povolení nahraj variantu <b>esp32_poe_csi</b>.",csi_active:"Aktivní",csi_idle:"Idle baseline připraven",ap_compat:"AP kompatibilita (HT LTF)",ap_ok:"OK",ap_incompat:"NEKOMPATIBILNÍ",ap_checking:"čekám na HT LTF…",ap_incompat_hint:"AP nevysílá 802.11n HT LTF rámce (pravděpodobně WiFi 6 HW v n-módu). CSI je pasivní, ale detekce pohybu nebude fungovat. Použij pre-WiFi 6 AP.",wifi_ap:"WIFI AP (CSI)",wifi_ssid:"SSID",wifi_pass:"Heslo",wifi_ap_hint:"Přepnutí AP pro CSI senzor. Uložení vyžaduje reboot. Prázdné heslo ponechá stávající; tlačítko default obnoví údaje ze <code>secrets.h</code>.",save_reboot:"💾 Uložit a rebootovat",wifi_use_default:"↩️ Compile-time default",wifi_scan:"🔎 Vyhledat okolní WiFi",wifi_scanning:"Vyhledávám sítě…",wifi_scan_hint:"Scan na několik sekund pozastaví CSI příjem. Během site learningu, kalibrace nebo OTA je blokovaný.",wifi_scan_found:"Nalezeno sítí: {n}. Kliknutím vyber SSID.",wifi_scan_none:"Žádné viditelné sítě",wifi_scan_failed:"Vyhledávání WiFi selhalo",wifi_scan_busy:"WiFi scan teď nelze spustit — probíhá údržba nebo učení.",wifi_current:"připojeno",wifi_channel:"kanál",wifi_pass_required:"Pro novou zabezpečenou síť zadej heslo.",wifi_ssid_empty:"SSID nesmí být prázdné",wifi_save_confirm:"Uložit SSID '{ssid}' a rebootovat? Pokud je heslo špatné, ESP se nepřipojí a bude třeba factory reset (GPIO0 5s).",wifi_saved_reboot:"Uloženo. Rebootuji…",wifi_save_failed:"Uložení selhalo",wifi_reset_confirm:"Obnovit compile-time default SSID ze secrets.h a rebootovat?",wifi_reset_reboot:"Reset. Rebootuji…",motion_state:"Stav pohybu",composite:"Souhrnné skóre pohybu (composite)",threshold_label:"Práh detekce pohybu (variance threshold):",save_config:"Uložit konfiguraci",upload_fw:"Nahrát Firmware",saved:"Uloženo",fw_update_title:"Aktualizace FW",pull_ota_title:"Pull OTA z URL",pull_ota_help:"HTTPS Pull OTA vyžaduje uloženou CA; HTTP zůstává jen kompatibilní režim pro privátní LAN. Přesměrování se nenásleduje a MD5 je povinná kontrola integrity firmware.",pull_ota_btn:"⤓ Stáhnout a flashnout",pull_ota_running:"Stahuji & flashuji…",pull_ota_phase_idle:"—",pull_ota_phase_connecting:"Připojuji se…",pull_ota_phase_downloading:"Stahuji…",pull_ota_phase_writing:"Zapisuji firmware…",pull_ota_phase_success:"✅ Hotovo, restartuji",pull_ota_phase_error:"❌ Chyba",pull_ota_no_url:"Zadej URL k firmware .bin",pull_ota_bad_md5:"MD5 musí mít 32 hex znaků",ota_cold_label:"Před OTA restartovat (doporučeno pro 100% úspěšnost)",ota_cold_restart:"Restartuji zařízení...",ota_waiting_reboot:"Čekám na zařízení",ota_ready_uploading:"Zařízení naběhlo, nahrávám...",ota_uploading:"Nahrávám firmware...",ota_cold_failed:"Zařízení nenaběhlo do 25 s — zkontroluj napájení a zkus znovu",ota_espota_prepare:"Připravit espota okno",ota_espota_ready:"ESPOTA okno otevřeno na 120 s",ota_espota_help:"Pro flash nástrojem espota.py (UDP 3232) místo nahrání souboru. Na ~120 s odpojí CSI WiFi (single-home), odstaví CSI/MQTT a pozastaví radar → uvolní CPU/RAM, aby espota spolehlivě prošlo auth. Otevři okno a hned spusť espota.py ze stejné sítě. Reboot není potřeba.",yes:"ANO",no:"NE",no_collecting:"NE — sbírá vzorky",motion:"POHYB",idle:"KLID",coverage:"Pokrytí",resolution:"Rozlišení",gate:"hradlo",hold_state:"DRŽENÍ",tamper_state:"SABOTÁŽ!",csi_restart:"Změna povolení CSI vyžaduje restart ESP. Restartovat teď?",calib_started:"Kalibrace zahájena (10s, neobývej místnost)",reset_confirm:"Resetovat idle baseline? CSI bude N sekund znovu sbírat vzorky.",no_events:"Žádné události",del_history:"Smazat celou historii?",noise_calib:"Spustit kalibraci šumu? (60s, nepohybujte se před senzorem)",tg_error:"Chyba",tg_unknown:"Neznámá",zone_name:"Název",zone_immediate:"🚨 Okamžité",zone_delay:"Zpoždění (ms)",zone_behavior:"Chování alarmu v zóně",zone_default:"Zóna",zones_saved:"Zóny uloženy",save_error:"Chyba při ukládání",starting:"Spouštím...",apply:"Použít",zone_added:"→ Zóna přidána, nezapomeň uložit!",gates_saved:"Hradla uložena",gates_error:"Chyba při ukládání hradel",preset_applied:"Předvolba nastavena",restarting:"Restartování...",enter_creds:"Zadejte uživatelské jméno a heslo",pass_mismatch:"Hesla se neshodují",pass_changed:"Heslo změněno",creds_changed:"Přihlašovací údaje změněny. Zařízení se restartuje.",conn_lost:"Spojení ztraceno",default_pass_warn:"⚠️ Výchozí heslo admin/admin — změňte v sekci Síť &amp; Cloud",net_section:"Ethernet síť",net_mode:"Režim",net_dhcp:"DHCP (automaticky)",net_static:"Statická IP",net_ip:"IP adresa",net_subnet:"Maska sítě",net_gateway:"Brána",net_dns:"DNS server",net_mac:"MAC",net_link:"Link",net_save:"💾 Uložit a restartovat",net_confirm:"Uložit síťové nastavení a restartovat zařízení? Ztratíš spojení pokud se IP změní.",net_ip_invalid:"Neplatná IP, brána nebo maska.",timeline_title:"TIMELINE UDÁLOSTÍ",total:"celkem",no_timeline:"Nedostatek dat pro timeline",tgen_mode:"Režim",tgen_port:"Cílový port",tgen_pps:"Paketů/s (PPS):",actions:"AKCE",config:"KONFIGURACE",not_enough:"Nedostatek dat.",static_label:"Statika",auto_calib:"📐 Auto-kalibrace prahu (10s)",reset_baseline:"♻️ Reset idle baseline",reconnect_wifi:"📶 Reconnect WiFi",csi_help:"<b>Auto-kalibrace:</b> 10s vzorkuje variance v klidu, nastaví práh = mean×1.5. Použij v prázdné místnosti.<br><b>Reset baseline:</b> vyčistí idle hodnoty. Po přesunu senzoru.<br><b>Reconnect WiFi:</b> restart asociace při RSSI dropech.",csi_rssi_quality:"Kvalita WiFi pro CSI",csi_rssi_hot:"Příliš silný — oddal zařízení od AP",csi_rssi_hot_hint:"Signál je silnější než −40 dBm. Pro CSI může u blízkého AP dojít k saturaci; posuň zařízení dále.",csi_rssi_good:"Vhodná",csi_rssi_weak:"Slabý — CSI může mít nízké SNR",csi_rssi_unavailable:"WiFi nepřipojena",site_learning:"SITE LEARNING (dlouhodobé)",learn_status:"Stav učení",learn_elapsed:"Uplynulo / cíl",learn_samples:"Přijaté / zamítnuté (pohyb / radar)",learn_bssid_resets:"BSSID resety",learn_thr_est:"Odhad prahu",learn_duration:"Délka učení:",learn_start:"▶️ Spustit učení",learn_stop:"⏹ Zastavit",learn_clear:"🗑 Smazat model",learn_confirm_start:"Spustit site learning na {h}? Místnost by měla být prázdná.",learn_confirm_stop:"Zastavit probíhající učení?",learn_confirm_clear:"Smazat naučený model? Detekce se vrátí na tovární práh.",learn_idle:"Neaktivní",learn_running:"Běží",learn_done:"Dokončeno",learned_model:"NAUČENÝ MODEL",learned_ready:"Model připraven",learned_thr:"Naučený práh",learned_mean:"Průměrná variance",learned_std:"Směrodatná odchylka variance",learned_max:"Maximální variance",learned_samples:"Vzorků",learn_refresh:"Vzorky obnovy EMA",learn_help:"<b>Site learning:</b> dlouhodobé vzorkování variance v prázdné místnosti (doporučeno 24–72 h). Radar-gate (LD2412) odfiltruje statické lidi. Po dokončení se automaticky nastaví <code>threshold = mean + 3×std</code>. <b>Učení přežívá OTA flash</b> (uloženo v NVS). Smazat model = reset na tovární hodnoty.",ml_mlp:"ML (MLP 17→18→9→1)",ml_enabled_lbl:"ML povoleno",ml_motion_lbl:"ML stav",ml_prob_lbl:"Pravděpodobnost",ml_threshold_lbl:"ML práh (enter):",ml_help:"<b>MLP klasifikátor:</b> 17 featur (turbulence — chaotičnost signálu, fáze, DSER — poměr Dopplerovy spektrální energie, PLCR — korelace úrovně fáze) → 18→9→1 sigmoid. Trénováno na espectre datasetu (F1 = 0.852). Enter ≥ threshold, exit = threshold × 0.70, N/M smoothing 4/5 z 6 oken. Výstup jde do fusion jako 3. signál.",detection_src:"Zdroj detekce",fusion_enabled:"Fusion povoleno",fusion_title:"Fúze — kdo vidí pohyb",radar_lbl:"Radar",feedback_false_alarm:"Falešný poplach",feedback_confirm_motion:"Potvrdit pohyb",feedback_saved:"Uloženo pro trénink",mw_radar_section:"MW radar",csi_section:"WiFi CSI",csi_offline:"CSI offline",csi_nodata:"Bez dat",radar_disconnected:"Radar odpojen",radar_show:"▸ Radar nepřipojen — zobrazit",radar_hide:"▾ Skrýt radar",csi_metrics_title:"CSI METRIKY (expert)",variance_window:"Rozptyl signálu (variance, okno)",src_ml:"Strojové učení",enabled:"Povoleno",hysteresis:"Práh ukončení pohybu (exit multiplier):",window_size:"Velikost okna (vzorky):",pub_interval:"Interval publikace (ms):",gate_out_of_range:"mimo rozsah",tz_section:"ČAS &amp; ZÓNA",tz_preset:"Předvolba",tz_custom_std:"Standardní offset (s)",tz_custom_dst:"Letní čas (s)",tz_save:"💾 Uložit časovou zónu",tz_saved:"Časová zóna uložena",sched_section:"NAPLÁNOVÁNÍ",sched_arm:"Čas auto-armování",sched_disarm:"Čas auto-odarmování",sched_days:"Dny",sched_save:"💾 Uložit naplánování",sched_saved:"Naplánování uloženo",sched_hint:"Zařízení se sám (od)armuje dle nastavené časové zóny. Formát HH:MM, prázdné = vypnuto.",sched_auto_arm:"Auto-arm po nečinnosti (min, 0 = vyp)",dow_mo:"Po",dow_tu:"Út",dow_we:"St",dow_th:"Čt",dow_fr:"Pá",dow_sa:"So",dow_su:"Ne",distance_cm:"VZDÁLENOST (cm)",comm_errors:"Chyby komunikace",restart_radar:"Restart radaru",restart_esp:"Restart ESP",restart_esp_confirm:"Restartovat ESP?",reset_mw:"Reset MW",min_range:"Min. dosah (hradlo)",max_range:"Max. dosah (hradlo)",enable_led:"Povolit LED (indikátor)",calib_noise:"Kalibrovat šum (60s)",interval_h:"Interval (h)",pet_immunity_title:"IMUNITA NA ZVÍŘATA",min_move_energy:"Min. energie pohybu",alarm_delay_title:"ZPOŽDĚNÍ ALARMU",absence_timeout_title:"ČASOVÝ LIMIT NEPŘÍTOMNOSTI",unmanned_duration:"Doba bez obsluhy (sec)",light_sensor_title:"SENZOR SVĚTLA (OUT pin)",light_off:"Vypnuto",enable_mqtt:"Povolit MQTT",telegram_notifications:"Telegram notifikace",enable_bot:"Povolit bota",credentials_title:"PŘIHLAŠOVACÍ ÚDAJE",zone_definitions:"DEFINICE ZÓN (cm)",csi_status_title:"STAV CSI",packets_per_s:"Pakety/s",motion_detection_title:"DETEKCE POHYBU",fusion_state:"Fusion stav",detected_state:"DETEKCE",fusion_on:"Fusion zapnut",fusion_off:"Fusion vypnut",error:"Chyba",comm_error:"Chyba komunikace",zone_from:"Od (cm)",zone_to:"Do (cm)",zone_entry_delay:"⏱ Zpoždění vstupu",zone_ignore:"🔕 Ignorovat",zone_ignore_static:"📡 Ignorovat statiku",preset_error:"Chyba předvolby",min_4_chars:"Min. 4 znaky",learn_complete:"Hotovo!",sec_radar_adv:"Pokročilá konfigurace radaru",ph_mqtt_server:"IP adresa serveru",ph_mqtt_port:"Port (1883)",ph_username:"Uživatelské jméno",ph_password:"Heslo",ph_bot_token:"Token bota",ph_chat_id:"ID chatu",ph_new_password:"Nové heslo",ph_confirm_pass:"Potvrdit heslo",ph_tz_std:"Standardní offset (s)",ph_tz_dst:"Letní čas offset (s)",ph_wifi_pass_hint:"(nemění se pokud prázdné při edit)",ph_md5_required:"MD5 kontrolní součet (povinný)"},en:{title:"LD2412 Security",loading:"LOADING...",arm:"ARM",disarm:"DISARM",disarmed:"🔓 DISARMED",arming:"⏳ ARMING...",armed:"🔒 ARMED",pending:"⚠️ PENDING",triggered:"🚨 TRIGGERED",sensor_health:"Stav senzoru",uart_state:"Stav UART",frame_rate:"Snímková frekvence",ram:"RAM (Free/Min)",chip_temp:"Chip Temperature",uptime:"Uptime",factory_reset_confirm:"Really perform radar factory reset?",tab_basic:"Basic",tab_security:"Security",tab_gates:"Gate sensitivity",tab_network:"Network & Cloud",tab_zones:"Zones",tab_events:"Events",tab_csi:"CSI",device_name:"Device Name (mDNS)",hold_time:"Hold Time (s)",move_sens:"Movement Sensitivity (%)",enable_diag:"Enable Diagnostics",radar_bt:"Radar Bluetooth (HLK app pairing)",radar_bt_warn:"⚠️ Leave OFF in deployed units — anyone in BT range can otherwise pair with the radar via the HLK mobile app. Enable only for diagnostics.",radar_bt_apply:"Radar will restart to apply the change.",antimask_title:"ANTI-MASKING (Tamper by Covering)",antimask_enable:"Enable silence alarm",timeout_sec:"Timeout (sec)",antimask_hint:"For warehouses, cabins, server rooms <b>DISABLE</b> — silence is normal.<br>For occupied spaces ENABLE — detects sensor covering.",loiter_title:"LOITERING (Suspicious Lingering)",loiter_enable:"Loitering notification",loiter_hint:"Alarm when someone stands &lt;2m from sensor longer than timeout.",hb_title:"HEARTBEAT (Periodic Report)",hb_hint:"0 = disabled, 4 = every 4 hours 'I'm OK' message.",pet_hint:"Filters small objects (cats, dogs) with low energy &lt;2m.",entry_delay:"Entry Delay (sec)",exit_delay:"Exit Delay (sec)",disarm_reminder:'Reminder "Still DISARMED"',hold_hint:"Duration after which radar reports 'no presence' without detection.",light_func:"Light Function",light_night:"Night mode (below threshold)",light_day:"Day mode (above threshold)",light_thr:"Light Threshold (0-255)",light_cur:"Current Light",light_hint:"OUT pin active only when light is below/above threshold.<br>Ideal for night security ('below threshold' mode).",movement:"Movement",static_:"Static",gate_legend:"Higher = more sensitive &middot; <span style='opacity:0.4'>gray = outside min/max range</span>",set_all:"Set all:",indoor:"Indoor",outdoor:"Outdoor",pets:"Pets",save_gates:"Save sensitivity",save_mqtt:"Save MQTT",save:"Save",username:"Username",new_pass:"New Password",change_pass:"Change Password",cfg_backup:"CONFIG BACKUP",cfg_export:"💾 Export",cfg_import:"📂 Import",cfg_import_confirm:"Replace all settings? Device will reboot.",cfg_import_ok:"Config imported, rebooting...",cfg_import_err:"Import failed: {err}",add_zone:"+ Add Zone",save_zones:"💾 Save Zones",learn_static:"📡 Learn Static",filter_all:"All",filter_alarm:"Alarm",filter_move:"Movement",filter_tamper:"Tamper",filter_hb:"Heartbeat",filter_sys:"System",filter_net:"Network",delete:"Delete",now:"now",load_more:"Load more...",csi_warn:"This firmware was not compiled with WiFi CSI support (missing <code>-D USE_CSI=1</code>). To enable, upload the <b>esp32_poe_csi</b> variant.",csi_active:"Active",csi_idle:"Idle baseline ready",ap_compat:"AP compatibility (HT LTF)",ap_ok:"OK",ap_incompat:"INCOMPATIBLE",ap_checking:"waiting for HT LTF…",ap_incompat_hint:"AP is not emitting 802.11n HT LTF frames (likely WiFi 6 hardware in n-mode). CSI is still passive but motion detection won't work. Use a pre-WiFi 6 AP.",wifi_ap:"WIFI AP (CSI)",wifi_ssid:"SSID",wifi_pass:"Password",wifi_ap_hint:"Switch AP for the CSI sensor. Saving requires reboot. An empty password keeps the current one; the default button restores <code>secrets.h</code>.",save_reboot:"💾 Save and reboot",wifi_use_default:"↩️ Compile-time default",wifi_scan:"🔎 Scan nearby WiFi",wifi_scanning:"Scanning networks…",wifi_scan_hint:"Scanning pauses CSI capture for a few seconds. It is blocked during site learning, calibration, or OTA.",wifi_scan_found:"Networks found: {n}. Click to select an SSID.",wifi_scan_none:"No visible networks",wifi_scan_failed:"WiFi scan failed",wifi_scan_busy:"WiFi scan is unavailable while maintenance or learning is active.",wifi_current:"connected",wifi_channel:"channel",wifi_pass_required:"Enter a password for the new secured network.",wifi_ssid_empty:"SSID cannot be empty",wifi_save_confirm:"Save SSID '{ssid}' and reboot? If the password is wrong the ESP won't associate and a factory reset (GPIO0 5s) will be required.",wifi_saved_reboot:"Saved. Rebooting…",wifi_save_failed:"Save failed",wifi_reset_confirm:"Restore compile-time default SSID from secrets.h and reboot?",wifi_reset_reboot:"Reset. Rebooting…",motion_state:"Motion State",composite:"Overall motion score (composite)",threshold_label:"Motion detection threshold (variance):",save_config:"Save Configuration",upload_fw:"Upload Firmware",saved:"Saved",fw_update_title:"Firmware Update",pull_ota_title:"Pull OTA from URL",pull_ota_help:"HTTPS Pull OTA requires a saved CA; HTTP remains a private-LAN compatibility mode. Redirects are not followed and MD5 remains required for artifact integrity.",pull_ota_btn:"⤓ Fetch & flash",pull_ota_running:"Fetching & flashing…",pull_ota_phase_idle:"—",pull_ota_phase_connecting:"Connecting…",pull_ota_phase_downloading:"Downloading…",pull_ota_phase_writing:"Writing firmware…",pull_ota_phase_success:"✅ Done, rebooting",pull_ota_phase_error:"❌ Error",pull_ota_no_url:"Enter URL to firmware .bin",pull_ota_bad_md5:"MD5 must be 32 hex characters",ota_cold_label:"Reboot device before OTA (recommended for 100% success)",ota_cold_restart:"Rebooting device...",ota_waiting_reboot:"Waiting for device",ota_ready_uploading:"Device up, uploading...",ota_uploading:"Uploading firmware...",ota_cold_failed:"Device did not come back within 25 s — check power and retry",ota_espota_prepare:"Prepare espota window",ota_espota_ready:"ESPOTA window open for 120 s",ota_espota_help:"For flashing with espota.py (UDP 3232) instead of file upload. For ~120 s it drops CSI WiFi (single-home), backs off CSI/MQTT and suspends the radar → frees CPU/RAM so espota reliably passes auth. Open the window, then run espota.py from the same LAN. No reboot needed.",yes:"YES",no:"NO",no_collecting:"NO — collecting samples",motion:"MOTION",idle:"IDLE",coverage:"Coverage",resolution:"Resolution",gate:"gate",hold_state:"HOLD",tamper_state:"TAMPER!",csi_restart:"Changing CSI requires ESP restart. Restart now?",calib_started:"Calibration started (10s, keep room empty)",reset_confirm:"Reset idle baseline? CSI will re-collect samples for a few seconds.",no_events:"No events",del_history:"Delete entire history?",noise_calib:"Start noise calibration? (60s, do not move in front of sensor)",tg_error:"Error",tg_unknown:"Unknown",zone_name:"Name",zone_immediate:"🚨 Immediate",zone_delay:"Delay (ms)",zone_behavior:"Zone alarm behavior",zone_default:"Zone",zones_saved:"Zones saved",save_error:"Save error",starting:"Starting...",apply:"Apply",zone_added:"→ Zone added, don't forget to save!",gates_saved:"Gates saved",gates_error:"Error saving gates",preset_applied:"Preset applied",restarting:"Restarting...",enter_creds:"Enter username and password",pass_mismatch:"Passwords don't match",pass_changed:"Password changed",creds_changed:"Credentials changed. Device will restart.",conn_lost:"Connection lost",default_pass_warn:"⚠️ Default password admin/admin — change in Network &amp; Cloud",net_section:"Ethernet network",net_mode:"Mode",net_dhcp:"DHCP (automatic)",net_static:"Static IP",net_ip:"IP address",net_subnet:"Subnet mask",net_gateway:"Gateway",net_dns:"DNS server",net_mac:"MAC",net_link:"Link",net_save:"💾 Save & reboot",net_confirm:"Save network config and reboot the device? You'll lose connection if the IP changes.",net_ip_invalid:"Invalid IP, gateway or subnet.",timeline_title:"EVENT TIMELINE",total:"total",no_timeline:"Not enough data for timeline",tgen_mode:"Mode",tgen_port:"Target Port",tgen_pps:"Packets/s (PPS):",actions:"ACTIONS",config:"CONFIGURATION",not_enough:"Not enough data.",static_label:"Static",auto_calib:"📐 Auto-calibrate threshold (10s)",reset_baseline:"♻️ Reset idle baseline",reconnect_wifi:"📶 Reconnect WiFi",csi_help:"<b>Auto-calibration:</b> 10s variance sampling in idle, sets threshold = mean×1.5. Use in empty room.<br><b>Reset baseline:</b> clears idle values. After moving sensor.<br><b>Reconnect WiFi:</b> restarts association on RSSI drops.",csi_rssi_quality:"WiFi quality for CSI",csi_rssi_hot:"Too strong — move device away from AP",csi_rssi_hot_hint:"Signal is stronger than −40 dBm. A nearby AP can saturate CSI; move the device farther away.",csi_rssi_good:"Suitable",csi_rssi_weak:"Weak — CSI may have low SNR",csi_rssi_unavailable:"WiFi disconnected",site_learning:"SITE LEARNING (long-term)",learn_status:"Learning state",learn_elapsed:"Elapsed / target",learn_samples:"Accepted / rejected (motion / radar)",learn_bssid_resets:"BSSID resets",learn_thr_est:"Threshold estimate",learn_duration:"Learning duration:",learn_start:"▶️ Start learning",learn_stop:"⏹ Stop",learn_clear:"🗑 Clear model",learn_confirm_start:"Start site learning for {h}? Room should be empty.",learn_confirm_stop:"Stop ongoing learning?",learn_confirm_clear:"Clear learned model? Detection falls back to factory threshold.",learn_idle:"Idle",learn_running:"Running",learn_done:"Done",learned_model:"LEARNED MODEL",learned_ready:"Model ready",learned_thr:"Learned threshold",learned_mean:"Mean variance",learned_std:"Std variance",learned_max:"Max variance",learned_samples:"Samples",learn_refresh:"EMA refresh samples",learn_help:"<b>Site learning:</b> long-term variance sampling in an empty room (24–72 h recommended). LD2412 radar-gate rejects stationary humans. On completion, <code>threshold = mean + 3×std</code> is applied automatically. <b>Learned model survives OTA flash</b> (stored in NVS). Clearing the model resets to factory threshold.",ml_mlp:"ML (MLP 17→18→9→1)",ml_enabled_lbl:"ML enabled",ml_motion_lbl:"ML state",ml_prob_lbl:"Probability",ml_threshold_lbl:"ML threshold (enter):",ml_help:"<b>MLP classifier:</b> 17 features (turbulence — signal chaoticness, phase, DSER — Doppler Spectral Energy Ratio, PLCR — Phase-Level Correlation Ratio) → 18→9→1 sigmoid. Trained on espectre dataset (F1 = 0.852). Enter ≥ threshold, exit = threshold × 0.70, N/M smoothing 4/5 of 6 windows. Output is fed into fusion as 3rd signal.",detection_src:"Detection source",fusion_enabled:"Fusion enabled",fusion_title:"Fusion — who sees motion",radar_lbl:"Radar",feedback_false_alarm:"False alarm",feedback_confirm_motion:"Confirm motion",feedback_saved:"Saved for training",mw_radar_section:"MW radar",csi_section:"WiFi CSI",csi_offline:"CSI offline",csi_nodata:"No data",radar_disconnected:"Radar disconnected",radar_show:"▸ Radar not connected — show",radar_hide:"▾ Hide radar",csi_metrics_title:"CSI METRICS (expert)",variance_window:"Signal variance (window)",src_ml:"Machine learning",enabled:"Enabled",hysteresis:"Motion exit threshold (multiplier):",window_size:"Window size (samples):",pub_interval:"Publish interval (ms):",gate_out_of_range:"out of range",tz_section:"TIME &amp; TIMEZONE",tz_preset:"Preset",tz_custom_std:"Standard offset (s)",tz_custom_dst:"DST offset (s)",tz_save:"💾 Save timezone",tz_saved:"Timezone saved",sched_section:"SCHEDULE",sched_arm:"Auto-arm time",sched_disarm:"Auto-disarm time",sched_days:"Days",sched_save:"💾 Save schedule",sched_saved:"Schedule saved",sched_hint:"Device auto-arms/disarms according to the configured timezone. Format HH:MM, empty = disabled.",sched_auto_arm:"Auto-arm after idle (min, 0 = off)",dow_mo:"Mo",dow_tu:"Tu",dow_we:"We",dow_th:"Th",dow_fr:"Fr",dow_sa:"Sa",dow_su:"Su",distance_cm:"DISTANCE (cm)",comm_errors:"Comm Errors",restart_radar:"Restart Radar",restart_esp:"Restart ESP",restart_esp_confirm:"Restart ESP?",reset_mw:"Reset MW",min_range:"Min Range (Gate)",max_range:"Max Range (Gate)",enable_led:"Enable LED (Indicator)",calib_noise:"Calibrate Noise (60s)",interval_h:"Interval (h)",pet_immunity_title:"PET IMMUNITY",min_move_energy:"Min Move Energy",alarm_delay_title:"ALARM DELAY",absence_timeout_title:"ABSENCE TIMEOUT",unmanned_duration:"Unmanned Duration (sec)",light_sensor_title:"LIGHT SENSOR (OUT pin)",light_off:"Off",enable_mqtt:"Enable MQTT",telegram_notifications:"Telegram Notifications",enable_bot:"Enable Bot",credentials_title:"CREDENTIALS",zone_definitions:"ZONE DEFINITIONS (cm)",csi_status_title:"CSI STATUS",packets_per_s:"Packets/s",motion_detection_title:"MOTION DETECTION",fusion_state:"Fusion state",detected_state:"DETECTED",fusion_on:"Fusion enabled",fusion_off:"Fusion disabled",error:"Error",comm_error:"Communication error",zone_from:"From (cm)",zone_to:"To (cm)",zone_entry_delay:"⏱ Entry delay",zone_ignore:"🔕 Ignore",zone_ignore_static:"📡 Ignore static",preset_error:"Preset error",min_4_chars:"Min. 4 characters",learn_complete:"Done!",sec_radar_adv:"Advanced radar configuration",ph_mqtt_server:"Server IP",ph_mqtt_port:"Port (1883)",ph_username:"Username",ph_password:"Password",ph_bot_token:"Bot Token",ph_chat_id:"Chat ID",ph_new_password:"New Password",ph_confirm_pass:"Confirm Password",ph_tz_std:"Standard offset (s)",ph_tz_dst:"DST offset (s)",ph_wifi_pass_hint:"(unchanged if empty when editing)",ph_md5_required:"MD5 checksum (required)"}};let LANG=localStorage.getItem("lang")||"en";function t(e){return I18N[LANG]&&I18N[LANG][e]||I18N.en[e]||e}function setLang(e){LANG=e,localStorage.setItem("lang",e),applyLang()}function applyLang(){document.querySelectorAll("[data-i18n]").forEach(e=>{let a=e.getAttribute("data-i18n");"INPUT"===e.tagName?e.placeholder=t(a):"OPTION"===e.tagName?e.textContent=t(a):e.innerHTML=t(a)}),document.querySelectorAll("[data-i18n-ph]").forEach(e=>{e.placeholder=t(e.getAttribute("data-i18n-ph"))}),document.querySelector("#lang_btn").textContent="cs"===LANG?"🇬🇧 EN":"🇨🇿 CZ",document.title=t("title"),document.documentElement.lang=LANG}
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
            <div id="mw_radar_block" class="radar-only">
                <div class="section-title" data-nocollapse style="text-align:left" data-i18n="mw_radar_section">MW radar</div>
                <div class="big-val" id="dist_val" style="color:var(--accent)">---</div>
                <div class="unit" data-i18n="distance_cm">DISTANCE (cm)</div>
                <svg class="spark" id="graph_dist"></svg>
            </div>
        </div>
        <div id="mw_movstat" class="radar-only" style="display:flex; gap:10px">
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
        <div id="radar_reveal" class="radar-reveal" style="display:none" onclick="toggleRadarReveal()">
            <span data-i18n="radar_show">▸ Radar nepřipojen — zobrazit</span>
        </div>
        <div id="csi_main_block">
            <div class="section-title" data-nocollapse data-i18n="csi_section">WiFi CSI</div>
            <div style="text-align:center; margin-top:4px">
                <div id="csi_main_state" style="font-weight:bold; font-size:1.3rem; color:#888">—</div>
                <div id="csi_main_link" class="unit" style="margin-top:2px">—</div>
            </div>
        </div>
    </div>

    <!-- FUSION EXPLAINABILITY (#8) — hidden until the first SSE frame carries a
         fusion object (CSI disabled / not compiled => permanently dash-filled card) -->
    <div class="card" id="fusion_panel" style="display:none">
        <div class="section-title" data-nocollapse data-i18n="fusion_title">Fúze — kdo vidí pohyb</div>
        <div class="fbars">
            <div class="fbar" id="fbar_radar"><div class="fbar-track"><div class="fbar-fill" id="fb_radar"></div></div><span data-i18n="radar_lbl">Radar</span></div>
            <div class="fbar"><div class="fbar-track"><div class="fbar-fill" id="fb_csi"></div></div><span>CSI</span></div>
            <div class="fbar"><div class="fbar-track"><div class="fbar-fill" id="fb_ml"></div></div><span>ML</span></div>
        </div>
        <div class="fgauge"><div class="fgauge-track"><div id="fg_fill" class="fgauge-fill"></div></div><span id="fg_val">–</span></div>
        <div class="freason" id="fusion_reason">–</div>
        <div style="display:flex; gap:8px; margin-top:8px">
            <button class="sec" style="flex:1; padding:5px; font-size:0.8rem" onclick="csiFeedback('no_motion')" data-i18n="feedback_false_alarm">Falešný poplach</button>
            <button class="sec" style="flex:1; padding:5px; font-size:0.8rem" onclick="csiFeedback('motion')" data-i18n="feedback_confirm_motion">Potvrdit pohyb</button>
        </div>
    </div>

    <!-- HEALTH & STATS -->
    <div class="card">
        <div class="stat-row radar-only"><span data-i18n="sensor_health">Zdraví senzoru</span><span id="h_score" class="stat-val">---%</span></div>
        <div class="stat-row radar-only"><span data-i18n="uart_state">UART Stav</span><span id="h_uart">---</span></div>
        <div class="stat-row radar-only"><span data-i18n="frame_rate">Snímková frekvence</span><span id="h_fps">--- FPS</span></div>
        <div class="stat-row radar-only"><span data-i18n="comm_errors">Comm Errors</span><span id="h_err" style="color:var(--warn)">0</span></div>
        <div class="stat-row"><span data-i18n="ram">RAM (Volná/Min)</span><span id="h_heap">--- / --- KB</span></div>
        <div class="stat-row"><span data-i18n="chip_temp">Teplota čipu</span><span id="h_temp">--- °C</span></div>
        <div class="stat-row"><span data-i18n="uptime">Doba běhu</span><span id="h_uptime">---</span></div>
        <div style="display:flex; gap:5px; margin-top:10px; flex-wrap: wrap;">
            <button class="sec radar-only" style="flex:1; min-width:80px;" onclick="api('radar/restart', {method:'POST'})" data-i18n="restart_radar">Restart Radar</button>
            <button class="sec" style="flex:1; min-width:80px;" onclick="if(confirm(t('restart_esp_confirm'))) api('restart', {method:'POST'})" data-i18n="restart_esp">Restart ESP</button>
            <button class="warn radar-only" style="flex:1; min-width:80px;" onclick="if(confirm(t('factory_reset_confirm'))) api('radar/factory_reset', {method:'POST'})" data-i18n="reset_mw">Reset MW</button>
        </div>
    </div>

    <!-- CONTROLS -->
    <div class="card">
        <div class="tabs">
            <div class="tab active" onclick="tab(0)" data-i18n="tab_basic">Základní</div>
            <div class="tab" onclick="tab(1)" data-i18n="tab_security">Bezpečnost</div>
            <div class="tab radar-only" onclick="tab(2)" data-i18n="tab_gates">Hradla</div>
            <div class="tab" onclick="tab(3)" data-i18n="tab_network">Síť & Cloud</div>
            <div class="tab radar-only" onclick="tab(4)" data-i18n="tab_zones">Zóny</div>
            <div class="tab" onclick="tab(5)" data-i18n="tab_events">Historie</div>
            <div class="tab" onclick="tab(6)" data-i18n="tab_csi">WiFi CSI</div>
        </div>

        <!-- TAB 0: BASIC -->
        <div id="tab0">
            <div class="stat-row"><span data-i18n="device_name">Jméno zařízení (mDNS)</span></div>
            <div style="display:flex; gap:5px; margin-bottom:10px">
                <input type="text" id="txt_hostname" placeholder="e.g. sensor-room1">
                <button class="sec" style="width:auto; margin:0" onclick="saveHostname()">OK</button>
            </div>

            <div class="radar-only">
            <div class="stat-row" style="margin-top:10px"><span data-i18n="move_sens">Citlivost Pohyb (%)</span></div>
            <input type="number" id="i_sens" min="0" max="100" onchange="saveBasic()">
            </div>

            <div style="display:flex; align-items:center; gap:8px; margin-top:15px; margin-bottom:5px">
                <input type="checkbox" id="chk_led" style="width:auto" onchange="saveBasic()">
                <label for="chk_led" data-i18n="enable_led">Enable LED (Indicator)</label>
            </div>

            <div class="section-title radar-only" data-collapsed data-i18n="sec_radar_adv">Pokročilá konfigurace radaru</div>
            <div class="radar-only">
            <div class="row-input">
                <span style="flex:1" data-i18n="min_range">Min Range (Gate)</span>
                <span id="i_min_cm" style="font-size:0.75rem; color:#888; margin-right:6px">—</span>
                <input type="number" id="i_min" min="0" max="13" style="width:60px" oninput="updGateCm()" onchange="saveBasic()">
            </div>
            <div class="row-input">
                <span style="flex:1" data-i18n="max_range">Max Range (Gate)</span>
                <span id="i_max_cm" style="font-size:0.75rem; color:#888; margin-right:6px">—</span>
                <input type="number" id="i_max" min="1" max="13" style="width:60px" oninput="updGateCm()" onchange="saveBasic()">
            </div>

            <div class="stat-row" style="margin-top:10px"><span data-i18n="hold_time">Doba držení (s)</span></div>
            <input type="number" id="i_hold" step="0.5" min="0" onchange="saveBasic()">

            <div style="display:flex; align-items:center; gap:8px; margin-bottom:10px; margin-top:10px">
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

            <button id="btn_calib" onclick="startCalib()" style="margin-top:15px" data-i18n="calib_noise">Calibrate Noise (60s)</button>
            </div>
        </div>

        <!-- TAB 1: SECURITY -->
        <div id="tab1" class="hidden">
            <div class="radar-only">
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
            </div>

            <div class="section-title"><span data-i18n="hb_title">HEARTBEAT (Pravidelný report)</span></div>
            <div class="row-input">
                <span data-i18n="interval_h">Interval (h)</span>
                <input type="number" id="i_hb" placeholder="4" min="0" max="24" style="width:80px" onchange="saveSec()">
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0 15px 0">
                <span data-i18n="hb_hint">0 = vypnuto, 4 = každé 4 hodiny zpráva "jsem OK".</span>
            </p>

            <div class="radar-only">
            <div class="section-title" data-i18n="pet_immunity_title">PET IMMUNITY</div>
            <div class="row-input">
                <span data-i18n="min_move_energy">Min Move Energy</span>
                <input type="number" id="i_pet" placeholder="10" min="0" max="50" style="width:80px" onchange="saveSec()">
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0">
                <span data-i18n="pet_hint">Filtruje malé objekty (kočky, psi) s nízkou energií &lt;2m.</span>
            </p>
            </div>

            <div class="section-title" data-i18n="alarm_delay_title">ALARM DELAY</div>
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

            <div class="radar-only">
            <div class="section-title" data-i18n="absence_timeout_title">ABSENCE TIMEOUT</div>
            <div class="row-input">
                <span data-i18n="unmanned_duration">Unmanned Duration (sec)</span>
                <input type="number" id="i_timeout" placeholder="10" min="0" max="255" style="width:80px" onchange="saveTimeout()">
            </div>
            <p style="font-size:0.7rem; color:#666; margin:2px 0 15px 0">
                <span data-i18n="hold_hint">Doba, po které radar hlásí "nepřítomnost" bez detekce.</span>
            </p>

            <div class="section-title" data-i18n="light_sensor_title">LIGHT SENSOR (OUT pin)</div>
            <div class="row-input">
                <span data-i18n="light_func">Funkce světla</span>
                <select id="sel_light_func" style="width:140px" onchange="saveLightConfig()">
                    <option value="0" data-i18n="light_off">Off</option>
                    <option value="1" data-i18n="light_night">Noční režim (pod práh)</option>
                    <option value="2" data-i18n="light_day">Denní režim (nad práh)</option>
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
        </div>

        <!-- TAB 2: GATES -->
        <div id="tab2" class="hidden">
            <div id="range_summary" style="font-size:0.8rem; color:#888; margin-bottom:8px; text-align:center"></div>

            <div style="font-size:0.75rem; color:#888; margin-bottom:8px; line-height:1.5">
                <span style="color:#03dac6; font-weight:bold">&#9632; <span data-i18n="movement">Pohyb</span></span> &middot;
                <span style="color:#bb86fc; font-weight:bold">&#9632; <span data-i18n="static_">Statika</span></span><br>
                <span data-i18n="gate_legend">Vyšší = citlivější &middot; <span style="opacity:0.4">šedá = mimo min/max rozsah</span></span>
            </div>

            <div style="background:#111; border-radius:8px; padding:8px; margin-bottom:8px">
                <div style="font-size:0.75rem; color:#888; margin-bottom:4px" data-i18n="set_all">Nastavit vše:</div>
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
                <button class="sec" style="flex:1; padding:5px; font-size:0.8rem" onclick="setPreset('indoor')" data-i18n="indoor">Interiér</button>
                <button class="sec" style="flex:1; padding:5px; font-size:0.8rem" onclick="setPreset('outdoor')" data-i18n="outdoor">Exteriér</button>
                <button class="sec" style="flex:1; padding:5px; font-size:0.8rem" onclick="setPreset('pet')" data-i18n="pets">Zvířata</button>
            </div>

            <div id="gates_container" style="max-height:400px; overflow-y:auto"></div>

            <button onclick="saveGates()" style="margin-top:8px" data-i18n="save_gates">Uložit Hradla</button>
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
                <input type="number" id="tz_std_in" placeholder="Standard offset (s)" data-i18n-ph="ph_tz_std" oninput="updateTzLabel()">
                <input type="number" id="tz_dst_in" placeholder="DST offset (s)" data-i18n-ph="ph_tz_dst" oninput="updateTzLabel()">
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
                <label for="chk_mqtt_en" data-i18n="enable_mqtt">Enable MQTT</label>
            </div>
            <input type="text" id="txt_mqtt_server" placeholder="Server IP" data-i18n-ph="ph_mqtt_server">
            <div style="display:flex; gap:5px">
                <input type="text" id="txt_mqtt_port" placeholder="Port (1883)" data-i18n-ph="ph_mqtt_port">
                <input type="text" id="txt_mqtt_user" placeholder="Username" data-i18n-ph="ph_username">
            </div>
            <input type="password" id="txt_mqtt_pass" placeholder="Password" data-i18n-ph="ph_password">
            <button onclick="saveMQTTConfig()" class="sec" data-i18n="save_mqtt">Uložit MQTT</button>

            <div class="section-title" data-i18n="telegram_notifications">Telegram Notifications</div>
            <div style="display:flex; align-items:center; gap:8px;">
                <input type="checkbox" id="chk_tg_en" style="width:auto">
                <label for="chk_tg_en" data-i18n="enable_bot">Enable Bot</label>
            </div>
            <input type="text" id="txt_tg_token" placeholder="Bot Token" data-i18n-ph="ph_bot_token">
            <input type="text" id="txt_tg_chat" placeholder="Chat ID" data-i18n-ph="ph_chat_id">
            <div style="display:flex; gap:5px">
                <button onclick="saveTelegram()" class="sec" data-i18n="save">Uložit</button>
                <button onclick="testTelegram()" class="sec">Test</button>
            </div>
            <div class="section-title" data-i18n="credentials_title">CREDENTIALS</div>
            <input type="text" id="txt_auth_user" placeholder="Username" data-i18n-ph="ph_username">
            <input type="password" id="txt_auth_pass" placeholder="New Password" data-i18n-ph="ph_new_password">
            <input type="password" id="txt_auth_pass2" placeholder="Confirm Password" data-i18n-ph="ph_confirm_pass">
            <button onclick="saveAuth()" class="warn" data-i18n="change_pass">Změnit heslo</button>

            <div class="section-title" data-i18n="cfg_backup">ZÁLOHA KONFIGURACE</div>
            <div style="display:flex; gap:5px">
              <button onclick="exportConfig()" class="sec" data-i18n="cfg_export">💾 Exportovat</button>
              <button onclick="importConfigPrompt()" class="sec" data-i18n="cfg_import">📂 Importovat</button>
            </div>
            <input type="file" id="cfg_import_file" accept="application/json" style="display:none" onchange="importConfig(this.files[0])">
        </div>

        <!-- TAB 4: ZONES -->
        <div id="tab4" class="hidden">
            <div class="label" style="margin-bottom:10px; font-size:0.8rem; color:#888" data-i18n="zone_definitions">ZONE DEFINITIONS (cm)</div>
            <!-- SVG Zone Map -->
            <div style="position:relative; margin-bottom:10px">
                <svg id="zone_map" width="100%" height="48" style="display:block"></svg>
                <div style="position:absolute; bottom:2px; left:4px; font-size:0.65rem; color:#555" id="zone_map_scale"></div>
            </div>
            <div id="zones_list"></div>
            <button onclick="addZone()" class="sec" style="margin-top:10px" data-i18n="add_zone">+ Přidat Zónu</button>
            <button onclick="saveZones()" data-i18n="save_zones">💾 Uložit Zóny</button>
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
                    <button onclick="startLearn()" id="btn_learn" class="sec" style="flex:1" data-i18n="learn_static">📡 Naučit statiku</button>
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
                        <option value="-1" data-i18n="filter_all">Vše</option>
                        <option value="5" data-i18n="filter_alarm">Alarm</option>
                        <option value="1" data-i18n="filter_move">Pohyb</option>
                        <option value="2" data-i18n="filter_tamper">Tamper</option>
                        <option value="4" data-i18n="filter_hb">Heartbeat</option>
                        <option value="0" data-i18n="filter_sys">Systém</option>
                        <option value="3" data-i18n="filter_net">Síť</option>
                    </select>
                    <button onclick="exportEvents()" class="sec" style="width:auto; padding:4px 8px; margin:0; font-size:0.8rem">CSV</button>
                    <button onclick="clearEvents()" class="warn" style="width:auto; padding:4px 8px; margin:0; font-size:0.8rem" data-i18n="delete">Smazat</button>
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
            <button id="evt_load_more" onclick="loadMoreEvents()" class="sec" style="display:none; margin-top:8px; font-size:0.85rem" data-i18n="load_more">Načíst další...</button>
        </div>

        <!-- TAB 6: WIFI CSI -->
        <div id="tab6" class="hidden">
            <div id="csi_compiled_warn" style="display:none; padding:10px; background:#3a1010; border-left:3px solid var(--warn); margin-bottom:12px; font-size:0.85rem">
                ⚠️ <span data-i18n="csi_warn">Tento firmware nebyl zkompilován s podporou WiFi CSI.</span>
            </div>

            <div class="section-title" data-i18n="csi_status_title">STAV CSI</div>
            <div class="stat-row"><span data-i18n="csi_active">Aktivní</span><span id="csi_active_val" style="font-weight:bold">—</span></div>
            <div class="stat-row"><span>WiFi SSID</span><span id="csi_ssid_val">—</span></div>
            <div class="stat-row"><span>WiFi RSSI</span><span id="csi_rssi_val">—</span></div>
            <div class="stat-row"><span data-i18n="csi_rssi_quality">Kvalita WiFi pro CSI</span><span id="csi_rssi_quality_val">—</span></div>
            <div class="stat-row"><span data-i18n="packets_per_s">Pakety/s</span><span id="csi_pps_val">—</span></div>
            <div class="stat-row"><span data-i18n="csi_idle">Idle baseline připraven</span><span id="csi_idle_val">—</span></div>
            <div class="stat-row"><span data-i18n="ap_compat">AP kompatibilita (HT LTF)</span><span id="csi_ap_compat" style="font-weight:bold">—</span></div>

            <div class="section-title" data-i18n="motion_detection_title">DETEKCE POHYBU</div>
            <div class="stat-row">
                <span data-i18n="motion_state">Stav pohybu</span>
                <span id="csi_motion_val" style="font-weight:bold; font-size:1.2rem">—</span>
            </div>
            <div class="section-title" data-collapsed data-i18n="csi_metrics_title">CSI METRIKY (expert)</div>
            <div class="stat-row"><span data-i18n="composite" title="Souhrnné skóre změn v CSI signálu — vyšší = větší pohyb">Composite skóre</span><span id="csi_comp_val">—</span></div>
            <div class="stat-row"><span data-i18n="variance_window" title="Rozptyl signálu v posledním okně vzorků">Variance (okno)</span><span id="csi_var_val">—</span></div>
            <svg id="csi_graph" viewBox="0 0 100 50" style="width:100%; height:60px; background:#1a1a1a; margin-top:5px; stroke:#03dac6"></svg>

            <div class="section-title">FÚZE (Radar + CSI)</div>
            <div class="stat-row">
                <span data-i18n="fusion_state">Fusion stav</span>
                <span id="fus_presence_val" style="font-weight:bold; font-size:1.2rem">—</span>
            </div>
            <div class="stat-row"><span>Spolehlivost</span><span id="fus_conf_val">—</span></div>
            <div class="stat-row"><span data-i18n="detection_src">Zdroj detekce</span><span id="fus_source_val">—</span></div>
            <div class="stat-row">
                <span data-i18n="fusion_enabled">Fusion povoleno</span>
                <label class="switch"><input type="checkbox" id="fus_en" onchange="toggleFusion(this.checked)"><span class="slider"></span></label>
            </div>

            <div class="section-title" data-collapsed><span data-i18n="config">KONFIGURACE</span></div>

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

            <div class="section-title" data-collapsed style="margin-top:14px">GENERÁTOR PROVOZU</div>
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

            <button onclick="saveCSIConfig()" style="margin-top:12px" data-i18n="save_config">Uložit konfiguraci</button>

            <div class="section-title"  data-i18n="wifi_ap">WIFI AP (CSI)</div>
            <div style="font-size:0.75rem; color:#888; margin-bottom:6px" data-i18n="wifi_ap_hint">
                Přepnutí AP pro CSI senzor. Uložení vyžaduje reboot. Prázdné heslo ponechá stávající; tlačítko default obnoví údaje ze <code>secrets.h</code>.
            </div>
            <label style="font-size:0.85rem" data-i18n="wifi_ssid">SSID</label>
            <input type="text" id="csi_wifi_ssid" maxlength="32" placeholder="—" oninput="csiWifiSelectionChanged()" style="margin-bottom:6px">
            <label style="font-size:0.85rem" data-i18n="wifi_pass">Heslo</label>
            <input type="password" id="csi_wifi_pass" maxlength="64" placeholder="(nemění se pokud prázdné při edit)" data-i18n-ph="ph_wifi_pass_hint" style="margin-bottom:6px">
            <button id="csi_wifi_scan_btn" onclick="startCsiWifiScan()" class="sec" data-i18n="wifi_scan">🔎 Vyhledat okolní WiFi</button>
            <div style="font-size:0.72rem; color:#777; margin-top:4px" data-i18n="wifi_scan_hint">Scan na několik sekund pozastaví CSI příjem.</div>
            <div id="csi_wifi_scan_status" style="font-size:0.78rem; color:#aaa; min-height:1.1em; margin-top:6px"></div>
            <div id="csi_wifi_scan_results" class="wifi-scan-list"></div>
            <div style="display:flex; gap:6px; flex-wrap:wrap">
                <button onclick="saveCsiWifi()" class="warn" style="flex:2; min-width:140px" data-i18n="save_reboot">💾 Uložit a rebootovat</button>
                <button onclick="resetCsiWifi()" class="sec" style="flex:1; min-width:120px" data-i18n="wifi_use_default">↩️ Compile-time default</button>
            </div>

            <div class="section-title" data-collapsed data-i18n="actions">AKCE</div>
            <div style="display:flex; flex-wrap:wrap; gap:6px">
                <button class="sec" style="flex:1; min-width:120px" onclick="csiCalibrate()" data-i18n="auto_calib">📐 Auto-kalibrace prahu (10s)</button>
                <button class="sec" style="flex:1; min-width:120px" onclick="csiResetBaseline()" data-i18n="reset_baseline">♻️ Reset idle baseline</button>
                <button class="sec" style="flex:1; min-width:120px" onclick="csiReconnect()" data-i18n="reconnect_wifi">📶 Reconnect WiFi</button>
            </div>
            <div id="csi_calib_bar" style="display:none; height:6px; background:#333; margin-top:8px; border-radius:3px; overflow:hidden">
                <div id="csi_calib_fill" style="height:100%; width:0%; background:var(--accent); transition:width 0.3s"></div>
            </div>
            <div style="font-size:0.75rem; color:#777; margin-top:8px">
                <span data-i18n="csi_help"><b>Auto-kalibrace:</b> 10 sekund vzorkuje variance v klidu, nastaví práh = mean × 1.5. Použij když je v místnosti nikdo.<br>
                <b>Reset baseline:</b> vyčistí naučené idle hodnoty (turbulence, fáze). Po přesunu senzoru.<br>
                <b>Reconnect WiFi:</b> přerušení / RSSI dropy řeší restart asociace.</span>
            </div>

            <div class="section-title" data-collapsed style="margin-top:14px" data-i18n="site_learning">SITE LEARNING (dlouhodobé)</div>
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

            <div class="section-title" data-collapsed style="margin-top:14px" data-i18n="learned_model">NAUČENÝ MODEL</div>
            <div class="stat-row"><span data-i18n="learned_ready">Model připraven</span><span id="csi_lm_ready">—</span></div>
            <div class="stat-row"><span data-i18n="learned_thr">Naučený práh</span><span id="csi_lm_thr">—</span></div>
            <div class="stat-row"><span data-i18n="learned_mean">Průměrná variance</span><span id="csi_lm_mean">—</span></div>
            <div class="stat-row"><span data-i18n="learned_std">Směrodatná odchylka variance</span><span id="csi_lm_std">—</span></div>
            <div class="stat-row"><span data-i18n="learned_max">Maximální variance</span><span id="csi_lm_max">—</span></div>
            <div class="stat-row"><span data-i18n="learned_samples">Vzorků</span><span id="csi_lm_samples">—</span></div>
            <div class="stat-row"><span data-i18n="learn_refresh">Vzorky obnovy EMA</span><span id="csi_lm_refresh">—</span></div>
            <div style="font-size:0.75rem; color:#777; margin-top:8px">
                <span data-i18n="learn_help"><b>Site learning:</b> dlouhodobé vzorkování variance v prázdné místnosti (doporučeno 24–72 h). Radar-gate (LD2412) odfiltruje statické lidi. Po dokončení se automaticky nastaví <code>threshold = mean + 3×std</code>. <b>Učení přežívá OTA flash</b> (uloženo v NVS). Smazat model = reset na tovární hodnoty.</span>
            </div>

            <div class="section-title" data-collapsed style="margin-top:14px">MODEL STANOVIŠTĚ (kandidát / použít / vrátit)</div>
            <div id="csi_sm_wrap" style="font-size:0.85rem">
              <div class="stat-row"><span>Aktivní</span><span id="csi_sm_active">—</span></div>
              <div class="stat-row"><span>Kandidát</span><span id="csi_sm_cand">—</span></div>
              <div class="stat-row"><span>Předchozí</span><span id="csi_sm_prev">—</span></div>
              <div class="stat-row"><span>Rozdíl (kandidát vs. aktivní)</span><span id="csi_sm_diff">—</span></div>
              <div id="csi_sm_note" style="font-size:0.78rem; color:#aaa; margin-top:4px"></div>
              <div style="display:flex; flex-wrap:wrap; gap:6px; margin-top:8px">
                <button class="sec" id="csi_sm_apply" style="flex:1; min-width:110px" onclick="csiApplyModel()">✅ Použít kandidáta</button>
                <button class="sec" id="csi_sm_discard" style="flex:1; min-width:110px" onclick="csiDiscardCandidate()">🗑 Zahodit kandidáta</button>
                <button class="sec" id="csi_sm_rollback" style="flex:1; min-width:110px" onclick="csiRollbackModel()">↩ Vrátit předchozí</button>
              </div>
              <div style="font-size:0.75rem; color:#777; margin-top:8px">
                Dokončené učení vytvoří <b>kandidáta</b> — detekce používá <b>aktivní</b> model, dokud kandidáta nepoužiješ. Původní model zůstane pro návrat; učení alarm nikdy nemění.
              </div>
            </div>

            <div class="section-title" data-collapsed style="margin-top:14px">CSI DIAGNOSTIKA (P1 · read-only)</div>
            <div style="font-size:0.85rem">
              <div class="stat-row"><span>Stav</span><span id="csi_dg_health">—</span></div>
              <div class="stat-row"><span>Poslední rozhodnutí</span><span id="csi_dg_decision">—</span></div>
              <div class="stat-row"><span>Stínový model (kandidát)</span><span id="csi_dg_shadow">—</span></div>
              <div class="stat-row"><span>Prstenec událostí</span><span id="csi_dg_events">—</span></div>
              <div style="font-size:0.75rem; color:#777; margin-top:8px">
                Diagnostika pouze pro čtení. <b>Stínový model</b> běží souběžně — <b>BEZ VLIVU NA ALARM</b>. Podrobnosti: <code>/api/csi/{decision,health,events,shadow}</code>.
              </div>
            </div>

            <div class="section-title" data-collapsed style="margin-top:14px" data-i18n="ml_mlp">ML (MLP 17→18→9→1)</div>
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
        <button id="btn_espota" onclick="prepareEspota()" class="sec" data-i18n="ota_espota_prepare">Připravit espota okno</button>
        <p style="font-size:0.7rem; color:#888; margin:6px 0 0 0" data-i18n="ota_espota_help">Pro flash nástrojem espota.py (UDP 3232) místo nahrání souboru. Otevře dočasné okno (odpojí CSI WiFi, odstaví CSI/MQTT, pozastaví radar), aby espota spolehlivě prošlo. Reboot není potřeba.</p>

        <hr style="border:none; border-top:1px solid #333; margin:14px 0 10px">
        <div class="stat-row"><span data-i18n="pull_ota_title">Pull OTA z URL</span></div>
        <div style="font-size:0.75rem; color:#777; margin-bottom:6px">
            <span data-i18n="pull_ota_help">ESP si stáhne firmware sám z dané URL. Funguje s HTTPS. Přesměrování (redirect) se z bezpečnostních důvodů nenásleduje — zadej přímou URL na .bin. MD5 je povinné a slouží jako hlavní kontrola integrity firmware.</span>
        </div>
        <input type="text" id="pull_url" placeholder="https://example.com/firmware.bin" style="width:100%; box-sizing:border-box; margin-bottom:6px">
        <input type="text" id="pull_auth" placeholder="Bearer …" style="width:100%; box-sizing:border-box; margin-bottom:6px">
        <input type="text" id="pull_md5" maxlength="32" placeholder="MD5 checksum (required)" data-i18n-ph="ph_md5_required" style="width:100%; box-sizing:border-box; margin-bottom:6px">
        <div id="pull_status" style="margin-top:4px; font-size:0.85em; color:#aaa; min-height:1.1em"></div>
        <button id="btn_pull" onclick="pullFW()" class="sec" data-i18n="pull_ota_btn">⤓ Stáhnout a flashnout</button>
        <details style="margin-top:10px; font-size:0.8em">
            <summary>HTTPS CA profil</summary>
            <div id="ota_trust_status" style="margin:6px 0; color:#aaa">Načítám stav…</div>
            <textarea id="ota_ca_pem" maxlength="3072" placeholder="-----BEGIN CERTIFICATE-----" style="width:100%; min-height:90px; box-sizing:border-box"></textarea>
            <button type="button" onclick="saveOtaTrust()" class="sec">Uložit CA pro HTTPS Pull OTA</button>
            <button type="button" onclick="clearOtaTrust()" class="sec">Smazat CA</button>
            <p style="color:#888">Certifikát je write-only; záloha konfigurace jej neobsahuje. HTTP na privátní LAN zůstává kompatibilní, ale není transportně ověřené.</p>
        </details>
    </div>
  </div>

  <div id="toast" data-i18n="saved">Uloženo</div>

<script>
const $=e=>document.getElementById(e),api=(e,n={})=>(n.body instanceof URLSearchParams&&!n.inBody&&(e+=(e.includes("?")?"&":"?")+n.body.toString(),delete n.body),delete n.inBody,fetch("/api/"+e,n).then(e=>(e.ok?showToast("OK"):showToast(t("error")),e)));function showToast(e){$("toast").innerText=e,$("toast").style.opacity=1,setTimeout(()=>$("toast").style.opacity=0,2e3)}let histDist=new Array(60).fill(0),histMov=new Array(60).fill(0),histStat=new Array(60).fill(0),histCsiComp=new Array(60).fill(0),zones=[],gateResolution=.75,cfgMinGate=0,cfgMaxGate=13,evtSource=null,reconnectTimeout=null;function connectSSE(){evtSource&&evtSource.close(),evtSource=new EventSource("/events"),evtSource.addEventListener("telemetry",e=>{const t=JSON.parse(e.data);if(updateUI(t),t.alarm_state&&(alarmArmed=t.armed,updateAlarmUI(t.alarm_state)),t.gate_move&&!$("tab2").classList.contains("hidden")&&updateGatesUI(t),t.csi&&(renderCsiMainPanel(t.csi),updateCSIUI(t.csi)),t.fusion){const e=$("fusion_panel");e&&"none"===e.style.display&&(e.style.display=""),renderFusionPanel(t.fusion)}}),evtSource.onerror=()=>{console.log("SSE connection lost, reconnecting in 3s..."),$("sse_icon").className="icon err",$("sse_icon").title=t("conn_lost"),evtSource.close(),reconnectTimeout&&clearTimeout(reconnectTimeout),reconnectTimeout=setTimeout(connectSSE,3e3)},evtSource.onopen=()=>{console.log("SSE connected"),$("sse_icon").className="icon ok",$("sse_icon").title="Realtime OK"}}function init(){connectSSE(),fetch("/api/version").then(e=>e.text()).then(e=>$("fw_ver").innerText=e),fetch("/api/health").then(e=>e.json()).then(e=>{g_isDefaultPass=!!e.is_default_pass,e.is_default_pass&&($("security_warning").style.display="block"),e.auth_user&&($("txt_auth_user").value=e.auth_user),e.hostname&&($("txt_hostname").value=e.hostname),updateHealth(e)}),loadOtaTrust(),loadMainConfig(),loadSecurityConfig(),loadMQTTConfig(),loadTelegramConfig(),loadZones(),loadAlarmStatus(),initCollapsible(),setInterval(()=>fetch("/api/health").then(e=>e.json()).then(updateHealth),5e3)}function loadMainConfig(){fetch("/api/config").then(e=>e.json()).then(e=>{$("i_max").value=e.max_gate,void 0!==e.min_gate&&($("i_min").value=e.min_gate),$("i_hold").value=null!=e.hold_time?e.hold_time/1e3:"",void 0!==e.led_en&&($("chk_led").checked=e.led_en),void 0!==e.eng_mode&&($("chk_eng").checked=e.eng_mode),e.mov_sens&&e.mov_sens.length>0&&($("i_sens").value=e.mov_sens[0]),e.resolution&&(gateResolution=e.resolution),void 0!==e.min_gate&&(cfgMinGate=e.min_gate),void 0!==e.max_gate&&(cfgMaxGate=e.max_gate);let n=(cfgMinGate*gateResolution*100).toFixed(0),a=(cfgMaxGate*gateResolution*100).toFixed(0);$("range_summary").innerHTML=`${t("coverage")}: <b>${n}cm – ${a}cm</b> &middot; ${t("resolution")}: ${gateResolution}m/${t("gate")}`,updGateCm(),renderGateSliders(e.mov_sens,e.stat_sens)})}function updateUI(e){if(e.error)return;const n=!1!==e.connected,a=e.distance_mm/10,i=n&&Number.isFinite(a),o=n&&null!=e.moving_energy,s=n&&null!=e.static_energy;let l,r;histDist.push(i?a:0),histDist.shift(),histMov.push(o?e.moving_energy:0),histMov.shift(),histStat.push(s?e.static_energy:0),histStat.shift(),drawSpark("graph_dist",histDist,400),drawSpark("graph_mov",histMov,100),drawSpark("graph_stat",histStat,100),$("dist_val").innerText=i?a.toFixed(0):"—",$("mov_val").innerText=o?e.moving_energy+"%":"—",$("stat_val").innerText=s?e.static_energy+"%":"—",g_radarPresent?n?(l=t("idle"),r="#888","detected"===e.state?(l=t("detected_state"),r="var(--accent)"):"hold"===e.state&&(l=t("hold_state"),r="#bb86fc"),e.tamper&&(l=t("tamper_state"),r="var(--warn)")):(l=t("radar_disconnected"),r="var(--warn)"):(l="",r="#888"),$("state_text").innerText=l,$("state_text").style.display=l?"":"none",$("state_text").style.color=r,drawZoneMap(e.raw_stat_dist,e.raw_mov_dist)}function renderGateSliders(e,n){let a="",i=t("gate_out_of_range");for(let t=0;t<14;t++){let o=Math.round(t*gateResolution*100),s=t>=cfgMinGate&&t<=cfgMaxGate,l=s?"":" gate-dimmed",r=s?"":` title="${i}"`,c=e?e[t]:50,d=n?n[t]:30;a+=`<div class="gate-wrapper${l}"${r}>\n            <div class="gate-label" style="width:65px; white-space:nowrap">G${t} <span style="color:#666">(${o}cm)</span></div>\n            <input type="range" class="mov-slider" id="g_m_${t}" value="${c}" min="0" max="100" title="Pohyb G${t}" oninput="$('lm_${t}').innerText=this.value" style="flex:1">\n            <span id="lm_${t}" style="width:22px; text-align:right; color:#03dac6; font-size:0.75rem">${c}</span>\n            <input type="range" class="stat-slider" id="g_s_${t}" value="${d}" min="0" max="100" title="Statika G${t}" oninput="$('ls_${t}').innerText=this.value" style="flex:1">\n            <span id="ls_${t}" style="width:22px; text-align:right; color:#bb86fc; font-size:0.75rem">${d}</span>\n        </div>`}$("gates_container").innerHTML=a}function setAllGates(){let e=$("g_m_all").value,t=$("g_s_all").value;for(let n=0;n<14;n++){let a=$(`g_m_${n}`),i=$(`g_s_${n}`);a&&(a.value=e,$(`lm_${n}`).innerText=e),i&&(i.value=t,$(`ls_${n}`).innerText=t)}}function updateGatesUI(e){}function updateHealth(e){let t=e.eth_link||e.ethernet&&e.ethernet.link_up;$("wifi_icon").className="icon "+(t?"ok":"err"),$("mqtt_icon").className="icon "+(e.mqtt&&e.mqtt.connected?"ok":"err"),$("h_score").innerText=e.health_score+"%",$("h_uart").innerText=e.uart_state,$("h_fps").innerText=e.frame_rate.toFixed(1)+" FPS",$("h_err").innerText=e.error_count,$("h_heap").innerText=(e.free_heap/1024).toFixed(1)+" / "+(e.min_heap/1024).toFixed(1)+" KB",null!=e.chip_temp&&($("h_temp").innerText=e.chip_temp.toFixed(1)+" °C");let n=e.uptime;$("h_uptime").innerText=Math.floor(n/3600)+"h "+Math.floor(n%3600/60)+"m",void 0!==e.radar_monitoring_disabled&&applyRadarMode(!e.radar_monitoring_disabled)}function drawSpark(e,t,n){const a=$(e);let i="";const o=100/(t.length-1);t.forEach((e,t)=>{const a=50-Math.min(e,n)/n*50;i+=`${t*o},${a} `}),a.innerHTML=`<polyline points="${i}" style="fill:none;stroke:inherit;stroke-width:2" />`}function setSectionCollapsed(e,t){e.classList.toggle("collapsed",t);let n=e.nextElementSibling;for(;n&&!n.classList.contains("section-title");)n.classList.toggle("sec-collapsed",t),n=n.nextElementSibling}function initCollapsible(){document.querySelectorAll(".section-title").forEach(e=>{void 0===e.dataset.nocollapse&&(e.classList.add("collapsible"),e.onclick=()=>setSectionCollapsed(e,!e.classList.contains("collapsed")),void 0!==e.dataset.collapsed&&setSectionCollapsed(e,!0))})}let g_radarPresent=!0,g_radarRevealed=!1;function applyRadarMode(e){g_radarPresent=e;const t=$("radar_reveal"),n=$("csi_main_block");e?(document.querySelectorAll(".radar-only").forEach(e=>e.classList.remove("r-hidden")),t&&(t.style.display="none"),n&&n.classList.remove("promoted")):(g_radarRevealed||document.querySelectorAll(".radar-only").forEach(e=>e.classList.add("r-hidden")),t&&(t.style.display=""),n&&n.classList.add("promoted"))}function toggleRadarReveal(){g_radarRevealed=!g_radarRevealed,document.querySelectorAll(".radar-only").forEach(e=>e.classList.toggle("r-hidden",!g_radarRevealed));const e=$("radar_reveal").querySelector("span");e&&(e.innerText=g_radarRevealed?t("radar_hide"):t("radar_show"))}function tab(e){["tab0","tab1","tab2","tab3","tab4","tab5","tab6"].forEach((t,n)=>{$(t).classList.toggle("hidden",n!==e),document.querySelectorAll(".tab")[n].classList.toggle("active",n===e)}),1===e&&(loadSecurityConfig(),loadAlarmStatus()),2===e&&loadMainConfig(),3===e&&(loadNetworkConfig(),loadTimezoneConfig(),loadScheduleConfig(),loadMQTTConfig(),loadTelegramConfig()),5===e&&loadEvents(),6===e&&loadCSIConfig()}function loadCSIConfig(){loadCsiWifi(),csiRenderSiteModel(),csiRenderDiagnostics(),fetch("/api/csi").then(e=>e.json()).then(e=>{if($("csi_compiled_warn").style.display=e.compiled?"none":"block",e.compiled){if($("csi_en").checked=!!e.enabled,void 0!==e.threshold&&($("csi_thr").value=e.threshold,$("csi_thr_lbl").innerText=parseFloat(e.threshold).toFixed(2)),void 0!==e.hysteresis&&($("csi_hyst").value=e.hysteresis,$("csi_hyst_lbl").innerText=parseFloat(e.hysteresis).toFixed(2)),void 0!==e.window&&($("csi_win").value=e.window,$("csi_win_lbl").innerText=e.window),void 0!==e.publish_ms&&($("csi_pub").value=e.publish_ms,$("csi_pub_lbl").innerText=e.publish_ms),void 0!==e.traffic_icmp&&($("csi_tmode").value=e.traffic_icmp?"icmp":"udp",$("csi_udp_port_row").style.display=e.traffic_icmp?"none":"flex"),void 0!==e.traffic_port&&($("csi_tport").value=e.traffic_port),void 0!==e.traffic_pps&&($("csi_tpps").value=e.traffic_pps,$("csi_pps_lbl").innerText=e.traffic_pps),$("csi_active_val").innerText=e.active?t("yes"):t("no"),$("csi_active_val").style.color=e.active?"var(--accent)":"#888",$("csi_ssid_val").innerText=e.wifi_ssid||"—",$("csi_rssi_val").innerText=void 0!==e.wifi_rssi&&0!==e.wifi_rssi?e.wifi_rssi+" dBm":"—",renderCsiRssiQuality(e.wifi_rssi),$("csi_pps_val").innerText=void 0!==e.pps?e.pps.toFixed(1):"—",$("csi_idle_val").innerText=e.idle_ready?t("yes"):t("no_collecting"),void 0!==e.ht_ltf_seen){const n=$("csi_ap_compat");n.title="",e.active?e.ht_ltf_seen?(n.innerText=t("ap_ok"),n.style.color="var(--accent)"):0===(e.packets||0)?(n.innerText=t("ap_checking"),n.style.color="#888"):(n.innerText=t("ap_incompat"),n.style.color="#e05252",n.title=t("ap_incompat_hint")):(n.innerText="—",n.style.color="")}void 0!==e.motion&&($("csi_motion_val").innerText=e.motion?t("motion"):t("idle"),$("csi_motion_val").style.color=e.motion?"var(--accent)":"#888"),void 0!==e.composite&&($("csi_comp_val").innerText=e.composite.toFixed(4)),void 0!==e.variance&&($("csi_var_val").innerText=e.variance.toFixed(4)),$("fus_en").checked=!!e.fusion_enabled,e.fusion&&updateFusionUI(e.fusion),updateLearningUI(e),void 0!==e.ml_enabled&&($("csi_ml_en").checked=!!e.ml_enabled),void 0!==e.ml_threshold&&($("csi_ml_thr").value=e.ml_threshold,$("csi_ml_thr_lbl").innerText=parseFloat(e.ml_threshold).toFixed(2)),updateMLUI(e)}})}function fmtDurationSec(e){if(!e||e<0)return"—";let t=Math.floor(e/3600),n=Math.floor(e%3600/60),a=e%60;return t>0?t+"h "+n+"m":n>0?n+"m "+a+"s":a+"s"}function updateLearningUI(e){if(void 0===e.learning_active)return;let n=!!e.learning_active,a=!n&&!!e.model_ready;$("csi_learn_status").innerText=n?t("learn_running"):a?t("learn_done"):t("learn_idle"),$("csi_learn_status").style.color=n?"var(--accent)":a?"#4caf50":"#888";let i=e.learning_elapsed_s||0,o=e.learning_duration_s||0;$("csi_learn_elapsed").innerText=fmtDurationSec(i)+" / "+fmtDurationSec(o);let s=void 0!==e.learning_progress?e.learning_progress:0;s>100&&(s=100),$("csi_learn_fill").style.width=s+"%";let l=$("csi_learn_progress_lbl");n&&o>0?(l.innerText=fmtLearnDur(i/3600)+" / "+fmtLearnDur(o/3600),l.style.display="block"):l.style.display="none";let r=e.learning_samples||0,c=e.learning_rejected_motion||0,d=e.learning_rejected_radar||0;$("csi_learn_samples").innerText=r+" / "+c+" / "+d,$("csi_learn_bssid").innerText=void 0!==e.learning_bssid_resets?e.learning_bssid_resets:"—",$("csi_learn_thr_est").innerText=void 0!==e.learning_threshold_estimate?parseFloat(e.learning_threshold_estimate).toFixed(4):"—",$("csi_lm_ready").innerText=e.model_ready?t("yes"):t("no"),$("csi_lm_ready").style.color=e.model_ready?"var(--accent)":"#888",$("csi_lm_thr").innerText=void 0!==e.learned_threshold?parseFloat(e.learned_threshold).toFixed(4):"—",$("csi_lm_mean").innerText=void 0!==e.learned_mean_variance?parseFloat(e.learned_mean_variance).toFixed(4):"—",$("csi_lm_std").innerText=void 0!==e.learned_std_variance?parseFloat(e.learned_std_variance).toFixed(4):"—",$("csi_lm_max").innerText=void 0!==e.learned_max_variance?parseFloat(e.learned_max_variance).toFixed(4):"—",$("csi_lm_samples").innerText=void 0!==e.learned_samples?e.learned_samples:"—",$("csi_lm_refresh").innerText=void 0!==e.learn_refresh_count?e.learn_refresh_count:"—"}function updateMLUI(e){void 0!==e.ml_motion&&($("csi_ml_motion_val").innerText=e.ml_motion?t("motion"):t("idle"),$("csi_ml_motion_val").style.color=e.ml_motion?"var(--accent)":"#888"),void 0!==e.ml_probability&&($("csi_ml_prob_val").innerText=(100*e.ml_probability).toFixed(1)+"%")}let csiLastDataMs=0;function renderFusionPanel(e){if(!e)return;const t=(e,t,n)=>{const a=$(e);var i;a&&(a.style.height=(i=t,Math.max(0,Math.min(100,i||0))+"%"),a.className="fbar-fill"+(n?" on":""))},n=$("fbar_radar"),a=!1===e.radar_present;if(n&&n.classList.toggle("na",a),a){const e=$("fb_radar");e&&(e.style.height="",e.className="fbar-fill")}else t("fb_radar",e.radar_lvl,e.radar);t("fb_csi",e.csi_lvl,e.csi),t("fb_ml",e.ml_lvl,e.ml);const i=Math.round(100*(e.confidence||0));$("fg_fill")&&($("fg_fill").style.width=i+"%"),$("fg_val")&&($("fg_val").innerText=i+"%"),$("fusion_reason")&&($("fusion_reason").innerText=e.reason||"–")}function csiFeedback(e){api("csi/feedback?label="+e,{method:"POST"}).then(e=>{e.ok&&e.json().then(e=>showToast(t("feedback_saved")+" ("+e.total+"/"+e.capacity+")"))})}function renderCsiMainPanel(e){const n=$("csi_main_state"),a=$("csi_main_link");if(!n||!a)return;const i=Date.now(),o=void 0!==e.pps?e.pps:0;o>0&&(csiLastDataMs=i);if(!(void 0!==e.rssi&&0!==e.rssi))return n.innerText=t("csi_offline"),n.style.color="#666",void(a.innerText="—");const s=o.toFixed(1)+" pkt/s · "+e.rssi+" dBm";if(i-csiLastDataMs>5e3)return n.innerText=t("csi_nodata"),n.style.color="var(--warn)",void(a.innerText=s);const l=!!e.motion;n.innerText=l?t("motion"):t("idle"),n.style.color=l?"var(--accent)":"#888",a.innerText=s}function renderCsiRssiQuality(e){const n=$("csi_rssi_quality_val");n&&(void 0===e||0===e?(n.innerText=t("csi_rssi_unavailable"),n.style.color="#888",n.title=""):e>-40?(n.innerText=t("csi_rssi_hot"),n.style.color="var(--warn)",n.title=t("csi_rssi_hot_hint")):e<-70?(n.innerText=t("csi_rssi_weak"),n.style.color="var(--warn)",n.title=""):(n.innerText=t("csi_rssi_good"),n.style.color="var(--accent)",n.title=""))}function updateCSIUI(e){if(e){if($("tab6").classList.contains("hidden"))return histCsiComp.push(e.composite||0),void histCsiComp.shift();histCsiComp.push(e.composite||0),histCsiComp.shift(),drawSpark("csi_graph",histCsiComp,2),void 0!==e.motion&&($("csi_motion_val").innerText=e.motion?t("motion"):t("idle"),$("csi_motion_val").style.color=e.motion?"var(--accent)":"#888"),void 0!==e.composite&&($("csi_comp_val").innerText=e.composite.toFixed(4)),void 0!==e.variance&&($("csi_var_val").innerText=e.variance.toFixed(4)),void 0!==e.pps&&($("csi_pps_val").innerText=e.pps.toFixed(1)),void 0!==e.rssi&&($("csi_rssi_val").innerText=0!==e.rssi?e.rssi+" dBm":"—",renderCsiRssiQuality(e.rssi)),e.calibrating?($("csi_calib_bar").style.display="block",$("csi_calib_fill").style.width=100*(e.calib_pct||0)+"%"):$("csi_calib_bar").style.display="none",updateMLUI(e),e.learning_active&&updateLearningUI(e),e.fusion&&updateFusionUI(e.fusion)}}function updateFusionUI(e){if(!e)return;let n={none:"—",radar:"Radar",csi:"CSI",both:"Radar + CSI",ml:t("src_ml")};$("fus_presence_val").innerText=e.presence?t("detected_state"):t("idle"),$("fus_presence_val").style.color=e.presence?"var(--warn)":"#888",$("fus_conf_val").innerText=void 0!==e.confidence?(100*e.confidence).toFixed(0)+"%":"—",$("fus_source_val").innerText=n[e.source]||e.source||"—"}function toggleFusion(e){let n=new URLSearchParams;n.append("fusion_enabled",e?"1":"0"),api("csi",{method:"POST",body:n}).then(e=>e.json()).then(n=>{showToast(e?t("fusion_on"):t("fusion_off")),e||($("fus_presence_val").innerText="—",$("fus_conf_val").innerText="—",$("fus_source_val").innerText="—")})}function saveCSIConfig(){let e=new URLSearchParams;e.append("enabled",$("csi_en").checked?"1":"0"),e.append("threshold",$("csi_thr").value),e.append("hysteresis",$("csi_hyst").value),e.append("window",$("csi_win").value),e.append("publish_ms",$("csi_pub").value),e.append("traffic_icmp","icmp"===$("csi_tmode").value?"1":"0"),e.append("traffic_port",$("csi_tport").value),e.append("traffic_pps",$("csi_tpps").value),api("csi",{method:"POST",body:e}).then(e=>e.json()).then(e=>{e.needs_restart&&confirm(t("csi_restart"))&&api("restart",{method:"POST"})})}function csiCalibrate(){api("csi/calibrate",{method:"POST"}).then(e=>{e.ok&&showToast(t("calib_started"))})}function csiResetBaseline(){confirm(t("reset_confirm"))&&api("csi/reset_baseline",{method:"POST"})}function csiReconnect(){api("csi/reconnect",{method:"POST"})}let csiWifiLoadedSsid="",csiWifiSelectedNetwork=null,csiWifiScanTimer=null,csiWifiScanPolls=0;function loadCsiWifi(){api("csi/wifi").then(e=>e.json()).then(e=>{e&&e.ssid&&($("csi_wifi_ssid").value=e.ssid,csiWifiLoadedSsid=e.ssid),csiWifiSelectedNetwork=null,$("csi_wifi_pass").value=""}).catch(()=>{})}function csiWifiSelectionChanged(){csiWifiSelectedNetwork&&$("csi_wifi_ssid").value!==csiWifiSelectedNetwork.ssid&&(csiWifiSelectedNetwork=null)}function renderCsiWifiScan(e){const n=$("csi_wifi_scan_results");n.innerHTML="";const a=Array.isArray(e.networks)?e.networks:[];a.length?($("csi_wifi_scan_status").innerText=t("wifi_scan_found").replace("{n}",a.length),a.forEach(e=>{const a=document.createElement("button");a.type="button",a.className="wifi-scan-row";const i=document.createElement("span");i.className="wifi-scan-name",i.textContent=(e.current?"✓ ":"")+e.ssid;const o=document.createElement("span");o.className="wifi-scan-meta",o.textContent=e.rssi+" dBm · "+t("wifi_channel")+" "+e.channel+" · "+(e.secure?"🔒 ":"")+e.security,e.current&&(o.textContent+=" · "+t("wifi_current")),o.style.color=e.rssi>=-55?"var(--accent)":e.rssi>=-70?"#d6b85a":"var(--warn)",a.appendChild(i),a.appendChild(o),a.addEventListener("click",()=>{csiWifiSelectedNetwork=e,$("csi_wifi_ssid").value=e.ssid,$("csi_wifi_pass").value="",e.secure&&e.ssid!==csiWifiLoadedSsid&&$("csi_wifi_pass").focus()}),n.appendChild(a)})):$("csi_wifi_scan_status").innerText=t("wifi_scan_none")}function pollCsiWifiScan(){fetch("/api/csi/wifi/scan").then(e=>e.json()).then(e=>{if("running"===e.status){if(csiWifiScanPolls++,csiWifiScanPolls<40)return void(csiWifiScanTimer=setTimeout(pollCsiWifiScan,500));e.status="failed"}$("csi_wifi_scan_btn").disabled=!1,"complete"===e.status?renderCsiWifiScan(e):$("csi_wifi_scan_status").innerText=t("wifi_scan_failed")}).catch(()=>{$("csi_wifi_scan_btn").disabled=!1,$("csi_wifi_scan_status").innerText=t("wifi_scan_failed")})}function startCsiWifiScan(){csiWifiScanTimer&&clearTimeout(csiWifiScanTimer),csiWifiScanPolls=0,$("csi_wifi_scan_btn").disabled=!0,$("csi_wifi_scan_status").innerText=t("wifi_scanning"),$("csi_wifi_scan_results").innerHTML="",fetch("/api/csi/wifi/scan",{method:"POST"}).then(e=>e.json().catch(()=>({})).then(t=>({ok:e.ok,data:t}))).then(e=>{if(!e.ok)return $("csi_wifi_scan_btn").disabled=!1,void($("csi_wifi_scan_status").innerText="busy"===e.data.status?t("wifi_scan_busy"):t("wifi_scan_failed"));pollCsiWifiScan()}).catch(()=>{$("csi_wifi_scan_btn").disabled=!1,$("csi_wifi_scan_status").innerText=t("wifi_scan_failed")})}function saveCsiWifi(){let e=$("csi_wifi_ssid").value.trim(),n=$("csi_wifi_pass").value;if(!e)return void showToast(t("wifi_ssid_empty"));let a=csiWifiSelectedNetwork&&csiWifiSelectedNetwork.ssid===e?csiWifiSelectedNetwork:null;if(e!==csiWifiLoadedSsid&&!n&&(!a||a.secure))return void showToast(t("wifi_pass_required"));if(!confirm(t("wifi_save_confirm").replace("{ssid}",e)))return;let i=new URLSearchParams;i.append("ssid",e),i.append("pass",n),!a||a.secure||n||i.append("clear_pass","1"),api("csi/wifi",{method:"POST",body:i,inBody:!0}).then(e=>e.json()).then(e=>{e&&e.saved?(showToast(t("wifi_saved_reboot")),setTimeout(()=>api("restart",{method:"POST"}),1500)):showToast(t("wifi_save_failed"))}).catch(()=>showToast(t("wifi_save_failed")))}function resetCsiWifi(){if(!confirm(t("wifi_reset_confirm")))return;let e=new URLSearchParams;e.append("reset","1"),api("csi/wifi",{method:"POST",body:e,inBody:!0}).then(e=>e.json()).then(e=>{e&&e.saved&&(showToast(t("wifi_reset_reboot")),setTimeout(()=>api("restart",{method:"POST"}),1500))})}function fmtLearnDur(e){if((e=parseFloat(e))<1)return Math.round(60*e)+" min";let t=Math.floor(e),n=Math.round(60*(e-t));return n?t+" h "+n+" min":t+" h"}function csiStartLearning(){let e=$("csi_learn_dur").value,n=fmtLearnDur(e);if(!confirm(t("learn_confirm_start").replace("{h}",n)))return;let a=new URLSearchParams;a.append("duration_h",e),api("csi/site_learning",{method:"POST",body:a}).then(()=>{setTimeout(loadCSIConfig,500)})}function csiStopLearning(){if(!confirm(t("learn_confirm_stop")))return;let e=new URLSearchParams;e.append("stop","1"),api("csi/site_learning",{method:"POST",body:e}).then(()=>{setTimeout(loadCSIConfig,500)})}function csiClearLearned(){if(!confirm(t("learn_confirm_clear")))return;let e=new URLSearchParams;e.append("clear_model","1"),api("csi/site_learning",{method:"POST",body:e}).then(()=>{setTimeout(loadCSIConfig,500)})}function saveMLConfig(){let e=new URLSearchParams;e.append("ml_enabled",$("csi_ml_en").checked?"1":"0"),e.append("ml_threshold",$("csi_ml_thr").value),api("csi",{method:"POST",body:e})}function fmtSlot(e){return e&&e.valid?"gen "+e.generation+" · thr "+parseFloat(e.threshold).toFixed(5)+" · "+e.samples+" smp":"—"}function csiRenderSiteModel(){api("csi/site_model").then(e=>e.json()).then(e=>{let t=e.active,n=e.candidate,a=e.previous;$("csi_sm_active").innerText=fmtSlot(t),$("csi_sm_cand").innerText=fmtSlot(n),$("csi_sm_prev").innerText=fmtSlot(a);let i="—";if(n&&n.valid&&t&&t.valid&&t.threshold>0){let e=(n.threshold-t.threshold)/t.threshold*100;i=(e>=0?"+":"")+e.toFixed(1)+" %",$("csi_sm_diff").style.color=Math.abs(e)>50?"#cf6679":""}else $("csi_sm_diff").style.color="";$("csi_sm_diff").innerText=i;let o=!!e.apply_required&&!e.learning_active;$("csi_sm_apply").disabled=!o,$("csi_sm_discard").disabled=!(n&&n.valid),$("csi_sm_rollback").disabled=!(a&&a.valid);let s="";e.learning_active?s="⏳ Learning — detection still on active gen "+(t&&t.valid?t.generation:"—"):e.apply_required?s="🟡 Candidate ready — not yet affecting the alarm. Review and Apply.":t&&t.valid&&(s="🟢 Detection on active gen "+t.generation+"."),$("csi_sm_note").innerText=s}).catch(()=>{})}function csiRenderDiagnostics(){api("csi/health").then(e=>e.json()).then(e=>{let t=$("csi_dg_health");t.innerText="score "+e.score+" · "+((e.reasons||[]).join(", ")||"—"),t.style.color=e.healthy?"#4caf50":e.score>=60?"#ffb300":"#cf6679"}).catch(()=>{}),api("csi/decision").then(e=>e.json()).then(e=>{e.valid?$("csi_dg_decision").innerText=(e.decision?"MOTION":"idle")+" · "+e.reason:$("csi_dg_decision").innerText=e.reason||"—"}).catch(()=>{}),api("csi/shadow").then(e=>e.json()).then(e=>{let t=$("csi_dg_shadow");if(!e.active)return t.innerText="no candidate",void(t.style.color="");let n=(e.agree||0)+(e.disagree||0),a=n?(e.agree/n*100).toFixed(1):"—";t.innerText="agree "+e.agree+" / dis "+e.disagree+" ("+a+"%) — NO ALARM EFFECT",t.style.color=e.disagree>0?"#ffb300":""}).catch(()=>{}),api("csi/events?limit=1").then(e=>e.json()).then(e=>{$("csi_dg_events").innerText=e.count+"/"+e.capacity+" · last seq "+e.last_seq}).catch(()=>{})}function csiApplyModel(){api("csi/site_model").then(e=>e.json()).then(e=>{let t=e.active,n=e.candidate,a="Použít kandidátní model?\n\n";if(a+="Aktivní:      "+fmtSlot(t)+"\n",a+="Kandidát: "+fmtSlot(n)+"\n",n&&n.valid&&t&&t.valid&&t.threshold>0){let e=(n.threshold-t.threshold)/t.threshold*100;a+="\nThreshold change: "+(e>=0?"+":"")+e.toFixed(1)+" %",Math.abs(e)>50&&(a+="\n\n⚠️ WARNING: large change (>50%). Check the learning was done in an empty room.")}a+="\n\nThe current active model is kept for rollback.",confirm(a)&&api("csi/site_model/apply",{method:"POST"}).then(e=>{e.ok||e.text().then(e=>alert("Apply failed: "+e)),setTimeout(()=>{csiRenderSiteModel(),loadCSIConfig()},400)})})}function csiRollbackModel(){confirm("Rollback to the previous model?\n\nThis swaps active and previous — you can rollback again to undo.")&&api("csi/site_model/rollback",{method:"POST"}).then(e=>{e.ok||e.text().then(e=>alert("Rollback failed: "+e)),setTimeout(()=>{csiRenderSiteModel(),loadCSIConfig()},400)})}function csiDiscardCandidate(){confirm("Discard the candidate model?\n\nActive and previous are untouched.")&&api("csi/site_model/candidate",{method:"DELETE"}).then(e=>{e.ok||e.text().then(e=>alert("Discard failed: "+e)),setTimeout(csiRenderSiteModel,400)})}const EVT_TYPES={0:{name:"SYS",color:"#888",icon:"⚙️"},1:{name:"MOV",color:"#03dac6",icon:"👤"},2:{name:"TMP",color:"#cf6679",icon:"🚨"},3:{name:"NET",color:"#bb86fc",icon:"🌐"},4:{name:"HB",color:"#4caf50",icon:"💚"},5:{name:"SEC",color:"#ff9800",icon:"🔒"}};let evtOffset=0,evtAllLoaded=!1;function evtTimeStr(e){if(e>17e8){return new Date(1e3*e).toLocaleString("cs-CZ",{day:"numeric",month:"numeric",hour:"2-digit",minute:"2-digit",second:"2-digit"})}return Math.floor(e/3600)+"h "+Math.floor(e%3600/60)+"m "+e%60+"s"}function renderTimelineBar(e){let n=Math.floor(Date.now()/1e3),a=new Array(288).fill(0),i=1,o=!1;if(e.forEach(e=>{if(e.ts>17e8){let t=n-e.ts;if(t>=0&&t<86400){let e=287-Math.floor(t/300);e>=0&&e<288&&(a[e]++,o=!0)}}}),!o)return void($("evt_timeline").innerHTML='<text x="144" y="20" text-anchor="middle" fill="#444" font-size="10">'+t("no_timeline")+"</text>");for(let e=0;e<288;e++)a[e]>i&&(i=a[e]);let s="";for(let e=0;e<288;e++){if(0===a[e])continue;let t=Math.max(2,a[e]/i*28),n=.3+a[e]/i*.7;s+=`<rect x="${e}" y="${32-t}" width="1" height="${t}" fill="var(--accent)" opacity="${n.toFixed(2)}"/>`}e.forEach(e=>{if(5===e.type&&e.ts>17e8){let t=n-e.ts;if(t>=0&&t<86400){let e=287-Math.floor(t/300);s+=`<rect x="${e}" y="0" width="1" height="32" fill="#ff9800" opacity="0.6"/>`}}}),$("evt_timeline").innerHTML=s}function renderEventList(e,n){let a=$("evt_timeline_list"),i="";e.forEach(e=>{let t=EVT_TYPES[e.type]||EVT_TYPES[0],n=evtTimeStr(e.ts),a=e.dist>0?`<span style="color:#888">${e.dist}cm</span>`:"";i+=`<div style="display:flex; gap:8px; padding:6px 4px; border-left:3px solid ${t.color}; margin-bottom:2px; background:#111; border-radius:0 4px 4px 0; align-items:flex-start">\n            <div style="flex-shrink:0; width:20px; text-align:center">${t.icon}</div>\n            <div style="flex:1; min-width:0">\n                <div style="display:flex; justify-content:space-between; gap:8px; flex-wrap:wrap">\n                    <span style="font-size:0.75rem; color:#666; white-space:nowrap">${n}</span>\n                    <span style="font-size:0.7rem; color:${t.color}; font-weight:bold">${t.name} ${a}</span>\n                </div>\n                <div style="font-size:0.82rem; margin-top:2px; word-break:break-word">${e.msg}</div>\n            </div>\n        </div>`}),n?a.innerHTML+=i:a.innerHTML=i||'<div style="text-align:center; padding:20px; color:#555">'+t("no_events")+"</div>"}function loadEvents(){evtOffset=0,evtAllLoaded=!1;let e=$("evt_filter").value;fetch("/api/events?limit=50&type="+e).then(e=>e.json()).then(e=>{let n=e.events||[],a=e.total||0;$("evt_total").textContent=a+" "+t("total"),renderTimelineBar(n),renderEventList(n,!1),evtOffset=n.length,evtAllLoaded=n.length>=(void 0!==e.count?a:n.length),$("evt_load_more").style.display=n.length>=50&&!evtAllLoaded?"block":"none"})}function loadMoreEvents(){let e=$("evt_filter").value;fetch("/api/events?limit=50&offset="+evtOffset+"&type="+e).then(e=>e.json()).then(e=>{let t=e.events||[];renderEventList(t,!0),evtOffset+=t.length,t.length<50&&(evtAllLoaded=!0),$("evt_load_more").style.display=evtAllLoaded?"none":"block"})}function exportEvents(){window.open("/api/events/csv","_blank")}function clearEvents(){confirm(t("del_history"))&&api("events/clear",{method:"POST"}).then(()=>loadEvents())}function startCalib(){confirm(t("noise_calib"))&&api("radar/calibrate",{method:"POST"})}function updGateCm(){const e=100*gateResolution,t=$("i_min_cm"),n=$("i_max_cm");t&&(t.innerText="≈ "+Math.round(($("i_min").value||0)*e)+" cm"),n&&(n.innerText="≈ "+Math.round(($("i_max").value||0)*e)+" cm")}function saveBasic(){let e=$("i_max").value,t=$("i_min").value,n=Math.round(1e3*parseFloat($("i_hold").value||0)),a=$("i_sens").value,i=$("chk_led").checked?1:0;api("config",{method:"POST",body:new URLSearchParams({gate:e,min_gate:t,hold:n,mov:a,led_en:i})})}function toggleEng(){let e=$("chk_eng").checked?1:0;api("engineering",{method:"POST",body:new URLSearchParams({enable:e})})}function loadRadarBt(){fetch("/api/radar/bluetooth").then(e=>e.json()).then(e=>{e.readable&&void 0!==e.enabled?$("chk_radar_bt").checked=!!e.enabled:$("chk_radar_bt").checked=!!e.configured,$("radar_mac_val").innerText=e.mac?"MAC: "+e.mac:""}).catch(e=>console.log("Radar BT status not loaded"))}function toggleRadarBt(){let e=$("chk_radar_bt").checked?1:0;!e||confirm(t("radar_bt_warn")+"\n\n"+t("radar_bt_apply"))?e||confirm(t("radar_bt_apply"))?api("radar/bluetooth",{method:"POST",body:new URLSearchParams({enable:e})}).then(()=>setTimeout(loadRadarBt,3e3)):$("chk_radar_bt").checked=!0:$("chk_radar_bt").checked=!1}function loadSecurityConfig(){fetch("/api/security/config").then(e=>e.json()).then(e=>{$("i_am").value=e.antimask_time||300,$("chk_am_en").checked=e.antimask_enabled||!1,$("i_loit").value=e.loiter_time||15,$("chk_loit_en").checked=!1!==e.loiter_alert,$("i_hb").value=e.heartbeat||4,$("i_pet").value=e.pet_immunity||0}).catch(e=>console.log("Security config not loaded")),fetch("/api/radar/light").then(e=>e.json()).then(e=>{void 0!==e.function&&($("sel_light_func").value=e.function),void 0!==e.threshold&&($("i_light_thresh").value=e.threshold),void 0!==e.current_level&&($("cur_light_val").innerText=e.current_level)}).catch(e=>console.log("Light config not loaded")),fetch("/api/radar/timeout").then(e=>e.json()).then(e=>{void 0!==e.duration&&($("i_timeout").value=e.duration)}).catch(e=>console.log("Timeout config not loaded")),loadRadarBt()}function saveLightConfig(){let e=$("sel_light_func").value,t=$("i_light_thresh").value;api("radar/light",{method:"POST",body:new URLSearchParams({function:e,threshold:t})})}function saveTimeout(){let e=$("i_timeout").value;api("radar/timeout",{method:"POST",body:new URLSearchParams({duration:e})})}function saveSec(){let e=$("i_am").value,t=$("chk_am_en").checked?1:0,n=$("i_loit").value,a=$("chk_loit_en").checked?1:0,i=$("i_hb").value,o=$("i_pet").value;api("security/config",{method:"POST",body:new URLSearchParams({antimask:e,antimask_en:t,loiter:n,loiter_alert:a,heartbeat:i,pet:o})})}function loadMQTTConfig(){fetch("/api/health").then(e=>e.json()).then(e=>{e.mqtt&&($("chk_mqtt_en").checked=!1!==e.mqtt.enabled,$("txt_mqtt_server").value=e.mqtt.server||"",$("txt_mqtt_port").value=e.mqtt.port||"",$("txt_mqtt_user").value=e.mqtt.user||"")})}function saveMQTTConfig(){let e=$("chk_mqtt_en").checked?1:0,t=$("txt_mqtt_server").value,n=$("txt_mqtt_port").value,a=$("txt_mqtt_user").value,i=$("txt_mqtt_pass").value;api("mqtt/config",{method:"POST",inBody:!0,body:new URLSearchParams({enabled:e,server:t,port:n,user:a,pass:i})})}function onNetModeChange(){let e=$("net_mode_sel").value;$("net_static_fields").style.display="static"===e?"block":"none"}function isValidIPv4(e){if(!e)return!1;let t=e.split(".");return 4===t.length&&t.every(e=>/^\d+$/.test(e)&&+e>=0&&+e<=255)}function loadNetworkConfig(){fetch("/api/network/config").then(e=>e.ok?e.json():null).then(e=>{e&&($("net_mode_sel").value=e.mode||"dhcp",$("net_ip").value=e.ip||"",$("net_subnet").value=e.subnet||"255.255.255.0",$("net_gateway").value=e.gateway||"",$("net_dns").value=e.dns||"",$("net_mac_lbl").innerText=e.mac||"—",$("net_link_lbl").innerText=(e.link_speed?e.link_speed+" Mbps ":"")+(e.full_duplex?"FD":e.link_speed?"HD":""),onNetModeChange())})}function saveNetworkConfig(){let e=$("net_mode_sel").value,n=new URLSearchParams;if(n.append("mode",e),"static"===e){let e=$("net_ip").value.trim(),a=$("net_subnet").value.trim(),i=$("net_gateway").value.trim(),o=$("net_dns").value.trim();if(!isValidIPv4(e)||!isValidIPv4(i)||a&&!isValidIPv4(a)||o&&!isValidIPv4(o))return void showToast(t("net_ip_invalid"));n.append("ip",e),n.append("gateway",i),a&&n.append("subnet",a),o&&n.append("dns",o)}confirm(t("net_confirm"))&&fetch("/api/network/config",{method:"POST",body:n}).then(e=>{showToast(e.ok?t("restarting"):t("save_error"))})}function loadTelegramConfig(){fetch("/api/telegram/config").then(e=>e.json()).then(e=>{$("chk_tg_en").checked=e.enabled,$("txt_tg_token").value=e.token||"",$("txt_tg_chat").value=e.chat_id||""})}function saveTelegram(){let e=$("chk_tg_en").checked?1:0,t=$("txt_tg_token").value,n=$("txt_tg_chat").value;api("telegram/config",{method:"POST",body:new URLSearchParams({enabled:e,token:t,chat_id:n})})}function testTelegram(){fetch("/api/telegram/test",{method:"POST"}).then(async e=>{const n=await e.json();if(!e.ok||!n.accepted)throw new Error(n.error||t("tg_unknown"));showToast("Telegram: queued");for(let e=0;e<160;e++){await new Promise(e=>setTimeout(e,500));const e=await fetch("/api/telegram/test/status?id="+encodeURIComponent(n.request_id)),a=await e.json();if(!e.ok)throw new Error(a.error||t("tg_unknown"));if(a.done)return void showToast(a.success?"Telegram OK!":t("tg_error"))}throw new Error("timeout")}).catch(e=>showToast(t("tg_error")+": "+(e.message||t("comm_error"))))}function onTzChange(){let e=$("tz_sel").value;$("tz_custom_fields").style.display="custom"===e?"block":"none",updateTzLabel()}function updateTzLabel(){let e,t,n=$("tz_sel").value;if("custom"===n)e=parseInt($("tz_std_in").value||"0",10),t=parseInt($("tz_dst_in").value||"0",10);else{let a=n.split(",");e=parseInt(a[0],10),t=parseInt(a[1]||"0",10)}let a=e/3600,i=a>=0?"+":"",o=t?" (DST +"+t/3600+"h)":"";$("tz_device_time").innerText="UTC"+i+a+"h"+o}function loadTimezoneConfig(){fetch("/api/timezone").then(e=>e.ok?e.json():null).then(e=>{if(!e)return;let t=e.tz_offset||0,n=e.dst_offset||0,a=t+","+n,i=$("tz_sel"),o=!1;for(let e=0;e<i.options.length;e++)if(i.options[e].value===a){i.selectedIndex=e,o=!0;break}o||(i.value="custom",$("tz_std_in").value=t,$("tz_dst_in").value=n),onTzChange()})}function loadScheduleConfig(){fetch("/api/schedule").then(e=>e.ok?e.json():null).then(e=>{e&&($("sched_arm_in").value=e.arm_time||"",$("sched_disarm_in").value=e.disarm_time||"",$("sched_auto_arm_in").value=null!=e.auto_arm_minutes?e.auto_arm_minutes:0)})}function saveSchedule(){let e=$("sched_arm_in").value||"",n=$("sched_disarm_in").value||"",a=parseInt($("sched_auto_arm_in").value||"0",10);(isNaN(a)||a<0)&&(a=0),a>1440&&(a=1440);let i=new URLSearchParams({arm_time:e,disarm_time:n,auto_arm_minutes:a}).toString();fetch("/api/schedule?"+i,{method:"POST"}).then(e=>{showToast(e.ok?t("sched_saved"):t("save_error"))})}function saveTimezone(){let e,n,a=$("tz_sel").value;if("custom"===a)e=parseInt($("tz_std_in").value||"0",10),n=parseInt($("tz_dst_in").value||"0",10);else{let t=a.split(",");e=parseInt(t[0],10),n=parseInt(t[1]||"0",10)}fetch("/api/timezone",{method:"POST",body:new URLSearchParams({tz_offset:e,dst_offset:n})}).then(e=>{showToast(e.ok?t("tz_saved"):t("save_error"))})}function loadZones(){fetch("/api/zones").then(e=>e.json()).then(e=>{zones=e,renderZones()}).catch(e=>zones=[])}const ZONE_COLORS=["#1a6b3a","#1a4a6b","#6b1a1a","#4a1a6b"];function renderZones(){let e="";zones.forEach((n,a)=>{const i=n.alarm_behavior??0;e+=`<div style="margin-bottom:5px; background:#222; padding:5px; border-radius:5px; border-left:3px solid ${ZONE_COLORS[i]||"#444"}">\n            <div style="display:flex; gap:5px; margin-bottom:5px">\n                <input type="text" value="${n.name}" id="z_name_${a}" style="flex:2" placeholder="${t("zone_name")}">\n                <input type="number" value="${n.min}" id="z_min_${a}" style="flex:1" placeholder="${t("zone_from")}">\n                <input type="number" value="${n.max}" id="z_max_${a}" style="flex:1" placeholder="${t("zone_to")}">\n            </div>\n            <div style="display:flex; gap:5px; align-items:center">\n                <select id="z_lvl_${a}" style="flex:1">\n                    <option value="0" ${0==n.level?"selected":""}>Log</option>\n                    <option value="1" ${1==n.level?"selected":""}>Info</option>\n                    <option value="2" ${2==n.level?"selected":""}>Warn</option>\n                    <option value="3" ${3==n.level?"selected":""}>ALARM</option>\n                </select>\n                <select id="z_ab_${a}" style="flex:2" title="${t("zone_behavior")}">\n                    <option value="0" ${0==i?"selected":""}>${t("zone_entry_delay")}</option>\n                    <option value="1" ${1==i?"selected":""}>${t("zone_immediate")}</option>\n                    <option value="2" ${2==i?"selected":""}>${t("zone_ignore")}</option>\n                    <option value="3" ${3==i?"selected":""}>${t("zone_ignore_static")}</option>\n                </select>\n                <input type="number" value="${n.delay||0}" id="z_del_${a}" style="flex:1" placeholder="${t("zone_delay")}">\n                <input type="checkbox" id="z_en_${a}" ${!1!==n.enabled?"checked":""} style="width:auto">\n                <button onclick="delZone(${a})" class="warn" style="width:auto; margin:0; padding:5px 10px">×</button>\n            </div>\n        </div>`}),$("zones_list").innerHTML=e,drawZoneMap()}function addZone(){zones.push({name:t("zone_default")+" "+(zones.length+1),min:0,max:100,level:0,alarm_behavior:0,delay:0,enabled:!0}),renderZones()}function delZone(e){zones.splice(e,1),renderZones()}function saveZones(){let e=[];zones.forEach((t,n)=>{e.push({name:document.getElementById(`z_name_${n}`).value,min:parseInt(document.getElementById(`z_min_${n}`).value),max:parseInt(document.getElementById(`z_max_${n}`).value),level:parseInt(document.getElementById(`z_lvl_${n}`).value),alarm_behavior:parseInt(document.getElementById(`z_ab_${n}`).value),delay:parseInt(document.getElementById(`z_del_${n}`).value),enabled:document.getElementById(`z_en_${n}`).checked})}),zones=e,fetch("/api/zones",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(zones)}).then(e=>{e.ok?showToast(t("zones_saved")):showToast(t("save_error"))})}function drawZoneMap(e,t){const n=$("zone_map"),a=$("zone_map_scale");if(!n)return;const i=n.clientWidth||300,o=1050;let s="";if(zones.forEach(e=>{if(!e.enabled)return;const t=e.alarm_behavior??0,n=ZONE_COLORS[t]||"#444",a=Math.round(e.min/o*i),l=Math.round(e.max/o*i);s+=`<rect x="${a}" y="4" width="${l-a}" height="40" fill="${n}" opacity="0.5" rx="3"/>`,s+=`<text x="${a+3}" y="16" font-size="9" fill="#aaa">${e.name}</text>`}),e>0){const t=Math.round(e/o*i);s+=`<line x1="${t}" y1="0" y2="48" x2="${t}" stroke="#bb86fc" stroke-width="2"/>`}if(t>0){const e=Math.round(t/o*i);s+=`<line x1="${e}" y1="0" y2="48" x2="${e}" stroke="#03dac6" stroke-width="2"/>`}n.innerHTML=s,a&&(a.innerText="0cm"+" ".repeat(10)+"525cm"+" ".repeat(10)+"1050cm")}let learnPollTimer=null;function startLearn(){const e=$("learn_dur").value;api(`radar/learn-static?duration=${e}`,{method:"POST"}).then(e=>{e.ok&&($("btn_learn").disabled=!0,$("learn_status").style.display="block",$("learn_status").innerText=t("starting"),learnPollTimer=setInterval(pollLearn,3e3))})}function pollLearn(){fetch("/api/radar/learn-static").then(e=>e.json()).then(e=>{const n=$("learn_status");if(e.active||100!==e.progress)n.innerText=`⏳ ${e.progress}% | ${t("static_label")}: ${e.static_freq_pct}% | Top gate: ${e.top_gate} (~${e.top_cm}cm)`;else{clearInterval(learnPollTimer),$("btn_learn").disabled=!1;let a=`✅ ${t("learn_complete")} Top gate: ${e.top_gate} (~${e.top_cm}cm), confidence: ${e.confidence}%`;e.suggest_ready?a+=` <button onclick="applyLearnZone(${e.suggest_min_cm},${e.suggest_max_cm})" class="sec" style="padding:2px 8px; margin-left:6px">${t("apply")}</button>`:a+=" ⚠️ "+t("not_enough"),n.innerHTML=a}})}function applyLearnZone(e,n){zones.push({name:t("static_label")+"-auto",min:e,max:n,level:0,alarm_behavior:3,delay:0,enabled:!0}),renderZones(),$("learn_status").innerHTML+=" &nbsp;<b>"+t("zone_added")+"</b>"}function saveGates(){let e=[],n=[];for(let t=0;t<14;t++)e.push(parseInt($(`g_m_${t}`).value)),n.push(parseInt($(`g_s_${t}`).value));fetch("/api/radar/gates",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({mov:e,stat:n})}).then(e=>{e.ok?showToast(t("gates_saved")):showToast(t("gates_error"))})}function setPreset(e){fetch("/api/preset?name="+e,{method:"POST"}).then(n=>{n.ok?(showToast(t("preset_applied")+" ("+e+")"),fetch("/api/config").then(e=>e.json()).then(e=>{e.mov_sens&&e.stat_sens&&renderGateSliders(e.mov_sens,e.stat_sens)})):showToast(t("preset_error"))})}function saveHostname(){let e=$("txt_hostname").value;api("config",{method:"POST",body:new URLSearchParams({hostname:e})})}function _otaStatus(e){$("ota_status").innerText=e||""}function _otaSendUpload(e){let n=new FormData;n.append("firmware",e);let a=new XMLHttpRequest;a.open("POST","/api/update"),a.timeout=18e4,a.withCredentials=!0,a.upload.onprogress=e=>$("ota_bar").style.width=e.loaded/e.total*100+"%",a.onload=()=>{$("btn_ota").disabled=!1,a.status>=200&&a.status<300&&0===a.responseText.indexOf("OK")?(_otaStatus(""),alert(t("restarting"))):(_otaStatus("❌ HTTP "+a.status),alert("OTA failed: HTTP "+a.status+"\n"+(a.responseText||"(no body)")))},a.onerror=()=>{$("btn_ota").disabled=!1,_otaStatus("❌ network"),alert("OTA failed: network error (device may still be rebooting)")},a.ontimeout=()=>{$("btn_ota").disabled=!1,_otaStatus("❌ timeout"),alert("OTA failed: timeout (3 min).")},_otaStatus(t("ota_uploading")),a.send(n)}function _otaWaitReboot(e,n){if((n=n||0)>25)return $("btn_ota").disabled=!1,_otaStatus("❌ "+t("ota_cold_failed")),void alert(t("ota_cold_failed"));_otaStatus(t("ota_waiting_reboot")+" ("+n+"s)"),fetch("/api/health",{cache:"no-store",credentials:"include"}).then(e=>e.ok?e.json():Promise.reject(e.status)).then(a=>{("number"==typeof a.uptime?a.uptime:99999)<30?(_otaStatus("✅ "+t("ota_ready_uploading")),setTimeout(()=>_otaSendUpload(e),500)):setTimeout(()=>_otaWaitReboot(e,n+1),1e3)}).catch(()=>setTimeout(()=>_otaWaitReboot(e,n+1),1e3))}function uploadFW(){let e=$("fw_file").files[0];e&&($("ota_bar").style.width="0%",$("btn_ota").disabled=!0,$("ota_cold_reboot").checked?(_otaStatus(t("ota_cold_restart")),fetch("/api/restart",{method:"POST",credentials:"include"}).catch(()=>{}).finally(()=>setTimeout(()=>_otaWaitReboot(e,0),3e3))):_otaSendUpload(e))}function prepareEspota(){const e=document.getElementById("btn_espota");e&&(e.disabled=!0),_otaStatus(t("ota_cold_restart")),fetch("/api/ota/espota/prepare?seconds=120",{method:"POST",credentials:"include"}).then(e=>e.text().then(t=>{let n={};try{n=t?JSON.parse(t):{}}catch(e){n={message:t}}if(!e.ok)throw new Error(n.error||n.message||"HTTP "+e.status);return n})).then(()=>_otaStatus(t("ota_espota_ready"))).catch(e=>_otaStatus("❌ "+e.message)).finally(()=>{e&&(e.disabled=!1)})}function loadOtaTrust(){fetch("/api/ota/trust",{credentials:"include"}).then(e=>e.json()).then(e=>{$("ota_trust_status").innerText=e.https_configured?"✅ HTTPS CA je uložená a ověřování je aktivní.":"⚠️ HTTPS Pull OTA je blokované, dokud neuložíš CA."}).catch(()=>{$("ota_trust_status").innerText="❌ Stav CA nelze načíst."})}function saveOtaTrust(){const e=($("ota_ca_pem").value||"").trim();e?fetch("/api/ota/trust",{method:"POST",credentials:"include",headers:{"Content-Type":"application/json"},body:JSON.stringify({ca_pem:e})}).then(e=>e.text().then(t=>{if(!e.ok)throw new Error(t||"HTTP "+e.status)})).then(()=>{$("ota_ca_pem").value="",loadOtaTrust(),showToast("HTTPS CA uložena")}).catch(e=>{$("ota_trust_status").innerText="❌ "+e.message}):$("ota_trust_status").innerText="⚠️ Vlož PEM CA certifikát."}function clearOtaTrust(){confirm("Odebrat CA? HTTPS Pull OTA pak bude blokované.")&&fetch("/api/ota/trust",{method:"POST",credentials:"include",headers:{"Content-Type":"application/json"},body:'{"clear":true}'}).then(e=>e.text().then(t=>{if(!e.ok)throw new Error(t||"HTTP "+e.status)})).then(loadOtaTrust).catch(e=>{$("ota_trust_status").innerText="❌ "+e.message})}function _pullStatus(e){$("pull_status").innerText=e||""}function _pullPhaseLabel(e){return"accepted"===e||"connecting"===e?t("pull_ota_phase_connecting"):"downloading"===e||"fetching"===e?t("pull_ota_phase_downloading"):"writing"===e||"flashing"===e?t("pull_ota_phase_writing"):"success"===e||"success_rebooting"===e?t("pull_ota_phase_success"):"error"===e||"failed"===e?t("pull_ota_phase_error"):t("pull_ota_phase_idle")}function _pullPoll(e){e>60?$("btn_pull").disabled=!1:fetch("/api/update/pull/status",{credentials:"include"}).then(e=>e.json()).then(t=>{const n=(t.phase||"idle").toLowerCase(),a=_pullPhaseLabel(n),i=t.message||t.last_error||"";_pullStatus(a+(i?" — "+i:"")),"success"!==n&&"success_rebooting"!==n?"error"!==n&&"failed"!==n?setTimeout(()=>_pullPoll(e+1),2e3):$("btn_pull").disabled=!1:setTimeout(()=>location.reload(),4e3)}).catch(()=>setTimeout(()=>_pullPoll(e+1),2e3))}function pullFW(){const e=($("pull_url").value||"").trim();if(!e)return void _pullStatus(t("pull_ota_no_url"));const n=(document.getElementById("pull_auth").value||"").trim(),a=(document.getElementById("pull_md5").value||"").trim();if(!/^[0-9a-fA-F]{32}$/.test(a))return void _pullStatus(t("pull_ota_bad_md5"));const i={url:e};n&&(i.auth=n),i.md5=a,$("btn_pull").disabled=!0,_pullStatus(t("pull_ota_running")),fetch("/api/update/pull",{method:"POST",credentials:"include",headers:{"Content-Type":"application/json"},body:JSON.stringify(i)}).then(e=>e.text().then(t=>{let n={};try{n=t?JSON.parse(t):{}}catch(e){n={message:t}}if(!e.ok)throw new Error(n.error||n.message||"HTTP "+e.status);return n})).then(e=>{if(e&&e.error)return _pullStatus(t("pull_ota_phase_error")+" — "+e.error),void($("btn_pull").disabled=!1);setTimeout(()=>_pullPoll(0),1500)}).catch(e=>{_pullStatus(t("pull_ota_phase_error")+" — "+e),$("btn_pull").disabled=!1})}let alarmArmed=!1,g_isDefaultPass=!1;function loadAlarmStatus(){fetch("/api/alarm/status").then(e=>e.json()).then(e=>{alarmArmed=e.armed,updateAlarmUI(e.state),$("i_entry_dl").value=e.entry_delay||30,$("i_exit_dl").value=e.exit_delay||30,$("chk_dis_rem").checked=!1!==e.disarm_reminder}).catch(()=>{})}function updateAlarmUI(e){let n=$("alarm_badge"),a=$("btn_arm");"disarmed"===e?(n.innerText=t("disarmed"),n.style.color="#888",a.innerText=t("arm"),a.style.background="#b00020"):"arming"===e?(n.innerText=t("arming"),n.style.color="orange",a.innerText=t("disarm"),a.style.background="#3700b3"):"armed_away"===e?(n.innerText=t("armed"),n.style.color="#00ff00",a.innerText=t("disarm"),a.style.background="#3700b3"):"pending"===e?(n.innerText=t("pending"),n.style.color="orange",a.innerText=t("disarm"),a.style.background="#3700b3"):"triggered"===e&&(n.innerText=t("triggered"),n.style.color="red",a.innerText=t("disarm"),a.style.background="#3700b3")}function toggleArm(){alarmArmed||!g_isDefaultPass?alarmArmed?api("alarm/disarm",{method:"POST"}).then(()=>{alarmArmed=!1,loadAlarmStatus()}):api("alarm/arm",{method:"POST"}).then(()=>{alarmArmed=!0,loadAlarmStatus()}):showToast(t("default_pass_warn"))}function saveAlarmConfig(){let e=$("i_entry_dl").value,t=$("i_exit_dl").value,n=$("chk_dis_rem").checked?1:0;api("alarm/config",{method:"POST",body:new URLSearchParams({entry_delay:e,exit_delay:t,disarm_reminder:n})})}function saveAuth(){let e=$("txt_auth_user").value,n=$("txt_auth_pass").value,a=$("txt_auth_pass2").value;e&&n?n===a?e.length<4||n.length<4?showToast(t("min_4_chars")):fetch("/api/auth/config",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:new URLSearchParams({user:e,pass:n})}).then(e=>{e.ok?(showToast(t("pass_changed")),alert(t("creds_changed"))):e.text().then(e=>showToast(e||t("error")))}):showToast(t("pass_mismatch")):showToast(t("enter_creds"))}function exportConfig(){let e=new Date,t=e.getFullYear().toString()+String(e.getMonth()+1).padStart(2,"0")+String(e.getDate()).padStart(2,"0"),n=document.createElement("a");n.href="/api/config/export",n.download="poe2412-config-"+t+".json",document.body.appendChild(n),n.click(),document.body.removeChild(n)}function importConfigPrompt(){$("cfg_import_file").click()}function importConfig(e){e&&(confirm(t("cfg_import_confirm"))?e.text().then(e=>fetch("/api/config/import",{method:"POST",headers:{"Content-Type":"application/json"},body:e})).then(e=>{e.ok?showToast(t("cfg_import_ok")):e.text().then(n=>showToast(t("cfg_import_err").replace("{err}",n||e.status)))}).catch(e=>{showToast(t("cfg_import_err").replace("{err}",e.message||e))}).finally(()=>{$("cfg_import_file").value=""}):$("cfg_import_file").value="")}window.onload=()=>{applyLang(),init()};
</script>
</body>
</html>
)rawliteral";

#endif

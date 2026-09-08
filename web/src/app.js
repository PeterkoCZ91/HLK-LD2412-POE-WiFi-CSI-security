// --- CORE ---
const $ = id => document.getElementById(id);
const api = (ep, opts={}) => {
    // ESPAsyncWebServer hasParam() only checks query params, not POST body, so
    // URLSearchParams bodies are folded into the query string by default.
    // EXCEPTION: opts.inBody keeps it a real form POST body — used for secrets
    // (MQTT password/PIN) that must never land in the URL / logs (S-0).
    if(opts.body instanceof URLSearchParams && !opts.inBody) {
        ep += (ep.includes('?') ? '&' : '?') + opts.body.toString();
        delete opts.body;
    }
    delete opts.inBody;
    return fetch('/api/'+ep, opts).then(r => {
        if(r.ok) showToast("OK"); else showToast(t('error'));
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
        if(d.csi) { renderCsiMainPanel(d.csi); updateCSIUI(d.csi); }
        if(d.fusion) {
            const fp = $('fusion_panel');
            if (fp && fp.style.display === 'none') fp.style.display = '';
            renderFusionPanel(d.fusion);
        }
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
        g_isDefaultPass = !!d.is_default_pass;
        if(d.is_default_pass) $('security_warning').style.display = 'block';
        if(d.auth_user) $('txt_auth_user').value = d.auth_user;
        if(d.hostname) $('txt_hostname').value = d.hostname;
        updateHealth(d);
    });
    loadOtaTrust();
    
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
        $('i_hold').value = (d.hold_time != null) ? d.hold_time / 1000 : '';   // ms → s
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
        updGateCm();   // cm hint next to Min/Max gate inputs

        // Gate Sliders (no energy bars — eng mode broken on V1.26)
        renderGateSliders(d.mov_sens, d.stat_sens);
    });
}

function updateUI(d) {
    // Partial telemetry frame (e.g. {"error":"mutex_timeout"}) has no radar fields —
    // skip it entirely so the readout keeps its last good state instead of flickering
    // to KLID / "Radar odpojen".
    if (d.error) return;
    // Radar disconnected (telemetry `connected:false`) → don't show stale zeros/NaN;
    // show "—" and flag "Radar odpojen".
    const connected = (d.connected !== false);
    const dist = d.distance_mm / 10;
    const distOk = connected && Number.isFinite(dist);
    const movOk = connected && (d.moving_energy != null);
    const statOk = connected && (d.static_energy != null);

    // Sparklines (push 0 instead of NaN/stale so the graph stays valid)
    histDist.push(distOk ? dist : 0); histDist.shift();
    histMov.push(movOk ? d.moving_energy : 0); histMov.shift();
    histStat.push(statOk ? d.static_energy : 0); histStat.shift();

    drawSpark('graph_dist', histDist, 400); // max 400cm
    drawSpark('graph_mov', histMov, 100);
    drawSpark('graph_stat', histStat, 100);

    // Values ("—" when radar offline instead of NaN/stale)
    $('dist_val').innerText = distOk ? dist.toFixed(0) : '—';
    $('mov_val').innerText = movOk ? d.moving_energy + '%' : '—';
    $('stat_val').innerText = statOk ? d.static_energy + '%' : '—';

    let st, stColor;
    if (!g_radarPresent) {
        // CSI-only unit: radar is intentionally absent — detection is shown by the
        // promoted WiFi CSI block, so keep the radar headline empty (no false alarm).
        st = ''; stColor = "#888";
    } else if (!connected) {
        st = t('radar_disconnected'); stColor = "var(--warn)";
    } else {
        st = t('idle'); stColor = "#888";
        if(d.state === "detected") { st = t('detected_state'); stColor = "var(--accent)"; }
        else if(d.state === "hold") { st = t('hold_state'); stColor = "#bb86fc"; }
        if(d.tamper) { st = t('tamper_state'); stColor = "var(--warn)"; }
    }
    $('state_text').innerText = st;
    $('state_text').style.display = st ? '' : 'none';
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
    // CSI-only units latch radar monitoring off → collapse MW chrome.
    if (d.radar_monitoring_disabled !== undefined) applyRadarMode(!d.radar_monitoring_disabled);
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
function setSectionCollapsed(el, collapsed) {
    // Toggle a class (not inline style) so siblings with inline display:block
    // (slider labels) keep their layout when re-expanded.
    el.classList.toggle('collapsed', collapsed);
    let next = el.nextElementSibling;
    while (next && !next.classList.contains('section-title')) {
        next.classList.toggle('sec-collapsed', collapsed);
        next = next.nextElementSibling;
    }
}

function initCollapsible() {
    // Caret indicator is a CSS ::after pseudo-element, so it survives applyLang()
    // overwriting the title's innerHTML on language switch.
    document.querySelectorAll('.section-title').forEach(el => {
        if (el.dataset.nocollapse !== undefined) return;   // status-card readouts stay fixed
        el.classList.add('collapsible');
        el.onclick = () => setSectionCollapsed(el, !el.classList.contains('collapsed'));
        if (el.dataset.collapsed !== undefined) setSectionCollapsed(el, true);
    });
}

// Radar presence drives whether MW chrome is shown. `radar_monitoring_disabled`
// (latched in SecurityMonitor) means this unit has no radar → CSI-only layout.
let g_radarPresent = true, g_radarRevealed = false;
function applyRadarMode(present) {
    g_radarPresent = present;
    const reveal = $('radar_reveal');
    const csiBlock = $('csi_main_block');
    if (present) {
        document.querySelectorAll('.radar-only').forEach(el => el.classList.remove('r-hidden'));
        if (reveal) reveal.style.display = 'none';
        if (csiBlock) csiBlock.classList.remove('promoted');
    } else {
        if (!g_radarRevealed) {
            document.querySelectorAll('.radar-only').forEach(el => el.classList.add('r-hidden'));
        }
        if (reveal) reveal.style.display = '';
        if (csiBlock) csiBlock.classList.add('promoted');
    }
}
function toggleRadarReveal() {
    g_radarRevealed = !g_radarRevealed;
    document.querySelectorAll('.radar-only').forEach(el => el.classList.toggle('r-hidden', !g_radarRevealed));
    const lbl = $('radar_reveal').querySelector('span');
    if (lbl) lbl.innerText = g_radarRevealed ? t('radar_hide') : t('radar_show');
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
    csiRenderSiteModel();
    csiRenderDiagnostics();
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
        $('csi_active_val').innerText = d.active ? t('yes') : t('no');
        $('csi_active_val').style.color = d.active ? 'var(--accent)' : '#888';
        $('csi_ssid_val').innerText  = d.wifi_ssid || '—';
        $('csi_rssi_val').innerText  = (d.wifi_rssi !== undefined && d.wifi_rssi !== 0) ? (d.wifi_rssi + ' dBm') : '—';
        renderCsiRssiQuality(d.wifi_rssi);
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

// Main-panel CSI indicator — renders regardless of active tab so the client sees
// at a glance that CSI is alive (and whether it detects). Three states:
// offline (no WiFi assoc / no packets), KLID (alive, idle), POHYB (detecting).
let csiLastDataMs = 0;   // last time we saw CSI packets (pps>0), for stale-data detection
function renderFusionPanel(f) {
    if (!f) return;
    const clamp = v => Math.max(0, Math.min(100, v || 0));
    const setBar = (id, lvl, on) => {
        const el = $(id); if (!el) return;
        el.style.height = clamp(lvl) + '%';
        el.className = 'fbar-fill' + (on ? ' on' : '');
    };
    // radar N/A on CSI-only (radar-less) nodes — clear the inline height so the
    // stylesheet's .fbar.na hatch (height:100%) can win; inline style beats CSS
    const rbar = $('fbar_radar');
    const radarNa = (f.radar_present === false);
    if (rbar) rbar.classList.toggle('na', radarNa);
    if (radarNa) {
        const el = $('fb_radar');
        if (el) { el.style.height = ''; el.className = 'fbar-fill'; }
    } else {
        setBar('fb_radar', f.radar_lvl, f.radar);
    }
    setBar('fb_csi', f.csi_lvl, f.csi);
    setBar('fb_ml', f.ml_lvl, f.ml);
    const pct = Math.round((f.confidence || 0) * 100);
    if ($('fg_fill')) $('fg_fill').style.width = pct + '%';
    if ($('fg_val')) $('fg_val').innerText = pct + '%';
    if ($('fusion_reason')) $('fusion_reason').innerText = f.reason || '–';
}

// T9: label the last ML decision as a false alarm or a confirmed motion, so
// the sister project's retrain pipeline gets real field samples over time.
function csiFeedback(label) {
    api('csi/feedback?label=' + label, {method:'POST'}).then(r => {
        if (r.ok) r.json().then(d => showToast(t('feedback_saved') + ' (' + d.total + '/' + d.capacity + ')'));
    });
}

function renderCsiMainPanel(csi) {
    const st = $('csi_main_state'), link = $('csi_main_link');
    if (!st || !link) return;
    const now = Date.now();
    const pps = (csi.pps !== undefined) ? csi.pps : 0;
    if (pps > 0) csiLastDataMs = now;
    // rssi==0 = not associated to AP (getWifiRSSI returns 0 when not WL_CONNECTED)
    const associated = (csi.rssi !== undefined && csi.rssi !== 0);
    if (!associated) {
        st.innerText = t('csi_offline');
        st.style.color = '#666';
        link.innerText = '—';
        return;
    }
    const linkTxt = pps.toFixed(1) + ' pkt/s · ' + csi.rssi + ' dBm';
    // Associated but no CSI frames for >5s → detection is starved (weak signal / AP issue).
    // Don't flicker per-frame on momentary pps=0; debounce on last-packet time.
    if (now - csiLastDataMs > 5000) {
        st.innerText = t('csi_nodata');
        st.style.color = 'var(--warn)';
        link.innerText = linkTxt;
        return;
    }
    const motion = !!csi.motion;
    st.innerText = motion ? t('motion') : t('idle');
    st.style.color = motion ? 'var(--accent)' : '#888';
    link.innerText = linkTxt;
}

// CSI needs a WiFi association even though this PoE device uses Ethernet for
// normal networking.  Both weak and excessively strong RSSI can degrade CSI.
function renderCsiRssiQuality(rssi) {
    const el = $('csi_rssi_quality_val');
    if (!el) return;
    if (rssi === undefined || rssi === 0) {
        el.innerText = t('csi_rssi_unavailable');
        el.style.color = '#888';
        el.title = '';
    } else if (rssi > -40) {
        el.innerText = t('csi_rssi_hot');
        el.style.color = 'var(--warn)';
        el.title = t('csi_rssi_hot_hint');
    } else if (rssi < -70) {
        el.innerText = t('csi_rssi_weak');
        el.style.color = 'var(--warn)';
        el.title = '';
    } else {
        el.innerText = t('csi_rssi_good');
        el.style.color = 'var(--accent)';
        el.title = '';
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
        $('csi_motion_val').innerText = csi.motion ? t('motion') : t('idle');
        $('csi_motion_val').style.color = csi.motion ? 'var(--accent)' : '#888';
    }
    if (csi.composite !== undefined) $('csi_comp_val').innerText = csi.composite.toFixed(4);
    if (csi.variance !== undefined)  $('csi_var_val').innerText  = csi.variance.toFixed(4);
    if (csi.pps !== undefined)       $('csi_pps_val').innerText  = csi.pps.toFixed(1);
    if (csi.rssi !== undefined) {
        $('csi_rssi_val').innerText = csi.rssi !== 0 ? csi.rssi + ' dBm' : '—';
        renderCsiRssiQuality(csi.rssi);
    }

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
    let srcLabels = {none:'—', radar:'Radar', csi:'CSI', both:'Radar + CSI', ml:t('src_ml')};
    $('fus_presence_val').innerText = f.presence ? t('detected_state') : t('idle');
    $('fus_presence_val').style.color = f.presence ? 'var(--warn)' : '#888';
    $('fus_conf_val').innerText = (f.confidence !== undefined) ? (f.confidence * 100).toFixed(0) + '%' : '—';
    $('fus_source_val').innerText = srcLabels[f.source] || f.source || '—';
}

function toggleFusion(en) {
    let p = new URLSearchParams();
    p.append('fusion_enabled', en ? '1' : '0');
    api('csi', {method:'POST', body:p}).then(r=>r.json()).then(d => {
        showToast(en ? t('fusion_on') : t('fusion_off'));
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

let csiWifiLoadedSsid = '';
let csiWifiSelectedNetwork = null;
let csiWifiScanTimer = null;
let csiWifiScanPolls = 0;

function loadCsiWifi() {
    api('csi/wifi').then(r => r.json()).then(d => {
        if (d && d.ssid) {
            $('csi_wifi_ssid').value = d.ssid;
            csiWifiLoadedSsid = d.ssid;
        }
        csiWifiSelectedNetwork = null;
        $('csi_wifi_pass').value = '';
    }).catch(() => {});
}

function csiWifiSelectionChanged() {
    if (csiWifiSelectedNetwork && $('csi_wifi_ssid').value !== csiWifiSelectedNetwork.ssid) {
        csiWifiSelectedNetwork = null;
    }
}

function renderCsiWifiScan(data) {
    const list = $('csi_wifi_scan_results');
    list.innerHTML = '';
    const networks = Array.isArray(data.networks) ? data.networks : [];
    if (!networks.length) {
        $('csi_wifi_scan_status').innerText = t('wifi_scan_none');
        return;
    }
    $('csi_wifi_scan_status').innerText = t('wifi_scan_found').replace('{n}', networks.length);
    networks.forEach(n => {
        const row = document.createElement('button');
        row.type = 'button';
        row.className = 'wifi-scan-row';
        const name = document.createElement('span');
        name.className = 'wifi-scan-name';
        name.textContent = (n.current ? '✓ ' : '') + n.ssid;
        const meta = document.createElement('span');
        meta.className = 'wifi-scan-meta';
        meta.textContent = n.rssi + ' dBm · ' + t('wifi_channel') + ' ' + n.channel + ' · ' + (n.secure ? '🔒 ' : '') + n.security;
        if (n.current) meta.textContent += ' · ' + t('wifi_current');
        meta.style.color = n.rssi >= -55 ? 'var(--accent)' : (n.rssi >= -70 ? '#d6b85a' : 'var(--warn)');
        row.appendChild(name);
        row.appendChild(meta);
        row.addEventListener('click', () => {
            csiWifiSelectedNetwork = n;
            $('csi_wifi_ssid').value = n.ssid;
            $('csi_wifi_pass').value = '';
            if (n.secure && n.ssid !== csiWifiLoadedSsid) $('csi_wifi_pass').focus();
        });
        list.appendChild(row);
    });
}

function pollCsiWifiScan() {
    fetch('/api/csi/wifi/scan').then(r => r.json()).then(d => {
        if (d.status === 'running') {
            csiWifiScanPolls++;
            if (csiWifiScanPolls < 40) {
                csiWifiScanTimer = setTimeout(pollCsiWifiScan, 500);
                return;
            }
            d.status = 'failed';
        }
        $('csi_wifi_scan_btn').disabled = false;
        if (d.status === 'complete') renderCsiWifiScan(d);
        else $('csi_wifi_scan_status').innerText = t('wifi_scan_failed');
    }).catch(() => {
        $('csi_wifi_scan_btn').disabled = false;
        $('csi_wifi_scan_status').innerText = t('wifi_scan_failed');
    });
}

function startCsiWifiScan() {
    if (csiWifiScanTimer) clearTimeout(csiWifiScanTimer);
    csiWifiScanPolls = 0;
    $('csi_wifi_scan_btn').disabled = true;
    $('csi_wifi_scan_status').innerText = t('wifi_scanning');
    $('csi_wifi_scan_results').innerHTML = '';
    fetch('/api/csi/wifi/scan', {method:'POST'}).then(r =>
        r.json().catch(() => ({})).then(d => ({ok:r.ok, data:d}))
    ).then(result => {
        if (!result.ok) {
            $('csi_wifi_scan_btn').disabled = false;
            $('csi_wifi_scan_status').innerText = result.data.status === 'busy' ? t('wifi_scan_busy') : t('wifi_scan_failed');
            return;
        }
        pollCsiWifiScan();
    }).catch(() => {
        $('csi_wifi_scan_btn').disabled = false;
        $('csi_wifi_scan_status').innerText = t('wifi_scan_failed');
    });
}

function saveCsiWifi() {
    let ssid = $('csi_wifi_ssid').value.trim();
    let pass = $('csi_wifi_pass').value;
    if (!ssid) { showToast(t('wifi_ssid_empty')); return; }
    let selected = csiWifiSelectedNetwork && csiWifiSelectedNetwork.ssid === ssid ? csiWifiSelectedNetwork : null;
    if (ssid !== csiWifiLoadedSsid && !pass && (!selected || selected.secure)) {
        showToast(t('wifi_pass_required')); return;
    }
    if (!confirm(t('wifi_save_confirm').replace('{ssid}', ssid))) return;
    let p = new URLSearchParams();
    p.append('ssid', ssid);
    p.append('pass', pass);
    if (selected && !selected.secure && !pass) p.append('clear_pass', '1');
    api('csi/wifi', {method:'POST', body:p, inBody:true}).then(r => r.json()).then(d => {
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
    api('csi/wifi', {method:'POST', body:p, inBody:true}).then(r => r.json()).then(d => {
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

// --- Site model (candidate / apply / rollback) ---
function fmtSlot(s) {
    if (!s || !s.valid) return '—';
    return 'gen ' + s.generation + ' · thr ' + parseFloat(s.threshold).toFixed(5) + ' · ' + s.samples + ' smp';
}
function csiRenderSiteModel() {
    api('csi/site_model').then(r=>r.json()).then(d => {
        let a = d.active, c = d.candidate, p = d.previous;
        $('csi_sm_active').innerText = fmtSlot(a);
        $('csi_sm_cand').innerText   = fmtSlot(c);
        $('csi_sm_prev').innerText   = fmtSlot(p);
        // difference candidate vs active threshold
        let diffTxt = '—';
        if (c && c.valid && a && a.valid && a.threshold > 0) {
            let pct = (c.threshold - a.threshold) / a.threshold * 100;
            diffTxt = (pct >= 0 ? '+' : '') + pct.toFixed(1) + ' %';
            $('csi_sm_diff').style.color = Math.abs(pct) > 50 ? '#cf6679' : '';
        } else { $('csi_sm_diff').style.color = ''; }
        $('csi_sm_diff').innerText = diffTxt;
        // Apply enabled only when a genuinely newer candidate exists
        let canApply = !!d.apply_required && !d.learning_active;
        $('csi_sm_apply').disabled    = !canApply;
        $('csi_sm_discard').disabled  = !(c && c.valid);
        $('csi_sm_rollback').disabled = !(p && p.valid);
        let note = '';
        if (d.learning_active) note = '⏳ Learning — detection still on active gen ' + (a && a.valid ? a.generation : '—');
        else if (d.apply_required) note = '🟡 Candidate ready — not yet affecting the alarm. Review and Apply.';
        else if (a && a.valid) note = '🟢 Detection on active gen ' + a.generation + '.';
        $('csi_sm_note').innerText = note;
    }).catch(()=>{});
}
// P1 read-only diagnostics: health / decision / shadow / event ring.
function csiRenderDiagnostics() {
    api('csi/health').then(r=>r.json()).then(d => {
        let el = $('csi_dg_health');
        el.innerText = 'score ' + d.score + ' · ' + ((d.reasons||[]).join(', ') || '—');
        el.style.color = d.healthy ? '#4caf50' : (d.score >= 60 ? '#ffb300' : '#cf6679');
    }).catch(()=>{});
    api('csi/decision').then(r=>r.json()).then(d => {
        if (!d.valid) { $('csi_dg_decision').innerText = d.reason || '—'; return; }
        $('csi_dg_decision').innerText = (d.decision ? 'MOTION' : 'idle') + ' · ' + d.reason;
    }).catch(()=>{});
    api('csi/shadow').then(r=>r.json()).then(d => {
        let el = $('csi_dg_shadow');
        if (!d.active) { el.innerText = 'no candidate'; el.style.color=''; return; }
        let tot = (d.agree||0) + (d.disagree||0);
        let pct = tot ? (d.agree/tot*100).toFixed(1) : '—';
        el.innerText = 'agree ' + d.agree + ' / dis ' + d.disagree + ' (' + pct + '%) — NO ALARM EFFECT';
        el.style.color = d.disagree > 0 ? '#ffb300' : '';
    }).catch(()=>{});
    api('csi/events?limit=1').then(r=>r.json()).then(d => {
        $('csi_dg_events').innerText = d.count + '/' + d.capacity + ' · last seq ' + d.last_seq;
    }).catch(()=>{});
}
function csiApplyModel() {
    api('csi/site_model').then(r=>r.json()).then(d => {
        let a = d.active, c = d.candidate;
        let msg = 'Použít kandidátní model?\n\n';
        msg += 'Aktivní:      ' + fmtSlot(a) + '\n';
        msg += 'Kandidát: ' + fmtSlot(c) + '\n';
        if (c && c.valid && a && a.valid && a.threshold > 0) {
            let pct = (c.threshold - a.threshold) / a.threshold * 100;
            msg += '\nThreshold change: ' + (pct>=0?'+':'') + pct.toFixed(1) + ' %';
            if (Math.abs(pct) > 50) msg += '\n\n⚠️ WARNING: large change (>50%). Check the learning was done in an empty room.';
        }
        msg += '\n\nThe current active model is kept for rollback.';
        if (!confirm(msg)) return;
        api('csi/site_model/apply', {method:'POST'}).then(r => {
            if (!r.ok) r.text().then(tx => alert('Apply failed: ' + tx));
            setTimeout(() => { csiRenderSiteModel(); loadCSIConfig(); }, 400);
        });
    });
}
function csiRollbackModel() {
    if (!confirm('Rollback to the previous model?\n\nThis swaps active and previous — you can rollback again to undo.')) return;
    api('csi/site_model/rollback', {method:'POST'}).then(r => {
        if (!r.ok) r.text().then(tx => alert('Rollback failed: ' + tx));
        setTimeout(() => { csiRenderSiteModel(); loadCSIConfig(); }, 400);
    });
}
function csiDiscardCandidate() {
    if (!confirm('Discard the candidate model?\n\nActive and previous are untouched.')) return;
    api('csi/site_model/candidate', {method:'DELETE'}).then(r => {
        if (!r.ok) r.text().then(tx => alert('Discard failed: ' + tx));
        setTimeout(csiRenderSiteModel, 400);
    });
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
    if (!hasBins) { $('evt_timeline').innerHTML = '<text x="144" y="20" text-anchor="middle" fill="#444" font-size="10">' + t('no_timeline') + '</text>'; return; }
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
    else { container.innerHTML = h || ('<div style="text-align:center; padding:20px; color:#555">' + t('no_events') + '</div>'); }
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

function updGateCm() {
    const r = gateResolution * 100;   // cm per gate
    const mn = $('i_min_cm'), mx = $('i_max_cm');
    if (mn) mn.innerText = '≈ ' + Math.round(($('i_min').value || 0) * r) + ' cm';
    if (mx) mx.innerText = '≈ ' + Math.round(($('i_max').value || 0) * r) + ' cm';
}

function saveBasic() {
    let m = $('i_max').value;
    let min = $('i_min').value;
    let h = Math.round(parseFloat($('i_hold').value || 0) * 1000);   // s → ms
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
    
    api(`security/config`, {
        method: 'POST',
        body: new URLSearchParams({
            'antimask': am,
            'antimask_en': am_en,
            'loiter': lo,
            'loiter_alert': lo_en,
            'heartbeat': hb,
            'pet': pt,
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
        inBody: true,   // keep the broker password out of the URL (S-0)
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
    .then(async r => {
        const d = await r.json();
        if (!r.ok || !d.accepted) throw new Error(d.error || t('tg_unknown'));
        showToast('Telegram: queued');
        for (let attempt = 0; attempt < 160; attempt++) {
            await new Promise(resolve => setTimeout(resolve, 500));
            const statusResponse = await fetch('/api/telegram/test/status?id=' + encodeURIComponent(d.request_id));
            const status = await statusResponse.json();
            if (!statusResponse.ok) throw new Error(status.error || t('tg_unknown'));
            if (status.done) {
                showToast(status.success ? "Telegram OK!" : t('tg_error'));
                return;
            }
        }
        throw new Error('timeout');
    })
    .catch(e => showToast(t('tg_error') + ': ' + (e.message || t('comm_error'))));
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
                <input type="text" value="${z.name}" id="z_name_${i}" style="flex:2" placeholder="${t('zone_name')}">
                <input type="number" value="${z.min}" id="z_min_${i}" style="flex:1" placeholder="${t('zone_from')}">
                <input type="number" value="${z.max}" id="z_max_${i}" style="flex:1" placeholder="${t('zone_to')}">
            </div>
            <div style="display:flex; gap:5px; align-items:center">
                <select id="z_lvl_${i}" style="flex:1">
                    <option value="0" ${z.level==0?'selected':''}>Log</option>
                    <option value="1" ${z.level==1?'selected':''}>Info</option>
                    <option value="2" ${z.level==2?'selected':''}>Warn</option>
                    <option value="3" ${z.level==3?'selected':''}>ALARM</option>
                </select>
                <select id="z_ab_${i}" style="flex:2" title="${t('zone_behavior')}">
                    <option value="0" ${ab==0?'selected':''}>${t('zone_entry_delay')}</option>
                    <option value="1" ${ab==1?'selected':''}>${t('zone_immediate')}</option>
                    <option value="2" ${ab==2?'selected':''}>${t('zone_ignore')}</option>
                    <option value="3" ${ab==3?'selected':''}>${t('zone_ignore_static')}</option>
                </select>
                <input type="number" value="${z.delay||0}" id="z_del_${i}" style="flex:1" placeholder="${t('zone_delay')}">
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
            let txt = `✅ ${t('learn_complete')} Top gate: ${d.top_gate} (~${d.top_cm}cm), confidence: ${d.confidence}%`;
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
function setPreset(pname) {
    fetch('/api/preset?name=' + pname, {method:'POST'}).then(r => {
        if(!r.ok) { showToast(t('preset_error')); return; }
        showToast(t('preset_applied') + " (" + pname + ")");
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

function prepareEspota() {
    const btn = document.getElementById("btn_espota");
    if (btn) btn.disabled = true;
    _otaStatus(t("ota_cold_restart"));
    fetch("/api/ota/espota/prepare?seconds=120", {method:"POST", credentials:"include"})
        .then(r => r.text().then(txt => {
            let d = {};
            try { d = txt ? JSON.parse(txt) : {}; } catch(e) { d = {message: txt}; }
            if (!r.ok) throw new Error(d.error || d.message || ("HTTP " + r.status));
            return d;
        }))
        .then(() => _otaStatus(t("ota_espota_ready")))
        .catch(e => _otaStatus("❌ " + e.message))
        .finally(() => { if (btn) btn.disabled = false; });
}

// --- PULL OTA ---
function loadOtaTrust() {
    fetch('/api/ota/trust', {credentials:'include'}).then(r => r.json()).then(d => {
        $('ota_trust_status').innerText = d.https_configured ? '✅ HTTPS CA je uložená a ověřování je aktivní.' : '⚠️ HTTPS Pull OTA je blokované, dokud neuložíš CA.';
    }).catch(() => { $('ota_trust_status').innerText = '❌ Stav CA nelze načíst.'; });
}
function saveOtaTrust() {
    const ca = ($('ota_ca_pem').value || '').trim();
    if (!ca) { $('ota_trust_status').innerText = '⚠️ Vlož PEM CA certifikát.'; return; }
    fetch('/api/ota/trust', {method:'POST', credentials:'include', headers:{'Content-Type':'application/json'}, body:JSON.stringify({ca_pem:ca})}).then(r => r.text().then(text => { if (!r.ok) throw new Error(text || ('HTTP ' + r.status)); })).then(() => { $('ota_ca_pem').value = ''; loadOtaTrust(); showToast('HTTPS CA uložena'); }).catch(e => { $('ota_trust_status').innerText = '❌ ' + e.message; });
}
function clearOtaTrust() {
    if (!confirm('Odebrat CA? HTTPS Pull OTA pak bude blokované.')) return;
    fetch('/api/ota/trust', {method:'POST', credentials:'include', headers:{'Content-Type':'application/json'}, body:'{"clear":true}'}).then(r => r.text().then(text => { if (!r.ok) throw new Error(text || ('HTTP ' + r.status)); })).then(loadOtaTrust).catch(e => { $('ota_trust_status').innerText = '❌ ' + e.message; });
}
function _pullStatus(msg) { $('pull_status').innerText = msg || ''; }
function _pullPhaseLabel(phase) {
    if (phase === "accepted" || phase === "connecting") return t("pull_ota_phase_connecting");
    if (phase === "downloading" || phase === "fetching") return t("pull_ota_phase_downloading");
    if (phase === "writing" || phase === "flashing") return t("pull_ota_phase_writing");
    if (phase === "success" || phase === "success_rebooting") return t("pull_ota_phase_success");
    if (phase === "error" || phase === "failed") return t("pull_ota_phase_error");
    return t("pull_ota_phase_idle");
}
function _pullPoll(tries) {
    if (tries > 60) { $('btn_pull').disabled = false; return; }
    fetch('/api/update/pull/status', {credentials:'include'})
        .then(r => r.json())
        .then(d => {
            const phase = (d.phase || 'idle').toLowerCase();
            const lbl = _pullPhaseLabel(phase);
            const msg = d.message || d.last_error || "";
            const err = msg ? (" — " + msg) : "";
            _pullStatus(lbl + err);
            if (phase === "success" || phase === "success_rebooting") {
                setTimeout(() => location.reload(), 4000);
                return;
            }
            if (phase === "error" || phase === "failed") {
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
    const auth = (document.getElementById("pull_auth").value || "").trim();
    const md5 = (document.getElementById("pull_md5").value || "").trim();
    if (!/^[0-9a-fA-F]{32}$/.test(md5)) { _pullStatus(t("pull_ota_bad_md5")); return; }
    const body = {url};
    if (auth) body.auth = auth;
    body.md5 = md5;
    $('btn_pull').disabled = true;
    _pullStatus(t('pull_ota_running'));
    fetch('/api/update/pull', {
        method: 'POST',
        credentials: 'include',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(body)
    })
    .then(r => r.text().then(txt => {
        let d = {};
        try { d = txt ? JSON.parse(txt) : {}; } catch(e) { d = {message: txt}; }
        if (!r.ok) throw new Error(d.error || d.message || ("HTTP " + r.status));
        return d;
    }))
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
let g_isDefaultPass = false;
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
    if(!alarmArmed && g_isDefaultPass) {
        showToast(t('default_pass_warn'));
        return;
    }
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
    if(u.length < 4 || p.length < 4) { showToast(t('min_4_chars')); return; }

    fetch('/api/auth/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ user: u, pass: p })
    }).then(r => {
        if(r.ok) { showToast(t('pass_changed')); alert(t('creds_changed')); }
        else r.text().then(errTxt => showToast(errTxt || t('error')));
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

// applyLang() must run on load too — assigning window.onload here would otherwise
// override the <body onload="applyLang()"> attribute (same slot), leaving static
// data-i18n labels untranslated. Run language pass first, then init.
window.onload = () => { applyLang(); init(); };

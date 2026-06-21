/*
 * Smart Plant  ESP32 (WiFi + web dashboard, monitoring only, no pump)
 * Reads soil moisture + DHT11, serves the dashboard on port 80.
 *
 * Wiring:
 *   Soil moisture OUT (analog) -> GPIO35   (ADC1, input-only, WiFi-safe)
 *   DHT11 DATA                 -> GPIO4    (3-pin module = no extra resistor)
 *   both sensors on 3V3, common GND with the ESP32

 */

#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>

// ---------- WiFi ----------
const char* WIFI_SSID = "Wifi_name";//change to your wifi name
const char* WIFI_PASS = "password";//change to your password 

// ---------- Sensors ----------
const int soilPin = 35;          
const int dhtPin  = 4;
DHT dht(dhtPin, DHT11);
WebServer server(80);

// raw thresholds, kept as-is for the Serial label:
//   raw > 3000 = DRY,  raw > 2000 = GOOD,  else WET
// The dashboard wants a 0..100 %, so we map raw -> % anchored to the same
// breakpoints: raw 3000 -> 30 % (dry line),  raw 2000 -> 70 % (wet line).
int moistPct(int raw) {
  int pct = 30 - (int)(0.04f * (raw - 3000));
  return constrain(pct, 0, 100);
}

float g_temp = NAN, g_hum = NAN;
int   g_raw  = 0;
unsigned long lastWateredAt = 0;
bool  hasWatered = false;
unsigned long lastPrint = 0;

void readSensors() {
  long sum = 0;
  for (int i = 0; i < 8; i++) { sum += analogRead(soilPin); delay(5); }
  g_raw = sum / 8;
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) g_temp = t;     // DHT11 sometimes returns NaN; keep last good value
  if (!isnan(h)) g_hum  = h;
}

void printSerial() {
  Serial.print("Moisture: ");
  Serial.print(g_raw);
  Serial.print(" -> ");
  if      (g_raw > 3000) Serial.print("DRY - Water Needed");
  else if (g_raw > 2000) Serial.print("GOOD");
  else                   Serial.print("WET");
  if (!isnan(g_temp)) { Serial.print("  |  Temp: "); Serial.print(g_temp, 1); Serial.print(" C"); }
  if (!isnan(g_hum))  { Serial.print("  Hum: ");     Serial.print(g_hum, 0);  Serial.print(" %"); }
  Serial.println();
}

String jsonData() {
  int pct = moistPct(g_raw);
  String s = "{";
  s += "\"moisture\":" + String(pct);
  s += ",\"temp\":"     + (isnan(g_temp) ? String("null") : String(g_temp, 1));
  s += ",\"humidity\":" + (isnan(g_hum)  ? String("null") : String((int)round(g_hum)));
  if (hasWatered) {
    unsigned long mins = (millis() - lastWateredAt) / 60000UL;
    s += ",\"lastWateredMin\":" + String(mins);
  } else {
    s += ",\"lastWateredMin\":null";
  }
  s += "}";
  return s;
}

// ---------- the dashboard page, served from flash ----------
const char PAGE_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="he" dir="rtl">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>Smart Plant · מעקב השקיה</title>
<link rel="preconnect" href="https://fonts.googleapis.com" />
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin />
<link href="https://fonts.googleapis.com/css2?family=Fraunces:opsz,wght@9..144,500;9..144,600&family=Inter:wght@400;500;600&family=IBM+Plex+Mono:wght@400;500;600&display=swap" rel="stylesheet" />
<style>
  :root {
    --paper:      #F2F4EC;
    --paper-2:    #FBFCF8;
    --ink:        #1E2B22;
    --ink-soft:   #5C6B5F;
    --line:       #D7DECF;

    --soil:       #6B4A2F;
    --soil-dark:  #4A3220;
    --leaf:       #4C9A5A;
    --leaf-deep:  #2F6B3C;

    --dry:        #D98A3D;
    --good:       #4C9A5A;
    --wet:        #4F8BD9;

    /* driven by status */
    --accent:     var(--good);

    --shadow: 0 1px 2px rgba(30,43,34,.06), 0 8px 24px rgba(30,43,34,.07);
    --radius: 18px;
  }

  * { box-sizing: border-box; }

  body {
    margin: 0;
    min-height: 100vh;
    background:
      radial-gradient(120% 80% at 100% -10%, #E7EEDD 0%, transparent 55%),
      radial-gradient(120% 80% at 0% 110%, #E9F1E6 0%, transparent 55%),
      var(--paper);
    color: var(--ink);
    font-family: "Inter", system-ui, -apple-system, "Segoe UI", sans-serif;
    -webkit-font-smoothing: antialiased;
    display: flex;
    justify-content: center;
    padding: clamp(16px, 4vw, 48px);
  }

  .app {
    width: 100%;
    max-width: 760px;
  }

  /* ---------- header ---------- */
  header {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 16px;
    margin-bottom: 28px;
  }
  .brand {
    display: flex;
    align-items: center;
    gap: 12px;
  }
  .brand h1 {
    font-family: "Fraunces", serif;
    font-weight: 600;
    font-size: clamp(26px, 5vw, 38px);
    letter-spacing: -.01em;
    margin: 0;
    line-height: 1;
  }
  .sprout {
    width: 34px; height: 34px; flex: none;
  }
  .conn {
    display: inline-flex;
    align-items: center;
    gap: 7px;
    font-family: "IBM Plex Mono", monospace;
    font-size: 12px;
    color: var(--ink-soft);
    direction: ltr;
  }
  .dot {
    width: 8px; height: 8px; border-radius: 50%;
    background: var(--leaf);
    box-shadow: 0 0 0 0 rgba(76,154,90,.5);
    animation: pulse 2.4s infinite;
  }
  .conn.off .dot { background: #C0473C; animation: none; }
  @keyframes pulse {
    0%   { box-shadow: 0 0 0 0 rgba(76,154,90,.45); }
    70%  { box-shadow: 0 0 0 7px rgba(76,154,90,0); }
    100% { box-shadow: 0 0 0 0 rgba(76,154,90,0); }
  }

  /* ---------- main grid ---------- */
  .grid {
    display: grid;
    grid-template-columns: 240px 1fr;
    gap: 20px;
  }
  @media (max-width: 600px) {
    .grid { grid-template-columns: 1fr; }
  }

  .card {
    background: var(--paper-2);
    border: 1px solid var(--line);
    border-radius: var(--radius);
    box-shadow: var(--shadow);
  }

  /* ---------- signature: the jar ---------- */
  .jar-card {
    grid-row: span 2;
    padding: 22px 18px 18px;
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
  }
  .jar-wrap { width: 168px; }
  svg.jar { width: 100%; height: auto; display: block; }

  .reading {
    margin-top: 14px;
    font-family: "IBM Plex Mono", monospace;
    line-height: 1;
  }
  .reading b {
    font-size: 44px;
    font-weight: 600;
    color: var(--accent);
    font-variant-numeric: tabular-nums;
  }
  .reading span { font-size: 20px; color: var(--ink-soft); }
  .reading small {
    display: block;
    margin-top: 6px;
    font-family: "Inter", sans-serif;
    font-size: 12px;
    letter-spacing: .08em;
    text-transform: uppercase;
    color: var(--ink-soft);
  }

  .status {
    margin-top: 16px;
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 8px 16px;
    border-radius: 999px;
    font-weight: 600;
    font-size: 15px;
    color: var(--accent);
    background: color-mix(in srgb, var(--accent) 12%, transparent);
    border: 1px solid color-mix(in srgb, var(--accent) 30%, transparent);
  }
  .status .leaf-ico { width: 16px; height: 16px; }

  /* ---------- sensor readouts ---------- */
  .sensors {
    padding: 20px;
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 8px;
  }
  .metric { padding: 4px 8px; }
  .metric .k {
    font-size: 11px;
    letter-spacing: .07em;
    text-transform: uppercase;
    color: var(--ink-soft);
    margin-bottom: 8px;
  }
  .metric .v {
    font-family: "IBM Plex Mono", monospace;
    font-size: 26px;
    font-weight: 500;
    font-variant-numeric: tabular-nums;
    direction: ltr;
  }
  .metric .v u { text-decoration: none; font-size: 15px; color: var(--ink-soft); }

  /* ---------- history + action ---------- */
  .bottom {
    padding: 18px 20px 20px;
    display: flex;
    flex-direction: column;
    gap: 14px;
  }
  .bottom .row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
  }
  .bottom h2 {
    font-family: "Fraunces", serif;
    font-weight: 500;
    font-size: 17px;
    margin: 0;
  }
  .last {
    font-family: "IBM Plex Mono", monospace;
    font-size: 12px;
    color: var(--ink-soft);
  }
  .spark { width: 100%; height: 56px; }
  .spark path.area { fill: color-mix(in srgb, var(--accent) 14%, transparent); }
  .spark path.line { fill: none; stroke: var(--accent); stroke-width: 2; stroke-linejoin: round; stroke-linecap: round; }
  .spark line.band { stroke: var(--line); stroke-width: 1; stroke-dasharray: 3 4; }

  button.water {
    appearance: none;
    border: none;
    cursor: pointer;
    font-family: "Inter", sans-serif;
    font-weight: 600;
    font-size: 15px;
    color: var(--paper-2);
    background: var(--leaf-deep);
    padding: 12px 18px;
    border-radius: 12px;
    display: inline-flex;
    align-items: center;
    gap: 9px;
    transition: transform .12s ease, background .2s ease;
  }
  button.water:hover { background: var(--leaf); }
  button.water:active { transform: translateY(1px) scale(.99); }
  button.water:focus-visible { outline: 3px solid color-mix(in srgb, var(--leaf) 45%, transparent); outline-offset: 2px; }
  button.water svg { width: 17px; height: 17px; }

  footer {
    margin-top: 22px;
    font-family: "IBM Plex Mono", monospace;
    font-size: 11px;
    color: var(--ink-soft);
    text-align: center;
    direction: ltr;
  }

  @media (prefers-reduced-motion: reduce) {
    * { animation: none !important; transition: none !important; }
  }
</style>
</head>
<body>
  <div class="app">
    <header>
      <div class="brand">
        <svg class="sprout" viewBox="0 0 32 32" fill="none" aria-hidden="true">
          <path d="M16 30V14" stroke="var(--leaf-deep)" stroke-width="2.4" stroke-linecap="round"/>
          <path d="M16 18C16 13 12 9 6 9c0 5 4 9 10 9Z" fill="var(--leaf)"/>
          <path d="M16 15C16 10 20 6 26 6c0 5-4 9-10 9Z" fill="var(--leaf-deep)"/>
        </svg>
        <h1>Smart Plant</h1>
      </div>
      <div class="conn" id="conn"><span class="dot"></span><span id="connText">מחובר</span></div>
    </header>

    <div class="grid">
      <!-- signature jar -->
      <section class="card jar-card">
        <div class="jar-wrap">
          <svg class="jar" viewBox="0 0 168 232" aria-hidden="true">
            <defs>
              <clipPath id="jarInner">
                <path d="M30 44 q0 -8 8 -8 h92 q8 0 8 8 v150 q0 14 -14 14 h-80 q-14 0 -14 -14 Z"/>
              </clipPath>
            </defs>

            <!-- jar back wall -->
            <path d="M30 44 q0 -8 8 -8 h92 q8 0 8 8 v150 q0 14 -14 14 h-80 q-14 0 -14 -14 Z"
                  fill="#fff" stroke="var(--line)" stroke-width="2"/>

            <!-- water + soil, clipped to the jar -->
            <g clip-path="url(#jarInner)">
              <!-- moisture fill -->
              <g id="water" style="transition: transform .9s cubic-bezier(.22,.61,.36,1)">
                <path id="wave" d="M-100 0 q42 -10 84 0 t84 0 t84 0 t84 0 V260 H-100 Z"
                      fill="var(--accent)" opacity="0.30"/>
                <path d="M-100 6 q42 -10 84 0 t84 0 t84 0 t84 0 V260 H-100 Z"
                      fill="var(--accent)" opacity="0.22"
                      style="animation: drift 6s linear infinite reverse"/>
              </g>
              <!-- soil mound at bottom -->
              <path d="M30 176 q40 -14 108 0 V216 H30 Z" fill="var(--soil)"/>
              <path d="M30 176 q40 -14 108 0 q-30 6 -54 6 t-54 -6Z" fill="var(--soil-dark)" opacity=".5"/>
              <!-- roots -->
              <path d="M84 176 V150 M84 168 q-10 4 -14 12 M84 162 q12 5 15 14"
                    stroke="var(--soil-dark)" stroke-width="2" fill="none" opacity=".55" stroke-linecap="round"/>
            </g>

            <!-- plant (on top) -->
            <g id="plant" style="transform-origin: 84px 150px; transition: transform .9s ease">
              <path d="M84 150 V96" stroke="var(--leaf-deep)" stroke-width="3" stroke-linecap="round"/>
              <path d="M84 120 C70 118 60 106 58 94 c12 0 24 8 26 20Z" fill="var(--leaf)"/>
              <path d="M84 108 C98 106 108 94 110 82 c-12 0 -24 8 -26 20Z" fill="var(--leaf-deep)"/>
              <path d="M84 96 C76 90 74 78 78 70 c8 6 10 18 6 26Z" fill="var(--leaf)"/>
            </g>

            <!-- jar rim -->
            <rect x="28" y="34" width="112" height="9" rx="4.5" fill="#fff" stroke="var(--line)" stroke-width="2"/>
          </svg>
        </div>

        <div class="reading">
          <b id="moistVal">--</b><span>%</span>
          <small>לחות קרקע</small>
        </div>

        <div class="status" id="statusPill">
          <svg class="leaf-ico" viewBox="0 0 16 16" fill="currentColor"><path d="M8 15V8M8 8C8 4 5 1 1 1c0 4 3 7 7 7Zm0-1C8 4 11 1 15 1c0 4-3 7-7 7Z"/></svg>
          <span id="statusText">—</span>
        </div>
      </section>

      <!-- sensors -->
      <section class="card sensors">
        <div class="metric">
          <div class="k">טמפרטורה</div>
          <div class="v"><span id="tempVal">--</span><u>°C</u></div>
        </div>
        <div class="metric">
          <div class="k">לחות אוויר</div>
          <div class="v"><span id="humVal">--</span><u>%</u></div>
        </div>
        <div class="metric">
          <div class="k">השקיה אחרונה</div>
          <div class="v" style="font-size:18px" id="lastWaterVal">--</div>
        </div>
      </section>

      <!-- history + action -->
      <section class="card bottom">
        <div class="row">
          <h2>היסטוריית לחות</h2>
          <span class="last" id="updated">—</span>
        </div>
        <svg class="spark" id="spark" viewBox="0 0 320 56" preserveAspectRatio="none" aria-hidden="true">
          <line class="band" x1="0" y1="39" x2="320" y2="39"></line>
          <line class="band" x1="0" y1="17" x2="320" y2="17"></line>
          <path class="area" d=""></path>
          <path class="line" d=""></path>
        </svg>
        <div class="row">
          <span class="last">סף יבש 30% · סף רטוב 70%</span>
          <button class="water" id="waterBtn">
            <svg viewBox="0 0 24 24" fill="currentColor"><path d="M12 2s7 8 7 13a7 7 0 1 1-14 0c0-5 7-13 7-13Z"/></svg>
            סמן שהשקיתי
          </button>
        </div>
      </section>
    </div>

    <footer>ESP32 · DHT11 + sensor · polling /data</footer>
  </div>

<script>
(() => {
  "use strict";

  // ====== CONFIG — שנה כאן כשמחברים למכשיר אמיתי ======
  const CONFIG = {
    demo:       false,        // false = למשוך נתונים אמיתיים מה-ESP32
    endpoint:   "/data",     // JSON: {moisture, temp, humidity, lastWateredMin}
    waterUrl:   "/water",    // פקודת השקיה (POST/GET)
    pollMs:     4000,
    dryBelow:   30,
    wetAbove:   70,
    historyMax: 40,
  };

  const $ = id => document.getElementById(id);
  const clamp = (n, a, b) => Math.max(a, Math.min(b, n));
  const history = [];

  // ----- demo state (drifts realistically, dries over time) -----
  let demo = { moisture: 58, temp: 23.4, humidity: 47, lastWateredMin: 35 };

  function nextDemo() {
    demo.moisture = clamp(demo.moisture - Math.random() * 1.2, 8, 96); // soil dries
    demo.temp     = clamp(demo.temp + (Math.random() - 0.5) * 0.4, 17, 31);
    demo.humidity = clamp(demo.humidity + (Math.random() - 0.5) * 2.5, 25, 75);
    demo.lastWateredMin += Math.round(CONFIG.pollMs / 60000) || 0;
    return { ...demo };
  }

  function statusOf(m) {
    if (m < CONFIG.dryBelow) return { key: "dry", label: "צריך מים", color: "var(--dry)" };
    if (m > CONFIG.wetAbove)  return { key: "wet", label: "רטוב מדי", color: "var(--wet)" };
    return { key: "good", label: "מצוין", color: "var(--good)" };
  }

  function fmtAgo(min) {
    if (min == null) return "—";
    if (min < 1)  return "הרגע";
    if (min < 60) return `לפני ${min} דק׳`;
    const h = Math.floor(min / 60);
    return h < 24 ? `לפני ${h} שע׳` : `לפני ${Math.floor(h / 24)} ימים`;
  }

  // jar interior geometry (matches viewBox)
  const FILL_TOP = 40, FILL_BOTTOM = 208;

  function render(d) {
    const m = Math.round(d.moisture);
    const st = statusOf(m);
    document.documentElement.style.setProperty("--accent", st.color);

    $("moistVal").textContent = m;
    $("tempVal").textContent  = d.temp.toFixed(1);
    $("humVal").textContent   = Math.round(d.humidity);
    $("lastWaterVal").textContent = fmtAgo(d.lastWateredMin);
    $("statusText").textContent   = st.label;
    $("updated").textContent = "עודכן " + new Date().toLocaleTimeString("he-IL", { hour: "2-digit", minute: "2-digit", second: "2-digit" });

    // raise the water + wilt the plant when dry
    const level = FILL_BOTTOM - (m / 100) * (FILL_BOTTOM - FILL_TOP);
    $("water").style.transform = `translateY(${level}px)`;
    const droop = st.key === "dry" ? 8 : st.key === "wet" ? -2 : 0;
    $("plant").style.transform = `rotate(${droop}deg)`;

    history.push(m);
    if (history.length > CONFIG.historyMax) history.shift();
    drawSpark();
  }

  function drawSpark() {
    if (history.length < 2) return;
    const W = 320, H = 56, n = history.length;
    const x = i => (i / (n - 1)) * W;
    const y = v => H - 4 - (clamp(v, 0, 100) / 100) * (H - 8);
    let line = `M ${x(0).toFixed(1)} ${y(history[0]).toFixed(1)}`;
    for (let i = 1; i < n; i++) line += ` L ${x(i).toFixed(1)} ${y(history[i]).toFixed(1)}`;
    const area = line + ` L ${W} ${H} L 0 ${H} Z`;
    const svg = $("spark");
    svg.querySelector(".line").setAttribute("d", line);
    svg.querySelector(".area").setAttribute("d", area);
  }

  function setConn(online) {
    const el = $("conn");
    el.classList.toggle("off", !online);
    $("connText").textContent = online ? (CONFIG.demo ? "מצב הדגמה" : "מחובר") : "לא מחובר";
  }

  async function tick() {
    if (CONFIG.demo) { setConn(true); render(nextDemo()); return; }
    try {
      const r = await fetch(CONFIG.endpoint, { cache: "no-store" });
      if (!r.ok) throw new Error(r.status);
      const j = await r.json();
      setConn(true);
      render({
        moisture: +j.moisture,
        temp: +j.temp,
        humidity: +j.humidity,
        lastWateredMin: j.lastWateredMin != null ? +j.lastWateredMin : null,
      });
    } catch (e) {
      setConn(false);
    }
  }

  $("waterBtn").addEventListener("click", async () => {
    if (CONFIG.demo) { demo.moisture = clamp(demo.moisture + 28, 0, 100); demo.lastWateredMin = 0; render({ ...demo }); return; }
    try {
      await fetch(CONFIG.waterUrl, { method: "POST" });
    } catch (e) { /* device offline — ignore */ }
    tick();
  });



  tick();
  setInterval(tick, CONFIG.pollMs);
})();
</script>
</body>
</html>

)=====";

void handleRoot() { server.send_P(200, "text/html", PAGE_HTML); }

void handleData() {
  readSensors();
  printSerial();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", jsonData());
}

// No pump in this build - the button just records when you watered by hand.
void handleWater() {
  lastWateredAt = millis();
  hasWatered = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);            // 0..4095
  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.println();
  Serial.print("Dashboard ready -> http://");
  Serial.println(WiFi.localIP());      // open this in a browser

  server.on("/",      HTTP_GET,  handleRoot);
  server.on("/data",  HTTP_GET,  handleData);
  server.on("/water", HTTP_POST, handleWater);
  server.on("/water", HTTP_GET,  handleWater);
  server.begin();

  readSensors();
}

void loop() {
  server.handleClient();

  // also print to Serial every 2 s even when no browser is open
  if (millis() - lastPrint > 2000) {
    lastPrint = millis();
    readSensors();
    printSerial();
  }
}

/**
 * @file WebServerHandler.h
 * @brief Klasse til at håndtere HTTP-requests (webinterface) på Pico W (RP2040).
 *
 * Håndterer:
 *  - Statusvisning og slider-kontrol
 *  - Parametrering af hardware via web
 *  - Log-konfiguration og status
 *  - Filbrowser + upload/download/delete på SD
 *  - Kommunikation med hardware via globale variabler (mutexbeskyttet)
 *
 * Mode UI:
 *  - Skift af mode laver PREVIEW reload (ingen gem): /opsaetning.htm?previewMode=Tid|Klokken|Astro
 *  - "Gem opsætning" gemmer parametre + redirecter til /opsaetning.htm (uden previewMode)
 *
 * Segmenter:
 *  - seg2/seg3 kan begrænses på ugedage via weekmask:
 *    tm_wday mapping: 0=Sunday ... 6=Saturday
 *    mask bit i = (1 << tm_wday)
 *
 * Astro:
 *  - Felter: astroEnabled, astroLat, astroLon, astroSunsetOffsetMin, astroSunriseOffsetMin, astroLuxEarlyStart
 */
#pragma once

#include <WiFi.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <SdFat.h>

#include "LysParam.h"
#include "mitjason.h"
#include "lyslog.h"

// Eksterne variabler (mutexbeskyttelse påkrævet hvis der skrives/ændres!)
extern mutex_t lys_mutex;
extern mutex_t nat_mutex;
extern mutex_t param_mutex;
extern mutex_t pir_mutex;

extern LysLog* lyslog;
extern LysParam lysparam;
extern MitJsonWiFi* mitjason;
extern SdFat sd;

// Midlertidig kopi til webformular (lokal kopi)


class WebServerHandler {
private:
    // ------------------ Utils: query parsing ------------------
    LysParam lysparamWeb;
    static bool hasQueryKeyEq(const String& params, const char* key, const char* value) {
        String needle = String(key) + "=" + value;
        return params.indexOf(needle) >= 0;
    }

    static int toMin(int h, int m) { return h * 60 + m; }
    static void fromMin(int v, int &h, int &m) { h = v / 60; m = v % 60; }

    static void clampStartNotBefore(int &startH, int &startM, int minAllowed) {
        int s = toMin(startH, startM);
        if (s < minAllowed) fromMin(minAllowed, startH, startM);
    }

    static String getRequestLine(const String& req) {
        int lineEnd = req.indexOf("\r\n");
        return (lineEnd > 0) ? req.substring(0, lineEnd) : req;
    }

    static String getQueryStringFromRequestLine(const String& requestLine) {
        int q = requestLine.indexOf('?');
        if (q < 0) return "";
        int space = requestLine.indexOf(' ', q);
        if (space < 0) space = requestLine.length();
        return requestLine.substring(q + 1, space);
    }

    static String getQueryParamFromRequestLine(const String& requestLine, const char* key) {
        String q = getQueryStringFromRequestLine(requestLine);
        if (q.length() == 0) return "";

        String needle = String(key) + "=";
        int p = q.indexOf(needle);
        if (p < 0) return "";

        int start = p + needle.length();
        int end = q.indexOf('&', start);
        if (end < 0) end = q.length();

        String val = q.substring(start, end);
        val.replace("+", " ");
        val.trim();
        return val;
    }

    static bool extractIntFromParams(const String& params, const char* key, int& out) {
        String needle = String(key) + "=";
        int p = params.indexOf(needle);
        if (p < 0) return false;
        int start = p + needle.length();
        int end = params.indexOf('&', start);
        if (end < 0) end = params.length();
        out = params.substring(start, end).toInt();
        return true;
    }

    static bool extractFloatFromParams(const String& params, const char* key, float& out) {
        String needle = String(key) + "=";
        int p = params.indexOf(needle);
        if (p < 0) return false;
        int start = p + needle.length();
        int end = params.indexOf('&', start);
        if (end < 0) end = params.length();
        out = params.substring(start, end).toFloat();
        return true;
    }

    // ------------------ Weekmask helpers ------------------
    static uint8_t parseWeekMaskFromQuery(const String& params, const char* prefix) {
        // prefix: "seg2d" / "seg3d", keys: seg2d0=1..seg2d6=1
        uint8_t mask = 0;
        for (int i = 0; i < 7; i++) {
            String needle = String(prefix) + String(i) + "=1";
            if (params.indexOf(needle) >= 0) mask |= (uint8_t)(1u << i);
        }
        // Hvis ingen dage valgt => fail-open alle dage
        return (mask == 0) ? (uint8_t)0x7F : mask;
    }

static String weekMaskToHtml(uint8_t mask, const char* prefix) {
    static const char* label[7] = {"Søn","Man","Tir","Ons","Tor","Fre","Lør"};
    String out;
    out.reserve(600);
    for (int i = 0; i < 7; i++) {
        bool checked = (mask & (1u << i)) != 0;
        out += "<label style=\"display:inline-block; margin-right:10px;\">";
        out += "<input type=\"checkbox\" name=\"";
        out += prefix;
        out += String(i);
        out += "\" value=\"1\" ";
        if (checked) out += "checked";
        out += "> ";
        out += label[i];
        out += "</label>";
    }
    return out;
}

    // ------------------ Filebrowser path parsing ------------------
    String extractPathFromRequestLine(const String& requestLine) {
        String path = "/";
        int q = requestLine.indexOf('?');
        if (q >= 0) {
            String rest = requestLine.substring(q + 1);
            int space = rest.indexOf(' ');
            if (space >= 0) rest = rest.substring(0, space);
            int p = rest.indexOf("path=");
            if (p >= 0) {
                int start = p + 5;
                int end = rest.indexOf('&', start);
                if (end == -1) end = rest.length();
                path = rest.substring(start, end);
                path.replace("%2F", "/");
                path.replace("%20", " ");
                if (!path.startsWith("/")) path = "/" + path;
            }
        }
        if (path.length() == 0) path = "/";
        return path;
    }

public:
    int&   aktuellysvaerdi;
    float& internaltemp;
    bool&  lys_permanet_on;
    bool&  hardware_aktiv;
    float& aktuellux;
    float& aktueltemp;
    float& aktuelpress;
    bool&  opdaterlys;
    int&   nylysvaerdi;
    bool&  softwarehardset;
    bool&  nataktivstatus;
    String* pir1_tid;
    String* pir2_tid;
    String* hwsw_tid;

    WebServerHandler(
        int& lys, float& temp, bool& lys_on, bool& hwsw_active,
        float& aktuel_lux, float& aktuel_temp, float& aktuel_press,
        bool& software_active, bool& updatelysprocent,
        int& nyupdatevaerdi, bool& natstatus,
        String* pir1txt, String* pir2txt, String* hwswtxt
    )
        : aktuellysvaerdi(lys),
          internaltemp(temp),
          lys_permanet_on(lys_on),
          hardware_aktiv(hwsw_active),
          aktuellux(aktuel_lux),
          aktueltemp(aktuel_temp),
          aktuelpress(aktuel_press),
          opdaterlys(updatelysprocent),
          nylysvaerdi(nyupdatevaerdi),
          softwarehardset(software_active),
          nataktivstatus(natstatus),
          pir1_tid(pir1txt),
          pir2_tid(pir2txt),
          hwsw_tid(hwswtxt)
    {}

    void nylysvaerdiCore1(int vaerdi, bool swstate) {
        mutex_enter_blocking(&lys_mutex);
        nylysvaerdi = vaerdi;
        opdaterlys = true;
        softwarehardset = swstate;
        mutex_exit(&lys_mutex);
    }

    // ------------------ Router ------------------
    void handle(WiFiClient& client, const String& req) {
        if (req.indexOf("GET / ") >= 0 || req.indexOf("GET /index.htm") >= 0) {
            sendIndex(client);
        } else if (req.indexOf("GET /?value=") >= 0) {
            int pos1 = req.indexOf('=');
            int pos2 = req.indexOf('&');
            if (pos1 > 0 && pos2 > pos1) {
                bool softhwset = false;
                int value = req.substring(pos1 + 1, pos2).toInt();
                nylysvaerdiCore1(value, softhwset);
            }
            sendOK(client);
        } else if (req.indexOf("GET /on.htm") >= 0) {
            if (!softwarehardset) {
                bool doLog = false;
                mutex_enter_blocking(&param_mutex);
                doLog = lysparam.logpirdetection;
                mutex_exit(&param_mutex);
                if (doLog && lyslog) lyslog->logPIR("Software on");
                softwarehardset = true;
            }
            sendOK(client);
        } else if (req.indexOf("GET /off.htm") >= 0) {
            if (!hardware_aktiv) nylysvaerdiCore1(0, false);
            if (softwarehardset) {
                bool doLog = false;
                mutex_enter_blocking(&param_mutex);
                doLog = lysparam.logpirdetection;
                mutex_exit(&param_mutex);
                if (doLog && lyslog) lyslog->logPIR("Software off");
                softwarehardset = false;
            }
            sendOK(client);
        } else if (req.indexOf("GET /status.htm") >= 0) {
            sendStatus(client);
        } else if (req.indexOf("GET /favicon.ico") >= 0) {
            client.println("HTTP/1.1 204 No Content");
            client.println();
            return;
        } else if (req.indexOf("GET /opsaetning.htm") >= 0) {
            sendOpsaetning(client, req);
        } else if (req.indexOf("GET /opsaetdata.htm") >= 0) {
            processOpsaetData(client, req);
        } else if (req.indexOf("GET /statusjson.htm") >= 0) {
            sendStatusJSON(client);
        } else if (req.indexOf("GET /logconfig.htm") >= 0) {
            sendLogConfig(client);
        } else if (req.indexOf("GET /gemlogconfig.htm") >= 0) {
            handleGemLogConfig(client, req);
        } else if (req.indexOf("GET /filebrowser.htm") >= 0) {
            sendFileBrowserPage(client);
        } else if (req.indexOf("GET /dirlist") >= 0) {
            handleDirList(client, req);
        } else if (req.indexOf("GET /download") >= 0) {
            handleDownload(client, req);
        } else if (req.indexOf("GET /delete") >= 0) {
            handleDelete(client, req);
        } else if (req.indexOf("POST /upload") >= 0) {
            handleUpload(client, req);
        } else {
            send404(client);
        }
    }

    // ------------------ Pages ------------------
    void sendIndex(WiFiClient& client) {
        client.print(F(
            "<!DOCTYPE html><html lang=\"dk\"><head>"
            "<meta charset=\"utf-8\" />"
            "<title>Kontrol panel</title>"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />"
            "<style>"
            "h1, h2, p {text-align: center;}"
            ".buttonon {background-color: #f44336; border: none; color: white; padding: 20px 40px; font-size: 20px;}"
            ".buttonoff {background-color: #04AA6D; border: none; color: white; padding: 20px 40px; font-size: 20px;}"
            ".stat {text-align:center; margin: 3px; font-size: 1.1em;}"
            ".clock {text-align:center; font-size: 2em; font-weight: bold; margin: 10px 0;}"
            "</style>"
            "</head><body>"
            "<h1>Kontrol panel</h1>"
            "<div class=\"clock\" id=\"clockdiv\">--:--:--</div>"
            "<h2>Lys on / Soft off</h2>"
            "<p>"
            "<button class=\"button buttonon\" title=\"Sætter systemet i låst ON\" id=\"onBtn\">ON</button>"
            "&nbsp;&nbsp;&nbsp;"
            "<button class=\"button buttonoff\" id=\"offBtn\" title=\"Soft OFF slukker lyset og returnerer til automatik.\">Soft OFF</button>"
            "</p>"
            "<h2>Variabel Lysværdi i %</h2>"
            "<p><input type=\"range\" min=\"0\" max=\"100\" class=\"slider\" id=\"Lysslider\" value=\""
        ));
        client.print(aktuellysvaerdi);
        client.print(F(
            "\"></p>"
            "<p>Value: <span id=\"demo\"></span></p>"
            "<p>valgt mode <span id=\"demovalg\">OFF</span></p>"
            "<div class=\"stat\">Målt LUX værdi = <span id=\"luxval\"></span></div>"
            "<div class=\"stat\">Målt temperatur = <span id=\"tempval\"></span> &deg;C</div>"
            "<div class=\"stat\">Målt Barometertryk = <span id=\"pressval\"></span> hPa</div>"
            "<div class=\"stat\">Målt Intern cpu temperatur = <span id=\"cputempval\"></span> C</div>"
            "<div class=\"stat\">Lys permanent on: <span id=\"lysperm\"></span></div>"
            "<p style=\"text-align:center;\">"
            "<button style=\"font-size:15px;\" onclick=\"opsaetning()\">Opsætning</button>"
            "<button style=\"font-size:15px;\" onclick=\"konfigurerLog()\">Log</button>"
            "<button style=\"font-size:15px;\" onclick=\"file()\">Fileoperationer</button>"
            "</p>"
            "<script>"
            "function opsaetning() { location.replace('/opsaetning.htm'); }"
            "function konfigurerLog() { location.replace('/logconfig.htm'); }"
            "function file() { location.replace('/filebrowser.htm'); }"
            "function opdaterDemovalg(val) {"
            "  const demovalg = document.getElementById('demovalg');"
            "  if(val === 0) demovalg.innerHTML = 'Off';"
            "  else if(val === 100) demovalg.innerHTML = 'On';"
            "  else demovalg.innerHTML = 'Variabel';"
            "}"
            "function pad(n){return n<10?'0'+n:n;}"
            "var serverTime=null;"
            "var serverTimeAt=null;"
            "function updateClock(){"
            "  if(serverTime===null) return;"
            "  var elapsed=Math.floor((Date.now()-serverTimeAt)/1000);"
            "  var t=new Date(serverTime.getTime()+elapsed*1000);"
            "  document.getElementById('clockdiv').textContent="
            "    pad(t.getHours())+':'+pad(t.getMinutes())+':'+pad(t.getSeconds());"
            "}"
            "document.addEventListener('DOMContentLoaded', function() {"
            "  const slider = document.getElementById('Lysslider');"
            "  const output = document.getElementById('demo');"
            "  const onBtn = document.getElementById('onBtn');"
            "  const offBtn = document.getElementById('offBtn');"
            "  output.innerHTML = slider.value;"
            "  opdaterDemovalg(parseInt(slider.value));"
            "  let statusLock = false;"
            "  let statusLockTimeout = null;"
            "  function lockStatusUpdate() {"
            "    statusLock = true;"
            "    if (statusLockTimeout) clearTimeout(statusLockTimeout);"
            "    statusLockTimeout = setTimeout(function(){ statusLock = false; }, 1200);"
            "  }"
            "  slider.oninput = function() {"
            "    output.innerHTML = this.value;"
            "    opdaterDemovalg(parseInt(this.value));"
            "    lockStatusUpdate();"
            "  };"
            "  slider.onchange = function() {"
            "    fetch('/?value=' + this.value + '&nocache=' + Math.random());"
            "    lockStatusUpdate();"
            "  };"
            "  onBtn.onclick = function() {"
            "    fetch('/on.htm&').then(function(){"
            "      slider.value = 100;"
            "      output.innerHTML = 100;"
            "      opdaterDemovalg(100);"
            "      lockStatusUpdate();"
            "    });"
            "  };"
            "  offBtn.onclick = function() {"
            "    fetch('/off.htm&').then(function(){"
            "      slider.value = 0;"
            "      output.innerHTML = 0;"
            "      opdaterDemovalg(0);"
            "      lockStatusUpdate();"
            "    });"
            "  };"
            "  function opdaterStatus() {"
            "    if (statusLock) return;"
            "    fetch('/status.htm').then(r=>r.text()).then(function(txt){"
            "      let lux = txt.match(/maalt lux=(-?[\\d\\.]+)/);"
            "      let temp = txt.match(/temp=(-?[\\d\\.]+)/);"
            "      let press = txt.match(/Hpa=(-?[\\d\\.]+)/);"
            "      let cputemp = txt.match(/Cputemp=(-?[\\d\\.]+)/);"
            "      let lys = txt.match(/lys procent=(-?[\\d\\.]+)/);"
            "      let perm = txt.match(/lys_on=([01])/);"
            "      if(lux) document.getElementById('luxval').innerText = lux[1];"
            "      if(temp) document.getElementById('tempval').innerText = temp[1];"
            "      if(press) document.getElementById('pressval').innerText = press[1];"
            "      if(cputemp) document.getElementById('cputempval').innerText = cputemp[1];"
            "      if(lys) {"
            "        slider.value = lys[1];"
            "        output.innerHTML = lys[1];"
            "        opdaterDemovalg(parseInt(lys[1]));"
            "      }"
            "      if(perm) document.getElementById('lysperm').innerText = perm[1]==1?'Ja':'Nej';"
            "    });"
            "  }"
            "  function hentTid() {"
            "    fetch('/statusjson.htm').then(r=>r.json()).then(function(j){"
            "      if(j.time){"
            "        var p=j.time.split(/[\\-: ]/);"
            "        serverTime=new Date(p[0],p[1]-1,p[2],p[3],p[4],p[5]);"
            "        serverTimeAt=Date.now();"
            "      }"
            "    }).catch(function(){});"
            "  }"
            "  opdaterStatus();"
            "  hentTid();"
            "  setInterval(opdaterStatus, 5000);"
            "  setInterval(hentTid, 30000);"
            "  setInterval(updateClock, 1000);"
            "});"
            "</script>"
            "</body></html>"
        ));
    }

    void sendStatus(WiFiClient& client) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-type:text/plain");
        client.println();

        mutex_enter_blocking(&lys_mutex);
        client.print("lys procent="); client.println(aktuellysvaerdi);
        client.print("maalt lux="); client.println(aktuellux);
        client.print("temp="); client.println(aktueltemp);
        client.print("Hpa="); client.println(aktuelpress);
        client.print("Cputemp="); client.println(internaltemp);
        client.print("lys_on="); client.println(lys_permanet_on ? 1 : 0);
        mutex_exit(&lys_mutex);

        mutex_enter_blocking(&pir_mutex);
        client.print("Sidste pir 1 aktivering = "); client.println(pir1_tid ? *pir1_tid : "");
        client.print("Sidste pir 2 aktivering = "); client.println(pir2_tid ? *pir2_tid : "");
        client.print("Sidste Kontakt aktivering = "); client.println(hwsw_tid ? *hwsw_tid : "");
        mutex_exit(&pir_mutex);
    }

    void sendOK(WiFiClient& client) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-type:text/plain");
        client.println("Cache-Control: no-cache");
        client.println();
        client.println("OK");
    }

    void send404(WiFiClient& client) {
        client.println("HTTP/1.1 404 Not Found");
        client.println("Content-type:text/plain");
        client.println();
        client.println("404 Not Found");
    }

    // ------------------ Opsætning ------------------
    void sendOpsaetning(WiFiClient& client, const String& req) {
        mutex_enter_blocking(&param_mutex);
        lysparamWeb = lysparam;
        mutex_exit(&param_mutex);

        String requestLine = getRequestLine(req);
        String previewMode = getQueryParamFromRequestLine(requestLine, "previewMode");

        String renderMode = lysparamWeb.styringsvalg;
        if (previewMode == "Tid" || previewMode == "Klokken" || previewMode == "Astro") {
            renderMode = previewMode; // kun UI
        }

        bool isTid     = (renderMode == "Tid");
        bool isKlokken = (renderMode == "Klokken");
        bool isAstro   = (renderMode == "Astro");
        bool isSegMode = (isKlokken || isAstro);

        // Base HTML + placeholder for mode blocks
        String html = R"rawliteral(
<!DOCTYPE html>
<html lang="da">
<head>
  <meta charset="utf-8">
  <title>Opsætning</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; max-width: 760px; margin: auto; }
    h1 { text-align: center; }
    .slider-block { margin: 18px 0; }
    label { min-width: 210px; display: inline-block; }
    .value { font-weight: bold; margin-left: 16px; }
    .mode-block { margin: 18px 0; padding: 10px; border-top: 1px solid #eee; }
    .segment-box { border: 1px solid #ddd; padding: 12px; margin: 12px 0; }
    .hint { font-size: 0.9em; color: #555; margin-top: 6px; }
    .weekdays { margin: 10px 0; }
    input[type="number"] { padding: 3px; }
  </style>
</head>
<body>
  <h1>Opsætning</h1>

  <form action="/opsaetdata.htm" method="get" oninput="updateValues()">

    <div class="slider-block">
      <label for="pwma">PwmA (%):</label>
      <input type="range" id="pwma" name="pwma" min="0" max="100" value="%PWMA%">
      <span class="value" id="val_pwma">%PWMA%</span>
    </div>

    <div class="slider-block">
      <label for="pwmc">PwmC (%):</label>
      <input type="range" id="pwmc" name="pwmc" min="0" max="100" value="%PWMC%">
      <span class="value" id="val_pwmc">%PWMC%</span>
    </div>

    <div class="slider-block">
      <label for="pwme">PwmE (%):</label>
      <input type="range" id="pwme" name="pwme" min="0" max="100" value="%PWME%">
      <span class="value" id="val_pwme">%PWME%</span>
    </div>

    <div class="slider-block">
      <label for="pwmg">PwmG (%):</label>
      <input type="range" id="pwmg" name="pwmg" min="0" max="100" value="%PWMG%">
      <span class="value" id="val_pwmg">%PWMG%</span>
    </div>

    <div class="slider-block">
      <label for="luxstart">Lux startværdi (LUX):</label>
      <input type="range" id="luxstart" name="luxstart" min="0" max="100" step="1" value="%LUX%">
      <span class="value" id="val_luxstart">%LUX%</span>
    </div>

    <div class="slider-block">
      <label for="delay">Delay (sek):</label>
      <input type="range" id="delay" name="delay" min="0" max="200" value="%NATDAG%">
      <span class="value" id="val_delay">%NATDAG%</span>
    </div>

    <div class="slider-block">
      <label for="stepfrekvens">Softlys step:</label>
      <input type="range" id="stepfrekvens" name="stepfrekvens" min="1" max="10" value="%SOFTSTEP%">
      <span class="value" id="val_stepfrekvens">%SOFTSTEP%</span>
    </div>

    %MODE_BLOCKS%

    <div class="mode-block">
      <label>Styringsmode:</label>
      <input type="radio" id="tid" name="modeselect" value="Tid" %MODETID% onchange="modePreviewChanged(this.value)"> Tid
      <input type="radio" id="klokken" name="modeselect" value="Klokken" %MODEKLOKKEN% onchange="modePreviewChanged(this.value)"> Klokken
      <input type="radio" id="astro" name="modeselect" value="Astro" %MODEASTRO% onchange="modePreviewChanged(this.value)"> Astro
      <div class="hint">Mode-skift her er kun preview. Tryk "Gem opsætning" for at gemme.</div>
    </div>

    <div style="text-align:center; margin: 18px 0;">
      <input type="submit" style="font-size:15px;" value="Gem opsætning">
      <button style="font-size:15px;" onclick="index()" type="button">Kontrol panel</button>
    </div>
  </form>

  <script>
    function index() { location.replace('/index.htm'); }

    function modePreviewChanged(val) {
      location.replace('/opsaetning.htm?previewMode=' + encodeURIComponent(val));
    }

    function updateValues() {
      document.getElementById('val_pwma').textContent = pwma.value;
      document.getElementById('val_pwmc').textContent = pwmc.value;
      document.getElementById('val_pwme').textContent = pwme.value;
      document.getElementById('val_pwmg').textContent = pwmg.value;
      document.getElementById('val_luxstart').textContent = luxstart.value;
      document.getElementById('val_delay').textContent = delay.value;
      document.getElementById('val_stepfrekvens').textContent = stepfrekvens.value;
    }
    updateValues();
  </script>
</body>
</html>
)rawliteral";

        // Build mode blocks
        String blocks;
        blocks.reserve(3500);

        if (isTid) {
            blocks += R"rawliteral(
<div class="segment-box">
  <strong>Timer (Tid-mode)</strong><br><br>

  <div class="slider-block">
    <label for="timera">TimerA (sek):</label>
    <input type="number" id="timera" name="timera" min="0" max="65535" value="%TIMERA%" style="width:90px;">
  </div>

  <div class="slider-block">
    <label for="timerc">TimerC (sek):</label>
    <input type="number" id="timerc" name="timerc" min="0" max="65535" value="%TIMERC%" style="width:90px;">
  </div>

  <div class="slider-block">
    <label for="timere">TimerE (sek):</label>
    <input type="number" id="timere" name="timere" min="0" max="65535" value="%TIMERE%" style="width:90px;">
  </div>
</div>
)rawliteral";
        }

        if (isSegMode) {
            blocks += R"rawliteral(
<div class="slider-block">
  <label for="klokkentimer">Klokken (segment 1 slut):</label>
  <input type="number" id="klokkentimer" name="klokkentimer" min="0" max="23" value="%KLOKKETIMER%" style="width:45px;"> :
  <input type="number" id="klokkenminutter" name="klokkenminutter" min="0" max="59" value="%KLOKKEMINUTTER%" style="width:45px;">
  <div class="hint">Segment 1 kører som før: nataktiv → pwmA indtil dette tidspunkt.</div>
</div>

<div class="segment-box">
  <strong>Segment 2 (tillæg)</strong><br>
  <label for="seg2Enabled">Aktiv:</label>
  <input type="checkbox" id="seg2Enabled" name="seg2Enabled" value="1" %SEG2ENABLED%><br><br>

  <label>Start (hh:mm):</label>
  <input type="number" name="seg2StartTimer" min="0" max="23" value="%SEG2STARTH%" style="width:45px;"> :
  <input type="number" name="seg2StartMinutter" min="0" max="59" value="%SEG2STARTM%" style="width:45px;"><br>

  <label>Slut (hh:mm):</label>
  <input type="number" name="seg2SlutTimer" min="0" max="23" value="%SEG2ENDH%" style="width:45px;"> :
  <input type="number" name="seg2SlutMinutter" min="0" max="59" value="%SEG2ENDM%" style="width:45px;"><br>

  <div class="weekdays">
    <label>Ugedage:</label>
    %SEG2WEEKDAYS%
  </div>

  <div class="hint">Segment 2 start flyttes automatisk frem hvis den er før segment 1 slut.</div>
</div>

<div class="segment-box">
  <strong>Segment 3 (tillæg)</strong><br>
  <label for="seg3Enabled">Aktiv:</label>
  <input type="checkbox" id="seg3Enabled" name="seg3Enabled" value="1" %SEG3ENABLED%><br><br>

  <label>Start (hh:mm):</label>
  <input type="number" name="seg3StartTimer" min="0" max="23" value="%SEG3STARTH%" style="width:45px;"> :
  <input type="number" name="seg3StartMinutter" min="0" max="59" value="%SEG3STARTM%" style="width:45px;"><br>

  <label>Slut (hh:mm):</label>
  <input type="number" name="seg3SlutTimer" min="0" max="23" value="%SEG3ENDH%" style="width:45px;"> :
  <input type="number" name="seg3SlutMinutter" min="0" max="59" value="%SEG3ENDM%" style="width:45px;"><br>

  <div class="weekdays">
    <label>Ugedage:</label>
    %SEG3WEEKDAYS%
  </div>

  <div class="hint">Segment 3 start flyttes automatisk frem hvis den er før segment 2 slut (eller segment 1 slut hvis segment 2 ikke er aktiv).</div>
</div>
)rawliteral";
        }

        if (isAstro) {
            blocks += R"rawliteral(
<div class="segment-box">
  <strong>Astro (solnedgang/solopgang)</strong><br><br>

  <label>Astro aktiv:</label>
  <input type="checkbox" id="astroEnabled" name="astroEnabled" value="1" checked disabled>
  <div class="hint">Astro er altid aktiv når Styringsmode = Astro.</div>
  <br><br>
  <div class="slider-block">
    <label for="astroLat">Latitude:</label>
    <input type="number" step="0.0001" id="astroLat" name="astroLat" value="%ASTROLAT%" style="width:120px;">
  </div>

  <div class="slider-block">
    <label for="astroLon">Longitude:</label>
    <input type="number" step="0.0001" id="astroLon" name="astroLon" value="%ASTROLON%" style="width:120px;">
  </div>

  <div class="slider-block">
    <label for="astroSunsetOffsetMin">Offset solnedgang (min):</label>
    <input type="number" id="astroSunsetOffsetMin" name="astroSunsetOffsetMin" value="%ASTROSUNSETOFF%" style="width:90px;">
  </div>

  <div class="slider-block">
    <label for="astroSunriseOffsetMin">Offset solopgang (min):</label>
    <input type="number" id="astroSunriseOffsetMin" name="astroSunriseOffsetMin" value="%ASTROSUNRISEOFF%" style="width:90px;">
  </div>

  <label for="astroLuxEarlyStart">Lux kan starte nat før solnedgang:</label>
  <input type="checkbox" id="astroLuxEarlyStart" name="astroLuxEarlyStart" value="1" %ASTROLUXEARLY%><br>
</div>
)rawliteral";
        }

        // Insert blocks
        html.replace("%MODE_BLOCKS%", blocks);

        // Replace common placeholders
        html.replace("%PWMA%", String(lysparamWeb.pwmA));
        html.replace("%PWMC%", String(lysparamWeb.pwmC));
        html.replace("%PWME%", String(lysparamWeb.pwmE));
        html.replace("%PWMG%", String(lysparamWeb.pwmG));
        html.replace("%LUX%", String((int)lysparamWeb.luxstartvaerdi));
        html.replace("%NATDAG%", String(lysparamWeb.natdagdelay));
        html.replace("%SOFTSTEP%", String(lysparamWeb.aktuelStepfrekvens));

        // Timer placeholders (only used in Tid blocks)
        html.replace("%TIMERA%", String(lysparamWeb.timerA));
        html.replace("%TIMERC%", String(lysparamWeb.timerC));
        html.replace("%TIMERE%", String(lysparamWeb.timerE));

        // Segment placeholders
        html.replace("%KLOKKETIMER%", String(lysparamWeb.slutKlokkeTimer));
        html.replace("%KLOKKEMINUTTER%", String(lysparamWeb.slutKlokkeMinutter));

        html.replace("%SEG2ENABLED%", lysparamWeb.seg2Enabled ? "checked" : "");
        html.replace("%SEG2STARTH%", String(lysparamWeb.seg2StartTimer));
        html.replace("%SEG2STARTM%", String(lysparamWeb.seg2StartMinutter));
        html.replace("%SEG2ENDH%", String(lysparamWeb.seg2SlutTimer));
        html.replace("%SEG2ENDM%", String(lysparamWeb.seg2SlutMinutter));
        html.replace("%SEG2WEEKDAYS%", weekMaskToHtml(lysparamWeb.seg2WeekMask, "seg2d"));

        html.replace("%SEG3ENABLED%", lysparamWeb.seg3Enabled ? "checked" : "");
        html.replace("%SEG3STARTH%", String(lysparamWeb.seg3StartTimer));
        html.replace("%SEG3STARTM%", String(lysparamWeb.seg3StartMinutter));
        html.replace("%SEG3ENDH%", String(lysparamWeb.seg3SlutTimer));
        html.replace("%SEG3ENDM%", String(lysparamWeb.seg3SlutMinutter));
        html.replace("%SEG3WEEKDAYS%", weekMaskToHtml(lysparamWeb.seg3WeekMask, "seg3d"));

        // Astro placeholders
        html.replace("%ASTROENABLED%", lysparamWeb.astroEnabled ? "checked" : "");
        html.replace("%ASTROLAT%", String(lysparamWeb.astroLat, 4));
        html.replace("%ASTROLON%", String(lysparamWeb.astroLon, 4));
        html.replace("%ASTROSUNSETOFF%", String(lysparamWeb.astroSunsetOffsetMin));
        html.replace("%ASTROSUNRISEOFF%", String(lysparamWeb.astroSunriseOffsetMin));
        html.replace("%ASTROLUXEARLY%", lysparamWeb.astroLuxEarlyStart ? "checked" : "");

        // Mode radio checked uses renderMode
        html.replace("%MODETID%", isTid ? "checked" : "");
        html.replace("%MODEKLOKKEN%", isKlokken ? "checked" : "");
        html.replace("%MODEASTRO%", isAstro ? "checked" : "");

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html; charset=utf-8");
        client.println("Connection: close");
        client.println();
        client.print(html);
    }

    void processOpsaetData(WiFiClient& client, const String& req) {
        // Extract params from full request (naiv men ok her)
        int q = req.indexOf('?');
        if (q < 0) {
            client.println("HTTP/1.1 400 Bad Request\r\n\r\nMissing query");
            return;
        }
        String params = req.substring(q + 1);
        int sp = params.indexOf(' ');
        if (sp > 0) params = params.substring(0, sp);

        // Start med nuværende værdier
        mutex_enter_blocking(&param_mutex);
        lysparamWeb = lysparam;
        mutex_exit(&param_mutex);

        // Standard
        extractIntFromParams(params, "pwma", lysparamWeb.pwmA);
        extractIntFromParams(params, "pwmc", lysparamWeb.pwmC);
        extractIntFromParams(params, "pwme", lysparamWeb.pwmE);
        extractIntFromParams(params, "pwmg", lysparamWeb.pwmG);

        // Backcompat + nyt
        { int tmp; if (extractIntFromParams(params, "toggle", tmp)) lysparamWeb.luxstartvaerdi = (float)tmp; }
        { int tmp; if (extractIntFromParams(params, "luxstart", tmp)) lysparamWeb.luxstartvaerdi = (float)tmp; }

        { int tmp; if (extractIntFromParams(params, "delay", tmp)) lysparamWeb.natdagdelay = tmp; }
        { int tmp; if (extractIntFromParams(params, "stepfrekvens", tmp)) lysparamWeb.aktuelStepfrekvens = tmp; }

        // Tid-mode timers
{ int tmp; if (extractIntFromParams(params, "timera", tmp)) lysparamWeb.timerA = (long)tmp; }
{ int tmp; if (extractIntFromParams(params, "timerc", tmp)) lysparamWeb.timerC = (long)tmp; }
{ int tmp; if (extractIntFromParams(params, "timere", tmp)) lysparamWeb.timerE = (long)tmp; }

        // Segment baseline
        extractIntFromParams(params, "klokkentimer", lysparamWeb.slutKlokkeTimer);
        extractIntFromParams(params, "klokkenminutter", lysparamWeb.slutKlokkeMinutter);

        // Segment 2
        lysparamWeb.seg2Enabled = hasQueryKeyEq(params, "seg2Enabled", "1");
        extractIntFromParams(params, "seg2StartTimer", lysparamWeb.seg2StartTimer);
        extractIntFromParams(params, "seg2StartMinutter", lysparamWeb.seg2StartMinutter);
        extractIntFromParams(params, "seg2SlutTimer", lysparamWeb.seg2SlutTimer);
        extractIntFromParams(params, "seg2SlutMinutter", lysparamWeb.seg2SlutMinutter);
        lysparamWeb.seg2WeekMask = parseWeekMaskFromQuery(params, "seg2d");

        // Segment 3
        lysparamWeb.seg3Enabled = hasQueryKeyEq(params, "seg3Enabled", "1");
        extractIntFromParams(params, "seg3StartTimer", lysparamWeb.seg3StartTimer);
        extractIntFromParams(params, "seg3StartMinutter", lysparamWeb.seg3StartMinutter);
        extractIntFromParams(params, "seg3SlutTimer", lysparamWeb.seg3SlutTimer);
        extractIntFromParams(params, "seg3SlutMinutter", lysparamWeb.seg3SlutMinutter);
        lysparamWeb.seg3WeekMask = parseWeekMaskFromQuery(params, "seg3d");

        // Astro
        lysparamWeb.astroEnabled = hasQueryKeyEq(params, "astroEnabled", "1");
        lysparamWeb.astroLuxEarlyStart = hasQueryKeyEq(params, "astroLuxEarlyStart", "1");
        extractFloatFromParams(params, "astroLat", lysparamWeb.astroLat);
        extractFloatFromParams(params, "astroLon", lysparamWeb.astroLon);
        extractIntFromParams(params, "astroSunsetOffsetMin", lysparamWeb.astroSunsetOffsetMin);
        extractIntFromParams(params, "astroSunriseOffsetMin", lysparamWeb.astroSunriseOffsetMin);

        // Mode
        String mode = "";
        {
            int p = params.indexOf("modeselect=");
            if (p >= 0) {
                int start = p + (int)strlen("modeselect=");
                int end = params.indexOf('&', start);
                if (end < 0) end = params.length();
                mode = params.substring(start, end);
                mode.trim();
            }
        }
        if (mode == "Tid" || mode == "Klokken" || mode == "Astro") {
            lysparamWeb.styringsvalg = mode;
          if (lysparamWeb.styringsvalg == "Astro") {
              lysparamWeb.astroEnabled = true;
          }
        }

        // Kædevalidering (harmløs i Tid-mode)
        int seg1End = toMin(lysparamWeb.slutKlokkeTimer, lysparamWeb.slutKlokkeMinutter);
        if (lysparamWeb.seg2Enabled) {
            clampStartNotBefore(lysparamWeb.seg2StartTimer, lysparamWeb.seg2StartMinutter, seg1End);
        }
        int seg2End = toMin(lysparamWeb.seg2SlutTimer, lysparamWeb.seg2SlutMinutter);
        int minStart3 = lysparamWeb.seg2Enabled ? seg2End : seg1End;
        if (lysparamWeb.seg3Enabled) {
            clampStartNotBefore(lysparamWeb.seg3StartTimer, lysparamWeb.seg3StartMinutter, minStart3);
        }

        // Commit + save
        mutex_enter_blocking(&param_mutex);
        lysparam = lysparamWeb;
        mutex_exit(&param_mutex);

        if (mitjason) mitjason->saveDefault(sd, &lysparamWeb);

        client.println("HTTP/1.1 303 See Other");
        client.println("Location: /opsaetning.htm");
        client.println();
    }

    // ------------------ JSON status ------------------
    void sendStatusJSON(WiFiClient& client) {
        JsonDocument doc;

        mutex_enter_blocking(&lys_mutex);
        doc["lys procent"] = aktuellysvaerdi;
        doc["maalt lux"]   = aktuellux;
        doc["temp"]        = aktueltemp;
        doc["Hpa"]         = aktuelpress;
        doc["Cputemp"]     = internaltemp;
        doc["lys_on"]      = lys_permanet_on;
        mutex_exit(&lys_mutex);

        mutex_enter_blocking(&pir_mutex);
        doc["Sidste pir 1 aktivering"]   = pir1_tid ? *pir1_tid : "";
        doc["Sidste pir 2 aktivering"]   = pir2_tid ? *pir2_tid : "";
        doc["Sidste Kontakt aktivering"] = hwsw_tid ? *hwsw_tid : "";
        mutex_exit(&pir_mutex);

        mutex_enter_blocking(&param_mutex);
        doc["softstep"] = lysparam.aktuelStepfrekvens;
        doc["mode"]     = lysparam.styringsvalg;
        doc["seg2mask"] = lysparam.seg2WeekMask;
        doc["seg3mask"] = lysparam.seg3WeekMask;
        doc["astroEnabled"] = lysparam.astroEnabled;
        doc["astroLat"] = lysparam.astroLat;
        doc["astroLon"] = lysparam.astroLon;
        mutex_exit(&param_mutex);

        // RTC tid til ur-visning i browser
        datetime_t t;
        rtc_get_datetime(&t);
        char timebuf[20];
        snprintf(timebuf, sizeof(timebuf), "%04d-%02d-%02d %02d:%02d:%02d",
                 t.year, t.month, t.day, t.hour, t.min, t.sec);
        doc["time"] = timebuf;

        String vis;
        serializeJsonPretty(doc, vis);
        client.println("HTTP/1.1 200 OK");
        client.println("Content-type: application/json");
        client.println();
        client.println(vis);
    }

    // ------------------ Log config ------------------
    void sendLogConfig(WiFiClient& client) {
        mutex_enter_blocking(&param_mutex);
        lysparamWeb = lysparam;
        mutex_exit(&param_mutex);

        String html = R"rawliteral(
<!DOCTYPE html>
<html lang="da">
<head>
    <meta charset="UTF-8">
    <title>Log-konfiguration</title>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; margin-top: 30px; }
        h2 { margin-bottom: 24px; }
        label, select { font-size: 1.1em; margin: 6px 0; }
        select { min-width: 140px; }
        button { font-size: 1.1em; padding: 8px 22px; margin-top: 20px; }
        form { display: inline-block; text-align: center; margin-top: 20px; }
        .row { margin: 16px 0; }
    </style>
</head>
<body>
    <h2>Log-konfiguration</h2>
    <form action="/gemlogconfig.htm" method="get">
        <div class="row">
            <label for="lognataktiv">Log natdetection:</label>
            <select id="lognataktiv" name="lognataktiv">
                <option value="1" %LOGNATAKTIV_ON%>Aktiveret</option>
                <option value="0" %LOGNATAKTIV_OFF%>Deaktiveret</option>
            </select>
        </div>
        <div class="row">
            <label for="logpirdetection">Log PIR-detection:</label>
            <select id="logpirdetection" name="logpirdetection">
                <option value="1" %LOGPIRDETECTION_ON%>Aktiveret</option>
                <option value="0" %LOGPIRDETECTION_OFF%>Deaktiveret</option>
            </select>
        </div>
        <button type="submit" style="font-size:15px;">Gem</button>
        <button style="font-size:15px;" onclick="index()" type="button">Kontrol panel</button>
    </form>
   <script>
   function index() { location.replace('/index.htm');}
   </script>
</body>
</html>
)rawliteral";

        html.replace("%LOGNATAKTIV_ON%",  lysparamWeb.lognataktiv ? "selected" : "");
        html.replace("%LOGNATAKTIV_OFF%", !lysparamWeb.lognataktiv ? "selected" : "");
        html.replace("%LOGPIRDETECTION_ON%",  lysparamWeb.logpirdetection ? "selected" : "");
        html.replace("%LOGPIRDETECTION_OFF%", !lysparamWeb.logpirdetection ? "selected" : "");

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html; charset=utf-8");
        client.println("Connection: close");
        client.println();
        client.print(html);
    }

    void handleGemLogConfig(WiFiClient& client, const String& req) {
        int idx1 = req.indexOf("lognataktiv=");
        int idx2 = req.indexOf("logpirdetection=");

        bool lognataktiv = false;
        bool logpirdetection = false;

        if (idx1 >= 0) {
            char val = req.charAt(idx1 + (int)strlen("lognataktiv="));
            lognataktiv = (val == '1');
        }
        if (idx2 >= 0) {
            char val = req.charAt(idx2 + (int)strlen("logpirdetection="));
            logpirdetection = (val == '1');
        }

        mutex_enter_blocking(&param_mutex);
        lysparam.lognataktiv = lognataktiv;
        lysparam.logpirdetection = logpirdetection;
        lysparamWeb = lysparam;
        mutex_exit(&param_mutex);

        if (lyslog) {
            lyslog->setLogNatAktiv(lognataktiv);
            lyslog->setLogPIRAktiv(logpirdetection);
        }

        if (mitjason) mitjason->saveDefault(sd, &lysparamWeb);

        client.println("HTTP/1.1 303 See Other");
        client.println("Location: /index.htm");
        client.println();
    }

    // ------------------ File browser ------------------
    void sendFileBrowserPage(WiFiClient& client) {
        String page = R"rawliteral(
<!DOCTYPE html>
<html lang="da">
<head>
  <meta charset="utf-8">
  <title>File Browser</title>
  <style>
    body { font-family: sans-serif; background: #f0f0f0; }
    #filelist { width: 90%; margin: 30px auto; background: #fff; border: 1px solid #ccc; }
    th, td { padding: 8px; }
    th { background: #e0e0e0; }
    tr:hover { background: #f9f9f9; }
    .dir { color: #0070c0; cursor: pointer; }
    .file { color: #333; }
    #uploadForm { margin: 10px auto; text-align: center; }
    #path { font-weight: bold; }
    button { margin: 0 2px; }
  </style>
</head>
<body>
  <h2>Filbrowser</h2>
  <div id="uploadForm">
    <span id="path">/</span>
    <input type="file" id="fileInput" multiple="multiple">
    <button onclick="uploadFile()">Upload</button>
  </div>
  <table id="filelist">
    <thead>
      <tr><th>Navn</th><th>Størrelse</th><th>Type</th><th>Handling</th></tr>
    </thead>
    <tbody id="tbody"></tbody>
  </table>
  <div style="text-align:center;">
    <button style="font-size:15px;" onclick="index()">Kontrol panel</button>
  </div>
<script>
let currentPath = "/";

function index() { location.replace('/index.htm');}
function loadDir(path) {
  fetch("/dirlist?path=" + encodeURIComponent(path))
    .then(resp => resp.json())
    .then(data => {
      currentPath = data.path;
      document.getElementById("path").textContent = currentPath;
      let tbody = document.getElementById("tbody");
      tbody.innerHTML = "";
      if (currentPath !== "/") {
        let up = currentPath.replace(/\\/g, '/').replace(/\/+$/, '').split('/');
        up.pop();
        let upPath = up.length > 1 ? up.join('/') : "/";
        tbody.innerHTML += `<tr>
          <td class="dir" onclick="loadDir('${upPath}')">.. (op)</td>
          <td></td><td></td><td></td>
        </tr>`;
      }
      data.entries.forEach(e => {
        let icon = e.isDir ? "📁" : "📄";
        let action = e.isDir ?
          `<button onclick="loadDir('${e.path}')">Åbn</button>` :
          `<a href="/download?path=${encodeURIComponent(e.path)}" target="_blank">Download</a>
           <button onclick="deleteFile('${e.path}')">Slet</button>`;
        tbody.innerHTML += `
          <tr>
            <td class="${e.isDir ? "dir" : "file"}" onclick="${e.isDir ? `loadDir('${e.path}')` : ""}">${icon} ${e.name}</td>
            <td>${e.isDir ? "" : e.size}</td>
            <td>${e.isDir ? "Mappe" : "Fil"}</td>
            <td>${action}</td>
          </tr>
        `;
      });
    });
}
function deleteFile(path) {
  if (!confirm("Slet filen?")) return;
  fetch("/delete?path=" + encodeURIComponent(path))
    .then(() => loadDir(currentPath));
}
function uploadFile() {
  let input = document.getElementById("fileInput");
  if (!input.files.length) return alert("Vælg fil");
  let form = new FormData();
  for (let i = 0; i < input.files.length; i++) {
    form.append("file", input.files[i]);
  }
  form.append("path", currentPath);
  fetch("/upload", {method: "POST", body: form})
    .then(() => { input.value = ""; loadDir(currentPath); });
}
window.onload = () => loadDir("/");
</script>
</body>
</html>
)rawliteral";

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println();
        client.print(page);
    }

    void handleDirList(WiFiClient& client, const String& req) {
        String requestLine = getRequestLine(req);
        String path = extractPathFromRequestLine(requestLine);

        FsFile dir = sd.open(path.c_str());
        if (!dir || !dir.isDir()) {
            client.println("HTTP/1.1 400 Bad Request");
            client.println("Content-Type: text/plain");
            client.println();
            client.println("Invalid directory");
            return;
        }

        String json = "{\"path\":\"" + path + "\",\"entries\":[";
        bool first = true;
        FsFile entry;
        char namebuf[64];

        while (entry.openNext(&dir, O_RDONLY)) {
            entry.getName(namebuf, sizeof(namebuf));
            if (!first) json += ",";
            json += "{";
            json += "\"name\":\"" + String(namebuf) + "\"";
            String entryPath = path;
            if (!entryPath.endsWith("/")) entryPath += "/";
            entryPath += String(namebuf);
            json += ",\"path\":\"" + entryPath + "\"";
            json += ",\"isDir\":" + String(entry.isDir() ? "true" : "false");
            if (!entry.isDir()) json += ",\"size\":" + String((unsigned long)entry.fileSize());
            json += "}";
            first = false;
            entry.close();
        }
        json += "]}";

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println();
        client.print(json);
    }

    void handleDelete(WiFiClient& client, const String& req) {
        String requestLine = getRequestLine(req);
        String path = extractPathFromRequestLine(requestLine);

        bool ok = sd.remove(path.c_str());
        client.println("HTTP/1.1 200 OK");
        client.println();
        client.println(ok ? "OK" : "FEJL");
    }

    void handleDownload(WiFiClient& client, const String& req) {
        String requestLine = getRequestLine(req);
        String path = extractPathFromRequestLine(requestLine);

        FsFile file = sd.open(path.c_str(), O_RDONLY);
        if (!file) {
            client.println("HTTP/1.1 404 Not Found\r\n\r\n");
            return;
        }

        char namebuf[64] = "download.bin";
        file.getName(namebuf, sizeof(namebuf));

        client.println("HTTP/1.1 200 OK");
        client.print("Content-Disposition: attachment; filename=\"");
        client.print(namebuf);
        client.println("\"");
        client.println("Content-Type: application/octet-stream");
        client.println();

        uint8_t buf[512];
        int n;
        while ((n = file.read(buf, sizeof(buf))) > 0) client.write(buf, n);
        file.close();
    }

    void handleUpload(WiFiClient& client, const String& req) {
        // Multipart upload parser (som din nuværende "rigtige C++" version)
        Serial.println("Upload valgt");

        int boundaryPos = req.indexOf("boundary=");
        if (boundaryPos < 0) {
            client.println("HTTP/1.1 400 Bad Request\r\n\r\nMissing boundary");
            return;
        }
        String boundary = "--" + req.substring(boundaryPos + 9, req.indexOf('\r', boundaryPos));

        int clPos = req.indexOf("Content-Length: ");
        int contentLen = -1;
        if (clPos >= 0) {
            int clEnd = req.indexOf('\r', clPos);
            contentLen = req.substring(clPos + 16, clEnd).toInt();
        }

        int bodyStart = req.indexOf("\r\n\r\n");
        if (bodyStart < 0) {
            client.println("HTTP/1.1 400 Bad Request\r\n\r\nNo body");
            return;
        }
        bodyStart += 4;
        size_t alreadyRead = req.length() - bodyStart;

        static const size_t BUFSIZE = 2048;
        std::vector<uint8_t> buffer;
        buffer.reserve(BUFSIZE * 2);
        size_t bufPos = 0;

        if (alreadyRead > 0) {
            buffer.insert(buffer.end(), req.c_str() + bodyStart, req.c_str() + req.length());
            bufPos += alreadyRead;
        }

        size_t totalBytesRead = alreadyRead;
        size_t totalBytesToRead = (contentLen > 0) ? (size_t)contentLen : (size_t)0x7FFFFFFF;

        enum ParseState { WAIT_BOUNDARY, READ_HEADERS, READ_DATA };
        ParseState state = WAIT_BOUNDARY;

        String curFilename, lastUploadPath = "/";
        FsFile curFile;
        bool isPathField = false;
        String formFieldName;

        auto ensureDirs = [](const String& fullpath) {
            int lastSlash = fullpath.lastIndexOf('/');
            if (lastSlash <= 0) return;
            String dir = fullpath.substring(0, lastSlash);
            String build = "";
            for (size_t i = 1; i < dir.length(); ++i) {
                if (dir[i] == '/' || i == dir.length() - 1) {
                    size_t end = (i == dir.length() - 1) ? i + 1 : i;
                    build = dir.substring(0, end);
                    if (!sd.exists(build.c_str())) sd.mkdir(build.c_str());
                }
            }
        };

        auto findBoundary = [&](const std::vector<uint8_t>& buf, const String& b, size_t start = 0) -> int {
            if (b.length() == 0 || buf.size() < b.length()) return -1;
            for (size_t i = start; i <= buf.size() - b.length(); ++i) {
                bool match = true;
                for (size_t j = 0; j < b.length(); ++j) {
                    if (buf[i + j] != (uint8_t)b[j]) { match = false; break; }
                }
                if (match) return (int)i;
            }
            return -1;
        };

        auto findCRLF = [](const std::vector<uint8_t>& buf, size_t start) -> int {
            for (size_t i = start; i + 1 < buf.size(); ++i) {
                if (buf[i] == 13 && buf[i + 1] == 10) return (int)i;
            }
            return -1;
        };

        bool done = false, anyFile = false;

        while (!done && totalBytesRead < totalBytesToRead) {
            if (buffer.size() < BUFSIZE && totalBytesRead < totalBytesToRead) {
                uint8_t tmp[BUFSIZE];
                int n = client.read(tmp, BUFSIZE);
                if (n > 0) {
                    buffer.insert(buffer.end(), tmp, tmp + n);
                    totalBytesRead += (size_t)n;
                } else {
                    delay(1);
                }
            }

            switch (state) {
                case WAIT_BOUNDARY: {
                    int bstart = findBoundary(buffer, boundary, 0);
                    if (bstart < 0) {
                        if (buffer.size() > boundary.length())
                            buffer.erase(buffer.begin(), buffer.end() - boundary.length());
                        break;
                    }
                    bufPos = (size_t)bstart + boundary.length();
                    if (bufPos + 2 <= buffer.size() && buffer[bufPos] == 13 && buffer[bufPos + 1] == 10) bufPos += 2;
                    if (bufPos + 2 <= buffer.size() && buffer[bufPos] == '-' && buffer[bufPos + 1] == '-') {
                        done = true;
                        break;
                    }
                    state = READ_HEADERS;
                    buffer.erase(buffer.begin(), buffer.begin() + bufPos);
                    bufPos = 0;
                    break;
                }

                case READ_HEADERS: {
                    String headers = "";
                    while (true) {
                        int crlf = findCRLF(buffer, bufPos);
                        if (crlf < 0) break;
                        if (crlf == (int)bufPos) { bufPos += 2; break; }
                        String line((char*)&buffer[bufPos], (size_t)crlf - bufPos);
                        headers += line + "\n";
                        bufPos = (size_t)crlf + 2;
                    }
                    if (bufPos == 0 || bufPos > buffer.size()) break;

                    int namePos = headers.indexOf("name=\"");
                    int nameEnd = headers.indexOf("\"", namePos + 6);
                    formFieldName = (namePos >= 0 && nameEnd > namePos) ? headers.substring(namePos + 6, nameEnd) : "";

                    int fnPos = headers.indexOf("filename=\"");
                    if (fnPos >= 0) {
                        int fnEnd = headers.indexOf("\"", fnPos + 10);
                        curFilename = headers.substring(fnPos + 10, fnEnd);
                        String fullpath = lastUploadPath;
                        if (!fullpath.endsWith("/")) fullpath += "/";
                        fullpath += curFilename;
                        ensureDirs(fullpath);

                        curFile = sd.open(fullpath.c_str(), O_WRITE | O_CREAT | O_TRUNC);
                        anyFile = true;
                        isPathField = false;
                        state = READ_DATA;
                    } else if (formFieldName == "path") {
                        isPathField = true;
                        curFilename = "";
                        state = READ_DATA;
                    } else {
                        state = WAIT_BOUNDARY;
                    }
                    buffer.erase(buffer.begin(), buffer.begin() + bufPos);
                    bufPos = 0;
                    break;
                }

                case READ_DATA: {
                    int boundaryIdx = findBoundary(buffer, boundary, bufPos);
                    if (boundaryIdx < 0) {
                        size_t toWrite = (buffer.size() > boundary.length()) ? buffer.size() - boundary.length() : 0;
                        if (!isPathField && curFile && toWrite > 0) {
                            curFile.write(&buffer[bufPos], toWrite);
                            buffer.erase(buffer.begin(), buffer.begin() + toWrite);
                        }
                        break;
                    } else {
                        if (isPathField) {
                            size_t len = (size_t)boundaryIdx;
                            if (len >= 2 && buffer[boundaryIdx - 2] == 13 && buffer[boundaryIdx - 1] == 10) len -= 2;
                            String pathStr((char*)&buffer[bufPos], len);
                            pathStr.trim();
                            if (!pathStr.startsWith("/")) pathStr = "/" + pathStr;
                            lastUploadPath = pathStr;
                        } else if (curFile) {
                            size_t toWrite = (size_t)boundaryIdx;
                            if (toWrite >= 2 && buffer[boundaryIdx - 2] == 13 && buffer[boundaryIdx - 1] == 10) toWrite -= 2;
                            if (toWrite > 0) curFile.write(&buffer[bufPos], toWrite);
                            curFile.close();
                        }
                        curFile = FsFile();
                        isPathField = false;
                        state = WAIT_BOUNDARY;
                        buffer.erase(buffer.begin(), buffer.begin() + (size_t)boundaryIdx + boundary.length());
                        bufPos = 0;
                    }
                    break;
                }
            }
        }

        if (curFile && curFile.isOpen()) curFile.close();

        if (anyFile) {
            client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nUpload OK");
        } else {
            client.println("HTTP/1.1 400 Bad Request\r\n\r\nNo files uploaded");
        }
    }
};

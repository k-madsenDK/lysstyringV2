/**
 * @file WebServerHandler.h
 * @brief Klasse til at håndtere HTTP-requests (webinterface) på Pico W.
 * 
 * Håndterer:
 *  - Statusvisning og slider-kontrol
 *  - Parametrering af hardware via web
 *  - Log-konfiguration og status
 *  - Kommunikation med hardware via globale variabler (mutexbeskyttet)
 */
#pragma once
#include <WiFi.h>
#include <Arduino.h>
#include "LysParam.h" // bruges af begge core
#include "mitjason.h"
#include <SdFat.h>

// Eksterne variabler fra main/core1, mutexbeskyttelse påkrævet hvis der skrives/ændres!
extern mutex_t lys_mutex;
extern mutex_t nat_mutex;
extern mutex_t param_mutex;
extern mutex_t pir_mutex;

extern LysParam lysparam;
LysParam lysparamWeb;         ///< Midlertidig kopi til webformular

extern MitJsonWiFi* mitjason;
extern SdFat sd;

/**
 * @class WebServerHandler
 * @brief HTTP-request handler: mapping fra web-side til systemparametre.
 */
class WebServerHandler {
  private:
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
    int& aktuellysvaerdi;    ///< PWM-lysværdi
    float& internaltemp;     ///< Intern CPU-temp
    bool& lys_permanet_on;   ///< Om lyset er permanent ON
    float& aktuellux;        ///< Aktuel målt lux
    float& aktueltemp;       ///< Aktuel temperatur
    float& aktuelpress;      ///< Aktuelt lufttryk
    bool& opdaterlys;        ///< Flag: ny lysværdi ønskes
    int& nylysvaerdi;        ///< Ny ønsket PWM-værdi
    bool& softwarehardset;   ///< Om software har låst systemet
    bool& nataktivstatus;    ///< Om nataktiv er aktiv
    String* pir1_tid;        ///< Tid for sidste PIR1-event
    String* pir2_tid;        ///< Tid for sidste PIR2-event
    String* hwsw_tid;        ///< Tid for sidste HW-switch-event
    
    WebServerHandler(
        int& lys, float& temp, bool& lys_on, float& aktuel_lux, float& aktuel_temp,
        float& aktuel_press, bool& software_active, bool& updatelysprocent,
        int& nyupdatevaerdi, bool& natstatus ,String* pir1txt, String* pir2txt, String* hwswtxt
    )
        : aktuellysvaerdi(lys), internaltemp(temp), lys_permanet_on(lys_on), aktuellux(aktuel_lux),
          aktueltemp(aktuel_temp), aktuelpress(aktuel_press), softwarehardset(software_active),
          opdaterlys(updatelysprocent), nylysvaerdi(nyupdatevaerdi), nataktivstatus(natstatus) , pir1_tid(pir1txt), pir2_tid(pir2txt), hwsw_tid(hwswtxt)
    {}
    
        void nylysvaerdiCore1(int vaerdi ,bool swstate){
         mutex_enter_blocking(&lys_mutex);
         nylysvaerdi = vaerdi;
         opdaterlys = true;
         softwarehardset = swstate;
         mutex_exit(&lys_mutex);
    }

    void handle(WiFiClient& client, const String& req) {
        if (req.indexOf("GET / ") >= 0 || req.indexOf("GET /index.htm") >= 0) {
            sendIndex(client);
        } else if (req.indexOf("GET /?value=") >= 0) {
            int pos1 = req.indexOf('=');
            int pos2 = req.indexOf('&');
            if (pos1 > 0 && pos2 > pos1) {
                bool softhwset = false;
                int value = req.substring(pos1+1, pos2).toInt();
                mutex_enter_blocking(&nat_mutex);
                if(!nataktivstatus) softhwset = true;
                mutex_exit(&nat_mutex);
                nylysvaerdiCore1(value , softhwset);
            }
            sendOK(client);
        } else if (req.indexOf("GET /on.htm") >= 0) {
            softwarehardset = true;
            sendOK(client);
        } else if (req.indexOf("GET /off.htm") >= 0) {
            if(!softwarehardset) nylysvaerdiCore1(0 , false);
            softwarehardset = false;
            sendOK(client);
        } else if (req.indexOf("GET /status.htm") >= 0) {
            sendStatus(client);
        } else if (req.indexOf("GET /favicon.ico") >= 0) {
            client.println("HTTP/1.1 204 No Content");
            client.println();
            return;
        } else if (req.indexOf("GET /opsaetning.htm") >= 0){
            sendOpsaetning(client);
        } else  if (req.indexOf("GET /opsaetdata.htm") >= 0) {
            processOpsaetData(client, req);
        } else if (req.indexOf("GET /statusjson.htm") >= 0) {
            sendStatusJSON(client);
        } else if (req.indexOf("GET /logconfig.htm") >= 0) {
            sendLogConfig(client);
        } else if (req.indexOf("GET /gemlogconfig.htm") >= 0) {
            handleGemLogConfig(client , req);  
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

/*
 * client.print("<p>M&aring;lt LUX v&aelig;rdi = "); client.print(aktuellux); client.println("</p>");
client.print("<p>M&aring;lt temperatur = "); client.print(aktueltemp); client.println(" &deg;C</p>");
client.print("<p>M&aring;lt Barometertryk = "); client.print(aktuelpress); client.println(" hPa</p>");
client.print("<p>M&aring;lt Intern cpu temperatur = "); client.print(internaltemp); client.println(" C</p>");
 * */
void sendIndex(WiFiClient& client) {
    client.print(
        "<!DOCTYPE html><html lang=\"dk\"><head>"
        "<meta charset=\"utf-8\" />"
        "<title>Kontrol panel</title>"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />"
        "<style>"
        "h1, h2, p {text-align: center;}"
        ".buttonon {background-color: #f44336; border: none; color: white; padding: 20px 40px; font-size: 20px;}"
        ".buttonoff {background-color: #04AA6D; border: none; color: white; padding: 20px 40px; font-size: 20px;}"
        ".stat {text-align:center; margin: 3px; font-size: 1.1em;}"
        "</style>"
        "</head><body>"
        "<h1>Kontrol panel</h1><br>"
        "<h2>Lys on / Soft off</h2>"
        "<p>"
        "<button class=\"button buttonon\" title=\"Sætter systemet i låst ON samme funktion som en fysisk kontakt\n Systemet returnere f&oslash;rst til automode ved brug af Softoff\" id=\"onBtn\">ON</button>"
        "&nbsp;&nbsp;&nbsp;"
        "<button class=\"button buttonoff\" id=\"offBtn\" title=\"Soft OFF slukker lyset og returnerer til automatik. For service: afbryd strømmen.\n Hvis lys_permanet_on  Ja brug denne knap til at returnere til automode \">Soft OFF</button>"
        "</p>"
        "<h2>Variabel Lysværdi i %</h2>"
        "<p><input type=\"range\" min=\"0\" max=\"100\" class=\"slider\" title=\"Slideren kan anvedes på 2 måder ved brug i automode vil den rette den aktuelle lysværdi dette resettes ved n&aelig;ste skift i logiken\n Bruges den når ON har v&aelig;ret brugt / lux v&aelig;rdien > setpunktet sættes systemet i l&aring;st mode og kan kun resettes af Softoff  \" id=\"Lysslider\" value=\"");
    client.print(aktuellysvaerdi);
    client.print(
        "\"></p>"
        "<p>Value: <span id=\"demo\"></span></p>"
        "<p>valgt mode <span id=\"demovalg\">OFF</span></p>"
        "<div class=\"stat\">Målt LUX værdi = <span id=\"luxval\"></span></div>"
        "<div class=\"stat\">Målt temperatur = <span id=\"tempval\"></span> &deg;C</div>"
        "<div class=\"stat\">Målt Barometertryk = <span id=\"pressval\"></span> hPa</div>"
        "<div class=\"stat\">Målt Intern cpu temperatur = <span id=\"cputempval\"></span> C</div>"
        "<div class=\"stat\">Lys permanent on: <span id=\"lysperm\"></span></div>"
        "<p style=\"text-align:center;\">"
        "<button style=\" font-size:15px;\" title=\"G&aring; til ops&aelig;tnings siden\" onclick=\"opsaetning()\">Opsætning</button>"
        "<button style=\" font-size:15px;\" title=\"ops&aelig;tning af log funktionerne\" onclick=\"konfigurerLog()\">  Log  </button>"
        "<button style=\" font-size:15px;\" title=\"Start fil operationer\" onclick=\"file()\">Fileoperationer</button>"
        "<script>"
        "function opsaetning() { location.replace('/opsaetning.htm');}"
        "function konfigurerLog() {location.replace('/logconfig.htm');}"
        " function file() {location.replace(\"/filebrowser.htm\");}"
        "function opdaterDemovalg(val) {"
        "  const demovalg = document.getElementById('demovalg');"
        "  if(val === 0) demovalg.innerHTML = 'Off';"
        "  else if(val === 100) demovalg.innerHTML = 'On';"
        "  else demovalg.innerHTML = 'Variabel';"
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
        "      let lux = txt.match(/maalt lux=([\\d\\.]+)/);"
        "      let temp = txt.match(/temp=([\\d\\.]+)/);"
        "      let press = txt.match(/Hpa=([\\d\\.]+)/);"
        "      let cputemp = txt.match(/Cputemp=([\\d\\.]+)/);"
        "      let lys = txt.match(/lys procent=([\\d\\.]+)/);"
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
        "  opdaterStatus();"
        "  setInterval(opdaterStatus, 5000);"
        "});"
        "</script>"
        "</body></html>"
    );
}
  
    void sendStatus(WiFiClient& client) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-type:text/plain");
        client.println();
        mutex_enter_blocking(&lys_mutex);
        client.print("lys procent="); client.println(aktuellysvaerdi);
        client.print("maalt lux="); client.println(aktuellux);
        client.print("temp="); client.println(aktueltemp);
        client.print("Hpa=");client.println(aktuelpress);
        client.print("Cputemp="); client.println(internaltemp);
        client.print("lys_on="); client.println(lys_permanet_on ? 1 : 0);
        mutex_exit(&lys_mutex);
        mutex_enter_blocking(&pir_mutex);
        client.print("Sidste pir 1 aktivering = ");  client.println(*pir1_tid);
        client.print("Sidste pir 2 aktivering = ");  client.println(*pir2_tid);
        client.print("Sidste Kontakt aktivering = ");  client.println(*hwsw_tid);
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


void sendOpsaetning(WiFiClient& client) {

    mutex_enter_blocking(&param_mutex);
    lysparamWeb = lysparam;
    mutex_exit(&param_mutex);
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Opsætning</title>
  <meta charset="utf-8">
  <style>
    body { font-family: Arial; max-width: 600px; margin: auto; }
    h1 { text-align: center; }
    .slider-block { margin: 18px 0; }
    label { min-width: 140px; display: inline-block; }
    .value { font-weight: bold; margin-left: 16px; }
    .mode-block { margin: 20px 0; }
  </style>
</head>
<body>
  <h1>Opsætning</h1>
  <form action="/opsaetdata.htm" method="get" oninput="updateValues()">
    <div class="slider-block">
      <label for="pwma">PwmA (%):</label>
      <input type="range" id="pwma" title="styrken som anvendes n&aring;r pir ikke er aktiveret styres af TimerA" name="pwma" min="0" max="100" value="%PWMA%">
      <span class="value" id="val_pwma">%PWMA%</span>
    </div>
    <div class="slider-block">
      <label for="pwmc">PwmC (%):</label>
      <input type="range" id="pwmc" title="styrken som anvendes n&aring;r pir er aktiveret styres af TimerC TimerC og TimerE aktiveres af Pir sensor" name="pwmc" min="0" max="100" value="%PWMC%">
      <span class="value" id="val_pwmc">%PWMC%</span>
    </div>
    <div class="slider-block">
      <label for="pwme">PwmE (%):</label>
      <input type="range" id="pwme" title="styrken som anvendes efter TimerC er udl&oslash;bet styres af TimerE TimerC og TimerE aktiveres af Pir sensor" name="pwme" min="0" max="100" value="%PWME%">
      <span class="value" id="val_pwme">%PWME%</span>
    </div>
    <div class="slider-block">
      <label for="pwmg">PwmG (%):</label>
      <input type="range" id="pwmg" title="styrken som anvendes efter TimerA / Klokken er paseret indtil Lux g&aring;r over Togglepunkt" name="pwmg" min="0" max="100" value="%PWMG%">
      <span class="value" id="val_pwmg">%PWMG%</span>
    </div>
    <div class="slider-block">
      <label for="toggle">Togglepunkt LUX:</label>
      <input type="range" id="toggle" title="skift mellem nat og dag" name="toggle" min="0" max="100" step="1" value="%LUX%">
      <span class="value" id="val_toggle">%LUX%</span>
    </div>
    <div class="slider-block">
      <label for="delay">Delay (sek):</label>
      <input type="range" id="delay" title="tiden som lux skal v&aelig;re under Togglepunkt f&oslash;r TimerA starter " name="delay" min="0" max="200" value="%NATDAG%">
      <span class="value" id="val_delay">%NATDAG%</span>
    </div>
    <!-- NYT: Softlys step slider -->
    <div class="slider-block">
      <label for="stepfrekvens">Softlys step:</label>
      <input type="range" id="stepfrekvens" name="stepfrekvens" min="1" max="10" value="%SOFTSTEP%">
      <span class="value" id="val_stepfrekvens">%SOFTSTEP%</span>
    </div>
    <div class="slider-block">
      <label for="timera">TimerA (sek):</label>
      <input type="number" id="timera" title="Tiden TimerA er aktiveret Aktiveres af togglemode i Tidsmode" name="timera" min="0" max="65535" value="%TIMERA%" style="width:80px;">
    </div>
    <div class="slider-block">
      <label for="timerc">TimerC (sek):</label>
      <input type="number" id="timerc" title="Tiden TimerC er aktiveret f&oslash;rste del af pirdetektion" name="timerc" min="0" max="65535" value="%TIMERC%" style="width:80px;">
    </div>
    <div class="slider-block">
      <label for="timere">TimerE (sek):</label>
      <input type="number" id="timere" title="Tiden TimerE er aktiveret anden del af pirdetektion" name="timere" min="0" max="65535" value="%TIMERE%" style="width:80px;">
    </div>
    <div class="slider-block">
      <label for="klokkentimer">Klokken (timer:min):</label>
      <input type="number" id="klokkentimer" title="Slut tidspunkt hvis man &oslash;nsker samme tispunkt &aring;ret rundt" name="klokkentimer" min="0" max="23" value="%KLOKKETIMER%" style="width:40px;"> :
      <input type="number" id="klokkenminutter" title="Slut tidspunkt hvis man &oslash;nsker samme tispunkt &aring;ret rundt"  name="klokkenminutter" min="0" max="59" value="%KLOKKEMINUTTER%" style="width:40px;">
    </div>
    <div class="mode-block">
      <label for="modeselect">Styringsmode:</label>
      <input type="radio" id="tid" title="Fast tidsl&aelig;ngde" name="modeselect" value="Tid" %MODETID%> Tid
      <input type="radio" id="klokken" title="Samme afslutnings tidspunkt &aring;ret rundt" name="modeselect" value="Klokken" %MODEKLOKKEN%> Klokken
    </div>
    <div style="text-align:center;">
      <input type="submit" title="Opdater v&aelig;rdierne" value="Gem opsætning">
    </div>
  </form>
  <script>
    function updateValues() {
      document.getElementById('val_pwma').textContent = pwma.value;
      document.getElementById('val_pwmc').textContent = pwmc.value;
      document.getElementById('val_pwme').textContent = pwme.value;
      document.getElementById('val_pwmg').textContent = pwmg.value;
      document.getElementById('val_toggle').textContent = toggle.value;
      document.getElementById('val_delay').textContent = delay.value;
      document.getElementById('val_stepfrekvens').textContent = stepfrekvens.value;
    }
    updateValues();
  </script>
</body>
</html>
)rawliteral";

    // Erstat alle placeholders med aktuelle værdier fra lysparamWeb
    html.replace("%PWMA%", String(lysparamWeb.pwmA));
    html.replace("%PWMC%", String(lysparamWeb.pwmC));
    html.replace("%PWME%", String(lysparamWeb.pwmE));
    html.replace("%PWMG%", String(lysparamWeb.pwmG));
    html.replace("%LUX%", String(int(lysparamWeb.luxstartvaerdi)));
    html.replace("%NATDAG%", String(lysparamWeb.natdagdelay));
    html.replace("%SOFTSTEP%", String(lysparamWeb.aktuelStepfrekvens)); // NYT
    html.replace("%TIMERA%", String(lysparamWeb.timerA));
    html.replace("%TIMERC%", String(lysparamWeb.timerC));
    html.replace("%TIMERE%", String(lysparamWeb.timerE));
    html.replace("%KLOKKETIMER%", String(lysparamWeb.slutKlokkeTimer));
    html.replace("%KLOKKEMINUTTER%", String(lysparamWeb.slutKlokkeMinutter));
    html.replace("%MODETID%", (lysparamWeb.styringsvalg == "Tid") ? "checked" : "");
    html.replace("%MODEKLOKKEN%", (lysparamWeb.styringsvalg == "Klokken") ? "checked" : "");
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Connection: close");
    client.println();
    client.print(html);
}

void processOpsaetData(WiFiClient& client, const String& req) {
    String params = req.substring(req.indexOf('?') + 1);
    int idx;
    #define EXTRACT_INT_PARAM(name, var) do { \
        idx = params.indexOf(#name "="); \
        if (idx >= 0) { \
            int start = idx + String(#name "=").length(); \
            int end = params.indexOf('&', start); \
            if (end == -1) end = params.length(); \
            var = params.substring(start, end).toInt(); \
        } \
    } while (0)
    #define EXTRACT_STR_PARAM(name, var) do { \
        idx = params.indexOf(#name "="); \
        if (idx >= 0) { \
            int start = idx + String(#name "=").length(); \
            int end = params.indexOf('&', start); \
            if (end == -1) end = params.length(); \
            var = params.substring(start, end); \
        } \
    } while (0)
    EXTRACT_INT_PARAM(pwma, lysparamWeb.pwmA);
    EXTRACT_INT_PARAM(pwmc, lysparamWeb.pwmC);
    EXTRACT_INT_PARAM(pwme, lysparamWeb.pwmE);
    EXTRACT_INT_PARAM(pwmg, lysparamWeb.pwmG);
    EXTRACT_INT_PARAM(toggle, lysparamWeb.luxstartvaerdi);
    EXTRACT_INT_PARAM(delay, lysparamWeb.natdagdelay);
    EXTRACT_INT_PARAM(stepfrekvens, lysparamWeb.aktuelStepfrekvens); // NYT
    EXTRACT_INT_PARAM(timera, lysparamWeb.timerA);
    EXTRACT_INT_PARAM(timerc, lysparamWeb.timerC);
    EXTRACT_INT_PARAM(timere, lysparamWeb.timerE);
    EXTRACT_INT_PARAM(klokkentimer, lysparamWeb.slutKlokkeTimer);
    EXTRACT_INT_PARAM(klokkenminutter, lysparamWeb.slutKlokkeMinutter);
    String mode;
    EXTRACT_STR_PARAM(modeselect, mode);
    int spacePos = mode.indexOf(' ');
    if (spacePos > 0) {
        mode = mode.substring(0, spacePos);
    }
    mode.trim();
    Serial.println("Extracted mode: " + String(mode));
    if (mode == "Tid" || mode == "Klokken") {
        lysparamWeb.styringsvalg = mode;
        Serial.println("Mode valgt " + String(mode));
    }
    mutex_enter_blocking(&param_mutex);
    lysparam = lysparamWeb;
    mutex_exit(&param_mutex);
    mitjason->saveDefault(sd,&lysparamWeb);
    client.println("HTTP/1.1 303 See Other");
    client.println("Location: /index.htm");
    client.println();
}

void sendStatusJSON(WiFiClient& client) {
    JsonDocument doc;

    // Beskyt lysdata
    mutex_enter_blocking(&lys_mutex);
    doc["lys procent"] = aktuellysvaerdi;
    doc["maalt lux"] = aktuellux;
    doc["temp"] = aktueltemp;
    doc["Hpa"] = aktuelpress;
    doc["Cputemp"] = internaltemp;
    doc["lys_on"] = lys_permanet_on;
    mutex_exit(&lys_mutex);

    // Beskyt PIR/HWSW-tider
    mutex_enter_blocking(&pir_mutex);
    doc["Sidste pir 1 aktivering"] = pir1_tid ? *pir1_tid : "";
    doc["Sidste pir 2 aktivering"] = pir2_tid ? *pir2_tid : "";
    doc["Sidste Kontakt aktivering"] = hwsw_tid ? *hwsw_tid : "";
    mutex_exit(&pir_mutex);

    mutex_enter_blocking(&param_mutex);
    doc["softstep"] = lysparam.aktuelStepfrekvens;
    mutex_exit(&param_mutex);
    // Tilføj evt. flere relevante værdier her...
    String vis;
    serializeJsonPretty(doc, vis);
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type: application/json");
    client.println();
    client.println(vis);
}

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
        select { min-width: 120px; }
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
        <button type="submit" title="Opdater status">Gem</button>
    </form>
</body>
</html>
)rawliteral";

    html.replace("%LOGNATAKTIV_ON%", lysparamWeb.lognataktiv ? "selected" : "");
    html.replace("%LOGNATAKTIV_OFF%", !lysparamWeb.lognataktiv ? "selected" : "");
    html.replace("%LOGPIRDETECTION_ON%", lysparamWeb.logpirdetection ? "selected" : "");
    html.replace("%LOGPIRDETECTION_OFF%", !lysparamWeb.logpirdetection ? "selected" : "");

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Connection: close");
    client.println();
    client.print(html);
}

void handleGemLogConfig(WiFiClient& client, const String& req) {
    // Eksempel: GET /gemlogconfig.htm?lognataktiv=1&logpirdetection=0
    int idx1 = req.indexOf("lognataktiv=");
    int idx2 = req.indexOf("logpirdetection=");

    bool lognataktiv = false;
    bool logpirdetection = false;

    if (idx1 >= 0) {
        char val = req.charAt(idx1 + strlen("lognataktiv="));
        lognataktiv = (val == '1');
    }
    if (idx2 >= 0) {
        char val = req.charAt(idx2 + strlen("logpirdetection="));
        logpirdetection = (val == '1');
    }

    // Gem i din config
    mutex_enter_blocking(&param_mutex);
    lysparam.lognataktiv = lognataktiv;
    lysparam.logpirdetection = logpirdetection;
    lysparamWeb = lysparam;
    mutex_exit(&param_mutex);
    mitjason->saveDefault(sd,&lysparamWeb);

    // Gem evt til SD, fx: mitjason->saveDefault(sd, &lysparamWeb);

    // Redirect tilbage til logconfig.htm
    client.println("HTTP/1.1 303 See Other");
    client.println("Location: /index.htm");
    client.println();
}

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
    .action { cursor: pointer; color: #d00; }
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
<script>
let currentPath = "/";

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

    // Directory listing handler


  // Directory listing handler
void handleDirList(WiFiClient& client, const String& req) {
    // Find kun den første linje (GET ...)

    int lineEnd = req.indexOf("\r\n");
    String requestLine = (lineEnd > 0) ? req.substring(0, lineEnd) : req;
    String path = extractPathFromRequestLine(requestLine);
    
    Serial.print("DIRLIST-requested path: '");
    Serial.println(path);

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
        if (!entry.isDir()) {
            json += ",\"size\":" + String((unsigned long)entry.fileSize());
        }
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

    // Delete handler
void handleDelete(WiFiClient& client, const String& req) {
    int lineEnd = req.indexOf("\r\n");
    String requestLine = (lineEnd > 0) ? req.substring(0, lineEnd) : req;
    String path = extractPathFromRequestLine(requestLine);

    Serial.print("DELETE path: ");
    Serial.println(path);

    bool ok = sd.remove(path.c_str());
    client.println("HTTP/1.1 200 OK");
    client.println();
    client.println(ok ? "OK" : "FEJL");
}

   // Download handler
void handleDownload(WiFiClient& client, const String& req) {
    // Tag kun GET-linjen

    // Find path-param
    int lineEnd = req.indexOf("\r\n");
    String requestLine = (lineEnd > 0) ? req.substring(0, lineEnd) : req;
    String path = extractPathFromRequestLine(requestLine);

    Serial.print("DOWNLOAD-requested path: '");
    Serial.println(path);

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
    while ((n = file.read(buf, sizeof(buf))) > 0) {
        client.write(buf, n);
    }
    file.close();
}
    // --- Multipart/form-data upload handler ---
// ... resten af klassen ...

// Multipart/form-data upload handler – buffer-baseret og robust
// Multipart/form-data upload handler – robust, flere filer, auto-mkdir, logging
    void handleUpload(WiFiClient& client, const String& req) {
        Serial.println("Upload valgt");

        // 1. Find boundary fra headers (req)
        int boundaryPos = req.indexOf("boundary=");
        if (boundaryPos < 0) {
            client.println("HTTP/1.1 400 Bad Request\r\n\r\nMissing boundary");
            Serial.println("Missing boundary!");
            return;
        }
        String boundary = "--" + req.substring(boundaryPos + 9, req.indexOf('\r', boundaryPos));
        String boundaryFinal = boundary + "--";
        Serial.print("Boundary: "); Serial.println(boundary);

        // 2. Find Content-Length
        int clPos = req.indexOf("Content-Length: ");
        int contentLen = -1;
        if (clPos >= 0) {
            int clEnd = req.indexOf('\r', clPos);
            contentLen = req.substring(clPos + 16, clEnd).toInt();
        }
        Serial.print("Content-Length: "); Serial.println(contentLen);

        // 3. Find body-start (efter første \r\n\r\n)
        int bodyStart = req.indexOf("\r\n\r\n");
        if (bodyStart < 0) {
            client.println("HTTP/1.1 400 Bad Request\r\n\r\nNo body");
            Serial.println("Ingen body i req!");
            return;
        }
        bodyStart += 4;
        size_t alreadyRead = req.length() - bodyStart;

        // 4. Opsæt buffer og parsing-state
        static const size_t BUFSIZE = 2048;
        std::vector<uint8_t> buffer;
        buffer.reserve(BUFSIZE * 2);
        size_t bufPos = 0;

        // Læs evt. allerede læst body fra req-buffer
        if (alreadyRead > 0) {
            buffer.insert(buffer.end(), req.c_str() + bodyStart, req.c_str() + req.length());
            bufPos += alreadyRead;
        }

        size_t totalBytesRead = alreadyRead;
        size_t totalBytesToRead = (contentLen > 0) ? contentLen : 0x7FFFFFFF;

        enum ParseState { WAIT_BOUNDARY, READ_HEADERS, READ_DATA };
        ParseState state = WAIT_BOUNDARY;

        String curFilename, curPath = "/";
        String lastUploadPath = "/";
        FsFile curFile;
        size_t fileBytesWritten = 0;
        bool isPathField = false;
        String formFieldName;

        // Helper: Opret alle nødvendige undermapper til en given sti
        auto ensureDirs = [](const String& fullpath) {
            int lastSlash = fullpath.lastIndexOf('/');
            if (lastSlash <= 0) return;
            String dir = fullpath.substring(0, lastSlash);
            String build = "";
            for (size_t i = 1; i < dir.length(); ++i) {
                if (dir[i] == '/' || i == dir.length() - 1) {
                    size_t end = (i == dir.length() - 1) ? i + 1 : i;
                    build = dir.substring(0, end);
                    if (!sd.exists(build.c_str())) {
                        if (!sd.mkdir(build.c_str())) {
                            Serial.print("Kunne ikke oprette mappe: "); Serial.println(build);
                        } else {
                            Serial.print("Oprettet mappe: "); Serial.println(build);
                        }
                    }
                }
            }
        };

        // Hjælpefunktion: Find boundary i buffer (robust, også hvis splittet over flere reads)
        auto findBoundary = [&](const std::vector<uint8_t>& buf, const String& b, size_t start=0) -> int {
            if (b.length() == 0 || buf.size() < b.length()) return -1;
            for (size_t i = start; i <= buf.size() - b.length(); ++i) {
                bool match = true;
                for (size_t j = 0; j < b.length(); ++j) {
                    if (buf[i + j] != (uint8_t)b[j]) { match = false; break; }
                }
                if (match) return i;
            }
            return -1;
        };

        // Hjælpefunktion: Find linje (\r\n) i buffer
        auto findCRLF = [](const std::vector<uint8_t>& buf, size_t start) -> int {
            for (size_t i = start; i + 1 < buf.size(); ++i) {
                if (buf[i] == 13 && buf[i+1] == 10) return i;
            }
            return -1;
        };

        bool done = false, anyFile = false;

        while (!done && totalBytesRead < totalBytesToRead) {
            // Fyld buffer op hvis der mangler
            if (buffer.size() < BUFSIZE && totalBytesRead < totalBytesToRead) {
                uint8_t tmp[BUFSIZE];
                int n = client.read(tmp, BUFSIZE);
                if (n > 0) {
                    buffer.insert(buffer.end(), tmp, tmp + n);
                    totalBytesRead += n;
                } else {
                    delay(1);
                }
            }

            switch (state) {
                case WAIT_BOUNDARY: {
                    int bstart = findBoundary(buffer, boundary, 0);
                    if (bstart < 0) {
                        // Trunk boundary? Hold max boundary.length bytes i buffer
                        if (buffer.size() > boundary.length())
                            buffer.erase(buffer.begin(), buffer.end() - boundary.length());
                        break;
                    }
                    bufPos = bstart + boundary.length();
                    // Fjern evt. \r\n eller -- (final boundary)
                    if (bufPos + 2 <= buffer.size() && buffer[bufPos] == 13 && buffer[bufPos+1] == 10) bufPos += 2;
                    if (bufPos + 2 <= buffer.size() && buffer[bufPos] == '-' && buffer[bufPos+1] == '-') {
                        done = true; // Final boundary
                        break;
                    }
                    state = READ_HEADERS;
                    buffer.erase(buffer.begin(), buffer.begin() + bufPos);
                    bufPos = 0;
                    break;
                }
                case READ_HEADERS: {
                    // Læs linjer indtil blank linje (\r\n)
                    String headers = "";
                    while (true) {
                        int crlf = findCRLF(buffer, bufPos);
                        if (crlf < 0) break; // vent på mere data
                        if (crlf == (int)bufPos) { // blank linje
                            bufPos += 2;
                            break;
                        }
                        String line((char*)&buffer[bufPos], crlf-bufPos);
                        headers += line + "\n";
                        bufPos = crlf + 2;
                    }
                    // Hvis ikke blank linje fundet, vent på mere data
                    if (bufPos == 0 || (bufPos > buffer.size())) break;

                    // Udtræk feltnavn og evt. filename
                    int namePos = headers.indexOf("name=\"");
                    int nameEnd = headers.indexOf("\"", namePos + 6);
                    formFieldName = (namePos >= 0) ? headers.substring(namePos + 6, nameEnd) : "";

                    int fnPos = headers.indexOf("filename=\"");
                    if (fnPos >= 0) {
                        int fnEnd = headers.indexOf("\"", fnPos + 10);
                        curFilename = headers.substring(fnPos + 10, fnEnd);
                        // Brug den senest modtagne path (fra path-felt), ellers root
                        String fullpath = lastUploadPath;
                        if (!fullpath.endsWith("/")) fullpath += "/";
                        fullpath += curFilename;
                        ensureDirs(fullpath);

                        curFile = sd.open(fullpath.c_str(), O_WRITE | O_CREAT | O_TRUNC);
                        if (!curFile) {
                            Serial.print("Kunne ikke åbne fil: "); Serial.println(fullpath);
                        } else {
                            Serial.print("Uploader til: "); Serial.println(fullpath);
                        }
                        fileBytesWritten = 0;
                        anyFile = true;
                        isPathField = false;
                        state = READ_DATA;
                    } else if (formFieldName == "path") {
                        // path-felt (uden filename)
                        isPathField = true;
                        curFilename = "";
                        state = READ_DATA;
                    } else {
                        // Andet felt, spring til næste boundary
                        state = WAIT_BOUNDARY;
                    }
                    buffer.erase(buffer.begin(), buffer.begin() + bufPos);
                    bufPos = 0;
                    break;
                }
                case READ_DATA: {
                    int boundaryIdx = findBoundary(buffer, boundary, bufPos);
                    if (boundaryIdx < 0) {
                        // Skriv alt undtagen sidste boundary.length bytes
                        size_t toWrite = (buffer.size() > boundary.length()) ? buffer.size() - boundary.length() : 0;
                        if (!isPathField && curFile && toWrite > 0) {
                            curFile.write(&buffer[bufPos], toWrite);
                            fileBytesWritten += toWrite;
                            buffer.erase(buffer.begin(), buffer.begin() + toWrite);
                        }
                        break;
                    } else {
                        if (isPathField) {
                            // Udtræk path - alt op til boundary (minus evt. foranstillede \r\n)
                            size_t len = boundaryIdx;
                            // Fjern evt. \r\n før boundary
                            if (len >= 2 && buffer[boundaryIdx-2]==13 && buffer[boundaryIdx-1]==10) len -= 2;
                            String pathStr((char*)&buffer[bufPos], len);
                            pathStr.trim();
                            if (!pathStr.startsWith("/")) pathStr = "/" + pathStr;
                            lastUploadPath = pathStr;
                            Serial.print("Sæt upload-sti til: "); Serial.println(pathStr);
                        } else if (curFile) {
                            // Skriv fil-data op til boundary (minus evt. foranstillede \r\n)
                            size_t toWrite = boundaryIdx;
                            if (toWrite >= 2 && buffer[boundaryIdx-2]==13 && buffer[boundaryIdx-1]==10) toWrite -= 2;
                            if (toWrite > 0) {
                                curFile.write(&buffer[bufPos], toWrite);
                                fileBytesWritten += toWrite;
                            }
                            Serial.print("Fil afsluttet, bytes skrevet: "); Serial.println(fileBytesWritten);
                            curFile.close();
                        }
                        curFile = FsFile();
                        isPathField = false;
                        state = WAIT_BOUNDARY;
                        // Fjern alt op til boundary (inkl. boundary)
                        buffer.erase(buffer.begin(), buffer.begin() + boundaryIdx + boundary.length());
                        bufPos = 0;
                    }
                    break;
                }
            }
        } // while

        // Luk evt. åben fil
        if (curFile && curFile.isOpen()) curFile.close();

        // Svar til client
        if (anyFile) {
            client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nUpload OK");
            Serial.println("Upload færdig");
        } else {
            client.println("HTTP/1.1 400 Bad Request\r\n\r\nNo files uploaded");
            Serial.println("Ingen filer fundet i upload");
        }
    }

 };

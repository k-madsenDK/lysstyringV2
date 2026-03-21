/**
 * @file lysstyringV2.ino
 * @brief Lysstyring til hus – Raspberry Pi Pico W (RP2040) + Earl Philhower Arduino core.
 *
 * Dual-core arkitektur:
 *   Core0: WiFi, NTP/RTC, SD-kort, webserver, filbrowser og FIFO-log-consumer.
 *   Core1: Sensorlæsning (VEML7700 lux, BMP280 tryk/temp), PIR/HW-switch,
 *          lys-automatik (Tid/Klokken/Astro), dimmerstyring og watchdog.
 *
 * Kræver tilføjelse i platform.txt ved hver core-opdatering:
 *   compiler.cpp.extra_flags=-DPICO_CORE0_STACK_ADDR=0x2003A000 -DPICO_CORE1_STACK_ADDR=0x20042000
 */
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "pico/stdlib.h"
#include "hardware/rtc.h"
#include <SdFat.h>
#include "pico/mutex.h"
#include <queue>
#include <ctime>
#include <cstring>

#include "WebServerHandler.h"
#include "LysAutomatik.h"
#include "mitjason.h"
#include "LysParam.h"
#include "SimpleHardwareTimer.h"
#include "lyslog.h"
#include <Ticker.h>

// -------------------- SD-kort pins (SPI) --------------------
#define SD_MISO 16
#define SD_CS   17
#define SD_SCK  18
#define SD_MOSI 19

// -------------------- Hardware konstanter --------------------
#define dimmerstart      14000   // PWM nedre grænse (16-bit)
#define dimmermax        48000   // PWM øvre grænse (16-bit)
#define dimmerrelayben   2       // GPIO til relæ
#define dimmerpwmben     0       // GPIO til AC-dimmer PWM
#define softlysstartstop 20      // Step frekvens default (bruges ikke – step via LysParam)
#define pir1def          14      // GPIO til PIR sensor 1
#define pir2def          15      // GPIO til PIR sensor 2
#define hwswdef          13      // GPIO til hardware switch (kontakt)
#define ntpupdatetimer   10000   // Interval for periodisk NTP-sync (ms)

// -------------------- Mutex (delt mellem core0 og core1) --------------------
mutex_t lys_mutex;     // Beskytter dimmer-værdier
mutex_t nat_mutex;     // Beskytter nataktiv-status
mutex_t param_mutex;   // Beskytter LysParam konfiguration
mutex_t pir_mutex;     // Beskytter PIR-tidsstempler
mutex_t epoch_mutex;   // Beskytter NTP-epoch deling

// -------------------- System / state --------------------
#define systemNavn "lyskontrol"
String hostname = systemNavn;

bool core1_separate_stack = true;

bool swaktiv = false;          // Software-on flag (sat fra web)
int  last_lysprocent = 0;      // Aktuel lysprocent (læses fra dimmer)
bool kopinatstatus = false;    // Kopi af nataktiv for webvisning

LysParam lysparam;

// -------------------- NTP / WiFi --------------------
WiFiUDP ntpUDP;
// Offset 0 – vi bruger TZ + localtime() til dansk tid (CET/CEST)
NTPClient timeClient(ntpUDP, "dk.pool.ntp.org", 0, 60000);
WiFiServer server(80);

volatile uint32_t ntpEpochCopy = 0;   // UTC epoch delt med core1
unsigned long lastPeriodicNtpMs = 0;

// -------------------- Konfiguration / Web / Log --------------------
MitJsonWiFi* mitjason = new MitJsonWiFi;

// Runtime data (produceres på core1, læses på core0 via mutex)
float last_lux = 0.0f;
float last_temp = 0.0f;
float last_pressure = 0.0f;
float internaltemp = 0.0f;
bool  hwaktiv = false;
bool  updatelysprocent = false;
int   nyupdatevaerdi = 0;
bool  tvungeton = false;
String sidste1pirtid = "";
String sidste2pirtid = "";
String sidstehwswtid = "";

// Automatik (oprettes på core1 i setup1)
LysAutomatik* automatik = nullptr;

WebServerHandler* webHandler = new WebServerHandler(
    last_lysprocent,
    internaltemp,
    tvungeton,
    hwaktiv,
    last_lux,
    last_temp,
    last_pressure,
    swaktiv,
    updatelysprocent,
    nyupdatevaerdi,
    kopinatstatus,
    &sidste1pirtid,
    &sidste2pirtid,
    &sidstehwswtid
);

SimpleHardwareTimer* fifoTimer = new SimpleHardwareTimer;
volatile bool queueDirty = false;
std::queue<uint32_t> logQueue;

SdFat sd;
bool ntpsat = false;

LysLog* lyslog = nullptr;

// -------------------- FIFO callback (læs events fra core1) --------------------
void fifoTimerCallback() {
    while (rp2040.fifo.available()) {
        uint32_t code;
        rp2040.fifo.pop_nb(&code);
        logQueue.push(code);
        queueDirty = true;
    }
}

// -------------------- NTP init + første sync --------------------
static bool setupWiFiAndNTP() {
    Serial.println("Forbinder til WiFi...");
    WiFi.setHostname("lyscontrol");
    WiFi.begin(mitjason->ssid, mitjason->password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nKunne ikke forbinde til WiFi!");
        return false;
    }
    Serial.println("\nTilsluttet!");
    Serial.print("IP adresse: ");
    Serial.println(WiFi.localIP());

    Serial.println("Starter NTP-klient...");
    timeClient.begin();

    int ntpTries = 0;
    bool ntpOk = false;
    while (ntpTries < 20) {
        if (timeClient.update()) {
            ntpOk = true;
            break;
        }
        Serial.println("Venter på NTP sync...");
        delay(1000);
        ntpTries++;
    }
    if (!ntpOk) {
        Serial.println("Kunne ikke hente tid fra NTP!");
        return false;
    }

    // Konverter UTC epoch til lokal tid via TZ og sæt RTC
    time_t rawTime = (time_t)timeClient.getEpochTime();

    tm timeinfo;
    if (!localtime_r(&rawTime, &timeinfo)) {
        Serial.println("localtime_r fejlede!");
        return false;
    }

    datetime_t t = {
        (int16_t)(timeinfo.tm_year + 1900),
        (int8_t)(timeinfo.tm_mon + 1),
        (int8_t)timeinfo.tm_mday,
        (int8_t)timeinfo.tm_wday,
        (int8_t)timeinfo.tm_hour,
        (int8_t)timeinfo.tm_min,
        (int8_t)timeinfo.tm_sec
    };
    rtc_init();
    rtc_set_datetime(&t);

    // Del UTC-epoch med core1
    mutex_enter_blocking(&epoch_mutex);
    ntpEpochCopy = (uint32_t)rawTime;
    mutex_exit(&epoch_mutex);

    Serial.println("RTC sat ud fra NTP (lokal tid via TZ)!");
    return true;
}

/** Periodisk NTP-opdatering. Kaldes fra loop() hvert ntpupdatetimer ms. */
static void periodicNtpUpdate() {
    if (WiFi.status() != WL_CONNECTED) return;
    unsigned long now = millis();
    if (now - lastPeriodicNtpMs < ntpupdatetimer) return;
    lastPeriodicNtpMs = now;

    if (timeClient.update()) {
        time_t rawTime = (time_t)timeClient.getEpochTime();

        tm timeinfo;
        if (localtime_r(&rawTime, &timeinfo)) {
            datetime_t t = {
                (int16_t)(timeinfo.tm_year + 1900),
                (int8_t)(timeinfo.tm_mon + 1),
                (int8_t)timeinfo.tm_mday,
                (int8_t)timeinfo.tm_wday,
                (int8_t)timeinfo.tm_hour,
                (int8_t)timeinfo.tm_min,
                (int8_t)timeinfo.tm_sec
            };
            rtc_set_datetime(&t);
        }

        mutex_enter_blocking(&epoch_mutex);
        ntpEpochCopy = (uint32_t)rawTime;
        mutex_exit(&epoch_mutex);
    }
}

/** Logger astro-data for i dag til hardware.log. Kaldes fra core0 via FIFO-event. */
static void logAstroLineForToday() {
    if (!lyslog) return;

    float lat, lon;
    int offSet, offRise;
    String mode;
    bool astroEn;

    mutex_enter_blocking(&param_mutex);
    lat = lysparam.astroLat;
    lon = lysparam.astroLon;
    offSet = lysparam.astroSunsetOffsetMin;
    offRise = lysparam.astroSunriseOffsetMin;
    mode = lysparam.styringsvalg;
    astroEn = lysparam.astroEnabled;
    mutex_exit(&param_mutex);

    datetime_t rt;
    rtc_get_datetime(&rt);

    AstroTimes at = AstroSun::computeLocalTimes(rt.year, rt.month, rt.day, lat, lon);

    auto hhmm = [](int minutes) -> String {
        if (minutes < 0) return "--:--";
        int h = (minutes / 60) % 24;
        int mi = minutes % 60;
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", h, mi);
        return String(buf);
    };
    auto wrapMin = [](int mm) -> int {
        while (mm < 0) mm += 1440;
        while (mm >= 1440) mm -= 1440;
        return mm;
    };

    int sunriseAdj = (at.sunriseMin >= 0) ? wrapMin(at.sunriseMin + offRise) : -1;
    int sunsetAdj  = (at.sunsetMin  >= 0) ? wrapMin(at.sunsetMin  + offSet)  : -1;

    char datebuf[16];
    snprintf(datebuf, sizeof(datebuf), "%04d-%02d-%02d", rt.year, rt.month, rt.day);

    String line = "ASTRO day=" + String(datebuf);
    line += " mode=" + mode;
    line += " enabled=" + String(astroEn ? "1" : "0");
    line += " lat=" + String(lat, 4) + " lon=" + String(lon, 4);
    line += " rise=" + hhmm(at.sunriseMin) + " set=" + hhmm(at.sunsetMin);
    line += " offRise=" + String(offRise) + " offSet=" + String(offSet);
    line += " riseAdj=" + hhmm(sunriseAdj) + " setAdj=" + hhmm(sunsetAdj);

    lyslog->logHardware(line);
}

/** Behandler FIFO-logkøen. Kaldes fra loop() når queueDirty er sat. */
static void checkforlog() {
    queueDirty = false;

    while (!logQueue.empty()) {
        uint32_t code = logQueue.front();
        logQueue.pop();

        switch (code) {
            case nataktivfalse:    if (lyslog) lyslog->logNatAktiv(false); break;
            case nataktivtrue:     if (lyslog) lyslog->logNatAktiv(true);  break;
            case pir1_detection:   if (lyslog) lyslog->logPIR("pir 1"); break;
            case pir2_detection:   if (lyslog) lyslog->logPIR("pir 2"); break;
            case hwsw_on:          if (lyslog) lyslog->logPIR("Kontakt on"); break;
            case swsw_on:          if (lyslog) lyslog->logPIR("Software on"); break;
            case hwsw_off:         if (lyslog) lyslog->logPIR("Kontakt off"); break;
            case swsw_off:         if (lyslog) lyslog->logPIR("Software off"); break;
            case wdt_reset:        if (lyslog) lyslog->logWatchdogReset(); break;
            case i2c_reset_wire:   if (lyslog) lyslog->logI2CReset("Wire"); break;
            case i2c_reset_wire1:  if (lyslog) lyslog->logI2CReset("Wire1"); break;
            case astro_log_request: logAstroLineForToday(); break;
            default: break;
        }
    }
}

/** Genopret WiFi-forbindelse ved tab. */
static int connectwifi(void) {
    WiFi.disconnect();
    WiFi.setHostname("lyscontrol");
    return WiFi.begin(mitjason->ssid, mitjason->password);
}

// ==================== Core0 Setup ====================
void setup() {
    Serial.begin(115200);

    mutex_init(&lys_mutex);
    mutex_init(&nat_mutex);
    mutex_init(&param_mutex);
    mutex_init(&pir_mutex);
    mutex_init(&epoch_mutex);

    delay(1200);

    // Dansk tidszone: CET (UTC+1) og CEST (UTC+2) med EU-regler
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();

    // SD-kort init via SPI
    SPI.setRX(SD_MISO);
    SPI.setCS(SD_CS);
    SPI.setSCK(SD_SCK);
    SPI.setTX(SD_MOSI);
    SPI.begin();

    if (!sd.begin(SdSpiConfig(SD_CS, DEDICATED_SPI, SD_SCK_MHZ(20)))) {
        Serial.println("SD init failed!");
    } else {
        Serial.println("SD init OK!");
    }

    lyslog = new LysLog(sd, lysparam.lognataktiv, lysparam.logpirdetection);

    // Indlæs konfiguration fra SD-kort
    mitjason->loadWiFi(sd, "/wifi.json");
    mitjason->loadDefault(sd, &lysparam);

    if (!setupWiFiAndNTP()) {
        if (lyslog) lyslog->logBootReboot("WIFI_NOT_FOUND");
        Serial.println("Netværks- eller NTP-fejl! Rebooter om 5 sek.");
        delay(5000);
        rp2040.reboot();
    }

    datetime_t t;
    rtc_get_datetime(&t);
    Serial.printf("Tid (RTC lokal): %04d-%02d-%02d %02d:%02d:%02d\n",
                  t.year, t.month, t.day, t.hour, t.min, t.sec);

    server.begin();

    // Vent på core1 er klar
    rp2040.idleOtherCore();
    ntpsat = true;
    rp2040.resumeOtherCore();
}

// ==================== Core0 Loop ====================
void loop() {
    // WiFi reconnect
    if (WiFi.status() != WL_CONNECTED) {
        int status = connectwifi();
        if (status == WL_CONNECTED) {
            Serial.println("[WiFi] Reconnected");
            if (lyslog) lyslog->logWiFiReconnect(WiFi.localIP().toString());
        }
    }

    periodicNtpUpdate();

    // Webserver
    WiFiClient client = server.available();
    if (client) {
        String req = "";
        unsigned long timeout = millis() + 2000;
        while (client.connected() && millis() < timeout) {
            if (client.available()) {
                char c = client.read();
                req += c;
                if (req.endsWith("\r\n\r\n")) break;
            }
        }
        if (req.length() > 0) {
            webHandler->handle(client, req);
        }
        client.stop();
    }

    // Behandl FIFO-events fra core1
    fifoTimerCallback();
    if (queueDirty) checkforlog();

    delay(1);
}

// ===================== Core1 (sensorer / automatik) =====================
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include "VEML7700_PIO.h"
#include "Dimmerfunktion.h"
#include "pirroutiner.h"
#include "SimpleSoftwareTimer.h"
#include "I2CBusRecover.h"
#include "hardware/watchdog.h"

// Sensorer
VEML7700_PIO* veml = new VEML7700_PIO();
Adafruit_BMP280* bmp = new Adafruit_BMP280(&Wire1);

bool timer_tik = false;
bool WEML7700_tilstede = false;
bool BMP280_tilstede = false;

// I2C fejltællere
volatile uint32_t i2cWireResets = 0;
volatile uint32_t i2cWire1Resets = 0;
static uint8_t bhNoVal = 0;
static uint8_t bmpBad = 0;

// Dimmer og PIR
dimmerfunktion* dimmer = new dimmerfunktion(dimmerrelayben, dimmerpwmben, dimmerstart, dimmermax);
pirroutiner* pirrou = nullptr;

// Software timere til core1
SimpleSoftwareTimer myTimer;
SimpleSoftwareTimer softlysTimer;

/** 1 Hz heartbeat – toggler LED og sætter timer_tik flag. */
void blink() {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    timer_tik = true;
}

/** 250 ms rutine – softstart/softsluk step og PIR-debounce. */
void softlysIrq() {
    if (dimmer->softstartAktiv()) dimmer->softstartStep();
    if (dimmer->softslukAktiv())  dimmer->softslukStep();
    if (pirrou) pirrou->timerRoutine();
}

// Astro-log: én request per dag via FIFO til core0
static int lastAstroReqY = -1, lastAstroReqM = -1, lastAstroReqD = -1;

/** Sender astro_log_request via FIFO én gang pr. dag (kaldet fra core1). */
static void requestAstroLogOncePerDay(time_t utcEpoch) {
    if (utcEpoch < 1700000000) return;

    time_t t = utcEpoch;
    tm ti;
    if (!localtime_r(&t, &ti)) return;

    int y = ti.tm_year + 1900;
    int m = ti.tm_mon + 1;
    int d = ti.tm_mday;

    if (y == lastAstroReqY && m == lastAstroReqM && d == lastAstroReqD) return;
    lastAstroReqY = y; lastAstroReqM = m; lastAstroReqD = d;

    rp2040.fifo.push_nb(astro_log_request);
}

/**
 * @brief Initialisér VEML7700 på Wire (I2C0, GPIO 4+5, 100 kHz).
 *        Kører bus recovery + scan før init.
 * @return true hvis VEML7700 fundet og konfigureret.
 */
bool setwire0() {
    I2CBusRecover::recover(5, 4);   // SCL=5, SDA=4

    Wire.setSDA(4);
    Wire.setSCL(5);
    Wire.begin();
    Wire.setClock(100000);
    Wire.setTimeout(200);

    Serial.println("Tester VEML7700 lyssensor (Wire)...");
    I2CBusRecover::scanTwoWire(Wire);

    if (veml->begin(&Wire)) {
        veml->setGain(VEML7700_PIO::GAIN_1);
        veml->setIntegrationTime(VEML7700_PIO::IT_100MS);
        Serial.println("VEML7700 fundet på Wire! (0x10)");
        return true;
    }
    Serial.println("VEML7700 ikke fundet!");
    return false;
}

/**
 * @brief Initialisér BMP280 på Wire1 (I2C1, GPIO 10+11, 100 kHz).
 *        Kører bus recovery før init.
 * @return true hvis BMP280 fundet.
 */
bool setwire1() {
    I2CBusRecover::recover(10, 11);

    Wire1.setSDA(10);
    Wire1.setSCL(11);
    Wire1.begin();
    Wire1.setClock(100000);
    Wire1.setTimeout(100);

    return bmp->begin(0x76);
}

// ==================== Core1 Setup ====================
void setup1() {
    while (!ntpsat) delay(100);

    if (watchdog_caused_reboot()) {
        rp2040.fifo.push_nb(wdt_reset);
    }

    WEML7700_tilstede = setwire0();

    pinMode(LED_BUILTIN, OUTPUT);

    automatik = new LysAutomatik(lysparam, dimmer);
    pirrou = new pirroutiner(pir1def, pir2def, hwswdef, &sidste1pirtid, &sidste2pirtid, &sidstehwswtid, lysparam);

    myTimer.setInterval(1000, blink);
    softlysTimer.setInterval(250, softlysIrq);

    BMP280_tilstede = setwire1();

    Serial.println("Tester BMP280 sensor...");
    if (!BMP280_tilstede) {
        Serial.println("BMP280 ikke fundet på I2C-bussen!");
    } else {
        Serial.print("Temperatur: "); Serial.print(bmp->readTemperature()); Serial.println(" *C");
        Serial.print("Lufttryk: ");  Serial.print(bmp->readPressure() / 100.0F); Serial.println(" hPa");
    }

    watchdog_enable(3000, 1);
}

// ==================== Core1 Loop ====================
static uint32_t paramSkipCount = 0;
static bool automatikInitDone = false;

void loop1() {
    watchdog_update();
    myTimer.run();
    softlysTimer.run();

    if (timer_tik) {
        timer_tik = false;

        // Hent NTP epoch (UTC) fra core0
        uint32_t ntpLocal;
        mutex_enter_blocking(&epoch_mutex);
        ntpLocal = ntpEpochCopy;
        mutex_exit(&epoch_mutex);

        requestAstroLogOncePerDay((time_t)ntpLocal);

        // Opdater nataktiv-kopi til web
        mutex_enter_blocking(&nat_mutex);
        kopinatstatus = automatik ? automatik->getNataktiv() : false;
        mutex_exit(&nat_mutex);

        // ---- VEML7700 læsning (Wire/I2C0) ----
        if (WEML7700_tilstede) {
            watchdog_update();
            float ny_lux = veml->readLux();
            if (isfinite(ny_lux) && ny_lux >= 0.0f && ny_lux <= 120000.0f) {
                last_lux = ny_lux;
                bhNoVal = 0;
            } else {
                if (++bhNoVal >= 8) {
                    Serial.println("[VEML7700] Læsefejl – Wire reset");
                    Wire.end();
                    setwire0();
                    bhNoVal = 0;
                    i2cWireResets++;
                    rp2040.fifo.push_nb(i2c_reset_wire);
                }
            }
        }

        // ---- Boot init (kør én gang når NTP-tid er realistisk) ----
        if (!automatikInitDone && automatik && ntpLocal >= 1700000000UL) {
            float bootLux = last_lux;

            // Brug frisk VEML7700-værdi hvis tilgængelig
            if (WEML7700_tilstede) {
                float v = veml->readLux();
                if (isfinite(v) && v >= 0.0f && v <= 20000.0f) bootLux = v;
                bhNoVal = 0;
            }

            automatik->initFromNow(bootLux, (time_t)ntpLocal);
            automatikInitDone = true;

            mutex_enter_blocking(&nat_mutex);
            kopinatstatus = automatik->getNataktiv();
            mutex_exit(&nat_mutex);
        }

        // ---- BMP280 læsning (Wire1/I2C1) ----
        if (BMP280_tilstede) {
            float t = bmp->readTemperature();
            float p = bmp->readPressure() / 100.0F;
            bool bad = isnan(t) || p < 300.0f || p > 1100.0f;
            if (bad) {
                if (++bmpBad >= 3) {
                    Serial.println("[BMP280] Dårlige læsninger – I2C recover (Wire1)");
                    Wire1.end();
                    setwire1();
                    bmpBad = 0;
                    i2cWire1Resets++;
                    rp2040.fifo.push_nb(i2c_reset_wire1);
                }
            } else {
                last_temp = t;
                last_pressure = p;
                bmpBad = 0;
            }
        }

        // ---- Tvungen on/off (hardware switch / software on) ----
        tvungeton = false;
        if (pirrou && pirrou->isHWSWBenLow()) hwaktiv = true;
        else if (hwaktiv) { hwaktiv = false; if (pirrou) pirrou->logHWSWOff(); }
        if (hwaktiv) tvungeton = true;
        if (swaktiv) tvungeton = true;

        static bool hwaktivlocal = false;

        if (!tvungeton) {
            // Sluk tvungen tilstand, returner til automatik
            if (hwaktivlocal && automatik) {
                hwaktivlocal = false;
                automatik->forceOff();
            }

            bool pirstatus = false;
            if (pirrou) {
                if (pirrou->isPIR1Activated()) pirstatus = true;
                if (pirrou->isPIR2Activated()) pirstatus = true;
            }

            // Opdater automatik (kun hvis mutex er ledig – undgå deadlock)
            uint32_t owner = 0;
            if (mutex_try_enter(&param_mutex, &owner)) {
                if (automatik) automatik->update(last_lux, pirstatus, (time_t)ntpLocal);
                mutex_exit(&param_mutex);
            } else {
                paramSkipCount++;
            }
        } else {
            // Tvungen on: tænd lys 100%
            if (!hwaktivlocal) {
                dimmer->taend();
                hwaktivlocal = true;
            }
        }

        internaltemp = analogReadTemp();
    }

    // Opdater lysprocent hvis core0 har sat flag (fra web-slider)
    mutex_enter_blocking(&lys_mutex);
    if (updatelysprocent) {
        updatelysprocent = false;
        dimmer->setlysiprocentSoft(nyupdatevaerdi);
    }
    last_lysprocent = dimmer->returneraktuelvaerdi();
    mutex_exit(&lys_mutex);

    delay(5);
}

/*
  HUSK at ændre i .arduino15/packages/rp2040/hardware/rp2040/x.x.x/platform.txt
  indsæt
    compiler.cpp.extra_flags=-DPICO_CORE0_STACK_ADDR=0x20041000 -DPICO_CORE1_STACK_ADDR=0x20042000
  dette skal gøres ved hver platform opdatering
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "pico/stdlib.h"
#include "hardware/rtc.h"
#include <SdFat.h>
#include "WebServerHandler.h"
#include "pico/mutex.h"
#include "LysAutomatik.h"
#include "mitjason.h"
#include "LysParam.h"
#include "SimpleHardwareTimer.h"
#include <queue>
#include "lyslog.h"
#include <ArduinoJson.h>   // behøves til JSON i WebServerHandler
#include <time.h>
#include <stdlib.h>

// -------------------- SD-kort pins --------------------
#define SD_MISO 16
#define SD_CS   17
#define SD_SCK  18
#define SD_MOSI 19

// -------------------- Hardware konstanter --------------------
#define dimmerstart 14000
#define dimmermax   48000
#define dimmerrelayben 2
#define dimmerpwmben 0
#define softlysstartstop 20
#define pir1def 14
#define pir2def 15
#define hwswdef 13
#define ntpupdatetimer 10000 // ms mellem ntp check

// -------------------- Mutex --------------------
mutex_t lys_mutex;
mutex_t nat_mutex;
mutex_t param_mutex;
mutex_t pir_mutex;
mutex_t epoch_mutex;

// -------------------- System / state --------------------
#define systemNavn "lyskontrol"
String hostname = systemNavn;

bool core1_separate_stack = true;
bool swaktiv = false;
int  last_lysprocent = 0;
bool kopinatstatus = false;
LysParam lysparam;

// -------------------- NTP / WiFi --------------------
WiFiUDP ntpUDP;
// Offset 0 – vi bruger TZ + localtime() til dansk tid (CET/CEST)
NTPClient timeClient(ntpUDP, "dk.pool.ntp.org", 0, 60000);
WiFiUDP udp;
WiFiServer server(80);

// NTP epoch kopi til core1 (UTC)
volatile uint32_t ntpEpochCopy = 0;
unsigned long lastPeriodicNtpMs = 0;
const unsigned long periodicNtpIntervalMs = 60000; // 60 sek

// -------------------- Eksterne variabler (defineres senere i filen) --------------------
extern float last_lux;
extern float last_temp;
extern float last_pressure;
extern float internaltemp;
extern bool  hwaktiv;
extern bool  updatelysprocent;
extern int   nyupdatevaerdi;
extern bool  tvungeton;
extern LysAutomatik *automatik;
extern String sidste1pirtid;
extern String sidste2pirtid;
extern String sidstehwswtid;

// -------------------- Konfiguration / Web / Log --------------------
MitJsonWiFi* mitjason = new MitJsonWiFi;

WebServerHandler *webHandler = new WebServerHandler(
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

SimpleHardwareTimer *fifoTimer = new SimpleHardwareTimer;
volatile bool queueDirty = false;
std::queue<uint32_t> logQueue;

SdFat sd;
FsFile myFile;
bool ntpsat = false;
bool BH1750_tilstede = false;
bool BMP280_tilstede = false;

// Opret log først som pointer (instansieres efter sd.begin)
LysLog *lyslog = nullptr;

// -------------------- FIFO callback --------------------
void fifoTimerCallback() {
    while (rp2040.fifo.available()) {
        uint32_t code;
        rp2040.fifo.pop_nb(&code);
        logQueue.push(code);
        queueDirty = true;
    }
}

// -------------------- NTP init + første sync --------------------
bool setupWiFiAndNTP() {
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
    Serial.print("NTP tid (formatted): ");
    Serial.println(timeClient.getFormattedTime());

    // epoch fra NTP er UTC (offset=0); konverter til lokal tid via TZ
    time_t rawTime = timeClient.getEpochTime();
    struct tm *timeinfo = localtime(&rawTime);
    datetime_t t = {
        (int16_t)(timeinfo->tm_year + 1900),
        (int8_t)(timeinfo->tm_mon + 1),
        (int8_t)timeinfo->tm_mday,
        (int8_t)timeinfo->tm_wday,
        (int8_t)timeinfo->tm_hour,
        (int8_t)timeinfo->tm_min,
        (int8_t)timeinfo->tm_sec
    };
    rtc_init();
    rtc_set_datetime(&t);

    // Gem UTC-epoch til core1
    mutex_enter_blocking(&epoch_mutex);
    ntpEpochCopy = (uint32_t)rawTime;
    mutex_exit(&epoch_mutex);

    Serial.println("RTC sat ud fra NTP (lokal tid via TZ)!");
    return true;
}

void periodicNtpUpdate() {
    if (WiFi.status() != WL_CONNECTED) return;
    unsigned long now = millis();
    if (now - lastPeriodicNtpMs < ntpupdatetimer) return;
    lastPeriodicNtpMs = now;
    if (timeClient.update()) {
        time_t rawTime = timeClient.getEpochTime();     // UTC
        struct tm *timeinfo = localtime(&rawTime);      // lokal tid m. DST
        datetime_t t = {
            (int16_t)(timeinfo->tm_year + 1900),
            (int8_t)(timeinfo->tm_mon + 1),
            (int8_t)timeinfo->tm_mday,
            (int8_t)timeinfo->tm_wday,
            (int8_t)timeinfo->tm_hour,
            (int8_t)timeinfo->tm_min,
            (int8_t)timeinfo->tm_sec
        };
        rtc_set_datetime(&t);
        mutex_enter_blocking(&epoch_mutex);
        ntpEpochCopy = (uint32_t)rawTime;               // hold UTC til beregninger
        mutex_exit(&epoch_mutex);
    }
}

// -------------------- Log af events --------------------
void checkforlog(){
    queueDirty = false;
    while (!logQueue.empty()) {
        uint32_t code = logQueue.front();
        logQueue.pop();
        switch(code){
          case nataktivfalse: if (lyslog) lyslog->logNatAktiv(false); break;
          case nataktivtrue:  if (lyslog) lyslog->logNatAktiv(true);  break;
          case pir1_detection: if (lyslog) lyslog->logPIR("pir 1");   break;
          case pir2_detection: if (lyslog) lyslog->logPIR("pir 2");   break;
          case hwsw_on:        if (lyslog) lyslog->logPIR("Kontakt on");   break;
          case swsw_on:        if (lyslog) lyslog->logPIR("Software on");  break;
          case hwsw_off:       if (lyslog) lyslog->logPIR("Kontakt off");  break;
          case swsw_off:       if (lyslog) lyslog->logPIR("Software off"); break;
          // Hardware
          case wdt_reset:        if (lyslog) lyslog->logWatchdogReset();   break;
          case i2c_reset_wire:   if (lyslog) lyslog->logI2CReset("Wire");  break;
          case i2c_reset_wire1:  if (lyslog) lyslog->logI2CReset("Wire1"); break;
          default: break;
        }
    }
}

// -------------------- WiFi reconnect --------------------
int connectwifi(void){
  WiFi.disconnect();
  WiFi.setHostname("lyscontrol");
  return WiFi.begin(mitjason->ssid, mitjason->password);
}

// -------------------- Setup (core0) --------------------
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

    // SD init
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

    // Opret log før net, så vi kan logge evt. rebootårsag
    lyslog = new LysLog(sd, lysparam.lognataktiv, lysparam.logpirdetection);

    // Load konfiguration
    mitjason->loadWiFi(sd ,"/wifi.json");
    mitjason->loadDefault(sd , &lysparam);

    if (!setupWiFiAndNTP()) {
        if (lyslog) lyslog->logBootReboot("WIFI_NOT_FOUND");
        Serial.println("Netværks- eller NTP-fejl! Rebooter om 5 sek.");
        delay(5000);
        rp2040.reboot();
    }

    // Vis tid læst fra RTC (lokal)
    datetime_t t;
    rtc_get_datetime(&t);
    Serial.printf("Tid (RTC lokal): %04d-%02d-%02d %02d:%02d:%02d\n",
                  t.year, t.month, t.day, t.hour, t.min, t.sec);

    fifoTimer->setInterval(10, fifoTimerCallback);
    server.begin();

    // Start core1
    rp2040.idleOtherCore();
    ntpsat = true;
    rp2040.resumeOtherCore();
}

// -------------------- Loop (core0) --------------------
void loop() {

    if(WiFi.status() != WL_CONNECTED){
        int status = connectwifi();
        if(status == WL_CONNECTED){
            Serial.println("[WiFi] Reconnected");
            if (lyslog) lyslog->logWiFiReconnect(WiFi.localIP().toString());
        }
    }

    periodicNtpUpdate();

    // Webserver (behold available() som aftalt)
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

    if(queueDirty) checkforlog();

    delay(1);
}

// ===================== Core1 (sensorer / automatik) =====================

#include <Wire.h>
#include <hp_BH1750.h>
#include <Adafruit_BMP280.h>
#include "Dimmerfunktion.h"
#include "pirroutiner.h"
#include "SimpleSoftwareTimer.h"
#include "I2CBusRecover.h"
#include "hardware/watchdog.h"

// Sensor / runtime variabler
hp_BH1750 *lightMeter = new hp_BH1750;
Adafruit_BMP280 *bmp = new Adafruit_BMP280(&Wire1);
bool   timer_tik  = false;
float  last_lux = 0.0;
float  last_temp = 0.0;
float  last_pressure = 0.0;
float  internaltemp = 0.0;
bool   hwaktiv = false;
bool   updatelysprocent = false;
int    nyupdatevaerdi = 0;
String sidste1pirtid = "";
String sidste2pirtid = "";
String sidstehwswtid = "";
bool   tvungeton = false;
bool   hwaktivlocal = false;

// I2C-stabilitets tællere (til debugging/overvågning)
volatile uint32_t i2cWireResets = 0;
volatile uint32_t i2cWire1Resets = 0;
static uint8_t bhNoVal = 0;     // antal sekvenser uden BH1750 værdi
static uint8_t bmpBad = 0;      // antal dårlige BMP280 målinger i træk

// Dimmer / automatik / PIR
dimmerfunktion *dimmer = new dimmerfunktion(dimmerrelayben,dimmerpwmben,dimmerstart,dimmermax);
LysAutomatik  *automatik = new LysAutomatik(lysparam, dimmer);
pirroutiner   *pirrou = new pirroutiner(pir1def, pir2def, hwswdef,
                                        &sidste1pirtid, &sidste2pirtid, &sidstehwswtid, lysparam);

// Timere
SimpleSoftwareTimer myTimer;
SimpleSoftwareTimer softlysTimer;

// Interface til core0
void setlysiprocentSoft(int procent){
    dimmer->setlysiprocentSoft(procent);
}

// 1 Hz blink (indikator)
void blink() {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    timer_tik = true;
}

// 250 ms fast funktion
void softlysIrq() {
    if (dimmer->softstartAktiv()) dimmer->softstartStep();
    if (dimmer->softslukAktiv())  dimmer->softslukStep();
    pirrou->timerRoutine();
}

// Core1 setup
void setup1() {
    while(!ntpsat){
        delay(100);
    }

    // Log hvis vi kommer fra watchdog reset
    if (watchdog_caused_reboot()) {
        rp2040.fifo.push_nb(wdt_reset);
    }

    I2CBusRecover::recover(5,4);
    I2CBusRecover::recover(10,11);
    Wire.setSDA(4);
    Wire.setSCL(5);
    Wire.begin();
    Wire.setClock(100000);     // BH1750 bus
    Wire1.setSDA(10);
    Wire1.setSCL(11);
    Wire1.begin();
    Wire1.setClock(1000000);   // BMP280 bus (test med 1 MHz som nu)

    pinMode(LED_BUILTIN, OUTPUT);

    myTimer.setInterval(1000, blink);
    softlysTimer.setInterval(250, softlysIrq);

    Serial.println("Tester BH1750 lyssensor...");
    if (lightMeter->begin(BH1750_TO_GROUND, &Wire) == true) {
        lightMeter->calibrateTiming();
        lightMeter->start(BH1750_QUALITY_HIGH2, BH1750_MTREG_DEFAULT);
        Serial.println("BH1750 fundet på I2C-bussen!");
        BH1750_tilstede = true;
    } else {
        Serial.println("BH1750 ikke fundet på I2C-bussen!");
    }

    Serial.println("Tester BMP280 sensor...");
    if (!bmp->begin(0x76)) {
        Serial.println("BMP280 ikke fundet på I2C-bussen!");
    } else {
        BMP280_tilstede = true;
        Serial.print("Temperatur: "); Serial.print(bmp->readTemperature()); Serial.println(" *C");
        Serial.print("Lufttryk: ");  Serial.print(bmp->readPressure() / 100.0F); Serial.println(" hPa");
    }

    watchdog_enable(500, 1);
}

// Core1 loop
void loop1() {
    watchdog_update();
    myTimer.run();
    softlysTimer.run();

    if(timer_tik){
        timer_tik = false;

        // Nat/dag status
        mutex_enter_blocking(&nat_mutex);
        kopinatstatus = automatik->getNataktiv();
        mutex_exit(&nat_mutex);

        // BH1750 (Wire) – stabilitet
        if(BH1750_tilstede){
            if (lightMeter->hasValue()) {
                last_lux = lightMeter->getLux();
                lightMeter->start();
                bhNoVal = 0;
            } else {
                if (++bhNoVal >= 8) { // ~2s uden nyt i HIGH2
                    Serial.println("[BH1750] Ingen værdier – I2C recover (Wire)");
                    Wire.end();
                    I2CBusRecover::recover(5,4);
                    Wire.setSDA(4); Wire.setSCL(5);
                    Wire.begin();
                    Wire.setClock(100000);
                    lightMeter->begin(BH1750_TO_GROUND, &Wire);
                    lightMeter->calibrateTiming();
                    lightMeter->start(BH1750_QUALITY_HIGH2, BH1750_MTREG_DEFAULT);
                    bhNoVal = 0;
                    i2cWireResets++;
                    rp2040.fifo.push_nb(i2c_reset_wire);
                }
            }
        }

        // BMP280 (Wire1) – stabilitet
        if(BMP280_tilstede){
            float t = bmp->readTemperature();
            float p = bmp->readPressure() / 100.0F;
            bool bad = isnan(t) || p < 300.0f || p > 1100.0f;
            if (bad) {
                if (++bmpBad >= 3) {
                    Serial.println("[BMP280] Dårlige læsninger – I2C recover (Wire1)");
                    Wire1.end();
                    I2CBusRecover::recover(10,11);
                    Wire1.setSDA(10); Wire1.setSCL(11);
                    Wire1.begin();
                    Wire1.setClock(1000000);
                    bmp->begin(0x76);
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

        tvungeton = false;
        if(pirrou->isHWSWBenLow()) hwaktiv = true;
        else if(hwaktiv) { hwaktiv = false; pirrou->logHWSWOff(); }
        if(hwaktiv) tvungeton = true;
        if(swaktiv) tvungeton = true;

        if(!tvungeton){
            if(hwaktivlocal){
                hwaktivlocal = false;
                automatik->forceOff();
            }
            bool pirstatus = false;
            if(pirrou->isPIR1Activated()) pirstatus = true;
            if(pirrou->isPIR2Activated()) pirstatus = true;

            uint32_t ntpLocal;
            mutex_enter_blocking(&epoch_mutex);
            ntpLocal = ntpEpochCopy; // UTC
            mutex_exit(&epoch_mutex);

            mutex_enter_blocking(&param_mutex);
            automatik->update(last_lux, pirstatus, (time_t)ntpLocal);
            mutex_exit(&param_mutex);
        } else {
            if(!hwaktivlocal){
                automatik->forceOn();
                hwaktivlocal = true;
            }
        }

        internaltemp = analogReadTemp();
    }

    // Opdater lys hvis core0 har sat flag
    mutex_enter_blocking(&lys_mutex);
    if(updatelysprocent){
      updatelysprocent = false;
      dimmer->setlysiprocentSoft(nyupdatevaerdi);
    }
    last_lysprocent = dimmer->returneraktuelvaerdi();
    mutex_exit(&lys_mutex);

    delay(5);
}
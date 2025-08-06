/*
  HUSK at ændre i .arduino15/packages/rp2040/hardware/rp2040/x.x.x/platform.txt
  indsætcompiler.cpp.extra_flags=-DPICO_CORE0_STACK_ADDR=0x20041000 -DPICO_CORE1_STACK_ADDR=0x20042000
  dette skal gøres ved hver platform opdatering
*/
/**
 * @file lysstyringV2.ino
 * @brief Hovedprogram til lyskontrolsystemet på Raspberry Pi Pico W.
 *
 * Opdeling:
 *   - Core0 håndterer webserver, SD, WiFi, NTP og system-setup.
 *   - Core1 håndterer sensoraflæsning, hardwarestyring, automatik og PIR.
 *
 * Delte variabler mellem cores er mutex-beskyttet.
 * SD-kort bruges til konfiguration og logning.
 */
 
#include <Arduino.h>
#include <WiFi.h>           // Til Pico W
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "pico/stdlib.h"
#include "hardware/rtc.h"
#include <SdFat.h>
#include "WebServerHandler.h"
#include "pico/mutex.h"
#include "LysAutomatik.h"
#include "mitjason.h"
#include "LysParam.h" // bruges af begge core
#include "SimpleHardwareTimer.h"
#include <queue>
#include "lyslog.h"


// --------- SD-kort opsætning ---------
#define SD_MISO 16
#define SD_CS   17
#define SD_SCK  18
#define SD_MOSI 19

// --------- Hardware konstanter ---------
#define dimmerstart 14000 //måles individuelt afhængig af pære type
#define dimmermax  48000 //som ovenfor
#define dimmerrelayben 2 //ben nummer som 220v relayet er tilsluttet
#define dimmerpwmben 0 //ben som bruges som pwmben
#define softlysstartstop 20 // start og stop værdien for softlys
#define pir1def 14 //pir1 ben
#define pir2def 15 //pir2 ben
#define hwswdef 13 //hardware switch

// --------- Globale mutex-objekter til at beskytte delte variabler mellem cores ---------
mutex_t lys_mutex;  //< Beskytter lys- og PWM-værdier
mutex_t nat_mutex;  //< Beskytter nat/dag-status
mutex_t param_mutex;//< Beskytter konfigurationsparametre (LysParam)
mutex_t pir_mutex;//< Beskytter PIR-relaterede tider

//ssid og password er predifineret i mitjason.h

// --------- WiFi og systemnavn ---------
#define systemNavn "lyskontrol"//msdn service
String hostname = systemNavn;


// --------- Globale tilstande for systemet ---------
bool core1_separate_stack = true; ///< Egen stak for core1
bool swaktiv = false;             ///< Software on/off (bruges i web)
int last_lysprocent = 0;          ///< PWM-værdi (0-100%)
bool kopinatstatus = false;       ///< Om nataktiv er aktiv
LysParam lysparam;                ///< Systemets konfigurationsparametre (mutexbeskyttet!)
// --------- Core1-delte variabler ---------
// (Se også lignende deklaration i core1-sektion)
extern float last_lux;
extern float last_temp;
extern float last_pressure;
extern float internaltemp;
extern bool hwaktiv;
extern bool updatelysprocent;
extern int nyupdatevaerdi;
extern bool tvungeton;
extern LysAutomatik *automatik;

extern String sidste1pirtid;
extern String sidste2pirtid;
extern String sidstehwswtid;
void blink();
void softlysIrq();

// --------- NTP- og WiFi-opsætning ---------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "dk.pool.ntp.org", 2 * 3600, 60000); // GMT+2, opdater hvert minut
WiFiUDP udp;
// Heap-allokering
WiFiServer server(80);

// --------- Funktioner ---------
// --------- Konfiguration & log ---------
MitJsonWiFi* mitjason = new MitJsonWiFi;
WebServerHandler *webHandler = new WebServerHandler(
    last_lysprocent,
    internaltemp,
    tvungeton,
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

//SimpleHardwareTimer softlysTimer;
// Global queue for received codes
std::queue<uint32_t> logQueue;
LysLog *lyslog = new LysLog(sd, lysparam.lognataktiv,lysparam.logpirdetection);

// --------- SD-kort og sensor flags ---------
SdFat sd;
FsFile myFile;
bool ntpsat = false;
bool BH1750_tilstede = false;// husk denne variabel styres fra core 1
bool BMP280_tilstede = false; // core 1 variabel

  /*
    denne klasse er beregnet til at styre et 220v relay med efterfølgende 8A dimmer kredsløb 
    fra KRIDA Electronics PWM 8A AC Light Dimmer Module 50Hz 60Hz TASMOTA
    pwm indgangen kan klare 0 til 10000hz
    Af sikkerheds hensyn er der monteret et relay foran dimmer modulet således at modulet kun får 220v 
    når det er i brug.
  */
// --------- FIFO log callback ---------
void fifoTimerCallback() {
    while (rp2040.fifo.available()) {
        uint32_t code;
        rp2040.fifo.pop_nb(&code);
        logQueue.push(code);
        queueDirty = true;
    }
}

// --------- WiFi og NTP setup ---------
/**
 * @brief Forbinder til WiFi og synkroniserer tid via NTP.
 * @return true hvis alt lykkedes, ellers false.
 */
bool setupWiFiAndNTP() {
    Serial.println("Forbinder til WiFi...");
    WiFi.setHostname("lyscontrol");
    WiFi.begin(mitjason->ssid, mitjason->password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    if (WiFi.status() != WL_CONNECTED) {
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
    Serial.print("NTP tid: ");
    Serial.println(timeClient.getFormattedTime());

    // Sæt Pico RTC ud fra NTP
    time_t rawTime = timeClient.getEpochTime();
    struct tm *timeinfo = gmtime(&rawTime);
    datetime_t t = {
        .year  = (int16_t)(timeinfo->tm_year + 1900),
        .month = (int8_t)(timeinfo->tm_mon + 1),
        .day   = (int8_t)timeinfo->tm_mday,
        .dotw  = (int8_t)timeinfo->tm_wday,
        .hour  = (int8_t)timeinfo->tm_hour,
        .min   = (int8_t)timeinfo->tm_min,
        .sec   = (int8_t)timeinfo->tm_sec
    };
    rtc_init();//initialiser hardware rtc
    rtc_set_datetime(&t);
    Serial.println("RTC sat ud fra NTP!");
    return true;
}

/**
 * @brief Setup-funktion, kaldes én gang ved opstart (core0).
 */
void setup() {
  // put your setup code here, to run once:
    Serial.begin(115200);
    mutex_init(&lys_mutex);
    mutex_init(&nat_mutex);
    mutex_init(&param_mutex);
    mutex_init(&pir_mutex);
    
  //  while(!Serial) { ; } // for debug vent på usb connection
    delay(1000);

    SPI.setRX(SD_MISO);
    SPI.setCS(SD_CS);
    SPI.setSCK(SD_SCK);
    SPI.setTX(SD_MOSI);
    SPI.begin();

    if (!sd.begin(SdSpiConfig(SD_CS, DEDICATED_SPI, SD_SCK_MHZ(20)))) {
        Serial.println("SD init failed!");
        return;
    }
    Serial.println("SD init OK!");

    mitjason->loadWiFi(sd ,"/wifi.json");
    mitjason->loadDefault(sd , &lysparam);// load lysparam inden core1 starter
    if (!setupWiFiAndNTP()) {
        Serial.println("\nKunne ikke forbinde til WiFi!");
        delay(5000);
        rp2040.reboot(); //reboot indtil networkværket køre igen 
    }
    // Tjek tiden fra RTC
    datetime_t t;
    rtc_get_datetime(&t);
    Serial.printf("Tid: %04d-%02d-%02d %02d:%02d:%02d\n", t.year, t.month, t.day, t.hour, t.min, t.sec);

    fifoTimer->setInterval(10, fifoTimerCallback); //10ms
    
    
    server.begin();
    //lysparam.lognataktiv = true;
    //lysparam.logpirdetection =true;
    rp2040.idleOtherCore();// stop core1
    ntpsat = true; //maker til core1 at den kan starte setup
    rp2040.resumeOtherCore();//start 
 }

/**
 * @brief Læser events fra logQueue og logger dem til SD-kort.
 */
void checkforlog(){
    queueDirty = false;
    while (!logQueue.empty()) {
        uint32_t code = logQueue.front();
        logQueue.pop();
        Serial.print("Log event code: ");
        Serial.println(code);
        switch(code){
          case nataktivfalse:
            lyslog->logNatAktiv(false);
          break;
          case nataktivtrue:
            lyslog->logNatAktiv(true);
          break;
          case pir1_detection:
            lyslog->logPIR("pir 1");
          break;
          case pir2_detection:
            lyslog->logPIR("pir 2");
          break;
          case hwsw_on:
            lyslog->logPIR("Kontakt on");
          break;
          case swsw_on:
            lyslog->logPIR("Software on");
          break;
          default:
          break;
          }
    }
  }

/**
 * @brief Forsøger at genforbinde til WiFi.
 * @return WiFi-status
 */
int connectwifi(void){ // rekonnect wifi
  WiFi.disconnect();
  WiFi.setHostname("lyscontrol");
  int statuslocal = WiFi.begin(mitjason->ssid, mitjason->password);
  return(statuslocal);
}// end connectwifi

/**
 * @brief Main loop (core0).
 * Håndterer webserver, log og WiFi-forbindelse.
 */
void loop() {
  // put your main code here, to run repeatedly:

  //Serial.println("Loop kører");
  if(WiFi.status() != WL_CONNECTED){
    Serial.println("Disconectet");
    int status = connectwifi();// er der disconect reconect
    if(status == WL_CONNECTED){
      Serial.println("Reconectet");
    } //end if
  }//end if
  WiFiClient client = server.available();
    if (client) {
      //Serial.println("Client forbundet!");
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
            //Serial.println("Handling web request:");
            //Serial.println(req);
            webHandler->handle(client, req);
        }
        client.stop();
    }// client
   if(queueDirty) checkforlog();
   delay(1);
}


// ---------------------- Core 1: Sensorer, hardware & automatik ------------------------

#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_BMP280.h>
#include "Dimmerfunktion.h"
#include "pirroutiner.h"
#include "SimpleSoftwareTimer.h"

// --------- Core1-delte variabler ---------
BH1750 *lightMeter = new BH1750;
Adafruit_BMP280 *bmp = new Adafruit_BMP280;
bool timer_tik  = false;          ///flag sættes 1 gang pr sekund ca. 
float last_lux = 0.0;             ///< Lysmåling fra BH1750
float last_temp = 0.0;            ///< Temperatur fra BMP280
float last_pressure = 0.0;        ///< Tryk fra BMP280
float internaltemp = 0.0;         ///< Intern CPU-temperatur
bool hwaktiv = false;             ///< Hardware-switch
bool updatelysprocent = false;    ///< Flag for ændring af lysprocent
int nyupdatevaerdi = 0;           ///< Ny PWM-værdi ønsket
String sidste1pirtid = "";        ///< Sidste PIR1-aktiveringstid
String sidste2pirtid = "";        ///< Sidste PIR2-aktiveringstid
String sidstehwswtid = "";        ///< Sidste hardware-switch tid
bool tvungeton = false;             ///< Flag for tvunget tænd
bool hwaktivlocal = false;          ///< Lokal HW-aktiv
bool softwareswActiveret = false;   ///< flag for software switch aktiveret

dimmerfunktion *dimmer = new dimmerfunktion(dimmerrelayben,dimmerpwmben,dimmerstart,dimmermax,&lysparam);
LysAutomatik *automatik = new LysAutomatik(lysparam, dimmer);
pirroutiner *pirrou = new pirroutiner( pir1def, pir2def, hwswdef,&sidste1pirtid , &sidste2pirtid, &sidstehwswtid, lysparam);
SimpleSoftwareTimer myTimer;
SimpleSoftwareTimer softlysTimer;



/**
 * @brief Sætter lysstyrken via softstart (bruges fra web/core0).
 */
void setlysiprocentSoft(int procent){ //core 0 web interface
    dimmer->setlysiprocentSoft(procent);
    //Serial.println("setlysiprocentSoft");
  }

/**
 * @brief Blinker onboard LED én gang pr. sekund (indikator for core1-kørsel)
 * sætter timer_tik.brugt i timer1 loop VIGTIG
 */
void blink() {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    timer_tik = true;
}

/**
 * @brief Softlys timer IRQ; styrer softstart/sluk og PIR-timer.
 */
void softlysIrq() {

    // Softstart step pr. sekund
    if (dimmer->softstartAktiv()) {
        dimmer->softstartStep();
    }
    if (dimmer->softslukAktiv()) {
        dimmer->softslukStep();
    }
    pirrou->timerRoutine();

}

/**
 * @brief Setup-funktion for core1; initialiserer sensorer og timer.
 */
void setup1() {
  // put your setup code here, to run once:
  while(!ntpsat){ //vent på ntp kredsen er igang
    delay(200);
  }//vent på core0 er initialiseret
  
  Wire.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  myTimer.setInterval(1000, blink);
  softlysTimer.setInterval(250, softlysIrq);
  
  Serial.println("Tester BH1750 lyssensor...");
  if (lightMeter->begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
     float lux = lightMeter->readLightLevel();
     Serial.print("Lysstyrke (lux): ");
     Serial.println(lux);
     BH1750_tilstede = true; 
   } else {
        Serial.println("BH1750 ikke fundet på I2C-bussen!");
    }
  Serial.println("Tester BMP280 sensor...");
    if (!bmp->begin(0x76)) {
        Serial.println("BMP280 ikke fundet på I2C-bussen!");
    } else {
      BMP280_tilstede = true;
      Serial.print("Temperatur: ");
      Serial.print(bmp->readTemperature());
      Serial.println(" *C");
      Serial.print("Lufttryk: ");
      Serial.print(bmp->readPressure() / 100.0F);
      Serial.println(" hPa");
    }
    
}

/**
 * @brief Main loop (core1). Håndterer sensorer, automatik og PWM.
 */
void loop1() {
  // put your main code here, to run repeatedly:
  myTimer.run();
  softlysTimer.run();
  if(timer_tik){
    timer_tik = false;
    mutex_enter_blocking(&nat_mutex);
    kopinatstatus = automatik->getNataktiv();
    mutex_exit(&nat_mutex);
    /*if(kopinatstatus){
              Serial.println("nataktiv true");
              } else {
              Serial.println("nataktiv false");
                }*/
    if(BH1750_tilstede){
      if(lightMeter->measurementReady()){ 
          last_lux = lightMeter->readLightLevel();
         /* Serial.print("Lysstyrke (lux): ");
          Serial.println(last_lux);
          Serial.println();*/
        }//end if
      }//end if bh1750
    if(BMP280_tilstede){
      last_temp = bmp->readTemperature();
      last_pressure = bmp->readPressure() / 100.0F;
      /*Serial.print("Temperatur: ");
      Serial.print(last_temp);
      Serial.println(" *C");
      Serial.print("Lufttryk: ");
      Serial.print(last_pressure);
      Serial.println(" hPa");
      Serial.println();*/
      }//end if BMP280
      tvungeton = false;
      if(pirrou->isHWSWBenLow()) hwaktiv = true; else if(hwaktiv) hwaktiv = false;
      if(hwaktiv) tvungeton = true;
      if(swaktiv)
        {  tvungeton = true;
          if(not softwareswActiveret){
              softwareswActiveret = true;
              pirrou->setswswOn();
            }
        }else softwareswActiveret = false;
      if(not tvungeton){
        if(hwaktivlocal){
            hwaktivlocal = false;
            automatik->forceOff();
          }//end if
        bool pirstatus = false;
        time_t ntpTid = timeClient.getEpochTime();
        if(pirrou->isPIR1Activated()) {pirstatus = true;}
        if(pirrou->isPIR2Activated()) {pirstatus = true;}
        mutex_enter_blocking(&param_mutex);
        automatik->update(last_lux, pirstatus, ntpTid);
        mutex_exit(&param_mutex);
      } else {
        if(not hwaktivlocal){
          automatik->forceOn();
          hwaktivlocal = true;
          }//end if
         
        }
    internaltemp = analogReadTemp(); //kontrol af cpu temp webside  
    }//end if timer_tik
  mutex_enter_blocking(&lys_mutex);
 if(updatelysprocent) //sættes af core 0
  {
    updatelysprocent =false;
    dimmer->setlysiprocentSoft(nyupdatevaerdi);
   }//end if
  last_lysprocent = dimmer->returneraktuelvaerdi();
  mutex_exit(&lys_mutex);

  delay(10);
}//end loop

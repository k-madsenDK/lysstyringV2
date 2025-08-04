#pragma once
#include <Arduino.h>
#include "hardware/rtc.h"
#include "LysParam.h"

extern mutex_t pir_mutex; 
extern mutex_t param_mutex;

class pirroutiner {
  private:
    int pir1ben = -1;
    int pir2ben = -1;
    int hwswben = -1;

    bool pir1_tilstede = false;
    bool pir2_tilstede = false;
    bool hwsw_tilstede = false;

    bool pir1_aktiv = false;
    bool pir2_aktiv = false;
    bool hwsw_aktiv = false;

    bool pir1_aktiveret = false;
    bool pir2_aktiveret = false;
    bool hwsw_aktiveret = false;

    uint8_t pir1_count = 0;
    uint8_t pir2_count = 0;
    uint8_t hwsw_count = 0;

    String* pir1_tid;
    String* pir2_tid;
    String* hwsw_tid;

    LysParam& param;
    
    void initInputs(void)
    {
      if(pir1ben >=0 && pir1ben < 29){
        pinMode(pir1ben, INPUT);
        digitalWrite(pir1ben, HIGH);
        pir1_tilstede = true;
      }
      if(pir2ben >=0 && pir2ben < 29){
        pinMode(pir2ben, INPUT);
        digitalWrite(pir2ben, HIGH);
        pir2_tilstede = true;
      }
      if(hwswben >=0 && hwswben < 29){
        pinMode(hwswben, INPUT);
        digitalWrite(hwswben, HIGH);
        hwsw_tilstede = true;
      }
    }

    String tidSomStrengFraRTC() {
      datetime_t t;
      rtc_get_datetime(&t);
      char buf[20];
      sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.min, t.sec);
      return String(buf);
    }

  public:
    pirroutiner(int pir1, int pir2, int hwsw, String* pir1txt, String* pir2txt, String* hwswtxt, LysParam& p)
      : pir1_tid(pir1txt), pir2_tid(pir2txt), hwsw_tid(hwswtxt) , param(p)
    {
      this->pir1ben = pir1;
      this->pir2ben = pir2;
      this->hwswben = hwsw;
      this->initInputs();
    }
        //mutex_enter_blocking(&param_mutex);
        //automatik->update(last_lux, pirstatus, ntpTid);
        //mutex_exit(&param_mutex);
    // Timer input routine - kaldes fra softlysIrq() hver 0.25 sek
    void timerRoutine() {
        if(pir1_tilstede) {
    if(digitalRead(pir1ben) == LOW) { // PIR1 aktiveret (aktiv LOW)
      pir1_count++;
      if(pir1_count >= 3 && !pir1_aktiveret && !pir1_aktiv) {
        pir1_aktiveret = true;
        pir1_aktiv = true;
        mutex_enter_blocking(&param_mutex);
        if(param.logpirdetection) rp2040.fifo.push_nb(pir1_detection);
        mutex_exit(&param_mutex);
        mutex_enter_blocking(&pir_mutex);
        *pir1_tid = tidSomStrengFraRTC();
        mutex_exit(&pir_mutex);
      }
    } else {
      pir1_count = 0;
      pir1_aktiv = false;
    }
  }

  if(pir2_tilstede) {
    if(digitalRead(pir2ben) == LOW) { // PIR2 aktiveret (aktiv LOW)
      pir2_count++;
      if(pir2_count >= 3 && !pir2_aktiveret && !pir2_aktiv) {
        pir2_aktiveret = true;
        pir2_aktiv = true;
        mutex_enter_blocking(&param_mutex);
        if(param.logpirdetection) rp2040.fifo.push_nb(pir2_detection);
        mutex_exit(&param_mutex);
        mutex_enter_blocking(&pir_mutex);
        *pir2_tid = tidSomStrengFraRTC();
        mutex_exit(&pir_mutex);
      }
    } else {
      pir2_count = 0;
      pir2_aktiv = false;
    }
  }

      if(hwsw_tilstede) {
        if(digitalRead(hwswben) == LOW) { // Kontakt trykket = aktivering!
        hwsw_count++;
        if(hwsw_count >= 2 && !hwsw_aktiveret) {
          hwsw_aktiveret = true;
          mutex_enter_blocking(&param_mutex);
          if(param.logpirdetection) rp2040.fifo.push_nb(hwsw_on); //log hvis valgt
          mutex_exit(&param_mutex);
          mutex_enter_blocking(&pir_mutex);
          *hwsw_tid = tidSomStrengFraRTC();
          mutex_exit(&pir_mutex);
          }
        } else {
        hwsw_count = 0;
        }
      hwsw_aktiv = (digitalRead(hwswben) == LOW);
      }
  }

    // Public getters for PIR
    bool isPIR1Activated() {
      bool wasActivated = pir1_aktiveret;
      pir1_aktiveret = false;
      return wasActivated;
    }
    bool isPIR2Activated() {
      bool wasActivated = pir2_aktiveret;
      pir2_aktiveret = false;
      return wasActivated;
    }
    void setswswOn(void){//log hvis valgt
        mutex_enter_blocking(&param_mutex);
        if(param.logpirdetection) rp2040.fifo.push_nb(swsw_on);
        mutex_exit(&param_mutex);
      }
      
    bool isPIR1Present() { return pir1_tilstede; }
    bool isPIR2Present() { return pir2_tilstede; }
    bool isPIR1BenLow() { return pir1_aktiv; }
    bool isPIR2BenLow() { return pir2_aktiv; }
    String getPIR1Time() { return *pir1_tid; }
    String getPIR2Time() { return *pir2_tid; }

    // Public getters for HW switch

    bool isHWSWPresent() { return hwsw_tilstede; }
    bool isHWSWBenLow() { return hwsw_aktiv; }
    String getHWSWTime() { return *hwsw_tid; }
};

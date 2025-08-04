#pragma once
#include <Arduino.h>
#include "LysParam.h"  // Tilføjet for at få adgang til aktuelStepfrekvens

//extern mutex_t param_mutex;

class dimmerfunktion {
  private:
    int pwmstartvaerdi;
    int pwmmaxvaerdi;
    int enprocent = 0;
    int relayben;
    int pwmben;
    int aktuelpwmvaerdi = 0;
    int aktuelprocentvaerdi = 0;
    
    // Softstart/sluk variabler
    bool softstart_aktiv = false;
    bool softsluk_aktiv = false;
    int soft_slut = 100;
    int soft_step = 5;
    int soft_nuvaerende = 0;
    int aktuelsetvaerdi = 0;
    LysParam* lysparam_ptr = nullptr; // pointer til LysParam

    void dimmerinit(){
      analogWriteRange(65535);
      analogWriteFreq(10000);
      analogWrite(pwmben, aktuelpwmvaerdi);
      pinMode(relayben, OUTPUT);
      digitalWrite(relayben, 0);
      this->enprocent = (pwmmaxvaerdi - pwmstartvaerdi) / 100;
    }

    // Sikker relæstyring
    void relayOn()  { digitalWrite(relayben, 1); }
    void relayOff() { digitalWrite(relayben, 0); }

    bool setlysiprocent(int nyvaerdi) {
      if (nyvaerdi < 0 || nyvaerdi > 100) return false;
      Serial.print("Ny lysværdi = ");Serial.println(nyvaerdi);
      aktuelprocentvaerdi = nyvaerdi;
      if(nyvaerdi == 0){
        aktuelsetvaerdi = 0;
        aktuelprocentvaerdi = 0;
        aktuelpwmvaerdi = 0;
        analogWrite(pwmben, aktuelpwmvaerdi);
        relayOff();
        return true;
      }
      aktuelpwmvaerdi = (enprocent * nyvaerdi) + pwmstartvaerdi;
      analogWrite(pwmben, aktuelpwmvaerdi);
      relayOn();
      return true;
    }
    
  public:
    // Ny constructor der kan tage pointer til LysParam
    dimmerfunktion(int relayben = 2, int pwmben = 0, int pwmlow = 0, int pwmhigh = 65535, LysParam* lysparam = nullptr) {
      this->relayben = relayben;
      this->pwmben = pwmben;
      this->pwmstartvaerdi = pwmlow;
      this->pwmmaxvaerdi = pwmhigh;
      this->lysparam_ptr = lysparam;
      this->dimmerinit();
    }

    // Hvis objektet ikke blev oprettet med LysParam, tilføj mulighed for at sætte pointer senere
    void setLysParam(LysParam* param) {
      this->lysparam_ptr = param;
    }

    void sluk(void) {
      this->setlysiprocentSoft(0);
    }
    
    void taend(void) {
      this->setlysiprocentSoft(100);
    }

    // Softstart - stigende lys
    void startSoftStart(int slutProcent = 100) {
      softstart_aktiv = true;
      softsluk_aktiv = false;
      soft_slut = slutProcent;
      // Brug step fra lysparam hvis tilgængelig
      if (lysparam_ptr) {
        soft_step = lysparam_ptr->aktuelStepfrekvens;
      }else {
        soft_step = 5;
      }
      soft_nuvaerende = aktuelprocentvaerdi;
    }

    void softstartStep() {
      if (!softstart_aktiv) return;
      soft_nuvaerende += soft_step;
      if (soft_nuvaerende >= soft_slut) {
        setlysiprocent(soft_slut);
        softstart_aktiv = false;
      } else {
        setlysiprocent(soft_nuvaerende);
      }
    }

    bool softstartAktiv() { return softstart_aktiv; }

    // Softsluk - faldende lys
    void startSoftSluk(int slutProcent ) {
      softsluk_aktiv = true;
      softstart_aktiv = false;
      soft_slut = slutProcent;
      // Brug step fra lysparam hvis tilgængelig
      if (lysparam_ptr) {
        soft_step = lysparam_ptr->aktuelStepfrekvens;
      } 
      soft_nuvaerende = aktuelprocentvaerdi;
    }

    void softslukStep() {
      if (!softsluk_aktiv) return;
      soft_nuvaerende -= soft_step;
      if (soft_nuvaerende <= soft_slut) {
        setlysiprocent(soft_slut);
        softsluk_aktiv = false;
      } else {
        setlysiprocent(soft_nuvaerende);
      }
    }
    
    void setlysiprocentSoft(int nyvaerdi) {
      if (nyvaerdi < 0 || nyvaerdi > 100) return;
      aktuelsetvaerdi = nyvaerdi;
      Serial.print("Ny setlysværdi = ");Serial.println(nyvaerdi);
      if (nyvaerdi > aktuelprocentvaerdi) {
        startSoftStart(nyvaerdi);
      } else if (nyvaerdi < aktuelprocentvaerdi) {
        startSoftSluk(nyvaerdi);
      } 
    }

    bool softslukAktiv() { return softsluk_aktiv; }

    int returnersetvaerdi(void) {
      return aktuelsetvaerdi;
    }

    int returneraktuelvaerdi(void){
      return aktuelprocentvaerdi;
    }
};

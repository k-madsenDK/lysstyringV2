#pragma once
#include <Arduino.h>

// Try to include the CYW43 headers provided by the Philhower core.
// If not available in the installed core, the library will compile but return false/-1 at runtime.
#if __has_include("pico/cyw43_arch.h") && __has_include("cyw43.h")
extern "C" {
  #include "pico/cyw43_arch.h"
  #include "cyw43.h"
}
  #define CYW43PWR_HAS_CYW43 1
  // Provided by the driver in the core
  extern cyw43_t cyw43_state;
#else
  #define CYW43PWR_HAS_CYW43 0
#endif

// Some core builds may not define CYW43_PERFORMANCE_PM; map it to NO_POWERSAVE as a safe fallback.
#ifndef CYW43_PERFORMANCE_PM
  #define CYW43_PERFORMANCE_PM CYW43_NO_POWERSAVE_MODE
#endif

namespace Cyw43Power {

  enum PowerMode {
    Default,     // Core default power-save policy
    NoSave,      // Disable power save (maximum radio performance)
    Performance  // CYW43_PERFORMANCE_PM if available, otherwise same as NoSave
  };

  // Set the WiFi power mode. Returns true on success.
  // Returns false if the current core build doesn't expose the cyw43 APIs.
  bool setPowerMode(PowerMode mode);

  // Get RSSI (in dBm, negative number, typically -30..-90).
  // Returns true on success, false if unsupported.
  bool getRSSI(int8_t& outDbm);

  // Get link status from the driver (CYW43_LINK_*), or -1 if unsupported.
  int linkStatus();
}

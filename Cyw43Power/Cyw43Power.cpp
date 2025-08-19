#include "Cyw43Power.h"

namespace Cyw43Power {

bool setPowerMode(PowerMode mode) {
#if CYW43PWR_HAS_CYW43
  uint32_t pm = CYW43_DEFAULT_PM;
  switch (mode) {
    case Default:     pm = CYW43_DEFAULT_PM;        break;
    case NoSave:      pm = CYW43_NO_POWERSAVE_MODE; break;
    case Performance: pm = CYW43_PERFORMANCE_PM;    break;
  }
  // Protect driver calls against LWIP background threads
  cyw43_arch_lwip_begin();
  int rc = cyw43_wifi_pm(&cyw43_state, pm);
  cyw43_arch_lwip_end();
  return rc == 0;
#else
  (void)mode;
  return false;
#endif
}

bool getRSSI(int8_t& outDbm) {
#if CYW43PWR_HAS_CYW43
  cyw43_arch_lwip_begin();
  int32_t rssi = 0;
  int rc = cyw43_wifi_get_rssi(&cyw43_state, &rssi);
  cyw43_arch_lwip_end();
  if (rc == 0 && rssi <= 0 && rssi >= -127) {
    outDbm = static_cast<int8_t>(rssi);
    return true;
  }
  return false;
#else
  (void)outDbm;
  return false;
#endif
}

int linkStatus() {
#if CYW43PWR_HAS_CYW43
  cyw43_arch_lwip_begin();
  int st = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
  cyw43_arch_lwip_end();
  return st;
#else
  return -1;
#endif
}

} // namespace Cyw43Power

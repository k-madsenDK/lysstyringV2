#pragma once
#include <Ticker.h>

class SimpleHardwareTimer {
public:
    void setInterval(uint32_t interval_ms, void (*cb)()) {
        ticker.attach_ms(interval_ms, cb);
    }
    void stop() {
        ticker.detach();
    }
private:
    Ticker ticker;
};

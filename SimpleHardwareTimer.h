#pragma once
/**
 * @file SimpleHardwareTimer.h
 * @brief Wrapper for Ticker hardware timer.
 *
 * Præcis timing uafhængigt af loop()-hastighed.
 */

#include <Ticker.h>

class SimpleHardwareTimer {
public:
    /**
     * @brief Start hardware-timer med interval og callback.
     * @param interval_ms Interval i millisekunder.
     * @param cb Callback-funktion.
     */
    void setInterval(uint32_t interval_ms, void (*cb)()) {
        ticker.attach_ms(interval_ms, cb);
    }

    /** Stop hardware-timeren. */
    void stop() {
        ticker.detach();
    }

private:
    Ticker ticker;
};

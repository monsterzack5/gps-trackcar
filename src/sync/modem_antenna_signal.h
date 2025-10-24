#pragma once

#include <zephyr/kernel.h>

// The NRF9151 board we're using only has a single antenna for GPS and for Cell.
// It needs to share the antenna when we're rxing/txing cell or rxing GPS.
// Since we are using periodic GPS signaling interrupts, we can asynchronously
// tell the modem to transmit right as the modem is trying to get a GPS fix.
// We use this signal to suspend handling network requests while the modem
// is using GPS.

extern k_poll_signal modem_is_free_signal;
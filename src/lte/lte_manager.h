#pragma once

#include <stdint.h>

enum class LteMode : uint8_t {
    Connected,
    Disconnected,
};

namespace lte {
int init();
int set_mode(LteMode mode);
} // namespace lte
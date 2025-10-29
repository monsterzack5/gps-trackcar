#pragma once

struct network_request {
    char body[CONFIG_NETWORK_PACKET_SIZE] = { 0 };
};

#pragma once

#include <zephyr/net/socket.h>

addrinfo* resolve_dns_with_caching(const char* url, const char* port, bool force_invalidate = false);
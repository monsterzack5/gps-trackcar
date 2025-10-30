#include "dns.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dns, CONFIG_TRACCAR_DEFAULT_LOG_LEVEL);

const size_t URL_MAX_SIZE = 32;
const size_t PORT_MAX_SIZE = 8;

struct DnsQuery {
    addrinfo* info_ptr;
    int64_t lookup_time;
    char name[URL_MAX_SIZE];
    char port[PORT_MAX_SIZE];
};

static DnsQuery cached_entries[CONFIG_DNS_CACHE_ENTRIES] = {};

static void print_resolved_address(addrinfo* address)
{
    char peer_addr[INET6_ADDRSTRLEN];

    if (address == nullptr) {
        return;
    }

    // IPv4
    if (address->ai_family == AF_INET) {
        sockaddr_in* ipv4 = (struct sockaddr_in*)address->ai_addr;
        inet_ntop(AF_INET, &ipv4->sin_addr, peer_addr, sizeof(peer_addr));
        LOG_DBG("Resolved IPv4 address: %s\n", peer_addr);
        return;
    }

    // IPv6
    if (address->ai_family == AF_INET6) {
        sockaddr_in6* ipv6 = (struct sockaddr_in6*)address->ai_addr;
        inet_ntop(AF_INET6, &ipv6->sin6_addr, peer_addr, sizeof(peer_addr));
        LOG_DBG("Resolved IPv6 address: %s\n", peer_addr);
    }
}

static int find_first_free_cache_slot()
{
    for (size_t i = 0; i < CONFIG_DNS_CACHE_ENTRIES; i += 1) {
        if (cached_entries[i].name[0] == '\0') {
            return i;
        }
    }
    return -1;
}

static int actually_resolve(DnsQuery* query)
{
    static const addrinfo hints = {
        .ai_flags = AI_NUMERICSERV, /* Let getaddrinfo() set port */
        .ai_socktype = SOCK_STREAM,
    };

    if (query->info_ptr != nullptr) {
        freeaddrinfo(query->info_ptr);
        query->info_ptr = nullptr;
    }

    bool did_resolve = false;
    for (int attempts = 1; attempts <= 3; attempts += 1) {
        int rc = getaddrinfo(query->name, query->port, &hints, &query->info_ptr);

        if (rc != 0) {
            LOG_ERR("getaddrinfo failed with rc = %d, attempt %d", rc, attempts);
            continue;
        }

        did_resolve = true;
        break;
    }

    if (!did_resolve) {
        return -1;
    }

    if (IS_ENABLED(CONFIG_LOG)) {
        print_resolved_address(query->info_ptr);
    }

    return 0;
}

static int clear_least_recently_used()
{

    int64_t oldest_time = INT64_MAX;
    size_t least_used_query_index = SIZE_MAX;

    for (size_t i = 0; i < CONFIG_DNS_CACHE_ENTRIES; i += 1) {
        if (cached_entries[i].lookup_time < oldest_time) {
            oldest_time = cached_entries[i].lookup_time;
            least_used_query_index = i;
        }
    }

    DnsQuery* query = &cached_entries[least_used_query_index];
    LOG_WRN("Deleting previous cache entry for %s as we need space!, last lookup time: %lld", query->name, query->lookup_time);
    query->name[0] = '\0';
    query->port[0] = '\0';
    query->lookup_time = 0;

    if (query->info_ptr != nullptr) {
        freeaddrinfo(cached_entries[least_used_query_index].info_ptr);
        query->info_ptr = nullptr;
    }

    return least_used_query_index;
}

//
addrinfo* resolve_dns_with_caching(const char* url, const char* port, bool force_invalidate)
{
    // Try to find the name in the array
    DnsQuery* current_query = nullptr;

    for (size_t i = 0; i < CONFIG_DNS_CACHE_ENTRIES; i += 1) {
        if (strncmp(cached_entries[i].name, url, URL_MAX_SIZE) == 0) {
            // Found the entry
            LOG_DBG("Found cached entry for url: %s, at index = %u", url, i);
            current_query = &cached_entries[i];
            break;
        }
    }

    if (current_query == nullptr) {
        int first_free = find_first_free_cache_slot();
        if (first_free == -1) {
            LOG_ERR("No more DNS Cache entries available, clearing least recently used cache!");
            first_free = clear_least_recently_used();
        }

        LOG_DBG("Using dns cache slot %u for url %s", first_free, url);

        // Store it
        snprintf(cached_entries[first_free].name, URL_MAX_SIZE, "%s", url);
        snprintf(cached_entries[first_free].port, PORT_MAX_SIZE, "%s", port);

        cached_entries[first_free].lookup_time = k_uptime_get();

        current_query = &cached_entries[first_free];
    }

    // Resolve DNS
    // Fun thing in zephyr, k_uptime_delta updates the pointer you pass to it!
    int64_t uptime_copy = current_query->lookup_time;
    int64_t delta = k_uptime_delta(&uptime_copy);

    // Return cached query if we're not told to invalidate, and the last query is less than 10 hours
    // 12 Hours
    if ((delta < 43200000ll) && !force_invalidate && current_query->info_ptr != nullptr) {
        LOG_DBG("Using cached DNS entry for %s", current_query->name);
        print_resolved_address(current_query->info_ptr);
        return current_query->info_ptr;
    }

    if (actually_resolve(current_query) == 0) {
        return current_query->info_ptr;
    }

    // We failed to resolve DNS! return an error.
    LOG_WRN("Failed to do a DNS Lookup for %s", current_query->name);
    return nullptr;
}
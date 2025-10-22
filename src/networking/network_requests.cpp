#include "network_requests.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/sys/socket.h>

#include "battery.h"
#include "connectivity.h"
#include "dns.h"
#include "network_info.h"
#include "tls.h"

LOG_MODULE_REGISTER(network_requests, LOG_LEVEL_DBG);

// TODO: Extra info struct that counts failed attempts
//       if too many failed attempts, re-resolve DNS
// TODO: Add a timer that checks if the network stack has been on too long
// TODO: Add watchdog
// TODO: Add a mutex that locks transmitting if we are currently trying to get a fix
// TODO: Packet builder with dedicated stack space
// TODO: Make packet queue generic

static void handle_network_request(k_work* work);
static int send_http_request(char* request_body, size_t request_length, char* receive_buffer, size_t receive_length);

// TODO:
// struct network_request {
// char body[512];
// };

// This contains time sensitive data, imei does not change, so we
// don't need to include it.
struct gps_tracker_packet {
    nrf_modem_gnss_pvt_data_frame frame;
    ProviderInfo info;
    uint8_t battery_soc;
};

K_THREAD_STACK_DEFINE(network_requests_workqueue_stack, (1024 * 10));
// TODO:
// Maybe this should be more generic and hold a packet?
// We want to queue all of our packets
// work on that later.
K_MSGQ_DEFINE(network_requests_msgq, sizeof(struct gps_tracker_packet), 20, 1);
static k_work_q network_requests_workqueue;

K_WORK_DEFINE(handle_message_queue, handle_network_request);

int network_requests_init()
{
    struct k_work_queue_config cfg = {
        .name = "network_requests_work_q",
        .no_yield = false
    };

    k_work_queue_init(&network_requests_workqueue);
    k_work_queue_start(&network_requests_workqueue, network_requests_workqueue_stack, K_THREAD_STACK_SIZEOF(network_requests_workqueue_stack), 10, &cfg);

    return 0;
}

int send_gps_update(const nrf_modem_gnss_pvt_data_frame& frame)
{
    gps_tracker_packet packet {};

    packet.frame = frame;
    packet.info = get_provider_info();
    packet.battery_soc = get_battery_soc();

    LOG_INF("Submitted to the networking msgq");
    int put_rc = k_msgq_put(&network_requests_msgq, &packet, K_NO_WAIT);
    if (put_rc == -ENOMSG) {
        LOG_WRN("Network requests queue is full! Dropping Packet!");
        return -1;
    } else if (put_rc != 0) {
        LOG_WRN("Failed to add packet to network msgq, rc = %d", put_rc);
    }

    k_work_submit_to_queue(&network_requests_workqueue, &handle_message_queue);

    return 0;
}

static void handle_network_request(k_work* work)
{
    ARG_UNUSED(work);

    // We should support different kinds of messages.
    // For now, handle GPS
    LOG_DBG("Handling network packet");
    gps_tracker_packet packet;
    // _peek because _get removes the packet, we only want to clear
    // the packet if we use it.
    int rc = k_msgq_peek(&network_requests_msgq, &packet);
    if (rc < 0) {
        LOG_WRN("Handle network request called with nothing to process!");
        return;
    }

    char request_buffer[1024] = { 0 };
    char query_string[800] = { 0 };
    char iso8601_time[60] = { 0 };
    char receive_buffer[1024] = { 0 };
    char network_info[256] = { 0 };
    char charge[12] = { 0 };

    // Build timestamp
    snprintf(iso8601_time, sizeof(iso8601_time),
        "%04u-%02u-%02uT%02u:%02u:%02uZ",
        packet.frame.datetime.year,
        packet.frame.datetime.month,
        packet.frame.datetime.day,
        packet.frame.datetime.hour,
        packet.frame.datetime.minute,
        packet.frame.datetime.seconds);

    snprintf(network_info, sizeof(network_info), "%u,%u,%u,%u,%u",
        packet.info.mcc,
        packet.info.mnc,
        packet.info.lac,
        packet.info.cellid,
        packet.info.signal_strength);

    // TODO: Make actually work
    // Build charge
    snprintf(charge, sizeof(charge), "%s", "false");

    // snprintf(query_string, sizeof(query_string), "/?id=%s&lat=%f&lon=%f&timestamp=%s", params.imei, params.frame.latitude, params.frame.longitude, iso8601_time);
    float battery_level = get_battery_soc();
    snprintf(query_string, sizeof(query_string),
        "/?"
        "id=%s"
        "&lat=%f"
        "&lon=%f"
        "&accuracy=%.1f"
        "&heading=%.1f"
        "&altitude=%.2f"
        "&timestamp=%s"
        "&cell=%s"
        "&batt=%.1f"
        "&charge=%s"
        "&temp=%.1f",
        get_imei(),
        packet.frame.latitude,
        packet.frame.longitude,
        (double)packet.frame.accuracy,
        (double)packet.frame.heading,
        (double)packet.frame.altitude,
        iso8601_time,
        network_info,
        (double)battery_level,
        charge,
        (double)packet.info.temperature);

    // Build Request
    snprintf(request_buffer, sizeof(request_buffer),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%s\r\n"
        "Connection: close\r\n"
        "User-Agent: curl/8.14.1\r\n" // todo: new user-agent?
        "Accept: */*\r\n"
        "\r\n",
        query_string, CONFIG_TRACCAR_HOSTNAME, CONFIG_TRACCAR_PORT);

    size_t request_length = strnlen(request_buffer, sizeof(request_buffer));
    size_t query_len = strnlen(query_string, sizeof(query_string));
    size_t iso_len = strnlen(iso8601_time, sizeof(iso8601_time));
    size_t network_info_len = strnlen(network_info, sizeof(network_info));
    LOG_INF("Request len: %u, Query Len: %u, iso len: %u network info: %u\n", request_length, query_len, iso_len, network_info_len);

    int send_rc = send_http_request(request_buffer, request_length, receive_buffer, sizeof(receive_buffer));
    if (send_rc == 0) {
        (void)k_msgq_get(&network_requests_msgq, &packet, K_NO_WAIT);
    }
    // size_t printed = 0;
    // size_t how_many_to_print = 30;
    // // LOG_DBG("%.*s", length_to_print, rx_buffer);
    // do {
    //     LOG_DBG("%.*s", how_many_to_print, &request_buffer[printed]);
    //     printed += 30;
    //     if (request_length < how_many_to_print) {
    //         how_many_to_print = request_length;
    //     }
    // } while (printed < request_length);
}

static int send_http_request(char* request_body, size_t request_length, char* receive_buffer, size_t receive_length)
{
    // Make sure we're connected
    set_networking_state(NetworkState::Connected);
    // Time for networking to stabilize
    k_sleep(K_MSEC(200));

    addrinfo* res = resolve_dns_with_caching(CONFIG_TRACCAR_HOSTNAME, CONFIG_TRACCAR_PORT);

    if (res == nullptr) {
        // Failed to resolve, fail now.
        set_networking_state(NetworkState::Disconnected);
        return -1;
    }

    int fd = -1;

    const auto cleanup = [&]() {
        if (fd > 0) {
            (void)close(fd);
        }
    };

    fd = socket(res->ai_family, SOCK_STREAM, IPPROTO_TLS_1_2);

    if (fd == -1) {
        LOG_DBG("Failed to open socket!\n");
        cleanup();
        return -1;
    }

    /* Setup TLS socket options */
    int err = tls_setup(fd);
    if (err) {
        cleanup();
        return -1;
    }

    LOG_DBG("Connecting to %s:%d\n", CONFIG_TRACCAR_HOSTNAME,
        ntohs(((struct sockaddr_in*)(res->ai_addr))->sin_port));
    err = connect(fd, res->ai_addr, res->ai_addrlen);
    if (err) {
        LOG_DBG("connect() failed, err: %d\n", errno);
        cleanup();
        return -1;
    }

    // TODO: Add back chunking.
    // Make our request
    int bytes = send(fd, request_body, request_length, 0);
    if (bytes < 0) {
        LOG_DBG("send() failed, err %d\n", errno);
        cleanup();
        return -1;
    }

    LOG_DBG("Sent %d bytes\n", bytes);

    // TODO: Add back chunking
    bytes = recv(fd, receive_buffer, receive_length, 0);
    if (bytes < 0) {
        LOG_DBG("recv() failed, err %d\n", errno);
        cleanup();
        return -1;
    }

    LOG_DBG("Received %d bytes\n", bytes);

    /* Print HTTP response */
    LOG_DGB("Received response:\n%s\n", receive_buffer);
    LOG_DBG("Finished, cleaning up\n");

    // TODO: Make this nicer
    // Time for TCP teardown
    k_sleep(K_MSEC(200));
    set_networking_state(NetworkState::Disconnected);

    cleanup();

    return 0;
}
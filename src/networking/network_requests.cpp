#include "network_requests.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include "battery.h"
#include "connectivity.h"
#include "network_info.h"
#include "dns.h"
#include "tls.h"

LOG_MODULE_REGISTER(network_requests, LOG_LEVEL_DBG);

// TODO: Make sure we properly understand which stack space we're using.

static void handle_network_request(k_work* work);
static void send_http_request(char* request_body, size_t request_length, char* receive_buffer, size_t receive_length);

// This contains time sensitive data, imei does not change, so we
// don't need to include it.
struct gps_tracker_packet {
    nrf_modem_gnss_pvt_data_frame frame;
    ProviderInfo info;
    uint8_t battery_soc;
};

// This creates a stack nKB large
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

    // 1. Create message struct
    // 2. Put it in the message queue
    // 3. Tell the queue to work.

    gps_tracker_packet packet {};

    packet.frame = frame;
    packet.info = get_provider_info();
    packet.battery_soc = get_battery_soc();

    // TODO: Check if there's a way to get if the queue is full
    //       and we dropped this packet.
    k_msgq_put(&network_requests_msgq, &packet, K_NO_WAIT);

    k_work_submit_to_queue(&network_requests_workqueue, &handle_message_queue);

    return 0;
}

static void handle_network_request(k_work* work)
{
    ARG_UNUSED(work);

    // We should support different kinds of messages.
    // For now, handle GPS

    gps_tracker_packet packet {};
    // _peek because _get removes the packet, we only want to clear
    // the packet if we use it.
    int rc = k_msgq_peek(&network_requests_msgq, &packet);
    if (rc == -ENOMSG) {
        LOG_WRN("Handle network request called with nothing to process!");
        return;
    }

    char request_buffer[1024] = { 0 };
    char query_string[800] = { 0 };
    char iso8601_time[60] = { 0 };
    char receive_buffer[1024] = { 0 };
    char network_info[256] = { 0 };
    char charge[6] = { 0 };

    // Build timestamp
    snprintf(iso8601_time, sizeof(iso8601_time),
        "%04u-%02u-%02uT%02u:%02u:%02uZ",
        packet.frame.datetime.year,
        packet.frame.datetime.month,
        packet.frame.datetime.day,
        packet.frame.datetime.hour,
        packet.frame.datetime.minute,
        packet.frame.datetime.seconds);

    // Build network info
    ProviderInfo info = get_provider_info();

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

    send_http_request(request_buffer, request_length, receive_buffer, sizeof(receive_buffer));
    // size_t printed = 0;
    // size_t how_many_to_print = 30;
    // // printk("%.*s", length_to_print, rx_buffer);
    // do {
    //     LOG_DBG("%.*s", how_many_to_print, &request_buffer[printed]);
    //     printed += 30;
    //     if (request_length < how_many_to_print) {
    //         how_many_to_print = request_length;
    //     }
    // } while (printed < request_length);
}

static void send_http_request(char* request_body, size_t request_length, char* receive_buffer, size_t receive_length)
{

    // Make sure we're connected
    set_networking_state(NetworkState::Connected);

    addrinfo* res = resolve_dns_with_caching(CONFIG_TRACCAR_HOSTNAME, CONFIG_TRACCAR_PORT);

    if (res == nullptr) {
        // Failed to resolve, fail now.
        set_networking_state(NetworkState::Disconnected);
    }

    int fd = socket(res->ai_family, SOCK_STREAM, IPPROTO_TLS_1_2);

    const auto cleanup = [&]() {
        freeaddrinfo(res);
        (void)close(fd);
    };

    if (fd == -1) {
        printk("Failed to open socket!\n");
        return cleanup();
    }

    /* Setup TLS socket options */
    int err = tls_setup(fd);
    if (err) {
        return cleanup();
    }

    LOG_DBG("Connecting to %s:%d\n", CONFIG_TRACCAR_HOSTNAME,
        ntohs(((struct sockaddr_in*)(res->ai_addr))->sin_port));
    err = connect(fd, res->ai_addr, res->ai_addrlen);
    if (err) {
        printk("connect() failed, err: %d\n", errno);
        return cleanup();
    }

    // TODO: Add back chunking.
    // Make our request
    int bytes = send(fd, request_body, request_length, 0);
    if (bytes < 0) {
        printk("send() failed, err %d\n", errno);
        return cleanup();
    }

    LOG_DBG("Sent %d bytes\n", bytes);

    // TODO: Add back chunking
    bytes = recv(fd, receive_buffer, receive_length, 0);
    if (bytes < 0) {
        printk("recv() failed, err %d\n", errno);
        return cleanup();
    }

    printk("Received %d bytes\n", bytes);

    /* Print HTTP response */
    printk("Received response:\n%s\n", receive_buffer);
    LOG_DBG("Finished, cleaning up\n");

    // TODO: Make this nicer
    // Time for TCP to end
    k_sleep(K_SECONDS(1));
    set_networking_state(NetworkState::Disconnected);

    cleanup();
}
#include "network_requests.h"

#include "battery.h"
#include "connectivity.h"
#include "dns.h"
#include "net_req.h"
#include "network_info.h"
#include "packet_builder.h"
#include "tls.h"

#include <nrf_socket.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "print_bin.h"

LOG_MODULE_REGISTER(network_requests, CONFIG_TRACCAR_DEFAULT_LOG_LEVEL);

// TODO: Extra info struct that counts failed attempts
//       if too many failed attempts, re-resolve DNS
// TODO: Add a timer that checks if the network stack has been on too long

// Forward Declarations
static void handle_network_request(k_work* work);
static int send_http_request(char* request_body, size_t request_length, char* receive_buffer, size_t receive_length);

// Sync Antenna Usage
k_poll_signal modem_is_free_signal;

static k_poll_event modem_event = K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &modem_is_free_signal);
static k_work_poll handle_message_queue;

K_THREAD_STACK_DEFINE(network_requests_workqueue_stack, (1024 * 10));
K_MSGQ_DEFINE(network_requests_msgq, sizeof(struct PacketBuilder), 20, 1);
static k_work_q network_requests_workqueue;

int network_requests_init()
{
    struct k_work_queue_config cfg = {
        .name = "network_requests_work_q",
        .no_yield = false
    };

    LOG_INF("network requests init'ed");

    k_work_queue_init(&network_requests_workqueue);
    k_work_poll_init(&handle_message_queue, handle_network_request);
    k_poll_signal_init(&modem_is_free_signal);
    k_work_queue_start(&network_requests_workqueue, network_requests_workqueue_stack, K_THREAD_STACK_SIZEOF(network_requests_workqueue_stack), 10, &cfg);

    return 0;
}

int send_gps_update(const nrf_modem_gnss_pvt_data_frame& frame)
{
    auto info = get_provider_info();
    auto bat_soc = get_battery_soc();

    PacketBuilder packet {};

    packet.build_gps_packet(frame, info, bat_soc);

    int put_rc = k_msgq_put(&network_requests_msgq, &packet, K_NO_WAIT);

    if (put_rc == -ENOMSG) {
        LOG_WRN("Network requests queue is full! Dropping Packet!");
        return -1;
    } else if (put_rc != 0) {
        LOG_WRN("Failed to add packet to network msgq, rc = %d", put_rc);
    }

    uint32_t is_signaled = 0;
    int result = 0;
    k_poll_signal_check(&modem_is_free_signal, &is_signaled, &result);

    k_work_poll_submit_to_queue(&network_requests_workqueue, &handle_message_queue, &modem_event, 1, K_HOURS(1));
    return 0;
}

static void handle_network_request(k_work* work)
{
    ARG_UNUSED(work);

    LOG_DBG("Handling network packet");
    // _peek because _get removes the packet, we only want to clear
    // the packet if we use it.
    PacketBuilder packet {};
    int rc = k_msgq_get(&network_requests_msgq, &packet, K_NO_WAIT);
    if (rc < 0) {
        LOG_WRN("Handle network request called with nothing to process!");
        return;
    }

    size_t request_len = strnlen(packet.get_raw_buffer(NULL), CONFIG_NETWORK_PACKET_SIZE);

    // LOG_INF("Body pulled, len = %u:\n", request_len);
    // print_u8_array((uint8_t*)packet.get_raw_buffer(NULL), request_len);

    char rec_buf[CONFIG_NETWORK_PACKET_SIZE] = { 0 };

    int send_rc = send_http_request(packet.get_raw_buffer(NULL), request_len, rec_buf, sizeof(rec_buf));

    // Re-queue the packet if we fail to send it.
    if (send_rc != 0) {
        k_msgq_put(&network_requests_msgq, &packet, K_NO_WAIT);
    }

    // If we have other packets to send, queue them, if not, disconnect
    if (k_msgq_num_used_get(&network_requests_msgq) > 0) {
        LOG_INF("Message queue not empty, scheduling another run");
        k_work_poll_submit_to_queue(&network_requests_workqueue, &handle_message_queue, &modem_event, 1, K_FOREVER);
    } else {
        set_networking_state(NetworkState::Disconnected);
    }
}

#include "print_bin.h"

static int send_http_request(char* request_body, size_t request_length, char* receive_buffer, size_t receive_length)
{
    // Make sure we're connected
    set_networking_state(NetworkState::Connected);
    // Time for networking to stabilize
    k_sleep(K_MSEC(200));

    addrinfo* res = resolve_dns_with_caching(CONFIG_TRACCAR_HOSTNAME, CONFIG_TRACCAR_PORT);

    if (res == nullptr) {
        // Failed to resolve, fail now.
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
        LOG_ERR("Failed to open socket!\n");
        cleanup();
        return -1;
    }

    /* Setup TLS socket options */
    int err = tls_setup(fd);
    if (err) {
        cleanup();
        return -1;
    }

    LOG_DBG("Connecting to %s:%d", CONFIG_TRACCAR_HOSTNAME,
        ntohs(((struct sockaddr_in*)(res->ai_addr))->sin_port));
    err = connect(fd, res->ai_addr, res->ai_addrlen);
    if (err) {
        LOG_ERR("connect() failed, err: %d", errno);
        cleanup();
        return -1;
    }

    LOG_DBG("Request length: %u", request_length);

    int bytes = send(fd, request_body, request_length, 0);
    if (bytes < 0) {
        LOG_ERR("send() failed, err %d", errno);
        LOG_ERR("Error string: %s", strerror(errno));
        cleanup();
        return -1;
    }

    LOG_DBG("Sent %d bytes\n", bytes);

    bytes = recv(fd, receive_buffer, receive_length, 0);
    if (bytes < 0) {
        LOG_DBG("recv() failed, err %d", errno);
        cleanup();
        return -1;
    }

    LOG_DBG("Received %d bytes", bytes);

    /* Print HTTP response */
    LOG_DBG("Received response:\n%s", receive_buffer);
    cleanup();

    return 0;
}
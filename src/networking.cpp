#include "networking.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include <modem/modem_key_mgmt.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/tls_credentials.h>

// #include <modem/nrf_modem_lib.h>

// #include "lte_manager.h"

LOG_MODULE_REGISTER(networking, LOG_LEVEL_DBG);

static void send_http_request(char* request_body, size_t request_length, char* receive_buffer, size_t receive_length);

static const char cert[] = {
#include "root_cert.pem.inc"

    // Needed for Zephyr TLS Functions, not Nordics
    // IF_ENABLED(CONFIG_TLS_CREDENTIALS, (0x00))
};

K_SEM_DEFINE(network_connected, 0, 1)

#define TLS_SEC_TAG 42

// TODO IMPORTANT:
// This code seems to randomly hang the entire device
// and provisioning can take a LONG time, we need to implement
// retries and timeouts, especially for whenever we take a sem.
int cert_provision(void)
{

    LOG_INF("Provisioning certificate\n");

    bool exists = false;
    int mismatch = 0;

    /* It may be sufficient for you application to check whether the correct
     * certificate is provisioned with a given tag directly using modem_key_mgmt_cmp().
     * Here, for the sake of the completeness, we check that a certificate exists
     * before comparing it with what we expect it to be.
     */

    int err = modem_key_mgmt_exists(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, &exists);
    if (err) {
        printk("Failed to check for certificates err %d\n", err);
        return err;
    }

    if (exists) {
        mismatch = modem_key_mgmt_cmp(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, cert,
            sizeof(cert));
        if (!mismatch) {
            printk("Certificate match\n");
            return 0;
        }

        printk("Certificate mismatch\n");
        err = modem_key_mgmt_delete(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN);
        if (err) {
            printk("Failed to delete existing certificate, err %d\n", err);
        }
    }

    printk("Provisioning certificate to the modem\n");

    /*  Provision certificate to the modem */
    // This takes a while
    err = modem_key_mgmt_write(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, cert,
        sizeof(cert));
    if (err) {
        printk("Failed to provision certificate, err %d\n", err);
        return err;
    }

    return 0;
}

int tls_setup(int fd)
{
    int err;
    int verify;

    /* Security tag that we have provisioned the certificate with */
    const sec_tag_t tls_sec_tag[] = {
        TLS_SEC_TAG,
    };

    /* Set up TLS peer verification */
    enum {
        NONE = 0,
        OPTIONAL = 1,
        REQUIRED = 2,
    };

    verify = REQUIRED;

    err = setsockopt(fd, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
    if (err) {
        printk("Failed to setup peer verification, err %d\n", errno);
        return err;
    }

    /* Associate the socket with the security tag
     * we have provisioned the certificate with.
     */
    err = setsockopt(fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag, sizeof(tls_sec_tag));
    if (err) {
        printk("Failed to setup TLS sec tag, err %d\n", errno);
        return err;
    }

    err = setsockopt(fd, SOL_TLS, TLS_HOSTNAME, CONFIG_TRACCAR_HOSTNAME,
        sizeof(CONFIG_TRACCAR_HOSTNAME) - 1);
    if (err) {
        printk("Failed to setup TLS hostname, err %d\n", errno);
        return err;
    }
    return 0;
}

int send_packet(const nrf_modem_gnss_pvt_data_frame& frame)
{
    // Build Packet
    // Pass it to send_http_request

    // TODO: These are all too big!
    char request_buffer[2048] = { 0 };
    char query_string[1024] = { 0 };
    char iso8601_time[60] = { 0 };
    char receive_buffer[1024] = { 0 };

    // Scary
    memset(request_buffer, '\0', sizeof(request_buffer));
    memset(query_string, '\0', sizeof(query_string));
    memset(iso8601_time, '\0', sizeof(iso8601_time));
    memset(receive_buffer, '\0', sizeof(receive_buffer));

    // Build query string
    // "/?id=123456&lat=48.8566&lon=2.3522&timestamp=1609459200000"

    // Build timestamp
    snprintf(iso8601_time, sizeof(iso8601_time),
        "%04u-%02u-%02uT%02u:%02u:%02uZ",
        frame.datetime.year,
        frame.datetime.month,
        frame.datetime.day,
        frame.datetime.hour,
        frame.datetime.minute,
        frame.datetime.seconds);
    // Build Query String
    snprintf(query_string, sizeof(query_string), "/?id=%s&lat=%f&lon=%f&timestamp=%s", "112233", frame.latitude, frame.longitude, iso8601_time);

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

    printk("Full Request:\n------------------\n%s\n------------------\n", request_buffer);

    send_http_request(request_buffer, request_length, receive_buffer, sizeof(receive_buffer));

    // TODO:
    return 0;
}

static void send_http_request(char* request_body, size_t request_length, char* receive_buffer, size_t receive_length)
{

    // Make sure we're connected

    set_networking_state(NetworkState::Connected);

    printk("Looking up %s\n", CONFIG_TRACCAR_HOSTNAME);

    const addrinfo hints = {
        .ai_flags = AI_NUMERICSERV, /* Let getaddrinfo() set port */
        .ai_socktype = SOCK_STREAM,
    };

    addrinfo* res;
    int err = getaddrinfo(CONFIG_TRACCAR_HOSTNAME, CONFIG_TRACCAR_PORT, &hints, &res);
    if (err) {
        printk("getaddrinfo() failed, err %d\n", errno);
        return;
    }

    // Does this only work with IPv6?
    char peer_addr[INET6_ADDRSTRLEN];
    inet_ntop(res->ai_family, &((struct sockaddr_in*)(res->ai_addr))->sin_addr, peer_addr, INET6_ADDRSTRLEN);

    // TODO: Cache this
    LOG_DBG("Resolved %s (%s)\n", peer_addr, net_family2str(res->ai_family));

    // if (IS_ENABLED(CONFIG_SAMPLE_TFM_MBEDTLS)) {
    // fd = socket(res->ai_family, SOCK_STREAM | SOCK_NATIVE_TLS, IPPROTO_TLS_1_2);
    // } else {
    // }
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
    err = tls_setup(fd);
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

// Callbacks
static void connectivity_event_handler(struct net_mgmt_event_callback* cb, uint32_t event,
    struct net_if* iface)
{
    // TODO: Handle ALL Events!
    if (event == NET_EVENT_CONN_IF_FATAL_ERROR) {
        printk("Fatal error received from the connectivity layer\n");
        return;
    }
}

static void l4_event_handler(struct net_mgmt_event_callback* cb, uint32_t event,
    struct net_if* iface)
{
    // TODO: Handle ALL events!
    switch (event) {
    case NET_EVENT_L4_CONNECTED:
        printk("Network connectivity established and IP address assigned\n");
        k_sem_give(&network_connected);
        break;
    case NET_EVENT_L4_DISCONNECTED:
        printk("Disconnected from the network\n");
        break;
    default:
        break;
    }
}

// I Don't think these need to be global?
static struct net_mgmt_event_callback conn_cb;
static struct net_mgmt_event_callback l4_cb;

// This function will hang until the network is online (if that is what you requested)
// TODO: Should it?
int set_networking_state(NetworkState state)
{

    int rc = 0;

    switch (state) {
    case NetworkState::Connected:
        rc = conn_mgr_all_if_connect(true);
        if (rc != 0) {
            LOG_ERR("Failed to activate all network interfaces, rc = %d", rc);
        }
        // TODO: Error handle this.
        k_sem_take(&network_connected, K_FOREVER);
        break;
    case NetworkState::Disconnected:
        rc = conn_mgr_all_if_disconnect(true);
        if (rc) {
            LOG_ERR("conn_mgr_all_if_disconnect, rc= %d\n", rc);
        }

        break;
    case NetworkState::Activated:
        // TODO: Should we bring up the modem interface, not all possible?
        //       I'm not sure if this has any side effects that I don't
        //       know about.
        // TODO: Is this calling all the proper init functions?
        // TODO: Is this setting us into a high power state?
        rc = conn_mgr_all_if_up(true);
        if (rc) {
            LOG_ERR("conn_mgr_all_if_up, rc = %d\n", rc);
            return rc;
        }
        break;

    case NetworkState::Deactivated:
        rc = conn_mgr_all_if_down(true);
        if (rc) {
            LOG_ERR("conn_mgr_all_if_down, rc = %d\n", rc);
        }
        break;
    default:
        // TODO: Not hard reset the system
        LOG_ERR("Invalid state passed to set_networking_state, state = %d", static_cast<uint32_t>(state));
        k_oops();
    }

    return rc;
}

int networking_init()
{

    /* Setup handler for Zephyr NET Connection Manager events. */
    net_mgmt_init_event_callback(&l4_cb, l4_event_handler, (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED));
    net_mgmt_add_event_callback(&l4_cb);

    /* Setup handler for Zephyr NET Connection Manager Connectivity layer. */
    net_mgmt_init_event_callback(&conn_cb, connectivity_event_handler, (NET_EVENT_CONN_IF_FATAL_ERROR));
    net_mgmt_add_event_callback(&conn_cb);

    int rc = 0;

    // TODO: Better error handling
    // Activate (but don't connect) the modem
    rc |= set_networking_state(NetworkState::Activated);
    rc |= cert_provision();

    return rc;
}

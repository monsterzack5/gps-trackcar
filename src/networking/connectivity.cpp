#include "connectivity.h"

#include <modem/modem_key_mgmt.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include "battery.h"
#include "network_info.h"
#include "tls.h"

LOG_MODULE_REGISTER(connectivity, CONFIG_TRACCAR_DEFAULT_LOG_LEVEL);

K_SEM_DEFINE(network_connected, 0, 1);

// Callbacks
static void connectivity_event_handler(struct net_mgmt_event_callback* cb, uint32_t event,
    struct net_if* iface)
{
    switch (event) {
    case NET_EVENT_CONN_IF_TIMEOUT:
        LOG_ERR("Timeout received from connectivity layer");
        break;
    case NET_EVENT_CONN_IF_FATAL_ERROR:
        LOG_ERR("Fatal error received from connectivity layer");
        break;
    default:
        LOG_ERR("Unknown event recieved from connectivity layer: %u", event);
    }
}

static void l4_event_handler(struct net_mgmt_event_callback* cb, uint32_t event,
    struct net_if* iface)
{
    switch (event) {
    case NET_EVENT_L4_CONNECTED:
        LOG_INF("Network connectivity established and IP address assigned\n");
        k_sem_give(&network_connected);
        break;
    case NET_EVENT_L4_DISCONNECTED:
        LOG_INF("Disconnected from the network\n");
        break;
    default:
        LOG_WRN("Unhandled network event: %u", event);
        break;
    }
}

// I Don't think these need to be global?
static struct net_mgmt_event_callback conn_cb;
static struct net_mgmt_event_callback l4_cb;

int set_networking_state(NetworkState state)
{
    // ---- Calling conn_up on the same state can be slow
    static auto current_state = NetworkState::Deactivated;

    if (current_state == state) {
        LOG_WRN("Requested modem state is the same as the current state!, state = %d", (int)state);
        return 0;
    }

    current_state = state;
    // ----

    int rc = 0;

    switch (state) {
    case NetworkState::Connected:
        rc = conn_mgr_all_if_connect(true);
        if (rc != 0) {
            LOG_ERR("Failed to activate all network interfaces, rc = %d", rc);
            return -1;
        }

        LOG_INF("Started Cell connection, waiting for online event");
        rc = k_sem_take(&network_connected, K_SECONDS(90));
        if (rc == -EAGAIN) {
            LOG_ERR("Timed out waiting for network to come online");
        }
        break;
    case NetworkState::Disconnected:
        rc = conn_mgr_all_if_disconnect(true);
        if (rc) {
            LOG_ERR("conn_mgr_all_if_disconnect, rc= %d\n", rc);
        }

        break;
    case NetworkState::Activated:
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

    // Activate (but don't connect) the modem
    int rc = 0;
    rc |= set_networking_state(NetworkState::Activated);
    rc |= cert_provision();

    return rc;
}


#include "tls.h"

#include <modem/modem_key_mgmt.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

LOG_MODULE_REGISTER(tls, CONFIG_TRACCAR_DEFAULT_LOG_LEVEL);

static const char cert[] = {
#include "root_cert.pem.inc"

    // Needed for Zephyr TLS Functions, not Nordics
    // IF_ENABLED(CONFIG_TLS_CREDENTIALS, (0x00))
};

static const uint32_t TLS_SEC_TAG = 42;
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
        LOG_ERR("Failed to check for certificates err %d\n", err);
        return err;
    }

    if (exists) {
        mismatch = modem_key_mgmt_cmp(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, cert,
            sizeof(cert));
        if (!mismatch) {
            LOG_INF("Certificate match\n");
            return 0;
        }

        LOG_WRN("Certificate mismatch\n");
        err = modem_key_mgmt_delete(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN);
        if (err) {
            LOG_ERR("Failed to delete existing certificate, err %d\n", err);
        }
    }

    LOG_DBG("Provisioning certificate to the modem\n");

    /*  Provision certificate to the modem */
    // This takes a while
    err = modem_key_mgmt_write(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, cert,
        sizeof(cert));
    if (err) {
        LOG_ERR("Failed to provision certificate, err %d\n", err);
        return err;
    }

    return 0;
}

// TODO: Any URL
int tls_setup(int fd)
{
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

    int verify = REQUIRED;

    int err = setsockopt(fd, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
    if (err) {
        LOG_ERR("Failed to setup peer verification, err %d\n", errno);
        return err;
    }

    /* Associate the socket with the security tag
     * we have provisioned the certificate with.
     */
    err = setsockopt(fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag, sizeof(tls_sec_tag));
    if (err) {
        LOG_ERR("Failed to setup TLS sec tag, err %d\n", errno);
        return err;
    }

    err = setsockopt(fd, SOL_TLS, TLS_HOSTNAME, CONFIG_TRACCAR_HOSTNAME,
        sizeof(CONFIG_TRACCAR_HOSTNAME) - 1);
    if (err) {
        LOG_ERR("Failed to setup TLS hostname, err %d\n", errno);
        return err;
    }

    // Session Cache
    int session_cache = TLS_SESSION_CACHE_ENABLED;
    err = setsockopt(fd, SOL_TLS, TLS_SESSION_CACHE, &session_cache, sizeof(session_cache));
    if (err) {
        LOG_ERR("Failed to enable TLS session cache, err %d\n", errno);
        return err;
    }

    return 0;
}

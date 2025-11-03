#pragma once

#include <nrf_modem_gnss.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "net_req.h"
#include "network_info.h"

class PacketBuilder {
public:
    const size_t buf_size = sizeof(buffer);
    bool m_finished = false;

    int build_gps_packet(const nrf_modem_gnss_pvt_data_frame& gps_frame, const ProviderInfo& info, uint8_t battery_soc);

    int append_get()
    {
        int rc = snprintf(&buffer[index], buf_size - index, "%s", "GET ");

        index += rc;
        return 0;
    }
    int append_query_start()
    {
        int rc = snprintf(&buffer[index], buf_size - index, "%s", "/?");
        index += rc;

        return 0;
    }
    int append_null_terminator()
    {
        if (index + 1 < buf_size) {
            buffer[index + 1] = '\0';
        }
        return 0;
    }
    int append_char(const char* append)
    {
        int rc = snprintf(&buffer[index], buf_size - index, "%s", append);
        index += rc;
        return 0;
    }
    int append_newline()
    {
        int rc = snprintf(&buffer[index], buf_size - index, "%s", "\r\n");
        index += rc;
        return 0;
    }
    int append_http1_1()
    {
        int rc = snprintf(&buffer[index], buf_size - index, "%s", " HTTP/1.1\r\n");
        index += rc;
        return 0;
    }
    int append_host_line(const char* url, const char* port)
    {
        int rc = snprintf(&buffer[index], buf_size - index, "Host: %s:%s\r\n", url, port);
        index += rc;
        return 0;
    }
    int append_user_agent_line(const char* agent)
    {
        int rc = snprintf(&buffer[index], buf_size - index, "User-Agent: %s\r\n", agent);
        index += rc;
        return 0;
    }
    int append_connection_close_line()
    {
        int rc = snprintf(&buffer[index], buf_size - index, "%s", "Connection: close\r\n");
        index += rc;
        return 0;
    }
    int append_accept_line()
    {
        int rc = snprintf(&buffer[index], buf_size - index, "%s", "Accept: */*\r\n");
        index += rc;
        return 0;
    }
    int append_query_int(const char* name, int append)
    {
        int rc = snprintf(&buffer[index], buf_size - index, "&%s=%d", name, append);
        index += rc;
        return 0;
    }
    int append_query_bool(const char* name, bool append)
    {
        int rc = 0;

        if (append) {
            rc = snprintf(&buffer[index], buf_size - index, "&%s=%s", name, "true");
        } else {
            rc = snprintf(&buffer[index], buf_size - index, "&%s=%s", name, "false");
        }

        index += rc;
        return 0;
    }
    int append_query_double(const char* name, double append)
    {
        int rc = snprintf(&buffer[index], buf_size - index, "&%s=%f", name, append);
        index += rc;
        return 0;
    }
    int append_query_char(const char* name, const char* append)
    {
        int rc = snprintf(&buffer[index], buf_size - index, "&%s=%s", name, append);
        index += rc;
        return 0;
    }
    int append_query_gps_iso8601(const char* name, const nrf_modem_gnss_datetime& date)
    {
        int rc = snprintf(&buffer[index], buf_size - index, "&%s=%04u-%02u-%02uT%02u:%02u:%02uZ", name,
            date.year,
            date.month,
            date.day,
            date.hour,
            date.minute,
            date.seconds);

        index += rc;
        return 0;
    }
    int append_query_cell_info(const char* name, const ProviderInfo& info)
    {
        int rc = snprintf(&buffer[index], buf_size - index, "&%s=%u,%u,%u,%u,%u", name,
            info.mcc,
            info.mnc,
            info.lac,
            info.cellid,
            info.signal_strength);

        index += rc;
        return 0;
    }

    char* get_raw_buffer(size_t* len)
    {
        if (len != nullptr) {
            *len = index;
        }

        return buffer;
    }

private:
    char buffer[CONFIG_NETWORK_PACKET_SIZE] = { 0 };
    size_t index = 0;
};

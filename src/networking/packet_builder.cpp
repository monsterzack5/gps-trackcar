#include "packet_builder.h"

int PacketBuilder::build_gps_packet(const nrf_modem_gnss_pvt_data_frame& gps_frame, const ProviderInfo& info, uint8_t battery_soc)
{
    append_get();
    append_query_start();

    append_query_char("id", get_imei());
    append_query_double("lat", gps_frame.latitude);
    append_query_double("lon", gps_frame.longitude);
    append_query_double("accuracy", gps_frame.accuracy);
    append_query_double("heading", gps_frame.heading);
    append_query_double("altitude", gps_frame.altitude);
    append_query_gps_iso8601("timestamp", gps_frame.datetime);
    append_query_cell_info("cell", info);

    append_query_int("batt", battery_soc); // Replace with actual battery level
    append_query_char("charge", "false");  // Replace with actual charge status
    append_query_double("temp", 11.11);    // Replace with actual temperature
    append_http1_1();
    append_host_line(CONFIG_TRACCAR_HOSTNAME, CONFIG_TRACCAR_PORT);
    append_connection_close_line();
    append_user_agent_line("curl/8.14.1");
    append_accept_line();
    append_newline();
    append_null_terminator();

    return 0;
}

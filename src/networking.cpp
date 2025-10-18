#include "networking.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include "lte_manager.h"

LOG_MODULE_DECLARE(networking, LOG_LEVEL_DBG);

#define SERVER_HOSTNAME "154.12.118.181"
#define SERVER_PORT 6666
#define RECV_BUF_SIZE 1024

// --- HTTP Request Components ---
// Note: \r\n is the standard line ending for HTTP
// static const char http_request_header[] = "POST /DATAINPUT HTTP/1.1\r\n"
//                                           "Host: " SERVER_HOSTNAME "\r\n"
//                                           "Authorization: Bearer ABC.123DEF\r\n"
//                                           "Content-Type: application/json\r\n"
//                                           "Content-Length: 15\r\n"
//                                           "Connection: close\r\n\r\n";

// static const char http_body[] = "{\"test\": 123}";

static char recv_buf[RECV_BUF_SIZE];

char request[1024] = { 0 };
char query_path[256] = { 0 };

int send_packet(const nrf_modem_gnss_pvt_data_frame& frame)
{

    // /?id=12345&lat=48.8566&lon=2.3522&timestamp=1609459200000

    // 2025-10-17T21:43:18Z

    // printf("%04u-%02u-%02uT%02u:%02u:%02uZ\n",
    //     frame.datetime.year,
    //     frame.datetime.month,
    //     frame.datetime.day,
    //     frame.datetime.hour,
    //     frame.datetime.minute,
    //     frame.datetime.seconds);

    snprintf(query_path, sizeof(query_path),
        "/?id=123456&lat=%f&lon=%f&timestamp=%04u-%02u-%02uT%02u:%02u:%02uZ",
        frame.latitude,
        frame.longitude,
        frame.datetime.year,
        frame.datetime.month,
        frame.datetime.day,
        frame.datetime.hour,
        frame.datetime.minute,
        frame.datetime.seconds);

    lte_set_modem_mode(ModemMode::connect_lte);

    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        query_path, SERVER_HOSTNAME);

    int fd; // Socket file descriptor
    int err;
    struct sockaddr_in addr;

    // 1. Set up the server address
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_HOSTNAME, &addr.sin_addr);

    // 2. Open a TCP socket
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        printk("Failed to create socket: %d\n", errno);
        return -errno;
    }
    printk("Socket created.\n");

    // 3. Connect to the server
    err = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (err < 0) {
        printk("Failed to connect to server: %d\n", errno);
        close(fd);
        return -errno;
    }
    printk("Connected to server %s:%d\n", SERVER_HOSTNAME, SERVER_PORT);

    // 4. Send the HTTP request (header + body)
    err = send(fd, request, sizeof(request), 0);
    if (err < 0) {
        printk("Failed to send HTTP Request: %d\n", errno);
        close(fd);
        return -errno;
    }
    // err = send(fd, http_body, sizeof(http_body) - 1, 0);
    // if (err < 0) {
    //     printk("Failed to send HTTP body: %d\n", errno);
    //     close(fd);
    //     return -errno;
    // }
    // printk("HTTP POST request sent.\n");

    // 5. Receive the response
    int bytes_received = recv(fd, recv_buf, sizeof(recv_buf) - 1, 0);
    if (bytes_received < 0) {
        printk("Failed to receive response: %d\n", errno);
    } else if (bytes_received == 0) {
        printk("Connection closed by server.\n");
    } else {
        recv_buf[bytes_received] = '\0'; // Null-terminate the string
        printk("--- SERVER RESPONSE ---\n%s\n-----------------------\n", recv_buf);
    }

    // 6. Close the socket
    close(fd);
    printk("Socket closed.\n");

    lte_set_modem_mode(ModemMode::deactivate_lte);

    return 0;
}

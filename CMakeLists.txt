# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
project(gps-tracker-test)

# Convert certificate to hex file
set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/certs)
zephyr_include_directories(${gen_dir})

zephyr_include_directories(src/gps)
zephyr_include_directories(src/networking)
zephyr_include_directories(src/sensors)
zephyr_include_directories(src/sync)
zephyr_include_directories(src/util)
zephyr_include_directories(src/sdcard)

generate_inc_file_for_target(
    app
    cert/LetsEncryptCert.pem
    ${gen_dir}/root_cert.pem.inc
)


target_sources(app PRIVATE
    src/main.cpp
    src/startup.c

    src/gps/gps.cpp
    src/gps/assistance.cpp
    
    src/networking/network_info.cpp
    src/networking/connectivity.cpp
    src/networking/dns.cpp
    src/networking/network_requests.cpp
    src/networking/tls.cpp
    src/networking/packet_builder.cpp

    src/sensors/battery.cpp

    src/sdcard/sdcard.cpp
)




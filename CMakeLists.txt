# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)


find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
project(gps-tracker-test)

# Convert certificate to hex file
set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/certs)
zephyr_include_directories(${gen_dir})
generate_inc_file_for_target(
    app
    cert/LetsEncryptCert.pem
    ${gen_dir}/root_cert.pem.inc
)


target_sources(app PRIVATE
    src/main.cpp
    src/startup.c

    src/gps.cpp
    src/networking.cpp

    # src/gps/assistance_minimal.cpp
    # src/gps/mcc_location_table.cpp
)




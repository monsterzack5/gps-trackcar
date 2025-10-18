# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)


find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
project(gps-tracker-test)

target_sources(app PRIVATE
    src/main.cpp
    src/startup.c

    src/lte_manager.cpp
    src/gps.cpp
    src/networking.cpp

    # src/gps/assistance_minimal.cpp
    # src/gps/mcc_location_table.cpp
    # src/gps/gps.cpp

    # src/lte/send_request.cpp
    # src/lte/lte_manager.cpp
)




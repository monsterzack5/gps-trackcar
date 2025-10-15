# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)


find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
project(gps-tracker-test)

target_sources(app PRIVATE
    src/main.c
    src/assistance_minimal.c
    src/mcc_location_table.c
    src/startup.c
)




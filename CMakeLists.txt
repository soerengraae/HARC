cmake_minimum_required(VERSION 3.20.0)
set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/nrf54l15dk_nrf54l05_cpuapp.overlay")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(VCPcontroller)

target_sources(app PRIVATE
    src/main.c
    src/ble_manager.c
    src/vcp_controller.c
    src/battery_reader.c
    src/csip_coordinator.c
    src/app_controller.c
    src/devices_manager.c
    src/has_controller.c
    src/display_manager.c
    src/power_manager.c
    src/button_manager.c
)

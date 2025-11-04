#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <zephyr/logging/log.h>
#include "battery_reader.h"

int display_manager_init(void);

void display_update_connection(bool connected, uint8_t device_id);

void display_refresh_periodic(void);

#endif // DISPLAY_MANAGER_H
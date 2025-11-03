#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <zephyr/logging/log.h>

int display_manager_init(void);

void display_update_battery(unt8_t battery_level, uint8_t device_id);

void display_update_volume(uint8_t volume, uint8_t mute, uint8_t device_id);

void display_update_connection(bool connected, uint8_t device_id);

#endif // DISPLAY_MANAGER_H
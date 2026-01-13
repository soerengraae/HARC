#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include "ble_manager.h"
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/settings/settings.h>

/* Connection state machine states for dual-device coordination */
enum sm_state {
    SM_IDLE,            /* Normal operation, waiting for user input */
    SM_WAKE,            /* Just woke up, determining next state */
    SM_FIRST_TIME_USE,  /* Initial pairing with new hearing aids */
    SM_BONDED_DEVICES,  /* Reconnecting to bonded devices */
    SM_POWER_OFF,       /* Shutting down */
};

#define APP_CONTROLLER_PAIRING_TIMEOUT K_SECONDS(30)
#define APP_CONTROLLER_ACTION_TIMEOUT K_SECONDS(10)

int8_t app_controller_notify_system_ready();
int8_t app_controller_notify_device_connected(uint8_t device_id);
int8_t app_controller_notify_device_disconnected(uint8_t device_id);
int8_t app_controller_notify_bonds_cleared();
int8_t app_controller_notify_device_ready(uint8_t device_id);
int8_t app_controller_notify_scan_complete();
int8_t app_controller_notify_csip_discovered(uint8_t device_id, int8_t err);
int8_t app_controller_notify_csip_member_match(uint8_t device_id, int8_t err, const bt_addr_le_t *addr);
int8_t app_controller_notify_bas_discovered(uint8_t device_id, int err);
int8_t app_controller_notify_vcp_discovered(uint8_t device_id, int err);
int8_t app_controller_notify_vcp_state_read(uint8_t device_id, int err);
int8_t app_controller_notify_volume_up_button_pressed();
int8_t app_controller_notify_volume_down_button_pressed();
int8_t app_controller_notify_pair_button_pressed();
int8_t app_controller_notify_preset_button_pressed();
int8_t app_controller_notify_clear_bonds_button_pressed();
int8_t app_controller_notify_has_discovered(uint8_t device_id, int err);
int8_t app_controller_notify_has_presets_read(uint8_t device_id, int err);
int8_t app_controller_notify_has_read_presets();
int8_t app_controller_notify_power_off();

#endif /* CONNECTION_MANAGER_H */
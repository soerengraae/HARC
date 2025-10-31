#ifndef VCP_CONTROLLER_H
#define VCP_CONTROLLER_H

#include "ble_manager.h"
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/vcp.h>
#include <zephyr/logging/log.h>

int vcp_controller_init(struct device_context *conn_ctx);
int vcp_cmd_discover(struct device_context *conn_ctx);
int vcp_cmd_volume_up(struct device_context *conn_ctx);
int vcp_cmd_volume_down(struct device_context *conn_ctx);
int vcp_cmd_set_volume(struct device_context *conn_ctx, uint8_t volume);
int vcp_cmd_mute(struct device_context *conn_ctx);
int vcp_cmd_unmute(struct device_context *conn_ctx);
int vcp_cmd_read_state(struct device_context *conn_ctx);
int vcp_cmd_read_flags(struct device_context *conn_ctx);
void vcp_controller_reset(struct device_context *conn_ctx);

/* Global state */
extern bool volume_direction;

#endif // VCP_CONTROLLER_H
#include "vcp_controller.h"

LOG_MODULE_REGISTER(vcp_controller, LOG_LEVEL_DBG);

static struct device_context *current_device_ctx;

bool volume_direction = true;

int vcp_cmd_discover(struct device_context *conn_ctx)
{
    current_device_ctx = conn_ctx;
    return bt_vcp_vol_ctlr_discover(conn_ctx->conn, &conn_ctx->vol_ctlr);
}

int vcp_cmd_read_state(struct device_context *conn_ctx)
{
    current_device_ctx = conn_ctx;
    return bt_vcp_vol_ctlr_read_state(conn_ctx->vol_ctlr);
}

int vcp_cmd_read_flags(struct device_context *conn_ctx)
{
    current_device_ctx = conn_ctx;
    return bt_vcp_vol_ctlr_read_flags(conn_ctx->vol_ctlr);
}

int vcp_cmd_volume_up(struct device_context *conn_ctx)
{
    current_device_ctx = conn_ctx;
    return bt_vcp_vol_ctlr_vol_up(conn_ctx->vol_ctlr);
}

int vcp_cmd_volume_down(struct device_context *conn_ctx)
{
    current_device_ctx = conn_ctx;
    return bt_vcp_vol_ctlr_vol_down(conn_ctx->vol_ctlr);
}

int vcp_cmd_set_volume(struct device_context *conn_ctx, uint8_t volume)
{
    current_device_ctx = conn_ctx;
    return bt_vcp_vol_ctlr_set_vol(conn_ctx->vol_ctlr, volume);
}

int vcp_cmd_mute(struct device_context *conn_ctx)
{
    current_device_ctx = conn_ctx;
    return bt_vcp_vol_ctlr_mute(conn_ctx->vol_ctlr);
}

int vcp_cmd_unmute(struct device_context *conn_ctx)
{
    current_device_ctx = conn_ctx;
    return bt_vcp_vol_ctlr_unmute(conn_ctx->vol_ctlr);
}

static void vcp_state_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err, uint8_t volume, uint8_t mute)
{
    if (err) {
        LOG_ERR("VCP state error (err %d)", err);
        ble_cmd_complete(err);
        return;
    }
    
    float volume_percent = (float)volume * 100.0f / 255.0f;
    if (current_ble_cmd && current_ble_cmd->type == BLE_CMD_VCP_READ_STATE) {
        LOG_INF("VCP state read: Volume: %u%%, Mute: %u", (uint8_t)(volume_percent), mute);
    } else {
        LOG_DBG("VCP state notification: Volume: %u%%, Mute: %u", (uint8_t)(volume_percent), mute);
    }

    if (volume >= 255) {
        volume_direction = false;
    } else if (volume <= 0) {
        volume_direction = true;
    }

    // Mark as complete only if this was a READ_STATE command
    if (current_ble_cmd && current_ble_cmd->type == BLE_CMD_VCP_READ_STATE) {
        ble_cmd_complete(0);
    }
}

static void vcp_flags_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err, uint8_t flags)
{
    if (err) {
        LOG_ERR("VCP flags error (err %d)", err);
        ble_cmd_complete(err);
        return;
    }

    LOG_DBG("VCP flags: 0x%02X", flags);
    
    // Mark as complete only if this was a READ_FLAGS command as it could also be a notification
    // in which case we don't want to accidentally complete a different command
    if (current_ble_cmd && current_ble_cmd->type == BLE_CMD_VCP_READ_FLAGS) {
        ble_cmd_complete(0);
    }
}

static void vcp_discover_cb(struct bt_vcp_vol_ctlr *vcp_vol_ctlr, int err,
                uint8_t vocs_count, uint8_t aics_count)
{
    if (err) {
        LOG_ERR("VCP discovery failed (err %d)", err);
        ble_cmd_complete(err);
        return;
    }

    LOG_INF("VCP discovery complete");

    current_device_ctx->vol_ctlr = vcp_vol_ctlr;

	current_device_ctx->info.vcp_discovered = true;

    // Initial flag read
	ble_cmd_vcp_read_flags(current_device_ctx->device_id, true);

	// Mark discovery command as complete
	ble_cmd_complete(0);
}

static void vcp_vol_down_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    if (err) {
        LOG_ERR("VCP volume down error (err %d)", err);
    } else {
        LOG_INF("Volume down success");
    }
    
    ble_cmd_complete(err);
}

static void vcp_vol_up_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    if (err) {
        LOG_ERR("VCP volume up error (err %d)", err);
    } else {
        LOG_INF("Volume up success");
    }

    ble_cmd_complete(err);
}

static void vcp_mute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    if (err) {
        LOG_ERR("VCP mute error (err %d)", err);
    } else {
        LOG_INF("Mute success");
    }

    ble_cmd_complete(err);
}

static void vcp_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    if (err) {
        LOG_ERR("VCP unmute error (err %d)", err);
    } else {
        LOG_INF("Unmute success");
    }

    ble_cmd_complete(err);
}

static void vcp_vol_up_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    if (err) {
        LOG_ERR("VCP volume up and unmute error (err %d)", err);
    } else {
        LOG_INF("Volume up and unmute success");
    }

    ble_cmd_complete(err);
}

static void vcp_vol_down_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    if (err) {
        LOG_ERR("VCP volume down and unmute error (err %d)", err);
    } else {
        LOG_INF("Volume down and unmute success");
    }

    ble_cmd_complete(err);
}

static struct bt_vcp_vol_ctlr_cb vcp_callbacks = {
    .state = vcp_state_cb,
    .flags = vcp_flags_cb,
    .discover = vcp_discover_cb,
    .vol_down = vcp_vol_down_cb,
    .vol_up = vcp_vol_up_cb,
    .mute = vcp_mute_cb,
    .unmute = vcp_unmute_cb,
    .vol_up_unmute = vcp_vol_up_unmute_cb,
    .vol_down_unmute = vcp_vol_down_unmute_cb,
    .vol_set = NULL,
};

/* Initialize VCP controller */
int vcp_controller_init(struct device_context *device_ctx)
{
    int err;

    err = bt_vcp_vol_ctlr_cb_register(&vcp_callbacks);
    if (err) {
        LOG_ERR("Failed to register VCP callbacks (err %d)", err);
        return err;
    }

    vcp_controller_reset(device_ctx);

    LOG_INF("VCP controller initialized");

    return 0;
}

/* Reset VCP controller state */
void vcp_controller_reset(struct device_context *conn_ctx)
{
    conn_ctx->info.vcp_discovered = false;
    conn_ctx->vol_ctlr = NULL;
}
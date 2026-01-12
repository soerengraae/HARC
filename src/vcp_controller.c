#include "vcp_controller.h"
#include "vcp_settings.h"
#include "devices_manager.h"
#include "app_controller.h"
#include "ble_manager.h"
#include "display_manager.h"

LOG_MODULE_REGISTER(vcp_controller, LOG_LEVEL_INF);

/* Track whether handles were loaded from cache (per device) - skip re-storing if true */
static bool handles_from_cache[CONFIG_BT_MAX_CONN];

static struct device_context *get_device_context_by_vol_ctlr(struct bt_vcp_vol_ctlr *vol_ctlr);

int vcp_cmd_discover(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    vcp_controller_reset(device_id);

    /* Reset cache flag - will be set if handles are successfully loaded from cache */
    handles_from_cache[device_id] = false;

    /* Try to load cached handles first */
    struct bt_vcp_vol_ctlr_handles cached_handles;
    int load_err = vcp_settings_load_handles(&ctx->info.addr, &cached_handles);
    if (load_err == 0) {
        LOG_INF("Loaded cached VCP handles [DEVICE ID %d]", device_id);
        int inject_err = bt_vcp_vol_ctlr_set_handles(ctx->conn, &cached_handles);
        if (inject_err != 0) {
            // Here if subscibtion fails
            LOG_WRN("Failed to inject cached VCP handles (err %d), proceeding with full discovery", inject_err);
            vcp_settings_clear_handles(&ctx->info.addr);
        } else {
            LOG_INF("Cached handles restored successfully");
            handles_from_cache[device_id] = true;
        }
    }

    return bt_vcp_vol_ctlr_discover(ctx->conn, &ctx->vcp_ctlr.vol_ctlr);
}

int vcp_cmd_read_state(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_read_state(ctx->vcp_ctlr.vol_ctlr);
}

int vcp_cmd_read_flags(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_read_flags(ctx->vcp_ctlr.vol_ctlr);
}

int vcp_cmd_volume_up(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_vol_up(ctx->vcp_ctlr.vol_ctlr);
}

int vcp_cmd_volume_down(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_vol_down(ctx->vcp_ctlr.vol_ctlr);
}

int vcp_cmd_set_volume(uint8_t device_id, uint8_t volume)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_set_vol(ctx->vcp_ctlr.vol_ctlr, volume);
}

int vcp_cmd_mute(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_mute(ctx->vcp_ctlr.vol_ctlr);
}

int vcp_cmd_unmute(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);
    return bt_vcp_vol_ctlr_unmute(ctx->vcp_ctlr.vol_ctlr);
}

static void vcp_state_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err, uint8_t volume, uint8_t mute)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);
    if (err) {
        LOG_ERR("VCP state error (err %d) [DEVICE ID %d]", err, ctx->device_id);
        ble_cmd_complete(ctx->device_id, err);
        return;
    }

    ctx->vcp_ctlr.state.volume = volume;
    ctx->vcp_ctlr.state.mute = mute;

    float volume_percent = (float)ctx->vcp_ctlr.state.volume * 100.0f / 255.0f;

    /* Update display with current volume state */
    display_manager_update_volume(ctx->device_id, volume, mute);

    // Mark as complete only if this was a READ_STATE command
    if (ctx->current_ble_cmd && ctx->current_ble_cmd->type == BLE_CMD_VCP_READ_STATE) {
        LOG_INF("VCP state read: Volume: %u%%, Mute: %u [DEVICE ID %d]", (uint8_t)(volume_percent), ctx->vcp_ctlr.state.mute, ctx->device_id);
        app_controller_notify_vcp_state_read(ctx->device_id, 0);
        ble_cmd_complete(ctx->device_id, 0);
    } else {
        LOG_DBG("VCP state notification: Volume: %u%%, Mute: %u [DEVICE ID %d]", (uint8_t)(volume_percent), ctx->vcp_ctlr.state.mute, ctx->device_id);
    }
}

static void vcp_flags_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err, uint8_t flags)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);
    if (err) {
        LOG_ERR("VCP flags error (err %d) [DEVICE ID %d]", err, ctx->device_id);
        ble_cmd_complete(ctx->device_id, err);
        return;
    }

    LOG_DBG("VCP flags: 0x%02X [DEVICE ID %d]", flags, ctx->device_id);
    
    // Mark as complete only if this was a READ_FLAGS command as it could also be a notification
    // in which case we don't want to accidentally complete a different command
    if (ctx->current_ble_cmd && ctx->current_ble_cmd->type == BLE_CMD_VCP_READ_FLAGS) {
        ble_cmd_complete(ctx->device_id, 0);
    }
}

static void vcp_discover_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err,
                uint8_t vocs_count, uint8_t aics_count)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);

    if (err) {
        LOG_ERR("VCP discovery failed (err %d) [DEVICE ID %d]", err, ctx->device_id);
        app_controller_notify_vcp_discovered(ctx->device_id, err);
        ble_cmd_complete(ctx->device_id, err);
        return;
    }

    LOG_INF("VCP discovery complete [DEVICE ID %d]", ctx->device_id);

    ctx->vcp_ctlr.vol_ctlr = vol_ctlr;
    ctx->info.vcp_discovered = true;

    /* Only extract and cache handles if they weren't loaded from cache.
     * This avoids unnecessary stack usage from settings operations when
     * handles are already in NVS. */
    if (!handles_from_cache[ctx->device_id]) {
        /* Cache handles for future reconnections */
        struct bt_vcp_vol_ctlr_handles handles;
        int get_err = bt_vcp_vol_ctlr_get_handles(vol_ctlr, &handles);
        if (get_err == 0) {
            /* Store handles for the current device */
            vcp_settings_store_handles(&ctx->info.addr, &handles);

            /* If this is a new device (initial pairing) and part of a CSIP set, also store handles
             * for all other set members. Since all hearing aids in the set have identical firmware
             * and GATT layout, we can reuse the same handles for all set members.
             * Skip this on reconnection to avoid unnecessary work and potential stack issues. */
            // if (ctx->info.is_new_device) {
                struct bond_collection collection;
                if (devices_manager_get_bonded_devices_collection(&collection) == 0) {
                    struct bonded_device_entry current_entry;
                    if (devices_manager_find_bonded_entry_by_addr(&ctx->info.addr, &current_entry) &&
                        current_entry.is_set_member) {

                    LOG_DBG("Current device is CSIP set member, caching VCP handles for all set members");

                    for (uint8_t i = 0; i < collection.count; i++) {
                        /* Skip the current device (already stored) */
                        if (bt_addr_le_cmp(&collection.devices[i].addr, &ctx->info.addr) == 0) {
                            continue;
                        }

                        /* Only store for devices in the same CSIP set */
                        if (collection.devices[i].is_set_member &&
                            memcmp(collection.devices[i].sirk, current_entry.sirk, CSIP_SIRK_SIZE) == 0) {

                            int err = vcp_settings_store_handles(&collection.devices[i].addr, &handles);
                            if (err == 0) {
                                char addr_str[BT_ADDR_LE_STR_LEN];
                                bt_addr_le_to_str(&collection.devices[i].addr, addr_str, sizeof(addr_str));
                                LOG_INF("VCP handles also cached for set member: %s", addr_str);
                            } else {
                                LOG_WRN("Failed to cache VCP handles for set member (err %d)", err);
                            }
                        }
                    }
                }
                }
            // }
        } else {
            LOG_WRN("Failed to get VCP handles for caching (err %d)", get_err);
        }
    } else {
        LOG_DBG("Handles were loaded from cache, skipping re-storage");
    }

    // Mark discovery command as complete
    app_controller_notify_vcp_discovered(ctx->device_id, err);
    ble_cmd_complete(ctx->device_id, 0);
}

static void vcp_vol_down_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);

    if (err) {
        LOG_ERR("VCP volume down error (err %d) [DEVICE ID %d]", err, ctx->device_id);
    } else {
        LOG_INF("Volume down success [DEVICE ID %d]", ctx->device_id);
    }
    
    ble_cmd_complete(ctx->device_id, err);
}

static void vcp_vol_up_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);

    if (err) {
        LOG_ERR("VCP volume up error (err %d) [DEVICE ID %d]", err, ctx->device_id);
    } else {
        LOG_INF("Volume up success [DEVICE ID %d]", ctx->device_id);
    }

    ble_cmd_complete(ctx->device_id, err);
}

static void vcp_mute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);
    
    if (err) {
        LOG_ERR("VCP mute error (err %d) [DEVICE ID %d]", err, ctx->device_id);
    } else {
        LOG_INF("Mute success [DEVICE ID %d]", ctx->device_id);
    }

    ble_cmd_complete(ctx->device_id, err);
}

static void vcp_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);

    if (err) {
        LOG_ERR("VCP unmute error (err %d) [DEVICE ID %d]", err, ctx->device_id);
    } else {
        LOG_INF("Unmute success [DEVICE ID %d]", ctx->device_id);
    }

    ble_cmd_complete(ctx->device_id, err);
}

static void vcp_vol_up_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);

    if (err) {
        LOG_ERR("VCP volume up and unmute error (err %d) [DEVICE ID %d]", err, ctx->device_id);
    } else {
        LOG_INF("Volume up and unmute success [DEVICE ID %d]", ctx->device_id);
    }

    ble_cmd_complete(ctx->device_id, err);
}

static void vcp_vol_down_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
    struct device_context *ctx = get_device_context_by_vol_ctlr(vol_ctlr);
    
    if (err) {
        LOG_ERR("VCP volume down and unmute error (err %d) [DEVICE ID %d]", err, ctx->device_id);
    } else {
        LOG_INF("Volume down and unmute success [DEVICE ID %d]", ctx->device_id);
    }

    ble_cmd_complete(ctx->device_id, err);
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
int vcp_controller_init(void)
{
    int err;

    err = bt_vcp_vol_ctlr_cb_register(&vcp_callbacks);
    if (err) {
        LOG_ERR("Failed to register VCP callbacks (err %d)", err);
        return err;
    }

    LOG_INF("VCP controller initialized");

    return 0;
}

/* Reset VCP controller state */
void vcp_controller_reset(uint8_t device_id)
{
    struct device_context *ctx = devices_manager_get_device_context_by_id(device_id);

    ctx->info.vcp_discovered = false;
    ctx->vcp_ctlr.vol_ctlr = NULL;
    handles_from_cache[device_id] = false;

    LOG_DBG("VCP controller state reset [DEVICE ID %d]", ctx->device_id);
}

static struct device_context *get_device_context_by_vol_ctlr(struct bt_vcp_vol_ctlr *vol_ctlr)
{
    struct bt_conn *conn = NULL;
    bt_vcp_vol_ctlr_conn_get(vol_ctlr, &conn);

    return devices_manager_get_device_context_by_conn(conn);
}
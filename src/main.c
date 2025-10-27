#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

#include "ble_manager.h"
#include "vcp_controller.h"
#include "battery_reader.h"
#include "csip.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    int err;

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        err = settings_subsys_init();
        if (err) {
            LOG_ERR("Settings init failed (err %d)", err);
        }

        err = settings_load();
        if (err) {
            LOG_ERR("Settings load failed (err %d)", err);
        }
    }

    err = vcp_controller_init();
	if (err) {
		LOG_ERR("VCP controller init failed (err %d)", err);
		return err;
	}

    /* Initialize Bluetooth */
    err = bt_enable(bt_ready_cb);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }

    while (1) {
        k_sleep(K_SECONDS(5));

        /* Log CSIP set information if we have a connection */
        if (conn_ctx && conn_ctx->conn) {
            if (csip_is_device_in_set(conn_ctx->conn)) {
                static bool logged_set_info = false;
                static bool logged_all_sets = false;
                if (!logged_set_info) {
                    csip_log_set_info(conn_ctx->conn);
                    logged_set_info = true;
                }

                /* Log other set members if any discovered */
                if (!logged_all_sets && csip_get_known_set_count() > 0) {
                    bt_addr_le_t other_members[CONFIG_BT_MAX_CONN];
                    size_t other_count = csip_get_other_set_members(conn_ctx->conn, other_members, CONFIG_BT_MAX_CONN);

                    if (other_count > 0) {
                        LOG_INF("Found %zu other set members:", other_count);
                        for (size_t i = 0; i < other_count; i++) {
                            char addr_str[BT_ADDR_LE_STR_LEN];
                            bt_addr_le_to_str(&other_members[i], addr_str, sizeof(addr_str));
                            LOG_INF("  Other member %zu: %s", i + 1, addr_str);
                        }
                    }

                    csip_log_all_sets();
                    logged_all_sets = true;
                }
            }
        }

        if (vcp_discovered && vol_ctlr) {
            LOG_DBG("Queueing VCP Volume Change");
            if (volume_direction) {
                ble_cmd_vcp_volume_up();
                ble_cmd_vcp_volume_up();
            } else {
                ble_cmd_vcp_volume_down();
                ble_cmd_vcp_volume_down();
            }
        }

        if (battery_discovered) {
            ble_cmd_bas_read_level();
        }
    }

    return 0;
}
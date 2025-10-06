#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

#include "ble_manager.h"
#include "vcp_controller.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

int main(void)
{
	int err;

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_subsys_init();
        if (err) {
            LOG_ERR("Settings init failed (err %d)", err);
        }
    }

	/* Initialize Bluetooth */
	err = bt_enable(bt_ready);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	while (1) {
		k_sleep(K_SECONDS(5));

		if (vcp_discovered && vol_ctlr) {
			LOG_DBG("Attempting to write VCP Volume Up (vol_ctlr=%p, default_conn=%p)", vol_ctlr, default_conn);
			
			if (volume_direction)
				vcp_volume_up();
			else
				vcp_volume_down();
		}
	}

	return 0;
}
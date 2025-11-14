#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>

#include "ble_manager.h"
#include "csip_coordinator.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    int err;

	err = csip_coordinator_init();
	if (err)
	{
		LOG_ERR("CSIP coordinator init failed (err %d)", err);
		return err;
	}

    /* Initialize Bluetooth */
    err = bt_enable(bt_ready_cb);

    while (!ble_manager_ready);

    csip_coordinator_init();
    csip_coordinator_rsi_scan_start(0);

    while (1)
    {
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
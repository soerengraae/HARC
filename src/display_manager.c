#include "display_manager.h"
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(display_manager, LOG_LEVEL_INF);

static const struct device *display_dev;

static struct{
   bool connected;
} device_state[2] = {0};


static void display_refresh(void)
{
    char buf[32];

    cfb_framebuffer_clear(display_dev, false);


    // Show data if available
    snprintf(buf, sizeof(buf), "Bat: %d%%", battery_level);
    cfb_print(display_dev, buf, 0, 18);

    // Convert volume from 0-255 to 0-100%
    uint8_t volume_percent = (uint8_t)((current_volume * 100) / 255);
    snprintf(buf, sizeof(buf), "Vol: %d%%%s", volume_percent, current_mute ? " (M)" : "");
    cfb_print(display_dev, buf, 0, 36);

    cfb_framebuffer_finalize(display_dev);
}

int display_manager_init(void)
{
    int err;

    LOG_INF("Display init: Getting device");
    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device not ready");
        return -ENODEV;
    }

    LOG_INF("Display init: Device ready, initializing framebuffer");
    err = cfb_framebuffer_init(display_dev);
    if (err) {
        LOG_ERR("Failed to init framebuffer (err %d)", err);
        return err;
    }

    LOG_INF("Display init: Clearing framebuffer");
    cfb_framebuffer_clear(display_dev, true);

    LOG_INF("Display init: Setting font");
    err = cfb_framebuffer_set_font(display_dev,0);
    if (err) {
        LOG_ERR("Failed to set font (err %d)", err);
        return err;
    }

    LOG_INF("Display init: Printing text");
    cfb_print(display_dev, "HARC Remote", 0, 0);
    cfb_print(display_dev, "Initializing...", 0, 16);

    LOG_INF("Display init: Finalizing framebuffer");
    cfb_framebuffer_finalize(display_dev);

    LOG_INF("Display initialized successfully");
    return 0;
}



void display_update_connection(bool connected, uint8_t device_id)
{
    if (device_id > 1) {
        LOG_WRN("Invalid device ID: %d", device_id);
        return;
    }
    
    device_state[device_id].connected = connected;
    LOG_INF("Connection: %s [Device %d]", 
            connected ? "Connected" : "Disconnected", device_id);
    
    display_refresh();
}

void display_refresh_periodic(void)
{
    display_refresh();
}
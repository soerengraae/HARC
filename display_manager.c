#include "display_manager.h"
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(display_manager, LOG_LEVEL_INF);

static const struct device *display_dev;

static struct{
    uint8_t battery_level;
    uint8_t volume;
    uint8_t mute;
    bool connected;
} device_state[2] = {0};


static void display_refresh(void)
{
    char buf[32];

    cfb_framebuffer_clear(display_dev, false);

    cfb_print(display_dev, "HARC Remote", 0, 0);

    if (device_state[0].connected){
        cfb_print(display_dev, "D!: Connected",0, 16);

        snprintf(buf, sizeof(buf), "Vol: %d%% %s",
                    device_state[0].volume,
                    device_state0.mute ? "MUTE" : "");
        cfb_print(display_dev, buf, 0, 24);


        snprintf(buf, sizeof(buf), "Bat: %d%%", device_state[0].battery_level);
        cfb_print(display_dev, buf, 0, 32);

    } else {
        cfb_print(display_dev, "D1: Disconnected",0, 16);
    }   
    cfb_framebuffer_finalize(display_dev);
}

int display_manager_init(void)
{
    int err;

    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device not ready");
        return -ENODEV;
    }

    LOG_INF("display device ready");
    err = cfb_framebuffer_init(display_dev);
    if (err) {
        LOG_ERR("Failed to init framebuffer (err %d)", err);
        return err;
    }
    cfb_framebuffer_clear(display_dev, true);

    err = cfb_framebuffer_set_font(display_dev,0);
    if (err) {
        LOG_ERR("Failed to set font (err %d)", err);
        return err;
    }

    cfb_print(display_dev, "HARC Remote", 0, 0);
    cfb_print(display_dev, "Initializing...", 0, 16);
    cfb_framebuffer_finalize(display_dev);

    LOG_INF("Display initialized");
    return 0;
}

void display_update_battery(uint8_t battery_level, uint8_t device_id)
{
    if (device_id > 1) {
        LOG_WRN("Invalid device ID: %d", device_id);
        return;
    }
    
    device_state[device_id].battery_level = battery_level;
    LOG_DBG("Battery: %d%% [Device %d]", battery_level, device_id);
    
    display_refresh();
}

void display_update_volume(uint8_t volume, uint8_t mute, uint8_t device_id)
{
    if (device_id > 1) {
        LOG_WRN("Invalid device ID: %d", device_id);
        return;
    }
    
    device_state[device_id].volume = volume;
    device_state[device_id].mute = mute;
    LOG_DBG("Volume: %d%% %s [Device %d]", 
            volume, mute ? "MUTE" : "", device_id);
    
    display_refresh();
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
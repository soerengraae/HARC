#include "display_manager.h"
#include "devices_manager.h"
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(display_manager, LOG_LEVEL_INF);

/* Display device */
static const struct device *display_dev;

/* Display dimensions (in characters for CFB) */
#define DISPLAY_ROWS 8
#define DISPLAY_COLS 16

/* Display state for both hearing aids */
struct display_state {
	char connection_state[16];
	uint8_t volume;
	bool mute;
	uint8_t battery_level;
	bool has_data;
};

static struct display_state device_display_state[2] = {0};
static struct k_mutex display_mutex;
static bool display_initialized = false;

/* Initialize the display */
int display_manager_init(void)
{
	k_mutex_init(&display_mutex);

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return -ENODEV;
	}

	/* Initialize character framebuffer */
	int err = cfb_framebuffer_init(display_dev);
	if (err) {
		LOG_ERR("Character framebuffer init failed (err %d)", err);
		return err;
	}

	/* Clear the display */
	err = cfb_framebuffer_clear(display_dev, true);
	if (err) {
		LOG_ERR("Failed to clear framebuffer (err %d)", err);
		return err;
	}

	/* Set font - try to get the smallest available font */
	err = cfb_framebuffer_set_font(display_dev, 0);
	if (err) {
		LOG_ERR("Failed to set font (err %d)", err);
		return err;
	}

	/* Get display properties */
	uint16_t rows = cfb_get_display_parameter(display_dev, CFB_DISPLAY_ROWS);
	uint16_t cols = cfb_get_display_parameter(display_dev, CFB_DISPLAY_COLS);
	uint16_t ppt = cfb_get_display_parameter(display_dev, CFB_DISPLAY_PPT);

	LOG_INF("Display initialized: %ux%u chars, %u ppt", cols, rows, ppt);

	/* Initialize display state */
	for (int i = 0; i < 2; i++) {
		strncpy(device_display_state[i].connection_state, "DISC", sizeof(device_display_state[i].connection_state));
		device_display_state[i].volume = 0;
		device_display_state[i].mute = false;
		device_display_state[i].battery_level = 0;
		device_display_state[i].has_data = false;
	}

	display_initialized = true;

	/* Show initial splash screen */
	display_manager_show_status("HARC Ready");

	LOG_INF("Display manager initialized");
	return 0;
}

void display_manager_clear(void)
{
	if (!display_initialized) {
		return;
	}

	k_mutex_lock(&display_mutex, K_FOREVER);
	cfb_framebuffer_clear(display_dev, true);
	cfb_framebuffer_finalize(display_dev);
	k_mutex_unlock(&display_mutex);
}

void display_manager_show_status(const char *message)
{
	if (!display_initialized) {
		return;
	}

	k_mutex_lock(&display_mutex, K_FOREVER);

	cfb_framebuffer_clear(display_dev, false);

	/* Center the message */
	cfb_print(display_dev, message, 0, 32);

	cfb_framebuffer_finalize(display_dev);
	k_mutex_unlock(&display_mutex);
}

void display_manager_update_connection_state(uint8_t device_id, const char *state)
{
	if (device_id > 1 || !display_initialized) {
		return;
	}

	k_mutex_lock(&display_mutex, K_FOREVER);
	strncpy(device_display_state[device_id].connection_state, state,
	        sizeof(device_display_state[device_id].connection_state) - 1);
	device_display_state[device_id].connection_state[sizeof(device_display_state[device_id].connection_state) - 1] = '\0';
	device_display_state[device_id].has_data = true;
	k_mutex_unlock(&display_mutex);

	display_manager_update();
}

void display_manager_update_volume(uint8_t device_id, uint8_t volume, uint8_t mute)
{
	if (device_id > 1 || !display_initialized) {
		return;
	}

	k_mutex_lock(&display_mutex, K_FOREVER);
	device_display_state[device_id].volume = volume;
	device_display_state[device_id].mute = mute;
	device_display_state[device_id].has_data = true;
	k_mutex_unlock(&display_mutex);

	display_manager_update();
}

void display_manager_update_battery(uint8_t device_id, uint8_t battery_level)
{
	if (device_id > 1 || !display_initialized) {
		return;
	}

	k_mutex_lock(&display_mutex, K_FOREVER);
	device_display_state[device_id].battery_level = battery_level;
	device_display_state[device_id].has_data = true;
	k_mutex_unlock(&display_mutex);

	display_manager_update();
}

/* Helper function to draw a volume bar */
static void draw_volume_bar(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t volume, bool mute)
{
	struct display_buffer_descriptor buf_desc;
	uint8_t bar_buffer[128];

	memset(bar_buffer, 0x00, sizeof(bar_buffer));

	if (mute) {
		/* Draw empty bar with 'M' for mute */
		for (int i = 0; i < width; i++) {
			bar_buffer[i] = 0x81; /* Top and bottom border */
		}
	} else {
		/* Calculate filled width based on volume (0-255) */
		uint16_t filled_width = (width * volume) / 255;

		/* Draw bar outline */
		for (int i = 0; i < width; i++) {
			if (i < filled_width) {
				bar_buffer[i] = 0xFF; /* Filled */
			} else {
				bar_buffer[i] = 0x81; /* Empty (top and bottom border) */
			}
		}
	}

	buf_desc.buf_size = width;
	buf_desc.width = width;
	buf_desc.height = height;
	buf_desc.pitch = width;

	display_write(display_dev, x, y, &buf_desc, bar_buffer);
}

void display_manager_update(void)
{
	if (!display_initialized) {
		return;
	}

	k_mutex_lock(&display_mutex, K_FOREVER);

	char line_buf[32];

	/* Clear the display */
	cfb_framebuffer_clear(display_dev, false);

	/* Top row: Battery levels in corners */
	/* Left battery */
	snprintf(line_buf, sizeof(line_buf), "L:%u%%", device_display_state[0].battery_level);
	cfb_print(display_dev, line_buf, 0, 0);

	/* Right battery - positioned at right corner */
	snprintf(line_buf, sizeof(line_buf), "R:%u%%", device_display_state[1].battery_level);
	cfb_print(display_dev, line_buf, 80, 0);

	/* Connection states below batteries */
	snprintf(line_buf, sizeof(line_buf), "%-6s", device_display_state[0].connection_state);
	cfb_print(display_dev, line_buf, 0, 16);

	snprintf(line_buf, sizeof(line_buf), "%-6s", device_display_state[1].connection_state);
	cfb_print(display_dev, line_buf, 80, 16);

	/* Determine if we show one or two volume bars */
	bool same_volume = (device_display_state[0].volume == device_display_state[1].volume &&
	                    device_display_state[0].mute == device_display_state[1].mute &&
	                    device_display_state[0].has_data && device_display_state[1].has_data);

	if (same_volume && device_display_state[0].has_data) {
		/* Single volume bar across the display */
		cfb_print(display_dev, "Volume:", 0, 32);

		if (device_display_state[0].mute) {
			cfb_print(display_dev, "[====MUTED====]", 0, 48);
		} else {
			/* Draw text-based volume bar */
			uint8_t vol_percent = (uint8_t)((device_display_state[0].volume * 100) / 255);
			int bar_length = (vol_percent * 14) / 100; /* 14 characters for bar */

			line_buf[0] = '[';
			for (int i = 0; i < 14; i++) {
				line_buf[i + 1] = (i < bar_length) ? '=' : ' ';
			}
			line_buf[15] = ']';
			line_buf[16] = '\0';

			cfb_print(display_dev, line_buf, 0, 48);

			/* Show percentage */
			snprintf(line_buf, sizeof(line_buf), "%u%%", vol_percent);
			cfb_print(display_dev, line_buf, 50, 56);
		}
	} else {
		/* Two separate volume bars */
		cfb_print(display_dev, "Vol:", 0, 32);

		/* Left volume bar */
		if (device_display_state[0].has_data) {
			if (device_display_state[0].mute) {
				cfb_print(display_dev, "L:[MUTE]", 0, 48);
			} else {
				uint8_t vol_percent = (uint8_t)((device_display_state[0].volume * 100) / 255);
				int bar_length = (vol_percent * 6) / 100;

				line_buf[0] = 'L';
				line_buf[1] = '[';
				for (int i = 0; i < 6; i++) {
					line_buf[i + 2] = (i < bar_length) ? '=' : ' ';
				}
				line_buf[8] = ']';
				line_buf[9] = '\0';

				cfb_print(display_dev, line_buf, 0, 48);
			}
		}

		/* Right volume bar */
		if (device_display_state[1].has_data) {
			if (device_display_state[1].mute) {
				cfb_print(display_dev, "R:[MUTE]", 64, 48);
			} else {
				uint8_t vol_percent = (uint8_t)((device_display_state[1].volume * 100) / 255);
				int bar_length = (vol_percent * 6) / 100;

				line_buf[0] = 'R';
				line_buf[1] = '[';
				for (int i = 0; i < 6; i++) {
					line_buf[i + 2] = (i < bar_length) ? '=' : ' ';
				}
				line_buf[8] = ']';
				line_buf[9] = '\0';

				cfb_print(display_dev, line_buf, 64, 48);
			}
		}
	}

	/* Finalize and update display */
	cfb_framebuffer_finalize(display_dev);

	k_mutex_unlock(&display_mutex);
}

#include "battery_reader.h"

LOG_MODULE_REGISTER(battery_reader, LOG_LEVEL_DBG);

static struct device_context *current_device_ctx;

/* Notification callback for battery level updates */
static uint8_t battery_notify_cb(struct bt_conn *conn,
								 struct bt_gatt_subscribe_params *params,
								 const void *data, uint16_t length)
{
	if (!data)
	{
		LOG_WRN("No data, battery level notifications unsubscribed");
		params->value_handle = 0;
		return BT_GATT_ITER_STOP;
	}

	if (length != 1)
	{
		LOG_WRN("Unexpected battery level length: %u", length);
		return BT_GATT_ITER_CONTINUE;
	}

	ble_manager_set_device_ctx_battery_level(conn, *(uint8_t *)data);
	LOG_INF("Battery level notification: %u%%", current_device_ctx->bas_ctlr.battery_level);

	return BT_GATT_ITER_CONTINUE;
}

/* Subscription parameters for battery level notifications */
static struct bt_gatt_subscribe_params battery_subscribe_params = {
	.notify = battery_notify_cb,
	.value = BT_GATT_CCC_NOTIFY,
};

/* Read callback for battery level characteristic */
static uint8_t battery_read_cb(struct bt_conn *conn, uint8_t err,
							   struct bt_gatt_read_params *params,
							   const void *data, uint16_t length)
{
	if (err)
	{
		LOG_ERR("Battery level read failed (err %u)", err);
		return BT_GATT_ITER_STOP;
	}

	if (!data)
	{
		LOG_DBG("Battery level read complete");
		return BT_GATT_ITER_STOP;
	}

	if (length != 1)
	{
		LOG_WRN("Unexpected battery level length: %u", length);
		return BT_GATT_ITER_STOP;
	}

	current_device_ctx->bas_ctlr.battery_level = *(uint8_t *)data;
	LOG_INF("Battery level read: %u%%", current_device_ctx->bas_ctlr.battery_level);

	ble_cmd_complete(0);

	return BT_GATT_ITER_STOP;
}

/* Read parameters for battery level */
static struct bt_gatt_read_params battery_read_params = {
	.func = battery_read_cb,
	.handle_count = 1,
};

/* Discovery callback for Battery Service characteristics */
static uint8_t discover_char_cb(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 struct bt_gatt_discover_params *params)
{
	int err = 0;
	struct bt_gatt_chrc *chrc;
	
	if (!attr) {
		LOG_DBG("Discovery complete for type %d", params->type);
		
		if (params->type == BT_GATT_DISCOVER_DESCRIPTOR) {
			/* We finished searching for CCC descriptor */
			if (current_device_ctx->bas_ctlr.battery_level_ccc_handle == 0) {
				/* Didn't find CCC - notifications not available */
				LOG_WRN("CCC descriptor not found - notifications not available");
			} else {
				LOG_DBG("CCC descriptor found at handle 0x%04x", current_device_ctx->bas_ctlr.battery_level_ccc_handle);
				// battery_subscribe_notifications();
			}
		}
		
		/* If we have the characteristic handle, mark discovery as complete */
		if (current_device_ctx->bas_ctlr.battery_level_handle != 0) {
			current_device_ctx->info.bas_discovered = true;
			// Complete the discovery command
			LOG_DBG("Battery Service discovery complete (handle: 0x%04x, CCC: 0x%04x)", 
			        current_device_ctx->bas_ctlr.battery_level_handle, current_device_ctx->bas_ctlr.battery_level_ccc_handle);
			ble_cmd_complete(0);
		} else {
			LOG_ERR("Battery Service discovery completed but no characteristic found");
		}
		
		return BT_GATT_ITER_STOP;
	}

	LOG_DBG("[ATTRIBUTE] handle 0x%04X", attr->handle);

	if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		chrc = (struct bt_gatt_chrc *)attr->user_data;
		
		if (!bt_uuid_cmp(chrc->uuid, BT_UUID_BAS_BATTERY_LEVEL)) {
			LOG_DBG("Found Battery Level characteristic at handle 0x%04X (properties 0x%02X)", chrc->value_handle, chrc->properties);
			current_device_ctx->bas_ctlr.battery_level_handle = chrc->value_handle;
			current_device_ctx->info.bas_discovered = true;

			/* Check if notifications are supported based on properties */
			if (chrc->properties & BT_GATT_CHRC_NOTIFY) {
				LOG_DBG("Characteristic supports notifications, attempting to subscribe");
				battery_subscribe_params.value_handle = current_device_ctx->bas_ctlr.battery_level_handle;
				battery_subscribe_params.ccc_handle = BT_GATT_AUTO_DISCOVER_CCC_HANDLE;
				battery_subscribe_params.end_handle = current_device_ctx->bas_ctlr.battery_service_handle_end;
				battery_subscribe_params.value = BT_GATT_CCC_NOTIFY;
				battery_subscribe_params.disc_params = params;
				atomic_set_bit(battery_subscribe_params.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

				int err = bt_gatt_subscribe(current_device_ctx->conn, &battery_subscribe_params);
				if (err)
				{
					LOG_ERR("Battery notification subscription failed (err %d)", err);
					return err;
				}

				LOG_INF("Successfully subscribed to battery level notifications");
				ble_cmd_complete(0);
			}
			// 	/* Try to discover CCC descriptor */
			// 	static struct bt_gatt_discover_params discover_params;
			// 	memset(&discover_params, 0, sizeof(discover_params));
			// 	discover_params.uuid = NULL;
			// 	discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
			// 	discover_params.start_handle = chrc->value_handle;
			// 	discover_params.end_handle = params->end_handle;
			// 	discover_params.func = discover_char_cb;
				
			// 	int err = bt_gatt_discover(conn, &discover_params);
			// 	if (err) {
			// 		LOG_WRN("Failed to discover CCC (err %d) - proceeding without notifications", err);
			// 	}
			// } else {
			// 	LOG_WRN("Characteristic does not support notifications");
			// }

			return BT_GATT_ITER_STOP;
		}
	} else if (params->type == BT_GATT_DISCOVER_DESCRIPTOR) {
		struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;
		
		if (!bt_uuid_cmp(chrc->uuid, BT_UUID_GATT_CCC)) {
			LOG_DBG("Found CCC descriptor at handle %u", attr->handle);
			current_device_ctx->bas_ctlr.battery_level_ccc_handle = attr->handle;
			current_device_ctx->info.bas_discovered = true;
			return BT_GATT_ITER_STOP;
		}
	}

	return BT_GATT_ITER_CONTINUE;
}

/* Discovery callback for Battery Service */
static uint8_t discover_service_cb(struct bt_conn *conn,
								   const struct bt_gatt_attr *attr,
								   struct bt_gatt_discover_params *params)
{
	if (!attr)
	{
		LOG_WRN("Battery Service not found");
		return BT_GATT_ITER_STOP;
	}

	struct bt_gatt_service_val *svc = (struct bt_gatt_service_val *)attr->user_data;

	LOG_DBG("Found Battery Service at handle 0x%04X-0x%04X",
			attr->handle, svc->end_handle);
	current_device_ctx->bas_ctlr.battery_service_handle = attr->handle;
	current_device_ctx->bas_ctlr.battery_service_handle_end = svc->end_handle;
	LOG_DBG("Discover characteristics within Battery Service");

	static struct bt_gatt_discover_params discover_params;
	memset(&discover_params, 0, sizeof(discover_params));
	discover_params.uuid = NULL;
	discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
	discover_params.start_handle = attr->handle + 1;
	discover_params.end_handle = svc->end_handle;
	discover_params.func = discover_char_cb;

	int err = bt_gatt_discover(conn, &discover_params);
	if (err)
	{
		LOG_ERR("Failed to discover characteristics (err %d)", err);
	}

	return BT_GATT_ITER_STOP;
}

/* Discover Battery Service on connected device */
int battery_discover(struct device_context *device_ctx)
{
	if (!device_ctx || !device_ctx->conn)
	{
		LOG_ERR("Invalid connection context");
		return -EINVAL;
	}

	current_device_ctx = device_ctx;

	if (current_device_ctx->state != CONN_STATE_BONDED)
	{
		LOG_WRN("Not starting Battery Service discovery - wrong state: %d", current_device_ctx->state);
		return -EINVAL;
	}

	LOG_DBG("Starting Battery Service discovery");

	if (!current_device_ctx->info.bas_discovered)
	{
		static struct bt_gatt_discover_params discover_params;
		memset(&discover_params, 0, sizeof(discover_params));
		discover_params.uuid = BT_UUID_BAS;
		discover_params.type = BT_GATT_DISCOVER_PRIMARY;
		discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
		discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		discover_params.func = discover_service_cb;

		int err = bt_gatt_discover(current_device_ctx->conn, &discover_params);
		if (err)
		{
			LOG_ERR("Battery Service discovery failed (err %d)", err);
			return err;
		}
	} else {
		LOG_DBG("Battery Service already discovered");
	}

	return 0;
}

/* Read battery level */
int battery_read_level(struct device_context *device_ctx)
{
	if (!device_ctx->conn)
	{
		LOG_ERR("Invalid connection");
		return -EINVAL;
	}

	current_device_ctx = device_ctx;

	if (!current_device_ctx->info.bas_discovered || current_device_ctx->bas_ctlr.battery_level_handle == 0)
	{
		LOG_WRN("Battery Service not discovered");
		return -ENOENT;
	}

	LOG_DBG("Reading battery level from handle 0x%04X", current_device_ctx->bas_ctlr.battery_level_handle);

	battery_read_params.single.handle = current_device_ctx->bas_ctlr.battery_level_handle;
	battery_read_params.single.offset = 0;

	int err = bt_gatt_read(current_device_ctx->conn, &battery_read_params);
	if (err)
	{
		LOG_ERR("Battery level read failed (err %d)", err);
		return err;
	}

	return 0;
}

/* Reset battery reader state */
void battery_reader_reset(struct device_context *device_ctx)
{
	current_device_ctx = device_ctx;

	current_device_ctx->info.bas_discovered = false;
	current_device_ctx->bas_ctlr.battery_level_handle = 0;
	current_device_ctx->bas_ctlr.battery_level_ccc_handle = 0;
	current_device_ctx->bas_ctlr.battery_level = 0;
	LOG_DBG("Battery reader state reset");
}

int battery_reader_init(struct device_context *device_ctx) {
	current_device_ctx = device_ctx;
	
	battery_reader_reset(current_device_ctx);

	LOG_INF("Battery reader initialized");

	return 0;
}
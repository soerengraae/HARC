#include "ble_scanner.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/audio/vcp.h>
#include <string.h>

LOG_MODULE_REGISTER(ble_scanner, LOG_LEVEL_INF);

uint8_t deviceCount = 0;
uint8_t hiCount = 0;

struct k_work_delayable printDevicesWork;

struct bt_device_node *discovered_devices = NULL;

static void printDevicesHandler(struct k_work *work)
{
	(void)(work);
	// printDiscoveredDevices();
	k_work_schedule(&printDevicesWork, K_SECONDS(10));
}

struct bt_device_node *clearDiscoveredDevices(void)
{
	struct bt_device_node *current = discovered_devices;
	struct bt_device_node *next;

	while (current)
	{
		next = current->next;
		k_free(current);
		current = next;
	}

	discovered_devices = NULL;
	hiCount = 0;

	return discovered_devices;
}

size_t getDevicesMemoryUsed(void)
{
	LOG_DBG("Devices: %d", deviceCount);
	LOG_DBG("Size of device node: %zu", sizeof(struct bt_device_node));
	return deviceCount * sizeof(struct bt_device_node);
}

struct bt_device_node *createDevice(void) {
	struct bt_device_node *new_node = k_malloc(sizeof(struct bt_device_node));
	if (!new_node)
	{
		LOG_ERR("Memory allocation failed");
		return NULL;
	}
	memset(new_node, 0, sizeof(*new_node)); // Initialize all fields to zero/NULL
	return new_node;
}

/* Save discovered device address */
bool saveHI(struct device_info *info)
{
	struct bt_device_node *current = discovered_devices;

	/* Check if the address is already in the list */
	while (current)
	{
		if (!bt_addr_le_cmp(&current->info.addr, &info->addr)) // Compare with the new info structure
		{
			return false; // Address already exists
		}
		current = current->next;
	}

	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(&info->addr, addr_str, sizeof(addr_str));
	LOG_INF("Saving device: %s", addr_str);

	/* Add new address to the list */
	if (getDevicesMemoryUsed() + sizeof(*current) > MAX_DISCOVERED_DEVICES_MEMORY_SIZE)
	{
		LOG_WRN("Memory limit reached (%zu bytes) - cannot save more addresses", getDevicesMemoryUsed());
		return false;
	}

	LOG_DBG("Allocating memory (%d bytes) for new device node", sizeof(struct bt_device_node));
	struct bt_device_node *new_node = createDevice();
	if (!new_node)
	{
		LOG_ERR("Memory allocation failed");
		return false;
	}

	bt_addr_le_copy(&new_node->info.addr, &info->addr);
	strncpy(new_node->info.name, info->name, BT_NAME_MAX_LEN); // Copy name with max length
	new_node->info.name[BT_NAME_MAX_LEN] = '\0'; // Ensure null-termination
	new_node->info.isHI = info->isHI;
	new_node->info.hasName = info->hasName;
	new_node->next = discovered_devices;
	discovered_devices = new_node;

	deviceCount++;

	if (new_node->info.isHI) {
		hiCount++;
		printDiscoveredHIs();
	}

	LOG_DBG("Saved new HI");

	return true;
}

struct bt_device_node *getHIByAddr(const bt_addr_le_t *addr)
{
	struct bt_device_node *current = discovered_devices;

	while (current)
	{
		if (!bt_addr_le_cmp(&current->info.addr, addr))
		{
			return current;
		}

		current = current->next;
	}

	return NULL;
}

struct bt_device_node *getHIByName(const char *name)
{
	struct bt_device_node *current = discovered_devices;

	while (current)
	{
		if (strcmp(current->info.name, name) == 0)
		{
			return current;
		}

		current = current->next;
	}

	return NULL;
}

void printDiscoveredHIs(void)
{
	if (!discovered_devices)
	{
		LOG_INF("No HIs discovered yet.");
		return;
	}

	LOG_INF("%d discovered HIs (only displaying the ones with names):", hiCount);
	struct bt_device_node *current = discovered_devices;
	if (!current)
	{
		return;
	}
	else
	{
		while (current)
		{
			if (current->info.isHI && current->info.hasName)
			{
				char addr_str[BT_ADDR_LE_STR_LEN];
				bt_addr_le_to_str(&current->info.addr, addr_str, sizeof(addr_str));
				LOG_INF(" - %s, %s", addr_str, current->info.name);
			}
			current = current->next;
		}
	}
}

/* Connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

/* Device discovery function
   Extracts HAS service and device name from advertisement data */
static bool device_found(struct bt_data *data, void *user_data)
{
	struct device_info *info = (struct device_info *)user_data;
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(&info->addr, addr_str, sizeof(addr_str));

	struct bt_device_node *existing_device = getHIByAddr(&info->addr);

	if (existing_device && existing_device->info.hasName)
		LOG_DBG("Advertisement data type 0x%X len %u from %s", data->type, data->data_len, existing_device->info.name);
	else
		LOG_DBG("Advertisement data type 0x%X len %u from %s", data->type, data->data_len, addr_str);

	switch (data->type)
	{
		case BT_DATA_SVC_DATA16:
			// Parse 16-bit service data
			if (data->data_len >= 2)
			{
				for (size_t i = 0; i <= data->data_len - 2; i += 2)
				{
					uint16_t uuid_val = sys_get_le16(&data->data[i]);
					LOG_DBG("Service Data UUID 0x%04X", uuid_val);

					if (uuid_val == 0xFEFE)
					{
						LOG_DBG("Found GN Hearing HI service UUID");
						info->isHI = true;

						if (existing_device != NULL)
							existing_device->info.isHI = info->isHI;
						else
							saveHI(info);

						break;
					}
				}
			}
			
			break;

		case BT_DATA_NAME_COMPLETE:
		case BT_DATA_NAME_SHORTENED:
			size_t name_len = MIN(data->data_len, BT_NAME_MAX_LEN);
			memcpy(info->name, data->data, name_len);
			info->name[name_len] = '\0';
			info->hasName = true;
			if (existing_device != NULL && !existing_device->info.hasName) {
				strcpy(existing_device->info.name, info->name);
				existing_device->info.hasName = true;
				if (existing_device->info.isHI)
					printDiscoveredHIs();
			}

			break;

		default:
			return true;
	}

	return true;
}

static void device_found_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	struct device_info info = {0};

	/* Initialize device info structure */
	info.addr = *addr;
	info.isHI = false;
	info.hasName = false;
	memset(info.name, 0, sizeof(info.name));

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	/* Parse advertisement data to extract HAS service and name */
	bt_data_parse(ad, device_found, &info);
}

/* Start BLE scanning */
void ble_scanner_start(void)
{
	int err;

	/* Stop any existing scan first */
	// bt_le_scan_stop();

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found_cb);
	if (err)
	{
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning for HIs");

	k_work_init_delayable(&printDevicesWork, printDevicesHandler);
	k_work_schedule(&printDevicesWork, K_SECONDS(10));
}

/* Initialize BLE scanner */
int ble_scanner_init(void)
{
	LOG_INF("BLE scanner initialized");
	return 0;
}
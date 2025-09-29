#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <stdint.h>

#define MAX_DISCOVERED_DEVICES_MEMORY_SIZE 1024 // 1 KB
#define BT_NAME_MAX_LEN 12

extern uint8_t deviceCount;
extern uint8_t hiCount;

/* Structure to pass data between parsing callbacks */
struct device_info
{
	bt_addr_le_t addr;
	char name[BT_NAME_MAX_LEN + 1];
	bool isHI;
	bool hasName;
};

struct bt_device_node
{
	struct device_info info;
	struct bt_device_node *next;
};

extern struct bt_device_node *discovered_devices;

/* BLE scanner functions */
int ble_scanner_init(void);
void ble_scanner_start(void);
void printDiscoveredDevices(void);
void printDiscoveredHIs(void);
struct bt_device_node *clearDiscoveredDevices(void);
struct bt_device_node *getHIByAddr(const bt_addr_le_t *addr);
struct bt_device_node *getHIByName(const char *name);
size_t getDevicesMemoryUsed(void);
struct bt_device_node *createDevice(void);
bool saveHI(struct device_info *info);

/* Connection management */
extern struct bt_conn_cb conn_callbacks;

#endif /* BLE_SCANNER_H */
#ifndef CSIP_H
#define CSIP_H

#include <zephyr/bluetooth/audio/csip.h>
#include <zephyr/bluetooth/conn.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Initialize CSIP Set Coordinator
 *
 * @return 0 on success, negative error code on failure
 */
int csip_init(void);

/**
 * @brief Discover CSIP sets on a connected device
 *
 * @param conn Connection to the device
 * @return 0 on success, negative error code on failure
 */
int csip_discover(struct bt_conn *conn);

/**
 * @brief Get set member information by connection
 *
 * @param conn Connection to query
 * @return Pointer to set member or NULL if not found
 */
struct bt_csip_set_coordinator_set_member *csip_get_set_member_by_conn(struct bt_conn *conn);

/**
 * @brief Get number of discovered sets
 *
 * @return Number of discovered sets
 */
size_t csip_get_discovered_set_count(void);

/**
 * @brief Get discovered set by index
 *
 * @param index Index of the set
 * @return Pointer to set member or NULL if invalid index
 */
struct bt_csip_set_coordinator_set_member *csip_get_discovered_set(size_t index);

/**
 * @brief Check if advertising data indicates a set member
 *
 * @param sirk The SIRK to check against
 * @param data Advertising data
 * @return true if the device is a set member, false otherwise
 */
bool csip_is_set_member(const uint8_t sirk[BT_CSIP_SIRK_SIZE], struct bt_data *data);

/**
 * @brief Reset CSIP state
 */
void csip_reset(void);

/**
 * @brief Check if a device is part of a coordinated set
 *
 * @param conn Connection to check
 * @return true if device is in a set, false otherwise
 */
bool csip_is_device_in_set(struct bt_conn *conn);

/**
 * @brief Get the set size for a connected device
 *
 * @param conn Connection to query
 * @return Set size or 0 if not in a set
 */
int csip_get_set_size(struct bt_conn *conn);

/**
 * @brief Get the device rank within its set
 *
 * @param conn Connection to query
 * @return Device rank or 0 if not in a set
 */
int csip_get_device_rank(struct bt_conn *conn);

/**
 * @brief Log detailed set information for a device
 *
 * @param conn Connection to query
 */
void csip_log_set_info(struct bt_conn *conn);

/**
 * @brief Check if an advertising device is a set member and add it
 *
 * @param addr Device address
 * @param data Advertisement data
 * @return true if device is a set member, false otherwise
 */
bool csip_check_and_add_set_member(const bt_addr_le_t *addr, struct bt_data *data);

/**
 * @brief Get addresses of other set members (excluding current connection)
 *
 * @param conn Current connection
 * @param members Array to store other member addresses
 * @param max_members Maximum number of addresses to return
 * @return Number of other set members found
 */
size_t csip_get_other_set_members(struct bt_conn *conn, bt_addr_le_t *members, size_t max_members);

/**
 * @brief Get all known set member addresses
 *
 * @param members Array to store member addresses
 * @param max_members Maximum number of addresses to return
 * @return Total number of set members found
 */
size_t csip_get_all_set_members(bt_addr_le_t *members, size_t max_members);

/**
 * @brief Check if an address is a known set member
 *
 * @param addr Address to check
 * @return true if address is a set member, false otherwise
 */
bool csip_is_address_in_set(const bt_addr_le_t *addr);

/**
 * @brief Get number of known sets
 *
 * @return Number of known sets
 */
size_t csip_get_known_set_count(void);

/**
 * @brief Log summary of all known sets and their members
 */
void csip_log_all_sets(void);

/**
 * @brief Debug SIRK matching against RSI data
 *
 * @param sirk SIRK to test
 * @param data RSI advertisement data
 */
void csip_debug_sirk_match(const uint8_t sirk[BT_CSIP_SIRK_SIZE], struct bt_data *data);

/**
 * @brief Suggest a device as a potential set member based on name pattern
 *
 * @param addr Device address
 * @param name Device name
 */
void csip_suggest_potential_member(const bt_addr_le_t *addr, const char *name);

#endif /* CSIP_H */
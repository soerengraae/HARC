#include "csip.h"
#include "ble_manager.h"
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(csip, LOG_LEVEL_DBG);

static struct bt_csip_set_coordinator_cb csip_callbacks;
static struct k_work_delayable csip_discovery_work;
static struct k_work_delayable csip_retry_work;
static struct k_work_delayable csip_member_search_work;

/* Set member tracking */
static struct bt_csip_set_coordinator_set_member *discovered_sets[CONFIG_BT_CSIP_SET_COORDINATOR_MAX_CSIS_INSTANCES];
static size_t discovered_set_count = 0;

/* SIRK storage for set member identification */
struct csip_set_info {
    uint8_t sirk[BT_CSIP_SIRK_SIZE];
    uint8_t set_size;
    bt_addr_le_t members[CONFIG_BT_MAX_CONN]; /* Store addresses of discovered set members */
    size_t member_count;
    bool sirk_valid;
};

static struct csip_set_info known_sets[CONFIG_BT_CSIP_SET_COORDINATOR_MAX_CSIS_INSTANCES];
static size_t known_set_count = 0;
static bool csip_discovery_in_progress = false;
static struct bt_conn *pending_discovery_conn = NULL;
static uint8_t csip_discovery_retry_count = 0;

#define CSIP_MAX_RETRIES 3
#define CSIP_RETRY_DELAY_MS 1000

/* Forward declarations */
static void csip_discovery_work_handler(struct k_work *work);
static void csip_retry_work_handler(struct k_work *work);
static void csip_member_search_work_handler(struct k_work *work);
static void csip_discover_cb(struct bt_conn *conn,
                            const struct bt_csip_set_coordinator_set_member *member,
                            int err, size_t set_count);
static void csip_lock_changed_cb(struct bt_csip_set_coordinator_csis_inst *inst, bool locked);
static void csip_sirk_changed_cb(struct bt_csip_set_coordinator_csis_inst *inst);
static struct csip_set_info *csip_find_or_create_set(const uint8_t sirk[BT_CSIP_SIRK_SIZE], uint8_t set_size);

int csip_init(void)
{
    k_work_init_delayable(&csip_discovery_work, csip_discovery_work_handler);
    k_work_init_delayable(&csip_retry_work, csip_retry_work_handler);
    k_work_init_delayable(&csip_member_search_work, csip_member_search_work_handler);

    /* Initialize CSIP callbacks */
    csip_callbacks.discover = csip_discover_cb;
    csip_callbacks.lock_changed = csip_lock_changed_cb;
    csip_callbacks.sirk_changed = csip_sirk_changed_cb;

    int err = bt_csip_set_coordinator_register_cb(&csip_callbacks);
    if (err) {
        LOG_ERR("Failed to register CSIP callbacks (err %d)", err);
        return err;
    }

    LOG_INF("CSIP Set Coordinator initialized");
    return 0;
}

int csip_discover(struct bt_conn *conn)
{
    if (!conn) {
        LOG_ERR("Invalid connection for CSIP discovery");
        return -EINVAL;
    }

    if (csip_discovery_in_progress) {
        LOG_WRN("CSIP discovery already in progress");
        return -EBUSY;
    }

    csip_discovery_in_progress = true;
    pending_discovery_conn = bt_conn_ref(conn);
    csip_discovery_retry_count = 0;

    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Starting CSIP discovery on %s", addr);

    int err = bt_csip_set_coordinator_discover(conn);
    if (err == -ENOMEM) {
        LOG_WRN("CSIP discovery failed due to memory allocation (err %d), will retry", err);
        k_work_schedule(&csip_retry_work, K_MSEC(CSIP_RETRY_DELAY_MS));
        return 0; /* Don't fail immediately, let retry handle it */
    } else if (err) {
        LOG_ERR("Failed to start CSIP discovery (err %d)", err);
        csip_discovery_in_progress = false;
        if (pending_discovery_conn) {
            bt_conn_unref(pending_discovery_conn);
            pending_discovery_conn = NULL;
        }
        return err;
    }

    /* Set discovery timeout */
    k_work_schedule(&csip_discovery_work, BT_CSIP_SET_COORDINATOR_DISCOVER_TIMER_VALUE);

    return 0;
}

static void csip_discovery_work_handler(struct k_work *work)
{
    LOG_WRN("CSIP discovery timeout");
    csip_discovery_in_progress = false;
    if (pending_discovery_conn) {
        bt_conn_unref(pending_discovery_conn);
        pending_discovery_conn = NULL;
    }
    ble_cmd_complete(-ETIMEDOUT);
}

static void csip_retry_work_handler(struct k_work *work)
{
    if (!pending_discovery_conn) {
        LOG_ERR("CSIP retry but no pending connection");
        csip_discovery_in_progress = false;
        ble_cmd_complete(-EINVAL);
        return;
    }

    csip_discovery_retry_count++;

    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(pending_discovery_conn), addr, sizeof(addr));

    if (csip_discovery_retry_count > CSIP_MAX_RETRIES) {
        LOG_ERR("CSIP discovery failed after %d retries on %s", CSIP_MAX_RETRIES, addr);
        csip_discovery_in_progress = false;
        bt_conn_unref(pending_discovery_conn);
        pending_discovery_conn = NULL;
        ble_cmd_complete(-ENOMEM);
        return;
    }

    LOG_INF("Retrying CSIP discovery on %s (attempt %d/%d)",
           addr, csip_discovery_retry_count, CSIP_MAX_RETRIES);

    int err = bt_csip_set_coordinator_discover(pending_discovery_conn);
    if (err == -ENOMEM) {
        LOG_WRN("CSIP discovery retry failed due to memory (err %d), scheduling next retry", err);
        k_work_schedule(&csip_retry_work, K_MSEC(CSIP_RETRY_DELAY_MS * csip_discovery_retry_count));
    } else if (err) {
        LOG_ERR("CSIP discovery retry failed (err %d)", err);
        csip_discovery_in_progress = false;
        bt_conn_unref(pending_discovery_conn);
        pending_discovery_conn = NULL;
        ble_cmd_complete(err);
    } else {
        LOG_DBG("CSIP discovery retry successful, setting timeout");
        k_work_schedule(&csip_discovery_work, BT_CSIP_SET_COORDINATOR_DISCOVER_TIMER_VALUE);
    }
}

static void csip_discover_cb(struct bt_conn *conn,
                            const struct bt_csip_set_coordinator_set_member *member,
                            int err, size_t set_count)
{
    k_work_cancel_delayable(&csip_discovery_work);
    k_work_cancel_delayable(&csip_retry_work);
    csip_discovery_in_progress = false;

    /* Clean up pending connection reference */
    if (pending_discovery_conn) {
        bt_conn_unref(pending_discovery_conn);
        pending_discovery_conn = NULL;
    }

    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err) {
        LOG_ERR("CSIP discovery failed on %s (err %d)", addr, err);
        ble_cmd_complete(err);
        return;
    }

    if (set_count == 0) {
        LOG_INF("No coordinated sets found on %s", addr);
        ble_cmd_complete(0);
        return;
    }

    LOG_INF("CSIP discovery completed on %s: found %zu sets", addr, set_count);

    /* Store the discovered set member */
    if (discovered_set_count < CONFIG_BT_CSIP_SET_COORDINATOR_MAX_CSIS_INSTANCES) {
        discovered_sets[discovered_set_count] = (struct bt_csip_set_coordinator_set_member *)member;
        discovered_set_count++;

        /* Extract and store SIRK information for each set */
        for (size_t i = 0; i < set_count; i++) {
            const struct bt_csip_set_coordinator_csis_inst *inst = &member->insts[i];
            LOG_INF("Set %zu: size=%u, rank=%u, lockable=%s",
                   i, inst->info.set_size, inst->info.rank,
                   inst->info.lockable ? "yes" : "no");

            /* Store SIRK and create/update set info */
            struct csip_set_info *set_info = csip_find_or_create_set(inst->info.sirk, inst->info.set_size);
            if (set_info) {
                /* Add current device address as first member */
                const bt_addr_le_t *device_addr = bt_conn_get_dst(conn);
                bool already_added = false;

                for (size_t j = 0; j < set_info->member_count; j++) {
                    if (bt_addr_le_eq(&set_info->members[j], device_addr)) {
                        already_added = true;
                        break;
                    }
                }

                if (!already_added && set_info->member_count < CONFIG_BT_MAX_CONN) {
                    bt_addr_le_copy(&set_info->members[set_info->member_count], device_addr);
                    set_info->member_count++;

                    char addr_str[BT_ADDR_LE_STR_LEN];
                    bt_addr_le_to_str(device_addr, addr_str, sizeof(addr_str));
                    LOG_INF("Added device %s as set member (total: %zu/%u)",
                           addr_str, set_info->member_count, set_info->set_size);
                }
            }
        }

        /* Start searching for other set members */
        LOG_INF("Starting active search for other set members");
        k_work_schedule(&csip_member_search_work, K_SECONDS(2));
    } else {
        LOG_WRN("Maximum number of discovered sets reached");
    }

    ble_cmd_complete(0);
}

static void csip_lock_changed_cb(struct bt_csip_set_coordinator_csis_inst *inst, bool locked)
{
    LOG_INF("CSIP set lock changed: %s (rank %u)",
           locked ? "locked" : "unlocked", inst->info.rank);
}

static void csip_sirk_changed_cb(struct bt_csip_set_coordinator_csis_inst *inst)
{
    LOG_INF("CSIP set SIRK changed (rank %u)", inst->info.rank);
}

struct bt_csip_set_coordinator_set_member *csip_get_set_member_by_conn(struct bt_conn *conn)
{
    return bt_csip_set_coordinator_set_member_by_conn(conn);
}

size_t csip_get_discovered_set_count(void)
{
    return discovered_set_count;
}

struct bt_csip_set_coordinator_set_member *csip_get_discovered_set(size_t index)
{
    if (index >= discovered_set_count) {
        return NULL;
    }
    return discovered_sets[index];
}

bool csip_is_set_member(const uint8_t sirk[BT_CSIP_SIRK_SIZE], struct bt_data *data)
{
    return bt_csip_set_coordinator_is_set_member(sirk, data);
}

static struct csip_set_info *csip_find_or_create_set(const uint8_t sirk[BT_CSIP_SIRK_SIZE], uint8_t set_size)
{
    /* First, try to find existing set with same SIRK */
    for (size_t i = 0; i < known_set_count; i++) {
        if (known_sets[i].sirk_valid &&
            memcmp(known_sets[i].sirk, sirk, BT_CSIP_SIRK_SIZE) == 0) {
            return &known_sets[i];
        }
    }

    /* Create new set if we have space */
    if (known_set_count < CONFIG_BT_CSIP_SET_COORDINATOR_MAX_CSIS_INSTANCES) {
        struct csip_set_info *new_set = &known_sets[known_set_count];
        memcpy(new_set->sirk, sirk, BT_CSIP_SIRK_SIZE);
        new_set->set_size = set_size;
        new_set->member_count = 0;
        new_set->sirk_valid = true;
        known_set_count++;

        LOG_DBG("Created new set info (total sets: %zu)", known_set_count);
        return new_set;
    }

    LOG_ERR("Cannot create new set - maximum sets reached");
    return NULL;
}

void csip_reset(void)
{
    k_work_cancel_delayable(&csip_discovery_work);
    k_work_cancel_delayable(&csip_retry_work);
    k_work_cancel_delayable(&csip_member_search_work);
    csip_discovery_in_progress = false;
    csip_discovery_retry_count = 0;

    if (pending_discovery_conn) {
        bt_conn_unref(pending_discovery_conn);
        pending_discovery_conn = NULL;
    }

    discovered_set_count = 0;
    memset(discovered_sets, 0, sizeof(discovered_sets));

    known_set_count = 0;
    memset(known_sets, 0, sizeof(known_sets));

    LOG_DBG("CSIP state reset");
}

bool csip_is_device_in_set(struct bt_conn *conn)
{
    struct bt_csip_set_coordinator_set_member *member = csip_get_set_member_by_conn(conn);
    return (member != NULL);
}

int csip_get_set_size(struct bt_conn *conn)
{
    struct bt_csip_set_coordinator_set_member *member = csip_get_set_member_by_conn(conn);
    if (!member) {
        return 0;
    }

    /* Return the size of the first set */
    if (member->insts[0].info.set_size > 0) {
        return member->insts[0].info.set_size;
    }

    return 0;
}

int csip_get_device_rank(struct bt_conn *conn)
{
    struct bt_csip_set_coordinator_set_member *member = csip_get_set_member_by_conn(conn);
    if (!member) {
        return 0;
    }

    /* Return the rank of the first set */
    return member->insts[0].info.rank;
}

void csip_log_set_info(struct bt_conn *conn)
{
    struct bt_csip_set_coordinator_set_member *member = csip_get_set_member_by_conn(conn);
    if (!member) {
        LOG_INF("Device is not part of any coordinated set");
        return;
    }

    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    for (size_t i = 0; i < BT_CSIP_SET_COORDINATOR_MAX_CSIS_INSTANCES; i++) {
        const struct bt_csip_set_coordinator_csis_inst *inst = &member->insts[i];
        if (inst->info.set_size > 0) {
            LOG_INF("Device %s: Set %zu - Size: %u, Rank: %u, Lockable: %s",
                   addr, i, inst->info.set_size, inst->info.rank,
                   inst->info.lockable ? "Yes" : "No");
        }
    }
}

bool csip_check_and_add_set_member(const bt_addr_le_t *addr, struct bt_data *data)
{
    bool is_set_member = false;

    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    LOG_DBG("Checking device %s against %zu known sets", addr_str, known_set_count);

    /* Check against all known SIRKs */
    for (size_t i = 0; i < known_set_count; i++) {
        if (!known_sets[i].sirk_valid) {
            continue;
        }

        LOG_DBG("Testing against set %zu", i);
        csip_debug_sirk_match(known_sets[i].sirk, data);

        if (bt_csip_set_coordinator_is_set_member(known_sets[i].sirk, data)) {
            is_set_member = true;
            LOG_INF("SIRK match confirmed for device %s in set %zu!", addr_str, i);

            /* Check if this address is already in our set */
            bool already_known = false;
            for (size_t j = 0; j < known_sets[i].member_count; j++) {
                if (bt_addr_le_eq(&known_sets[i].members[j], addr)) {
                    already_known = true;
                    break;
                }
            }

            /* Add new set member if we have space */
            if (!already_known && known_sets[i].member_count < CONFIG_BT_MAX_CONN) {
                bt_addr_le_copy(&known_sets[i].members[known_sets[i].member_count], addr);
                known_sets[i].member_count++;

                char addr_str[BT_ADDR_LE_STR_LEN];
                bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
                LOG_INF("Found new set member %s (set %zu: %zu/%u members)",
                       addr_str, i, known_sets[i].member_count, known_sets[i].set_size);
            }
            break; /* Device can only be in one set at a time for our purposes */
        }
    }

    return is_set_member;
}

size_t csip_get_other_set_members(struct bt_conn *conn, bt_addr_le_t *members, size_t max_members)
{
    if (!conn || !members || max_members == 0) {
        return 0;
    }

    const bt_addr_le_t *conn_addr = bt_conn_get_dst(conn);
    size_t found_count = 0;

    /* Find the set this connection belongs to */
    for (size_t i = 0; i < known_set_count && found_count < max_members; i++) {
        if (!known_sets[i].sirk_valid) {
            continue;
        }

        /* Check if this connection's address is in this set */
        bool conn_in_set = false;
        for (size_t j = 0; j < known_sets[i].member_count; j++) {
            if (bt_addr_le_eq(&known_sets[i].members[j], conn_addr)) {
                conn_in_set = true;
                break;
            }
        }

        if (conn_in_set) {
            /* Copy other members (not the current connection) */
            for (size_t j = 0; j < known_sets[i].member_count && found_count < max_members; j++) {
                if (!bt_addr_le_eq(&known_sets[i].members[j], conn_addr)) {
                    bt_addr_le_copy(&members[found_count], &known_sets[i].members[j]);
                    found_count++;
                }
            }
            break; /* Connection should only be in one set */
        }
    }

    return found_count;
}

size_t csip_get_all_set_members(bt_addr_le_t *members, size_t max_members)
{
    if (!members || max_members == 0) {
        return 0;
    }

    size_t total_count = 0;

    for (size_t i = 0; i < known_set_count && total_count < max_members; i++) {
        if (!known_sets[i].sirk_valid) {
            continue;
        }

        for (size_t j = 0; j < known_sets[i].member_count && total_count < max_members; j++) {
            bt_addr_le_copy(&members[total_count], &known_sets[i].members[j]);
            total_count++;
        }
    }

    return total_count;
}

bool csip_is_address_in_set(const bt_addr_le_t *addr)
{
    if (!addr) {
        return false;
    }

    for (size_t i = 0; i < known_set_count; i++) {
        if (!known_sets[i].sirk_valid) {
            continue;
        }

        for (size_t j = 0; j < known_sets[i].member_count; j++) {
            if (bt_addr_le_eq(&known_sets[i].members[j], addr)) {
                return true;
            }
        }
    }

    return false;
}

size_t csip_get_known_set_count(void)
{
    return known_set_count;
}

void csip_log_all_sets(void)
{
    LOG_INF("=== CSIP Set Summary ===");
    LOG_INF("Known sets: %zu", known_set_count);

    for (size_t i = 0; i < known_set_count; i++) {
        if (!known_sets[i].sirk_valid) {
            continue;
        }

        LOG_INF("Set %zu: Size %u, Discovered members: %zu",
               i, known_sets[i].set_size, known_sets[i].member_count);

        /* Log SIRK (first 4 bytes for debugging) */
        LOG_INF("  SIRK (partial): %02x %02x %02x %02x...",
               known_sets[i].sirk[0], known_sets[i].sirk[1],
               known_sets[i].sirk[2], known_sets[i].sirk[3]);

        for (size_t j = 0; j < known_sets[i].member_count; j++) {
            char addr_str[BT_ADDR_LE_STR_LEN];
            bt_addr_le_to_str(&known_sets[i].members[j], addr_str, sizeof(addr_str));
            LOG_INF("  Member %zu: %s", j + 1, addr_str);
        }
    }
    LOG_INF("========================");
}

void csip_debug_sirk_match(const uint8_t sirk[BT_CSIP_SIRK_SIZE], struct bt_data *data)
{
    LOG_DBG("=== SIRK Match Debug ===");
    LOG_DBG("SIRK to match: %02x %02x %02x %02x...", sirk[0], sirk[1], sirk[2], sirk[3]);
    LOG_DBG("RSI data type: 0x%02x, len: %u", data->type, data->data_len);

    if (data->data_len >= 6) {
        LOG_DBG("RSI data: %02x %02x %02x %02x %02x %02x",
               data->data[0], data->data[1], data->data[2],
               data->data[3], data->data[4], data->data[5]);
    }

    bool result = bt_csip_set_coordinator_is_set_member(sirk, data);
    LOG_DBG("Match result: %s", result ? "YES" : "NO");
    LOG_DBG("========================");
}

static void csip_member_search_work_handler(struct k_work *work)
{
    LOG_INF("=== Active Set Member Search ===");

    if (known_set_count == 0) {
        LOG_INF("No known sets to search for");
        return;
    }

    for (size_t i = 0; i < known_set_count; i++) {
        if (!known_sets[i].sirk_valid) {
            continue;
        }

        LOG_INF("Set %zu: Found %zu/%u members so far",
               i, known_sets[i].member_count, known_sets[i].set_size);

        if (known_sets[i].member_count < known_sets[i].set_size) {
            LOG_INF("Still missing %u members - continuing scan...",
                   known_sets[i].set_size - known_sets[i].member_count);

            /* Schedule another search in 10 seconds if we haven't found all members */
            k_work_schedule(&csip_member_search_work, K_SECONDS(10));
        } else {
            LOG_INF("All set members found for set %zu!", i);
        }
    }

    LOG_INF("=== End Set Member Search ===");
}

void csip_suggest_potential_member(const bt_addr_le_t *addr, const char *name)
{
    if (!addr || !name) {
        return;
    }

    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    /* Check if we have incomplete sets */
    for (size_t i = 0; i < known_set_count; i++) {
        if (!known_sets[i].sirk_valid) {
            continue;
        }

        if (known_sets[i].member_count < known_sets[i].set_size) {
            /* Check if this address is already known */
            bool already_known = false;
            for (size_t j = 0; j < known_sets[i].member_count; j++) {
                if (bt_addr_le_eq(&known_sets[i].members[j], addr)) {
                    already_known = true;
                    break;
                }
            }

            if (!already_known) {
                LOG_INF("Potential set member detected: %s (%s) for set %zu",
                       addr_str, name, i);
                LOG_INF("Set %zu currently has %zu/%u members",
                       i, known_sets[i].member_count, known_sets[i].set_size);
            }
        }
    }
}


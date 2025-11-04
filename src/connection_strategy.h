#ifndef CONNECTION_STRATEGY_H
#define CONNECTION_STRATEGY_H

#include "ble_manager.h"
#include <zephyr/bluetooth/bluetooth.h>

/**
 * @brief Connection strategy types based on available bonded devices
 */
enum connection_strategy {
    STRATEGY_NO_BONDS,           // No bonded devices - start fresh pairing
    STRATEGY_SINGLE_BOND,        // One device bonded - search for its pair
    STRATEGY_VERIFIED_SET,       // Two bonds with matching stored SIRKs
    STRATEGY_UNVERIFIED_SET,     // Two bonds, need SIRK verification
    STRATEGY_MULTIPLE_SETS       // 3+ bonds, need selection logic
};

/**
 * @brief Context for connection strategy execution
 */
struct connection_strategy_context {
    enum connection_strategy strategy;
    struct bond_collection bonds;
    uint8_t primary_device_idx;     // Index of device to connect first
    uint8_t secondary_device_idx;   // Index of device to connect second
    bool has_matching_set;          // True if found matching SIRK pair
};

/**
 * @brief Connection state machine phases for progressive dual-device connection
 */
enum connection_phase {
    PHASE_IDLE,                    // No active connection sequence
    PHASE_PRIMARY_CONNECTING,      // Connecting to first device
    PHASE_PRIMARY_DISCOVERING,     // Discovering CSIP on first device
    PHASE_SECONDARY_CONNECTING,    // Connecting to second device
    PHASE_SECONDARY_DISCOVERING,   // Discovering CSIP on second device
    PHASE_VERIFYING_SET,          // Verifying SIRK match between devices
    PHASE_COMPLETED               // Both devices connected and verified
};

/**
 * @brief Global connection state machine for dual-device coordination
 */
struct connection_state_machine {
    enum connection_phase phase;
    struct connection_strategy_context strategy_ctx;
    bool primary_ready;           // Primary device fully discovered
    bool secondary_ready;         // Secondary device fully discovered
    bool set_verified;           // SIRK verification completed
};

/* Strategy API */
int determine_connection_strategy(struct connection_strategy_context *ctx);
int execute_connection_strategy(struct connection_strategy_context *ctx);
bool sirk_match(const uint8_t *sirk1, const uint8_t *sirk2);

/* State machine API */
extern struct connection_state_machine g_conn_state_machine;
void connection_state_machine_init(void);
void connection_state_machine_on_csip_discovered(uint8_t device_id);
int connection_state_machine_connect_secondary(void);

/* RSI scanning for pair discovery */
int start_rsi_scan_for_pair(uint8_t device_id);
void stop_rsi_scan_for_pair(void);

#endif /* CONNECTION_STRATEGY_H */

#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdint>
#include <cstddef>

#if defined(ESP32_PLATFORM) && defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE

#include <helpers/esp32/SerialBLEInterface.h>
#include "ble_frame_queue.h"
#include "ble_init_gate.h"
#include "ble_task_mutex.h"
#include "ble_auth_watchdog.h"

namespace sigurdos {
namespace comms {

struct BleSerialObserverStats {
    bool begun = false;
    bool enabled = false;
    bool connected = false;
    bool advertising_expected = false;
    uint32_t begin_count = 0;
    uint32_t enable_count = 0;
    uint32_t disable_count = 0;
    uint32_t connect_count = 0;
    uint32_t disconnect_count = 0;
    uint32_t mtu_change_count = 0;
    uint32_t auth_success_count = 0;
    uint32_t auth_failure_count = 0;
    uint32_t auth_timeout_count = 0;
    uint32_t ble_write_count = 0;
    uint32_t ble_write_drop_count = 0;
    uint32_t rx_frame_count = 0;
    uint32_t tx_frame_count = 0;
    uint32_t tx_drop_count = 0;
    uint16_t last_conn_id = 0;
    uint16_t last_mtu = 0;
    uint8_t last_rx_code = 0;
    uint8_t last_tx_code = 0;
};

class ObservedSerialBLEInterface final : public SerialBLEInterface {
public:
    void configure(const char* prefix, const char* name, uint32_t pin_code);
    void begin(const char* prefix, char* name, uint32_t pin_code);
    void enable() override;
    void disable() override;
    bool isEnabled() const override;
    bool isConnected() const override;
    size_t writeFrame(const uint8_t src[], size_t len) override;
    size_t checkRecvFrame(uint8_t dest[]) override;

    BleSerialObserverStats stats() const;

protected:
    uint32_t onPassKeyRequest() override;
    void onPassKeyNotify(uint32_t pass_key) override;
    bool onConfirmPIN(uint32_t pass_key) override;
    bool onSecurityRequest() override;
    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override;
    void onConnect(BLEServer* server) override;
    void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override;
    void onMtuChanged(BLEServer* server, esp_ble_gatts_cb_param_t* param) override;
    void onDisconnect(BLEServer* server) override;
    void onWrite(BLECharacteristic* characteristic, esp_ble_gatts_cb_param_t* param) override;

private:
    void refreshConnectionState();
    bool initializeConfigured();

    BleSerialObserverStats _stats{};
    BleInitGate _init_gate;
    char _configured_prefix[16]{};
    char _configured_name[32]{};
    uint32_t _configured_pin = 0;
    mutable BleTaskMutex _task_mutex;
    BleAuthWatchdog _auth_watchdog;
    BLEServer* _connected_server = nullptr;

    // NET-002 (#813): the base class receive queue is written from the
    // Bluedroid host task (onWrite) and drained from the app loop task
    // (checkRecvFrame) without synchronization. Received frames are kept
    // here instead — the base queue stays empty. Same capacity as upstream.
    BleFrameQueue<MAX_FRAME_SIZE, FRAME_QUEUE_SIZE> _rx_queue;
};

} // namespace comms
} // namespace sigurdos

#endif

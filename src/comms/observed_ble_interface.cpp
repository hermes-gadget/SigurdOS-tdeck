// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "observed_ble_interface.h"

#include <cstring>
#include <esp_gap_ble_api.h>

#if defined(ESP32_PLATFORM) && defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE

namespace sigurdos {
namespace comms {

bool ObservedSerialBLEInterface::peerIsBonded(const BlePeerAddress& peer) const
{
    int count = esp_ble_get_bond_device_num();
    if (count <= 0) return false;
#if defined(CONFIG_BT_SMP_MAX_BONDS)
    static constexpr int MAX_BONDS = CONFIG_BT_SMP_MAX_BONDS;
#else
    static constexpr int MAX_BONDS = 15;
#endif
    if (count > MAX_BONDS) return false;
    esp_ble_bond_dev_t bonds[MAX_BONDS]{};
    int listed = count;
    if (esp_ble_get_bond_device_list(&listed, bonds) != ESP_OK ||
        listed < 0 || listed > count) {
        return false;
    }
    for (int i = 0; i < listed; ++i) {
        if (std::memcmp(bonds[i].bd_addr, peer.bytes, sizeof(peer.bytes)) == 0) {
            return true;
        }
    }
    return false;
}

void ObservedSerialBLEInterface::recordAuthenticationFailure(uint32_t now_ms)
{
    _auth_throttle.recordFailure(_connecting_peer, now_ms);
    _advertising_suppressed = true;
}

void ObservedSerialBLEInterface::applyAdvertisingThrottle(uint32_t now_ms)
{
    if (!_init_gate.initialized() || !SerialBLEInterface::isEnabled() ||
        SerialBLEInterface::isConnected()) {
        return;
    }
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    if (!advertising) return;
    if (_auth_throttle.blocked(now_ms)) {
        advertising->stop();
        _advertising_suppressed = true;
    } else if (_advertising_suppressed) {
        advertising->start();
        _advertising_suppressed = false;
    }
}

void ObservedSerialBLEInterface::refreshConnectionState()
{
    _stats.enabled = SerialBLEInterface::isEnabled();
    _stats.connected = SerialBLEInterface::isConnected();
    _stats.advertising_expected = _stats.enabled && !_stats.connected &&
                                  !_auth_throttle.blocked(millis());
}

void ObservedSerialBLEInterface::configure(const char* prefix, const char* name,
                                           uint32_t pin_code)
{
    BleTaskMutex::Guard guard(_task_mutex);
    if (_init_gate.initialized()) return;
    if (!prefix) prefix = "";
    if (!name) name = "";
    std::strncpy(_configured_prefix, prefix, sizeof(_configured_prefix) - 1);
    _configured_prefix[sizeof(_configured_prefix) - 1] = '\0';
    std::strncpy(_configured_name, name, sizeof(_configured_name) - 1);
    _configured_name[sizeof(_configured_name) - 1] = '\0';
    _configured_pin = pin_code;
    _init_gate.configure();
    _rx_queue.clear();
    _stats = BleSerialObserverStats{};
    refreshConnectionState();
}

bool ObservedSerialBLEInterface::initializeConfigured()
{
    BleTaskMutex::Guard guard(_task_mutex);
    return _init_gate.ensureInitialized(millis(), [this] {
        char mutable_name[sizeof(_configured_name)]{};
        std::memcpy(mutable_name, _configured_name, sizeof(mutable_name));
        SerialBLEInterface::begin(_configured_prefix, mutable_name,
                                  _configured_pin);
        if (!validateInitializedStack()) {
            _stats.init_failure_count++;
            rollbackInitialization();
            return false;
        }
        _rx_queue.clear();
        _stats.begun = true;
        _stats.begin_count++;
        refreshConnectionState();
        return true;
    });
}

bool ObservedSerialBLEInterface::validateInitializedStack()
{
    if (!BLEDevice::getInitialized()) return false;
    if (BLEDevice::setMTU(MAX_FRAME_SIZE) != ESP_OK) return false;
    if (BLEDevice::getMTU() != MAX_FRAME_SIZE) return false;
    return BLEDevice::getAdvertising() != nullptr;
}

void ObservedSerialBLEInterface::rollbackInitialization()
{
    _local_disable = true;
    _authentication_completed = true;
    _auth_watchdog.cancel();
    _rx_queue.clear();
    _connected_server = nullptr;
    if (BLEDevice::getInitialized()) BLEDevice::deinit(false);
    _local_disable = false;
    refreshConnectionState();
}

void ObservedSerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code)
{
    BleTaskMutex::Guard guard(_task_mutex);
    configure(prefix, name, pin_code);
    initializeConfigured();
}

void ObservedSerialBLEInterface::enable()
{
    BleTaskMutex::Guard guard(_task_mutex);
    if (!initializeConfigured()) return;
    SerialBLEInterface::enable();  // also clears the (now unused) base buffers
    _rx_queue.clear();
    _advertising_suppressed = false;
    _stats.enable_count++;
    applyAdvertisingThrottle(millis());
    refreshConnectionState();
}

void ObservedSerialBLEInterface::disable()
{
    BleTaskMutex::Guard guard(_task_mutex);
    _local_disable = true;
    _authentication_completed = true;
    _auth_watchdog.cancel();
    if (!_init_gate.initialized()) {
        _rx_queue.clear();
        _stats.disable_count++;
        refreshConnectionState();
        _local_disable = false;
        return;
    }
    SerialBLEInterface::disable();
    _rx_queue.clear();
    _stats.disable_count++;
    refreshConnectionState();
    _local_disable = false;
}

void ObservedSerialBLEInterface::openPairingWindow()
{
    BleTaskMutex::Guard guard(_task_mutex);
    _auth_throttle.openPairingWindow(millis());
    _advertising_suppressed = false;
    if (_init_gate.initialized() && SerialBLEInterface::isEnabled() &&
        !SerialBLEInterface::isConnected()) {
        BLEAdvertising* advertising = BLEDevice::getAdvertising();
        if (advertising) advertising->start();
    }
    refreshConnectionState();
}

bool ObservedSerialBLEInterface::isConnected() const
{
    BleTaskMutex::Guard guard(_task_mutex);
    return _init_gate.initialized() && SerialBLEInterface::isConnected();
}

uint32_t ObservedSerialBLEInterface::connectionGeneration() const
{
    BleTaskMutex::Guard guard(_task_mutex);
    return (_init_gate.initialized() && SerialBLEInterface::isConnected())
               ? _stats.connection_generation
               : 0;
}

bool ObservedSerialBLEInterface::isEnabled() const
{
    BleTaskMutex::Guard guard(_task_mutex);
    return _init_gate.initialized() && SerialBLEInterface::isEnabled();
}

BleSerialObserverStats ObservedSerialBLEInterface::stats() const
{
    BleTaskMutex::Guard guard(_task_mutex);
    return _stats;
}

size_t ObservedSerialBLEInterface::writeFrame(const uint8_t src[], size_t len)
{
    BleTaskMutex::Guard guard(_task_mutex);
    if (!_init_gate.initialized()) return 0;
    size_t written = SerialBLEInterface::writeFrame(src, len);
    if (written > 0) {
        _stats.tx_frame_count++;
        _stats.last_tx_code = src ? src[0] : 0;
    } else if (len > 0) {
        _stats.tx_drop_count++;
    }
    refreshConnectionState();
    return written;
}

size_t ObservedSerialBLEInterface::checkRecvFrame(uint8_t dest[])
{
    BleTaskMutex::Guard guard(_task_mutex);
    if (!_init_gate.initialized()) return 0;
    // Drives the base transmit queue and connection housekeeping. The base
    // receive queue stays empty (onWrite no longer feeds it), so any frame
    // returned here comes from _rx_queue.
    size_t len = SerialBLEInterface::checkRecvFrame(dest);
    uint16_t expired_conn_id = 0;
    const uint32_t now_ms = millis();
    if (_auth_watchdog.takeExpired(now_ms, expired_conn_id)) {
        _stats.auth_timeout_count++;
        _authentication_completed = true;
        recordAuthenticationFailure(now_ms);
        if (_connected_server) _connected_server->disconnect(expired_conn_id);
    }
    if (len == 0) {
        len = _rx_queue.pop(dest);
    }
    if (len > 0) {
        _stats.rx_frame_count++;
        _stats.last_rx_code = dest ? dest[0] : 0;
    }
    refreshConnectionState();
    applyAdvertisingThrottle(now_ms);
    return len;
}

bool ObservedSerialBLEInterface::removeAllBonds()
{
    BleTaskMutex::Guard guard(_task_mutex);
    if (!initializeConfigured()) return false;

    // No old credential may remain reachable while the security database is
    // being changed. Keep the service stopped even when a purge call fails.
    if (SerialBLEInterface::isEnabled()) {
        _local_disable = true;
        _authentication_completed = true;
        SerialBLEInterface::disable();
        _local_disable = false;
        _stats.disable_count++;
    }
    refreshConnectionState();
    _stats.bond_purge_attempt_count++;

    int count = esp_ble_get_bond_device_num();
    _stats.bonded_device_count = count;
    if (count < 0) {
        _stats.bond_purge_error_count++;
        return false;
    }
    if (count == 0) return true;

#if defined(CONFIG_BT_SMP_MAX_BONDS)
    static constexpr int MAX_BONDS = CONFIG_BT_SMP_MAX_BONDS;
#else
    static constexpr int MAX_BONDS = 15;
#endif
    if (count > MAX_BONDS) {
        _stats.bond_purge_error_count++;
        return false;
    }

    esp_ble_bond_dev_t bonds[MAX_BONDS]{};
    int listed = count;
    if (esp_ble_get_bond_device_list(&listed, bonds) != ESP_OK ||
        listed < 0 || listed > count) {
        _stats.bond_purge_error_count++;
        return false;
    }

    bool submitted_all = true;
    for (int i = 0; i < listed; ++i) {
        if (esp_ble_remove_bond_device(bonds[i].bd_addr) != ESP_OK) {
            submitted_all = false;
            _stats.bond_purge_error_count++;
        }
    }
    return submitted_all;
}

int ObservedSerialBLEInterface::bondedDeviceCount()
{
    BleTaskMutex::Guard guard(_task_mutex);
    if (!_init_gate.initialized()) return -1;
    const int count = esp_ble_get_bond_device_num();
    _stats.bonded_device_count = count;
    if (count < 0) _stats.bond_purge_error_count++;
    return count;
}

/*
 * BLE callbacks below can only run after initializeConfigured() creates the
 * controller, server, service, and characteristics.
 */

uint32_t ObservedSerialBLEInterface::onPassKeyRequest()
{
    BleTaskMutex::Guard guard(_task_mutex);
    return SerialBLEInterface::onPassKeyRequest();
}

void ObservedSerialBLEInterface::onPassKeyNotify(uint32_t pass_key)
{
    BleTaskMutex::Guard guard(_task_mutex);
    SerialBLEInterface::onPassKeyNotify(pass_key);
}

bool ObservedSerialBLEInterface::onConfirmPIN(uint32_t pass_key)
{
    BleTaskMutex::Guard guard(_task_mutex);
    return SerialBLEInterface::onConfirmPIN(pass_key);
}

bool ObservedSerialBLEInterface::onSecurityRequest()
{
    BleTaskMutex::Guard guard(_task_mutex);
    if (!_auth_throttle.attemptAllowed(_connecting_peer,
                                       _connecting_peer_bonded, millis())) {
        return false;
    }
    return SerialBLEInterface::onSecurityRequest();
}

void ObservedSerialBLEInterface::onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl)
{
    BleTaskMutex::Guard guard(_task_mutex);
    _auth_watchdog.cancel();
    if (cmpl.success) {
        _stats.auth_success_count++;
        _stats.connection_generation++;
        if (_stats.connection_generation == 0) _stats.connection_generation = 1;
        _auth_throttle.recordSuccess();
    } else {
        _stats.auth_failure_count++;
        recordAuthenticationFailure(millis());
    }
    _authentication_completed = true;
    SerialBLEInterface::onAuthenticationComplete(cmpl);
    refreshConnectionState();
}

void ObservedSerialBLEInterface::onConnect(BLEServer* server)
{
    BleTaskMutex::Guard guard(_task_mutex);
    _connected_server = server;
    SerialBLEInterface::onConnect(server);
    refreshConnectionState();
}

void ObservedSerialBLEInterface::onConnect(BLEServer* server,
                                           esp_ble_gatts_cb_param_t* param)
{
    BleTaskMutex::Guard guard(_task_mutex);
    _connected_server = server;
    _stats.connect_count++;
    if (param) {
        _stats.last_conn_id = param->connect.conn_id;
        if (server) _stats.last_mtu = server->getPeerMTU(param->connect.conn_id);
        std::memcpy(_connecting_peer.bytes, param->connect.remote_bda,
                    sizeof(_connecting_peer.bytes));
        _connecting_peer_bonded = peerIsBonded(_connecting_peer);
        _authentication_completed = false;
        _auth_watchdog.arm(param->connect.conn_id, millis());
    }
    SerialBLEInterface::onConnect(server, param);
    refreshConnectionState();
}

void ObservedSerialBLEInterface::onMtuChanged(BLEServer* server,
                                              esp_ble_gatts_cb_param_t* param)
{
    BleTaskMutex::Guard guard(_task_mutex);
    _stats.mtu_change_count++;
    if (server && param) {
        _stats.last_conn_id = param->mtu.conn_id;
        _stats.last_mtu = server->getPeerMTU(param->mtu.conn_id);
    }
    SerialBLEInterface::onMtuChanged(server, param);
    refreshConnectionState();
}

void ObservedSerialBLEInterface::onDisconnect(BLEServer* server)
{
    BleTaskMutex::Guard guard(_task_mutex);
    _stats.disconnect_count++;
    const bool abandoned_authentication = _auth_watchdog.armed() &&
                                          !_authentication_completed &&
                                          !_local_disable;
    _auth_watchdog.cancel();
    if (abandoned_authentication) recordAuthenticationFailure(millis());
    SerialBLEInterface::onDisconnect(server);
    // Pending frames belong to the dead connection; the base class drops its
    // own buffers on the disconnect transition, mirror that for _rx_queue.
    _rx_queue.clear();
    _connected_server = nullptr;
    _authentication_completed = false;
    refreshConnectionState();
    applyAdvertisingThrottle(millis());
}

void ObservedSerialBLEInterface::onWrite(BLECharacteristic* characteristic,
                                         esp_ble_gatts_cb_param_t* param)
{
    BleTaskMutex::Guard guard(_task_mutex);
    // NET-002 (#813): deliberately NOT forwarded to the base class. Its
    // receive queue is written here on the Bluedroid host task and drained
    // on the app loop task with no synchronization — concurrent access tears
    // the queue index and frame contents. _rx_queue locks the handoff.
    if (!characteristic) return;
    const size_t len = characteristic->getLength();
    _stats.ble_write_count++;
    const BleFrameQueuePushResult result =
        _rx_queue.tryPush(characteristic->getData(), len);
    if (rxAdmissionAction(result) == BleRxAdmissionAction::DisconnectPeer) {
        // This is a transport fault, not telemetry-only packet loss. The
        // installed callback API cannot change the ATT response status, so
        // disconnect the writer after the already-issued acknowledgement.
        _stats.ble_write_drop_count++;
        _stats.rx_fault_disconnect_count++;
        _rx_queue.clear();
        if (_connected_server && param) {
            _connected_server->disconnect(param->write.conn_id);
        } else if (_connected_server) {
            _connected_server->disconnect(_stats.last_conn_id);
        }
    }
    refreshConnectionState();
}

} // namespace comms
} // namespace sigurdos

#endif

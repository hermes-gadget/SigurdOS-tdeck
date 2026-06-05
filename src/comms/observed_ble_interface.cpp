// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "observed_ble_interface.h"

#if defined(ESP32_PLATFORM) && defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE

namespace sigurdos {
namespace comms {

void ObservedSerialBLEInterface::refreshConnectionState()
{
    _stats.enabled = isEnabled();
    _stats.connected = SerialBLEInterface::isConnected();
    _stats.advertising_expected = _stats.enabled && !_stats.connected;
}

void ObservedSerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code)
{
    SerialBLEInterface::begin(prefix, name, pin_code);
    _stats = BleSerialObserverStats{};
    _stats.begun = true;
    _stats.begin_count = 1;
    refreshConnectionState();
}

void ObservedSerialBLEInterface::enable()
{
    SerialBLEInterface::enable();
    _stats.enable_count++;
    refreshConnectionState();
}

void ObservedSerialBLEInterface::disable()
{
    SerialBLEInterface::disable();
    _stats.disable_count++;
    refreshConnectionState();
}

bool ObservedSerialBLEInterface::isConnected() const
{
    return SerialBLEInterface::isConnected();
}

size_t ObservedSerialBLEInterface::writeFrame(const uint8_t src[], size_t len)
{
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
    size_t len = SerialBLEInterface::checkRecvFrame(dest);
    if (len > 0) {
        _stats.rx_frame_count++;
        _stats.last_rx_code = dest ? dest[0] : 0;
    }
    refreshConnectionState();
    return len;
}

uint32_t ObservedSerialBLEInterface::onPassKeyRequest()
{
    return SerialBLEInterface::onPassKeyRequest();
}

void ObservedSerialBLEInterface::onPassKeyNotify(uint32_t pass_key)
{
    SerialBLEInterface::onPassKeyNotify(pass_key);
}

bool ObservedSerialBLEInterface::onConfirmPIN(uint32_t pass_key)
{
    return SerialBLEInterface::onConfirmPIN(pass_key);
}

bool ObservedSerialBLEInterface::onSecurityRequest()
{
    return SerialBLEInterface::onSecurityRequest();
}

void ObservedSerialBLEInterface::onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl)
{
    if (cmpl.success) {
        _stats.auth_success_count++;
    } else {
        _stats.auth_failure_count++;
    }
    SerialBLEInterface::onAuthenticationComplete(cmpl);
    refreshConnectionState();
}

void ObservedSerialBLEInterface::onConnect(BLEServer* server)
{
    SerialBLEInterface::onConnect(server);
    refreshConnectionState();
}

void ObservedSerialBLEInterface::onConnect(BLEServer* server,
                                           esp_ble_gatts_cb_param_t* param)
{
    _stats.connect_count++;
    if (param) {
        _stats.last_conn_id = param->connect.conn_id;
    }
    SerialBLEInterface::onConnect(server, param);
    refreshConnectionState();
}

void ObservedSerialBLEInterface::onMtuChanged(BLEServer* server,
                                              esp_ble_gatts_cb_param_t* param)
{
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
    _stats.disconnect_count++;
    SerialBLEInterface::onDisconnect(server);
    refreshConnectionState();
}

void ObservedSerialBLEInterface::onWrite(BLECharacteristic* characteristic,
                                         esp_ble_gatts_cb_param_t* param)
{
    int len = characteristic ? characteristic->getLength() : 0;
    if (len > 0) {
        _stats.ble_write_count++;
        if (len > MAX_FRAME_SIZE) {
            _stats.ble_write_drop_count++;
        }
    }
    SerialBLEInterface::onWrite(characteristic, param);
    refreshConnectionState();
}

} // namespace comms
} // namespace sigurdos

#endif

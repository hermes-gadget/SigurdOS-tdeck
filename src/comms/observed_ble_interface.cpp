// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "observed_ble_interface.h"

#if defined(ESP32_PLATFORM) && defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE

namespace sigurdos {
namespace comms {

namespace {

static constexpr const char* MESHCORE_BLE_SERVICE_UUID =
    "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char* MESHCORE_BLE_TX_UUID =
    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

} // namespace

ObservedSerialBLEInterface::ObservedSerialBLEInterface()
    : _tx_status_callbacks(*this)
{
}

void ObservedSerialBLEInterface::refreshConnectionState()
{
    _stats.enabled = isEnabled();
    _stats.connected = SerialBLEInterface::isConnected();
    _stats.advertising_expected = _stats.enabled && !_stats.connected;
}

void ObservedSerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code)
{
    SerialBLEInterface::begin(prefix, name, pin_code);
    _rx_queue.clear();
    _tx_tokens.clear();
    _delivery_events.clear();
    _stats = BleSerialObserverStats{};
    _stats.begun = true;
    _stats.begin_count = 1;
    refreshConnectionState();
}

void ObservedSerialBLEInterface::enable()
{
    SerialBLEInterface::enable();  // also clears the (now unused) base buffers
    _rx_queue.clear();
    failPendingDeliveries();
    _stats.enable_count++;
    refreshConnectionState();
}

void ObservedSerialBLEInterface::disable()
{
    SerialBLEInterface::disable();
    _rx_queue.clear();
    failPendingDeliveries();
    _stats.disable_count++;
    refreshConnectionState();
}

bool ObservedSerialBLEInterface::isConnected() const
{
    return SerialBLEInterface::isConnected();
}

size_t ObservedSerialBLEInterface::writeFrame(const uint8_t src[], size_t len)
{
    return enqueueFrame(src, len, 0);
}

size_t ObservedSerialBLEInterface::writeFrameTracked(const uint8_t src[], size_t len,
                                                     uint32_t token)
{
    if (token == 0) return 0;
    return enqueueFrame(src, len, token);
}

size_t ObservedSerialBLEInterface::enqueueFrame(const uint8_t src[], size_t len,
                                                uint32_t token)
{
    if (_tx_tokens.size() >= FRAME_QUEUE_SIZE) {
        if (len > 0) _stats.tx_drop_count++;
        return 0;
    }
    size_t written = SerialBLEInterface::writeFrame(src, len);
    if (written > 0) {
        if (!_tx_tokens.push(reinterpret_cast<const uint8_t*>(&token),
                             sizeof(token))) {
            _stats.tx_drop_count++;
            return 0;
        }
        _stats.tx_frame_count++;
        _stats.last_tx_code = src ? src[0] : 0;
    } else if (len > 0) {
        _stats.tx_drop_count++;
    }
    refreshConnectionState();
    return written;
}

bool ObservedSerialBLEInterface::pollFrameDelivery(CompanionFrameDelivery& delivery)
{
    uint8_t event[sizeof(uint32_t) + 1]{};
    if (_delivery_events.pop(event) != sizeof(event)) return false;
    std::memcpy(&delivery.token, event, sizeof(delivery.token));
    delivery.succeeded = event[sizeof(uint32_t)] != 0;
    return true;
}

size_t ObservedSerialBLEInterface::checkRecvFrame(uint8_t dest[])
{
    // Drives the base transmit queue and connection housekeeping. The base
    // receive queue stays empty (onWrite no longer feeds it), so any frame
    // returned here comes from _rx_queue.
    _notification_attempt_active =
        _tx_tokens.size() > 0 && !SerialBLEInterface::isWriteBusy();
    size_t len = SerialBLEInterface::checkRecvFrame(dest);
    _notification_attempt_active = false;
    if (len == 0) {
        len = _rx_queue.pop(dest);
    }
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
    observeTxCharacteristic(server);
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
    // Pending frames belong to the dead connection; the base class drops its
    // own buffers on the disconnect transition, mirror that for _rx_queue.
    _rx_queue.clear();
    failPendingDeliveries();
    refreshConnectionState();
}

void ObservedSerialBLEInterface::TxStatusCallbacks::onStatus(
    BLECharacteristic* characteristic, Status status, uint32_t code)
{
    (void)characteristic;
    _owner.handleNotificationStatus(status, code);
}

void ObservedSerialBLEInterface::observeTxCharacteristic(BLEServer* server)
{
    if (!server) return;
    BLEService* service = server->getServiceByUUID(MESHCORE_BLE_SERVICE_UUID);
    if (!service) return;
    BLECharacteristic* tx = service->getCharacteristic(MESHCORE_BLE_TX_UUID);
    if (tx) tx->setCallbacks(&_tx_status_callbacks);
}

void ObservedSerialBLEInterface::handleNotificationStatus(
    ::BLECharacteristicCallbacks::Status status, uint32_t code)
{
    (void)code;
    if (!_notification_attempt_active) return;
    _notification_attempt_active = false;

    uint32_t token = 0;
    if (_tx_tokens.pop(reinterpret_cast<uint8_t*>(&token)) != sizeof(token)) return;
    const bool succeeded = status == ::BLECharacteristicCallbacks::SUCCESS_NOTIFY;
    if (succeeded) _stats.notify_success_count++;
    else _stats.notify_failure_count++;
    if (token == 0) return;

    uint8_t event[sizeof(uint32_t) + 1]{};
    std::memcpy(event, &token, sizeof(token));
    event[sizeof(uint32_t)] = succeeded ? 1 : 0;
    _delivery_events.push(event, sizeof(event));
}

void ObservedSerialBLEInterface::failPendingDeliveries()
{
    uint32_t token = 0;
    while (_tx_tokens.pop(reinterpret_cast<uint8_t*>(&token)) == sizeof(token)) {
        if (token == 0) continue;
        uint8_t event[sizeof(uint32_t) + 1]{};
        std::memcpy(event, &token, sizeof(token));
        _delivery_events.push(event, sizeof(event));
    }
    _notification_attempt_active = false;
}

void ObservedSerialBLEInterface::onWrite(BLECharacteristic* characteristic,
                                         esp_ble_gatts_cb_param_t* param)
{
    (void)param;
    // NET-002 (#813): deliberately NOT forwarded to the base class. Its
    // receive queue is written here on the Bluedroid host task and drained
    // on the app loop task with no synchronization — concurrent access tears
    // the queue index and frame contents. _rx_queue locks the handoff.
    if (!characteristic) return;
    const size_t len = characteristic->getLength();
    if (len == 0) return;
    _stats.ble_write_count++;
    if (!_rx_queue.push(characteristic->getData(), len)) {
        _stats.ble_write_drop_count++;  // oversize frame or queue full
    }
    refreshConnectionState();
}

} // namespace comms
} // namespace sigurdos

#endif

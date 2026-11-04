/**
 * @file TinyCTSServerNimBLE.h
 * @brief Bluetooth Current Time Service (CTS) server for NimBLE-Arduino
 * (ESP32, nRF5). Use it through TinyCTSServer.h.
 */

#pragma once
#include <NimBLEDevice.h>

#include "../TinyCTSServerBase.h"

namespace tiny_cts {

/**
 * @class TinyCTSServer
 * @brief A BLE peripheral that offers the Current Time Service (0x1805) and
 * serves the time of a time source to the BLE clients (phones, watches, other
 * boards running a TinyCTSClient, `tinycts` on the desktop).
 *
 * It provides the characteristics Current Time (read, notify), Local Time
 * Information and Reference Time Information (read). While the time source
 * returns 0 (time not known) the Current Time is reported with an unknown date.
 *
 * Call begin() once and process() in loop() if you want periodic
 * notifications (setNotifyInterval()). The settings are in TinyCTSServerBase.
 *
 * @code
 * TinyCTSClockSource clock;
 * TinyCTSServer server;
 * void setup() {
 *   clock.setTime(rtc.now().unixtime());
 *   server.setTimeZone(60, 60);  // CET, summer time
 *   server.begin(clock, "my-clock");
 * }
 * void loop() { server.process(); }
 * @endcode
 */
class TinyCTSServer : public TinyCTSServerBase, public NimBLECharacteristicCallbacks {
 public:
  TinyCTSServer() = default;

  /**
   * @brief Start the service and the advertising. NimBLEDevice::init() is
   * called with the name, if the NimBLE stack is not initialized yet.
   * @param source The time source. It must stay valid as long as the server is
   * used.
   * @param deviceName The name of the BLE device.
   * @return true on success.
   */
  bool begin(TinyCTSTimeSource& source, const char* deviceName = "TinyCTS") {
    _source = &source;
    if (!NimBLEDevice::isInitialized()) NimBLEDevice::init(deviceName);
    if (_current != nullptr) {
      // started before (and stopped with end()): the service exists already
      updateCurrent(0);
      updateLocal();
      updateReference();
      return startAdvertising();
    }
    NimBLEServer* server = NimBLEDevice::getServer();
    if (server == nullptr) server = NimBLEDevice::createServer();
    if (server == nullptr) return false;
    server->advertiseOnDisconnect(true);

    NimBLEService* service = server->createService(NimBLEUUID(CTS_SERVICE_UUID));
    if (service == nullptr) return false;

    uint32_t current = NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY;
    uint32_t local = NIMBLE_PROPERTY::READ;
    if (_settable) {
      current |= NIMBLE_PROPERTY::WRITE;
      local |= NIMBLE_PROPERTY::WRITE;
      if (_encrypted) {
        current |= NIMBLE_PROPERTY::WRITE_ENC;
        local |= NIMBLE_PROPERTY::WRITE_ENC;
      }
    }
    _current = service->createCharacteristic(NimBLEUUID(CTS_CURRENT_TIME_UUID), current);
    _local = service->createCharacteristic(NimBLEUUID(CTS_LOCAL_TIME_INFO_UUID), local);
    _reference = service->createCharacteristic(
        NimBLEUUID(CTS_REFERENCE_TIME_INFO_UUID), NIMBLE_PROPERTY::READ);
    if (_current == nullptr || _local == nullptr || _reference == nullptr) return false;
    _current->setCallbacks(this);
    _local->setCallbacks(this);
    _reference->setCallbacks(this);
    updateCurrent(0);
    updateLocal();
    updateReference();
    if (!server->start()) return false;  // starts the services

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NimBLEUUID(CTS_SERVICE_UUID));
    adv->enableScanResponse(true);
    return startAdvertising();
  }

  /**
   * @brief Stop advertising and disconnect the clients. The time is reported
   * as unknown until begin() is called again.
   */
  void end() {
    NimBLEDevice::getAdvertising()->stop();
    NimBLEServer* server = NimBLEDevice::getServer();
    if (server != nullptr) {
      for (uint16_t handle : server->getPeerDevices()) server->disconnect(handle);
    }
    _source = nullptr;
    if (_current != nullptr) updateCurrent(0);
  }

  /** @brief Number of connected clients. */
  int getConnectedCount() override {
    NimBLEServer* server = NimBLEDevice::getServer();
    return server != nullptr ? server->getConnectedCount() : 0;
  }

  /**
   * @brief Call this regularly in loop(): it sends the notifications.
   * @return true if the Current Time was notified.
   */
  bool process() {
    if (_current == nullptr || !notifyDue()) return false;
    return notifyTime(0);
  }

  /** @brief Notify the subscribed clients now. */
  bool notifyTime(uint8_t adjustReason = 0) {
    if (_current == nullptr) return false;
    updateCurrent(adjustReason);
    return _current->notify();
  }

 protected:
  void onTimeZoneChanged() override {
    if (_local == nullptr) return;
    updateLocal();
    notifyTime(TinyCTSCurrentTime::TimeZoneChange);
  }

  void onReferenceChanged() override {
    if (_reference != nullptr) updateReference();
  }

  void onRead(NimBLECharacteristic* chr, NimBLEConnInfo&) override {
    if (chr == _current) updateCurrent(0);
  }

  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo&) override {
    NimBLEAttValue value = chr->getValue();
    if (chr == _current) {
      if (setTimeFromWrite(value.data(), value.size())) {
        notifyTime(TinyCTSCurrentTime::ManualUpdate);
      } else {
        updateCurrent(0);  // refused: show the real time again
      }
    } else if (chr == _local) {
      setTimeZoneFromWrite(value.data(), value.size());
      updateLocal();
      notifyTime(TinyCTSCurrentTime::TimeZoneChange);
    }
  }

  bool startAdvertising() {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    if (_advertisingMs > 0) {
      // units of 0.625 ms
      const uint16_t units = static_cast<uint16_t>(_advertisingMs * 8 / 5);
      adv->setMinInterval(units);
      adv->setMaxInterval(units);
    }
    return adv->start();
  }

  void updateCurrent(uint8_t reason) {
    uint8_t data[TinyCTSCurrentTime::Size];
    _current->setValue(data, currentTimeValue(reason, data));
  }

  void updateLocal() {
    uint8_t data[TinyCTSLocalTimeInfo::Size];
    _local->setValue(data, localTimeValue(data));
  }

  void updateReference() {
    uint8_t data[TinyCTSReferenceTimeInfo::Size];
    _reference->setValue(data, referenceTimeValue(data));
  }

  NimBLECharacteristic* _current = nullptr;
  NimBLECharacteristic* _local = nullptr;
  NimBLECharacteristic* _reference = nullptr;
};

}  // namespace tiny_cts

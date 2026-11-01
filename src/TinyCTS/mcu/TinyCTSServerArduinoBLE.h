/**
 * @file TinyCTSServerArduinoBLE.h
 * @brief Bluetooth Current Time Service (CTS) server for the ArduinoBLE
 * library (Nano 33 BLE, Nano 33 IoT, Uno R4 WiFi, ...). Use it through
 * TinyCTSServer.h.
 */

#pragma once
#include "TinyCTSArduinoBLE.h"
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
 * begin() starts the BLE hardware (BLE.begin()). Call process() in loop(): it
 * calls BLE.poll(), which handles the requests of the clients. Only one server
 * per program is possible. The settings are in TinyCTSServerBase.
 *
 * With enableTimeSetting() the writable characteristics need an encrypted
 * connection: ArduinoBLE then asks for it also for reading them.
 *
 * @code
 * TinyCTSClockSource clock;
 * TinyCTSServer server;
 * void setup() {
 *   clock.setTime(1767225600);
 *   server.setTimeZone(60, 0);  // CET
 *   server.begin(clock, "my-clock");
 * }
 * void loop() { server.process(); }
 * @endcode
 */
class TinyCTSServer : public TinyCTSServerBase {
 public:
  TinyCTSServer() = default;

  /**
   * @brief Start the BLE hardware, the service and the advertising.
   * @param source The time source. It must stay valid as long as the server is
   * used.
   * @param deviceName The name of the BLE device.
   * @return true on success.
   */
  bool begin(TinyCTSTimeSource& source, const char* deviceName = "TinyCTS") {
    _source = &source;
    if (!ctsArduinoBleBegin()) return false;
    instance() = this;
    if (_service == nullptr) {
      uint16_t encryption = _settable && _encrypted ? BLEEncryption : 0;
      uint16_t write = _settable ? BLEWrite : 0;
      _service = new BLEService("1805");
      _current = new BLECharacteristic("2A2B", BLERead | BLENotify | write | encryption,
                                       TinyCTSCurrentTime::Size);
      _local = new BLECharacteristic("2A0F", BLERead | write | encryption,
                                     TinyCTSLocalTimeInfo::Size);
      _reference = new BLECharacteristic("2A14", BLERead, TinyCTSReferenceTimeInfo::Size);
      _current->setEventHandler(BLERead, onReadCurrent);
      if (_settable) {
        _current->setEventHandler(BLEWritten, onWriteCurrent);
        _local->setEventHandler(BLEWritten, onWriteLocal);
      }
      _service->addCharacteristic(*_current);
      _service->addCharacteristic(*_local);
      _service->addCharacteristic(*_reference);
      BLE.addService(*_service);
    }
    updateCurrent(0);
    updateLocal();
    updateReference();
    BLE.setLocalName(deviceName);
    BLE.setDeviceName(deviceName);
    BLE.setAdvertisedService(*_service);
    return BLE.advertise() != 0;
  }

  /** @brief Stop advertising. */
  void end() {
    BLE.stopAdvertise();
    _source = nullptr;
    instance() = nullptr;
  }

  /**
   * @brief Call this regularly in loop(): it handles the requests of the
   * clients and sends the notifications.
   * @return true if the Current Time was notified.
   */
  bool process() {
    if (_service == nullptr) return false;
    BLE.poll();
    if (!notifyDue()) return false;
    return notifyTime(0);
  }

  /** @brief Notify the subscribed clients now. */
  bool notifyTime(uint8_t adjustReason = 0) {
    if (_current == nullptr) return false;
    updateCurrent(adjustReason);
    return true;
  }

 protected:
  static TinyCTSServer*& instance() {
    static TinyCTSServer* server = nullptr;
    return server;
  }

  void onTimeZoneChanged() override {
    if (_local == nullptr) return;
    updateLocal();
    notifyTime(TinyCTSCurrentTime::TimeZoneChange);
  }

  void onReferenceChanged() override {
    if (_reference != nullptr) updateReference();
  }

  // the events of ArduinoBLE: plain functions without a context
  static void onReadCurrent(BLEDevice, BLECharacteristic) {
    if (instance() != nullptr) instance()->updateCurrent(0);
  }

  static void onWriteCurrent(BLEDevice, BLECharacteristic chr) {
    TinyCTSServer* s = instance();
    if (s == nullptr) return;
    if (s->setTimeFromWrite(chr.value(), chr.valueLength())) {
      s->updateCurrent(TinyCTSCurrentTime::ManualUpdate);
    } else {
      s->updateCurrent(0);  // refused: show the real time again
    }
  }

  static void onWriteLocal(BLEDevice, BLECharacteristic chr) {
    TinyCTSServer* s = instance();
    if (s == nullptr) return;
    s->setTimeZoneFromWrite(chr.value(), chr.valueLength());
    s->updateLocal();
    s->updateCurrent(TinyCTSCurrentTime::TimeZoneChange);
  }

  // (writing the value notifies the subscribed clients)
  void updateCurrent(uint8_t reason) {
    uint8_t data[TinyCTSCurrentTime::Size];
    _current->writeValue(data, static_cast<int>(currentTimeValue(reason, data)));
  }

  void updateLocal() {
    uint8_t data[TinyCTSLocalTimeInfo::Size];
    _local->writeValue(data, static_cast<int>(localTimeValue(data)));
  }

  void updateReference() {
    uint8_t data[TinyCTSReferenceTimeInfo::Size];
    _reference->writeValue(data, static_cast<int>(referenceTimeValue(data)));
  }

  BLEService* _service = nullptr;
  BLECharacteristic* _current = nullptr;
  BLECharacteristic* _local = nullptr;
  BLECharacteristic* _reference = nullptr;
};

}  // namespace tiny_cts

/**
 * @file TinyCTSServerPico.h
 * @brief Bluetooth Current Time Service (CTS) server for the Raspberry Pi Pico
 * W / Pico 2 W (BLE library of the arduino-pico core, based on BTstack). Use it
 * through TinyCTSServer.h.
 */

#pragma once
#include <BluetoothLock.h>

#include <cstdlib>
#include <cstring>

#include "TinyCTSPicoBLE.h"
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
 * Select "Tools -> IP/Bluetooth Stack -> IPv4 + Bluetooth". begin() starts the
 * BLE hardware: do not call BLE.begin() yourself. The Bluetooth callbacks read
 * the time source: it must be fast and not block. Call process() in loop() if
 * you want periodic notifications (setNotifyInterval()). The settings are in
 * TinyCTSServerBase.
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
class TinyCTSServer : public TinyCTSServerBase,
                      public BLECharacteristicCallbacks,
                      public BLEServerCallbacks {
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
    bool pairing = _settable && _encrypted;
    if (!ctsPicoBegin(deviceName, pairing)) return false;
    if (_service != nullptr) {
      // started before (and stopped with end()): the service exists already
      updateCurrent(0, false);
      updateLocal();
      updateReference();
      BLE.startAdvertising();
      return true;
    }

    // writing needs an encrypted connection (ATT_SECURITY_ENCRYPTED = 1)
    uint8_t writePerm = pairing ? 1 : 0;
    uint16_t write = _settable ? BLEWrite : 0;
    _service = new BLEService(BLEUUID(CTS_SERVICE_UUID));
    _current = new Value(BLEUUID(CTS_CURRENT_TIME_UUID), BLERead | BLENotify | write, writePerm);
    _local = new Value(BLEUUID(CTS_LOCAL_TIME_INFO_UUID), BLERead | write, writePerm);
    _reference = new Value(BLEUUID(CTS_REFERENCE_TIME_INFO_UUID), BLERead, 0);
    _current->setCallbacks(this);
    _local->setCallbacks(this);
    _reference->setCallbacks(this);
    updateCurrent(0, false);
    updateLocal();
    updateReference();
    _service->addCharacteristic(_current);
    _service->addCharacteristic(_local);
    _service->addCharacteristic(_reference);
    BLE.server()->setCallbacks(this);  // counts the connections
    BLE.server()->addService(_service);
    BLE.startAdvertising();
    return true;
  }

  /** @brief Stop advertising. */
  void end() {
    BLE.stopAdvertising();
    _source = nullptr;
  }

  /**
   * @brief Call this regularly in loop(): it sends the notifications.
   * @return true if the Current Time was notified.
   */
  bool process() {
    if (_current == nullptr || !notifyDue()) return false;
    return notifyTime(0);
  }

  /**
   * @brief Number of connected clients. The server callbacks of the BLE
   * library are used for this: do not replace them with setCallbacks().
   */
  int getConnectedCount() override { return _connected; }

  /** @brief Notify the subscribed clients now. */
  bool notifyTime(uint8_t adjustReason = 0) {
    if (_current == nullptr) return false;
    BluetoothLock lock;  // the callbacks of the stack use the time source too
    updateCurrent(adjustReason, true);
    return true;
  }

 protected:
  /// A characteristic that is written without a notification (the base class
  /// notifies with every setValue()): the value is the one of the last
  /// notification or read.
  class Value : public BLECharacteristic {
   public:
    Value(BLEUUID uuid, uint16_t flags, uint8_t writePerm)
        : BLECharacteristic(uuid, flags, nullptr, 0, writePerm) {}

    void set(const uint8_t* data, size_t len, bool notify) {
      _charData = realloc(_charData, len);
      memcpy(_charData, data, len);
      _charLen = len;
      if (notify) requestNotify();
    }
  };

  void onTimeZoneChanged() override {
    if (_local == nullptr) return;
    BluetoothLock lock;
    updateLocal();
    updateCurrent(TinyCTSCurrentTime::TimeZoneChange, true);
  }

  void onReferenceChanged() override {
    if (_reference == nullptr) return;
    BluetoothLock lock;
    updateReference();
  }

  // (run in the context of the Bluetooth stack)
  void onConnect(BLEServer*) override { _connected++; }
  void onDisconnect(BLEServer*) override {
    if (_connected > 0) _connected--;
  }

  void onRead(BLECharacteristic* chr) override {
    if (chr == _current) updateCurrent(0, false);
  }

  void onWrite(BLECharacteristic* chr) override {
    const uint8_t* data = static_cast<const uint8_t*>(chr->valueData());
    size_t len = chr->valueLen();
    if (chr == _current) {
      if (setTimeFromWrite(data, len)) {
        updateCurrent(TinyCTSCurrentTime::ManualUpdate, true);
      } else {
        updateCurrent(0, false);  // refused: show the real time again
      }
    } else if (chr == _local) {
      setTimeZoneFromWrite(data, len);
      updateLocal();
      updateCurrent(TinyCTSCurrentTime::TimeZoneChange, true);
    }
  }

  void updateCurrent(uint8_t reason, bool notify) {
    uint8_t data[TinyCTSCurrentTime::Size];
    _current->set(data, currentTimeValue(reason, data), notify);
  }

  void updateLocal() {
    uint8_t data[TinyCTSLocalTimeInfo::Size];
    _local->set(data, localTimeValue(data), false);
  }

  void updateReference() {
    uint8_t data[TinyCTSReferenceTimeInfo::Size];
    _reference->set(data, referenceTimeValue(data), false);
  }

  BLEService* _service = nullptr;
  Value* _current = nullptr;
  Value* _local = nullptr;
  Value* _reference = nullptr;
  volatile int _connected = 0;
};

}  // namespace tiny_cts

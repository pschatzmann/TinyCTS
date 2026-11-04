/**
 * @file TinyCTSServerDesktop.h
 * @brief Bluetooth Current Time Service (CTS) server for the desktop: BlueZ
 * (Linux), CoreBluetooth (macOS) or WinRT (Windows). Use it through
 * TinyCTSServer.h.
 */

#pragma once
#include <mutex>

#include "TinyCTSBackend.h"
#include "../TinyCTSServerBase.h"

namespace tiny_cts {

/**
 * @class TinyCTSServer
 * @brief A BLE peripheral that offers the Current Time Service (0x1805) and
 * serves the time of a time source to the BLE clients (phones, watches, boards
 * with a TinyCTSClient).
 *
 * This is the version for the desktop. The computer must have a Bluetooth
 * adapter that can be a peripheral. Call process() in your loop: on Linux it
 * handles the requests of BlueZ, on the other systems they are answered by a
 * thread of the system, which also calls the time source. The settings are in
 * TinyCTSServerBase. Only one server per process is possible.
 *
 * The name of the device is only used on Linux and macOS: Windows advertises
 * the name of the computer.
 *
 * @code
 * TinyCTSSystemSource source;
 * TinyCTSServer server;
 * server.setTimeZone(60, 0);
 * if (!server.begin(source, "my-computer")) fprintf(stderr, "%s\n", server.getLastError().c_str());
 * while (running) { server.process(); sleep_ms(10); }
 * server.end();
 * @endcode
 */
class TinyCTSServer : public TinyCTSServerBase, private TinyCTSPeripheralHandler {
 public:
  TinyCTSServer() = default;
  ~TinyCTSServer() override { end(); }

  /** @brief The Bluetooth adapter to use (Linux only, default: hci0). Call it before begin(). */
  void setAdapter(const char* name) { _adapterName = name; }

  /**
   * @brief Register the service and start the advertising.
   * @param source The time source. It must stay valid as long as the server is
   * used.
   * @param deviceName The name of the BLE device.
   * @return true on success. The reason is in getLastError().
   */
  bool begin(TinyCTSTimeSource& source, const char* deviceName = "TinyCTS") {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _source = &source;
    _error.clear();
    _peripheral = ctsCreatePeripheral(_adapterName);
    if (!_peripheral) {
      _error = "no Bluetooth stack";
      return false;
    }
    if (!_peripheral->start(deviceName, _settable, _encrypted, this, _error)) {
      _peripheral.reset();
      return false;
    }
    return true;
  }

  /** @brief Unregister the service and stop the advertising. */
  void end() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_peripheral) _peripheral->stop();
    _peripheral.reset();
    _source = nullptr;
  }

  /**
   * @brief Handle the requests of the clients and send the notifications. Call
   * this regularly in loop().
   * @return true if the Current Time was notified.
   */
  bool process() {
    {
      std::lock_guard<std::recursive_mutex> lock(_mutex);
      if (!_peripheral) return false;
      _peripheral->process();
      if (!notifyDue()) return false;
    }
    return notifyTime(0);
  }

  /** @brief Notify the subscribed clients now. */
  bool notifyTime(uint8_t adjustReason = 0) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!_peripheral) return false;
    uint8_t data[TinyCTSCurrentTime::Size];
    size_t len = currentTimeValue(adjustReason, data);
    _peripheral->notify(CTS_CURRENT_TIME_UUID, std::vector<uint8_t>(data, data + len));
    return true;
  }

  /** @brief The reason why begin() failed. */
  const std::string& getLastError() const { return _error; }

  /**
   * @brief Number of connected clients: on Linux the devices connected to the
   * adapter, on macOS and Windows the clients that subscribed to the Current
   * Time (the only connections these systems report). 0 when stopped.
   */
  int getConnectedCount() override {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _peripheral ? _peripheral->connectedCount() : 0;
  }

  /** @brief The number of values that the clients have read. */
  uint32_t getRequestCount() const { return _requests; }

 protected:
  void onTimeZoneChanged() override { notifyTime(TinyCTSCurrentTime::TimeZoneChange); }

  bool onRead(uint16_t uuid, std::vector<uint8_t>& value) override {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    value.resize(TinyCTSCurrentTime::Size);
    if (uuid == CTS_CURRENT_TIME_UUID) {
      value.resize(currentTimeValue(0, value.data()));
    } else if (uuid == CTS_LOCAL_TIME_INFO_UUID) {
      value.resize(localTimeValue(value.data()));
    } else if (uuid == CTS_REFERENCE_TIME_INFO_UUID) {
      value.resize(referenceTimeValue(value.data()));
    } else {
      return false;
    }
    _requests++;
    return true;
  }

  bool onWrite(uint16_t uuid, const uint8_t* data, size_t len) override {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (uuid == CTS_CURRENT_TIME_UUID && setTimeFromWrite(data, len)) {
      notifyTime(TinyCTSCurrentTime::ManualUpdate);
      return true;
    }
    if (uuid == CTS_LOCAL_TIME_INFO_UUID && setTimeZoneFromWrite(data, len)) {
      notifyTime(TinyCTSCurrentTime::TimeZoneChange);
      return true;
    }
    return false;
  }

  std::string _adapterName = "hci0", _error;
  std::unique_ptr<TinyCTSPeripheral> _peripheral;
  std::recursive_mutex _mutex;
  uint32_t _requests = 0;
};

}  // namespace tiny_cts

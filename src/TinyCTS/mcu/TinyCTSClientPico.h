/**
 * @file TinyCTSClientPico.h
 * @brief Bluetooth Current Time Service (CTS) client for the Raspberry Pi Pico
 * W / Pico 2 W (BLE library of the arduino-pico core, based on BTstack). Use it
 * through TinyCTSClient.h.
 */

#pragma once
#include "../TinyCTSClientBase.h"
#include "TinyCTSPicoBLE.h"

namespace tiny_cts {

/**
 * @class TinyCTSClient
 * @brief Reads the time from a BLE device that offers the Current Time Service
 * (a phone, a watch, a TinyCTSServer, ...). It is also a TinyCTSTimeSource, so
 * the time can be served by a TinyCTSServer.
 *
 * Select "Tools -> IP/Bluetooth Stack -> IPv4 + Bluetooth". begin() starts the
 * BLE hardware (do not call BLE.begin() yourself), scans for the device,
 * connects, reads the characteristics and disconnects: the time is then
 * extrapolated with millis(). Call update() from time to time (e.g. once per
 * hour) to stay in sync. The functions block until they are done. The getters
 * are in TinyCTSClientBase.
 *
 * @code
 * TinyCTSClient cts;
 * void setup() {
 *   if (cts.begin()) Serial.println((uint32_t)cts.getTime());
 * }
 * @endcode
 */
class TinyCTSClient : public TinyCTSClientBase {
 public:
  /**
   * @param device The BLE address ("aa:bb:cc:dd:ee:ff") or the name of the
   * device. nullptr (default): the first device that offers the service.
   * @param scanMs Time to look for the device (rounded up to seconds).
   */
  explicit TinyCTSClient(const char* device = nullptr, uint32_t scanMs = 5000)
      : _scanMs(scanMs) {
    if (device != nullptr) _device = device;
  }

  /**
   * @brief Start the BLE hardware, find the device and read the time.
   * @return true if the time was read.
   */
  bool begin() {
    if (!ctsPicoBegin("", false)) return false;
    return update();
  }

  /**
   * @brief Read the time (and the other characteristics) again: the device is
   * connected, read and disconnected.
   * @return true if the time was read.
   */
  bool update() override {
    BLEClient* client = connect();
    if (client == nullptr) return false;
    bool ok = false;
    BLERemoteService* service = client->service(BLEUUID(CTS_SERVICE_UUID));
    if (service == nullptr) _error = "the device has no Current Time Service";
    if (service != nullptr) {
      uint8_t data[16];
      size_t len;
      if (read(service->characteristic(BLEUUID(CTS_LOCAL_TIME_INFO_UUID)), data, len)) {
        storeLocalInfo(data, len);
      }
      if (read(service->characteristic(BLEUUID(CTS_REFERENCE_TIME_INFO_UUID)), data, len)) {
        storeReferenceInfo(data, len);
      }
      BLERemoteCharacteristic* chr = service->characteristic(BLEUUID(CTS_CURRENT_TIME_UUID));
      uint32_t t0 = TINY_CTS_MILLIS();
      if (read(chr, data, len)) {
        ok = storeCurrentTime(data, len, t0, TINY_CTS_MILLIS());
      } else {
        _error = "the Current Time cannot be read";
      }
    }
    client->disconnect();
    return ok;
  }

  /**
   * @brief Set the time of the device by writing the Current Time. It works
   * if the device allows it (and the connection is paired, if it requires it).
   * @param unixMs The UTC time in ms since 1970-01-01.
   */
  bool setTimeMs(uint64_t unixMs) override {
    BLEClient* client = connect();
    if (client == nullptr) return false;
    bool ok = false;
    BLERemoteService* service = client->service(BLEUUID(CTS_SERVICE_UUID));
    BLERemoteCharacteristic* chr =
        service != nullptr ? service->characteristic(BLEUUID(CTS_CURRENT_TIME_UUID)) : nullptr;
    if (chr != nullptr && chr->canWrite()) {
      uint8_t data[TinyCTSCurrentTime::Size];
      ok = chr->setValue(data, timeToWrite(unixMs, data));
    }
    client->disconnect();
    return ok;
  }

 protected:
  BLEClient* connect() {
    _error.clear();
    uint32_t seconds = (_scanMs + 999) / 1000;
    if (seconds == 0) seconds = 1;
    // the scan with the UUID keeps the devices that advertise the service
    BLEScanReport* report =
        _device.empty() ? BLE.scan(BLEUUID(CTS_SERVICE_UUID), seconds) : BLE.scan(seconds);
    BLEClient* client = BLE.client();
    bool connected = false;
    for (auto& item : *report) {
      std::string address = item.getAddress().toString().c_str();
      std::string name = item.getName();
      if (!_device.empty() && !isWanted(address, name)) continue;
      connected = client->connect(item, 10);
      if (connected) {
        _address = address;
        _name = name;
      }
      break;
    }
    BLE.clearScan();
    if (!connected) _error = "no device found or cannot connect";
    return connected ? client : nullptr;
  }

  /// Reads the value from the device (the get...() functions of the stack read
  /// it first) and copies it.
  static bool read(BLERemoteCharacteristic* chr, uint8_t* out, size_t& len) {
    if (chr == nullptr || !chr->canRead()) return false;
    chr->getUInt8();
    const void* value = chr->valueData();
    len = chr->valueLen();
    if (value == nullptr || len == 0) return false;
    if (len > 16) len = 16;
    memcpy(out, value, len);
    return true;
  }

  uint32_t _scanMs;
};

}  // namespace tiny_cts

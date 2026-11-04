/**
 * @file TinyCTSClientArduinoBLE.h
 * @brief Bluetooth Current Time Service (CTS) client for the ArduinoBLE
 * library (Nano 33 BLE, Nano 33 IoT, Uno R4 WiFi, ...). Use it through
 * TinyCTSClient.h.
 */

#pragma once
#include "TinyCTSArduinoBLE.h"
#include "../TinyCTSClientBase.h"

namespace tiny_cts {

/**
 * @class TinyCTSClient
 * @brief Reads the time from a BLE device that offers the Current Time Service
 * (a phone, a watch, a TinyCTSServer, ...). It is also a TinyCTSTimeSource, so
 * the time can be served by a TinyCTSServer.
 *
 * begin() starts the BLE hardware (BLE.begin()), scans for the device,
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
   * @param scanMs Time to look for the device.
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
    if (!ctsArduinoBleBegin()) return false;
    return update();
  }

  /**
   * @brief Read the time (and the other characteristics) again: the device is
   * connected, read and disconnected.
   * @return true if the time was read.
   */
  bool update() override {
    BLEDevice peripheral;
    if (!connect(peripheral)) return false;
    uint8_t data[16];
    size_t len;
    if (read(peripheral, "2a0f", data, len)) storeLocalInfo(data, len);
    if (read(peripheral, "2a14", data, len)) storeReferenceInfo(data, len);
    uint32_t t0 = TINY_CTS_MILLIS();
    bool ok = false;
    if (!read(peripheral, "2a2b", data, len)) {
      _error = "the Current Time cannot be read";
    } else {
      ok = storeCurrentTime(data, len, t0, TINY_CTS_MILLIS());
    }
    peripheral.disconnect();
    return ok;
  }

  /**
   * @brief Set the time of the device by writing the Current Time. It works
   * if the device allows it (and the connection is paired, if it requires it).
   * @param unixMs The UTC time in ms since 1970-01-01.
   */
  bool setTimeMs(uint64_t unixMs) override {
    BLEDevice peripheral;
    if (!connect(peripheral)) return false;
    bool ok = false;
    BLECharacteristic chr = peripheral.characteristic("2a2b");
    if (chr && chr.canWrite()) {
      uint8_t data[TinyCTSCurrentTime::Size];
      ok = chr.writeValue(data, static_cast<int>(timeToWrite(unixMs, data))) != 0;
    }
    peripheral.disconnect();
    return ok;
  }

 protected:
  /// true if the device is advertising the Current Time Service
  static bool offersService(BLEDevice& d) {
    for (int i = 0; i < d.advertisedServiceUuidCount(); i++) {
      if (d.advertisedServiceUuid(i).equalsIgnoreCase("1805")) return true;
    }
    return false;
  }

  bool connect(BLEDevice& peripheral) {
    _error.clear();
    BLE.scan();
    uint32_t start = millis();
    bool found = false;
    while (!found && static_cast<uint32_t>(millis() - start) < _scanMs) {
      BLEDevice d = BLE.available();
      if (!d) continue;
      std::string address = d.address().c_str();
      std::string name = d.hasLocalName() ? d.localName().c_str() : "";
      if (_device.empty() ? offersService(d) : isWanted(address, name)) {
        peripheral = d;
        _address = address;
        _name = name;
        found = true;
      }
    }
    BLE.stopScan();
    if (!found) {
      _error = "no device found";
      return false;
    }
    if (!peripheral.connect()) {
      _error = "cannot connect";
      return false;
    }
    if (!peripheral.discoverAttributes()) {
      peripheral.disconnect();
      _error = "cannot discover the services";
      return false;
    }
    return true;
  }

  static bool read(BLEDevice& peripheral, const char* uuid, uint8_t* out, size_t& len) {
    BLECharacteristic chr = peripheral.characteristic(uuid);
    if (!chr || !chr.canRead() || !chr.read()) return false;
    len = static_cast<size_t>(chr.valueLength());
    if (len == 0) return false;
    if (len > 16) len = 16;
    memcpy(out, chr.value(), len);
    return true;
  }

  uint32_t _scanMs;
};

}  // namespace tiny_cts

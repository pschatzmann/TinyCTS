/**
 * @file TinyCTSClientNimBLE.h
 * @brief Bluetooth Current Time Service (CTS) client for NimBLE-Arduino
 * (ESP32, nRF5). Use it through TinyCTSClient.h.
 */

#pragma once
#include <NimBLEDevice.h>

#include "../TinyCTSClientBase.h"

namespace tiny_cts {

/**
 * @class TinyCTSClient
 * @brief Reads the time from a BLE device that offers the Current Time Service
 * (a phone, a watch, a TinyCTSServer, ...). It is also a TinyCTSTimeSource, so
 * the time can be served by a TinyCTSServer.
 *
 * begin() scans for the device, connects, reads the characteristics and
 * disconnects: the time is then extrapolated with millis(). Call update() from
 * time to time (e.g. once per hour) to stay in sync. The getters are in
 * TinyCTSClientBase.
 *
 * @code
 * TinyCTSClient cts;
 * void setup() {
 *   if (cts.begin()) Serial.println(cts.getTimeMs());
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
   * @brief Find the device and read the time. NimBLEDevice::init() is called,
   * if the NimBLE stack is not initialized yet.
   * @return true if the time was read.
   */
  bool begin() {
    if (!NimBLEDevice::isInitialized()) NimBLEDevice::init("");
    return update();
  }

  /**
   * @brief Read the time (and the other characteristics) again: the device is
   * connected, read and disconnected.
   * @return true if the time was read.
   */
  bool update() override {
    NimBLEClient* client = connect();
    if (client == nullptr) return false;
    bool ok = false;
    NimBLERemoteService* service = client->getService(NimBLEUUID(CTS_SERVICE_UUID));
    if (service == nullptr) {
      _error = "the device has no Current Time Service";
    } else {
      readInfo(service);
      NimBLERemoteCharacteristic* chr =
          service->getCharacteristic(NimBLEUUID(CTS_CURRENT_TIME_UUID));
      if (chr == nullptr || !chr->canRead()) {
        _error = "the Current Time cannot be read";
      } else {
        uint32_t t0 = TINY_CTS_MILLIS();
        NimBLEAttValue v = chr->readValue();
        uint32_t t1 = TINY_CTS_MILLIS();
        ok = storeCurrentTime(v.data(), v.size(), t0, t1);
      }
    }
    release(client);
    return ok;
  }

  /**
   * @brief Set the time of the device by writing the Current Time. It works
   * if the device allows it (and the connection is paired, if it requires it).
   * @param unixMs The UTC time in ms since 1970-01-01.
   */
  bool setTimeMs(uint64_t unixMs) override {
    NimBLEClient* client = connect();
    if (client == nullptr) return false;
    bool ok = false;
    NimBLERemoteService* service = client->getService(NimBLEUUID(CTS_SERVICE_UUID));
    NimBLERemoteCharacteristic* chr =
        service != nullptr ? service->getCharacteristic(NimBLEUUID(CTS_CURRENT_TIME_UUID))
                           : nullptr;
    if (chr != nullptr && chr->canWrite()) {
      uint8_t data[TinyCTSCurrentTime::Size];
      ok = chr->writeValue(data, timeToWrite(unixMs, data), true);
    }
    release(client);
    return ok;
  }

 protected:
  NimBLEClient* connect() {
    _error.clear();
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    NimBLEScanResults results = scan->getResults(_scanMs, false);
    const NimBLEAdvertisedDevice* found = nullptr;
    for (int i = 0; i < results.getCount() && found == nullptr; i++) {
      const NimBLEAdvertisedDevice* d = results.getDevice(i);
      if (_device.empty()) {
        if (d->isAdvertisingService(NimBLEUUID(CTS_SERVICE_UUID))) found = d;
      } else if (isWanted(d->getAddress().toString(), d->getName())) {
        found = d;
      }
    }
    if (found == nullptr) {
      _error = "no device found";
      return nullptr;
    }
    NimBLEClient* client = NimBLEDevice::createClient();
    if (client == nullptr) {
      _error = "no Bluetooth client available";
      return nullptr;
    }
    if (!client->connect(found)) {
      NimBLEDevice::deleteClient(client);
      _error = "cannot connect";
      return nullptr;
    }
    _name = found->getName();
    _address = found->getAddress().toString();
    return client;
  }

  /** Disconnect and free the client: a new one is created for each use. */
  static void release(NimBLEClient* client) {
    client->disconnect();
    NimBLEDevice::deleteClient(client);
  }

  void readInfo(NimBLERemoteService* service) {
    NimBLERemoteCharacteristic* chr =
        service->getCharacteristic(NimBLEUUID(CTS_LOCAL_TIME_INFO_UUID));
    if (chr != nullptr && chr->canRead()) {
      NimBLEAttValue v = chr->readValue();
      storeLocalInfo(v.data(), v.size());
    }
    chr = service->getCharacteristic(NimBLEUUID(CTS_REFERENCE_TIME_INFO_UUID));
    if (chr != nullptr && chr->canRead()) {
      NimBLEAttValue v = chr->readValue();
      storeReferenceInfo(v.data(), v.size());
    }
  }

  uint32_t _scanMs;
};

}  // namespace tiny_cts

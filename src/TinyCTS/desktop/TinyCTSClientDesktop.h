/**
 * @file TinyCTSClientDesktop.h
 * @brief Bluetooth Current Time Service (CTS) client for the desktop: BlueZ
 * (Linux), CoreBluetooth (macOS) or WinRT (Windows). Use it through
 * TinyCTSClient.h.
 */

#pragma once
#include "TinyCTSBackend.h"
#include "../TinyCTSClientBase.h"

namespace tiny_cts {

/**
 * @class TinyCTSClient
 * @brief Reads the time from a BLE device that offers the Current Time Service
 * (a phone, a watch, a TinyCTSServer, ...). It is also a TinyCTSTimeSource.
 *
 * This is the version for the desktop. The functions block until they are
 * done. begin() scans for the device, connects, reads the characteristics and
 * disconnects: the time is then extrapolated with the clock of the computer.
 * The getters are in TinyCTSClientBase.
 *
 * @code
 * TinyCTSClient cts;
 * if (cts.begin()) printf("%llu\n", (unsigned long long)cts.getTimeMs());
 * else fprintf(stderr, "%s\n", cts.getLastError().c_str());
 * @endcode
 */
class TinyCTSClient : public TinyCTSClientBase {
 public:
  /**
   * @param device The Bluetooth address ("AA:BB:CC:DD:EE:FF") or the name of
   * the device (macOS: the identifier that CoreBluetooth has assigned instead
   * of the address). nullptr (default): the first device that advertises the
   * service.
   * @param scanMs Time to look for the device.
   */
  explicit TinyCTSClient(const char* device = nullptr, uint32_t scanMs = 5000)
      : _scanMs(scanMs) {
    if (device != nullptr) _device = device;
  }

  /** @brief The Bluetooth adapter to use (Linux only, default: hci0). */
  void setAdapter(const char* name) {
    _adapterName = name;
    _central.reset();
  }

  /** @brief Find the device and read the time. @return true on success. */
  bool begin() { return update(); }

  /**
   * @brief Read the time (and the other characteristics) again: the device is
   * connected, read and disconnected.
   * @return true if the time was read. The reason is in getLastError().
   */
  bool update() {
    if (!connect()) return false;
    bool ok = false;
    std::vector<uint8_t> data;
    std::string ignored;
    if (_central->read(CTS_LOCAL_TIME_INFO_UUID, data, ignored)) {
      storeLocalInfo(data.data(), data.size());
    }
    if (_central->read(CTS_REFERENCE_TIME_INFO_UUID, data, ignored)) {
      storeReferenceInfo(data.data(), data.size());
    }
    uint32_t t0 = TINY_CTS_MILLIS();
    if (!_central->read(CTS_CURRENT_TIME_UUID, data, _error)) {
      _error = "cannot read the time: " + _error;
    } else {
      _roundTripMs = static_cast<uint32_t>(TINY_CTS_MILLIS() - t0);
      ok = storeCurrentTime(data.data(), data.size(), t0, t0 + _roundTripMs);
      if (!ok) _error = "the device does not know the time";
    }
    _central->disconnect();
    return ok;
  }

  /**
   * @brief Set the time of the device by writing the Current Time. It works
   * if the device allows it; if it requires an encrypted connection, pair it
   * first.
   *
   * The time is the one at the moment of the call: the time that the
   * connection needs and half of the round trip time of the write are added.
   * @param unixMs The UTC time in ms since 1970-01-01.
   */
  bool setTimeMs(uint64_t unixMs) override {
    uint32_t start = TINY_CTS_MILLIS();
    if (!connect()) return false;
    // the round trip time of a read: the write needs about half of it
    std::vector<uint8_t> data;
    std::string ignored;
    uint32_t t0 = TINY_CTS_MILLIS();
    _central->read(CTS_CURRENT_TIME_UUID, data, ignored);
    _roundTripMs = static_cast<uint32_t>(TINY_CTS_MILLIS() - t0);
    uint8_t out[TinyCTSCurrentTime::Size];
    size_t len = timeToWrite(
        unixMs + static_cast<uint32_t>(TINY_CTS_MILLIS() - start) + _roundTripMs / 2, out);
    bool ok = _central->write(CTS_CURRENT_TIME_UUID, out, len, _error);
    if (!ok) _error = "rejected: " + _error;
    _central->disconnect();
    return ok;
  }

  /**
   * @brief Set the time zone of the device by writing its Local Time
   * Information (if the device allows it).
   */
  bool setTimeZone(const TinyCTSLocalTimeInfo& info) {
    if (!connect()) return false;
    uint8_t out[TinyCTSLocalTimeInfo::Size];
    info.encode(out);
    bool ok = _central->write(CTS_LOCAL_TIME_INFO_UUID, out, sizeof out, _error);
    if (!ok) _error = "rejected: " + _error;
    _central->disconnect();
    return ok;
  }

  /**
   * @brief List the devices that are in range.
   * @param timeoutMs How long to scan.
   */
  std::vector<TinyCTSScanEntry> scan(uint32_t timeoutMs) {
    std::vector<TinyCTSScanEntry> result;
    _error.clear();
    if (central()) _central->scan(timeoutMs, result, _error);
    return result;
  }

  /** @brief The reason why the last call failed. */
  const std::string& getLastError() const { return _error; }

  /** @brief The round trip time of the last read or write in ms (the connection interval). */
  uint32_t getRoundTripMs() const { return _roundTripMs; }

 protected:
  TinyCTSCentral* central() {
    if (!_central) _central = ctsCreateCentral(_adapterName);
    if (!_central) _error = "no Bluetooth stack";
    return _central.get();
  }

  bool connect() {
    _error.clear();
    if (central() == nullptr) return false;
    TinyCTSScanEntry found;
    if (!_central->connect(_device, _scanMs, found, _error)) return false;
    _name = found.name;
    _address = found.address;
    return true;
  }

  uint32_t _scanMs;
  uint32_t _roundTripMs = 0;
  std::string _adapterName = "hci0";
  std::string _error;
  std::unique_ptr<TinyCTSCentral> _central;
};

}  // namespace tiny_cts

/**
 * @file TinyCTSClientBase.h
 * @brief The logic of the Current Time Service client, independent of the
 * Bluetooth stack: decoding of the characteristics and the clock that runs
 * between two updates. The classes for NimBLE and for the Raspberry Pi Pico (W)
 * derive from it.
 */

#pragma once
#include <cctype>
#include <cstdint>
#include <ctime>
#include <string>

#include "TinyCTSTimeSource.h"
#include "TinyCTSTypes.h"

namespace tiny_cts {

/**
 * @class TinyCTSClientBase
 * @brief The data of a TinyCTSClient. Use TinyCTSClient, which adds the
 * Bluetooth part.
 *
 * The client is a local clock: the time that was read from the device is
 * extrapolated with millis(). With setUpdateInterval() the time is read
 * again automatically - when it is not known yet and then in the interval
 * (e.g. every hour), so the clock stays in sync without a permanent
 * connection:
 * @code
 * TinyCTSClient cts;
 * cts.setUpdateInterval(3600000);  // read the device every hour
 * uint64_t now = cts.getTimeMs();  // reads it the first time
 * @endcode
 */
class TinyCTSClientBase : public TinyCTSTimeSource {
 public:
  /**
   * @brief Read the time (and the other characteristics) from the device.
   * Implemented by the Bluetooth stacks.
   * @return true if the time was read.
   */
  virtual bool update() { return false; }

  /**
   * @brief Read the time from the device automatically: when it is not
   * known yet and when intervalMs has passed since the last successful read.
   * After a failed read the next attempt is made after retryMs.
   * @param intervalMs 0 (default): only begin() / update() read the time.
   * @param retryMs Time between the attempts while the device cannot be read.
   */
  void setUpdateInterval(uint32_t intervalMs, uint32_t retryMs = 60000) {
    _intervalMs = intervalMs;
    _retryMs = retryMs;
  }

  /**
   * @brief getTimeMs() reads the device when an update is due (default:
   * true). An update blocks for the scan and the connection: switch it off
   * if getTimeMs() must not block (e.g. when the client is the time source
   * of a TinyCTSServer) and call updateIfDue() in loop() instead.
   */
  void setUpdateInGetTime(bool active) { _updateInGetTime = active; }

  /**
   * @brief Read the device if an update is due (see setUpdateInterval()).
   * @return true if the time was read.
   */
  bool updateIfDue() {
    if (_intervalMs == 0 || _updating) return false;
    const uint32_t now = TINY_CTS_MILLIS();
    const bool due = _timeMs == 0 || static_cast<uint32_t>(now - _updateMillis) >= _intervalMs;
    if (!due) return false;
    if (_hasAttempt && _lastFailed && static_cast<uint32_t>(now - _attemptMillis) < _retryMs) return false;
    _updating = true;
    _hasAttempt = true;
    _attemptMillis = now;
    _lastFailed = !update();
    _updating = false;
    return !_lastFailed;
  }

  /**
   * @brief The UTC time in ms since 1970-01-01, extrapolated since the last
   * update. With setUpdateInterval() the device is read first if an update is
   * due.
   * @return 0 if the time is not known.
   */
  uint64_t getTimeMs() override {
    if (_updateInGetTime) updateIfDue();
    if (_timeMs == 0) return 0;
    uint32_t now = TINY_CTS_MILLIS();
    _timeMs += static_cast<uint32_t>(now - _lastMillis);
    _lastMillis = now;
    return _timeMs;
  }

  /** @brief The UTC time in seconds since 1970-01-01 (0 if not known). */
  uint32_t getTime() { return static_cast<uint32_t>(getTimeMs() / 1000); }

  /** @brief The UTC time as std::tm. */
  std::tm getTm() {
    time_t t = static_cast<time_t>(getTime());
    std::tm tm = {};
    ctsGmtime(t, tm);
    return tm;
  }

  /** @brief The local time as std::tm, with the offsets of the device. */
  std::tm getLocalTm() {
    time_t t = static_cast<time_t>(getTime()) + _info.utcOffsetMinutes() * 60;
    std::tm tm = {};
    ctsGmtime(t, tm);
    return tm;
  }

  /** @brief The time zone of the device (valid after update() if it has it). */
  const TinyCTSLocalTimeInfo& getLocalTimeInfo() const { return _info; }

  /** @brief Where the time of the device comes from. */
  const TinyCTSReferenceTimeInfo& getReferenceTimeInfo() const { return _ref; }

  /** @brief The Current Time as it was sent by the device. */
  const TinyCTSCurrentTime& getCurrentTime() const { return _current; }

  /**
   * @brief The time between the read request of the Current Time and its
   * answer in ms (the time is valid in the middle). 0 before the first read.
   */
  uint32_t getRoundTripMs() const { return _roundTripMs; }

  /**
   * @brief The value of millis() when the Current Time was read: getTimeMs()
   * at that moment was the time of the device.
   */
  uint32_t getUpdateMillis() const { return _updateMillis; }

  /** @brief Why the last update() or setTimeMs() failed ("" if it worked). */
  const std::string& getLastError() const { return _error; }

  /** @brief The name of the device that was used (empty before begin()). */
  const std::string& getDeviceName() const { return _name; }

  /** @brief The address of the device that was used. */
  const std::string& getDeviceAddress() const { return _address; }

  /**
   * @brief Tell the client the time zone of the device, if it does not have the
   * Local Time Information. Used for the local time and, with
   * setTimeIsLocal(), when the time is set.
   */
  void setLocalTimeInfo(const TinyCTSLocalTimeInfo& info) { _info = info; }

  /** @brief The Current Time of the device is the local time, not UTC. */
  void setTimeIsLocal(bool local) { _timeIsLocal = local; }

 protected:
  /**
   * The Current Time was read.
   * @param startMs millis() before the request, endMs after the response: the
   * time is valid in the middle.
   * @return false if the data is not a valid time.
   */
  bool storeCurrentTime(const uint8_t* data, size_t len, uint32_t startMs, uint32_t endMs) {
    TinyCTSCurrentTime t;
    uint64_t ms;
    if (!TinyCTSCurrentTime::decode(data, len, t) || !t.toUnixMs(ms)) {
      _error = "the device does not know the time";
      return false;
    }
    if (_timeIsLocal) ms -= static_cast<int64_t>(_info.utcOffsetMinutes()) * 60000;
    _current = t;
    _roundTripMs = static_cast<uint32_t>(endMs - startMs);
    _timeMs = ms + _roundTripMs / 2;
    _lastMillis = endMs;
    _updateMillis = endMs;
    return true;
  }

  void storeLocalInfo(const uint8_t* data, size_t len) {
    TinyCTSLocalTimeInfo::decode(data, len, _info);
  }

  void storeReferenceInfo(const uint8_t* data, size_t len) {
    TinyCTSReferenceTimeInfo::decode(data, len, _ref);
  }

  /** The 10 bytes to write to set the time of the device. */
  size_t timeToWrite(uint64_t unixMs, uint8_t* out) const {
    if (_timeIsLocal) unixMs += static_cast<int64_t>(_info.utcOffsetMinutes()) * 60000;
    return TinyCTSCurrentTime::fromUnixMs(unixMs, TinyCTSCurrentTime::ManualUpdate).encode(out);
  }

  /** true if the device is the one that was asked for (address or name). */
  bool isWanted(const std::string& address, const std::string& name) const {
    return equalsIgnoreCase(address, _device) || name == _device;
  }

  static bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
      if (tolower(a[i]) != tolower(b[i])) return false;
    }
    return true;
  }

  std::string _device, _name, _address;
  std::string _error;
  uint32_t _intervalMs = 0;
  uint32_t _retryMs = 60000;
  uint32_t _attemptMillis = 0;
  bool _hasAttempt = false;
  bool _lastFailed = false;
  bool _updating = false;
  bool _updateInGetTime = true;
  uint32_t _roundTripMs = 0;
  uint32_t _updateMillis = 0;
  uint64_t _timeMs = 0;
  uint32_t _lastMillis = 0;
  bool _timeIsLocal = false;
  TinyCTSCurrentTime _current;
  TinyCTSLocalTimeInfo _info;
  TinyCTSReferenceTimeInfo _ref;
};

}  // namespace tiny_cts

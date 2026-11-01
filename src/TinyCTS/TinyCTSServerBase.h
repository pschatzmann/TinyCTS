/**
 * @file TinyCTSServerBase.h
 * @brief The logic of the Current Time Service server, independent of the
 * Bluetooth stack: the values of the characteristics and the handling of the
 * writes. The classes for NimBLE and for the Raspberry Pi Pico (W) derive from
 * it.
 */

#pragma once
#include <cstdint>

#include "TinyCTSTimeSource.h"
#include "TinyCTSTypes.h"

namespace tiny_cts {

/**
 * @class TinyCTSServerBase
 * @brief Settings and values of a TinyCTSServer. Use TinyCTSServer, which adds
 * the Bluetooth part.
 */
class TinyCTSServerBase {
 public:
  virtual ~TinyCTSServerBase() = default;

  /**
   * @brief Notify the subscribed clients regularly in process(). 0 (default):
   * only when the time was set or the time zone changed. The specification
   * asks for a notification when the time changes by a larger step: use a long
   * interval, e.g. 60000 ms.
   */
  void setNotifyInterval(uint32_t ms) { _notifyMs = ms; }

  /**
   * @brief The time zone that is offered in the Local Time Information.
   * @param utcOffsetMinutes The standard time zone, e.g. 60 for CET.
   * @param dstMinutes The summer time offset now: 0, 30, 60 or 120.
   */
  void setTimeZone(int utcOffsetMinutes, int dstMinutes = 0) {
    _info = TinyCTSLocalTimeInfo::fromMinutes(utcOffsetMinutes, dstMinutes);
    onTimeZoneChanged();
  }

  /** @brief Where the time comes from and how exact it is (125 ms steps). */
  void setReferenceInfo(uint8_t source, uint8_t accuracy = 255) {
    _ref.source = source;
    _ref.accuracy = accuracy;
    onReferenceChanged();
  }

  /**
   * @brief The Current Time contains the local time (UTC + the offsets of the
   * Local Time Information) instead of UTC (default). Some clients expect the
   * local time. Call it before begin().
   */
  void setTimeIsLocal(bool local) { _timeIsLocal = local; }

  /**
   * @brief Allow the clients to set the time of the time source by writing the
   * Current Time (and the time zone by writing the Local Time Information).
   * Needs a time source that implements setTimeMs(). Call it before begin().
   * @param requireEncryption The clients must be paired (encrypted
   * connection). Use false only in a trusted environment: everybody who is
   * near the device can change the time!
   */
  void enableTimeSetting(bool requireEncryption = true) {
    _settable = true;
    _encrypted = requireEncryption;
  }

  /** @brief The number of times that a client has set the time. */
  uint32_t getTimeSetCount() const { return _setCount; }

 protected:
  /** The Local Time Information has changed: update the characteristic. */
  virtual void onTimeZoneChanged() {}
  /** The Reference Time Information has changed. */
  virtual void onReferenceChanged() {}

  /**
   * The value of the Current Time: 10 bytes. While the time is not known,
   * the date is unknown (all 0).
   */
  size_t currentTimeValue(uint8_t reason, uint8_t* out) {
    uint64_t ms = _source != nullptr ? _source->getTimeMs() : 0;
    TinyCTSCurrentTime t;
    if (ms != 0) {
      if (_timeIsLocal) ms += static_cast<int64_t>(_info.utcOffsetMinutes()) * 60000;
      t = TinyCTSCurrentTime::fromUnixMs(ms, reason);
    }
    return t.encode(out);
  }

  size_t localTimeValue(uint8_t* out) const { return _info.encode(out); }

  size_t referenceTimeValue(uint8_t* out) const { return _ref.encode(out); }

  /**
   * A client wrote the Current Time.
   * @return true if the time source was set.
   */
  bool setTimeFromWrite(const uint8_t* data, size_t len) {
    TinyCTSCurrentTime t;
    uint64_t ms;
    if (_source == nullptr || !_settable) return false;
    if (!TinyCTSCurrentTime::decode(data, len, t) || !t.toUnixMs(ms)) return false;
    if (_timeIsLocal) ms -= static_cast<int64_t>(_info.utcOffsetMinutes()) * 60000;
    if (!_source->setTimeMs(ms)) return false;
    _setCount++;
    return true;
  }

  /**
   * A client wrote the Local Time Information.
   * @return true if the time zone was changed.
   */
  bool setTimeZoneFromWrite(const uint8_t* data, size_t len) {
    TinyCTSLocalTimeInfo info;
    if (!_settable || !TinyCTSLocalTimeInfo::decode(data, len, info)) return false;
    _info = info;
    return true;
  }

  /** true if the notification interval is over (and starts the next one). */
  bool notifyDue() {
    if (_source == nullptr || _notifyMs == 0) return false;
    uint32_t now = TINY_CTS_MILLIS();
    if (static_cast<uint32_t>(now - _lastNotify) < _notifyMs) return false;
    _lastNotify = now;
    return true;
  }

  TinyCTSTimeSource* _source = nullptr;
  TinyCTSLocalTimeInfo _info;
  TinyCTSReferenceTimeInfo _ref;
  uint32_t _notifyMs = 0;
  uint32_t _lastNotify = 0;
  uint32_t _setCount = 0;
  bool _timeIsLocal = false;
  bool _settable = false;
  bool _encrypted = true;
};

}  // namespace tiny_cts

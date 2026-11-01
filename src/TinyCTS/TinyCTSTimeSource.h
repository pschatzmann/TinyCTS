/**
 * @file TinyCTSTimeSource.h
 * @brief Time sources of the TinyCTSServer: the interface, a software clock, the
 * system clock and an adapter for any object with getTimeMs() (e.g. the
 * TinyNTPClient of the TinyNTP library).
 */

#pragma once
#include <cstdint>

// (the Renesas core of the Uno R4 has the header, but no usable timeval)
// the system clock: gettimeofday() on the boards that have it (the Renesas core
// of the Uno R4 has the header, but no usable timeval), std::chrono on the desktop
#if !defined(ARDUINO)
#define TINY_CTS_HAS_SYSTEM_TIME 1
#include <chrono>
#if !defined(_WIN32)
#include <sys/time.h>
#define TINY_CTS_HAS_SETTIMEOFDAY 1
#endif
#elif defined(__has_include) && !defined(ARDUINO_ARCH_RENESAS) && \
    !defined(ARDUINO_ARCH_RENESAS_UNO) && !defined(ARDUINO_ARCH_RENESAS_PORTENTA)
#if __has_include(<sys/time.h>)
#include <sys/time.h>
#define TINY_CTS_HAS_SYSTEM_TIME 1
#define TINY_CTS_HAS_SETTIMEOFDAY 1
#endif
#endif

// millis(): from Arduino.h, or the steady clock where there is no Arduino API
// (the tests and the command line tool); override with your own macro if needed
#ifndef TINY_CTS_MILLIS
#if defined(ARDUINO)
#define TINY_CTS_MILLIS() (::millis())
#else
#include <chrono>
#define TINY_CTS_MILLIS()                                               \
  (static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>( \
                             std::chrono::steady_clock::now().time_since_epoch()) \
                             .count()))
#endif
#endif

namespace tiny_cts {

/**
 * @class TinyCTSTimeSource
 * @brief Abstract base class of the time sources: an object that knows the
 * current UTC time.
 */
class TinyCTSTimeSource {
 public:
  virtual ~TinyCTSTimeSource() = default;

  /**
   * @brief The current UTC time in milliseconds since 1970-01-01. It must not
   * block.
   * @return The time or 0 as long as it is not known.
   */
  virtual uint64_t getTimeMs() = 0;

  /**
   * @brief Set the current UTC time, e.g. when a client writes the Current
   * Time characteristic. The default implementation does not support this.
   * @return true if the time was set.
   */
  virtual bool setTimeMs(uint64_t unixMs) {
    (void)unixMs;
    return false;
  }
};

/**
 * @class TinyCTSClockSource
 * @brief A software clock: you set the UTC time (from an RTC, a GPS, ...) and it
 * keeps counting with millis(). The time is 0 (not known) until it is set.
 * Read or set it at least once every 49 days (overflow of millis()).
 */
class TinyCTSClockSource : public TinyCTSTimeSource {
 public:
  void setTime(uint32_t unixSeconds, uint16_t milliseconds = 0) {
    setTimeMs(unixSeconds * 1000ULL + milliseconds);
  }

  bool setTimeMs(uint64_t unixMs) override {
    _timeMs = unixMs;
    _lastMillis = TINY_CTS_MILLIS();
    return true;
  }

  void clear() { _timeMs = 0; }
  bool isSet() const { return _timeMs != 0; }

  uint64_t getTimeMs() override {
    if (_timeMs == 0) return 0;
    uint32_t now = TINY_CTS_MILLIS();
    _timeMs += static_cast<uint32_t>(now - _lastMillis);
    _lastMillis = now;
    return _timeMs;
  }

 protected:
  uint64_t _timeMs = 0;
  uint32_t _lastMillis = 0;
};

#ifdef TINY_CTS_HAS_SYSTEM_TIME
/**
 * @class TinyCTSSystemSource
 * @brief The UTC time of the system clock (gettimeofday()). Not available on
 * the boards without gettimeofday() (AVR, Uno R4). Times before
 * minValidSeconds (default 2023-11-14) are treated as "clock not set".
 */
class TinyCTSSystemSource : public TinyCTSTimeSource {
 public:
  explicit TinyCTSSystemSource(uint32_t minValidSeconds = 1700000000UL)
      : _minValid(minValidSeconds) {}

  uint64_t getTimeMs() override {
#if defined(ARDUINO)
    timeval tv;
    if (gettimeofday(&tv, nullptr) != 0) return 0;
    uint64_t sec = static_cast<uint64_t>(tv.tv_sec);
    uint64_t ms = sec * 1000ULL + tv.tv_usec / 1000;
#else
    uint64_t ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    uint64_t sec = ms / 1000;
#endif
    return sec < _minValid ? 0 : ms;
  }

  /** Sets the system clock: needs the rights (root) on the desktop. Not on Windows. */
  bool setTimeMs(uint64_t unixMs) override {
#if defined(TINY_CTS_HAS_SETTIMEOFDAY)
    timeval tv;
    tv.tv_sec = static_cast<time_t>(unixMs / 1000);
    tv.tv_usec = static_cast<suseconds_t>(unixMs % 1000 * 1000);
    return settimeofday(&tv, nullptr) == 0;
#else
    (void)unixMs;
    return false;
#endif
  }

 protected:
  uint32_t _minValid;
};
#endif

/**
 * @class TinyCTSSourceAdapter
 * @brief Uses any object with a getTimeMs() method as time source, e.g. the
 * TinyNTPClient of the TinyNTP library:
 * @code
 * TinyNTPClient<WiFiUDP> ntp;
 * TinyCTSSourceAdapter<TinyNTPClient<WiFiUDP>> source(ntp);
 * @endcode
 */
template <typename T>
class TinyCTSSourceAdapter : public TinyCTSTimeSource {
 public:
  explicit TinyCTSSourceAdapter(T& source) : _source(source) {}
  uint64_t getTimeMs() override { return _source.getTimeMs(); }

 protected:
  T& _source;
};

}  // namespace tiny_cts

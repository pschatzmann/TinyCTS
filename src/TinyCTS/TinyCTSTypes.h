/**
 * @file TinyCTSTypes.h
 * @brief The data of the Bluetooth Current Time Service (CTS, 0x1805): UUIDs
 * and the encoding of the characteristics Current Time (0x2A2B), Local Time
 * Information (0x2A0F) and Reference Time Information (0x2A14). No Bluetooth
 * stack is needed: this file is used by the library and the command line tool.
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>

namespace tiny_cts {

/// 16 bit UUID of the Current Time Service
constexpr uint16_t CTS_SERVICE_UUID = 0x1805;
/// 16 bit UUID of the Current Time characteristic (read, notify, write)
constexpr uint16_t CTS_CURRENT_TIME_UUID = 0x2A2B;
/// 16 bit UUID of the Local Time Information characteristic (read, write)
constexpr uint16_t CTS_LOCAL_TIME_INFO_UUID = 0x2A0F;
/// 16 bit UUID of the Reference Time Information characteristic (read)
constexpr uint16_t CTS_REFERENCE_TIME_INFO_UUID = 0x2A14;

/// Number of days since 1970-01-01 of a date in the proleptic Gregorian calendar
inline int32_t ctsDaysFromCivil(int32_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

/// The date of a number of days since 1970-01-01
inline void ctsCivilFromDays(int32_t z, int32_t& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = static_cast<int32_t>(yoe) + era * 400 + (m <= 2);
}

/// gmtime_r() where it exists, gmtime_s() on Windows
inline void ctsGmtime(time_t t, std::tm& tm) {
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
}

/**
 * @class TinyCTSCurrentTime
 * @brief The Current Time characteristic (0x2A2B): 10 bytes with the date and
 * time (Exact Time 256), the day of week, fractions of a second and the reason
 * for the last adjustment.
 *
 * Use fromUnixMs() and toUnixMs() to convert from and to the time in
 * milliseconds since 1970-01-01. The conversion does not use the time zone: it
 * is up to the user of the library if the date and time are UTC (default) or
 * the local time (see TinyCTSServer::setTimeIsLocal()).
 */
struct TinyCTSCurrentTime {
  /// Size of the encoded characteristic
  static constexpr size_t Size = 10;
  /// Bits of adjustReason
  enum AdjustReason : uint8_t {
    ManualUpdate = 1,
    ExternalReference = 2,
    TimeZoneChange = 4,
    DstChange = 8
  };

  uint16_t year = 0;   ///< 1582-9999, 0 = unknown
  uint8_t month = 0;   ///< 1-12, 0 = unknown
  uint8_t day = 0;     ///< 1-31, 0 = unknown
  uint8_t hours = 0;   ///< 0-23
  uint8_t minutes = 0; ///< 0-59
  uint8_t seconds = 0; ///< 0-59
  uint8_t dayOfWeek = 0;     ///< 1 = Monday ... 7 = Sunday, 0 = unknown
  uint8_t fractions256 = 0;  ///< 1/256 seconds
  uint8_t adjustReason = 0;  ///< bits of AdjustReason

  /** @brief The characteristic for a time in ms since 1970-01-01. */
  static TinyCTSCurrentTime fromUnixMs(uint64_t unixMs, uint8_t reason = 0) {
    TinyCTSCurrentTime t;
    const uint32_t seconds = static_cast<uint32_t>(unixMs / 1000);
    const int32_t days = static_cast<int32_t>(seconds / 86400);
    const uint32_t rest = seconds % 86400;
    int32_t y;
    unsigned m, d;
    ctsCivilFromDays(days, y, m, d);
    t.year = static_cast<uint16_t>(y);
    t.month = static_cast<uint8_t>(m);
    t.day = static_cast<uint8_t>(d);
    t.hours = static_cast<uint8_t>(rest / 3600);
    t.minutes = static_cast<uint8_t>(rest % 3600 / 60);
    t.seconds = static_cast<uint8_t>(rest % 60);
    t.dayOfWeek = static_cast<uint8_t>((days + 3) % 7 + 1);  // 1970-01-01: Thu
    t.fractions256 = static_cast<uint8_t>(unixMs % 1000 * 256 / 1000);
    t.adjustReason = reason;
    return t;
  }

  /**
   * @brief The time in ms since 1970-01-01.
   * @param unixMs Result
   * @return false if the date is unknown or invalid (before 1970 included).
   */
  bool toUnixMs(uint64_t& unixMs) const {
    if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 ||
        day > 31 || hours > 23 || minutes > 59 || seconds > 59) {
      return false;
    }
    const int64_t days = ctsDaysFromCivil(year, month, day);
    // reject days that a month does not have (31 February)
    int32_t y;
    unsigned m, d;
    ctsCivilFromDays(static_cast<int32_t>(days), y, m, d);
    if (y != year || m != month || d != day) return false;
    unixMs = (static_cast<uint64_t>(days) * 86400 + hours * 3600UL +
              minutes * 60UL + seconds) *
                 1000 +
             fractions256 * 1000UL / 256;
    return true;
  }

  /** @brief Write the 10 bytes. @return 10 */
  size_t encode(uint8_t* out) const {
    out[0] = year & 0xFF;
    out[1] = year >> 8;
    out[2] = month;
    out[3] = day;
    out[4] = hours;
    out[5] = minutes;
    out[6] = seconds;
    out[7] = dayOfWeek;
    out[8] = fractions256;
    out[9] = adjustReason;
    return Size;
  }

  /**
   * @brief Read the characteristic. Some devices send the 9 bytes of the
   * Exact Time 256 without the adjust reason: this is accepted.
   * @return false if the data is too short.
   */
  static bool decode(const uint8_t* data, size_t len, TinyCTSCurrentTime& t) {
    if (data == nullptr || len < Size - 1) return false;
    t.year = static_cast<uint16_t>(data[0] | data[1] << 8);
    t.month = data[2];
    t.day = data[3];
    t.hours = data[4];
    t.minutes = data[5];
    t.seconds = data[6];
    t.dayOfWeek = data[7];
    t.fractions256 = data[8];
    t.adjustReason = len >= Size ? data[9] : 0;
    return true;
  }
};

/**
 * @class TinyCTSLocalTimeInfo
 * @brief The Local Time Information characteristic (0x2A0F): the time zone and
 * the daylight saving time (DST) offset. Local time = UTC + time zone + DST.
 */
struct TinyCTSLocalTimeInfo {
  static constexpr size_t Size = 2;
  static constexpr int8_t UnknownTimeZone = -128;
  static constexpr uint8_t UnknownDst = 255;

  int8_t timeZone = UnknownTimeZone;  ///< offset to UTC in 15 minutes, -48..56
  uint8_t dstOffset = UnknownDst;     ///< 0 = standard, 2 = +0.5h, 4 = +1h, 8 = +2h

  /**
   * @brief From the offsets in minutes.
   * @param utcOffsetMinutes The standard time zone, e.g. 60 for CET
   * @param dstMinutes The summer time offset in minutes: 0, 30, 60 or 120
   */
  static TinyCTSLocalTimeInfo fromMinutes(int utcOffsetMinutes, int dstMinutes = 0) {
    TinyCTSLocalTimeInfo i;
    i.timeZone = static_cast<int8_t>(utcOffsetMinutes / 15);
    i.dstOffset = dstMinutes <= 0 ? 0
                  : dstMinutes <= 30 ? 2
                  : dstMinutes <= 60 ? 4
                                     : 8;
    return i;
  }

  /** @brief false if the time zone is not known. */
  bool isKnown() const { return timeZone != UnknownTimeZone; }

  /** @brief The time zone in minutes (0 if not known). */
  int zoneMinutes() const { return isKnown() ? timeZone * 15 : 0; }

  /** @brief The summer time offset in minutes (0 if not known). */
  int dstMinutes() const { return dstOffset == UnknownDst ? 0 : dstOffset * 15; }

  /** @brief Offset of the local time to UTC in minutes. */
  int utcOffsetMinutes() const { return zoneMinutes() + dstMinutes(); }

  size_t encode(uint8_t* out) const {
    out[0] = static_cast<uint8_t>(timeZone);
    out[1] = dstOffset;
    return Size;
  }

  static bool decode(const uint8_t* data, size_t len, TinyCTSLocalTimeInfo& i) {
    if (data == nullptr || len < Size) return false;
    i.timeZone = static_cast<int8_t>(data[0]);
    i.dstOffset = data[1];
    return true;
  }
};

/**
 * @class TinyCTSReferenceTimeInfo
 * @brief The Reference Time Information characteristic (0x2A14): where the
 * time of the server comes from, how exact it is and how old.
 */
struct TinyCTSReferenceTimeInfo {
  static constexpr size_t Size = 4;
  /// Values of source
  enum Source : uint8_t {
    Unknown = 0,
    NetworkTimeProtocol = 1,
    GPS = 2,
    RadioTimeSignal = 3,
    Manual = 4,
    AtomicClock = 5,
    CellularNetwork = 6
  };

  uint8_t source = Unknown;
  uint8_t accuracy = 255;       ///< in 125 ms (0-253), 254 = more than 31.5 s, 255 = unknown
  uint8_t daysSinceUpdate = 255;  ///< 0-254, 255 = 255 days or more
  uint8_t hoursSinceUpdate = 255; ///< 0-23, 255 if the days are 255 or more

  /** @brief The accuracy in ms (the value is in 125 ms): -1 if not known. */
  int accuracyMs() const { return accuracy >= 254 ? -1 : accuracy * 125; }

  size_t encode(uint8_t* out) const {
    out[0] = source;
    out[1] = accuracy;
    out[2] = daysSinceUpdate;
    out[3] = hoursSinceUpdate;
    return Size;
  }

  static bool decode(const uint8_t* data, size_t len, TinyCTSReferenceTimeInfo& i) {
    if (data == nullptr || len < Size) return false;
    i.source = data[0];
    i.accuracy = data[1];
    i.daysSinceUpdate = data[2];
    i.hoursSinceUpdate = data[3];
    return true;
  }

  /** @brief The name of a source. */
  static const char* sourceName(uint8_t source) {
    switch (source) {
      case NetworkTimeProtocol: return "NTP";
      case GPS: return "GPS";
      case RadioTimeSignal: return "radio time signal";
      case Manual: return "manual";
      case AtomicClock: return "atomic clock";
      case CellularNetwork: return "cellular network";
      default: return "unknown";
    }
  }
};

}  // namespace tiny_cts

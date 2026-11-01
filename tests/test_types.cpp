// Tests of the encoding of the CTS characteristics
#include <ctime>

#include "test.h"
#include "TinyCTS/TinyCTSTypes.h"

using namespace tiny_cts;

TEST(current_time_encoding_known_vector) {
  // 2026-01-01 00:00:00.500 UTC was a Thursday
  TinyCTSCurrentTime t = TinyCTSCurrentTime::fromUnixMs(1767225600500ULL, 1);
  uint8_t d[10];
  CHECK_EQ(t.encode(d), (size_t)10);
  const uint8_t expected[10] = {0xEA, 0x07, 1, 1, 0, 0, 0, 4, 128, 1};
  for (int i = 0; i < 10; i++) CHECK_EQ(d[i], expected[i]);
}

TEST(current_time_roundtrip) {
  const uint64_t samples[] = {0ULL,           1ULL,           86399999ULL,
                              951782400000ULL /*2000-02-29*/, 1709164800000ULL,
                              4102444799999ULL /*2099-12-31*/, 253402300799000ULL};
  for (uint64_t ms : samples) {
    if (ms > 4294967295ULL * 1000) continue;  // 32 bit seconds
    TinyCTSCurrentTime t = TinyCTSCurrentTime::fromUnixMs(ms);
    uint8_t d[10];
    t.encode(d);
    TinyCTSCurrentTime r;
    CHECK(TinyCTSCurrentTime::decode(d, 10, r));
    uint64_t back = 0;
    if (ms < 1970) continue;
    CHECK(r.toUnixMs(back));
    // 1/256 s resolution: at most 4 ms lost
    CHECK(back <= ms && ms - back < 4);
  }
}

TEST(current_time_matches_gmtime_for_every_day) {
  // compare with the C library for 1970-2100, one sample per day
  for (uint64_t s = 0; s < 4102444800ULL; s += 86400 + 3601) {
    TinyCTSCurrentTime t = TinyCTSCurrentTime::fromUnixMs(s * 1000);
    time_t tt = static_cast<time_t>(s);
    std::tm tm;
    gmtime_r(&tt, &tm);
    CHECK_EQ(t.year, tm.tm_year + 1900);
    CHECK_EQ(t.month, tm.tm_mon + 1);
    CHECK_EQ(t.day, tm.tm_mday);
    CHECK_EQ(t.hours, tm.tm_hour);
    CHECK_EQ(t.minutes, tm.tm_min);
    CHECK_EQ(t.seconds, tm.tm_sec);
    CHECK_EQ(t.dayOfWeek, tm.tm_wday == 0 ? 7 : tm.tm_wday);
  }
}

TEST(current_time_invalid) {
  uint64_t ms;
  TinyCTSCurrentTime t;  // all unknown
  CHECK(!t.toUnixMs(ms));
  t = TinyCTSCurrentTime::fromUnixMs(1767225600000ULL);
  t.month = 13;
  CHECK(!t.toUnixMs(ms));
  t = TinyCTSCurrentTime::fromUnixMs(1767225600000ULL);
  t.month = 2;
  t.day = 31;  // 31 February
  CHECK(!t.toUnixMs(ms));
  t = TinyCTSCurrentTime::fromUnixMs(1767225600000ULL);
  t.hours = 24;
  CHECK(!t.toUnixMs(ms));
  t.hours = 0;
  t.year = 1969;
  CHECK(!t.toUnixMs(ms));
}

TEST(current_time_decode_lengths) {
  TinyCTSCurrentTime t;
  uint8_t d[10] = {0xEA, 0x07, 1, 1, 0, 0, 0, 4, 0, 8};
  CHECK(!TinyCTSCurrentTime::decode(d, 8, t));
  CHECK(!TinyCTSCurrentTime::decode(nullptr, 10, t));
  CHECK(TinyCTSCurrentTime::decode(d, 9, t));  // Exact Time 256 only
  CHECK_EQ(t.adjustReason, 0);
  CHECK(TinyCTSCurrentTime::decode(d, 10, t));
  CHECK_EQ(t.adjustReason, 8);
}

TEST(local_time_info) {
  TinyCTSLocalTimeInfo i = TinyCTSLocalTimeInfo::fromMinutes(60, 60);
  CHECK_EQ(i.timeZone, 4);
  CHECK_EQ(i.dstOffset, 4);
  CHECK_EQ(i.utcOffsetMinutes(), 120);
  i = TinyCTSLocalTimeInfo::fromMinutes(-300, 0);  // New York, standard time
  CHECK_EQ(i.utcOffsetMinutes(), -300);
  uint8_t d[2];
  i.encode(d);
  CHECK_EQ(d[0], 0xEC);  // -20
  TinyCTSLocalTimeInfo r;
  CHECK(TinyCTSLocalTimeInfo::decode(d, 2, r));
  CHECK_EQ(r.timeZone, -20);
  CHECK(!TinyCTSLocalTimeInfo::decode(d, 1, r));
  i = TinyCTSLocalTimeInfo::fromMinutes(330, 30);  // India (+5:30), half hour
  CHECK_EQ(i.timeZone, 22);
  CHECK_EQ(i.dstOffset, 2);
  TinyCTSLocalTimeInfo unknown;
  CHECK(!unknown.isKnown());
  CHECK_EQ(unknown.utcOffsetMinutes(), 0);
}

TEST(reference_time_info) {
  TinyCTSReferenceTimeInfo i;
  CHECK_EQ(i.accuracyMs(), -1);
  i.source = TinyCTSReferenceTimeInfo::GPS;
  i.accuracy = 8;
  CHECK_EQ(i.accuracyMs(), 1000);
  uint8_t d[4];
  CHECK_EQ(i.encode(d), (size_t)4);
  TinyCTSReferenceTimeInfo r;
  CHECK(TinyCTSReferenceTimeInfo::decode(d, 4, r));
  CHECK_EQ(r.source, 2);
  CHECK_STR(TinyCTSReferenceTimeInfo::sourceName(r.source), "GPS");
  CHECK(!TinyCTSReferenceTimeInfo::decode(d, 3, r));
}

TEST_MAIN("types")

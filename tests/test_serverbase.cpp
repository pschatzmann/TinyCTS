// Tests of the logic of the server (without Bluetooth)
#include <cstdint>

static uint32_t fakeMillis = 0;
#define TINY_CTS_MILLIS() (fakeMillis)

#include "test.h"
#include "TinyCTS/TinyCTSServerBase.h"

using namespace tiny_cts;

struct Source : TinyCTSTimeSource {
  uint64_t ms = 0;
  bool allow = true;
  uint64_t getTimeMs() override { return ms; }
  bool setTimeMs(uint64_t v) override {
    if (!allow) return false;
    ms = v;
    return true;
  }
};

struct Server : TinyCTSServerBase {
  using TinyCTSServerBase::_source;
  int zoneChanged = 0, refChanged = 0;
  void attach(TinyCTSTimeSource& s) { _source = &s; }
  size_t current(uint8_t r, uint8_t* o) { return currentTimeValue(r, o); }
  size_t local(uint8_t* o) { return localTimeValue(o); }
  size_t reference(uint8_t* o) { return referenceTimeValue(o); }
  bool write(const uint8_t* d, size_t n) { return setTimeFromWrite(d, n); }
  bool writeZone(const uint8_t* d, size_t n) { return setTimeZoneFromWrite(d, n); }
  bool due() { return notifyDue(); }
  void onTimeZoneChanged() override { zoneChanged++; }
  void onReferenceChanged() override { refChanged++; }
};

TEST(unknown_time) {
  Server s;
  Source src;
  s.attach(src);
  uint8_t d[10];
  CHECK_EQ(s.current(0, d), (size_t)10);
  for (int i = 0; i < 10; i++) CHECK_EQ(d[i], 0);
}

TEST(current_time_utc_and_local) {
  Server s;
  Source src;
  src.ms = 1767225600000ULL;  // 2026-01-01 00:00:00 UTC
  s.attach(src);
  s.setTimeZone(60, 60);
  CHECK_EQ(s.zoneChanged, 1);
  uint8_t d[10];
  s.current(0, d);
  CHECK_EQ(d[4], 0);  // UTC: hour 0
  s.setTimeIsLocal(true);
  s.current(0, d);
  CHECK_EQ(d[4], 2);  // UTC+2
  uint8_t z[2];
  CHECK_EQ(s.local(z), (size_t)2);
  CHECK_EQ(z[0], 4);
  CHECK_EQ(z[1], 4);
}

TEST(reference_info) {
  Server s;
  s.setReferenceInfo(TinyCTSReferenceTimeInfo::GPS, 8);
  CHECK_EQ(s.refChanged, 1);
  uint8_t d[4];
  s.reference(d);
  CHECK_EQ(d[0], 2);
  CHECK_EQ(d[1], 8);
}

TEST(write_needs_enableTimeSetting) {
  Server s;
  Source src;
  s.attach(src);
  uint8_t d[10];
  TinyCTSCurrentTime::fromUnixMs(1767225600000ULL).encode(d);
  CHECK(!s.write(d, 10));
  s.enableTimeSetting();
  CHECK(s.write(d, 10));
  CHECK_EQ(src.ms, (uint64_t)1767225600000ULL);
  CHECK_EQ(s.getTimeSetCount(), (uint32_t)1);
}

TEST(write_refused) {
  Server s;
  Source src;
  s.attach(src);
  s.enableTimeSetting();
  uint8_t d[10] = {0};  // unknown date
  CHECK(!s.write(d, 10));
  CHECK(!s.write(d, 5));  // too short
  TinyCTSCurrentTime::fromUnixMs(1767225600000ULL).encode(d);
  src.allow = false;  // a source that cannot be set
  CHECK(!s.write(d, 10));
  CHECK_EQ(s.getTimeSetCount(), (uint32_t)0);
}

TEST(write_local_time_is_converted_to_utc) {
  Server s;
  Source src;
  s.attach(src);
  s.enableTimeSetting();
  s.setTimeZone(60, 0);
  s.setTimeIsLocal(true);
  uint8_t d[10];
  TinyCTSCurrentTime::fromUnixMs(1767229200000ULL).encode(d);  // 01:00 local
  CHECK(s.write(d, 10));
  CHECK_EQ(src.ms, (uint64_t)1767225600000ULL);  // 00:00 UTC
}

TEST(write_time_zone) {
  Server s;
  uint8_t z[2] = {static_cast<uint8_t>(-20), 0};
  CHECK(!s.writeZone(z, 2));
  s.enableTimeSetting();
  CHECK(s.writeZone(z, 2));
  uint8_t out[2];
  s.local(out);
  CHECK_EQ(out[0], z[0]);
}

TEST(notify_interval) {
  Server s;
  Source src;
  s.attach(src);
  fakeMillis = 1000;
  CHECK(!s.due());  // off
  s.setNotifyInterval(500);
  CHECK(s.due());
  fakeMillis = 1200;
  CHECK(!s.due());
  fakeMillis = 1500;
  CHECK(s.due());
}

TEST_MAIN("serverbase")

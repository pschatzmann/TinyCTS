// Tests of the logic of the client (without Bluetooth)
#include <cstdint>

static uint32_t fakeMillis = 0;
#define TINY_CTS_MILLIS() (fakeMillis)

#include "test.h"
#include "TinyCTS/TinyCTSClientBase.h"

using namespace tiny_cts;

struct Client : TinyCTSClientBase {
  void wanted(const char* d) { _device = d; }
  bool store(const uint8_t* d, size_t n, uint32_t a, uint32_t b) {
    return storeCurrentTime(d, n, a, b);
  }
  void local(const uint8_t* d, size_t n) { storeLocalInfo(d, n); }
  size_t write(uint64_t ms, uint8_t* o) { return timeToWrite(ms, o); }
  bool is(const char* a, const char* n) { return isWanted(a, n); }
};

TEST(time_is_extrapolated) {
  Client c;
  CHECK_EQ(c.getTimeMs(), (uint64_t)0);
  uint8_t d[10];
  TinyCTSCurrentTime::fromUnixMs(1767225600000ULL).encode(d);
  fakeMillis = 5000;
  CHECK(c.store(d, 10, 5000, 5040));  // 40 ms round trip: +20 ms
  fakeMillis = 7040;
  CHECK_NEAR(c.getTimeMs(), 1767225600000ULL + 20 + 2000, 8);  // 1/256 s resolution
  CHECK_EQ(c.getTm().tm_year, 126);
}

TEST(unknown_time_is_refused) {
  Client c;
  uint8_t d[10] = {0};
  CHECK(!c.store(d, 10, 0, 0));
  CHECK_EQ(c.getTimeMs(), (uint64_t)0);
}

TEST(local_time_and_zone) {
  fakeMillis = 0;
  Client c;
  uint8_t z[2] = {4, 4};  // UTC+2
  c.local(z, 2);
  uint8_t d[10];
  TinyCTSCurrentTime::fromUnixMs(1767225600000ULL).encode(d);
  c.store(d, 10, 0, 0);
  CHECK_EQ(c.getLocalTm().tm_hour, 2);
  CHECK_EQ(c.getTm().tm_hour, 0);
}

TEST(time_is_local) {
  fakeMillis = 0;
  Client c;
  uint8_t z[2] = {4, 0};  // UTC+1
  c.local(z, 2);
  c.setTimeIsLocal(true);
  uint8_t d[10];
  TinyCTSCurrentTime::fromUnixMs(1767229200000ULL).encode(d);  // 01:00 local
  c.store(d, 10, 0, 0);
  CHECK_NEAR(c.getTimeMs(), 1767225600000ULL, 5);  // 00:00 UTC
  uint8_t out[10];
  c.write(1767225600000ULL, out);
  TinyCTSCurrentTime t;
  TinyCTSCurrentTime::decode(out, 10, t);
  CHECK_EQ(t.hours, 1);
  CHECK_EQ(t.adjustReason, TinyCTSCurrentTime::ManualUpdate);
}

TEST(wanted_device) {
  Client c;
  c.wanted("aa:bb:cc:dd:ee:ff");
  CHECK(c.is("AA:BB:CC:DD:EE:FF", "x"));
  CHECK(!c.is("AA:BB:CC:DD:EE:00", "x"));
  c.wanted("my-clock");
  CHECK(c.is("00:00:00:00:00:00", "my-clock"));
}

TEST_MAIN("clientbase")

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


TEST(round_trip_and_update_time) {
  Client c;
  CHECK_EQ(c.getRoundTripMs(), 0u);
  uint8_t d[10];
  TinyCTSCurrentTime::fromUnixMs(1767225600000ULL).encode(d);
  CHECK(c.store(d, 10, 9000, 9060));
  CHECK_EQ(c.getRoundTripMs(), 60u);
  CHECK_EQ(c.getUpdateMillis(), 9060u);
  CHECK(c.getLastError().empty());
}

TEST(error_for_an_unknown_time) {
  Client c;
  uint8_t d[10] = {0};
  CHECK(!c.store(d, 10, 0, 0));
  CHECK(!c.getLastError().empty());
}


/// A client whose update() "reads" a device with a fixed clock.
struct AutoClient : TinyCTSClientBase {
  int reads = 0;
  bool reachable = true;
  uint64_t deviceMs = 1767225600000ULL;  // device time at fakeMillis = 0
  bool update() override {
    reads++;
    if (!reachable) return false;
    uint8_t d[10];
    TinyCTSCurrentTime::fromUnixMs(deviceMs + fakeMillis).encode(d);
    return storeCurrentTime(d, 10, fakeMillis, fakeMillis);
  }
};

TEST(manual_by_default) {
  fakeMillis = 1000;
  AutoClient c;
  CHECK_EQ(c.getTimeMs(), (uint64_t)0);
  CHECK_EQ(c.reads, 0);
}

TEST(reads_when_unknown_and_after_the_interval) {
  fakeMillis = 1000;
  AutoClient c;
  c.setUpdateInterval(3600000);
  CHECK_NEAR(c.getTimeMs(), c.deviceMs + 1000, 8);  // first read
  CHECK_EQ(c.reads, 1);
  fakeMillis += 1800000;  // half an hour: extrapolated
  CHECK_NEAR(c.getTimeMs(), c.deviceMs + fakeMillis, 8);
  CHECK_EQ(c.reads, 1);
  fakeMillis += 1800000;  // one hour after the read: read again
  c.getTimeMs();
  CHECK_EQ(c.reads, 2);
}

TEST(failed_reads_wait_for_the_retry_time) {
  fakeMillis = 1000;
  AutoClient c;
  c.reachable = false;
  c.setUpdateInterval(3600000, 60000);
  CHECK_EQ(c.getTimeMs(), (uint64_t)0);
  CHECK_EQ(c.reads, 1);
  fakeMillis += 30000;
  CHECK_EQ(c.getTimeMs(), (uint64_t)0);  // no new attempt yet
  CHECK_EQ(c.reads, 1);
  fakeMillis += 30000;
  c.reachable = true;
  CHECK(c.getTimeMs() != 0);  // retry
  CHECK_EQ(c.reads, 2);
}

TEST(update_only_in_update_if_due) {
  fakeMillis = 1000;
  AutoClient c;
  c.setUpdateInterval(3600000);
  c.setUpdateInGetTime(false);
  CHECK_EQ(c.getTimeMs(), (uint64_t)0);  // getTimeMs() does not block
  CHECK_EQ(c.reads, 0);
  CHECK(c.updateIfDue());
  CHECK(!c.updateIfDue());  // not due again
  CHECK_EQ(c.reads, 1);
  CHECK(c.getTimeMs() != 0);
}

TEST_MAIN("clientbase")

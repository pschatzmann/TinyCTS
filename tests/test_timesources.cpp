// Tests of the time sources
#include <chrono>
#include <cstdint>

static uint32_t fakeMillis = 0;
#define TINY_CTS_MILLIS() (fakeMillis)

#include "test.h"
#include "TinyCTS/TinyCTSTimeSource.h"

using namespace tiny_cts;

struct Fixed : TinyCTSTimeSource {
  uint64_t getTimeMs() override { return 42; }
};

TEST(base_class_cannot_be_set) {
  Fixed f;
  CHECK_EQ(f.getTimeMs(), (uint64_t)42);
  CHECK(!f.setTimeMs(1));
}

TEST(clock_not_set_is_zero) {
  TinyCTSClockSource c;
  CHECK(!c.isSet());
  CHECK_EQ(c.getTimeMs(), (uint64_t)0);
}

TEST(clock_counts_with_millis) {
  TinyCTSClockSource c;
  fakeMillis = 1000;
  c.setTime(1700000000, 250);
  CHECK(c.isSet());
  fakeMillis = 3500;
  CHECK_EQ(c.getTimeMs(), (uint64_t)1700000002750ULL);
  c.clear();
  CHECK_EQ(c.getTimeMs(), (uint64_t)0);
}

TEST(clock_survives_millis_overflow) {
  TinyCTSClockSource c;
  fakeMillis = 0xFFFFFF00u;
  c.setTimeMs(1000000000000ULL);
  fakeMillis = 0x100;  // wrapped: 512 ms later
  CHECK_EQ(c.getTimeMs(), (uint64_t)1000000000512ULL);
}

TEST(system_source) {
  TinyCTSSystemSource s;
  uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
  CHECK_NEAR(s.getTimeMs(), now, 1000);
  TinyCTSSystemSource future(4000000000UL);  // clock "not set"
  CHECK_EQ(future.getTimeMs(), (uint64_t)0);
}

struct HasGetTime {
  uint64_t getTimeMs() { return 7; }
};

TEST(adapter) {
  HasGetTime h;
  TinyCTSSourceAdapter<HasGetTime> a(h);
  CHECK_EQ(a.getTimeMs(), (uint64_t)7);
  CHECK(!a.setTimeMs(1));
}

TEST_MAIN("timesources")

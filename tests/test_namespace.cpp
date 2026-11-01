// TinyCTS.h uses the namespace; TINY_CTS_NO_BLE leaves out NimBLE
#define TINY_CTS_NO_BLE
#include "test.h"
#include "TinyCTS.h"

TEST(plain_names) {
  TinyCTSCurrentTime t;  // no tiny_cts:: needed
  CHECK_EQ(t.year, 0);
  TinyCTSClockSource c;
  CHECK(!c.isSet());
}

TEST_MAIN("namespace")

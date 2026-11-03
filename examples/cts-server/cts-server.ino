// Serves the time of the system clock to the BLE devices around (phones,
// watches, other boards with a TinyCTSClient). Here the clock is set by hand:
// use the time of an RTC, a GPS or the TinyNTPClient instead.
#include "TinyCTS.h"

TinyCTSClockSource clock_;
TinyCTSServer server;

void setup() {
  Serial.begin(115200);
  clock_.setTime(1767225600);  // 2026-01-01 00:00:00 UTC: replace with a real time
  server.setTimeZone(60, 0);   // CET, no summer time
  server.setReferenceInfo(TinyCTSReferenceTimeInfo::Manual);
  server.setNotifyInterval(60000);  // tell the subscribed clients every minute
  // let the clients set the time, if they are paired:
  // server.enableTimeSetting();
  if (!server.begin(clock_, "TinyCTS")) Serial.println("begin failed");
}

void loop() {
  server.process();
  delay(10);
}

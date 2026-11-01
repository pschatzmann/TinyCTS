// Reads the time from a BLE device with the Current Time Service (a phone with
// a CTS app, a smart watch, a board with a TinyCTSServer).
#include "TinyCTS.h"

TinyCTSClient cts;  // the first device that offers the service

void setup() {
  Serial.begin(115200);
  if (!cts.begin()) {
    Serial.println("no device with the Current Time Service found");
    return;
  }
  Serial.print("time from ");
  Serial.print(cts.getDeviceName().c_str());
  Serial.print(" (");
  Serial.print(cts.getDeviceAddress().c_str());
  Serial.println(")");
}

void loop() {
  std::tm tm = cts.getTm();  // UTC
  char text[64];
  snprintf(text, sizeof text, "%04d-%02d-%02d %02d:%02d:%02d UTC", tm.tm_year + 1900,
           tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  Serial.println(text);
  delay(1000);
}

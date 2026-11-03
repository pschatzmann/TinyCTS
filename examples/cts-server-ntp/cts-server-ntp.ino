// Serves the time to the BLE devices around (phones, watches, other boards with
// a TinyCTSClient). The initial time comes from an NTP server over WiFi (with
// the TinyNTP library); then WiFi is switched off and the software clock keeps
// the time.
#include <WiFi.h>
#include <WiFiUdp.h>
#include "TinyNTP.h"
#include "TinyCTS.h"

const char* ssid = "SSID";
const char* password = "PASSWORD";

TinyNTPClient<WiFiUDP> ntp;
TinyCTSClockSource clock_;
TinyCTSServer server;

void setTimeFromNTP() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
#ifdef ESP32
  WiFi.setSleep(false);  // the access point does not hold back the responses
#endif

  while (!ntp.begin()) {
    Serial.println("NTP request failed");
    delay(1000);
  }
  clock_.setTimeMs(ntp.getTimeMs());  // UTC
  Serial.print("time from NTP: ");
  Serial.println(ntp.getTimeSec());

  // the time is known: WiFi is not needed any more
  ntp.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void setup() {
  Serial.begin(115200);
  setTimeFromNTP();
  server.setTimeZone(60, 0);  // CET, no summer time
  server.setReferenceInfo(TinyCTSReferenceTimeInfo::NetworkTimeProtocol);
  server.setNotifyInterval(60000);  // tell the subscribed clients every minute
  if (!server.begin(clock_, "TinyCTS")) Serial.println("begin failed");
}

void loop() {
  server.process();
  delay(10);
}

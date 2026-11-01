// tinycts: command line Current Time Service (CTS) client and server for the
// desktop (Linux, macOS, Windows) based on the TinyCTS library (TinyCTSClient and
// TinyCTSServer with the backend of the operating system).
//
//   tinycts [options] [device]     read the time of a device
//   tinycts scan [options]         list the devices that are in range
//   tinycts serve [options]        serve the time of this computer
//   tinycts set [options] device   set the time of a device
//   tinycts tz                     the time zone information of this computer
//
// (main() is in main.cpp.)
//
// Build: cmake -S tools/tinycts -B build-tinycts && cmake --build build-tinycts
#include <signal.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>

#include "TinyCTS.h"

static bool timeIsLocal = false;

// ---- command line -----------------------------------------------------------
static const char* usage =
    "tinycts: Bluetooth Current Time Service client and server based on the\n"
    "TinyCTS library (Linux, macOS, Windows)\n"
    "\n"
    "Usage:\n"
    "  tinycts [options] [device]   show the time of a device\n"
    "  tinycts scan [options]       list the devices in range (the ones with the\n"
    "                               Current Time Service are marked)\n"
    "  tinycts serve [options]      serve the time of this computer\n"
    "  tinycts set [options] <device>\n"
    "                               set the time of a device that allows it\n"
    "                               (e.g. a TinyCTSServer with enableTimeSetting())\n"
    "  tinycts tz                   the time zone information of this computer as\n"
    "                               it is sent to the clients\n"
    "\n"
    "Options:\n"
    "  -i <adapter>   Bluetooth adapter (Linux only, default: hci0)\n"
    "  -t <ms>        time to look for the device (default: 10000)\n"
    "  -n <seconds>   serve: notify the subscribed clients every n seconds\n"
    "                 (default: 0: only when a client reads)\n"
    "  -a <name>      serve: name of the device (default: tinycts)\n"
    "  -z             set: send also the time zone of this computer\n"
    "  --local        the Current Time is the local time instead of UTC\n"
    "  -v             show diagnostic messages\n"
    "  -h             show this help\n"
    "\n"
    "The device is a Bluetooth address (AA:BB:CC:DD:EE:FF) or a name (macOS does not\n"
    "tell the addresses: use the identifier that 'scan' shows). Without it,\n"
    "the first device in range that advertises the Current Time Service is used.\n"
    "Phones usually do not advertise it: use the address of the phone.\n"
    "'set' writes the clock of this computer, corrected by half of the round trip\n"
    "time. If the device requires an encrypted connection, pair it first (e.g.\n"
    "with bluetoothctl). Serving needs a Bluetooth adapter that can be a\n"
    "peripheral; the clients get the time of this computer: make sure that it is\n"
    "synchronized.\n";

static int64_t systemTimeMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static void printTime(const char* label, uint64_t ms, const char* suffix) {
  time_t sec = ms / 1000;
  std::tm tm;
  ctsGmtime(sec, tm);
  printf("%-14s %04d-%02d-%02d %02d:%02d:%02d.%03d%s\n", label, tm.tm_year + 1900,
         tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
         static_cast<int>(ms % 1000), suffix);
}

static std::string offsetText(int minutes) {
  char buf[16];
  snprintf(buf, sizeof buf, "UTC%c%02d:%02d", minutes < 0 ? '-' : '+', abs(minutes) / 60,
           abs(minutes) % 60);
  return buf;
}

static std::tm localTm(time_t t) {
  std::tm tm;
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  return tm;
}

/// The offset of the local time of this computer to UTC in minutes (summer time included)
static int localOffsetMinutes(time_t t) {
  std::tm tm = localTm(t);
  int64_t asUtc = static_cast<int64_t>(ctsDaysFromCivil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday)) * 86400 +
                  tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
  return static_cast<int>((asUtc - static_cast<int64_t>(t)) / 60);
}

/// The time zone of this computer as CTS sends it
static TinyCTSLocalTimeInfo localInfo() {
  time_t now = time(nullptr);
  int dst = localTm(now).tm_isdst > 0 ? 60 : 0;
  return TinyCTSLocalTimeInfo::fromMinutes(localOffsetMinutes(now) - dst, dst);
}

// ---- tinycts [device] -------------------------------------------------------
static int runQuery(const std::string& adapter, const std::string& wanted, uint32_t timeoutMs) {
  TinyCTSClient client(wanted.empty() ? nullptr : wanted.c_str(), timeoutMs);
  client.setAdapter(adapter.c_str());
  client.setTimeIsLocal(timeIsLocal);
  if (!client.begin()) {
    fprintf(stderr, "%s (waited %u ms)\n", client.getLastError().c_str(), timeoutMs);
    return 1;
  }
  const TinyCTSLocalTimeInfo& info = client.getLocalTimeInfo();
  const TinyCTSReferenceTimeInfo& ref = client.getReferenceTimeInfo();
  uint64_t ms = client.getTimeMs();
  int offset = info.utcOffsetMinutes();

  printf("%-14s %s (%s)\n", "device:", client.getDeviceName().c_str(),
         client.getDeviceAddress().c_str());
  printTime("UTC:", ms, "");
  if (info.isKnown()) {
    printTime("local:", ms + static_cast<int64_t>(offset) * 60000,
              (" " + offsetText(offset) + (info.dstMinutes() > 0 ? " (summer time)" : "")).c_str());
  }
  if (ref.source != TinyCTSReferenceTimeInfo::Unknown || ref.accuracyMs() >= 0 ||
      ref.daysSinceUpdate < 255) {
    std::string text = TinyCTSReferenceTimeInfo::sourceName(ref.source);
    if (ref.accuracyMs() >= 0) text += ", accuracy " + std::to_string(ref.accuracyMs()) + " ms";
    if (ref.daysSinceUpdate < 255) {
      text += ", updated " + std::to_string(ref.daysSinceUpdate) + " d " +
              std::to_string(ref.hoursSinceUpdate == 255 ? 0 : ref.hoursSinceUpdate) + " h ago";
    }
    printf("%-14s %s\n", "source:", text.c_str());
  }
  printf("%-14s %+.3f s (device - this computer)\n", "clock offset:",
         (static_cast<int64_t>(ms) - systemTimeMs()) / 1000.0);
  return 0;
}

// ---- tinycts scan -----------------------------------------------------------
static int runScan(const std::string& adapter, uint32_t timeoutMs) {
  TinyCTSClient client;
  client.setAdapter(adapter.c_str());
  std::vector<TinyCTSScanEntry> devices = client.scan(timeoutMs);
  for (const TinyCTSScanEntry& d : devices) {
    printf("%s  %4d dBm  %-30s %s\n", d.address.c_str(), d.rssi, d.name.c_str(),
           d.hasCts ? "Current Time Service" : "");
  }
  if (devices.empty()) fprintf(stderr, "%s\n", client.getLastError().empty() ? "no devices found" : client.getLastError().c_str());
  return devices.empty() ? 1 : 0;
}

// ---- tinycts set ------------------------------------------------------------
static int runSet(const std::string& adapter, const std::string& wanted, uint32_t timeoutMs,
                  bool withZone) {
  TinyCTSClient client(wanted.c_str(), timeoutMs);
  client.setAdapter(adapter.c_str());
  client.setTimeIsLocal(timeIsLocal);
  TinyCTSLocalTimeInfo info = localInfo();
  if (timeIsLocal) client.setLocalTimeInfo(info);  // the device is in the zone of this computer
  if (withZone && !client.setTimeZone(info)) {
    fprintf(stderr, "cannot set the time zone: %s\n", client.getLastError().c_str());
    return 1;
  }
  if (!client.setTimeMs(systemTimeMs())) {
    fprintf(stderr, "%s\n", client.getLastError().c_str());
    return client.getLastError().rfind("rejected", 0) == 0 ? 3 : 1;
  }
  uint64_t ms = systemTimeMs();
  printf("%-14s %s (%s)\n", "device:", client.getDeviceName().c_str(),
         client.getDeviceAddress().c_str());
  printTime("time set:", ms, timeIsLocal ? " local" : " UTC");
  printf("%-14s %u ms (half of it is added to the time)\n", "round trip:", client.getRoundTripMs());
  return 0;
}

// ---- tinycts tz -------------------------------------------------------------
static int runZone() {
  TinyCTSLocalTimeInfo info = localInfo();
  time_t now = time(nullptr);
  std::tm tm = localTm(now);
  char name[64];
  strftime(name, sizeof name, "%Z", &tm);
  printf("%-14s %s (%s)\n", "zone:", name, offsetText(localOffsetMinutes(now)).c_str());
  printf("%-14s %d (%d minutes)\n", "time zone:", info.timeZone, info.zoneMinutes());
  printf("%-14s %d (%d minutes%s)\n", "dst offset:", info.dstOffset, info.dstMinutes(),
         tm.tm_isdst > 0 ? ", summer time" : "");
  printf("%-14s %04d-%02d-%02d %02d:%02d:%02d\n", "now:", tm.tm_year + 1900, tm.tm_mon + 1,
         tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return 0;
}

// ---- tinycts serve ----------------------------------------------------------
static volatile sig_atomic_t stopRequested = 0;
static void onSignal(int) { stopRequested = 1; }

static int runServe(const std::string& adapter, const std::string& name, int notifyEvery) {
  TinyCTSSystemSource source;
  TinyCTSServer server;
  server.setAdapter(adapter.c_str());
  server.setTimeIsLocal(timeIsLocal);
  TinyCTSLocalTimeInfo info = localInfo();
  server.setTimeZone(info.zoneMinutes(), info.dstMinutes());
  if (notifyEvery > 0) server.setNotifyInterval(notifyEvery * 1000U);
  if (!server.begin(source, name.c_str())) {
    fprintf(stderr, "%s\n", server.getLastError().c_str());
    return 1;
  }
  signal(SIGINT, onSignal);
  signal(SIGTERM, onSignal);
  printf("serving the time of this computer as \"%s\" (%s), stop with ctrl-c\n", name.c_str(),
         timeIsLocal ? "local time" : "UTC");
  fflush(stdout);
  while (!stopRequested) {
    server.process();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  server.end();
  printf("\nserved %u requests\n", server.getRequestCount());
  return 0;
}

int tinycts_main(int argc, char** argv) {
  bool serveMode = false, set = false, scan = false, zone = false, withZone = false;
  std::string device, adapter = "hci0", name = "tinycts";
  uint32_t timeoutMs = 10000;
  int notifyEvery = 0;
  bool verbose = false;

  for (int i = 1; i < argc; i++) {
    const char* a = argv[i];
    auto value = [&]() -> const char* {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing value for %s\n", a);
        exit(2);
      }
      return argv[++i];
    };
    if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
      fputs(usage, stdout);
      return 0;
    } else if (!strcmp(a, "-v")) {
      verbose = true;
    } else if (!strcmp(a, "-i")) {
      adapter = value();
    } else if (!strcmp(a, "-t")) {
      timeoutMs = atoi(value());
    } else if (!strcmp(a, "-n")) {
      notifyEvery = atoi(value());
    } else if (!strcmp(a, "-a")) {
      name = value();
    } else if (!strcmp(a, "-z")) {
      withZone = true;
    } else if (!strcmp(a, "--local")) {
      timeIsLocal = true;
    } else if (!strcmp(a, "serve")) {
      serveMode = true;
    } else if (!strcmp(a, "set")) {
      set = true;
    } else if (!strcmp(a, "scan")) {
      scan = true;
    } else if (!strcmp(a, "tz")) {
      zone = true;
    } else if (a[0] == '-') {
      fprintf(stderr, "unknown option %s\n\n%s", a, usage);
      return 2;
    } else {
      device = a;
    }
  }
  if (timeoutMs == 0 || notifyEvery < 0) {
    fprintf(stderr, "invalid option value\n\n%s", usage);
    return 2;
  }
#if defined(TINY_CTS_USE_BLUEZ)
  tiny_cts::bluez::verbose() = verbose;
#else
  (void)verbose;
#endif
  if (zone) return runZone();
  if (set && device.empty()) {
    fprintf(stderr, "set needs the address or the name of the device\n");
    return 2;
  }
  if (serveMode) return runServe(adapter, name, notifyEvery);
  if (scan) return runScan(adapter, timeoutMs);
  if (set) return runSet(adapter, device, timeoutMs, withZone);
  return runQuery(adapter, device, timeoutMs);
}

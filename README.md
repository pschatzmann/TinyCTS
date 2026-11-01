# TinyCTS

A header-only Bluetooth Low Energy **Current Time Service** (CTS, GATT service 0x1805) client and server for Arduino and the Linux desktop. The same classes (`TinyCTSClient`, `TinyCTSServer`) run on several Bluetooth stacks, which are selected automatically:

| Platform | Stack | Needs |
|---|---|---|
| ESP32, nRF5 | [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) 2.x | the library |
| Arduino Nano 33 BLE, Nano RP2040 Connect, Portenta, Giga, Uno R4 WiFi, Nano 33 IoT, MKR WiFi 1010 | [ArduinoBLE](https://github.com/arduino-libraries/ArduinoBLE) | the library |
| Raspberry Pi Pico W / Pico 2 W | BLE library of the arduino-pico core (BTstack) | *Tools -> IP/Bluetooth Stack -> IPv4 + Bluetooth* |
| Linux | BlueZ over D-Bus | GLib/GIO (`libglib2.0-dev`) |
| macOS | CoreBluetooth | Xcode command line tools **(not tested)** |
| Windows 10/11 | WinRT (C++/WinRT) | Visual Studio **(not tested)** |

(The choice can be forced with `TINY_CTS_USE_NIMBLE`, `TINY_CTS_USE_ARDUINOBLE`, `TINY_CTS_USE_PICO_BLE`, `TINY_CTS_USE_BLUEZ`, `TINY_CTS_USE_COREBLUETOOTH` or `TINY_CTS_USE_WINRT`.) It is the BLE counterpart of [TinyNTP](https://github.com/pschatzmann/TinyNTP): the same ideas (time sources, client, server, command line tool), but the time travels over Bluetooth instead of the network.

## Features

**Client** (`TinyCTSClient`)
- Finds a device that offers the Current Time Service (or the one with a given address or name), reads the time, the time zone and the reference time information, and disconnects
- Extrapolates the time with `millis()` between updates; returns seconds, milliseconds or `std::tm` (UTC and local time)
- Can set the time of a device that allows it (`setTimeMs()`)
- Is a `TinyCTSTimeSource`: it can feed a `TinyCTSServer` (a BLE time repeater)

**Server** (`TinyCTSServer`)
- Serves the time of a `TinyCTSTimeSource` to phones, watches, other boards and `tinycts`: Current Time (read, notify), Local Time Information and Reference Time Information
- Time sources: `TinyCTSClockSource` (set it from an RTC or GPS), `TinyCTSSystemSource` (system clock), `TinyCTSSourceAdapter` (any object with `getTimeMs()`, e.g. the `TinyNTPClient` of TinyNTP) or your own class derived from `TinyCTSTimeSource`
- Optionally lets paired clients set the time (`enableTimeSetting()`)

**Command line tool** (`tinycts` for Linux, macOS and Windows, built on the classes above)
- `tinycts [device]`: show the time of a device
- `tinycts scan`: list the devices in range
- `tinycts serve`: serve the time of your computer
- `tinycts set <device>`: set the time of a device
- `tinycts tz`: the time zone information of your computer, as it is sent to the clients

## CTS or NTP?

[TinyNTP](https://github.com/pschatzmann/TinyNTP) gets the time from a server on the network. TinyCTS gets it from a device that is next to you, over Bluetooth. Neither is better in general: they solve different problems.

**What TinyCTS gives you**

- **No network needed.** No Wi-Fi, no Ethernet, no internet, no router, no NTP server and no access point password in your firmware. This is the only option for boards that have Bluetooth but no IP connection (nRF52, Nano 33 BLE, ...), for devices that are used outdoors or on the move, and for places where Wi-Fi is not allowed or not available.
- **Lower power.** A short BLE connection every now and then costs much less energy than joining a Wi-Fi network, and BLE can run while the Wi-Fi radio stays off.
- **The time zone comes with the time.** CTS sends the time zone and the summer time offset (Local Time Information), so a device shows the right local time without a time zone table or a POSIX TZ string. When the user travels or the clocks change, the device that you read follows.
- **Use the clock that is already there.** A phone, a smart watch or a computer is usually set by the mobile network or by NTP. Your small device does not need to be able to reach an NTP server itself.
- **Other direction too.** A board with Wi-Fi can run a `TinyNTPClient` and a `TinyCTSServer` and give the network time to all the BLE-only devices around it (see below). Or a board with a GPS/RTC serves the time to phones and other boards without any infrastructure.
- **Simple deployment.** Pairing-free reading, nothing to configure on the network (no UDP port, no firewall, no DNS).

**What NTP is better at**

- **Accuracy.** NTP measures the round trip time and the offset of the clock, and it knows the quality of the server (stratum, dispersion). CTS has none of that: the time is only as good as the clock of the other device, the resolution is 1/256 s (about 4 ms) and the delay of the BLE connection (tens of milliseconds) can only be estimated. `TinyCTSClient` takes half of the round trip time into account, which is good enough for a clock or a log, but not for a measurement that needs a few milliseconds.
- **Range and reach.** NTP works over the whole internet. CTS needs a device with the service within Bluetooth range (a few meters to some ten meters), and it is not the first thing that you find on a phone: many phones offer CTS only through an app, or only to paired accessories.
- **Authority.** The pool servers are synchronized to reference clocks. A phone or a watch is only as good as its own source, and the Reference Time Information tells you only what the other device claims.
- **Security.** NTP is not secure either, but CTS has no authentication of its own: it relies on BLE pairing, and the time that you read from an unknown device in range is not trustworthy (see Security below).

**Rules of thumb**

| You have | Use |
|---|---|
| A board with Wi-Fi/Ethernet and internet | TinyNTP |
| A board with Bluetooth only, or no network at hand | TinyCTS (client) |
| A phone, watch or computer that should get the time of your board | TinyCTS (server) |
| Both kinds of boards | TinyNTP on the connected board, plus a `TinyCTSServer` that repeats its time |
| A time stamp that must be accurate to a few milliseconds | NTP or GPS, not CTS |

The two libraries work together. A time source of one library can feed the other with the adapter `TinyCTSSourceAdapter`:

```cpp
// ESP32: gets the time from the internet and serves it to the BLE devices around
TinyNTPClient<WiFiUDP> ntp(NTP_IP_GOOGLE_1);
TinyCTSSourceAdapter<TinyNTPClient<WiFiUDP>> source(ntp);
TinyCTSServer cts;

void setup() {
  WiFi.begin("SSID", "PASSWORD");
  while (WiFi.status() != WL_CONNECTED) delay(500);
  ntp.begin();
  cts.setReferenceInfo(TinyCTSReferenceTimeInfo::NetworkTimeProtocol);
  cts.begin(source, "ntp-repeater");
}

void loop() { cts.process(); }
```

In the other direction `TinyCTSClient` has a `getTimeMs()`, so it can serve the time to a `TinyNTPServer` with a small adapter, which makes your board an NTP server for the network that gets its time from a phone:

```cpp
struct CtsTimeSource : tiny_ntp::TinyTimeSource {
  TinyCTSClient& cts;
  explicit CtsTimeSource(TinyCTSClient& c) : cts(c) {}
  uint64_t getTimeMs() override { return cts.getTimeMs(); }
};
```

(call `cts.update()` from time to time to stay in sync).

## Quick Start

Client:
```cpp
#include "TinyCTS.h"

TinyCTSClient cts;  // the first device that offers the service

void setup() {
  Serial.begin(115200);
  cts.begin();  // scans, connects, reads the time
}

void loop() {
  std::tm tm = cts.getTm();  // UTC; getLocalTm() uses the time zone of the device
  Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n", tm.tm_year + 1900, tm.tm_mon + 1,
                tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  delay(1000);
}
```

Server:
```cpp
#include "TinyCTS.h"

TinyCTSClockSource clock_;
TinyCTSServer server;

void setup() {
  clock_.setTime(rtc.now().unixtime());
  server.setTimeZone(60, 0);  // CET (minutes); the second value is the summer time offset
  server.begin(clock_, "my-clock");
}

void loop() { server.process(); }
```

All classes are in the namespace `tiny_cts`, which `TinyCTS.h` uses (`using namespace tiny_cts;`). Define `NO_TINY_CTS_NAMESPACE` before the include to switch this off. Define `TINY_CTS_NO_BLE` to leave out the Bluetooth classes (code that only needs the encoding of the characteristics or the time sources).

On the Pico W `begin()` starts the BLE hardware: do not call `BLE.begin()` yourself. The calls of the client block until they are done. On the boards with ArduinoBLE `begin()` starts the hardware (`BLE.begin()`) and `process()` calls `BLE.poll()`; ArduinoBLE can not refuse a write, so the time is restored, and `enableTimeSetting()` makes the writable characteristics encrypted (also for reading). On the desktop `TinyCTSServer::process()` must be called regularly (Linux: it handles the requests of BlueZ); the client has additionally `scan()`, `getLastError()` and `setAdapter()`.

**Desktop notes:** macOS does not tell the Bluetooth address of a device: use the identifier that `scan` shows. Windows advertises the name of the computer (the name of the server is ignored). On macOS and Windows the sources `src/TinyCTS/desktop/TinyCTSCoreBluetooth.mm` and `src/TinyCTS/desktop/TinyCTSWinRT.cpp` have to be part of the build: the CMake target `TinyCTS` adds them. The macOS and Windows code could not be compiled or run when it was written: please report problems.

## UTC or local time?

The specification leaves it open if the Current Time characteristic contains UTC or the local time. TinyCTS sends **UTC** by default, together with the Local Time Information (time zone and summer time offset), so that the client can calculate the local time. Some clients (e.g. phones) expect the local time: use `server.setTimeIsLocal(true)` / `client.setTimeIsLocal(true)` / `tinycts --local`.

## Security

CTS has no authentication of its own: it relies on BLE pairing. Setting the time is off by default. `enableTimeSetting()` requires an encrypted (paired) connection unless you pass `false`; use that only in a trusted environment, because everybody in range could change the time you serve.

## Command line tool

```
cmake -S tools/tinycts -B build-tinycts && cmake --build build-tinycts   # Windows: add --config Release
build-tinycts/tinycts scan
build-tinycts/tinycts AA:BB:CC:DD:EE:FF
```

The desktop classes are used by the tool as well: `TinyCTSClient` and `TinyCTSServer` in your own program (link with the target `TinyCTS` in cmake). Linux needs BlueZ and the development files of GLib (Debian/Ubuntu: `libglib2.0-dev`). On macOS the program needs the permission to use Bluetooth (the tool embeds an `Info.plist`; the terminal may ask). `tinycts --help` lists the options. Output of a query:

```
device:        my-clock (AA:BB:CC:DD:EE:FF)
UTC:           2026-11-01 08:08:26.250
local:         2026-11-01 09:08:26.250 UTC+01:00
source:        NTP, accuracy 125 ms
clock offset:  +0.012 s (device - this computer)
```

## Source layout

- `src/TinyCTS/`: the parts that are the same everywhere: `TinyCTSTypes.h` (encoding of the characteristics), `TinyCTSTimeSource.h`, `TinyCTSServerBase.h` / `TinyCTSClientBase.h` (the logic), `TinyCTSConfig.h` (selects the stack) and the headers `TinyCTSServer.h` / `TinyCTSClient.h` that include the right backend
- `src/TinyCTS/mcu/`: the backends for microcontrollers: NimBLE, ArduinoBLE, Pico W
- `src/TinyCTS/desktop/`: the backends for the desktop: BlueZ (Linux), CoreBluetooth (macOS), WinRT (Windows) and the classes `TinyCTSClient` / `TinyCTSServer` that use them

## Tests

The encoding of the characteristics and the time sources are tested on the desktop (no Bluetooth needed):

```
cmake -S tests -B build-tests && cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## Installation in Arduino

Clone the project into the Arduino libraries folder (on ESP32/nRF5 together with NimBLE-Arduino 2.x):

```
cd ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/TinyCTS.git
```

## Examples

- `cts-client`: read the time from a BLE device
- `cts-server`: serve the time of a software clock

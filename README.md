# TinyCTS

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-green.svg)](https://www.gnu.org/licenses/gpl-3.0)

A minimal, header-only Bluetooth Low Energy **Current Time Service** (CTS, GATT service 0x1805) client and server for Arduino and the desktop, designed for efficiency and portability. It works with the Bluetooth stack of your platform (NimBLE-Arduino, ArduinoBLE, the Pico W core, BlueZ, CoreBluetooth, WinRT). It is the Bluetooth counterpart of [TinyNTP](https://github.com/pschatzmann/TinyNTP): the same ideas, but the time travels over Bluetooth instead of the network.


## Features

**Client**
- Reads the time from a phone, a watch, a computer or another board that offers the Current Time Service, without a network
- Minimal resource usage, suitable for embedded/IoT
- Finds the first device with the service, or the one with a given address or name
- Returns time as seconds, milliseconds or `std::tm` struct (UTC and local time)
- Can set the time of a device that allows it

**Time zones**
- The time zone and the summer time offset come with the time (Local Time Information): no time zone table needed
- The Current Time can be UTC (default) or the local time

**Server**
- `TinyCTSServer` serves the time to the devices around you, from a software clock (`TinyCTSClockSource`) set by an RTC or GPS, the system time (`TinyCTSSystemSource`), the TinyNTP client (`TinyCTSSourceAdapter`); your own sources derive from `TinyCTSTimeSource`
- Optionally lets paired clients set the time

**General**
- `tinycts`: command line tool for the desktop (Linux; macOS and Windows are *experimental*) to read the time of a device, serve the time of your computer, set the time of a device and show the time zone of your computer
- The same classes on ESP32, nRF5, Arduino Nano 33 BLE / Uno R4 WiFi (ArduinoBLE), Raspberry Pi Pico W and the desktop (Linux; macOS and Windows are *experimental*)
- Header-only, easy to integrate
- cmake support


**Experimental:** the backends for macOS (CoreBluetooth) and Windows (WinRT) have not been compiled or run yet. They are included so that they can be tried and improved; please report problems.


## Quick Start

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
Call `cts.update()` from time to time (e.g. once per hour) to stay in sync, or let the client do it: with `cts.setUpdateInterval(3600000)` it reads the device when the time is not known yet and then every hour (after a failed read it tries again after a minute). In between, the time runs on with `millis()`.

To serve the time to the devices around you:

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

Include `TinyCTS.h`: it provides everything. All classes are in the namespace `tiny_cts`, and `TinyCTS.h` uses it (`using namespace tiny_cts;`), so you can use the plain class names. If your code has classes with the same names, define `NO_TINY_CTS_NAMESPACE` before you include `TinyCTS.h` and write `tiny_cts::TinyCTSClient`. The Bluetooth stack is selected from the board: see [Bluetooth Stacks](https://github.com/pschatzmann/TinyCTS/wiki/Bluetooth-Stacks). Define `TINY_CTS_NO_BLE` to leave out the Bluetooth classes (code that only needs the encoding of the characteristics or the time sources).


## Documentation

- [Class Documentation](https://pschatzmann.github.io/TinyCTS/annotated.html)
- [Bluetooth Stacks](https://github.com/pschatzmann/TinyCTS/wiki/Bluetooth-Stacks): the supported platforms (ESP32, nRF5, ArduinoBLE boards, Pico W, Linux, macOS and Windows (experimental)), notes for each and the source layout
- [CTS or NTP?](https://github.com/pschatzmann/TinyCTS/wiki/CTS-or-NTP): what Bluetooth time is good for, where NTP is better, and how to use both together with TinyNTP
- [Time Zones](https://github.com/pschatzmann/TinyCTS/wiki/Time-Zones): Local Time Information, UTC or local time
- [Server Time Sources](https://github.com/pschatzmann/TinyCTS/wiki/Server-Time-Sources): software clock, system time, TinyNTP and your own sources for the server
- [Security](https://github.com/pschatzmann/TinyCTS/wiki/Security): pairing and setting the time
- [Command Line Tool](https://github.com/pschatzmann/TinyCTS/wiki/Command-Line-Tool): `tinycts` for the desktop
- [Examples](https://github.com/pschatzmann/TinyCTS/wiki/Examples): the example sketches
- [Tests](https://github.com/pschatzmann/TinyCTS/wiki/Tests): the automatic tests and how to run them

## Installation in Arduino

You can download the library as zip and call include Library -> zip library. Or you can git clone this project into the Arduino libraries folder e.g. with

```
cd  ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/TinyCTS.git
```

I recommend to use git because you can easily update to the latest version just by executing the git pull command in the project folder. The Bluetooth library that your board needs is described in [Bluetooth Stacks](https://github.com/pschatzmann/TinyCTS/wiki/Bluetooth-Stacks) (e.g. NimBLE-Arduino 2.x for the ESP32).

## License

This library is licensed under the GNU GPL v3. See `License.txt` for details.

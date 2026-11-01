/**
 * @file TinyCTSConfig.h
 * @brief Selects the Bluetooth stack: the BLE library of the Raspberry Pi Pico
 * (W) core (BTstack) on the RP2040/RP2350, BlueZ on Linux (needs GLib/GIO),
 * CoreBluetooth on macOS, WinRT on Windows, NimBLE-Arduino everywhere else.
 * ArduinoBLE is used on the Arduino boards that are made for it (mbed cores,
 * Uno R4 WiFi, Nano 33 IoT, MKR WiFi 1010); define TINY_CTS_USE_ARDUINOBLE to
 * use it on another board. Define TINY_CTS_USE_NIMBLE, TINY_CTS_USE_PICO_BLE,
 * TINY_CTS_USE_BLUEZ,
 * TINY_CTS_USE_COREBLUETOOTH or TINY_CTS_USE_WINRT to choose yourself.
 * TINY_CTS_USE_DESKTOP is defined for the last three.
 */

#pragma once

#if !defined(TINY_CTS_USE_NIMBLE) && !defined(TINY_CTS_USE_PICO_BLE) && \
    !defined(TINY_CTS_USE_BLUEZ) && !defined(TINY_CTS_USE_COREBLUETOOTH) && \
    !defined(TINY_CTS_USE_WINRT) && !defined(TINY_CTS_USE_ARDUINOBLE)
// the boards for which ArduinoBLE is the Bluetooth library: Arduino mbed cores
// (Nano 33 BLE, Nano RP2040 Connect, Portenta, Giga), Uno R4 WiFi, Nano 33 IoT,
// MKR WiFi 1010
#if defined(ARDUINO_ARCH_MBED) || defined(ARDUINO_UNOR4_WIFI) || \
    defined(ARDUINO_SAMD_NANO_33_IOT) || defined(ARDUINO_SAMD_MKRWIFI1010)
#define TINY_CTS_USE_ARDUINOBLE
#elif defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_RP2350)
#define TINY_CTS_USE_PICO_BLE
#elif defined(__linux__) && !defined(ARDUINO)
#define TINY_CTS_USE_BLUEZ
#elif defined(__APPLE__) && !defined(ARDUINO)
#define TINY_CTS_USE_COREBLUETOOTH
#elif defined(_WIN32) && !defined(ARDUINO)
#define TINY_CTS_USE_WINRT
#else
#define TINY_CTS_USE_NIMBLE
#endif
#endif

#if defined(TINY_CTS_USE_BLUEZ) || defined(TINY_CTS_USE_COREBLUETOOTH) || \
    defined(TINY_CTS_USE_WINRT)
#define TINY_CTS_USE_DESKTOP
#endif

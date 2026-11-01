/**
 * @file TinyCTSClient.h
 * @brief The Current Time Service client: NimBLE-Arduino (ESP32, nRF5), BlueZ
 * (Linux), CoreBluetooth (macOS), WinRT (Windows) ArduinoBLE or the BLE library of the Raspberry Pi Pico W core, see TinyCTSConfig.h.
 */

#pragma once
#include "TinyCTSConfig.h"

#if defined(TINY_CTS_USE_DESKTOP)
#include "desktop/TinyCTSClientDesktop.h"
#elif defined(TINY_CTS_USE_ARDUINOBLE)
#include "mcu/TinyCTSClientArduinoBLE.h"
#elif defined(TINY_CTS_USE_PICO_BLE)
#include "mcu/TinyCTSClientPico.h"
#else
#include "mcu/TinyCTSClientNimBLE.h"
#endif

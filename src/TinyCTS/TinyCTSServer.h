/**
 * @file TinyCTSServer.h
 * @brief The Current Time Service server: NimBLE-Arduino (ESP32, nRF5), BlueZ
 * (Linux), CoreBluetooth (macOS), WinRT (Windows) ArduinoBLE or the BLE library of the Raspberry Pi Pico W core, see TinyCTSConfig.h.
 */

#pragma once
#include "TinyCTSConfig.h"

#if defined(TINY_CTS_USE_DESKTOP)
#include "desktop/TinyCTSServerDesktop.h"
#elif defined(TINY_CTS_USE_ARDUINOBLE)
#include "mcu/TinyCTSServerArduinoBLE.h"
#elif defined(TINY_CTS_USE_PICO_BLE)
#include "mcu/TinyCTSServerPico.h"
#else
#include "mcu/TinyCTSServerNimBLE.h"
#endif

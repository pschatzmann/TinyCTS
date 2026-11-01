/**
 * @file TinyCTSBackend.h
 * @brief Includes the Bluetooth stack of the desktop: BlueZ (Linux) is header
 * only. CoreBluetooth (macOS) and WinRT (Windows) are in the files
 * TinyCTSCoreBluetooth.mm and TinyCTSWinRT.cpp, which have to
 * be part of the build (the CMake target TinyCTS does that).
 */

#pragma once
#include "../TinyCTSConfig.h"
#include "TinyCTSDesktop.h"

#if defined(TINY_CTS_USE_BLUEZ)
#include "TinyCTSBlueZBackend.h"
#endif

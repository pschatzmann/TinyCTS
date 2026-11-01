/**
 * @file TinyCTS.h
 * @brief Include this file to use the library: Bluetooth Current Time Service
 * (CTS) client and server for NimBLE-Arduino, the time sources and the
 * encoding of the CTS characteristics. All classes are in the namespace
 * tiny_cts. This header uses the namespace (using namespace tiny_cts;), so
 * that the classes can be used without it. Define NO_TINY_CTS_NAMESPACE before
 * the include to switch this off.
 *
 * The client and the server need the library NimBLE-Arduino. Define
 * TINY_CTS_NO_BLE before the include to get only the time sources and the
 * encoding of the characteristics (e.g. on the desktop).
 */

#pragma once

#include "TinyCTS/TinyCTSTimeSource.h"
#include "TinyCTS/TinyCTSTypes.h"

// (not detected with __has_include: the Arduino build adds the library to the
// include path only after it has seen the #include)
#if !defined(TINY_CTS_NO_BLE)
#include "TinyCTS/TinyCTSClient.h"
#include "TinyCTS/TinyCTSServer.h"
#endif

#if !defined(NO_TINY_CTS_NAMESPACE)
using namespace tiny_cts;
#endif

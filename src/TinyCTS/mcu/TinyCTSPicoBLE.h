/**
 * @file TinyCTSPicoBLE.h
 * @brief Starts the BLE stack of the Raspberry Pi Pico (W) core once, for the
 * client and the server.
 */

#pragma once
#include <BLE.h>

namespace tiny_cts {

/**
 * Start the BLE hardware, if that has not been done yet.
 * @param name The name of the device (server)
 * @param pairing Allow pairing ("just works"); it can be selected only before
 * the BLE hardware is started.
 * @return false if the hardware was started before without the pairing that
 * is requested now.
 */
inline bool ctsPicoBegin(const char* name, bool pairing) {
  static bool started = false;
  static bool paired = false;
  if (started) return paired || !pairing;
  if (pairing) BLE.setSecurity(BLESecurityJustWorks);
  BLE.begin(name);
  started = true;
  paired = pairing;
  return true;
}

}  // namespace tiny_cts

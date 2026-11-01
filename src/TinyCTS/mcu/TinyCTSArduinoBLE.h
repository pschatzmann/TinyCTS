/**
 * @file TinyCTSArduinoBLE.h
 * @brief Starts the ArduinoBLE stack once, for the client and the server.
 */

#pragma once
#include <ArduinoBLE.h>

namespace tiny_cts {

/**
 * Start the BLE hardware, if that has not been done yet.
 * @return false if the hardware cannot be started.
 */
inline bool ctsArduinoBleBegin() {
  static bool started = false;
  if (!started) started = BLE.begin() != 0;
  return started;
}

}  // namespace tiny_cts

/**
 * @file TinyCTSDesktop.h
 * @brief The interface between the desktop classes TinyCTSClient/TinyCTSServer
 * and the Bluetooth stack of the operating system: BlueZ (Linux),
 * CoreBluetooth (macOS) and WinRT (Windows). The functions block until they are
 * done; every OS implements the two classes below.
 */

#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tiny_cts {

/// A device that was found by TinyCTSClient::scan()
struct TinyCTSScanEntry {
  /// The Bluetooth address. macOS does not tell it: there it is the identifier
  /// (UUID) that CoreBluetooth has assigned to the device.
  std::string address;
  std::string name;
  int rssi = 0;
  bool hasCts = false;  ///< advertises the Current Time Service
};

/// The client side of the operating system (central role).
class TinyCTSCentral {
 public:
  virtual ~TinyCTSCentral() = default;

  /** List the devices in range. */
  virtual bool scan(uint32_t timeoutMs, std::vector<TinyCTSScanEntry>& result,
                    std::string& error) = 0;

  /**
   * Find a device, connect and discover the Current Time Service.
   * @param device Address, identifier or name. Empty: the first device that
   * advertises the service.
   * @param timeoutMs Time to look for the device.
   */
  virtual bool connect(const std::string& device, uint32_t timeoutMs, TinyCTSScanEntry& found,
                       std::string& error) = 0;

  /** Read a characteristic of the connected device (16 bit UUID). */
  virtual bool read(uint16_t uuid, std::vector<uint8_t>& value, std::string& error) = 0;

  /** Write a characteristic of the connected device (with response). */
  virtual bool write(uint16_t uuid, const uint8_t* data, size_t len, std::string& error) = 0;

  /** Disconnect. */
  virtual void disconnect() = 0;
};

/// The requests of the clients, answered by the server.
class TinyCTSPeripheralHandler {
 public:
  virtual ~TinyCTSPeripheralHandler() = default;
  /** A client reads a characteristic: return its value. */
  virtual bool onRead(uint16_t uuid, std::vector<uint8_t>& value) = 0;
  /** A client writes a characteristic: return false to refuse. */
  virtual bool onWrite(uint16_t uuid, const uint8_t* data, size_t len) = 0;
};

/// The server side of the operating system (peripheral role): the service
/// 0x1805 with the characteristics Current Time (read, notify, write),
/// Local Time Information (read, write) and Reference Time Information (read).
class TinyCTSPeripheral {
 public:
  virtual ~TinyCTSPeripheral() = default;

  /**
   * Register the service and start the advertising.
   * @param settable The clients may write Current Time and Local Time Information.
   * @param encrypted Writing needs an encrypted (paired) connection.
   */
  virtual bool start(const std::string& name, bool settable, bool encrypted,
                     TinyCTSPeripheralHandler* handler, std::string& error) = 0;

  /** Handle the requests, if the stack needs the application to do that. */
  virtual void process() = 0;

  /** Send the new value of a characteristic to the subscribed clients. */
  virtual void notify(uint16_t uuid, const std::vector<uint8_t>& value) = 0;

  /** Stop. */
  virtual void stop() = 0;
};

/** The client side of this operating system. @param adapter Linux: hci0 */
std::unique_ptr<TinyCTSCentral> ctsCreateCentral(const std::string& adapter);

/** The server side of this operating system. @param adapter Linux: hci0 */
std::unique_ptr<TinyCTSPeripheral> ctsCreatePeripheral(const std::string& adapter);

}  // namespace tiny_cts

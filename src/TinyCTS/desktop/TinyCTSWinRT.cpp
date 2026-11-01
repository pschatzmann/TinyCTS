// TinyCTSCentral and TinyCTSPeripheral for Windows 10/11: C++/WinRT (Windows.Devices.Bluetooth).
//
// Compile with MSVC (C++17, /EHsc) and link windowsapp.lib: the CMake target
// TinyCTS does that. UNTESTED: this file could not be compiled or run when it was
// written.
//
// The calls block with .get(), so they must not be made from a single threaded
// apartment (STA) thread that has a message loop: a console program is fine.
// Windows chooses the name that is advertised (the name of the computer).
// (the Arduino IDE compiles every .cpp file of a library: nothing to do there)
#if defined(_WIN32) && !defined(ARDUINO)

#define _CRT_SECURE_NO_WARNINGS  // sscanf

#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>

#include "TinyCTSDesktop.h"
#include "../TinyCTSTypes.h"

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;
using namespace tiny_cts;

namespace {

guid ctsGuid(uint16_t uuid16) {
  return guid{static_cast<uint32_t>(uuid16), 0x0000, 0x1000,
              {0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb}};
}

void initApartment() {
  static bool done = false;
  if (!done) {
    done = true;
    try {
      init_apartment();
    } catch (...) {
      // already initialized by the program
    }
  }
}

std::string addressText(uint64_t a) {
  char buf[20];
  snprintf(buf, sizeof buf, "%02X:%02X:%02X:%02X:%02X:%02X", (unsigned)((a >> 40) & 0xFF),
           (unsigned)((a >> 32) & 0xFF), (unsigned)((a >> 24) & 0xFF),
           (unsigned)((a >> 16) & 0xFF), (unsigned)((a >> 8) & 0xFF), (unsigned)(a & 0xFF));
  return buf;
}

/// "AA:BB:CC:DD:EE:FF" to the number. false if it is not an address.
bool parseAddress(const std::string& s, uint64_t& a) {
  unsigned b[6];
  char tail;
  if (sscanf(s.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x%c", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5],
             &tail) != 6) {
    return false;
  }
  a = 0;
  for (unsigned v : b) a = (a << 8) | v;
  return true;
}

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (tolower(a[i]) != tolower(b[i])) return false;
  }
  return true;
}

std::vector<uint8_t> bytesOf(IBuffer const& buffer) {
  DataReader reader = DataReader::FromBuffer(buffer);
  std::vector<uint8_t> v(reader.UnconsumedBufferLength());
  if (!v.empty()) reader.ReadBytes(array_view<uint8_t>(v));
  return v;
}

IBuffer bufferOf(const uint8_t* data, size_t len) {
  DataWriter writer;
  if (len > 0) writer.WriteBytes(array_view<uint8_t const>(data, data + len));
  return writer.DetachBuffer();
}

// ---- TinyCTSCentral ----------------------------------------------------------------
class WinCentral : public TinyCTSCentral {
 public:
  WinCentral() { initApartment(); }
  ~WinCentral() override { disconnect(); }

  bool scan(uint32_t timeoutMs, std::vector<TinyCTSScanEntry>& result,
            std::string& error) override {
    try {
      collect(timeoutMs, [] { return false; });
      std::lock_guard<std::mutex> lock(_mutex);
      for (auto& kv : _seen) result.push_back(kv.second);
      return true;
    } catch (hresult_error const& e) {
      error = to_string(e.message());
      return false;
    }
  }

  bool connect(const std::string& wanted, uint32_t timeoutMs, TinyCTSScanEntry& found,
               std::string& error) override {
    disconnect();
    try {
      uint64_t address = 0;
      bool byAddress = parseAddress(wanted, address);
      bool ok = collect(timeoutMs, [&] { return pick(wanted, byAddress, address, found); });
      if (!ok) {
        error = wanted.empty() ? "no device with the Current Time Service found"
                               : "device " + wanted + " not found";
        return false;
      }
      parseAddress(found.address, address);
      _device = BluetoothLEDevice::FromBluetoothAddressAsync(address).get();
      if (!_device) {
        error = "cannot connect to " + found.address;
        return false;
      }
      auto services =
          _device.GetGattServicesForUuidAsync(ctsGuid(CTS_SERVICE_UUID), BluetoothCacheMode::Uncached)
              .get();
      if (services.Status() != GattCommunicationStatus::Success || services.Services().Size() == 0) {
        error = "the device has no Current Time Service";
        disconnect();
        return false;
      }
      _service = services.Services().GetAt(0);
      return true;
    } catch (hresult_error const& e) {
      error = "cannot connect: " + to_string(e.message());
      disconnect();
      return false;
    }
  }

  bool read(uint16_t uuid, std::vector<uint8_t>& value, std::string& error) override {
    try {
      GattCharacteristic c = characteristic(uuid, error);
      if (!c) return false;
      auto r = c.ReadValueAsync(BluetoothCacheMode::Uncached).get();
      if (r.Status() != GattCommunicationStatus::Success) {
        error = "read failed (status " + std::to_string((int)r.Status()) + ")";
        return false;
      }
      value = bytesOf(r.Value());
      return true;
    } catch (hresult_error const& e) {
      error = to_string(e.message());
      return false;
    }
  }

  bool write(uint16_t uuid, const uint8_t* data, size_t len, std::string& error) override {
    try {
      GattCharacteristic c = characteristic(uuid, error);
      if (!c) return false;
      auto r = c.WriteValueWithResultAsync(bufferOf(data, len), GattWriteOption::WriteWithResponse)
                   .get();
      if (r.Status() != GattCommunicationStatus::Success) {
        error = "write failed (status " + std::to_string((int)r.Status()) + ")";
        return false;
      }
      return true;
    } catch (hresult_error const& e) {
      error = to_string(e.message());
      return false;
    }
  }

  void disconnect() override {
    try {
      if (_service) _service.Close();
      if (_device) _device.Close();
    } catch (...) {
    }
    _service = nullptr;
    _device = nullptr;
  }

 private:
  GattCharacteristic characteristic(uint16_t uuid, std::string& error) {
    if (!_service) {
      error = "not connected";
      return nullptr;
    }
    auto r = _service.GetCharacteristicsForUuidAsync(ctsGuid(uuid), BluetoothCacheMode::Uncached).get();
    if (r.Status() != GattCommunicationStatus::Success || r.Characteristics().Size() == 0) {
      error = "the device does not have the characteristic";
      return nullptr;
    }
    return r.Characteristics().GetAt(0);
  }

  /// Scans until done() is true or the time is over.
  template <typename Done>
  bool collect(uint32_t timeoutMs, Done done) {
    {
      std::lock_guard<std::mutex> lock(_mutex);
      _seen.clear();
    }
    BluetoothLEAdvertisementWatcher watcher;
    watcher.ScanningMode(BluetoothLEScanningMode::Active);
    auto token = watcher.Received([this](auto&&, BluetoothLEAdvertisementReceivedEventArgs const& a) {
      TinyCTSScanEntry e;
      e.address = addressText(a.BluetoothAddress());
      e.name = to_string(a.Advertisement().LocalName());
      e.rssi = a.RawSignalStrengthInDBm();
      for (guid const& g : a.Advertisement().ServiceUuids()) {
        if (g == ctsGuid(CTS_SERVICE_UUID)) e.hasCts = true;
      }
      std::lock_guard<std::mutex> lock(_mutex);
      TinyCTSScanEntry& old = _seen[a.BluetoothAddress()];
      if (e.name.empty()) e.name = old.name;
      e.hasCts = e.hasCts || old.hasCts;
      old = e;
    });
    watcher.Start();
    auto start = std::chrono::steady_clock::now();
    bool ok = false;
    for (;;) {
      if (done()) {
        ok = true;
        break;
      }
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count();
      if (ms >= timeoutMs) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    watcher.Stop();
    watcher.Received(token);
    return ok;
  }

  bool pick(const std::string& wanted, bool byAddress, uint64_t address, TinyCTSScanEntry& found) {
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto& kv : _seen) {
      const TinyCTSScanEntry& e = kv.second;
      bool match = wanted.empty() ? e.hasCts
                                  : (byAddress ? kv.first == address
                                               : (e.name == wanted || equalsIgnoreCase(e.address, wanted)));
      if (match) {
        found = e;
        return true;
      }
    }
    return false;
  }

  std::mutex _mutex;
  std::map<uint64_t, TinyCTSScanEntry> _seen;
  BluetoothLEDevice _device{nullptr};
  GattDeviceService _service{nullptr};
};

// ---- TinyCTSPeripheral -------------------------------------------------------------
class WinPeripheral : public TinyCTSPeripheral {
 public:
  WinPeripheral() { initApartment(); }
  ~WinPeripheral() override { stop(); }

  bool start(const std::string&, bool settable, bool encrypted, TinyCTSPeripheralHandler* handler,
             std::string& error) override {
    try {
      _handler = handler;
      auto created = GattServiceProvider::CreateAsync(ctsGuid(CTS_SERVICE_UUID)).get();
      if (created.Error() != BluetoothError::Success) {
        error = "cannot create the service (error " + std::to_string((int)created.Error()) + ")";
        return false;
      }
      _provider = created.ServiceProvider();
      add(0, CTS_CURRENT_TIME_UUID, true, settable, encrypted);
      add(1, CTS_LOCAL_TIME_INFO_UUID, false, settable, encrypted);
      add(2, CTS_REFERENCE_TIME_INFO_UUID, false, false, encrypted);

      GattServiceProviderAdvertisingParameters params;
      params.IsConnectable(true);
      params.IsDiscoverable(true);
      _provider.StartAdvertising(params);
      return true;
    } catch (hresult_error const& e) {
      error = to_string(e.message());
      stop();
      return false;
    }
  }

  void process() override {}  // the requests are answered by a thread of the system

  void notify(uint16_t uuid, const std::vector<uint8_t>& value) override {
    if (uuid != CTS_CURRENT_TIME_UUID || _subscribers == 0 || !_chars[0]) return;
    try {
      _chars[0].NotifyValueAsync(bufferOf(value.data(), value.size()));  // not waited for
    } catch (...) {
    }
  }

  void stop() override {
    try {
      _handler = nullptr;  // the handlers of the events do nothing from now on
      if (_provider) _provider.StopAdvertising();
    } catch (...) {
    }
    for (int i = 0; i < 3; i++) {
      try {
        if (_chars[i]) {
          _chars[i].ReadRequested(_readToken[i]);
          _chars[i].WriteRequested(_writeToken[i]);
          if (i == 0) _chars[i].SubscribedClientsChanged(_subToken);
        }
      } catch (...) {
      }
      _chars[i] = nullptr;
    }
    _provider = nullptr;
  }

 private:
  void add(int index, uint16_t uuid, bool notify, bool writable, bool encrypted) {
    GattLocalCharacteristicParameters p;
    GattCharacteristicProperties props = GattCharacteristicProperties::Read;
    if (notify) props = props | GattCharacteristicProperties::Notify;
    if (writable) props = props | GattCharacteristicProperties::Write;
    p.CharacteristicProperties(props);
    p.ReadProtectionLevel(GattProtectionLevel::Plain);
    p.WriteProtectionLevel(encrypted ? GattProtectionLevel::EncryptionRequired
                                     : GattProtectionLevel::Plain);
    auto r = _provider.Service().CreateCharacteristicAsync(ctsGuid(uuid), p).get();
    if (r.Error() != BluetoothError::Success) {
      throw hresult_error(E_FAIL, L"cannot create the characteristic");
    }
    GattLocalCharacteristic c = r.Characteristic();
    _readToken[index] = c.ReadRequested(
        [this, uuid](GattLocalCharacteristic const&, GattReadRequestedEventArgs const& args) {
          auto deferral = args.GetDeferral();
          auto request = args.GetRequestAsync().get();
          std::vector<uint8_t> v;
          TinyCTSPeripheralHandler* h = _handler;
          if (h != nullptr && h->onRead(uuid, v)) {
            request.RespondWithValue(bufferOf(v.data(), v.size()));
          } else {
            request.RespondWithProtocolError(GattProtocolError::UnlikelyError());
          }
          deferral.Complete();
        });
    _writeToken[index] = c.WriteRequested(
        [this, uuid](GattLocalCharacteristic const&, GattWriteRequestedEventArgs const& args) {
          auto deferral = args.GetDeferral();
          auto request = args.GetRequestAsync().get();
          std::vector<uint8_t> v = bytesOf(request.Value());
          TinyCTSPeripheralHandler* h = _handler;
          bool ok = h != nullptr && h->onWrite(uuid, v.data(), v.size());
          if (request.Option() == GattWriteOption::WriteWithResponse) {
            if (ok) {
              request.Respond();
            } else {
              request.RespondWithProtocolError(GattProtocolError::WriteRequestRejected());
            }
          }
          deferral.Complete();
        });
    if (index == 0) {
      _subToken = c.SubscribedClientsChanged(
          [this](GattLocalCharacteristic const& s, Windows::Foundation::IInspectable const&) {
            _subscribers = static_cast<int>(s.SubscribedClients().Size());
          });
    }
    _chars[index] = c;
  }

  TinyCTSPeripheralHandler* _handler = nullptr;
  GattServiceProvider _provider{nullptr};
  GattLocalCharacteristic _chars[3] = {nullptr, nullptr, nullptr};
  event_token _readToken[3], _writeToken[3], _subToken;
  volatile int _subscribers = 0;
};

}  // namespace

namespace tiny_cts {

std::unique_ptr<TinyCTSCentral> ctsCreateCentral(const std::string&) {
  return std::unique_ptr<TinyCTSCentral>(new WinCentral());
}

std::unique_ptr<TinyCTSPeripheral> ctsCreatePeripheral(const std::string&) {
  return std::unique_ptr<TinyCTSPeripheral>(new WinPeripheral());
}

}  // namespace tiny_cts

#endif  // _WIN32 && !ARDUINO

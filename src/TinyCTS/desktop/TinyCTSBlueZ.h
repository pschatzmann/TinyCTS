/**
 * @file TinyCTSBlueZ.h
 * @brief The access to BlueZ, the Bluetooth stack of Linux, over D-Bus (GIO):
 * the functions that the BlueZ client and server share. Needs the development
 * files of GLib (Debian/Ubuntu: libglib2.0-dev).
 */

#pragma once
#include <gio/gio.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "../TinyCTSTypes.h"

namespace tiny_cts {
namespace bluez {

/// Print the D-Bus calls to stderr
inline bool& verbose() {
  static bool v = false;
  return v;
}

/// The 128 bit UUID of a 16 bit UUID
inline std::string fullUuid(uint16_t uuid16) {
  char buf[40];
  snprintf(buf, sizeof buf, "0000%04x-0000-1000-8000-00805f9b34fb", uuid16);
  return buf;
}

/// The connection to the system bus (nullptr and the message in error if not possible)
inline GDBusConnection* bus(std::string& error) {
  static GDBusConnection* conn = nullptr;
  if (conn == nullptr) {
    GError* err = nullptr;
    conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
    if (conn == nullptr) {
      error = std::string("cannot connect to the system bus: ") + err->message;
      g_error_free(err);
    }
  }
  return conn;
}

/// A synchronous call to BlueZ. Returns nullptr and the message in error.
inline GVariant* call(const std::string& path, const char* iface, const char* method,
                      GVariant* params, std::string& error, int timeoutMs = 20000) {
  GDBusConnection* conn = bus(error);
  if (conn == nullptr) {
    if (params != nullptr && g_variant_is_floating(params)) g_variant_unref(params);
    return nullptr;
  }
  GError* err = nullptr;
  GVariant* result = g_dbus_connection_call_sync(
      conn, "org.bluez", path.c_str(), iface, method, params, nullptr,
      G_DBUS_CALL_FLAGS_NONE, timeoutMs, nullptr, &err);
  if (result == nullptr) {
    g_dbus_error_strip_remote_error(err);
    error = err->message;
    if (verbose()) fprintf(stderr, "%s.%s failed: %s\n", iface, method, err->message);
    g_error_free(err);
  } else if (verbose()) {
    fprintf(stderr, "%s.%s ok\n", iface, method);
  }
  return result;
}

/// A device that BlueZ knows
struct Device {
  std::string path, address, name;
  bool cts = false;       ///< the UUIDs include the Current Time Service
  bool seen = false;      ///< RSSI known: in range during the discovery
  bool resolved = false;  ///< ServicesResolved
  bool connected = false;
  int rssi = 0;
};

/// The object path of the Bluetooth adapter (empty and the message in error if not found)
inline std::string findAdapter(const std::string& name, std::string& error) {
  GVariant* objs = call("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
                        nullptr, error);
  if (objs == nullptr) {
    error = "cannot talk to BlueZ: " + error + " (is the bluetooth service running?)";
    return "";
  }
  std::string wanted = "/org/bluez/" + name;
  std::string found;
  GVariantIter* it;
  g_variant_get(objs, "(a{oa{sa{sv}}})", &it);
  const char* path;
  GVariant* ifaces;
  while (g_variant_iter_next(it, "{&o@a{sa{sv}}}", &path, &ifaces)) {
    GVariant* a = g_variant_lookup_value(ifaces, "org.bluez.Adapter1", nullptr);
    if (a != nullptr) {
      if (wanted == path) found = path;
      g_variant_unref(a);
    }
    g_variant_unref(ifaces);
  }
  g_variant_iter_free(it);
  g_variant_unref(objs);
  if (found.empty()) error = "no Bluetooth adapter " + name + " found";
  return found;
}

inline std::vector<Device> listDevices(const std::string& adapter) {
  std::vector<Device> devices;
  std::string error;
  GVariant* objs = call("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
                        nullptr, error);
  if (objs == nullptr) return devices;
  std::string cts = fullUuid(CTS_SERVICE_UUID);
  GVariantIter* it;
  g_variant_get(objs, "(a{oa{sa{sv}}})", &it);
  const char* path;
  GVariant* ifaces;
  while (g_variant_iter_next(it, "{&o@a{sa{sv}}}", &path, &ifaces)) {
    GVariant* p = g_variant_lookup_value(ifaces, "org.bluez.Device1", G_VARIANT_TYPE("a{sv}"));
    if (p != nullptr && g_str_has_prefix(path, (adapter + "/").c_str())) {
      Device d;
      d.path = path;
      const char* s;
      if (g_variant_lookup(p, "Address", "&s", &s)) d.address = s;
      if (g_variant_lookup(p, "Alias", "&s", &s)) d.name = s;
      if (g_variant_lookup(p, "Name", "&s", &s)) d.name = s;
      gint16 rssi;
      if (g_variant_lookup(p, "RSSI", "n", &rssi)) {
        d.seen = true;
        d.rssi = rssi;
      }
      gboolean b;
      if (g_variant_lookup(p, "ServicesResolved", "b", &b)) d.resolved = b;
      if (g_variant_lookup(p, "Connected", "b", &b)) d.connected = b;
      GVariant* uuids = g_variant_lookup_value(p, "UUIDs", G_VARIANT_TYPE("as"));
      if (uuids != nullptr) {
        gsize n;
        const gchar** list = g_variant_get_strv(uuids, &n);
        for (gsize i = 0; i < n; i++) {
          if (cts == list[i]) d.cts = true;
        }
        g_free(list);
        g_variant_unref(uuids);
      }
      devices.push_back(d);
    }
    if (p != nullptr) g_variant_unref(p);
    g_variant_unref(ifaces);
  }
  g_variant_iter_free(it);
  g_variant_unref(objs);
  return devices;
}

/// The object path of a characteristic of a connected device (empty if not found)
inline std::string findCharacteristic(const std::string& devicePath, uint16_t uuid16) {
  std::string result, error;
  GVariant* objs = call("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
                        nullptr, error);
  if (objs == nullptr) return result;
  std::string uuid = fullUuid(uuid16);
  GVariantIter* it;
  g_variant_get(objs, "(a{oa{sa{sv}}})", &it);
  const char* path;
  GVariant* ifaces;
  while (g_variant_iter_next(it, "{&o@a{sa{sv}}}", &path, &ifaces)) {
    GVariant* p = g_variant_lookup_value(ifaces, "org.bluez.GattCharacteristic1",
                                         G_VARIANT_TYPE("a{sv}"));
    if (p != nullptr) {
      const char* s;
      if (g_str_has_prefix(path, (devicePath + "/").c_str()) &&
          g_variant_lookup(p, "UUID", "&s", &s) && uuid == s) {
        result = path;
      }
      g_variant_unref(p);
    }
    g_variant_unref(ifaces);
  }
  g_variant_iter_free(it);
  g_variant_unref(objs);
  return result;
}

inline bool readValue(const std::string& characteristic, std::vector<uint8_t>& out,
                      std::string& error) {
  GVariant* r = call(characteristic, "org.bluez.GattCharacteristic1", "ReadValue",
                     g_variant_new("(a{sv})", nullptr), error);
  if (r == nullptr) return false;
  GVariant* bytes = g_variant_get_child_value(r, 0);
  gsize n;
  const uint8_t* data = static_cast<const uint8_t*>(g_variant_get_fixed_array(bytes, &n, 1));
  out.assign(data, data + n);
  g_variant_unref(bytes);
  g_variant_unref(r);
  return true;
}

inline bool writeValue(const std::string& characteristic, const uint8_t* data, size_t len,
                       std::string& error) {
  GVariant* bytes = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, data, len, 1);
  GVariant* r = call(characteristic, "org.bluez.GattCharacteristic1", "WriteValue",
                     g_variant_new("(@aya{sv})", bytes, nullptr), error);
  if (r == nullptr) return false;
  g_variant_unref(r);
  return true;
}

inline bool equalsIgnoreCase(const std::string& a, const std::string& b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(),
                    [](char x, char y) { return tolower(x) == tolower(y); });
}

/// Look for devices. The scan stops when found(devices) returns true, or at the timeout.
template <typename Found>
inline std::vector<Device> discover(const std::string& adapter, uint32_t timeoutMs,
                                    Found found, std::string& error) {
  call(adapter, "org.bluez.Adapter1", "SetDiscoveryFilter",
       g_variant_new_parsed("({'Transport': <'le'>, 'DuplicateData': <false>},)"), error);
  bool started =
      call(adapter, "org.bluez.Adapter1", "StartDiscovery", nullptr, error) != nullptr;
  if (!started && error.find("InProgress") != std::string::npos) error.clear();
  std::vector<Device> devices;
  auto t0 = std::chrono::steady_clock::now();
  for (;;) {
    devices = listDevices(adapter);
    if (found(devices)) break;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
    if (ms >= timeoutMs) break;
    g_usleep(250 * 1000);
  }
  std::string ignored;
  if (started) call(adapter, "org.bluez.Adapter1", "StopDiscovery", nullptr, ignored);
  return devices;
}

}  // namespace bluez
}  // namespace tiny_cts

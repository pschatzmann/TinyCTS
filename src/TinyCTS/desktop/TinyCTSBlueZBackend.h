/**
 * @file TinyCTSBlueZBackend.h
 * @brief TinyCTSCentral and TinyCTSPeripheral for Linux: BlueZ over D-Bus (GIO).
 */

#pragma once
#include <cstring>

#include "TinyCTSBlueZ.h"
#include "TinyCTSDesktop.h"

namespace tiny_cts {

/**
 * @class TinyCTSBlueZCentral
 * @brief The client side for Linux: scans, connects and reads or writes the
 * characteristics through BlueZ (org.bluez.Adapter1, Device1 and
 * GattCharacteristic1) with the functions of TinyCTSBlueZ.
 */
class TinyCTSBlueZCentral : public TinyCTSCentral {
 public:
  explicit TinyCTSBlueZCentral(const std::string& adapter) : _adapterName(adapter) {}
  ~TinyCTSBlueZCentral() override { disconnect(); }

  bool scan(uint32_t timeoutMs, std::vector<TinyCTSScanEntry>& result,
            std::string& error) override {
    if (!open(error)) return false;
    std::vector<TinyCTSBlueZ::Device> devices = TinyCTSBlueZ::discover(
        _adapter, timeoutMs, [](const std::vector<TinyCTSBlueZ::Device>&) { return false; }, error);
    for (const TinyCTSBlueZ::Device& d : devices) {
      if (d.seen) result.push_back(entry(d));
    }
    return true;
  }

  bool connect(const std::string& wanted, uint32_t timeoutMs, TinyCTSScanEntry& found,
               std::string& error) override {
    disconnect();
    if (!open(error)) return false;
    std::vector<TinyCTSBlueZ::Device> devices = TinyCTSBlueZ::discover(
        _adapter, timeoutMs,
        [&](const std::vector<TinyCTSBlueZ::Device>& l) { return pick(l, wanted) != nullptr; }, error);
    const TinyCTSBlueZ::Device* d = pick(devices, wanted);
    if (d == nullptr) {
      error = wanted.empty() ? "no device with the Current Time Service found"
                             : "device " + wanted + " not found";
      return false;
    }
    _device = *d;
    if (!_device.connected &&
        TinyCTSBlueZ::call(_device.path, "org.bluez.Device1", "Connect", nullptr, error, 30000) ==
            nullptr) {
      error = "cannot connect to " + _device.address + ": " + error;
      _device = TinyCTSBlueZ::Device();
      return false;
    }
    for (int i = 0; i < 100; i++) {  // up to 10 s
      for (const TinyCTSBlueZ::Device& l : TinyCTSBlueZ::listDevices(_adapter)) {
        if (l.path == _device.path && l.resolved) {
          found = entry(_device);
          return true;
        }
      }
      g_usleep(100 * 1000);
    }
    error = "no services from " + _device.address;
    disconnect();
    return false;
  }

  bool read(uint16_t uuid, std::vector<uint8_t>& value, std::string& error) override {
    std::string path = TinyCTSBlueZ::findCharacteristic(_device.path, uuid);
    if (path.empty()) {
      error = "the device does not have the characteristic";
      return false;
    }
    return TinyCTSBlueZ::readValue(path, value, error);
  }

  bool write(uint16_t uuid, const uint8_t* data, size_t len, std::string& error) override {
    std::string path = TinyCTSBlueZ::findCharacteristic(_device.path, uuid);
    if (path.empty()) {
      error = "the device does not have the characteristic";
      return false;
    }
    return TinyCTSBlueZ::writeValue(path, data, len, error);
  }

  void disconnect() override {
    if (_device.path.empty()) return;
    std::string ignored;
    TinyCTSBlueZ::call(_device.path, "org.bluez.Device1", "Disconnect", nullptr, ignored);
    _device = TinyCTSBlueZ::Device();
  }

 private:
  bool open(std::string& error) {
    error.clear();
    _adapter = TinyCTSBlueZ::findAdapter(_adapterName, error);
    return !_adapter.empty();
  }

  static TinyCTSScanEntry entry(const TinyCTSBlueZ::Device& d) {
    TinyCTSScanEntry e;
    e.address = d.address;
    e.name = d.name;
    e.rssi = d.rssi;
    e.hasCts = d.cts;
    return e;
  }

  static const TinyCTSBlueZ::Device* pick(const std::vector<TinyCTSBlueZ::Device>& devices,
                                   const std::string& wanted) {
    for (const TinyCTSBlueZ::Device& d : devices) {
      if (wanted.empty()) {
        if (d.cts && d.seen) return &d;
      } else if (TinyCTSBlueZ::equalsIgnoreCase(d.address, wanted) || d.name == wanted) {
        if (d.seen || d.connected) return &d;
      }
    }
    return nullptr;
  }

  std::string _adapterName, _adapter;
  TinyCTSBlueZ::Device _device;
};

/**
 * @class TinyCTSBlueZPeripheral
 * @brief The server side for Linux: exports the Current Time Service as a GATT
 * application and an advertisement on D-Bus and registers them at BlueZ
 * (GattManager1 and LEAdvertisingManager1). BlueZ calls back over D-Bus, so
 * process() has to run the main context of GLib.
 */
class TinyCTSBlueZPeripheral : public TinyCTSPeripheral {
 public:
  explicit TinyCTSBlueZPeripheral(const std::string& adapter) : _adapterName(adapter) {}
  ~TinyCTSBlueZPeripheral() override { stop(); }

  bool start(const std::string& name, bool settable, bool encrypted,
             TinyCTSPeripheralHandler* handler, std::string& error) override {
    _handler = handler;
    _name = name;
    _settable = settable;
    _encrypted = encrypted;
    _error.clear();
    _adapter = TinyCTSBlueZ::findAdapter(_adapterName, _error);
    GDBusConnection* conn = _adapter.empty() ? nullptr : TinyCTSBlueZ::bus(_error);
    if (conn == nullptr) {
      error = _error;
      return false;
    }

    _nodeInfo = g_dbus_node_info_new_for_xml(xml(), nullptr);
    _chars[0] = {std::string(Service) + "/char0", CTS_CURRENT_TIME_UUID, true};
    _chars[1] = {std::string(Service) + "/char1", CTS_LOCAL_TIME_INFO_UUID, false};
    _chars[2] = {std::string(Service) + "/char2", CTS_REFERENCE_TIME_INFO_UUID, false};
    if (!exportObject(App, "org.freedesktop.DBus.ObjectManager") ||
        !exportObject(Service, "org.bluez.GattService1") ||
        !exportObject(_chars[0].path.c_str(), "org.bluez.GattCharacteristic1") ||
        !exportObject(_chars[1].path.c_str(), "org.bluez.GattCharacteristic1") ||
        !exportObject(_chars[2].path.c_str(), "org.bluez.GattCharacteristic1") ||
        !exportObject(Advertisement, "org.bluez.LEAdvertisement1")) {
      error = _error;
      stop();
      return false;
    }
    _started = true;

    // BlueZ calls back while it registers: the main loop has to run
    _pending = 2;
    g_dbus_connection_call(conn, "org.bluez", _adapter.c_str(), "org.bluez.GattManager1",
                           "RegisterApplication", g_variant_new_parsed("(%o, @a{sv} {})", App),
                           nullptr, G_DBUS_CALL_FLAGS_NONE, 20000, nullptr, onRegistered, this);
    g_dbus_connection_call(conn, "org.bluez", _adapter.c_str(),
                           "org.bluez.LEAdvertisingManager1", "RegisterAdvertisement",
                           g_variant_new_parsed("(%o, @a{sv} {})", Advertisement), nullptr,
                           G_DBUS_CALL_FLAGS_NONE, 20000, nullptr, onRegistered, this);
    for (int i = 0; i < 4000 && _pending > 0; i++) {  // up to 20 s
      process();
      g_usleep(5 * 1000);
    }
    if (_pending > 0 && _error.empty()) _error = "BlueZ does not answer";
    if (!_error.empty()) {
      error = _error;
      stop();
      return false;
    }
    _registered = true;
    return true;
  }

  void process() override {
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
  }

  void notify(uint16_t uuid, const std::vector<uint8_t>& value) override {
    if (!_notifying) return;
    for (const Chr& c : _chars) {
      if (c.uuid == uuid) emitValue(c, value);
    }
  }

  void stop() override {
    std::string ignored;
    if (_registered) {
      TinyCTSBlueZ::call(_adapter, "org.bluez.GattManager1", "UnregisterApplication",
                  g_variant_new("(o)", App), ignored, 2000);
      TinyCTSBlueZ::call(_adapter, "org.bluez.LEAdvertisingManager1", "UnregisterAdvertisement",
                  g_variant_new("(o)", Advertisement), ignored, 2000);
      _registered = false;
    }
    GDBusConnection* conn = TinyCTSBlueZ::bus(ignored);
    for (guint id : _ids) {
      if (conn != nullptr) g_dbus_connection_unregister_object(conn, id);
    }
    _ids.clear();
    if (_nodeInfo != nullptr) g_dbus_node_info_unref(_nodeInfo);
    _nodeInfo = nullptr;
    _started = false;
    _notifying = false;
  }

 private:
  static constexpr const char* App = "/org/tinycts";
  static constexpr const char* Service = "/org/tinycts/service0";
  static constexpr const char* Advertisement = "/org/tinycts/adv0";

  /// A characteristic of the exported service: its object path and UUID
  struct Chr {
    std::string path;
    uint16_t uuid = 0;
    bool current = false;
  };

  static const char* xml() {
    return "<node>"
           " <interface name='org.freedesktop.DBus.ObjectManager'>"
           "  <method name='GetManagedObjects'>"
           "   <arg type='a{oa{sa{sv}}}' name='objects' direction='out'/>"
           "  </method>"
           " </interface>"
           " <interface name='org.bluez.GattService1'>"
           "  <property name='UUID' type='s' access='read'/>"
           "  <property name='Primary' type='b' access='read'/>"
           " </interface>"
           " <interface name='org.bluez.GattCharacteristic1'>"
           "  <method name='ReadValue'>"
           "   <arg type='a{sv}' name='options' direction='in'/>"
           "   <arg type='ay' name='value' direction='out'/>"
           "  </method>"
           "  <method name='WriteValue'>"
           "   <arg type='ay' name='value' direction='in'/>"
           "   <arg type='a{sv}' name='options' direction='in'/>"
           "  </method>"
           "  <method name='StartNotify'/>"
           "  <method name='StopNotify'/>"
           "  <property name='UUID' type='s' access='read'/>"
           "  <property name='Service' type='o' access='read'/>"
           "  <property name='Flags' type='as' access='read'/>"
           "  <property name='Notifying' type='b' access='read'/>"
           " </interface>"
           " <interface name='org.bluez.LEAdvertisement1'>"
           "  <method name='Release'/>"
           "  <property name='Type' type='s' access='read'/>"
           "  <property name='ServiceUUIDs' type='as' access='read'/>"
           "  <property name='LocalName' type='s' access='read'/>"
           " </interface>"
           "</node>";
  }

  bool exportObject(const char* path, const char* iface) {
    static const GDBusInterfaceVTable vtable = {onCall, onGet, nullptr, {}};
    GError* err = nullptr;
    GDBusConnection* conn = TinyCTSBlueZ::bus(_error);
    GDBusInterfaceInfo* info = g_dbus_node_info_lookup_interface(_nodeInfo, iface);
    guint id = conn == nullptr ? 0
                               : g_dbus_connection_register_object(conn, path, info, &vtable,
                                                                   this, nullptr, &err);
    if (id == 0) {
      if (err != nullptr) {
        _error = std::string("cannot export ") + path + ": " + err->message;
        g_error_free(err);
      }
      return false;
    }
    _ids.push_back(id);
    return true;
  }

  static void onRegistered(GObject* source, GAsyncResult* res, gpointer self) {
    TinyCTSBlueZPeripheral* s = static_cast<TinyCTSBlueZPeripheral*>(self);
    GError* err = nullptr;
    GVariant* r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);
    s->_pending--;
    if (r == nullptr) {
      g_dbus_error_strip_remote_error(err);
      s->_error = std::string("BlueZ refused the registration: ") + err->message;
      g_error_free(err);
      return;
    }
    g_variant_unref(r);
  }

  const Chr* find(const char* path) const {
    for (const Chr& c : _chars) {
      if (c.path == path) return &c;
    }
    return nullptr;
  }

  static GVariant* bytes(const std::vector<uint8_t>& v) {
    return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, v.data(), v.size(), 1);
  }

  GVariant* flags(const Chr& c) const {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&b, "s", "read");
    if (c.current) g_variant_builder_add(&b, "s", "notify");
    if (_settable && c.uuid != CTS_REFERENCE_TIME_INFO_UUID) {
      g_variant_builder_add(&b, "s", _encrypted ? "encrypt-write" : "write");
    }
    return g_variant_builder_end(&b);
  }

  GVariant* managed() {
    GVariantBuilder objects;
    g_variant_builder_init(&objects, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
    GVariant* service = g_variant_new_parsed(
        "{'org.bluez.GattService1': {'UUID': <%s>, 'Primary': <true>}}",
        TinyCTSBlueZ::fullUuid(CTS_SERVICE_UUID).c_str());
    g_variant_builder_add(&objects, "{o@a{sa{sv}}}", Service, service);
    for (const Chr& c : _chars) {
      GVariantBuilder props;
      g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&props, "{sv}", "UUID",
                            g_variant_new_string(TinyCTSBlueZ::fullUuid(c.uuid).c_str()));
      g_variant_builder_add(&props, "{sv}", "Service", g_variant_new_object_path(Service));
      g_variant_builder_add(&props, "{sv}", "Flags", flags(c));
      GVariantBuilder ifaces;
      g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
      g_variant_builder_add(&ifaces, "{sa{sv}}", "org.bluez.GattCharacteristic1", &props);
      g_variant_builder_add(&objects, "{oa{sa{sv}}}", c.path.c_str(), &ifaces);
    }
    return g_variant_new("(a{oa{sa{sv}}})", &objects);
  }

  void emitValue(const Chr& c, const std::vector<uint8_t>& value) {
    std::string ignored;
    GDBusConnection* conn = TinyCTSBlueZ::bus(ignored);
    if (conn == nullptr) return;
    GVariantBuilder changed;
    g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&changed, "{sv}", "Value", bytes(value));
    g_dbus_connection_emit_signal(
        conn, nullptr, c.path.c_str(), "org.freedesktop.DBus.Properties", "PropertiesChanged",
        g_variant_new("(sa{sv}as)", "org.bluez.GattCharacteristic1", &changed, nullptr),
        nullptr);
  }

  static void onCall(GDBusConnection*, const gchar*, const gchar* path, const gchar*,
                     const gchar* method, GVariant* params, GDBusMethodInvocation* inv,
                     gpointer self) {
    TinyCTSBlueZPeripheral* s = static_cast<TinyCTSBlueZPeripheral*>(self);
    if (!strcmp(method, "GetManagedObjects")) {
      g_dbus_method_invocation_return_value(inv, s->managed());
    } else if (!strcmp(method, "ReadValue")) {
      const Chr* c = s->find(path);
      std::vector<uint8_t> value;
      if (c == nullptr || s->_handler == nullptr || !s->_handler->onRead(c->uuid, value)) {
        g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.Failed", "unknown");
        return;
      }
      g_dbus_method_invocation_return_value(inv, g_variant_new("(@ay)", bytes(value)));
    } else if (!strcmp(method, "WriteValue")) {
      const Chr* c = s->find(path);
      GVariant* data;
      GVariant* options;
      g_variant_get(params, "(@ay@a{sv})", &data, &options);
      gsize n;
      const uint8_t* d = static_cast<const uint8_t*>(g_variant_get_fixed_array(data, &n, 1));
      bool ok = c != nullptr && s->_handler != nullptr && s->_handler->onWrite(c->uuid, d, n);
      g_variant_unref(data);
      g_variant_unref(options);
      if (ok) {
        g_dbus_method_invocation_return_value(inv, nullptr);
      } else {
        g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.NotPermitted",
                                                   "the time cannot be set");
      }
    } else if (!strcmp(method, "StartNotify")) {
      s->_notifying = true;
      g_dbus_method_invocation_return_value(inv, nullptr);
    } else if (!strcmp(method, "StopNotify")) {
      s->_notifying = false;
      g_dbus_method_invocation_return_value(inv, nullptr);
    } else {  // Release
      g_dbus_method_invocation_return_value(inv, nullptr);
    }
  }

  static GVariant* onGet(GDBusConnection*, const gchar*, const gchar* path, const gchar* iface,
                         const gchar* prop, GError**, gpointer self) {
    TinyCTSBlueZPeripheral* s = static_cast<TinyCTSBlueZPeripheral*>(self);
    if (!strcmp(iface, "org.bluez.GattService1")) {
      if (!strcmp(prop, "UUID")) {
        return g_variant_new_string(TinyCTSBlueZ::fullUuid(CTS_SERVICE_UUID).c_str());
      }
      return g_variant_new_boolean(TRUE);
    }
    if (!strcmp(iface, "org.bluez.LEAdvertisement1")) {
      if (!strcmp(prop, "Type")) return g_variant_new_string("peripheral");
      if (!strcmp(prop, "LocalName")) return g_variant_new_string(s->_name.c_str());
      std::string u = TinyCTSBlueZ::fullUuid(CTS_SERVICE_UUID);
      const gchar* uuids[] = {u.c_str(), nullptr};
      return g_variant_new_strv(uuids, 1);
    }
    const Chr* c = s->find(path);
    if (c == nullptr) return nullptr;
    if (!strcmp(prop, "UUID")) return g_variant_new_string(TinyCTSBlueZ::fullUuid(c->uuid).c_str());
    if (!strcmp(prop, "Service")) return g_variant_new_object_path(Service);
    if (!strcmp(prop, "Notifying")) return g_variant_new_boolean(s->_notifying);
    return s->flags(*c);
  }

  std::string _adapterName, _adapter, _name, _error;
  TinyCTSPeripheralHandler* _handler = nullptr;
  GDBusNodeInfo* _nodeInfo = nullptr;
  std::vector<guint> _ids;
  Chr _chars[3];
  int _pending = 0;
  bool _settable = false, _encrypted = true;
  bool _started = false, _registered = false, _notifying = false;
};

inline std::unique_ptr<TinyCTSCentral> ctsCreateCentral(const std::string& adapter) {
  return std::unique_ptr<TinyCTSCentral>(new TinyCTSBlueZCentral(adapter));
}

inline std::unique_ptr<TinyCTSPeripheral> ctsCreatePeripheral(const std::string& adapter) {
  return std::unique_ptr<TinyCTSPeripheral>(new TinyCTSBlueZPeripheral(adapter));
}

}  // namespace tiny_cts

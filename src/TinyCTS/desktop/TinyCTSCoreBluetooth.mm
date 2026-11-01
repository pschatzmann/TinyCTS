// TinyCTSCentral and TinyCTSPeripheral for macOS: CoreBluetooth.
//
// Compile with ARC (-fobjc-arc) and link the frameworks CoreBluetooth and
// Foundation: the CMake target TinyCTS does that. UNTESTED: this file could not
// be compiled or run when it was written.
//
// CoreBluetooth calls the delegates on a queue of its own, so that the
// functions can block while the calling thread waits for a flag. macOS asks the
// user for the permission to use Bluetooth: a program that is not an app bundle
// needs an Info.plist with NSBluetoothAlwaysUsageDescription (embedded with the
// linker, see tools/tinycts/CMakeLists.txt) or it is killed.
#if defined(__APPLE__) && !defined(ARDUINO)

#if !__has_feature(objc_arc)
#error "TinyCTSCoreBluetooth.mm must be compiled with -fobjc-arc"
#endif

#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>

#include <chrono>
#include <cstdlib>

#include "TinyCTSDesktop.h"
#include "../TinyCTSTypes.h"

using namespace tiny_cts;

static CBUUID* ctsUuid(uint16_t uuid16) {
  return [CBUUID UUIDWithString:[NSString stringWithFormat:@"%04X", uuid16]];
}

static uint16_t shortUuid(CBUUID* uuid) {
  return static_cast<uint16_t>(strtol([uuid.UUIDString UTF8String], nullptr, 16));
}

static std::string toString(NSString* s) { return s == nil ? std::string() : std::string([s UTF8String]); }

/// Waits until the condition is true. The delegates run on another thread.
template <typename Cond>
static bool waitFor(uint32_t timeoutMs, Cond cond) {
  auto start = std::chrono::steady_clock::now();
  while (!cond()) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - start)
                  .count();
    if (ms >= timeoutMs) return false;
    [NSThread sleepForTimeInterval:0.01];
  }
  return true;
}

static std::string stateError(CBManagerState state) {
  switch (state) {
    case CBManagerStateUnauthorized:
      return "Bluetooth access denied: allow it for this program in System Settings > Privacy & Security > Bluetooth";
    case CBManagerStatePoweredOff:
      return "Bluetooth is switched off";
    case CBManagerStateUnsupported:
      return "this computer does not support Bluetooth Low Energy";
    default:
      return "Bluetooth is not available";
  }
}

// ---- Central ----------------------------------------------------------------
/// The delegate of CBCentralManager and CBPeripheral: collects the devices that
/// are found, connects to one and keeps the characteristics of its Current
/// Time Service. The delegate methods run on a dispatch queue of their own.
@interface TinyCTSCBCentral : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
@property(strong) dispatch_queue_t queue;
@property(strong) CBCentralManager* manager;
@property(strong) NSMutableDictionary<NSString*, NSDictionary*>* seen;
@property(strong) CBPeripheral* peripheral;
@property(strong) NSMutableDictionary<NSNumber*, CBCharacteristic*>* chars;
@property(strong) NSData* value;
@property(strong) NSString* failure;
@property CBManagerState state;
@property BOOL ready;
@property BOOL operationDone;
@property BOOL disconnected;
@end

@implementation TinyCTSCBCentral
- (instancetype)init {
  self = [super init];
  if (self) {
    _queue = dispatch_queue_create("tinycts.central", DISPATCH_QUEUE_SERIAL);
    _seen = [NSMutableDictionary new];
    _chars = [NSMutableDictionary new];
    _state = CBManagerStateUnknown;
    _manager = [[CBCentralManager alloc] initWithDelegate:self queue:_queue];
  }
  return self;
}

- (void)centralManagerDidUpdateState:(CBCentralManager*)central {
  self.state = central.state;
}

- (void)centralManager:(CBCentralManager*)central
    didDiscoverPeripheral:(CBPeripheral*)peripheral
        advertisementData:(NSDictionary<NSString*, id>*)adv
                     RSSI:(NSNumber*)RSSI {
  NSString* name = adv[CBAdvertisementDataLocalNameKey];
  if (name == nil) name = peripheral.name;
  NSArray* uuids = adv[CBAdvertisementDataServiceUUIDsKey];
  BOOL cts = [uuids containsObject:ctsUuid(CTS_SERVICE_UUID)];
  @synchronized(self.seen) {
    NSDictionary* old = self.seen[peripheral.identifier.UUIDString];
    if (name == nil) name = old[@"name"];
    if (old != nil && [old[@"cts"] boolValue]) cts = YES;
    self.seen[peripheral.identifier.UUIDString] =
        @{@"p" : peripheral, @"name" : name ?: @"", @"rssi" : RSSI, @"cts" : @(cts)};
  }
}

- (void)centralManager:(CBCentralManager*)central didConnectPeripheral:(CBPeripheral*)peripheral {
  peripheral.delegate = self;
  [peripheral discoverServices:@[ ctsUuid(CTS_SERVICE_UUID) ]];
}

- (void)centralManager:(CBCentralManager*)central
    didFailToConnectPeripheral:(CBPeripheral*)peripheral
                         error:(NSError*)error {
  self.failure = error.localizedDescription ?: @"cannot connect";
}

- (void)centralManager:(CBCentralManager*)central
    didDisconnectPeripheral:(CBPeripheral*)peripheral
                      error:(NSError*)error {
  self.disconnected = YES;
}

- (void)peripheral:(CBPeripheral*)peripheral didDiscoverServices:(NSError*)error {
  for (CBService* s in peripheral.services) {
    if ([s.UUID isEqual:ctsUuid(CTS_SERVICE_UUID)]) {
      [peripheral discoverCharacteristics:nil forService:s];
      return;
    }
  }
  self.failure = error.localizedDescription ?: @"the device has no Current Time Service";
}

- (void)peripheral:(CBPeripheral*)peripheral
    didDiscoverCharacteristicsForService:(CBService*)service
                                   error:(NSError*)error {
  for (CBCharacteristic* c in service.characteristics) {
    self.chars[@(shortUuid(c.UUID))] = c;
  }
  if (error != nil) self.failure = error.localizedDescription;
  self.ready = YES;
}

- (void)peripheral:(CBPeripheral*)peripheral
    didUpdateValueForCharacteristic:(CBCharacteristic*)characteristic
                              error:(NSError*)error {
  self.value = characteristic.value;
  self.failure = error.localizedDescription;
  self.operationDone = YES;
}

- (void)peripheral:(CBPeripheral*)peripheral
    didWriteValueForCharacteristic:(CBCharacteristic*)characteristic
                             error:(NSError*)error {
  self.failure = error.localizedDescription;
  self.operationDone = YES;
}
@end

namespace {

/// The client side for macOS: TinyCTSCentral on top of TinyCTSCBCentral. The functions
/// block until the delegate has set the flag that they wait for.
class CoreBluetoothCentral : public TinyCTSCentral {
 public:
  CoreBluetoothCentral() { _c = [TinyCTSCBCentral new]; }
  ~CoreBluetoothCentral() override { disconnect(); }

  bool scan(uint32_t timeoutMs, std::vector<TinyCTSScanEntry>& result,
            std::string& error) override {
    if (!powered(error)) return false;
    [_c.manager scanForPeripheralsWithServices:nil
                                       options:@{CBCentralManagerScanOptionAllowDuplicatesKey : @NO}];
    waitFor(timeoutMs, [] { return false; });
    [_c.manager stopScan];
    @synchronized(_c.seen) {
      for (NSString* key in _c.seen) result.push_back(entry(key, _c.seen[key]));
    }
    return true;
  }

  bool connect(const std::string& wanted, uint32_t timeoutMs, TinyCTSScanEntry& found,
               std::string& error) override {
    disconnect();
    if (!powered(error)) return false;
    [_c.seen removeAllObjects];
    [_c.manager scanForPeripheralsWithServices:nil
                                       options:@{CBCentralManagerScanOptionAllowDuplicatesKey : @NO}];
    NSString* key = nil;
    waitFor(timeoutMs, [&] {
      key = pick(wanted);
      return key != nil;
    });
    [_c.manager stopScan];
    if (key == nil) {
      error = wanted.empty() ? "no device with the Current Time Service found"
                             : "device " + wanted + " not found";
      return false;
    }
    NSDictionary* d;
    @synchronized(_c.seen) {
      d = _c.seen[key];
    }
    _c.failure = nil;
    _c.ready = NO;
    _c.disconnected = NO;
    [_c.chars removeAllObjects];
    _c.peripheral = d[@"p"];
    [_c.manager connectPeripheral:_c.peripheral options:nil];
    if (!waitFor(30000, [&] { return _c.ready || _c.failure != nil; }) || _c.failure != nil) {
      error = _c.failure != nil ? "cannot connect: " + toString(_c.failure) : "connection timeout";
      disconnect();
      return false;
    }
    found = entry(key, d);
    return true;
  }

  bool read(uint16_t uuid, std::vector<uint8_t>& value, std::string& error) override {
    CBCharacteristic* c = _c.chars[@(uuid)];
    if (c == nil) {
      error = "the device does not have the characteristic";
      return false;
    }
    _c.operationDone = NO;
    _c.failure = nil;
    _c.value = nil;
    [_c.peripheral readValueForCharacteristic:c];
    if (!waitFor(10000, [&] { return _c.operationDone; })) {
      error = "timeout";
      return false;
    }
    if (_c.failure != nil || _c.value == nil) {
      error = _c.failure != nil ? toString(_c.failure) : "no value";
      return false;
    }
    const uint8_t* p = static_cast<const uint8_t*>(_c.value.bytes);
    value.assign(p, p + _c.value.length);
    return true;
  }

  bool write(uint16_t uuid, const uint8_t* data, size_t len, std::string& error) override {
    CBCharacteristic* c = _c.chars[@(uuid)];
    if (c == nil) {
      error = "the device does not have the characteristic";
      return false;
    }
    if (!(c.properties & CBCharacteristicPropertyWrite)) {
      error = "the characteristic cannot be written";
      return false;
    }
    _c.operationDone = NO;
    _c.failure = nil;
    [_c.peripheral writeValue:[NSData dataWithBytes:data length:len]
            forCharacteristic:c
                         type:CBCharacteristicWriteWithResponse];
    if (!waitFor(10000, [&] { return _c.operationDone; })) {
      error = "timeout";
      return false;
    }
    if (_c.failure != nil) {
      error = toString(_c.failure);
      return false;
    }
    return true;
  }

  void disconnect() override {
    if (_c.peripheral == nil) return;
    _c.disconnected = NO;
    [_c.manager cancelPeripheralConnection:_c.peripheral];
    waitFor(2000, [&] { return _c.disconnected; });
    _c.peripheral = nil;
    [_c.chars removeAllObjects];
  }

 private:
  bool powered(std::string& error) {
    waitFor(5000, [&] { return _c.state != CBManagerStateUnknown; });
    if (_c.state != CBManagerStatePoweredOn) {
      error = stateError(_c.state);
      return false;
    }
    return true;
  }

  static TinyCTSScanEntry entry(NSString* key, NSDictionary* d) {
    TinyCTSScanEntry e;
    e.address = toString(key);
    e.name = toString(d[@"name"]);
    e.rssi = [d[@"rssi"] intValue];
    e.hasCts = [d[@"cts"] boolValue];
    return e;
  }

  NSString* pick(const std::string& wanted) {
    @synchronized(_c.seen) {
      for (NSString* key in _c.seen) {
        NSDictionary* d = _c.seen[key];
        if (wanted.empty()) {
          if ([d[@"cts"] boolValue]) return key;
        } else if ([key caseInsensitiveCompare:[NSString stringWithUTF8String:wanted.c_str()]] ==
                       NSOrderedSame ||
                   toString(d[@"name"]) == wanted) {
          return key;
        }
      }
    }
    return nil;
  }

  TinyCTSCBCentral* _c;
};

}  // namespace

// ---- Peripheral -------------------------------------------------------------
/// The delegate of CBPeripheralManager: adds the service, advertises it and
/// answers the read and write requests with the TinyCTSPeripheralHandler. The
/// delegate methods run on a dispatch queue of their own.
@interface TinyCTSCBPeripheral : NSObject <CBPeripheralManagerDelegate>
{
 @public
  TinyCTSPeripheralHandler* handler;
}
@property(strong) dispatch_queue_t queue;
@property(strong) CBPeripheralManager* manager;
@property(strong) NSMutableDictionary<NSNumber*, CBMutableCharacteristic*>* chars;
@property(strong) NSString* failure;
@property CBManagerState state;
@property BOOL serviceAdded;
@property BOOL advertising;
@property int subscribers;
@end

@implementation TinyCTSCBPeripheral
- (instancetype)init {
  self = [super init];
  if (self) {
    _queue = dispatch_queue_create("tinycts.peripheral", DISPATCH_QUEUE_SERIAL);
    _chars = [NSMutableDictionary new];
    _state = CBManagerStateUnknown;
    _manager = [[CBPeripheralManager alloc] initWithDelegate:self queue:_queue];
  }
  return self;
}

- (void)peripheralManagerDidUpdateState:(CBPeripheralManager*)peripheral {
  self.state = peripheral.state;
}

- (void)peripheralManager:(CBPeripheralManager*)peripheral
            didAddService:(CBService*)service
                    error:(NSError*)error {
  self.failure = error.localizedDescription;
  self.serviceAdded = YES;
}

- (void)peripheralManagerDidStartAdvertising:(CBPeripheralManager*)peripheral
                                       error:(NSError*)error {
  self.failure = error.localizedDescription;
  self.advertising = YES;
}

- (void)peripheralManager:(CBPeripheralManager*)peripheral
                         central:(CBCentral*)central
    didSubscribeToCharacteristic:(CBCharacteristic*)characteristic {
  self.subscribers++;
}

- (void)peripheralManager:(CBPeripheralManager*)peripheral
                             central:(CBCentral*)central
    didUnsubscribeFromCharacteristic:(CBCharacteristic*)characteristic {
  if (self.subscribers > 0) self.subscribers--;
}

- (void)peripheralManager:(CBPeripheralManager*)peripheral didReceiveReadRequest:(CBATTRequest*)request {
  std::vector<uint8_t> value;
  uint16_t uuid = shortUuid(request.characteristic.UUID);
  if (handler == nullptr || !handler->onRead(uuid, value)) {
    [peripheral respondToRequest:request withResult:CBATTErrorUnlikelyError];
  } else if (request.offset > value.size()) {
    [peripheral respondToRequest:request withResult:CBATTErrorInvalidOffset];
  } else {
    request.value = [NSData dataWithBytes:value.data() + request.offset
                                   length:value.size() - request.offset];
    [peripheral respondToRequest:request withResult:CBATTErrorSuccess];
  }
}

- (void)peripheralManager:(CBPeripheralManager*)peripheral
  didReceiveWriteRequests:(NSArray<CBATTRequest*>*)requests {
  CBATTError result = CBATTErrorSuccess;
  for (CBATTRequest* r in requests) {
    uint16_t uuid = shortUuid(r.characteristic.UUID);
    const uint8_t* p = static_cast<const uint8_t*>(r.value.bytes);
    if (handler == nullptr || !handler->onWrite(uuid, p, r.value.length)) {
      result = CBATTErrorWriteNotPermitted;
    }
  }
  [peripheral respondToRequest:requests.firstObject withResult:result];
}
@end

namespace {

/// The server side for macOS: TinyCTSPeripheral on top of TinyCTSCBPeripheral.
class CoreBluetoothPeripheral : public TinyCTSPeripheral {
 public:
  CoreBluetoothPeripheral() { _p = [TinyCTSCBPeripheral new]; }
  ~CoreBluetoothPeripheral() override { stop(); }

  bool start(const std::string& name, bool settable, bool encrypted,
             TinyCTSPeripheralHandler* handler, std::string& error) override {
    waitFor(5000, [&] { return _p.state != CBManagerStateUnknown; });
    if (_p.state != CBManagerStatePoweredOn) {
      error = stateError(_p.state);
      return false;
    }
    _p->handler = handler;
    CBMutableService* service =
        [[CBMutableService alloc] initWithType:ctsUuid(CTS_SERVICE_UUID) primary:YES];
    NSMutableArray<CBCharacteristic*>* list = [NSMutableArray new];
    add(list, CTS_CURRENT_TIME_UUID, true, settable, encrypted);
    add(list, CTS_LOCAL_TIME_INFO_UUID, false, settable, encrypted);
    add(list, CTS_REFERENCE_TIME_INFO_UUID, false, false, encrypted);
    service.characteristics = list;

    _p.failure = nil;
    _p.serviceAdded = NO;
    [_p.manager addService:service];
    if (!waitFor(5000, [&] { return _p.serviceAdded; }) || _p.failure != nil) {
      error = _p.failure != nil ? "cannot add the service: " + toString(_p.failure) : "timeout";
      return false;
    }
    _p.advertising = NO;
    [_p.manager startAdvertising:@{
      CBAdvertisementDataServiceUUIDsKey : @[ ctsUuid(CTS_SERVICE_UUID) ],
      CBAdvertisementDataLocalNameKey : [NSString stringWithUTF8String:name.c_str()]
    }];
    if (!waitFor(5000, [&] { return _p.advertising; }) || _p.failure != nil) {
      error = _p.failure != nil ? "cannot advertise: " + toString(_p.failure) : "timeout";
      return false;
    }
    return true;
  }

  void process() override {}  // the requests are answered by the thread of the system

  void notify(uint16_t uuid, const std::vector<uint8_t>& value) override {
    CBMutableCharacteristic* c = _p.chars[@(uuid)];
    if (c == nil || _p.subscribers == 0) return;
    [_p.manager updateValue:[NSData dataWithBytes:value.data() length:value.size()]
          forCharacteristic:c
       onSubscribedCentrals:nil];
  }

  void stop() override {
    [_p.manager stopAdvertising];
    [_p.manager removeAllServices];
    _p->handler = nullptr;
    [_p.chars removeAllObjects];
  }

 private:
  void add(NSMutableArray<CBCharacteristic*>* list, uint16_t uuid, bool notify, bool writable,
           bool encrypted) {
    CBCharacteristicProperties props = CBCharacteristicPropertyRead;
    CBAttributePermissions perms = CBAttributePermissionsReadable;
    if (notify) props |= CBCharacteristicPropertyNotify;
    if (writable) {
      props |= CBCharacteristicPropertyWrite;
      perms |= encrypted ? CBAttributePermissionsWriteEncryptionRequired
                         : CBAttributePermissionsWriteable;
    }
    // no value: the reads and writes are given to the handler
    CBMutableCharacteristic* c = [[CBMutableCharacteristic alloc] initWithType:ctsUuid(uuid)
                                                                    properties:props
                                                                         value:nil
                                                                   permissions:perms];
    _p.chars[@(uuid)] = c;
    [list addObject:c];
  }

  TinyCTSCBPeripheral* _p;
};

}  // namespace

namespace tiny_cts {

std::unique_ptr<TinyCTSCentral> ctsCreateCentral(const std::string&) {
  return std::unique_ptr<TinyCTSCentral>(new CoreBluetoothCentral());
}

std::unique_ptr<TinyCTSPeripheral> ctsCreatePeripheral(const std::string&) {
  return std::unique_ptr<TinyCTSPeripheral>(new CoreBluetoothPeripheral());
}

}  // namespace tiny_cts

#endif  // __APPLE__ && !ARDUINO

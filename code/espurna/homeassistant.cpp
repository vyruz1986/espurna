/*

HOME ASSISTANT MODULE

Original module
Copyright (C) 2017-2019 by Xose Pérez <xose dot perez at gmail dot com>

Reworked queueing and RAM usage reduction
Copyright (C) 2019-2021 by Maxim Prokhorov <prokhorov dot max at outlook dot com>

*/

#include "espurna.h"

#if HOMEASSISTANT_SUPPORT

#include "light.h"
#include "mqtt.h"
#include "relay.h"
#include "sensor.h"
#include "ws.h"

#include <ArduinoJson.h>

#include <forward_list>
#include <memory>

namespace homeassistant {

// Output is supposed to be used as both part of the MQTT config topic and the `uniq_id` field
// TODO: manage UTF8 strings? in case we somehow receive `desc`, like it was done originally

String normalize_ascii(String&& value, bool lower = false) {
    auto* ptr = const_cast<char*>(value.c_str());
    for (;;) {
        switch (*ptr) {
        case '\0':
            goto return_value;
        case '0' ... '9':
        case 'a' ... 'z':
            break;
        case 'A' ... 'Z':
            if (lower) {
                *ptr += 32;
            }
            break;
        default:
            *ptr = '_';
            break;
        }
        ++ptr;
    }

return_value:
    return std::move(value);
}

// Common data used across the discovery payloads.
// ref. https://developers.home-assistant.io/docs/entity_registry_index/

class Device {
public:
    struct Strings {
        Strings() = delete;
        Strings(const Strings&) = delete;

        Strings(Strings&&) = default;
        Strings(String&& prefix_, String&& name_, const String& identifier_, const String& version_, const String& manufacturer_, const String& device_) :
            prefix(std::move(prefix_)),
            name(std::move(name_)),
            identifier(identifier_),
            version(version_),
            manufacturer(manufacturer_),
            device(device_)
        {
			name = normalize_ascii(std::move(name));
            identifier = normalize_ascii(std::move(identifier), true);
        }

        String prefix;
        String name;
        String identifier;
        String version;
        String manufacturer;
        String device;
    };

    using StringsPtr = std::unique_ptr<Strings>;

    static constexpr size_t BufferSize { JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(5) };

    using Buffer = StaticJsonBuffer<BufferSize>;
    using BufferPtr = std::unique_ptr<Buffer>;

    Device() = delete;
    Device(const Device&) = delete;

    Device(Device&&) = default;
    Device(String&& prefix, String&& name, const String& identifier, const String& version, const String& manufacturer, const String& device) :
        _strings(std::make_unique<Strings>(std::move(prefix), std::move(name), identifier, version, manufacturer, device)),
        _buffer(std::make_unique<Buffer>()),
        _root(_buffer->createObject())
    {
        JsonArray& ids = _root.createNestedArray("ids");
        ids.add(_strings->identifier.c_str());

        _root["name"] = _strings->name.c_str();
        _root["sw"] = _strings->version.c_str();
        _root["mf"] = _strings->manufacturer.c_str();
        _root["mdl"] = _strings->device.c_str();
    }

    const String& name() const {
        return _strings->name;
    }

    const String& prefix() const {
        return _strings->prefix;
    }

    const String& identifier() const {
        return _strings->identifier;
    }

    JsonObject& root() {
        return _root;
    }

private:
    StringsPtr _strings;
    BufferPtr _buffer;
    JsonObject& _root;
};

using DevicePtr = std::unique_ptr<Device>;
using JsonBufferPtr = std::unique_ptr<DynamicJsonBuffer>;

class Context {
public:
    Context() = delete;
    Context(DevicePtr&& device, size_t capacity) :
        _device(std::move(device)),
        _capacity(capacity)
    {}

    const String& name() const {
        return _device->name();
    }

    const String& prefix() const {
        return _device->prefix();
    }

    const String& identifier() const {
        return _device->identifier();
    }

    JsonObject& device() {
        return _device->root();
    }

    void reset() {
        _json = std::make_unique<DynamicJsonBuffer>(_capacity);
    }

    size_t capacity() const {
        return _capacity;
    }

    size_t size() {
        if (_json) {
            return _json->size();
        }
        
        return 0;
    }

    JsonObject& makeObject() {
        if (!_json) {
            reset();
        }

        return _json->createObject();
    }

private:
    String _prefix;
    DevicePtr _device;

    JsonBufferPtr _json;
    size_t _capacity { 0ul };
};

Context makeContext() {
    auto device = std::make_unique<Device>(
        getSetting("haPrefix", HOMEASSISTANT_PREFIX),
        getSetting("hostname", getIdentifier()),
        getIdentifier(),
        getVersion(),
        getManufacturer(),
        getDevice()
    );

    return Context(std::move(device), 2048ul);
}

String quote(String&& value) {
    if (value.equalsIgnoreCase("y")
        || value.equalsIgnoreCase("n")
        || value.equalsIgnoreCase("yes")
        || value.equalsIgnoreCase("no")
        || value.equalsIgnoreCase("true")
        || value.equalsIgnoreCase("false")
        || value.equalsIgnoreCase("on")
        || value.equalsIgnoreCase("off")
    ) {
        String result;
        result.reserve(value.length() + 2);
        result += '"';
        result += value;
        result += '"';
        return result;
    }

    return std::move(value);
}

// - Discovery object is expected to accept Context reference as input
//   (and all implementations do just that)
// - topic() & message() return refs, since those *may* be called multiple times before advancing to the next 'entity'
// - We use short-hand names right away, since we don't expect this to be used to generate yaml
// - In case the object uses the JSON makeObject() as state, make sure we don't use it (state)
//   and the object itself after next() or ok() return false
// - Make sure JSON state is not created on construction, but lazy-loaded as soon as it is needed.
//   Meaning, we don't cause invalid refs immediatly when there are more than 1 discovery object present and we reset the storage.

class Discovery {
public:
    virtual ~Discovery() {
    }

    virtual bool ok() const = 0;
    virtual const String& topic() = 0;
    virtual const String& message() = 0;
    virtual bool next() = 0;
};

#if RELAY_SUPPORT

struct RelayContext {
    String availability;
    String payload_available;
    String payload_not_available;
    String payload_on;
    String payload_off;
};

RelayContext makeRelayContext() {
    return {
        mqttTopic(MQTT_TOPIC_STATUS, false),
        quote(mqttPayloadStatus(true)),
        quote(mqttPayloadStatus(false)),
        quote(relayPayload(PayloadStatus::On)),
        quote(relayPayload(PayloadStatus::Off))
    };
}

class RelayDiscovery : public Discovery {
public:
    RelayDiscovery() = delete;
    explicit RelayDiscovery(Context& ctx) :
        _ctx(ctx),
        _relay(makeRelayContext()),
        _relays(relayCount())
    {
        if (!_relays) {
            return;
        }

        auto& json = root();
        json["dev"] = _ctx.device();
        json["avty_t"] = _relay.availability.c_str();
        json["pl_avail"] = _relay.payload_available.c_str();
        json["pl_not_avail"] = _relay.payload_not_available.c_str();
        json["pl_on"] = _relay.payload_on.c_str();
        json["pl_off"] = _relay.payload_off.c_str();
    }

    JsonObject& root() {
        if (!_root) {
            _root = &_ctx.makeObject();
        }

        return *_root;
    }

    bool ok() const override {
        return (_relays) && (_index < _relays);
    }

    const String& uniqueId() {
        if (!_unique_id.length()) {
            _unique_id = _ctx.identifier() + '_' + F("relay") + '_' + _index;
        }
        return _unique_id;
    }

    const String& topic() override {
        if (!_topic.length()) {
            _topic = _ctx.prefix();
            _topic += F("/switch/");
            _topic += uniqueId();
            _topic += F("/config");
        }
        return _topic;
    }

    const String& message() override {
        if (!_message.length()) {
            auto& json = root();
            json["uniq_id"] = uniqueId();
            json["name"] = _ctx.name() + ' ' + _index;
            json["stat_t"] = mqttTopic(MQTT_TOPIC_RELAY, _index, false);
            json["cmd_t"] = mqttTopic(MQTT_TOPIC_RELAY, _index, true);
            json.printTo(_message);
        }
        return _message;
    }

    bool next() override {
        if (_index < _relays) {
            auto current = _index;
            ++_index;
            if ((_index > current) && (_index < _relays)) {
                _unique_id = "";
                _topic = "";
                _message = "";
                return true;
            }
        }

        return false;
    }

private:
    Context& _ctx;
    JsonObject* _root { nullptr };

    RelayContext _relay;
    unsigned char _index { 0u };
    unsigned char _relays { 0u };

    String _unique_id;
    String _topic;
    String _message;
};

#endif

// Example payload:
// {
//  "brightness": 255,
//  "color_temp": 155,
//  "color": {
//    "r": 255,
//    "g": 180,
//    "b": 200,
//    "x": 0.406,
//    "y": 0.301,
//    "h": 344.0,
//    "s": 29.412
//  },
//  "effect": "colorloop",
//  "state": "ON",
//  "transition": 2,
//  "white_value": 150
// }

// Notice that we only support JSON schema payloads, leaving it to the user to configure special
// per-channel topics, as those don't really fit into the HASS idea of lights controls for a single device

#if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE

static_assert(
    (MQTT_LIBRARY != MQTT_LIBRARY_ASYNCMQTTCLIENT) ||
    ((TCP_MSS == 1460) && (MQTT_LIBRARY == MQTT_LIBRARY_ASYNCMQTTCLIENT)),
    "Can't reliably send / receive JSON payloads with small TCP buffers"
);

class LightDiscovery : public Discovery {
public:
    explicit LightDiscovery(Context& ctx) :
        _ctx(ctx)
    {}

    JsonObject& root() {
        if (!_root) {
            _root = &_ctx.makeObject();
        }

        return *_root;
    }

    bool ok() const override {
        return true;
    }

    bool next() override {
        return false;
    }

    const String& uniqueId() {
        if (!_unique_id.length()) {
            _unique_id = _ctx.identifier() + '_' + F("light");
        }

        return _unique_id;
    }

    const String& topic() override {
        if (!_topic.length()) {
            _topic = _ctx.prefix();
            _topic += F("/light/");
            _topic += uniqueId();
            _topic += F("/config");
        }

        return _topic;
    }

    const String& message() override {
        if (!_message.length()) {
            auto& json = root();

            json["schema"] = "json";
            json["uniq_id"] = uniqueId();

            json["name"] = _ctx.name() + ' ' + F("Light");

            json["stat_t"] = mqttTopic(MQTT_TOPIC_LIGHT_JSON, false);
            json["cmd_t"] = mqttTopic(MQTT_TOPIC_LIGHT_JSON, true);

            json["avty_t"] = mqttTopic(MQTT_TOPIC_STATUS, false);
            json["pl_avail"] = quote(mqttPayloadStatus(true));
            json["pl_not_avail"] = quote(mqttPayloadStatus(false));

            // ref. SUPPORT_... flags throughout the light component
            // send `true` for every payload we support sending / receiving
            // already enabled by default: "state", "transition"

            // TODO: handle "rgb", "color_temp" and "white_value"

            json["brightness"] = true;

            if (lightHasColor()) {
                json["rgb"] = true;
            }

            if (lightHasColor() || lightUseCCT()) {
                json["max_mireds"] = LIGHT_WARMWHITE_MIRED;
                json["min_mireds"] = LIGHT_COLDWHITE_MIRED;
                json["color_temp"] = true;
            }

            json.printTo(_message);
        }

        return _message;
    }

private:
    Context& _ctx;
    JsonObject* _root { nullptr };

    String _unique_id;
    String _topic;
    String _message;
};

void publishLightJson() {
    if (!mqttConnected()) {
        return;
    }

    DynamicJsonBuffer buffer(512);
    JsonObject& root = buffer.createObject();

    root["state"] = lightState() ? "ON" : "OFF";
    root["brightness"] = lightBrightness();

    String message;
    root.printTo(message);

    String topic = mqttTopic(MQTT_TOPIC_LIGHT_JSON, false);
    mqttSendRaw(topic.c_str(), message.c_str(), false);
}

void receiveLightJson(char* payload) {
    DynamicJsonBuffer buffer(1024);
    JsonObject& root = buffer.parseObject(payload);

    if (!root.success()) {
        return;
    }

    if (!root.containsKey("state")) {
        return;
    }

    auto state = root["state"].as<String>();
    if (state == F("ON")) {
        lightState(true);
    } else if (state == F("OFF")) {
        lightState(false);
    } else {
        return;
    }

    unsigned long transition { lightTransitionTime() };
    if (root.containsKey("transition")) {
        auto seconds = root["transition"].as<float>();
        if (seconds > 0) {
            transition = static_cast<unsigned long>(seconds * 1000.0);
        }
    }

    if (root.containsKey("brightness")) {
        lightBrightness(root["brightness"].as<long>());
    }

    // TODO: handle "rgb", "color_temp" and "white_value"

    lightUpdate({transition, lightTransitionStep()});
}

#endif

#if SENSOR_SUPPORT

class SensorDiscovery : public Discovery {
public:
    SensorDiscovery() = delete;
    explicit SensorDiscovery(Context& ctx) :
        _ctx(ctx),
        _magnitudes(magnitudeCount())
    {}

    JsonObject& root() {
        if (!_root) {
            _root = &_ctx.makeObject();
        }

        return *_root;
    }

    bool ok() const {
        return _index < _magnitudes;
    }

    const String& topic() override {
        if (!_topic.length()) {
            _topic = _ctx.prefix();
            _topic += F("/sensor/");
            _topic += uniqueId();
            _topic += F("/config");
        }

        return _topic;
    }

    const String& message() override {
        if (!_message.length()) {
            auto& json = root();
            json["dev"] = _ctx.device();
            json["uniq_id"] = uniqueId();

            json["name"] = _ctx.name() + ' ' + name() + ' ' + localId();
            json["stat_t"] = mqttTopic(magnitudeTopicIndex(_index).c_str(), false);
            json["unit_of_meas"] = magnitudeUnits(_index);

            json.printTo(_message);
        }

        return _message;
    }

    const String& name() {
        if (!_name.length()) {
            _name = magnitudeTopic(magnitudeType(_index));
        }

        return _name;
    }

    unsigned char localId() const {
        return magnitudeIndex(_index);
    }

    const String& uniqueId() {
        if (!_unique_id.length()) {
            _unique_id = _ctx.identifier() + '_' + name() + '_' + localId();
        }

        return _unique_id;
    }

    bool next() override {
        if (_index < _magnitudes) {
            auto current = _index;
            ++_index;
            if ((_index > current) && (_index < _magnitudes)) {
                _unique_id = "";
                _name = "";
                _topic = "";
                _message = "";
                return true;
            }
        }

        return false;
    }

private:
    Context& _ctx;
    JsonObject* _root { nullptr };

    unsigned char _index { 0u };
    unsigned char _magnitudes { 0u };

    String _unique_id;
    String _name;
    String _topic;
    String _message;
};

#endif

// Reworked discovery class. Continiously schedules itself until we have no more entities to send.
// Topic and message are generated on demand and most of JSON payload is cached for re-use.
// (both, to avoid manually generating JSON and to avoid possible UTF8 issues when concatenating char raw strings)

class DiscoveryTask {
public:
    using Entity = std::unique_ptr<Discovery>;
    using Entities = std::forward_list<Entity>;

    using Action = std::function<bool(const String&, const String&)>;

    static constexpr int Retries { 5 };

    DiscoveryTask(bool enabled, Action action) :
        _enabled(enabled),
        _action(action)
    {}

    void add(Entity&& entity) {
        _entities.push_front(std::move(entity));
    }

    template <typename T>
    void add() {
        _entities.push_front(std::make_unique<T>(_ctx));
    }

    Context& context() {
        return _ctx;
    }

    bool empty() const {
        return _entities.empty();
    }

    bool operator()() {
        if (!mqttConnected() || _entities.empty()) {
            return false;
        }

        auto& entity = _entities.front();
        if (!entity->ok()) {
            _entities.pop_front();
            _ctx.reset();
            return true;
        }

        const auto* topic = entity->topic().c_str();
        const auto* msg = _enabled
            ? entity->message().c_str()
            : "";

        auto res = _action(topic, msg);
        if (!res) {
            if (--_retry < 0) {
                DEBUG_MSG_P(PSTR("[HASS] Discovery failed after %d retries\n"), Retries);
                return false;
            }

            DEBUG_MSG_P(PSTR("[HASS] Sending failed, retrying %d / %d\n"), (Retries - _retry), Retries);
            return true;
        }

        _retry = Retries;
        if (entity->next()) {
            return true;
        }

        _entities.pop_front();
        if (!_entities.empty()) {
            _ctx.reset();
            return true;
        }

        return false;
    }

private:
    bool _enabled { false };
    int _retry { Retries };
    Context _ctx { makeContext() };
    Action _action;
    Entities _entities;
};

namespace internal {

constexpr unsigned long interval { 100ul };
bool retain { false };
bool enabled { false };
bool sent { false };
Ticker timer;

} // namespace internal

bool mqttSend(const String& topic, const String& message) {
    return ::mqttSendRaw(topic.c_str(), message.c_str(), internal::retain) > 0;
}

bool enabled() {
    return internal::enabled;
}

void publishDiscovery() {
    static bool busy { false };
    if (busy) {
        return;
    }

    if (internal::sent) {
        return;
    }

    bool current = internal::enabled;
    internal::enabled = getSetting("haEnabled", 1 == HOMEASSISTANT_ENABLED);
    internal::retain = getSetting("haRetain", 1 == HOMEASSISTANT_RETAIN);

    if (current != internal::enabled) {
        auto task = std::make_shared<DiscoveryTask>(internal::enabled, homeassistant::mqttSend);

#if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE
        task->add<LightDiscovery>();
#endif
#if RELAY_SUPPORT
        task->add<RelayDiscovery>();
#endif
#if SENSOR_SUPPORT
        task->add<SensorDiscovery>();
#endif

        if (task->empty()) {
            return;
        }

        internal::timer.attach_ms(internal::interval, [task]() {
            if (!(*task)()) {
                internal::timer.detach();
                internal::sent = true;
                busy = false;
            }
        });
    }
}

void mqttCallback(unsigned int type, const char* topic, char* payload) {
    if (MQTT_DISCONNECT_EVENT == type) {
        internal::sent = false;
        return;
    }

    if (MQTT_CONNECT_EVENT == type) {
#if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE
        ::mqttSubscribe(MQTT_TOPIC_LIGHT_JSON);
        schedule_function(publishLightJson);
#endif
        schedule_function(publishDiscovery);
        return;
    }

#if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE
    if (type == MQTT_MESSAGE_EVENT) {
        String t = ::mqttMagnitude(topic);
        if (t.equals(MQTT_TOPIC_LIGHT_JSON)) {
            receiveLightJson(payload);
        }
        return;
    }
#endif
}

namespace web {

#if WEB_SUPPORT

void onVisible(JsonObject& root) {
    root["haVisible"] = 1;
}

void onConnected(JsonObject& root) {
    root["haPrefix"] = getSetting("haPrefix", HOMEASSISTANT_PREFIX);
    root["haEnabled"] = getSetting("haEnabled", 1 == HOMEASSISTANT_ENABLED);
    root["haRetain"] = getSetting("haRetain", 1 == HOMEASSISTANT_RETAIN);
}

bool onKeyCheck(const char* key, JsonVariant& value) {
    return (strncmp(key, "ha", 2) == 0);
}

#endif

} // namespace web
} // namespace homeassistant

void haSetup() {
#if WEB_SUPPORT
    wsRegister()
        .onVisible(homeassistant::web::onVisible)
        .onConnected(homeassistant::web::onConnected)
        .onKeyCheck(homeassistant::web::onKeyCheck);
#endif

#if LIGHT_PROVIDER != LIGHT_PROVIDER_NONE
    lightSetReportListener(homeassistant::publishLightJson);
#endif
    mqttRegister(homeassistant::mqttCallback);

    espurnaRegisterReload([]() {
        if (mqttConnected()) {
            homeassistant::publishDiscovery();
        }
    });
}

#endif // HOMEASSISTANT_SUPPORT

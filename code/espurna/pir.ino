/*

PIR MODULE

Copyright (C) 2016-2017 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if PIR_SUPPORT

// -----------------------------------------------------------------------------
// PIR
// -----------------------------------------------------------------------------

#include <vector>

typedef struct {
    uint8_t pin;
    bool lastState;
} pir_t;

std::vector<pir_t> _pirs;

#if MQTT_SUPPORT
#ifdef MQTT_TOPIC_PIR
void pirMQTT(unsigned char id, uint8_t event) {
    if (id >= _pirs.size()) return;
    char payload[2];
    snprintf_P(payload, sizeof(payload), PSTR("%d"), event);
    mqttSend(MQTT_TOPIC_PIR, id, payload);
}
#endif
#endif

uint8_t mapEvent(bool pirState) {
    if (pirState == LOW) return PIR_EVENT_NONE;
    if (pirState == HIGH) return PIR_EVENT_DETECTING;
}

bool getPirState(){
    return digitalRead(PIR_PIN);
}

void _pirWebSocketOnSend(JsonObject& root) {
    root["pirVisible"] = 1;
    root["pirState"] = getPirState();
}

void pirEvent(unsigned int id, unsigned char event) {

    DEBUG_MSG_P(PSTR("[PIR] Toggled #%d, event: %d\n"), id, event);

    #if MQTT_SUPPORT
    #ifdef MQTT_TOPIC_PIR
        pirMQTT(id, event);
    #endif
    #endif

    // Update websocket clients
    #if WEB_SUPPORT
        wsSend(_pirWebSocketOnSend);
    #endif

}

void pirSetup() {

    #ifdef PIR_PIN
    {
        pinMode(PIR_PIN, INPUT_PULLUP);
        bool pirState = getPirState();
        _pirs.push_back({PIR_PIN, pirState});
    }
    #endif

    #if WEB_SUPPORT

        // Websockets
        wsOnSendRegister(_pirWebSocketOnSend);
        
        // API register
        apiRegister(PIR_TOPIC, PIR_TOPIC, [](char * buffer, size_t len) {
            snprintf_P(buffer, len, PSTR("%d"), getPirState());
        });

    #endif

    DEBUG_MSG_P(PSTR("[PIR] Number of pirs: %d\n"), _pirs.size());

}

void pirLoop() {

    for (unsigned int i=0; i < _pirs.size(); i++) {
        bool pirState = digitalRead(_pirs[i].pin);
        if(pirState != _pirs[i].lastState) {
            unsigned char mapped = mapEvent(pirState);
            _pirs[i].lastState = pirState;
            pirEvent(i, mapped);
        }
    }
}

#endif
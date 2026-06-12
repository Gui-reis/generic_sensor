#pragma once

#include <stdint.h>

/*
 * MqttSecrets.h é local e ignorado pelo Git. Copie MqttSecrets.example.h para
 * esse nome e preencha os dados do seu cluster antes de compilar para o ESP32.
 */
#ifndef __has_include
#define __has_include(file) 0
#endif

#if __has_include("MqttSecrets.h")
#include "MqttSecrets.h"
#endif

#ifndef MQTT_ENABLED
#define MQTT_ENABLED false
#endif

#ifndef MQTT_HOST
#define MQTT_HOST ""
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 8883
#endif

#ifndef MQTT_USERNAME
#define MQTT_USERNAME ""
#endif

#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif

#ifndef MQTT_TOPIC
#define MQTT_TOPIC "sensores/esp32/temperatura"
#endif

namespace mqtt_config {
constexpr bool kEnabled = MQTT_ENABLED;
constexpr char kHost[] = MQTT_HOST;
constexpr uint16_t kPort = MQTT_PORT;
constexpr char kUsername[] = MQTT_USERNAME;
constexpr char kPassword[] = MQTT_PASSWORD;
constexpr char kTopic[] = MQTT_TOPIC;
} // namespace mqtt_config

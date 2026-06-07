#include "MqttPublisher.h"

#include <WiFi.h>

#include "MqttConfig.h"

MqttPublisher::MqttPublisher()
    : mqttClient_(secureClient_), clientId_{0}, lastConnectionAttemptAt_(0) {}

////////////////////////////////

void MqttPublisher::begin() {
  if (!mqtt_config::kEnabled) {
    Serial.println(
        "MQTT desativado. Configure include/MqttSecrets.h para habilitar.");
    return;
  }

  /*
   * Identifica cada placa pelo eFuse para impedir que dois dispositivos usem o
   * mesmo client ID e derrubem mutuamente suas sessões no broker.
   */
  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(clientId_, sizeof(clientId_), "esp32-sensor-%04X%08X",
           static_cast<uint16_t>(chipId >> 32),
           static_cast<uint32_t>(chipId));

  /*
   * Esta primeira integração usa TLS sem validar a autoridade certificadora.
   * Isso é adequado apenas para o teste inicial; produção deve usar setCACert().
   */
  secureClient_.setInsecure();
  mqttClient_.setServer(mqtt_config::kHost, mqtt_config::kPort);

  /* Permite a primeira tentativa assim que o Wi-Fi estiver disponível. */
  lastConnectionAttemptAt_ = millis() - kReconnectIntervalMs;

  Serial.print("MQTT configurado para ");
  Serial.print(mqtt_config::kHost);
  Serial.print(":");
  Serial.println(mqtt_config::kPort);
}

////////////////////////////////

void MqttPublisher::loop() {
  if (!mqtt_config::kEnabled) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (mqttClient_.connected()) {
      mqttClient_.disconnect();
    }
    return;
  }

  if (mqttClient_.connected()) {
    mqttClient_.loop();
    return;
  }

  const uint32_t now = millis();
  if (now - lastConnectionAttemptAt_ < kReconnectIntervalMs) {
    return;
  }

  lastConnectionAttemptAt_ = now;
  connect();
}

////////////////////////////////

bool MqttPublisher::publishTemperature(float temperatureCelsius) {
  if (!mqtt_config::kEnabled || !mqttClient_.connected()) {
    return false;
  }

  char payload[kPayloadSize];
  const int written = snprintf(
      payload, sizeof(payload),
      "{\"sensor\":\"%s\",\"temperatura_celsius\":%.2f}", clientId_,
      temperatureCelsius);

  if (written < 0 || static_cast<size_t>(written) >= sizeof(payload)) {
    Serial.println("Não foi possível montar o payload MQTT.");
    return false;
  }

  const bool published = mqttClient_.publish(mqtt_config::kTopic, payload);
  if (!published) {
    Serial.println("Falha ao publicar a temperatura via MQTT.");
    return false;
  }

  Serial.print("Mensagem MQTT enviada para '");
  Serial.print(mqtt_config::kTopic);
  Serial.print("': ");
  Serial.println(payload);
  return true;
}

////////////////////////////////

bool MqttPublisher::isConnected() { return mqttClient_.connected(); }

////////////////////////////////

void MqttPublisher::connect() {
  Serial.print("Conectando ao broker MQTT... ");

  const bool connected = mqttClient_.connect(
      clientId_, mqtt_config::kUsername, mqtt_config::kPassword);

  if (connected) {
    Serial.println("conectado!");
    return;
  }

  Serial.print("falhou. Código: ");
  Serial.println(mqttClient_.state());
}

#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

/**
 * Mantém a conexão segura com o broker MQTT e publica leituras de temperatura.
 *
 * As tentativas de reconexão são espaçadas, sem laços ou delays bloqueantes.
 * Enquanto o Wi-Fi estiver indisponível, a classe apenas aguarda a restauração
 * da rede pelo componente responsável pela conectividade.
 */
class MqttPublisher {
public:
  MqttPublisher();

  ////////////////////////////////

  /** Configura TLS, endereço do broker e o identificador deste ESP32. */
  void begin();

  ////////////////////////////////

  /** Mantém a sessão MQTT e tenta reconectar em intervalos controlados. */
  void loop();

  ////////////////////////////////

  /** Publica uma leitura válida em JSON. Retorna true quando ela foi aceita. */
  bool publishTemperature(float temperatureCelsius);

  ////////////////////////////////

  /** Informa se existe uma sessão ativa com o broker. */
  bool isConnected();

private:
  static constexpr uint32_t kReconnectIntervalMs = 5000;
  static constexpr size_t kClientIdSize = 32;
  static constexpr size_t kPayloadSize = 128;

  void connect();

  WiFiClientSecure secureClient_;
  PubSubClient mqttClient_;
  char clientId_[kClientIdSize];
  uint32_t lastConnectionAttemptAt_;
};

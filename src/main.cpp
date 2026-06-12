#include <Arduino.h>
#include <DallasTemperature.h>

#include "MqttPublisher.h"
#include "NetworkManager.h"
#include "TemperatureSensor.h"
#include "UtcClock.h"

namespace {
constexpr uint8_t kOneWireBusPin = 4;
constexpr uint32_t kTemperatureReadIntervalMs = 1000;
constexpr uint32_t kTemperaturePublishIntervalMs = 5000;

MqttPublisher mqttPublisher;
NetworkManager networkManager;
TemperatureSensor temperatureSensor(kOneWireBusPin);
UtcClock utcClock;
uint32_t lastTemperatureReadAt = 0;
uint32_t lastTemperaturePublishAt = 0;

void printTemperature() {
  const float temperatureCelsius = temperatureSensor.readCelsius();

  // DEVICE_DISCONNECTED_C é o valor especial retornado pela biblioteca quando
  // nenhum sensor responde. Não o mostramos como se fosse uma temperatura real.
  if (temperatureCelsius == DEVICE_DISCONNECTED_C) {
    Serial.println("Não foi possível ler o sensor de temperatura.");
    return;
  }

  Serial.print("Temperatura: ");
  Serial.print(temperatureCelsius);
  Serial.println(" °C");

  const uint32_t now = millis();
  if (now - lastTemperaturePublishAt >= kTemperaturePublishIntervalMs &&
      mqttPublisher.isConnected()) {
    /*
     * Reserva um buffer com o tamanho exato documentado pelo relógio. A leitura
     * local continua funcionando enquanto o NTP sincroniza, mas nenhuma mensagem
     * incompleta ou com a data inicial de 1970 é enviada ao broker.
     */
    char utcTimestamp[UtcClock::kTimestampBufferSize];
    if (!utcClock.formatCurrentTimestamp(utcTimestamp,
                                         sizeof(utcTimestamp))) {
      Serial.println(
          "Horário UTC ainda indisponível; publicação MQTT adiada.");
      return;
    }

    if (mqttPublisher.publishTemperature(temperatureCelsius, utcTimestamp)) {
      lastTemperaturePublishAt = now;
    }
  }
}
} // namespace

void setup() {
  Serial.begin(115200);
  temperatureSensor.begin();
  networkManager.begin();
  utcClock.begin();
  mqttPublisher.begin();

  // Força uma primeira leitura logo após a inicialização, sem esperar um ciclo.
  lastTemperatureReadAt = millis() - kTemperatureReadIntervalMs;
  lastTemperaturePublishAt = millis() - kTemperaturePublishIntervalMs;
}

void loop() {
  // O portal precisa ser processado continuamente, inclusive enquanto o sensor
  // está funcionando sem conexão com uma rede externa.
  networkManager.loop();
  mqttPublisher.loop();

  // O controle por millis(), ao contrário de delay(1000), não bloqueia o
  // servidor HTTP e mantém a página de configuração responsiva durante as
  // medições.
  const uint32_t now = millis();
  if (now - lastTemperatureReadAt >= kTemperatureReadIntervalMs) {
    lastTemperatureReadAt = now;
    printTemperature();
  }
}

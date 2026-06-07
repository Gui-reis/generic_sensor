#include <Arduino.h>
#include <DallasTemperature.h>

#include "NetworkManager.h"
#include "TemperatureSensor.h"

namespace {
constexpr uint8_t kOneWireBusPin = 4;
constexpr uint32_t kTemperatureReadIntervalMs = 1000;

NetworkManager networkManager;
TemperatureSensor temperatureSensor(kOneWireBusPin);
uint32_t lastTemperatureReadAt = 0;

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
}
} // namespace

void setup() {
  Serial.begin(115200);
  temperatureSensor.begin();
  networkManager.begin();

  // Força uma primeira leitura logo após a inicialização, sem esperar um ciclo.
  lastTemperatureReadAt = millis() - kTemperatureReadIntervalMs;
}

void loop() {
  // O portal precisa ser processado continuamente, inclusive enquanto o sensor
  // está funcionando sem conexão com uma rede externa.
  networkManager.loop();

  // O controle por millis(), ao contrário de delay(1000), não bloqueia o
  // servidor HTTP e mantém a página de configuração responsiva durante as
  // medições.
  const uint32_t now = millis();
  if (now - lastTemperatureReadAt >= kTemperatureReadIntervalMs) {
    lastTemperatureReadAt = now;
    printTemperature();
  }
}

#include "TemperatureSensor.h"

TemperatureSensor::TemperatureSensor(uint8_t oneWirePin)
    : oneWire_(oneWirePin), sensors_(&oneWire_) {}

void TemperatureSensor::begin() {
  sensors_.begin();

  Serial.print("Sensores de temperatura encontrados: ");
  Serial.println(sensors_.getDeviceCount());
}

float TemperatureSensor::readCelsius() {
  // A biblioteca inicia a conversão e aguarda o DS18B20 concluir a medição.
  sensors_.requestTemperatures();
  return sensors_.getTempCByIndex(0);
}

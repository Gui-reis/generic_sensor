#pragma once

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>

/**
 * Pequeno adaptador para o sensor de temperatura Dallas/DS18B20.
 *
 * Encapsular OneWire e DallasTemperature deixa o main.cpp responsável apenas
 * por orquestrar os componentes da aplicação, sem detalhes de cada biblioteca.
 */
class TemperatureSensor {
public:
  /** Guarda o pino OneWire e conecta a biblioteca Dallas ao barramento. */
  explicit TemperatureSensor(uint8_t oneWirePin);

  ////////////////////////////////

  /** Inicializa o barramento OneWire e procura os sensores conectados. */
  void begin();

  ////////////////////////////////

  /** Solicita e devolve a temperatura do primeiro sensor, em graus Celsius. */
  float readCelsius();

private:
  OneWire oneWire_;
  DallasTemperature sensors_;
};

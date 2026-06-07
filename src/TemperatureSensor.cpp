#include "TemperatureSensor.h"

TemperatureSensor::TemperatureSensor(uint8_t oneWirePin)
    : oneWire_(oneWirePin), sensors_(&oneWire_) {
  /*
   * oneWire_ encapsula o pino físico usado pelo DS18B20. sensors_ recebe esse
   * barramento por referência para que a biblioteca DallasTemperature possa
   * enviar comandos e ler respostas dos dispositivos conectados.
   */
}

////////////////////////////////

void TemperatureSensor::begin() {
  /*
   * Inicializa a biblioteca DallasTemperature. Nesse momento ela prepara o
   * barramento OneWire e faz a descoberta dos sensores disponíveis.
   */
  sensors_.begin();

  /*
   * Mostra a quantidade encontrada no monitor serial para facilitar diagnóstico
   * de fiação, pull-up ausente ou sensor desconectado durante os testes.
   */
  Serial.print("Sensores de temperatura encontrados: ");
  Serial.println(sensors_.getDeviceCount());
}

////////////////////////////////

float TemperatureSensor::readCelsius() {
  /*
   * Solicita uma nova conversão de temperatura. A biblioteca aguarda a
   * conclusão da medição conforme a resolução configurada para o sensor.
   */
  sensors_.requestTemperatures();

  /*
   * Retorna a leitura do primeiro sensor encontrado no barramento. Caso nenhum
   * sensor responda, a própria biblioteca retorna DEVICE_DISCONNECTED_C.
   */
  return sensors_.getTempCByIndex(0);
}

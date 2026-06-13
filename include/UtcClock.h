#pragma once

#include <Arduino.h>
#include <time.h>

/**
 * Sincroniza e fornece o horário atual no fuso UTC.
 *
 * O relógio interno do ESP32 não conhece a data real logo após ligar a placa.
 * A chamada a begin() configura o serviço SNTP da plataforma para consultar
 * servidores NTP assim que houver acesso à internet. Depois da sincronização,
 * formatCurrentTimestamp() transforma o instante atual no padrão ISO 8601 usado
 * pelo payload MQTT.
 */
class UtcClock {
public:
  /** Tamanho de "2026-06-12T18:30:00Z" incluindo o terminador nulo. */
  static constexpr size_t kTimestampBufferSize = 21;

  /** Configura o relógio do sistema para sincronizar a data e a hora via NTP. */
  void begin();

  ////////////////////////////////

  /**
   * Escreve o horário UTC atual no buffer no formato ISO 8601.
   *
   * Retorna false enquanto o NTP ainda não tiver fornecido uma data válida ou
   * se o buffer recebido não tiver espaço para a representação completa.
   */
  bool formatCurrentTimestamp(char *buffer, size_t bufferSize) const;

private:
  /**
   * O relógio começa próximo de 1970 antes da sincronização. Qualquer instante
   * posterior a 1º de janeiro de 2024 é suficiente para diferenciá-lo desse
   * valor inicial sem vincular o firmware à data em que foi compilado.
   */
  static constexpr time_t kMinimumValidEpoch = 1704067200;
};

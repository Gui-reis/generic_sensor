#include "UtcClock.h"

namespace {
/*
 * Dois servidores independentes dão ao serviço SNTP uma alternativa caso um
 * endereço esteja temporariamente indisponível. O prefixo "pool" distribui as
 * consultas entre servidores públicos próximos do dispositivo.
 */
constexpr char kPrimaryNtpServer[] = "pool.ntp.org";
constexpr char kSecondaryNtpServer[] = "time.nist.gov";
} // namespace

void UtcClock::begin() {
  /*
   * O primeiro e o segundo argumentos representam, respectivamente, o
   * deslocamento do fuso e o ajuste de horário de verão, ambos em segundos.
   * Mantê-los zerados faz o relógio trabalhar diretamente em UTC e evita que o
   * timestamp publicado dependa da localização física do ESP32.
   *
   * configTime() apenas inicia o serviço de sincronização e retorna sem esperar
   * pela rede. Portanto, é seguro chamá-la mesmo quando o portal de configuração
   * está ativo; o SNTP consultará os servidores quando a internet estiver
   * disponível.
   */
  configTime(0, 0, kPrimaryNtpServer, kSecondaryNtpServer);
  Serial.println("Sincronização do relógio UTC configurada via NTP.");
}

////////////////////////////////

bool UtcClock::formatCurrentTimestamp(char *buffer, size_t bufferSize) const {
  /*
   * Verifica o ponteiro e o espaço antes de escrever. O tamanho público da
   * classe permite que o chamador reserve exatamente os 20 caracteres do
   * timestamp ISO 8601 mais o terminador nulo exigido pelas strings em C.
   */
  if (buffer == nullptr || bufferSize < kTimestampBufferSize) {
    return false;
  }

  /*
   * time() lê o relógio de sistema mantido pelo ESP32. Antes da primeira
   * resposta NTP ele ainda aponta para o início da época Unix; nesse estado não
   * há uma data real para associar à medição e a publicação deve aguardar.
   */
  time_t now;
  time(&now);
  if (now < kMinimumValidEpoch) {
    return false;
  }

  /*
   * gmtime_r() decompõe o instante explicitamente em UTC e grava o resultado em
   * uma estrutura fornecida pelo chamador. A variante reentrante não depende de
   * um buffer global compartilhado pela biblioteca C.
   */
  struct tm utcTime;
  if (gmtime_r(&now, &utcTime) == nullptr) {
    return false;
  }

  /*
   * O sufixo literal Z informa aos consumidores MQTT que o horário está em UTC.
   * strftime() retorna zero se não conseguir escrever toda a representação, o
   * que também impede o envio de um JSON com timestamp truncado.
   */
  const size_t written = strftime(buffer, bufferSize, "%Y-%m-%dT%H:%M:%SZ",
                                  &utcTime);
  return written > 0;
}

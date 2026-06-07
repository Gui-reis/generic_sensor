#include "WifiConnector.h"

#include <WiFi.h>

constexpr uint32_t WifiConnector::kConnectionTimeoutMs;

void WifiConnector::begin() {
  /*
   * Desabilita a persistência automática da biblioteca WiFi para que ela não
   * mantenha uma segunda cópia das credenciais fora do nosso controle. A NVS
   * administrada por WifiCredentialStore passa a ser a única fonte de verdade.
   * O auto reconnect continua habilitado para recuperar quedas momentâneas da
   * rede depois que uma conexão tiver sido estabelecida com sucesso.
   */
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
}

////////////////////////////////

bool WifiConnector::connect(WifiCredential *credentials,
                            uint8_t credentialCount) {
  /*
   * Sem nenhuma credencial não existe conexão a tentar. O retorno false instrui
   * NetworkManager a iniciar diretamente o portal de configuração.
   */
  if (credentialCount == 0) {
    return false;
  }

  /*
   * Copia as credenciais para candidatas temporárias. RSSI e visibilidade são
   * preenchidos pelo scan e não devem alterar os dados persistentes recebidos
   * do repositório de credenciais.
   */
  ConnectionCandidate candidates[WifiCredentialStore::kMaxKnownNetworks];
  for (uint8_t index = 0; index < credentialCount; ++index) {
    candidates[index] = {credentials[index], INT32_MIN, false};
  }

  /*
   * Coloca o rádio no modo estação, procura as redes próximas e reorganiza as
   * credenciais para tentar primeiro as opções com maior chance de sucesso.
   */
  WiFi.mode(WIFI_STA);
  updateVisibilityAndSortBySignal(candidates, credentialCount);

  /*
   * Percorre todas as redes conhecidas. As visíveis estão ordenadas por RSSI;
   * redes ocultas ou ausentes do scan permanecem no final e também são
   * tentadas.
   */
  for (uint8_t index = 0; index < credentialCount; ++index) {
    if (tryConnection(candidates[index])) {
      return true;
    }
  }

  /*
   * Nenhuma tentativa funcionou. Desconecta qualquer tentativa pendente antes
   * de o rádio ser reconfigurado como Access Point pelo fluxo de fallback.
   */
  WiFi.disconnect();
  return false;
}

////////////////////////////////

bool WifiConnector::isConnected() const {
  /*
   * A biblioteca WiFi é a autoridade sobre o estado real do rádio. Consultá-la
   * evita manter uma variável local que poderia ficar desatualizada após
   * quedas.
   */
  return WiFi.status() == WL_CONNECTED;
}

////////////////////////////////

bool WifiConnector::tryConnection(const ConnectionCandidate &candidate) {
  /* Exibe o SSID que está sendo testado sem jamais imprimir a senha. */
  Serial.print("Tentando conectar a '");
  Serial.print(candidate.credential.ssid);
  Serial.print("'");

  /*
   * Limpa o estado da tentativa anterior e inicia uma nova associação usando o
   * SSID e a senha recebidos da lista de credenciais conhecidas.
   */
  WiFi.disconnect();
  WiFi.begin(candidate.credential.ssid.c_str(),
             candidate.credential.password.c_str());

  /*
   * Aguarda somente até o tempo limite configurado. A subtração entre valores
   * de millis() continua correta mesmo quando o contador de 32 bits sofre
   * overflow.
   */
  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < kConnectionTimeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  /* Converte o estado final da biblioteca em sucesso ou falha da tentativa. */
  return WiFi.status() == WL_CONNECTED;
}

////////////////////////////////

void WifiConnector::updateVisibilityAndSortBySignal(
    ConnectionCandidate *candidates, uint8_t candidateCount) {
  /*
   * Executa um scan síncrono e inclui redes ocultas no resultado. O retorno é a
   * quantidade de access points encontrados, ou um valor negativo em caso de
   * erro.
   */
  Serial.println("Procurando redes Wi-Fi conhecidas nas proximidades...");
  const int16_t networkCount = WiFi.scanNetworks(false, true);

  /*
   * Reinicializa os dados transitórios de cada candidata e procura SSIDs iguais
   * no resultado do scan. Se houver vários access points com o mesmo nome,
   * guarda o maior RSSI, isto é, o sinal menos negativo e portanto mais forte.
   */
  for (uint8_t candidateIndex = 0; candidateIndex < candidateCount;
       ++candidateIndex) {
    candidates[candidateIndex].visible = false;
    candidates[candidateIndex].rssi = INT32_MIN;

    for (int16_t networkIndex = 0; networkIndex < networkCount;
         ++networkIndex) {
      if (WiFi.SSID(networkIndex) ==
          candidates[candidateIndex].credential.ssid) {
        candidates[candidateIndex].visible = true;
        if (WiFi.RSSI(networkIndex) > candidates[candidateIndex].rssi) {
          candidates[candidateIndex].rssi = WiFi.RSSI(networkIndex);
        }
      }
    }
  }

  /* Os resultados já foram copiados; libera a memória interna usada pelo scan.
   */
  WiFi.scanDelete();

  /*
   * Ordena no próprio array: redes visíveis vêm antes das não visíveis e, entre
   * redes visíveis, o maior RSSI ganha prioridade. Como existem no máximo cinco
   * elementos, a ordenação simples é pequena, previsível e suficiente.
   */
  for (uint8_t left = 0; left < candidateCount; ++left) {
    for (uint8_t right = left + 1; right < candidateCount; ++right) {
      const bool rightHasPriority =
          (candidates[right].visible && !candidates[left].visible) ||
          (candidates[right].visible == candidates[left].visible &&
           candidates[right].rssi > candidates[left].rssi);

      if (rightHasPriority) {
        const ConnectionCandidate temporary = candidates[left];
        candidates[left] = candidates[right];
        candidates[right] = temporary;
      }
    }
  }
}

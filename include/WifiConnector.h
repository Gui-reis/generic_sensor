#pragma once

#include <Arduino.h>

#include "WifiCredential.h"
#include "WifiCredentialStore.h"

/**
 * Procura redes conhecidas e tenta conectar o ESP32 à melhor candidata.
 *
 * Este componente concentra as decisões relacionadas ao rádio no modo estação:
 * scan, prioridade por intensidade do sinal, timeout e tentativas de
 * associação. Ele não conhece NVS, servidor HTTP, DNS ou arquivos do portal.
 */
class WifiConnector {
public:
  /** Configura o comportamento geral da biblioteca Wi-Fi. */
  void begin();

  ////////////////////////////////

  /** Tenta conectar usando as credenciais fornecidas, em ordem de prioridade.
   */
  bool connect(WifiCredential *credentials, uint8_t credentialCount);

  ////////////////////////////////

  /** Informa se o ESP32 está conectado a uma rede no modo estação. */
  bool isConnected() const;

private:
  /**
   * Combina uma credencial persistente com os dados temporários do último scan.
   * A estrutura existe somente durante o processo de conexão e nunca é gravada.
   */
  struct ConnectionCandidate {
    WifiCredential credential;
    int32_t rssi;
    bool visible;
  };

  /** Tempo máximo gasto tentando cada rede conhecida antes do próximo fallback.
   */
  static constexpr uint32_t kConnectionTimeoutMs = 12000;

  /** Executa uma única tentativa de associação Wi-Fi para a candidata dada. */
  bool tryConnection(const ConnectionCandidate &candidate);

  ////////////////////////////////

  /** Atualiza visibilidade/RSSI das candidatas e reordena o array. */
  void updateVisibilityAndSortBySignal(ConnectionCandidate *candidates,
                                       uint8_t candidateCount);
};

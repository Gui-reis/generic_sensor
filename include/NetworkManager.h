#pragma once

#include <Arduino.h>

#include "ConfigurationPortal.h"
#include "WifiConnector.h"
#include "WifiCredentialStore.h"

/**
 * Orquestra os componentes responsáveis pela conectividade Wi-Fi do sensor.
 *
 * O fluxo executado por begin() é:
 *  1. configurar o conector e carregar da NVS as redes salvas anteriormente;
 *  2. procurar redes próximas e tentar primeiro as conhecidas com melhor sinal;
 *  3. caso nenhuma conexão funcione, iniciar o portal de configuração.
 *
 * A classe funciona como uma fachada pequena para o restante da aplicação. Os
 * detalhes de persistência, associação Wi-Fi e servidor web ficam isolados em
 * WifiCredentialStore, WifiConnector e ConfigurationPortal, respectivamente.
 */
class NetworkManager {
public:
  /** Conecta o portal ao repositório de credenciais e mantém ambos inativos. */
  NetworkManager();

  ////////////////////////////////

  /** Inicia a conexão com redes conhecidas ou, como fallback, o portal web. */
  void begin();

  ////////////////////////////////

  /**
   * Processa as requisições HTTP e DNS do portal.
   * Deve ser chamado frequentemente no loop() para que a página seja
   * responsiva.
   */
  void loop();

  ////////////////////////////////

  /** Informa se o ESP32 está conectado a uma rede no modo estação. */
  bool isConnected() const;

  ////////////////////////////////

  /** Informa se o Access Point de configuração está ativo e pronto para uso. */
  bool isConfigurationPortalActive() const;

private:
  WifiCredentialStore credentialStore_;
  WifiConnector connector_;
  ConfigurationPortal portal_;
};

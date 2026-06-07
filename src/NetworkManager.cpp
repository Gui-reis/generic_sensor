#include "NetworkManager.h"

#include <WiFi.h>

NetworkManager::NetworkManager() : portal_(credentialStore_) {
  /*
   * ConfigurationPortal recebe a mesma instância de WifiCredentialStore usada
   * na inicialização. Assim, uma credencial enviada pelo navegador será gravada
   * no repositório que NetworkManager consultará depois da reinicialização.
   */
}

////////////////////////////////

void NetworkManager::begin() {
  /*
   * Prepara o comportamento global do rádio antes de carregar ou tentar
   * qualquer credencial. A persistência automática da biblioteca permanece
   * desabilitada, deixando WifiCredentialStore como a única fonte de verdade.
   */
  connector_.begin();

  /*
   * A lista possui tamanho fixo para manter o consumo de memória previsível no
   * ESP32. O repositório compacta entradas válidas e devolve somente a
   * quantidade que deve ser considerada pelo conector.
   */
  WifiCredential credentials[WifiCredentialStore::kMaxKnownNetworks];
  const uint8_t credentialCount = credentialStore_.load(
      credentials, WifiCredentialStore::kMaxKnownNetworks);

  /*
   * Uma conexão estabelecida encerra a inicialização sem expor o Access Point
   * de configuração. O sensor pode então seguir sua operação normal no modo
   * estação.
   */
  if (connector_.connect(credentials, credentialCount)) {
    Serial.println("Wi-Fi conectado!");
    Serial.print("Rede: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP do ESP32: ");
    Serial.println(WiFi.localIP());
    return;
  }

  /*
   * Se não havia credenciais ou todas as tentativas falharam, abre o portal
   * para que o usuário possa selecionar uma nova rede sem regravar o firmware.
   */
  Serial.println("Nenhuma rede conhecida pôde ser conectada.");
  portal_.begin();
}

////////////////////////////////

void NetworkManager::loop() {
  /*
   * ConfigurationPortal retorna imediatamente quando não está ativo. Delegar em
   * todos os ciclos mantém NetworkManager simples e evita duplicar o estado do
   * portal nesta fachada.
   */
  portal_.loop();
}

////////////////////////////////

bool NetworkManager::isConnected() const {
  /* O conector consulta diretamente a biblioteca WiFi para obter o estado real.
   */
  return connector_.isConnected();
}

////////////////////////////////

bool NetworkManager::isConfigurationPortalActive() const {
  /* O próprio portal é a autoridade sobre a conclusão de sua inicialização. */
  return portal_.isActive();
}

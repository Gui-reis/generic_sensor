#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>

/**
 * Gerencia toda a conectividade Wi-Fi do sensor.
 *
 * O fluxo executado por begin() é:
 *  1. carregar da NVS as redes salvas anteriormente;
 *  2. procurar redes próximas e tentar primeiro as conhecidas com melhor sinal;
 *  3. caso nenhuma conexão funcione, criar um Access Point e um portal web.
 *
 * As credenciais ficam na NVS (memória não volátil) do ESP32 por meio da classe
 * Preferences. Dessa forma elas sobrevivem a reinicializações e atualizações do
 * firmware que não apaguem a flash.
 */
class NetworkManager {
public:
  /** Constrói o gerenciador com servidor HTTP na porta padrão e portal parado.
   */
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
  /**
   * Representa uma rede conhecida. ssid/password vêm da NVS; rssi/visible são
   * preenchidos temporariamente após cada scan para ordenar as tentativas.
   */
  struct Credential {
    String ssid;
    String password;
    int32_t rssi;
    bool visible;
  };

  /** Limitar a quantidade evita uso de estruturas dinâmicas grandes no ESP32.
   */
  static constexpr uint8_t kMaxKnownNetworks = 5;

  /** Tempo máximo gasto tentando cada rede conhecida antes do próximo fallback.
   */
  static constexpr uint32_t kConnectionTimeoutMs = 12000;

  /** Porta padrão do DNS usada para o captive portal. */
  static constexpr uint16_t kDnsPort = 53;

  /** Namespace da NVS onde ficam a contagem, SSIDs e senhas. */
  static const char *const kPreferencesNamespace;

  /** Nome do Access Point exposto quando não há conexão com rede conhecida. */
  static const char *const kAccessPointSsid;

  /** Senha do Access Point; deve ter pelo menos oito caracteres. */
  static const char *const kAccessPointPassword;

  WebServer server_;
  DNSServer dnsServer_;
  Preferences preferences_;
  Credential credentials_[kMaxKnownNetworks];
  uint8_t credentialCount_;
  bool portalActive_;

  /** Carrega e compacta as credenciais salvas na memória não volátil. */
  void loadCredentials();

  ////////////////////////////////

  /** Salva ou atualiza uma credencial, mantendo a mais recente no início. */
  void saveCredential(const String &ssid, const String &password);

  ////////////////////////////////

  /** Tenta conectar a redes conhecidas, já ordenadas pela melhor chance. */
  bool connectToKnownNetwork();

  ////////////////////////////////

  /** Executa uma única tentativa de associação Wi-Fi para a credencial dada. */
  bool tryConnection(const Credential &credential);

  ////////////////////////////////

  /** Atualiza visibilidade/RSSI das credenciais e reordena o array. */
  void updateVisibilityAndSortBySignal();

  ////////////////////////////////

  /** Liga o Access Point, DNS cativo e servidor HTTP do portal. */
  void startConfigurationPortal();

  ////////////////////////////////

  /** Registra todas as rotas HTTP servidas pelo portal. */
  void configureWebServerRoutes();

  ////////////////////////////////

  /** Envia ao navegador a página HTML de configuração gravada em PROGMEM. */
  void handleRoot();

  ////////////////////////////////

  /** Faz o scan e retorna ao navegador as redes em formato JSON. */
  void handleNetworkScan();

  ////////////////////////////////

  /** Valida o formulário, salva as credenciais e reinicia o ESP32. */
  void handleSave();

  ////////////////////////////////

  /** Redireciona URLs desconhecidas para a raiz do captive portal. */
  void handleNotFound();

  ////////////////////////////////

  /** Escapa um texto para que ele possa ser inserido com segurança no JSON. */
  static String escapeJson(const String &value);
};

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
  NetworkManager();

  /** Inicia a conexão ou, como fallback, o portal de configuração. */
  void begin();

  /**
   * Processa as requisições HTTP e DNS do portal.
   * Deve ser chamado frequentemente no loop() para que a página seja
   * responsiva.
   */
  void loop();

  /** Informa se o ESP32 está conectado a uma rede no modo estação. */
  bool isConnected() const;

  /** Informa se o Access Point de configuração está ativo. */
  bool isConfigurationPortalActive() const;

private:
  struct Credential {
    String ssid;
    String password;
    int32_t rssi;
    bool visible;
  };

  // Limitar a quantidade evita uso de estruturas dinâmicas grandes no ESP32.
  static constexpr uint8_t kMaxKnownNetworks = 5;
  static constexpr uint32_t kConnectionTimeoutMs = 12000;
  static constexpr uint16_t kDnsPort = 53;

  static const char *const kPreferencesNamespace;
  static const char *const kAccessPointSsid;
  static const char *const kAccessPointPassword;

  WebServer server_;
  DNSServer dnsServer_;
  Preferences preferences_;
  Credential credentials_[kMaxKnownNetworks];
  uint8_t credentialCount_;
  bool portalActive_;

  void loadCredentials();
  void saveCredential(const String &ssid, const String &password);
  bool connectToKnownNetwork();
  bool tryConnection(const Credential &credential);
  void updateVisibilityAndSortBySignal();
  void startConfigurationPortal();
  void configureWebServerRoutes();

  void handleRoot();
  void handleNetworkScan();
  void handleSave();
  void handleNotFound();

  static String escapeJson(const String &value);
};

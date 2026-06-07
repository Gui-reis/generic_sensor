#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>

#include "WifiCredentialStore.h"

/**
 * Expõe o Access Point e a interface web usados para configurar uma nova rede.
 *
 * A classe reúne os elementos que formam o captive portal como uma única
 * responsabilidade: Access Point, DNS curinga, servidor HTTP e arquivos do
 * LittleFS. As credenciais aprovadas pelo formulário são entregues ao
 * WifiCredentialStore, sem que o portal conheça o formato usado na NVS.
 */
class ConfigurationPortal {
public:
  /** Constrói o portal com servidor HTTP na porta padrão e estado inativo. */
  explicit ConfigurationPortal(WifiCredentialStore &credentialStore);

  ////////////////////////////////

  /** Monta o LittleFS e inicia Access Point, DNS e servidor HTTP. */
  void begin();

  ////////////////////////////////

  /** Processa as próximas requisições DNS e HTTP recebidas pelo portal. */
  void loop();

  ////////////////////////////////

  /** Informa se o portal terminou sua inicialização e está pronto para uso. */
  bool isActive() const;

private:
  /** Porta padrão do DNS usada para o captive portal. */
  static constexpr uint16_t kDnsPort = 53;

  /** Nome do Access Point exposto quando não há conexão com rede conhecida. */
  static const char *const kAccessPointSsid;

  /** Senha do Access Point; deve ter pelo menos oito caracteres. */
  static const char *const kAccessPointPassword;

  WifiCredentialStore &credentialStore_;
  WebServer server_;
  DNSServer dnsServer_;
  bool active_;
  bool fileSystemMounted_;

  /** Registra todas as rotas HTTP servidas pelo portal. */
  void configureWebServerRoutes();

  ////////////////////////////////

  /** Envia ao navegador a página principal armazenada no LittleFS. */
  void handleRoot();

  ////////////////////////////////

  /** Faz o scan e retorna ao navegador as redes em formato JSON. */
  void handleNetworkScan();

  ////////////////////////////////

  /** Valida o formulário, salva as credenciais e reinicia o ESP32. */
  void handleSave();

  ////////////////////////////////

  /**
   * Envia um arquivo do LittleFS em fluxo, com tipo e política de cache dados.
   * Retorna false quando a partição ou o caminho não estão disponíveis.
   */
  bool sendFileFromLittleFs(const char *path, const char *contentType,
                            bool allowBrowserCache);

  ////////////////////////////////

  /** Redireciona URLs desconhecidas para a raiz do captive portal. */
  void handleNotFound();

  ////////////////////////////////

  /** Escapa um texto para que ele possa ser inserido com segurança no JSON. */
  static String escapeJson(const String &value);
};

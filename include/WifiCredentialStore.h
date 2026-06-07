#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "WifiCredential.h"

/**
 * Armazena e recupera as credenciais Wi-Fi na memória não volátil do ESP32.
 *
 * A classe concentra todo o conhecimento sobre o namespace e o formato das
 * chaves usadas na NVS. Dessa forma, os componentes responsáveis por conectar
 * e exibir o portal trabalham apenas com WifiCredential e não dependem de
 * detalhes da biblioteca Preferences.
 */
class WifiCredentialStore {
public:
  /** Limita o uso de RAM e a quantidade de tentativas feitas na inicialização.
   */
  static constexpr uint8_t kMaxKnownNetworks = 5;

  /** Carrega as credenciais válidas em destination e devolve sua quantidade. */
  uint8_t load(WifiCredential *destination, uint8_t capacity);

  ////////////////////////////////

  /** Salva ou atualiza uma credencial, mantendo a mais recente no início. */
  void save(const String &ssid, const String &password);

private:
  /** Namespace da NVS onde ficam a contagem, SSIDs e senhas. */
  static const char *const kPreferencesNamespace;

  Preferences preferences_;
};

#include "WifiCredentialStore.h"

constexpr uint8_t WifiCredentialStore::kMaxKnownNetworks;

const char *const WifiCredentialStore::kPreferencesNamespace = "wifi-config";

uint8_t WifiCredentialStore::load(WifiCredential *destination,
                                  uint8_t capacity) {
  /*
   * Abre o namespace em modo somente leitura. Esse modo protege a flash contra
   * gravações acidentais enquanto estamos apenas reconstruindo a lista em RAM.
   */
  preferences_.begin(kPreferencesNamespace, true);
  uint8_t savedCount = preferences_.getUChar("count", 0);

  /*
   * Limita o valor lido ao tamanho físico do array recebido. Essa proteção
   * evita acesso fora dos limites caso a NVS tenha sido escrita por outra
   * versão do firmware ou contenha um valor inválido.
   */
  if (savedCount > capacity) {
    savedCount = capacity;
  }

  /*
   * Cada posição utiliza duas chaves, por exemplo ssid0/pass0. Entradas sem
   * SSID são ignoradas e as válidas são compactadas no início do array para que
   * os próximos componentes possam percorrê-lo sem buracos.
   */
  uint8_t validCount = 0;
  for (uint8_t index = 0; index < savedCount; ++index) {
    const String ssidKey = "ssid" + String(index);
    const String passwordKey = "pass" + String(index);
    const String savedSsid = preferences_.getString(ssidKey.c_str(), "");

    if (savedSsid.isEmpty()) {
      continue;
    }

    destination[validCount].ssid = savedSsid;
    destination[validCount].password =
        preferences_.getString(passwordKey.c_str(), "");
    ++validCount;
  }

  /*
   * Fecha o namespace, liberando os recursos internos usados por Preferences, e
   * informa no monitor serial quantas redes realmente ficaram disponíveis.
   */
  preferences_.end();

  Serial.print("Redes Wi-Fi conhecidas: ");
  Serial.println(validCount);

  return validCount;
}

////////////////////////////////

void WifiCredentialStore::save(const String &ssid, const String &password) {
  /*
   * A rede recém-configurada ocupa a primeira posição, tornando-se a mais nova.
   * As credenciais anteriores são carregadas da NVS antes da escrita para que o
   * portal não dependa da ordem usada por uma tentativa de conexão anterior.
   */
  WifiCredential current[kMaxKnownNetworks];
  const uint8_t currentCount = load(current, kMaxKnownNetworks);

  WifiCredential updated[kMaxKnownNetworks];
  updated[0] = {ssid, password};
  uint8_t updatedCount = 1;

  /*
   * Copia as redes antigas sem duplicar o SSID recém-salvo. A condição de
   * limite também descarta naturalmente a rede mais antiga quando já existem
   * cinco.
   */
  for (uint8_t index = 0;
       index < currentCount && updatedCount < kMaxKnownNetworks; ++index) {
    if (current[index].ssid == ssid) {
      continue;
    }
    updated[updatedCount++] = current[index];
  }

  /*
   * Abre o namespace para escrita, salva a nova quantidade e persiste cada par
   * de SSID e senha em uma posição previsível. Preferences cuida da gravação na
   * NVS.
   */
  preferences_.begin(kPreferencesNamespace, false);
  preferences_.putUChar("count", updatedCount);

  for (uint8_t index = 0; index < updatedCount; ++index) {
    const String ssidKey = "ssid" + String(index);
    const String passwordKey = "pass" + String(index);
    preferences_.putString(ssidKey.c_str(), updated[index].ssid);
    preferences_.putString(passwordKey.c_str(), updated[index].password);
  }

  /*
   * Remove posições excedentes de uma lista anterior maior. Sem essa limpeza as
   * credenciais antigas não seriam usadas, mas continuariam ocupando a flash.
   */
  for (uint8_t index = updatedCount; index < kMaxKnownNetworks; ++index) {
    const String ssidKey = "ssid" + String(index);
    const String passwordKey = "pass" + String(index);
    preferences_.remove(ssidKey.c_str());
    preferences_.remove(passwordKey.c_str());
  }

  /* Encerra a sessão de escrita para garantir que os recursos sejam liberados.
   */
  preferences_.end();
}

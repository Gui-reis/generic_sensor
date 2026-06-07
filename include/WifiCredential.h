#pragma once

#include <Arduino.h>

/**
 * Representa somente os dados persistentes necessários para acessar uma rede.
 *
 * Informações temporárias produzidas durante um scan, como RSSI e visibilidade,
 * pertencem ao processo de conexão e não são armazenadas junto da credencial.
 */
struct WifiCredential {
  String ssid;
  String password;
};

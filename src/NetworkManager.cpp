#include "NetworkManager.h"

#include <WiFi.h>

// A página é mantida em PROGMEM para não ocupar desnecessariamente a RAM do
// microcontrolador. Ela usa apenas HTML/CSS/JavaScript local: o portal funciona
// mesmo que o ESP32 ainda não possua acesso à Internet.
namespace {
const char kConfigurationPage[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Configurar Wi-Fi do sensor</title>
  <style>
    :root { color-scheme: light; font-family: system-ui, sans-serif; }
    body { background: #f2f5f7; margin: 0; padding: 24px; color: #1f2933; }
    main { background: white; border-radius: 14px; box-shadow: 0 8px 30px #0002;
           margin: 8vh auto; max-width: 480px; padding: 28px; }
    h1 { font-size: 1.55rem; margin-top: 0; }
    p { color: #52606d; line-height: 1.5; }
    label { display: block; font-weight: 650; margin: 18px 0 7px; }
    select, input, button { box-sizing: border-box; border-radius: 8px;
                            font: inherit; padding: 12px; width: 100%; }
    select, input { background: white; border: 1px solid #bcccdc; }
    button { background: #1261a0; border: 0; color: white; cursor: pointer;
             font-weight: 700; margin-top: 22px; }
    button:disabled { background: #9fb3c8; cursor: wait; }
    #status { font-size: .92rem; min-height: 1.4em; }
  </style>
</head>
<body>
  <main>
    <h1>Configurar Wi-Fi do sensor</h1>
    <p>Escolha uma rede encontrada pelo ESP32. A rede será lembrada nas próximas inicializações.</p>
    <form action="/save" method="POST">
      <label for="ssid">Rede Wi-Fi</label>
      <select id="ssid" name="ssid" required disabled>
        <option value="">Procurando redes...</option>
      </select>
      <div id="status">O scan pode levar alguns segundos.</div>

      <label for="password">Senha</label>
      <input id="password" name="password" type="password" maxlength="63"
             autocomplete="current-password" placeholder="Deixe vazio para rede aberta">

      <button id="save" type="submit" disabled>Salvar e conectar</button>
    </form>
  </main>
  <script>
    const select = document.querySelector('#ssid');
    const status = document.querySelector('#status');
    const button = document.querySelector('#save');

    // O endpoint devolve um JSON pequeno gerado pelo próprio ESP32. Usar
    // textContent e elementos DOM, em vez de innerHTML, impede que nomes de rede
    // contendo caracteres especiais sejam interpretados como código HTML.
    fetch('/networks')
      .then(response => {
        if (!response.ok) throw new Error('Falha no scan');
        return response.json();
      })
      .then(networks => {
        select.textContent = '';
        if (!networks.length) {
          const option = document.createElement('option');
          option.textContent = 'Nenhuma rede encontrada';
          option.value = '';
          select.appendChild(option);
          status.textContent = 'Aproxime o sensor do roteador e recarregue a página.';
          return;
        }

        networks.forEach(network => {
          const option = document.createElement('option');
          option.value = network.ssid;
          option.textContent = `${network.ssid} (${network.rssi} dBm)${network.open ? ' — aberta' : ''}`;
          select.appendChild(option);
        });
        select.disabled = false;
        button.disabled = false;
        status.textContent = `${networks.length} rede(s) encontrada(s).`;
      })
      .catch(() => {
        status.textContent = 'Não foi possível procurar redes. Recarregue a página para tentar novamente.';
      });
  </script>
</body>
</html>
)HTML";

const char kSavedPage[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pt-BR">
<head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Configuração salva</title></head>
<body style="font-family:system-ui;text-align:center;padding:10vh 20px">
  <h1>Credenciais salvas!</h1>
  <p>O sensor será reiniciado e tentará conectar à rede selecionada.</p>
</body>
</html>
)HTML";
} // namespace

constexpr uint8_t NetworkManager::kMaxKnownNetworks;
constexpr uint32_t NetworkManager::kConnectionTimeoutMs;
constexpr uint16_t NetworkManager::kDnsPort;

const char *const NetworkManager::kPreferencesNamespace = "wifi-config";
const char *const NetworkManager::kAccessPointSsid = "Sensor-IoT-Setup";
const char *const NetworkManager::kAccessPointPassword = "12345678";

NetworkManager::NetworkManager()
    : server_(80), credentialCount_(0), portalActive_(false) {}

void NetworkManager::begin() {
  // Evita que WiFi.begin() grave também na configuração interna do framework.
  // A única fonte de verdade das credenciais passa a ser Preferences.
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  loadCredentials();

  if (connectToKnownNetwork()) {
    Serial.println("Wi-Fi conectado!");
    Serial.print("Rede: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP do ESP32: ");
    Serial.println(WiFi.localIP());
    return;
  }

  Serial.println("Nenhuma rede conhecida pôde ser conectada.");
  startConfigurationPortal();
}

void NetworkManager::loop() {
  if (!portalActive_) {
    return;
  }

  // handleClient() atende HTTP; processNextRequest() responde qualquer domínio
  // com o IP do AP, comportamento esperado de um captive portal simples.
  dnsServer_.processNextRequest();
  server_.handleClient();
}

bool NetworkManager::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool NetworkManager::isConfigurationPortalActive() const {
  return portalActive_;
}

void NetworkManager::loadCredentials() {
  preferences_.begin(kPreferencesNamespace, true);
  credentialCount_ = preferences_.getUChar("count", 0);

  // Protege o array caso uma versão anterior ou memória corrompida contenha um
  // valor maior que o limite aceito por este firmware.
  if (credentialCount_ > kMaxKnownNetworks) {
    credentialCount_ = kMaxKnownNetworks;
  }

  uint8_t validCount = 0;
  for (uint8_t index = 0; index < credentialCount_; ++index) {
    const String ssidKey = "ssid" + String(index);
    const String passwordKey = "pass" + String(index);
    const String savedSsid = preferences_.getString(ssidKey.c_str(), "");

    if (savedSsid.isEmpty()) {
      continue;
    }

    credentials_[validCount].ssid = savedSsid;
    credentials_[validCount].password =
        preferences_.getString(passwordKey.c_str(), "");
    credentials_[validCount].rssi = INT32_MIN;
    credentials_[validCount].visible = false;
    ++validCount;
  }

  credentialCount_ = validCount;
  preferences_.end();

  Serial.print("Redes Wi-Fi conhecidas: ");
  Serial.println(credentialCount_);
}

void NetworkManager::saveCredential(const String &ssid,
                                    const String &password) {
  // A rede recém-configurada vai para o início da lista. Se ela já existia,
  // remove-se a versão antiga; se a lista estava cheia, a mais antiga sai.
  Credential updated[kMaxKnownNetworks];
  updated[0] = {ssid, password, INT32_MIN, false};
  uint8_t updatedCount = 1;

  for (uint8_t index = 0;
       index < credentialCount_ && updatedCount < kMaxKnownNetworks; ++index) {
    if (credentials_[index].ssid == ssid) {
      continue;
    }
    updated[updatedCount++] = credentials_[index];
  }

  preferences_.begin(kPreferencesNamespace, false);
  preferences_.putUChar("count", updatedCount);

  for (uint8_t index = 0; index < updatedCount; ++index) {
    const String ssidKey = "ssid" + String(index);
    const String passwordKey = "pass" + String(index);
    preferences_.putString(ssidKey.c_str(), updated[index].ssid);
    preferences_.putString(passwordKey.c_str(), updated[index].password);
  }

  // Limpa slots que possam ter sobrado de uma lista maior anterior.
  for (uint8_t index = updatedCount; index < kMaxKnownNetworks; ++index) {
    const String ssidKey = "ssid" + String(index);
    const String passwordKey = "pass" + String(index);
    preferences_.remove(ssidKey.c_str());
    preferences_.remove(passwordKey.c_str());
  }
  preferences_.end();
}

bool NetworkManager::connectToKnownNetwork() {
  if (credentialCount_ == 0) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  updateVisibilityAndSortBySignal();

  // Redes visíveis aparecem primeiro, ordenadas pelo RSSI. Redes ocultas ou
  // temporariamente fora do scan continuam sendo tentadas ao final da lista.
  for (uint8_t index = 0; index < credentialCount_; ++index) {
    if (tryConnection(credentials_[index])) {
      return true;
    }
  }

  WiFi.disconnect();
  return false;
}

bool NetworkManager::tryConnection(const Credential &credential) {
  Serial.print("Tentando conectar a '");
  Serial.print(credential.ssid);
  Serial.print("'");

  WiFi.disconnect();
  WiFi.begin(credential.ssid.c_str(), credential.password.c_str());

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < kConnectionTimeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  return WiFi.status() == WL_CONNECTED;
}

void NetworkManager::updateVisibilityAndSortBySignal() {
  Serial.println("Procurando redes Wi-Fi conhecidas nas proximidades...");
  const int16_t networkCount = WiFi.scanNetworks(false, true);

  for (uint8_t credentialIndex = 0; credentialIndex < credentialCount_;
       ++credentialIndex) {
    credentials_[credentialIndex].visible = false;
    credentials_[credentialIndex].rssi = INT32_MIN;

    for (int16_t networkIndex = 0; networkIndex < networkCount;
         ++networkIndex) {
      if (WiFi.SSID(networkIndex) == credentials_[credentialIndex].ssid) {
        credentials_[credentialIndex].visible = true;
        if (WiFi.RSSI(networkIndex) > credentials_[credentialIndex].rssi) {
          credentials_[credentialIndex].rssi = WiFi.RSSI(networkIndex);
        }
      }
    }
  }

  WiFi.scanDelete();

  // O número máximo é cinco, portanto uma ordenação simples mantém o código
  // pequeno e previsível sem adicionar dependências ou alocações dinâmicas.
  for (uint8_t left = 0; left < credentialCount_; ++left) {
    for (uint8_t right = left + 1; right < credentialCount_; ++right) {
      const bool rightHasPriority =
          (credentials_[right].visible && !credentials_[left].visible) ||
          (credentials_[right].visible == credentials_[left].visible &&
           credentials_[right].rssi > credentials_[left].rssi);

      if (rightHasPriority) {
        const Credential temporary = credentials_[left];
        credentials_[left] = credentials_[right];
        credentials_[right] = temporary;
      }
    }
  }
}

void NetworkManager::startConfigurationPortal() {
  // WIFI_AP_STA mantém o AP ativo e permite que o rádio faça scans de redes no
  // endpoint /networks sem derrubar a conexão do usuário com o portal.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(kAccessPointSsid, kAccessPointPassword);

  configureWebServerRoutes();

  // Todas as consultas DNS são direcionadas para o ESP32. Muitos celulares
  // reconhecem isso e abrem automaticamente a tela de configuração.
  dnsServer_.start(kDnsPort, "*", WiFi.softAPIP());
  server_.begin();
  portalActive_ = true;

  Serial.println("Portal de configuração iniciado.");
  Serial.print("Conecte-se ao Wi-Fi: ");
  Serial.println(kAccessPointSsid);
  Serial.print("Senha do Access Point: ");
  Serial.println(kAccessPointPassword);
  Serial.print("Acesse: http://");
  Serial.println(WiFi.softAPIP());
}

void NetworkManager::configureWebServerRoutes() {
  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/networks", HTTP_GET, [this]() { handleNetworkScan(); });
  server_.on("/save", HTTP_POST, [this]() { handleSave(); });
  server_.onNotFound([this]() { handleNotFound(); });
}

void NetworkManager::handleRoot() {
  server_.send_P(200, "text/html; charset=utf-8", kConfigurationPage);
}

void NetworkManager::handleNetworkScan() {
  const int16_t networkCount = WiFi.scanNetworks(false, true);
  String json = "[";
  bool hasItem = false;

  // Reserva espaço aproximado para reduzir realocações e fragmentação da heap.
  if (networkCount > 0) {
    json.reserve(static_cast<size_t>(networkCount) * 70U + 2U);
  }

  for (int16_t index = 0; index < networkCount; ++index) {
    const String currentSsid = WiFi.SSID(index);
    if (currentSsid.isEmpty()) {
      continue; // Uma rede oculta não pode ser selecionada pelo nome no portal.
    }

    // Alguns roteadores aparecem mais de uma vez (um BSSID por access point).
    // Como o usuário escolhe pelo SSID, mostramos apenas uma entrada, usando a
    // leitura com melhor RSSI para representar aquela rede.
    int16_t bestIndexForSsid = index;
    for (int16_t candidate = 0; candidate < networkCount; ++candidate) {
      const bool sameSsid = WiFi.SSID(candidate) == currentSsid;
      const bool strongerSignal =
          WiFi.RSSI(candidate) > WiFi.RSSI(bestIndexForSsid);
      const bool sameSignalButEarlier =
          WiFi.RSSI(candidate) == WiFi.RSSI(bestIndexForSsid) &&
          candidate < bestIndexForSsid;

      if (sameSsid && (strongerSignal || sameSignalButEarlier)) {
        bestIndexForSsid = candidate;
      }
    }

    if (bestIndexForSsid != index) {
      continue;
    }

    if (hasItem) {
      json += ',';
    }
    hasItem = true;

    json += F("{\"ssid\":\"");
    json += escapeJson(currentSsid);
    json += F("\",\"rssi\":");
    json += String(WiFi.RSSI(index));
    json += F(",\"open\":");
    json +=
        (WiFi.encryptionType(index) == WIFI_AUTH_OPEN) ? F("true") : F("false");
    json += '}';
  }

  json += ']';
  WiFi.scanDelete();
  server_.send(200, "application/json; charset=utf-8", json);
}

void NetworkManager::handleSave() {
  String ssid = server_.arg("ssid");
  const String password = server_.arg("password");
  ssid.trim();

  if (ssid.isEmpty()) {
    server_.send(400, "text/plain; charset=utf-8", "Selecione uma rede Wi-Fi.");
    return;
  }

  if (ssid.length() > 32 || password.length() > 63) {
    server_.send(400, "text/plain; charset=utf-8",
                 "SSID ou senha excede o tamanho permitido pelo Wi-Fi.");
    return;
  }

  saveCredential(ssid, password);
  server_.send_P(200, "text/html; charset=utf-8", kSavedPage);

  // Dá tempo para o navegador receber a resposta antes da reinicialização.
  delay(1500);
  ESP.restart();
}

void NetworkManager::handleNotFound() {
  // Redirecionar URLs desconhecidas ajuda os detectores de captive portal de
  // Android, iOS e computadores a chegarem à página principal.
  server_.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(),
                     true);
  server_.send(302, "text/plain", "");
}

String NetworkManager::escapeJson(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t index = 0; index < value.length(); ++index) {
    const char character = value[index];
    switch (character) {
    case '\\':
      escaped += F("\\\\");
      break;
    case '"':
      escaped += F("\\\"");
      break;
    case '\n':
      escaped += F("\\n");
      break;
    case '\r':
      escaped += F("\\r");
      break;
    case '\t':
      escaped += F("\\t");
      break;
    default:
      // Caracteres de controle não são válidos diretamente em strings JSON.
      if (static_cast<uint8_t>(character) < 0x20) {
        escaped += '?';
      } else {
        escaped += character;
      }
    }
  }

  return escaped;
}

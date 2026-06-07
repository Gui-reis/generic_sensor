#include "NetworkManager.h"

#include <WiFi.h>

/*
 * A página é mantida em PROGMEM para não ocupar desnecessariamente a RAM do
 * microcontrolador. Ela usa apenas HTML, CSS e JavaScript locais, portanto o
 * portal funciona mesmo que o ESP32 ainda não possua acesso à Internet.
 */
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

    /*
     * O endpoint devolve um JSON pequeno gerado pelo próprio ESP32. Usar
     * textContent e elementos DOM, em vez de innerHTML, impede que nomes de rede
     * contendo caracteres especiais sejam interpretados como código HTML.
     */
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
} /* namespace */

constexpr uint8_t NetworkManager::kMaxKnownNetworks;
constexpr uint32_t NetworkManager::kConnectionTimeoutMs;
constexpr uint16_t NetworkManager::kDnsPort;

const char *const NetworkManager::kPreferencesNamespace = "wifi-config";
const char *const NetworkManager::kAccessPointSsid = "Sensor-IoT-Setup";
const char *const NetworkManager::kAccessPointPassword = "12345678";

NetworkManager::NetworkManager()
    : server_(80), credentialCount_(0), portalActive_(false) {
  /*
   * O servidor HTTP é construído na porta 80, que é a porta padrão acessada
   * pelos navegadores. A lista começa vazia e o portal começa desativado; esses
   * valores serão atualizados durante begin() conforme o estado da conexão.
   */
}

////////////////////////////////

void NetworkManager::begin() {
  /*
   * Desabilita a persistência automática da biblioteca WiFi para que ela não
   * mantenha uma segunda cópia das credenciais fora do nosso controle. A NVS
   * administrada por Preferences passa a ser a única fonte de verdade.
   * O auto reconnect continua habilitado para recuperar quedas momentâneas da
   * rede depois que uma conexão tiver sido estabelecida com sucesso.
   */
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  /*
   * Primeiro recupera as redes já cadastradas. Em seguida tenta conectar a cada
   * uma delas, priorizando as redes visíveis que possuem o melhor sinal.
   */
  loadCredentials();

  if (connectToKnownNetwork()) {
    /*
     * Uma conexão foi estabelecida, portanto o dispositivo pode seguir no modo
     * estação e não precisa expor o Access Point de configuração.
     */
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
  startConfigurationPortal();
}

////////////////////////////////

void NetworkManager::loop() {
  /*
   * Quando o portal não está ativo não há DNS nem requisições HTTP para tratar.
   * Retornar imediatamente mantém o custo deste método praticamente nulo
   * durante a operação normal do sensor conectado ao roteador.
   */
  if (!portalActive_) {
    return;
  }

  /*
   * O DNS responde os domínios consultados com o endereço do próprio ESP32,
   * ajudando celulares e computadores a detectar o captive portal. Logo depois,
   * handleClient() processa uma eventual requisição HTTP recebida pelo
   * servidor. Este método deve ser chamado continuamente pelo loop principal.
   */
  dnsServer_.processNextRequest();
  server_.handleClient();
}

////////////////////////////////

bool NetworkManager::isConnected() const {
  /*
   * A biblioteca WiFi é a autoridade sobre o estado real do rádio. Consultá-la
   * evita manter uma variável local que poderia ficar desatualizada após
   * quedas.
   */
  return WiFi.status() == WL_CONNECTED;
}

////////////////////////////////

bool NetworkManager::isConfigurationPortalActive() const {
  /*
   * portalActive_ é alterado somente depois que DNS e HTTP foram iniciados,
   * então true significa que loop() já pode atender o portal com segurança.
   */
  return portalActive_;
}

////////////////////////////////

void NetworkManager::loadCredentials() {
  /*
   * Abre o namespace em modo somente leitura. Esse modo protege a flash contra
   * gravações acidentais enquanto estamos apenas reconstruindo a lista em RAM.
   */
  preferences_.begin(kPreferencesNamespace, true);
  credentialCount_ = preferences_.getUChar("count", 0);

  /*
   * Limita o valor lido ao tamanho físico do array. Essa proteção evita acesso
   * fora dos limites caso a NVS tenha sido escrita por outra versão do firmware
   * ou contenha um valor inválido.
   */
  if (credentialCount_ > kMaxKnownNetworks) {
    credentialCount_ = kMaxKnownNetworks;
  }

  /*
   * Cada posição utiliza duas chaves, por exemplo ssid0/pass0. Entradas sem
   * SSID são ignoradas e as válidas são compactadas no início do array para que
   * os próximos métodos possam percorrê-lo sem buracos.
   */
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

    /*
     * RSSI e visibilidade são informações temporárias. Elas começam com os
     * valores mais desfavoráveis e serão preenchidas pelo próximo scan.
     */
    credentials_[validCount].rssi = INT32_MIN;
    credentials_[validCount].visible = false;
    ++validCount;
  }

  /*
   * Atualiza a contagem com o número realmente carregado e fecha o namespace,
   * liberando os recursos internos usados por Preferences.
   */
  credentialCount_ = validCount;
  preferences_.end();

  Serial.print("Redes Wi-Fi conhecidas: ");
  Serial.println(credentialCount_);
}

////////////////////////////////

void NetworkManager::saveCredential(const String &ssid,
                                    const String &password) {
  /*
   * A rede recém-configurada ocupa a primeira posição, tornando-se a mais nova.
   * O array temporário permite reorganizar a lista antes de tocar na flash.
   */
  Credential updated[kMaxKnownNetworks];
  updated[0] = {ssid, password, INT32_MIN, false};
  uint8_t updatedCount = 1;

  /*
   * Copia as redes antigas sem duplicar o SSID recém-salvo. A condição de
   * limite também descarta naturalmente a rede mais antiga quando já existem
   * cinco.
   */
  for (uint8_t index = 0;
       index < credentialCount_ && updatedCount < kMaxKnownNetworks; ++index) {
    if (credentials_[index].ssid == ssid) {
      continue;
    }
    updated[updatedCount++] = credentials_[index];
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

////////////////////////////////

bool NetworkManager::connectToKnownNetwork() {
  /*
   * Sem nenhuma credencial não existe conexão a tentar. O retorno false instrui
   * begin() a iniciar diretamente o portal de configuração.
   */
  if (credentialCount_ == 0) {
    return false;
  }

  /*
   * Coloca o rádio no modo estação, procura as redes próximas e reorganiza as
   * credenciais para tentar primeiro as opções com maior chance de sucesso.
   */
  WiFi.mode(WIFI_STA);
  updateVisibilityAndSortBySignal();

  /*
   * Percorre todas as redes conhecidas. As visíveis estão ordenadas por RSSI;
   * redes ocultas ou ausentes do scan permanecem no final e também são
   * tentadas.
   */
  for (uint8_t index = 0; index < credentialCount_; ++index) {
    if (tryConnection(credentials_[index])) {
      return true;
    }
  }

  /*
   * Nenhuma tentativa funcionou. Desconecta qualquer tentativa pendente antes
   * de o rádio ser reconfigurado como Access Point pelo fluxo de fallback.
   */
  WiFi.disconnect();
  return false;
}

////////////////////////////////

bool NetworkManager::tryConnection(const Credential &credential) {
  /* Exibe o SSID que está sendo testado sem jamais imprimir a senha. */
  Serial.print("Tentando conectar a '");
  Serial.print(credential.ssid);
  Serial.print("'");

  /*
   * Limpa o estado da tentativa anterior e inicia uma nova associação usando o
   * SSID e a senha recebidos da lista de credenciais conhecidas.
   */
  WiFi.disconnect();
  WiFi.begin(credential.ssid.c_str(), credential.password.c_str());

  /*
   * Aguarda somente até o tempo limite configurado. A subtração entre valores
   * de millis() continua correta mesmo quando o contador de 32 bits sofre
   * overflow.
   */
  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < kConnectionTimeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  /* Converte o estado final da biblioteca em sucesso ou falha da tentativa. */
  return WiFi.status() == WL_CONNECTED;
}

////////////////////////////////

void NetworkManager::updateVisibilityAndSortBySignal() {
  /*
   * Executa um scan síncrono e inclui redes ocultas no resultado. O retorno é a
   * quantidade de access points encontrados, ou um valor negativo em caso de
   * erro.
   */
  Serial.println("Procurando redes Wi-Fi conhecidas nas proximidades...");
  const int16_t networkCount = WiFi.scanNetworks(false, true);

  /*
   * Reinicializa os dados transitórios de cada credencial e procura SSIDs
   * iguais no resultado do scan. Se houver vários access points com o mesmo
   * nome, guarda o maior RSSI, isto é, o sinal menos negativo e portanto mais
   * forte.
   */
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

  /* Os resultados já foram copiados; libera a memória interna usada pelo scan.
   */
  WiFi.scanDelete();

  /*
   * Ordena no próprio array: redes visíveis vêm antes das não visíveis e, entre
   * redes visíveis, o maior RSSI ganha prioridade. Como existem no máximo cinco
   * elementos, a ordenação simples é pequena, previsível e suficiente.
   */
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

////////////////////////////////

void NetworkManager::startConfigurationPortal() {
  /*
   * O modo combinado AP + estação mantém o portal disponível para o usuário e,
   * ao mesmo tempo, permite que o rádio procure os roteadores próximos quando o
   * navegador solicitar o endpoint /networks.
   */
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(kAccessPointSsid, kAccessPointPassword);

  /* Registra todas as URLs antes de começar a aceitar requisições HTTP. */
  configureWebServerRoutes();

  /*
   * O DNS curinga responde qualquer domínio com o IP do ESP32. Esse
   * comportamento é usado pelos sistemas operacionais para detectar e abrir
   * captive portals.
   */
  dnsServer_.start(kDnsPort, "*", WiFi.softAPIP());

  /*
   * Inicia o servidor e só então marca o portal como ativo. Assim loop() nunca
   * tenta processar componentes que ainda não foram completamente configurados.
   */
  server_.begin();
  portalActive_ = true;

  /* Mostra no monitor serial todas as informações necessárias para o acesso. */
  Serial.println("Portal de configuração iniciado.");
  Serial.print("Conecte-se ao Wi-Fi: ");
  Serial.println(kAccessPointSsid);
  Serial.print("Senha do Access Point: ");
  Serial.println(kAccessPointPassword);
  Serial.print("Acesse: http://");
  Serial.println(WiFi.softAPIP());
}

////////////////////////////////

void NetworkManager::configureWebServerRoutes() {
  /*
   * Cada lambda captura this para encaminhar a requisição ao método da
   * instância. A raiz entrega a página, /networks produz o scan e /save recebe
   * o formulário. Qualquer outro endereço é tratado como uma tentativa de
   * acessar o captive portal.
   */
  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/networks", HTTP_GET, [this]() { handleNetworkScan(); });
  server_.on("/save", HTTP_POST, [this]() { handleSave(); });
  server_.onNotFound([this]() { handleNotFound(); });
}

////////////////////////////////

void NetworkManager::handleRoot() {
  /*
   * send_P lê o HTML diretamente da flash (PROGMEM), evitando criar uma cópia
   * completa da página na RAM antes de enviá-la ao navegador.
   */
  server_.send_P(200, "text/html; charset=utf-8", kConfigurationPage);
}

////////////////////////////////

void NetworkManager::handleNetworkScan() {
  /*
   * Faz um scan síncrono incluindo redes ocultas. Redes ocultas ainda aparecem
   * no rádio, mas serão descartadas abaixo porque não possuem um SSID
   * selecionável.
   */
  const int16_t networkCount = WiFi.scanNetworks(false, true);

  /*
   * Inicia manualmente um array JSON. A flag informa se já existe um objeto no
   * array e, portanto, se uma vírgula deve ser adicionada antes do próximo
   * item.
   */
  String json = "[";
  bool hasItem = false;

  /*
   * Reserva um tamanho aproximado para evitar várias realocações da String e
   * reduzir a fragmentação da heap enquanto o JSON é construído.
   */
  if (networkCount > 0) {
    json.reserve(static_cast<size_t>(networkCount) * 70U + 2U);
  }

  /* Percorre cada access point devolvido pelo rádio. */
  for (int16_t index = 0; index < networkCount; ++index) {
    const String currentSsid = WiFi.SSID(index);

    /* Uma rede sem nome não pode ser escolhida pelo campo select da página. */
    if (currentSsid.isEmpty()) {
      continue;
    }

    /*
     * Um mesmo SSID pode aparecer em vários access points. Procura a ocorrência
     * com sinal mais forte; em caso de empate escolhe a de menor índice para
     * que exatamente uma entrada represente aquela rede no JSON.
     */
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

    /* Ignora todas as ocorrências que não sejam a representante escolhida. */
    if (bestIndexForSsid != index) {
      continue;
    }

    /* Adiciona a vírgula somente entre objetos, nunca antes do primeiro. */
    if (hasItem) {
      json += ',';
    }
    hasItem = true;

    /*
     * Monta um objeto com nome, intensidade do sinal e informação de segurança.
     * O SSID passa por escapeJson() para não quebrar a sintaxe do documento.
     */
    json += F("{\"ssid\":\"");
    json += escapeJson(currentSsid);
    json += F("\",\"rssi\":");
    json += String(WiFi.RSSI(index));
    json += F(",\"open\":");
    json +=
        (WiFi.encryptionType(index) == WIFI_AUTH_OPEN) ? F("true") : F("false");
    json += '}';
  }

  /*
   * Fecha o array, libera os resultados do scan e envia a resposta com o tipo
   * de conteúdo correto para que response.json() possa interpretá-la no
   * navegador.
   */
  json += ']';
  WiFi.scanDelete();
  server_.send(200, "application/json; charset=utf-8", json);
}

////////////////////////////////

void NetworkManager::handleSave() {
  /*
   * Lê os campos enviados por POST. Somente o SSID é aparado: espaços podem
   * fazer parte de uma senha válida e, por isso, a senha deve permanecer
   * exatamente igual.
   */
  String ssid = server_.arg("ssid");
  const String password = server_.arg("password");
  ssid.trim();

  /* Rejeita formulários sem uma rede selecionada antes de gravar qualquer dado.
   */
  if (ssid.isEmpty()) {
    server_.send(400, "text/plain; charset=utf-8", "Selecione uma rede Wi-Fi.");
    return;
  }

  /*
   * Aplica os limites definidos pelo padrão Wi-Fi: SSID com até 32 bytes e
   * senha WPA/WPA2 com até 63 caracteres. Entradas maiores recebem HTTP 400.
   */
  if (ssid.length() > 32 || password.length() > 63) {
    server_.send(400, "text/plain; charset=utf-8",
                 "SSID ou senha excede o tamanho permitido pelo Wi-Fi.");
    return;
  }

  /* Persiste a rede somente depois que todas as validações foram aprovadas. */
  saveCredential(ssid, password);

  /* Confirma a operação ao navegador antes de reinicializar o microcontrolador.
   */
  server_.send_P(200, "text/html; charset=utf-8", kSavedPage);

  /*
   * A pequena espera permite que o pacote HTTP deixe o ESP32 antes do restart.
   * Após reiniciar, begin() carregará e tentará a credencial recém-salva.
   */
  delay(1500);
  ESP.restart();
}

////////////////////////////////

void NetworkManager::handleNotFound() {
  /*
   * Redireciona qualquer endereço desconhecido para a raiz do ESP32. Isso
   * atende tanto URLs digitadas pelo usuário quanto páginas de teste usadas por
   * Android, iOS e computadores para detectar a presença de um captive portal.
   */
  server_.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(),
                     true);
  server_.send(302, "text/plain", "");
}

////////////////////////////////

String NetworkManager::escapeJson(const String &value) {
  /*
   * Cria o texto de saída com uma pequena folga para as barras adicionais
   * usadas no escape. reserve() reduz cópias e realocações durante a montagem
   * da String.
   */
  String escaped;
  escaped.reserve(value.length() + 8);

  /* Examina cada byte do SSID e substitui os caracteres especiais do JSON. */
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
      /*
       * Outros caracteres de controle não são válidos diretamente em uma string
       * JSON. Eles são substituídos por '?' enquanto bytes imprimíveis,
       * inclusive os que compõem UTF-8, são copiados sem alteração.
       */
      if (static_cast<uint8_t>(character) < 0x20) {
        escaped += '?';
      } else {
        escaped += character;
      }
    }
  }

  /* Devolve uma string segura para ser inserida entre aspas no documento JSON.
   */
  return escaped;
}

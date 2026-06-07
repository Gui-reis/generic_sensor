#include "ConfigurationPortal.h"

#include <LittleFS.h>
#include <WiFi.h>

constexpr uint16_t ConfigurationPortal::kDnsPort;

const char *const ConfigurationPortal::kAccessPointSsid = "Sensor-IoT-Setup";
const char *const ConfigurationPortal::kAccessPointPassword = "12345678";

ConfigurationPortal::ConfigurationPortal(WifiCredentialStore &credentialStore)
    : credentialStore_(credentialStore), server_(80), active_(false),
      fileSystemMounted_(false) {
  /*
   * O servidor HTTP é construído na porta 80, que é a porta padrão acessada
   * pelos navegadores. O portal e o indicador do LittleFS começam desativados;
   * esses estados somente mudam depois que begin() inicializa cada recurso com
   * sucesso.
   */
}

////////////////////////////////

void ConfigurationPortal::begin() {
  /*
   * Monta a partição do LittleFS antes de começar a aceitar requisições. O
   * parâmetro true permite formatar a partição somente quando a montagem
   * inicial falhar, o que também prepara automaticamente uma flash ainda vazia.
   * Os arquivos de data/ continuam precisando ser enviados pelo target
   * uploadfs.
   */
  fileSystemMounted_ = LittleFS.begin(true);

  if (fileSystemMounted_) {
    Serial.println("LittleFS montado com sucesso.");
  } else {
    /*
     * A falha não impede a criação do Access Point. Os endpoints que dependem
     * de arquivos responderão com uma mensagem clara em vez de tentar acessar
     * um sistema de arquivos indisponível.
     */
    Serial.println("Não foi possível montar o LittleFS.");
  }

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
  active_ = true;

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

void ConfigurationPortal::loop() {
  /*
   * Quando o portal não está ativo não há DNS nem requisições HTTP para tratar.
   * Retornar imediatamente mantém o custo deste método praticamente nulo
   * durante a operação normal do sensor conectado ao roteador.
   */
  if (!active_) {
    return;
  }

  /*
   * O DNS responde os domínios consultados com o endereço do próprio ESP32,
   * ajudando celulares e computadores a detectar o captive portal. Logo depois,
   * handleClient() processa uma eventual requisição HTTP recebida pelo
   * servidor.
   */
  dnsServer_.processNextRequest();
  server_.handleClient();
}

////////////////////////////////

bool ConfigurationPortal::isActive() const {
  /*
   * active_ é alterado somente depois que DNS e HTTP foram iniciados, então
   * true significa que loop() já pode atender o portal com segurança.
   */
  return active_;
}

////////////////////////////////

void ConfigurationPortal::configureWebServerRoutes() {
  /*
   * Cada lambda captura this para encaminhar a requisição ao método da
   * instância. Além da página principal, CSS e JavaScript possuem URLs próprias
   * e são lidos diretamente do LittleFS. /networks produz o scan e /save recebe
   * o formulário. Qualquer outro endereço é tratado como uma tentativa de
   * acessar o captive portal.
   */
  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/styles.css", HTTP_GET, [this]() {
    sendFileFromLittleFs("/styles.css", "text/css; charset=utf-8", true);
  });
  server_.on("/script.js", HTTP_GET, [this]() {
    sendFileFromLittleFs("/script.js", "application/javascript; charset=utf-8",
                         true);
  });
  server_.on("/networks", HTTP_GET, [this]() { handleNetworkScan(); });
  server_.on("/save", HTTP_POST, [this]() { handleSave(); });
  server_.onNotFound([this]() { handleNotFound(); });
}

////////////////////////////////

void ConfigurationPortal::handleRoot() {
  /*
   * A página principal agora é um arquivo comum na partição LittleFS. O helper
   * abre o arquivo e entrega seu conteúdo em fluxo, sem montar uma grande
   * String em RAM e sem manter o HTML misturado ao código C++.
   */
  sendFileFromLittleFs("/index.html", "text/html; charset=utf-8", false);
}

////////////////////////////////

void ConfigurationPortal::handleNetworkScan() {
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

void ConfigurationPortal::handleSave() {
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
  credentialStore_.save(ssid, password);

  /*
   * Confirma a operação com uma segunda página armazenada no LittleFS. Se o
   * arquivo tiver sido removido por engano, o helper envia HTTP 500 e o restart
   * ainda acontece, pois a credencial já foi validada e salva corretamente.
   */
  sendFileFromLittleFs("/saved.html", "text/html; charset=utf-8", false);

  /*
   * A pequena espera permite que o pacote HTTP deixe o ESP32 antes do restart.
   * Após reiniciar, begin() carregará e tentará a credencial recém-salva.
   */
  delay(1500);
  ESP.restart();
}

////////////////////////////////

bool ConfigurationPortal::sendFileFromLittleFs(const char *path,
                                               const char *contentType,
                                               bool allowBrowserCache) {
  /*
   * Nunca tenta abrir um arquivo quando begin() não conseguiu montar a
   * partição. A resposta explícita facilita diagnosticar pelo navegador e pelo
   * monitor serial que o problema está no LittleFS, e não na rede ou no
   * WebServer.
   */
  if (!fileSystemMounted_) {
    Serial.println("LittleFS indisponível ao atender uma requisição HTTP.");
    server_.send(
        500, "text/plain; charset=utf-8",
        "LittleFS indisponível. Regrave o firmware e os arquivos do portal.");
    return false;
  }

  /*
   * Abre somente caminhos definidos pelo próprio firmware. Não usamos a URL da
   * requisição como caminho, impedindo que o navegador tente acessar arquivos
   * arbitrários da partição.
   */
  File file = LittleFS.open(path, "r");

  if (!file || file.isDirectory()) {
    /*
     * Um arquivo ausente normalmente significa que uploadfs não foi executado
     * depois da compilação. Registra o caminho para tornar o diagnóstico direto
     * mesmo quando o usuário enxerga apenas uma página de erro no navegador.
     */
    Serial.print("Arquivo não encontrado no LittleFS: ");
    Serial.println(path);
    file.close();
    server_.send(
        500, "text/plain; charset=utf-8",
        "Arquivo do portal não encontrado. Execute o upload do LittleFS.");
    return false;
  }

  /*
   * CSS e JavaScript podem ser reutilizados pelo cache do navegador durante a
   * sessão. As páginas HTML não são armazenadas para que uma atualização do
   * portal apareça imediatamente depois de recarregar o endereço.
   */
  if (allowBrowserCache) {
    server_.sendHeader("Cache-Control", "public, max-age=3600");
  } else {
    server_.sendHeader("Cache-Control", "no-store");
  }

  /*
   * streamFile() envia o arquivo em blocos e evita copiá-lo por inteiro para a
   * RAM. O File permanece aberto durante a transmissão e é fechado logo depois.
   */
  server_.streamFile(file, contentType);
  file.close();
  return true;
}

////////////////////////////////

void ConfigurationPortal::handleNotFound() {
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

String ConfigurationPortal::escapeJson(const String &value) {
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

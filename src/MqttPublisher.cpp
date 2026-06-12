#include "MqttPublisher.h"

#include <WiFi.h>

#include "MqttConfig.h"

MqttPublisher::MqttPublisher()
    : mqttClient_(secureClient_), clientId_{0}, lastConnectionAttemptAt_(0) {
  /*
   * PubSubClient não cria a própria conexão de rede: ele recebe secureClient_
   * como transporte e passa a enviar todos os pacotes MQTT pelo canal TLS dessa
   * instância. Os demais campos começam zerados até begin() preparar a classe.
   */
}

////////////////////////////////

void MqttPublisher::begin() {
  /*
   * A configuração padrão mantém MQTT desligado. Isso permite compilar e usar o
   * sensor sem criar MqttSecrets.h e evita tentativas contra um host vazio.
   */
  if (!mqtt_config::kEnabled) {
    Serial.println(
        "MQTT desativado. Configure include/MqttSecrets.h para habilitar.");
    return;
  }

  /*
   * Identifica cada placa pelo eFuse para impedir que dois dispositivos usem o
   * mesmo client ID e derrubem mutuamente suas sessões no broker. O valor é
   * dividido em duas partes porque snprintf não oferece o mesmo suporte a
   * inteiros de 64 bits em todas as versões do framework Arduino para ESP32.
   */
  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(clientId_, sizeof(clientId_), "esp32-sensor-%04X%08X",
           static_cast<uint16_t>(chipId >> 32),
           static_cast<uint32_t>(chipId));

  /*
   * Esta primeira integração usa TLS sem validar a autoridade certificadora.
   * Isso é adequado apenas para o teste inicial; produção deve usar setCACert()
   * e sincronizar o relógio antes de abrir a conexão segura.
   */
  secureClient_.setInsecure();

  /*
   * Informa ao PubSubClient onde abrir o transporte TCP/TLS. A conexão não é
   * iniciada aqui; a primeira tentativa será feita por loop() quando houver
   * uma rede Wi-Fi disponível.
   */
  mqttClient_.setServer(mqtt_config::kHost, mqtt_config::kPort);

  /*
   * Retrocede o marcador em um intervalo completo para que loop() possa tentar
   * conectar imediatamente. A aritmética sem sinal continua válida mesmo se o
   * contador de millis() estiver próximo de sofrer overflow.
   */
  lastConnectionAttemptAt_ = millis() - kReconnectIntervalMs;

  /*
   * Exibe somente endereço e porta para facilitar o diagnóstico. As credenciais
   * nunca são enviadas ao monitor serial.
   */
  Serial.print("MQTT configurado para ");
  Serial.print(mqtt_config::kHost);
  Serial.print(":");
  Serial.println(mqtt_config::kPort);
}

////////////////////////////////

void MqttPublisher::loop() {
  /*
   * Quando MQTT não foi habilitado, a classe permanece completamente inativa e
   * retorna rapidamente em todos os ciclos do firmware.
   */
  if (!mqtt_config::kEnabled) {
    return;
  }

  /*
   * MQTT depende da conexão de estação mantida pelo NetworkManager. Se o Wi-Fi
   * cair, encerra qualquer sessão que o PubSubClient ainda considere aberta e
   * aguarda a biblioteca WiFi informar uma nova conexão válida.
   */
  if (WiFi.status() != WL_CONNECTED) {
    if (mqttClient_.connected()) {
      mqttClient_.disconnect();
    }
    return;
  }

  /*
   * Em uma sessão ativa, PubSubClient::loop() processa os pacotes de controle e
   * keep-alives do protocolo. Chamá-lo com frequência evita que o broker
   * encerre uma conexão saudável por inatividade.
   */
  if (mqttClient_.connected()) {
    mqttClient_.loop();
    return;
  }

  /*
   * Uma falha de DNS, TLS ou autenticação não pode transformar o loop principal
   * em uma sequência contínua de reconexões. A diferença entre valores de
   * millis() também trata corretamente o overflow periódico do contador.
   */
  const uint32_t now = millis();
  if (now - lastConnectionAttemptAt_ < kReconnectIntervalMs) {
    return;
  }

  /*
   * Registra o instante antes de conectar, mantendo o intervalo mesmo se a
   * tentativa falhar imediatamente. connect() realiza somente uma tentativa.
   */
  lastConnectionAttemptAt_ = now;
  connect();
}

////////////////////////////////

bool MqttPublisher::publishTemperature(float temperatureCelsius) {
  /*
   * Publicar só é possível depois de begin() habilitar o componente e loop()
   * estabelecer uma sessão. A leitura continua sendo impressa normalmente se o
   * broker estiver indisponível, portanto uma falha MQTT não perde o ciclo
   * local.
   */
  if (!mqtt_config::kEnabled || !mqttClient_.connected()) {
    return false;
  }

  /*
   * Usa um buffer fixo em vez de várias concatenações com String. Isso deixa o
   * consumo de memória previsível e evita fragmentação do heap durante a
   * operação contínua do ESP32.
   */
  char payload[kPayloadSize];
  const int written = snprintf(
      payload, sizeof(payload),
      "{\"sensor\":\"%s\",\"temperatura_celsius\":%.2f}", clientId_,
      temperatureCelsius);

  /*
   * snprintf retorna um valor negativo em caso de erro ou o tamanho necessário
   * quando o buffer é pequeno. Nos dois casos, não publica um JSON incompleto.
   */
  if (written < 0 || static_cast<size_t>(written) >= sizeof(payload)) {
    Serial.println("Não foi possível montar o payload MQTT.");
    return false;
  }

  /*
   * PubSubClient copia o tópico e o payload para seu pacote de saída durante a
   * chamada, então o buffer local pode ser descartado quando a função terminar.
   */
  const bool published = mqttClient_.publish(mqtt_config::kTopic, payload);
  if (!published) {
    Serial.println("Falha ao publicar a temperatura via MQTT.");
    return false;
  }

  /*
   * Confirma a publicação no monitor serial para que o teste possa comparar o
   * tópico e o conteúdo observados no ESP32 com os recebidos no HiveMQ.
   */
  Serial.print("Mensagem MQTT enviada para '");
  Serial.print(mqtt_config::kTopic);
  Serial.print("': ");
  Serial.println(payload);
  return true;
}

////////////////////////////////

bool MqttPublisher::isConnected() {
  /*
   * PubSubClient é a fonte de verdade da sessão; consultar diretamente a
   * biblioteca evita manter um segundo estado que poderia ficar desatualizado.
   */
  return mqttClient_.connected();
}

////////////////////////////////

void MqttPublisher::connect() {
  /*
   * O client ID identifica a placa, enquanto usuário e senha autorizam o acesso
   * ao cluster HiveMQ. Nenhuma credencial é incluída nas mensagens de log.
   */
  Serial.print("Conectando ao broker MQTT... ");
  const bool connected = mqttClient_.connect(
      clientId_, mqtt_config::kUsername, mqtt_config::kPassword);

  /* Uma resposta positiva significa que a sessão está pronta para publicar. */
  if (connected) {
    Serial.println("conectado!");
    return;
  }

  /*
   * O código de estado do PubSubClient diferencia erros de transporte,
   * indisponibilidade do broker e recusas de autenticação durante o
   * diagnóstico.
   */
  Serial.print("falhou. Código: ");
  Serial.println(mqttClient_.state());
}

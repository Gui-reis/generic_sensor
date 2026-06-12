#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

/**
 * Mantém a conexão segura com o broker MQTT e publica leituras de temperatura.
 *
 * A classe esconde do restante da aplicação os detalhes de TLS, autenticação,
 * identificação do cliente, serialização do payload e manutenção da sessão
 * MQTT.
 * O main.cpp precisa apenas chamar begin() durante a inicialização, loop() em
 * todos os ciclos e publishTemperature() quando houver uma nova leitura válida.
 *
 * As tentativas de reconexão são espaçadas, sem laços ou delays bloqueantes.
 * Enquanto o Wi-Fi estiver indisponível, a classe aguarda a restauração da
 * rede pelo componente responsável pela conectividade. Assim, uma falha no
 * broker não interrompe as leituras nem o portal de configuração.
 */
class MqttPublisher {
public:
  /**
   * Conecta o PubSubClient ao mesmo cliente TLS mantido por esta instância e
   * inicializa o estado usado para controlar as futuras tentativas de conexão.
   */
  MqttPublisher();

  ////////////////////////////////

  /**
   * Carrega a configuração MQTT, cria um client ID único para este ESP32 e
   * prepara o endereço do broker. Não bloqueia aguardando Wi-Fi ou MQTT.
   */
  void begin();

  ////////////////////////////////

  /**
   * Mantém a sessão MQTT quando conectada e inicia no máximo uma tentativa de
   * reconexão por intervalo. Deve ser chamado frequentemente pelo loop
   * principal.
   */
  void loop();

  ////////////////////////////////

  /**
   * Monta um JSON com a temperatura e o instante UTC recebidos, então publica
   * no tópico configurado. Retorna false se MQTT não estiver conectado, se o
   * payload não couber no buffer ou se o broker rejeitar a publicação.
   */
  bool publishTemperature(float temperatureCelsius, const char *utcTimestamp);

  ////////////////////////////////

  /** Consulta o PubSubClient para informar o estado atual da sessão MQTT. */
  bool isConnected();

private:
  /** Tempo mínimo entre duas tentativas de conexão com o broker. */
  static constexpr uint32_t kReconnectIntervalMs = 5000;

  /**
   * Espaço reservado para o prefixo e os 12 dígitos hexadecimais do chip ID.
   */
  static constexpr size_t kClientIdSize = 32;

  /** Espaço fixo para serializar o JSON sem alocações dinâmicas de String. */
  static constexpr size_t kPayloadSize = 128;

  /** Executa uma única tentativa de autenticação e registra o resultado. */
  void connect();

  /** Canal TCP protegido por TLS que transporta os pacotes MQTT. */
  WiFiClientSecure secureClient_;

  /**
   * Implementação do protocolo MQTT que usa secureClient_ como transporte.
   */
  PubSubClient mqttClient_;

  /** Identificador estável e exclusivo apresentado ao broker por este ESP32. */
  char clientId_[kClientIdSize];

  /**
   * Instante da última tentativa, usado para evitar reconexões em loop
   * apertado.
   */
  uint32_t lastConnectionAttemptAt_;
};

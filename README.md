# Sensor de temperatura ESP32

Firmware PlatformIO para ESP32 que lê um sensor Dallas/DS18B20, gerencia a
conexão Wi-Fi sem exigir credenciais gravadas no código-fonte e publica as
leituras em um broker MQTT compatível com TLS, como o HiveMQ Cloud.

## Fluxo de inicialização

1. O ESP32 carrega da memória NVS até cinco redes configuradas anteriormente.
2. Faz um scan e tenta primeiro as redes conhecidas que estão visíveis, da mais
   forte para a mais fraca. Redes conhecidas que não apareceram no scan também
   são tentadas, o que permite usar redes com SSID oculto que já tenham sido
   salvas por uma versão anterior/configuração válida.
3. Se nenhuma conexão funcionar em até 12 segundos por rede, inicia o Access
   Point `Sensor-IoT-Setup`, protegido pela senha `12345678`.
4. Ao conectar ao Access Point, acesse `http://192.168.4.1`. O portal procura as
   redes próximas, permite escolher uma delas e salva a senha na NVS.
5. Depois de salvar, o ESP32 reinicia e tenta conectar novamente.

A leitura do sensor continua sendo executada e impressa no monitor serial mesmo
quando o portal de configuração está ativo. Quando há Wi-Fi e MQTT configurado,
a temperatura também é publicada a cada cinco segundos.

## Estrutura

- `src/main.cpp`: inicialização e orquestração dos componentes.
- `include/NetworkManager.h` e `src/NetworkManager.cpp`: fachada que orquestra
  a tentativa de conexão e ativa o portal quando nenhuma rede conhecida funciona.
- `include/WifiCredentialStore.h` e `src/WifiCredentialStore.cpp`: leitura,
  atualização e persistência das credenciais na NVS por meio de Preferences.
- `include/WifiConnector.h` e `src/WifiConnector.cpp`: scan, priorização por RSSI
  e tentativas de conexão às redes conhecidas.
- `include/ConfigurationPortal.h` e `src/ConfigurationPortal.cpp`: Access Point,
  DNS cativo, servidor HTTP, montagem do LittleFS e rotas de configuração.
- `data/index.html` e `data/saved.html`: páginas exibidas durante a configuração.
- `data/styles.css` e `data/script.js`: apresentação e comportamento do portal,
  mantidos separados do firmware e armazenados na partição LittleFS.
- `include/TemperatureSensor.h` e `src/TemperatureSensor.cpp`: encapsulamento do
  barramento OneWire e da biblioteca DallasTemperature.
- `include/MqttPublisher.h` e `src/MqttPublisher.cpp`: conexão TLS, manutenção da
  sessão MQTT, reconexão temporizada e publicação das temperaturas.
- `include/MqttConfig.h`: valores padrão seguros e carregamento opcional das
  credenciais locais definidas em `include/MqttSecrets.h`.
- `include/MqttSecrets.example.h`: modelo versionado para configurar o HiveMQ sem
  gravar usuário e senha reais no repositório.

## Configurar o HiveMQ Cloud

1. Crie um arquivo local a partir do modelo:

   ```sh
   cp include/MqttSecrets.example.h include/MqttSecrets.h
   ```

2. Edite `include/MqttSecrets.h` e informe o host, a porta, o usuário, a senha e
   o tópico do cluster. O arquivo real está no `.gitignore` e não deve ser
   commitado.
3. Compile e envie o firmware normalmente. O client ID é gerado a partir do
   identificador único do ESP32, no formato `esp32-sensor-...`.

A mensagem publicada tem este formato:

```json
{"sensor":"esp32-sensor-AABBCCDDEEFF","temperatura_celsius":23.75}
```

As leituras continuam ocorrendo a cada segundo, mas as publicações são limitadas
a uma a cada cinco segundos. Se a conexão cair, o firmware tenta restabelecer a
sessão MQTT a cada cinco segundos, sem laços de espera ou `delay()`.

> **Atenção:** esta primeira versão usa `WiFiClientSecure::setInsecure()` para
> facilitar o teste, portanto não valida a identidade do broker. Antes de usar o
> firmware em produção, configure o certificado raiz com `setCACert()` e
> sincronize o relógio do ESP32.

## Compilar e enviar

Com o PlatformIO instalado:

```sh
# Compila o firmware.
pio run

# Envia os arquivos de data/ para a partição LittleFS do ESP32.
pio run --target uploadfs

# Envia o firmware para a partição de aplicação.
pio run --target upload

# Abre o monitor serial.
pio device monitor --baud 115200
```

O firmware e o LittleFS ocupam partições diferentes da flash. Por isso,
`upload` não substitui `uploadfs`: sempre que algum HTML, CSS ou JavaScript do
diretório `data/` for alterado, execute novamente o target `uploadfs`. Se apenas
o código C++ mudar, basta enviar o firmware normalmente.

Ao iniciar o portal de configuração, o firmware monta o LittleFS antes de
aceitar requisições HTTP. Se a partição estiver nova ou inválida, ela pode ser
formatada automaticamente; nesse caso os arquivos do portal precisam ser enviados
novamente com
`pio run --target uploadfs`.

O pino de dados do DS18B20 permanece configurado como GPIO 4. Antes de usar o
firmware em produção, altere `kAccessPointPassword` em
`src/ConfigurationPortal.cpp` para uma senha própria com pelo menos oito
caracteres.

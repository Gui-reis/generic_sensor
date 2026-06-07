# Sensor de temperatura ESP32

Firmware PlatformIO para ESP32 que lê um sensor Dallas/DS18B20 e gerencia a
conexão Wi-Fi sem exigir credenciais gravadas no código-fonte.

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
quando o portal de configuração está ativo.

## Estrutura

- `src/main.cpp`: inicialização e orquestração dos componentes.
- `include/NetworkManager.h` e `src/NetworkManager.cpp`: redes conhecidas,
  Preferences/NVS, conexão, Access Point, DNS e portal HTTP.
- `include/TemperatureSensor.h` e `src/TemperatureSensor.cpp`: encapsulamento do
  barramento OneWire e da biblioteca DallasTemperature.

## Compilar e enviar

Com o PlatformIO instalado:

```sh
pio run
pio run --target upload
pio device monitor --baud 115200
```

O pino de dados do DS18B20 permanece configurado como GPIO 4. Antes de usar o
firmware em produção, altere `kAccessPointPassword` em
`src/NetworkManager.cpp` para uma senha própria com pelo menos oito caracteres.

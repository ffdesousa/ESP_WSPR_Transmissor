# ESP_WSPR_ver_3_006

Este firmware transforma um ESP8266 (por exemplo, o Wemos D1 Mini) em um beacon WSPR simples. Ele utiliza o gerador de frequência Si5351A para transmitir sinais WSPR com sincronização precisa via NTP, e dispõe de uma interface web para configuração e calibração remota.

---

## Sumário

- [Visão Geral](#visão-geral)
- [Requisitos de Hardware](#requisitos-de-hardware)
- [Requisitos de Software](#requisitos-de-software)
- [Funcionalidades](#funcionalidades)
- [Detalhes de Implementação](#detalhes-de-implementação)
  - [Sincronização de Tempo via NTP](#sincronização-de-tempo-via-ntp)
  - [Interface Web e Configuração](#interface-web-e-configuração)
  - [Modo de Calibração](#modo-de-calibração)
  - [Transmissão WSPR](#transmissão-wspr)
  - [Persistência com EEPROM](#persistência-com-eeprom)
- [Fluxo de Execução](#fluxo-de-execução)
- [Agradecimentos e Créditos](#agradecimentos-e-créditos)
- [Considerações Finais](#considerações-finais)

---

## Visão Geral

O firmware foi desenvolvido para:

- Transmitir sinais WSPR utilizando o Si5351A.
- Sincronizar o tempo via NTP para garantir transmissões precisas a cada 4 minutos.
- Permitir a configuração remota via interface web, onde o usuário pode ajustar credenciais WiFi, indicativo, grid, frequência e fator de calibração.
- Oferecer um modo de calibração interativo com ajustes finos enviados via AJAX, atualizando dinamicamente o botão (cor e texto) conforme o estado (ativado/desativado).

Comparando com a versão anterior, foram implementadas melhorias significativas na interface web (com AJAX para salvar configurações e controle do modo de calibração), no gerenciamento de conexões WiFi e na robustez geral do firmware. Essas modificações ampliam a usabilidade do sistema e permitem ajustes remotos sem redirecionamento indesejado.

---

## Requisitos de Hardware

- **ESP8266** (ex.: Wemos D1 Mini)
- **Si5351A** (conectado via I2C – pinos D1 para SCL e D2 para SDA)
- **Fonte de alimentação adequada**
- **Conexão WiFi**

---

## Requisitos de Software

- **Bibliotecas Necessárias:**
  - Etherkit Si5351
  - Etherkit JTEncode
  - Hora (TimeLib)
  - Wire (biblioteca padrão do Arduino)
  - NTPtimeESP
  - ESP8266WiFi
  - ESP8266WebServer
  - EEPROM
  - Outras bibliotecas padrão do Arduino

---

## Funcionalidades

- **Transmissão WSPR:**  
  Codifica os parâmetros (indicativo, grid e potência) via JTEncode e transmite os símbolos WSPR ajustando dinamicamente a frequência.

- **Sincronização NTP:**  
  Implementa dois métodos:
  - **Modo Bloqueante:** Aguarda a resposta do servidor NTP ou atinge um timeout.
  - **Modo Não Bloqueante:** Tenta obter o tempo sem travar a execução, permitindo continuidade em outras tarefas.

- **Interface Web para Configuração:**  
  Permite configurar:
  - Credenciais WiFi
  - Indicativo e grid locator
  - Frequência e fator de calibração  
  Além de reset de fábrica e controle do modo de calibração via AJAX, sem redirecionamento indesejado.

- **Modo de Calibração:**  
  Alterna entre ativar e desativar o modo de calibração. Quando ativado, o firmware reinicializa o Si5351A, configura o PLL e ativa a saída CLK0. O ajuste fino é realizado via requisições AJAX, com feedback visual no botão que muda de cor e texto para indicar o estado.
  
- **Persistência com EEPROM:**  
  Configurações e parâmetros de rádio são armazenados na EEPROM para garantir a persistência entre reinicializações.

---

## Detalhes de Implementação

### Sincronização de Tempo via NTP

- **Modo Bloqueante:**  
  A função `epochUnixNTP()` aguarda uma resposta válida ou um timeout (10 segundos) para sincronizar o relógio.

- **Modo Não Bloqueante:**  
  A função `nonBlockingEpochUnixNTP()` tenta obter o tempo sem interromper o fluxo do programa, verificando periodicamente a validade da sincronização.

### Interface Web e Configuração

- **Geração Dinâmica da Página HTML:**  
  A função `handleRoot()` monta uma página com formulários para configuração dos dados de rede e parâmetros do rádio.  
  Um array de bandas é utilizado para atualizar automaticamente a frequência conforme a seleção.  
- **Interatividade com AJAX:**  
  Requisições para salvar configurações, reset de fábrica e para alternar o modo de calibração são feitas via AJAX, garantindo uma interface responsiva sem redirecionamentos.

### Modo de Calibração

- **Ativação/Desativação:**  
  A função `handleStartCalibrate()` alterna o estado do modo de calibração:
  - **Ativado:** Reinicializa o Si5351A, configura o PLL, aplica a correção e ativa a saída CLK0.
  - **Desativado:** Desliga a saída CLK0.
- **Ajuste Fino:**  
  A função `vfo()` é chamada periodicamente (a cada 100 ms) para atualizar o fator de calibração conforme incrementos enviados para a rota `/adjustCal`.

### Transmissão WSPR

- **Codificação e Envio:**  
  A função `encode()` utiliza a biblioteca JTEncode para converter os dados no formato WSPR, ajustando a frequência para cada símbolo com base em um espaçamento fixo e transmitindo os sinais com um delay específico.

- **Sincronização de Transmissão:**  
  O loop principal verifica o tempo sincronizado via NTP e dispara a transmissão a cada 4 minutos, conforme as condições estabelecidas.

### Persistência com EEPROM

- **Armazenamento dos Parâmetros:**  
  Funções específicas (como `loadWiFiCredentials()`, `loadFreqAndCal()`, `saveFreqAndCal()` e `saveWiFiCredentials()`) são responsáveis por salvar e recuperar as configurações na EEPROM, garantindo que os dados persistam após reinicializações.

---

## Fluxo de Execução

1. **Setup:**
   - Inicializa a EEPROM, configura os pinos (incluindo o LED de transmissão) e a comunicação serial.
   - Carrega as credenciais WiFi e os parâmetros do rádio a partir da EEPROM.
   - Se não houver configuração de SSID, o dispositivo entra em modo Access Point para configuração via interface web.
   - Caso haja configuração, conecta-se à rede WiFi e inicia o servidor web.
   - Inicializa o Si5351A (se não estiver em modo de configuração) e define o provedor de sincronização do tempo.

2. **Loop:**
   - Processa as requisições HTTP do servidor web.
   - Se estiver em modo de calibração, executa a função `vfo()` periodicamente para realizar ajustes finos.
   - Caso contrário, gerencia a conexão WiFi, sincroniza o tempo via NTP e dispara a transmissão WSPR a cada 4 minutos.
   - Após cada transmissão, re-sincroniza o tempo para o próximo ciclo.

---

## Agradecimentos e Créditos

Este firmware é fruto de um trabalho colaborativo e integra as contribuições de diversos desenvolvedores e entusiastas do rádio amador:

- **Marco, IW5EJM:**  
  *Código Original de Multiband NODEMCU WSPR.*

- **Barbaros Asuroglu, WB2CBA:**  
  *Modificações para uma saída única, sincronização de tempo rápida via WiFi e alterações no flash de modo LED.*

- **Jason Milldrum, NT7S:**  
  *Desenvolvimento do Beacon*

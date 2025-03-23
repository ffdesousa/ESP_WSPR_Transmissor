//ESP_WSPR_ver_3_006

//Beacon WSPR simples para ESP8266 e placa Si5351A
//FIX CLK0 Saída para qualquer banda, com sincronização de tempo rápida via WiFi (servidor NTP).


//Informações de hardware
// ---------------------
//Taxa de transmissão da porta de depuração serial: 115200
//Si5351A é conectado via I2C no pino D1 (SCL) e D2 (SDA) como marcado no Wemos D1 mini Lite
//freq0 é usado na saída clock0.

//Requisitos de hardware
// ---------------------
//Este firmware deve ser executado em uma placa compatível com ESP8266
//testado em Wemos D1 Mini

//Bibliotecas necessárias
//------------------
//Etherkit Si5351 (Gerenciador de bibliotecas)
//Etherkit JTEncode (Gerenciador de bibliotecas)
//Hora (Gerenciador de bibliotecas)
//Wire (Biblioteca Padrão Arduino)
//NTPtimeESP
//ESP8266WiFi

//[ LICENÇA e CRÉDITOS ]*********************************************
//Código Original de Multiband NODEMCU WSPR por Marco,IW5EJM

//Modificado para uma saída, sincronização de tempo rápido e modificações de flash de modo LED por Barbaros Asuroglu, WB2CBA

//Beacon WSPR simples para ESP8266, com o Etherkit ou SV1AFN Si5351A Breakout
//Board, por Jason Milldrum NT7S.

//Tradução para português melhorias para garantir estabilidade de drift na banda de 10m, por Mater PU5ALE

//Modificado para confi

////Modificações e interface Web para configuração e calibrage, sincronização de tempo via NTP não bloqueante, persistência com EEPROM, modo de calibração (calibrador de MatePU5ALE). Fernando Fernandes, PU9FSO Email: pu9fso@gmail.com QRZ: https://www.qrz.com/db/Pu9fso. Fernando Fernandes, PU9FSO Email: pu9fso@gmail.com QRZ: https://www.qrz.com/db/Pu9fso

//Código original baseado em Feld Hell beacon para Arduino por Mark
//Vandewettering K6HX, adaptado para o Si5351A por Robert
//Liesenfeld AK6L <ak6l@ak6l.org>.  Configuração do temporizador
//código por Thomas Knutsen LA3PNA.

//Licença
// -------
//A permissão é concedida, gratuitamente, a qualquer pessoa que obtenha
//uma cópia deste software e dos arquivos de documentação associados (o
//"Software"), para lidar com o Software sem restrições, incluindo
//sem limitação os direitos de usar, copiar, modificar, fundir, publicar,
//distribuir, sublicenciar e/ou vender cópias do Software e
//permitir que as pessoas a quem o Software é fornecido o façam, sujeito
//às seguintes condições:

//O aviso de direitos autorais acima e este aviso de permissão serão
//incluído em todas as cópias ou partes substanciais do Software.

//O SOFTWARE É FORNECIDO "NO ESTADO EM QUE SE ENCONTRA", SEM GARANTIA DE QUALQUER TIPO,
//EXPRESSAS OU IMPLÍCITAS, INCLUINDO, MAS NÃO SE LIMITANDO ÀS GARANTIAS DE
//COMERCIALIZAÇÃO, ADEQUAÇÃO A UMA FINALIDADE ESPECÍFICA E NÃO VIOLAÇÃO.
//EM NENHUMA HIPÓTESE OS AUTORES OU DETENTORES DE DIREITOS AUTORAIS SERÃO RESPONSÁVEIS POR:
//QUALQUER RECLAMAÇÃO, DANOS OU OUTRA RESPONSABILIDADE, SEJA EM UMA AÇÃO DE
//CONTRATO, ATO ILÍCITO OU DE OUTRA FORMA, DECORRENTE DE, FORA OU EM CONEXÃO
//COM O SOFTWARE OU O USO OU OUTRAS NEGOCIAÇÕES NO SOFTWARE.




//*********************************[ BIBLIOTECAS ]*****************************************
#include <si5351.h>
#include "Wire.h"
#include <JTEncode.h>
#include <int.h>
#include <TimeLib.h>
#include <NTPtimeESP.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <stdlib.h>            // Necessário para strtoul()
#include <ESP8266WebServer.h>  // Biblioteca para servidor web

// Endereços na EEPROM para os parâmetros (4 bytes cada)
#define EEPROM_ADDR_FREQ 400  // Endereço para freq
#define EEPROM_ADDR_CAL 404   // Endereço para cal_factor

// Endereços na EEPROM para as credenciais Wi-Fi
#define EEPROM_ADDR_WIFI 0  // Gravação sequencial das credenciais Wi‑Fi

//******************************[ DEFINIÇÕES ]********************************************
#define TONE_SPACING 146  // ~1.46 Hz
#define WSPR_DELAY 683    // Delay para WSPR
#define WSPR_CTC 10672    // Valor CTC para WSPR
#define SYMBOL_COUNT WSPR_SYMBOL_COUNT

#define TX_LED_PIN 2  // LED integrado onboard

#define SEND_INTV 10
#define RECV_TIMEOUT 10

#define SI5351_REF 25000000UL  // frequência do cristal do Si5351

//**************************[ DECLARAÇÃO DE PARÂMETROS DO USUÁRIO ]***********************************
unsigned long freq = 28125215UL;  // Frequência de transmissão WSPR (Freq canal + 1,5 kHz +/- 100 Hz)
int32_t cal_factor = 0;           // Fator de calibração SI5351 para cada banda

int32_t old_cal = 0;
uint64_t rx_freq = 0;

// Dados de Wi-Fi e da estação
char ssid[32];      // SSID (até 31 caracteres + '\0')
char password[64];  // Senha (até 63 caracteres + '\0')
char call[7];       // Indicativo (até 6 caracteres + '\0')
char loc[5];        // GRID (até 4 caracteres + '\0')

#define TIME_ZONE -4.0f  // Diferença de fuso horário em relação ao UTC

//****************************[ DECLARAÇÃO DE VARIÁVEIS ]************************************************
uint8_t dbm = 23;
uint8_t tx_buffer[SYMBOL_COUNT];

const char* WiFi_hostname = "ESP_WSPR";

// Parâmetros para sincronização NTP e controle de tentativas
unsigned long lastNTPCheck = 0;
const unsigned long NTP_INTERVAL = 60000;  // Tenta sincronizar a cada 60 segundos quando possível
bool ntpFailed = false;
unsigned long lastNTPTry = 0;
const unsigned long NTP_RETRY_INTERVAL = 60000;  // Se falhar, tenta novamente após 60 segundos

// Variáveis para sincronização NTP não bloqueante
bool ntpSyncInProgress = false;
unsigned long ntpSyncStartTime = 0;
const unsigned long NTP_SYNC_TIMEOUT = 10000;  // 10 segundos de timeout

// Número de tentativas de conexão Wi-Fi
int tentativas = 0;

// Flag para indicar se estamos em modo de configuração (AP WIFI) e calibrção
bool configMode = false;
bool calibMode = false;
char ajustCall = '\0';
bool newAjust = false;

//*********************************[ BIBLIOTECAS NTP ]*****************************************
const char* NTP_Server = "ntp1.inrim.it";
NTPtime NTPch(NTP_Server);
strDateTime dateTime;

//*********************************[ OBJETOS PARA TRANSMISSÃO ]*****************************************
Si5351 si5351;
JTEncode jtencode;
bool warmup = false;

//*********************************[ OBJETO DO SERVIDOR WEB ]*****************************************
ESP8266WebServer server(80);

//*********************************[ FUNÇÃO DE CONFIGURAÇÃO WEB ]*****************************************
void handleRoot() {
  String page = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Configuração ESP_WSPR</title>";
  page += "<style>";
  page += "body { font-family: Arial, sans-serif; background-color: #f4f4f4; color: #333; margin: 0; padding: 20px; }";
  page += ".container { max-width: 400px; margin: 0 auto; padding: 0 20px; }";
  page += "h1, h2 { color: #0066cc; text-align: center; }";
  page += "form { background: #fff; padding: 20px; margin-bottom: 20px; border-radius: 5px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
  page += "#calButtonsContainer { background: #fff; padding: 20px; margin-bottom: 20px; border-radius: 5px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
  page += "input[type='text'], input[type='password'], input[type='number'], select { width: 90%; padding: 10px; margin: 5px; border: 1px solid #ccc; border-radius: 3px; }";
  page += "input[type='submit'], button { background-color: #0066cc; color: #fff; border: none; padding: 10px 20px; border-radius: 3px; cursor: pointer; margin-top: 10px; }";
  page += "input[type='submit'].resetBtn { background-color: red; }";
  page += "input[type='submit']:hover, button:hover { opacity: 0.9; }";
  // Estilos para o modal
  page += "#modal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.7); z-index: 1000; }";
  page += "#modalContent { position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); color: #fff; font-size: 20px; padding: 20px; background: #333; border-radius: 5px; }";
  // Container para botões de calibração (grid de duas colunas)
  page += "#calButtonsContainer { background: #fff; padding: 10px; display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }";
  page += "</style>";
  page += "<script>";

  // Array de bandas (valores em centésimos de Hz)
  page += "var bands = [";
  page += "  {name: '160m', start: 1838000, mid: 1838100, end: 1838200},";
  page += "  {name: '80m',  start: 3570000, mid: 3570100, end: 3570200},";
  page += "  {name: '60m',  start: 5288600, mid: 5288700, end: 5288800},";
  page += "  {name: '40m',  start: 7040000, mid: 7040100, end: 7040200},";
  page += "  {name: '30m',  start: 10140100, mid: 10140200, end: 10140300},";
  page += "  {name: '20m',  start: 14097000, mid: 14097100, end: 14097200},";
  page += "  {name: '17m',  start: 18106000, mid: 18106100, end: 18106200},";
  page += "  {name: '15m',  start: 21096000, mid: 21096100, end: 21096200},";
  page += "  {name: '12m',  start: 24926000, mid: 24926100, end: 24926200},";
  page += "  {name: '10m',  start: 28126000, mid: 28126100, end: 28126200}";
  page += "];";

  // Função: atualiza o input de frequência com base na banda selecionada
  page += "function updateFreqFromBand() {";
  page += "  var bandSelect = document.getElementById('bandSelect');";
  page += "  var freqInput = document.getElementById('freqInput');";
  page += "  var selectedMid = parseInt(bandSelect.value);";
  page += "  freqInput.value = selectedMid;";
  page += "}";

  // Função: atualiza a select box com base no valor do input de frequência
  page += "function updateBandFromFreq() {";
  page += "  var bandSelect = document.getElementById('bandSelect');";
  page += "  var freqInput = document.getElementById('freqInput');";
  page += "  var freqVal = parseInt(freqInput.value);";
  page += "  for (var i = 0; i < bands.length; i++) {";
  page += "    if (freqVal >= bands[i].start && freqVal <= bands[i].end) {";
  page += "      bandSelect.value = bands[i].mid;";
  page += "      return;";
  page += "    }";
  page += "  }";
  page += "}";

  page += "function showModal(message) {";
  page += "  document.getElementById('modalContent').innerText = message;";
  page += "  document.getElementById('modal').style.display = 'block';";
  page += "}";
  page += "function hideModal() {";
  page += "  document.getElementById('modal').style.display = 'none';";
  page += "}";

  page += "document.addEventListener('DOMContentLoaded', function() {";
  page += "  updateBandFromFreq();";
  page += "  document.getElementById('freqInput').addEventListener('blur', updateBandFromFreq);";
  page += "  document.getElementById('bandSelect').addEventListener('change', updateFreqFromBand);";

  // AJAX para salvar configurações
  page += "  const form = document.getElementById('configForm');";
  page += "  form.addEventListener('submit', function(e) {";
  page += "    e.preventDefault();";
  page += "    showModal('Salvando configurações, por favor aguarde...');";
  page += "    const formData = new FormData(form);";
  page += "    fetch('/save', { method: 'POST', body: formData })";
  page += "      .then(response => response.text())";
  page += "      .then(data => {";
  page += "         console.log(data);";
  page += "         setTimeout(function() { location.reload(); }, 2000);";
  page += "      })";
  page += "      .catch(error => {";
  page += "         console.error('Erro:', error);";
  page += "         hideModal();";
  page += "      });";
  page += "  });";

  // AJAX para reset de fábrica com confirmação
  page += "  const resetForm = document.getElementById('resetForm');";
  page += "  resetForm.addEventListener('submit', function(e) {";
  page += "    e.preventDefault();";
  page += "    if (confirm('Você realmente deseja redefinir as configurações para o padrão? O dispositivo entrará em modo AP para configuração.')) {";
  page += "      showModal('Executando Reset de Fábrica, por favor aguarde...');";
  page += "      fetch('/reset', { method: 'GET' })";
  page += "        .then(response => response.text())";
  page += "        .then(data => {";
  page += "           console.log(data);";
  page += "           setTimeout(function() { location.href = 'http://192.168.4.1/'; }, 2000);";
  page += "        })";
  page += "        .catch(error => {";
  page += "           console.error('Erro:', error);";
  page += "           hideModal();";
  page += "        });";
  page += "    }";
  page += "  });";



    // AJAX para reset de fábrica com confirmação
  page += "  const startCalibrateForm = document.getElementById('startCalibrateForm');";
  page += "  startCalibrateForm.addEventListener('submit', function(e) {";
  page += "    e.preventDefault();";
  page += "    if (confirm('Você realmente deseja ativar o modo de calibração? Caso sim, use os botoões de ajuste para calibrar a frequencia desejada, ao final clique em salvar!')) {";
  page += "      showModal('Ativando o modo de calibração, por favor aguarde...');";
  page += "      fetch('/start_calibrate', { method: 'GET' })";
  page += "        .then(response => response.text())";
  page += "        .then(data => {";
  page += "           console.log(data);";
  page += "           var btn = document.getElementById('calibrateBtn');";
  page += "           if(data === 'Ativado') {";
  page += "              btn.style.backgroundColor = 'red';";
  page += "              btn.innerText = 'Desativar Modo de Calibração';";
  page += "           }";
  page += "           if(data === 'Desativado') {";
  page += "              btn.style.backgroundColor = '#0066cc';";
  page += "              btn.innerText = 'Ativar Modo de Calibração';";
  page += "           }";
  page += "           hideModal();";
  page += "        })";
  page += "        .catch(error => {";
  page += "           console.error('Erro:', error);";
  page += "           hideModal();";
  page += "        });";
  page += "    }";
  page += "  });";

  page += "});";
  page += "function sendAdjust(inc) {";
  page += "  var xhr = new XMLHttpRequest();";
  page += "  xhr.open('POST', '/adjustCal', true);";
  page += "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');";
  page += "  xhr.onreadystatechange = function() {";
  page += "    if (xhr.readyState === 4 && xhr.status === 200) {";
  page += "      document.getElementById('calDisplay').innerText = xhr.responseText;";
  page += "      document.getElementById('rxDisplay').innerText = 'RX: ' + xhr.responseText.split('|')[0];";
  page += "      var parts = xhr.responseText.split('|');";
  page += "      if (parts.length >= 2) {";
  page += "        document.getElementById('calInput').value = parts[1].trim();";
  page += "      }";
  page += "    }";
  page += "  };";
  page += "  xhr.send('inc=' + inc);";
  page += "}";

  page += "</script>";
  page += "</head><body>";
  page += "<div class='container'>";
  page += "<h1>Configuração do Dispositivo</h1>";
  // Formulário para salvar configurações
  page += "<form id='configForm' method='POST' action='/save'>";
  page += "Nome da rede WiFi: <br><input type='text' name='ssid' value='" + String(ssid) + "'><br>";
  page += "Senha: <br><input type='password' name='password' value='" + String(password) + "'><br>";
  page += "Indicativo: <br><input type='text' name='call' value='" + String(call) + "'><br>";
  page += "Grid Locator: <br><input type='text' name='loc' value='" + String(loc) + "'><br>";
  // Seção para seleção de banda e entrada de frequência
  page += "Banda: <br><select id='bandSelect' name='band'>";
  page += "<option value='1838100'>160m</option>";
  page += "<option value='3570100'>80m</option>";
  page += "<option value='5288700'>60m</option>";
  page += "<option value='7040100'>40m</option>";
  page += "<option value='10140200'>30m</option>";
  page += "<option value='14097100'>20m</option>";
  page += "<option value='18106100'>17m</option>";
  page += "<option value='21096100'>15m</option>";
  page += "<option value='24926100'>12m</option>";
  page += "<option value='28126100'>10m</option>";
  page += "</select><br>";
  page += "Frequência: <br><input id='freqInput' type='number' name='freq' value='" + String(freq) + "'><br>";
  page += "Fator de calibração: <br><input id='calInput' type='number' name='cal_factor' value='" + String(cal_factor) + "'><br>";
  page += "<input type='submit' value='Salvar'>";
  page += "</form>";
 
  // Nova seção: Calibração
  page += "<div style='text-align: center; margin-top:20px;'>";
  page += "<h2>Calibração</h2>";
  page += "<form id='startCalibrateForm' method='GET' action='/start_calibrate'>";
  page += "<button id='calibrateBtn' type='submit'>Ativar Modo Calibração</button>";
  page += "</form>";
  page += "<p>Ajuste a calibração:</p>";
  page += "<div id=\"calDisplay\"></div>";
  page += "<div id=\"rxDisplay\"></div>";
  page += "<div id='calButtonsContainer'>";
  page += "<button onclick=\"sendAdjust('r')\">+0.01 Hz</button>";
  page += "<button onclick=\"sendAdjust('f')\">-0.01 Hz</button>";
  page += "<button onclick=\"sendAdjust('t')\">+0.1 Hz</button>";
  page += "<button onclick=\"sendAdjust('g')\">-0.1 Hz</button>";
  page += "<button onclick=\"sendAdjust('y')\">+1 Hz</button>";
  page += "<button onclick=\"sendAdjust('h')\">-1 Hz</button>";
  page += "<button onclick=\"sendAdjust('u')\">+10 Hz</button>";
  page += "<button onclick=\"sendAdjust('j')\">-10 Hz</button>";
  page += "<button onclick=\"sendAdjust('i')\">+100 Hz</button>";
  page += "<button onclick=\"sendAdjust('k')\">-100 Hz</button>";
  page += "<button onclick=\"sendAdjust('o')\">+1 Khz</button>";
  page += "<button onclick=\"sendAdjust('l')\">-1 Khz</button>";
  page += "<button onclick=\"sendAdjust('p')\">+10 Khz</button>";
  page += "<button onclick=\"sendAdjust('a')\">-10 Khz</button>";
  page += "</div>";
  page += "</div>";
 
  // Formulário para reset de fábrica
  page += "<div style='text-align: center; margin-top:20px;'>";
  page += "<form id='resetForm' method='GET' action='/reset'>";
  page += "<input class='resetBtn' type='submit' value='Redefinir configuração para padrão'>";
  page += "</form>";
  page += "</div>";

  page += "</div>";
  page += "<div id='modal'><div id='modalContent'>Carregando...</div></div>";
  page += "</body></html>";
  server.send(200, "text/html", page);
}



void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password") && server.hasArg("call") && server.hasArg("loc") && server.hasArg("freq") && server.hasArg("cal_factor")) {
    String newSSID = server.arg("ssid");
    String newPass = server.arg("password");
    String newCall = server.arg("call");
    String newLoc = server.arg("loc");
    unsigned long newFreq = strtoul(server.arg("freq").c_str(), NULL, 10);
    int newCal = server.arg("cal_factor").toInt();

    // Salva as credenciais Wi-Fi
    int addr = EEPROM_ADDR_WIFI;
    for (int i = 0; i < newSSID.length(); i++) {
      EEPROM.write(addr++, newSSID[i]);
    }
    EEPROM.write(addr++, '\0');
    for (int i = 0; i < newPass.length(); i++) {
      EEPROM.write(addr++, newPass[i]);
    }
    EEPROM.write(addr++, '\0');
    for (int i = 0; i < newCall.length(); i++) {
      EEPROM.write(addr++, newCall[i]);
    }
    EEPROM.write(addr++, '\0');
    for (int i = 0; i < newLoc.length(); i++) {
      EEPROM.write(addr++, newLoc[i]);
    }
    EEPROM.write(addr++, '\0');
    EEPROM.commit();

    // Salva os parâmetros de rádio
    EEPROM.put(EEPROM_ADDR_FREQ, newFreq);
    EEPROM.put(EEPROM_ADDR_CAL, newCal);
    EEPROM.commit();

    server.send(200, "text/html", "<html><body><h1>Configurações Salvas!</h1>Reiniciando...</body></html>");
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/html", "Parâmetros incompletos");
  }
}

void handleReset() {
  // Exemplo: limpar a EEPROM ou restaurar valores padrão.
  // Aqui, limpamos a EEPROM (opcional: pode-se também sobrescrever com valores padrão)
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  server.send(200, "text/html", "<html><body><h1>Reset de Fábrica Efetuado!</h1>Reiniciando...</body></html>");
  delay(2000);
  ESP.restart();
}

void handleStartCalibrate() {
  if (!calibMode) {
    calibMode = true;
    Serial.println("- Modo de calibração iniciado...");
    server.send(200, "text/html", "Ativado");
    si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
    si5351.set_correction(cal_factor, SI5351_PLL_INPUT_XO);
    si5351.set_pll(SI5351_PLL_FIXED, SI5351_PLLA);
    si5351.set_freq(freq * 100, SI5351_CLK0);
    si5351.set_clock_pwr(SI5351_CLK0, 1); // Ativa CLK0 explicitamente
  } else {
    calibMode = false;
    Serial.println("- Modo de calibração desativado...");
    server.send(200, "text/html", "Desativado");
    si5351.set_clock_pwr(SI5351_CLK0, 0); // Desativa CLK0 ao sair
  }
}

// Rota: ajusta incrementalmente a calibração (AJAX)
void handleAdjustCal() {
  if (server.hasArg("inc")) {
    ajustCall = server.arg("inc").charAt(0);
    newAjust = true;
  } else {
    server.send(400, "text/plain", "Parâmetro 'inc' ausente");
  }
}

void startAPMode() {
  // Inicia o modo AP com um SSID fixo para configuração
  WiFi.mode(WIFI_AP);

  // Define o IP, gateway e sub-rede para o AP
  IPAddress apIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);


  WiFi.softAPConfig(apIP, gateway, subnet);
  WiFi.softAP("ESP_WSPR_Config");

  Serial.println("Modo AP iniciado: conecte-se ao SSID 'ESP_WSPR_Config'");
  Serial.println("Acesse http://192.168.4.1/ para configurar o dispositivo.");



  // Registra as rotas do servidor web
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_GET, handleReset);  // Rota para reset de fábrica
  server.on("/start_calibrate", HTTP_GET, handleStartCalibrate);
  server.on("/adjustCal", HTTP_POST, handleAdjustCal);
  server.begin();
  Serial.println("Servidor web de configuração iniciado.");
}

//*********************************[ FUNÇÃO DE SINCRONIA DE TEMPO (BLOQUEANTE) ]******************************************
time_t epochUnixNTP() {
  digitalWrite(TX_LED_PIN, LOW);
  Serial.println("- NTP Time Sync...");
  Serial.print("- NTP Time:");

  NTPch.setSendInterval(SEND_INTV);
  NTPch.setRecvTimeout(RECV_TIMEOUT);

  unsigned long startTime = millis();
  const unsigned long timeout = 10000;  // 10 segundos de timeout

  while (millis() - startTime < timeout) {
    dateTime = NTPch.getNTPtime(TIME_ZONE, 1);
    if (dateTime.valid) {
      ntpFailed = false;
      break;
    }
    delay(1);
  }

  if (!dateTime.valid) {
    ntpFailed = true;
    lastNTPTry = millis();
    Serial.println();
    Serial.println("Falha na sincronização NTP. Possível ausência de acesso à internet!");
    Serial.println("Para ajustar as credenciais Wi-Fi, digite 'wifi' e pressione Enter.");
    digitalWrite(TX_LED_PIN, HIGH);
    return now();
  }

  NTPch.printDateTime(dateTime);
  setTime(dateTime.hour, dateTime.minute, dateTime.second, dateTime.day, dateTime.month, dateTime.year);
  Serial.println(now());
  digitalWrite(TX_LED_PIN, HIGH);
  return 0;
}

//*********************************[ FUNÇÃO DE SINCRONIA DE TEMPO (NÃO BLOQUEANTE) ]******************************************
void nonBlockingEpochUnixNTP() {
  if (!ntpSyncInProgress) {
    ntpSyncStartTime = millis();
    ntpSyncInProgress = true;
    Serial.println("- Iniciando sincronização NTP (não bloqueante)...");
  }

  dateTime = NTPch.getNTPtime(TIME_ZONE, 0);  // Tenta obter o tempo sem bloquear
  if (dateTime.valid) {
    ntpSyncInProgress = false;
    ntpFailed = false;
    Serial.println("- NTP Time recebido com sucesso.");
    NTPch.printDateTime(dateTime);
    setTime(dateTime.hour, dateTime.minute, dateTime.second, dateTime.day, dateTime.month, dateTime.year);
    Serial.print("Tempo sincronizado: ");
    Serial.println(now());
  } else {
    unsigned long elapsed = millis() - ntpSyncStartTime;
    if (elapsed > NTP_SYNC_TIMEOUT) {
      ntpSyncInProgress = false;
      lastNTPTry = millis();
      Serial.println();
      Serial.println("Timeout na sincronização NTP. Possível ausência de acesso à internet!");
      Serial.println(String("Acesse http://") + WiFi.localIP().toString() + String("/ para configurar o dispositivo."));
    }
    ntpFailed = true;
  }
}

//*********************************[ FUNÇÃO DE CODIFICAÇÃO E TRANSMISSÃO WSPR ]**********************************************
void encode() {
  uint8_t i;
  jtencode.wspr_encode(call, loc, dbm, tx_buffer);
  digitalWrite(TX_LED_PIN, LOW);
  Serial.println("- TX ON - STARTING TRANSMISSION...");
  for (i = 0; i < SYMBOL_COUNT; i++) {
    si5351.set_freq((freq * 100) + (tx_buffer[i] * TONE_SPACING), SI5351_CLK0);
    delay(WSPR_DELAY);
  }
  si5351.set_clock_pwr(SI5351_CLK0, 0);
  digitalWrite(TX_LED_PIN, HIGH);
  Serial.println("- TX OFF - END OF TRANSMISSION...");
}

//*********************************[ FUNÇÕES DE GRAVAÇÃO/LEITURA DA EEPROM ]**********************************
void saveFreqAndCal(unsigned long f, int32_t cal) {
  EEPROM.put(EEPROM_ADDR_FREQ, f);
  EEPROM.put(EEPROM_ADDR_CAL, cal);
  EEPROM.commit();
  Serial.println("Parâmetros freq e cal_factor salvos na EEPROM.");
  Serial.println("Reiniciando o dispositivo...");
  delay(500);
  ESP.restart();
}

void loadFreqAndCal() {
  EEPROM.get(EEPROM_ADDR_FREQ, freq);
  EEPROM.get(EEPROM_ADDR_CAL, cal_factor);
  Serial.print("Carregado freq: ");
  Serial.println(freq);
  Serial.print("Carregado cal_factor: ");
  Serial.println(cal_factor);
}

void saveWiFiCredentials(const char* _ssid, const char* _password, const char* _call, const char* _loc) {
  int addr = EEPROM_ADDR_WIFI;
  for (int i = 0; i < strlen(_ssid); i++) {
    EEPROM.write(addr++, _ssid[i]);
  }
  EEPROM.write(addr++, '\0');
  for (int i = 0; i < strlen(_password); i++) {
    EEPROM.write(addr++, _password[i]);
  }
  EEPROM.write(addr++, '\0');
  for (int i = 0; i < strlen(_call); i++) {
    EEPROM.write(addr++, _call[i]);
  }
  EEPROM.write(addr++, '\0');
  for (int i = 0; i < strlen(_loc); i++) {
    EEPROM.write(addr++, _loc[i]);
  }
  EEPROM.write(addr++, '\0');
  EEPROM.commit();
  Serial.println("Credenciais Wi-Fi salvas na EEPROM.");
  Serial.println("Reiniciando o dispositivo para aplicar as novas credenciais...");
  delay(500);
  ESP.restart();
}

void loadWiFiCredentials() {
  int addr = EEPROM_ADDR_WIFI;
  int i = 0;
  while (EEPROM.read(addr) != '\0' && i < 31) {
    ssid[i++] = EEPROM.read(addr++);
  }
  ssid[i] = '\0';
  addr++;
  i = 0;
  while (EEPROM.read(addr) != '\0' && i < 63) {
    password[i++] = EEPROM.read(addr++);
  }
  password[i] = '\0';
  addr++;
  i = 0;
  while (EEPROM.read(addr) != '\0' && i < 6) {
    call[i++] = EEPROM.read(addr++);
  }
  call[i] = '\0';
  addr++;
  i = 0;
  while (EEPROM.read(addr) != '\0' && i < 4) {
    loc[i++] = EEPROM.read(addr++);
  }
  loc[i] = '\0';
}

//*********************************[ FUNÇÕES DE CONEXÃO WIFI ]****************************************
void tryWiFiConnection() {
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado ao Wi-Fi!");

  } else {
    Serial.println("\nFalha na conexão com o Wi-Fi.");
  }
}

void connectWiFiWithRetries() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.println("Conexão perdida. Tentando reconectar...");
  while (tentativas < 2 && WiFi.status() != WL_CONNECTED) {
    tryWiFiConnection();
    tentativas++;
    if (WiFi.status() == WL_CONNECTED) break;
    Serial.print("Tentativa ");
    Serial.print(tentativas);
    Serial.println(" falhou.");
    delay(2000);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Não foi possível reconectar automaticamente.");
  }
}

void connectWiFiNonBlocking() {
  static unsigned long lastAttempt = 0;
  if (WiFi.status() != WL_CONNECTED && (millis() - lastAttempt > 10000)) {
    Serial.println("Tentando conectar ao Wi-Fi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    lastAttempt = millis();
  }
}



//**************************************[ FUNÇÃO SETUP ]*************************************************
void setup() {
  EEPROM.begin(512);
  pinMode(TX_LED_PIN, OUTPUT);
  digitalWrite(TX_LED_PIN, LOW);

  Serial.begin(115200);
  while (!Serial)
    ;

  Serial.println("*************************************************************");
  Serial.println("[ RIDICULOUS SIMPLE WSPR TRANSMITTER - ESP_WSPR_ver_3_006 ]");
  Serial.println("- Configuração SERIAL COM concluída!");
  delay(10);

  loadWiFiCredentials();
  loadFreqAndCal();

  // Se não houver SSID salvo, entra em modo de configuração AP
  if (ssid[0] == '\0') {
    configMode = true;
    Serial.println("Nenhuma configuração encontrada. Entrando no modo de configuração AP.");
    startAPMode();
  } else {
    // Se houver configurações, tenta conectar como cliente
    WiFi.mode(WIFI_STA);
    tryWiFiConnection();
    // Também inicia o servidor web para ajustes remotos se desejado
    server.begin();
    Serial.println("Servidor web iniciado.");
  }

  // Inicializa o módulo Si5351 (somente se não estiver em modo de configuração)
  if (!configMode) {
    Serial.println("- A CONFIGURAÇÃO DO MÓDULO DE RÁDIO começa...");
    si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
    si5351.set_correction(cal_factor, SI5351_PLL_INPUT_XO);
    Serial.println("- Módulo RÁDIO inicializando...");
    si5351.set_freq(freq * 100, SI5351_CLK0);
    si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
    si5351.set_clock_pwr(SI5351_CLK0, 0);
    Serial.println("- Configuração do módulo de rádio bem-sucedida...");
    digitalWrite(TX_LED_PIN, HIGH);
    Serial.println("Sistema iniciado.");
    Serial.println(String("Acesse http://") + WiFi.localIP().toString() + String("/ para configurar o dispositivo."));
  }


  // Registra as rotas do servidor web sempre, seja em AP ou STA:
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_GET, handleReset);
  server.on("/start_calibrate", HTTP_GET, handleStartCalibrate);
  server.on("/adjustCal", HTTP_POST, handleAdjustCal);
  server.begin();
  Serial.println("Servidor web iniciado.");
}



static void vfo(void) {
  rx_freq = freq * 100;
  old_cal = cal_factor;

  if (newAjust) {
    newAjust = false;
    switch (ajustCall) {
      case 'r': rx_freq += 1; break;
      case 'f': rx_freq -= 1; break;
      case 't': rx_freq += 10; break;
      case 'g': rx_freq -= 10; break;
      case 'y': rx_freq += 100; break;
      case 'h': rx_freq -= 100; break;
      case 'u': rx_freq += 1000; break;
      case 'j': rx_freq -= 1000; break;
      case 'i': rx_freq += 10000; break;
      case 'k': rx_freq -= 10000; break;
      case 'o': rx_freq += 100000; break;
      case 'l': rx_freq -= 100000; break;
      case 'p': rx_freq += 1000000; break;
      case 'a': rx_freq -= 1000000; break;
    }
    ajustCall = '\0';
    cal_factor = (int32_t)((freq * 100) - rx_freq) + old_cal;
    server.send(200, "text/plain", String(rx_freq) + " | " + String(cal_factor));
    Serial.print(F("Diferença atual:"));
    Serial.println(cal_factor);

    // Aplica a correção apenas quando há ajuste
    si5351.set_correction(cal_factor, SI5351_PLL_INPUT_XO);
    si5351.set_freq(freq * 100, SI5351_CLK0);
  }
}


//*****************************************[ FUNÇÃO LOOP ]******************************************************
void loop() {
  // O servidor web fica ativo em qualquer modo
  server.handleClient();

  // Se estiver em modo de configuração, o restante do loop não é executado
  if (configMode) return;

  // Se estiver em modo de calibração, o restante não é executado
 if (calibMode) {
    static unsigned long lastVfoUpdate = 0;
    if (millis() - lastVfoUpdate > 100) { // Atualiza a cada 100ms
      si5351.update_status();
      vfo();
      lastVfoUpdate = millis();
    }
    return;
  }


  //Transmissor
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFiNonBlocking();
  } else {
    if (ntpFailed) {
      if (millis() - lastNTPTry > NTP_RETRY_INTERVAL) {
        nonBlockingEpochUnixNTP();
        lastNTPCheck = millis();
      }
    } else {
      if (millis() - lastNTPCheck > NTP_INTERVAL) {
        Serial.println("Tentando sincronizar NTP...");
        nonBlockingEpochUnixNTP();
        lastNTPCheck = millis();
      }
    }
  }

  if (!ntpFailed) {
    // Transmissão WSPR: inicia a cada 4 minutos, no primeiro segundo do minuto par
    if ((minute() + 1) % 4 == 0 && second() == 10 && !warmup) {
      warmup = 1;
      Serial.println("- Aquecimento do módulo de rádio iniciado...");
      si5351.set_freq(freq * 100, SI5351_CLK0);
      si5351.set_clock_pwr(SI5351_CLK0, 1);
    }
    if (minute() % 4 == 0 && second() == 0) {
      Serial.print("- Horário de início da transmissão:");
      Serial.println(now());
      encode();
      warmup = 0;
      delay(4000);
      // Após transmissão, sincroniza novamente
      setSyncProvider(epochUnixNTP);
    }
  }

  delay(10);
}

//*************************************[ END OF PROGRAM ]

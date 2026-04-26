/**
 * Projeto: Sistema de Monitoramento Térmico com Dashboard Web
 * Descrição: Realiza a leitura de temperatura via sensor DS18B20, exibe dados em um LCD 16x2 
 * e disponibiliza um dashboard web em tempo real através do ESP32.
 */

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>

// --- Credenciais da Rede Wi-Fi ---
const char* ssid = "";
const char* password = "";

// --- Configurações de Hardware ---
#define SENSOR_PIN 4                  // Pino de dados do sensor DS18B20
OneWire oneWire(SENSOR_PIN);          // Inicializa o barramento OneWire
DallasTemperature sensors(&oneWire);  // Passa a referência OneWire para a biblioteca Dallas
LiquidCrystal_I2C lcd(0x27, 16, 2);   // Configura o LCD (Endereço 0x27, 16 colunas, 2 linhas)
AsyncWebServer server(80);            // Inicializa o servidor web na porta 80

// --- Variáveis de Controle Global ---
float tempAtual = 0.0;     // Armazena a leitura térmica atual
float tempMax = 0.0;       // Armazena o limite definido pelo usuário
bool configurado = false;  // Flag de controle de configuração

// --- Interface do Dashboard (HTML/CSS/JS) ---
// Armazenado na memória PROGMEM para economizar memória RAM
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Bancada Térmica Mosson</title>
  <style>
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #f4f7f6; color: #333; text-align: center; }
    .card { background: white; padding: 2rem; border-radius: 15px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); display: inline-block; margin-top: 50px; min-width: 300px; }
    .temp { font-size: 4rem; font-weight: bold; color: #2c3e50; }
    .status { font-size: 1.2rem; margin-top: 10px; padding: 10px; border-radius: 5px; }
    .normal { background: #e8f5e9; color: #2e7d32; }
    .alerta { background: #ffebee; color: #c62828; animation: blink 1s infinite; }
    @keyframes blink { 50% { opacity: 0.7; } }
  </style>
</head>
<body>
  <div class="card">
    <h2>Monitoramento Ativo</h2>
    <div id="temp-box" class="temp"><span id="valor">--</span>°C</div>
    <div id="status-box" class="status normal">SISTEMA ESTÁVEL</div>
    <p>Limite de Segurança: <span id="limite">--</span>°C</p>
  </div>

  <script>
    // Função JavaScript para buscar dados do ESP32 a cada 2 segundos
    setInterval(function ( ) {
      fetch('/data').then(response => response.json()).then(json => {
        document.getElementById("valor").innerHTML = json.temp.toFixed(1);
        document.getElementById("limite").innerHTML = json.max.toFixed(1);
        
        const box = document.getElementById("temp-box");
        const status = document.getElementById("status-box");
        
        // Lógica de alerta visual no navegador
        if(json.temp > json.max) {
          status.innerHTML = "ALERTA: TEMP CRÍTICA!";
          status.className = "status alerta";
          box.style.color = "#c62828";
        } else {
          status.innerHTML = "SISTEMA ESTÁVEL";
          status.className = "status normal";
          box.style.color = "#2c3e50";
        }
      });
    }, 2000);
  </script>
</body></html>)rawliteral";

void setup() {
    Serial.begin(115200);
    
   // --- Inicialização de Periféricos ---
  lcd.init();
    lcd.backlight();
    sensors.begin();

    lcd.setCursor(0, 0);
    lcd.print("Aguardando Setup");

     // --- Entrada de Dados via Serial ---
    // O sistema aguarda a definição da temperatura máxima antes de prosseguir
  Serial.println("\n[SISTEMA DE PROTEÇÃO TÉRMICA]");
    Serial.println("Digite a temperatura máxima permitida (°C):");
    
  while (tempMax <= 0) {
        if (Serial.available() > 0) {
            tempMax = Serial.parseFloat();
         
    }
        delay(100);
     
  }
    
  Serial.print("Limite definido: ");
    Serial.println(tempMax);

     // --- Procedimento de Conexão Wi-Fi ---
  lcd.clear();
    lcd.print("Conectando WiFi");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
     
  }
    
  Serial.println("\nWiFi Conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

  // Exibe o IP no LCD para facilitar o acesso ao Dashboard
    lcd.clear();
    lcd.print("IP: ");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());

     // --- Definição das Rotas do Servidor Web ---
   // Rota principal para carregar a página HTML
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", index_html);
     
  });

     // Rota de API que retorna os dados em formato JSON
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json = "{\"temp\":" + String(tempAtual) + ",\"max\":" + String(tempMax) + "}";
        request->send(200, "application/json", json);
     
  });

    server.begin();
    delay(3000);
}

void loop() {
  // Solicita a leitura da temperatura ao sensor
    sensors.requestTemperatures();
    tempAtual = sensors.getTempCByIndex(0);

     // --- Atualização de Dados no Display LCD ---
  lcd.setCursor(0, 0);
    lcd.print("Temp: ");
    lcd.print(tempAtual, 1);
    lcd.print("C   ");

    lcd.setCursor(0, 1);
  // Verificação de segurança: Alerta se a temperatura ultrapassar o limite
    if (tempAtual > tempMax) {
        lcd.print("ALERTA CRITICO! ");
     
  }
  else {
        lcd.print("Limite: ");
        lcd.print(tempMax, 0);
        lcd.print("C   ");
     
  }

    delay(2000);  // Intervalo de atualização do loop
}
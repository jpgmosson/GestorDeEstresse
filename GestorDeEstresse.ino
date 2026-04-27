#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>

// --- Credenciais Wi-Fi ---
const char* ssid = "Samsung Galaxy S3+";
const char* password = "joaolindo";

// --- Hardware ---
#define SENSOR_PIN 4
OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);
LiquidCrystal_I2C lcd(0x27, 16, 2);
AsyncWebServer server(80);

// --- Variáveis Globais ---
float tempAtual = 0.0;
float tempMax = 0.0;

// --- Dashboard HTML/CSS/JS ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Bancada Termica Mosson</title>
  <style>
    body { font-family: 'Segoe UI', sans-serif; background: #f4f7f6; color: #333; text-align: center; }
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
    <div id="status-box" class="status normal">SISTEMA ESTAVEL</div>
    <p>Limite de Seguranca: <span id="limite">--</span>°C</p>
  </div>
  <script>
    setInterval(function ( ) {
      fetch('/data').then(response => response.json()).then(json => {
        document.getElementById("valor").innerHTML = json.temp.toFixed(1);
        document.getElementById("limite").innerHTML = json.max.toFixed(1);
        const box = document.getElementById("temp-box");
        const status = document.getElementById("status-box");
        if(json.temp > json.max) {
          status.innerHTML = "ALERTA: TEMP CRITICA!";
          status.className = "status alerta";
          box.style.color = "#c62828";
        } else {
          status.innerHTML = "SISTEMA ESTAVEL";
          status.className = "status normal";
          box.style.color = "#2c3e50";
        }
      });
    }, 2000);
  </script>
</body></html>)rawliteral";

void setup() {
  Serial.begin(115200);
  
  // Inicializa Perifericos
  lcd.init();
  lcd.backlight();
  sensors.begin();

  lcd.setCursor(0, 0);
  lcd.print("Aguardando Setup");

  // --- Input via Serial ---
  Serial.println("\n[SISTEMA DE PROTECAO TERMICA]");
  Serial.println("Digite a temperatura maxima permitida (C):");
  
  while (tempMax <= 0) {
    if (Serial.available() > 0) {
      tempMax = Serial.parseFloat();
    }
    delay(100);
  }
  
  Serial.print("Limite definido: ");
  Serial.println(tempMax);

  // --- Conexao WiFi ---
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

  lcd.clear();
  lcd.print("IP: ");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());

  // --- Rotas do Servidor ---
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{\"temp\":" + String(tempAtual) + ",\"max\":" + String(tempMax) + "}";
    request->send(200, "application/json", json);
  });

  server.begin();
  delay(3000);
}

void loop() {
  sensors.requestTemperatures();
  tempAtual = sensors.getTempCByIndex(0);

  // Atualiza LCD
  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  lcd.print(tempAtual, 1);
  lcd.print("C   ");

  lcd.setCursor(0, 1);
  if (tempAtual >= tempMax) {
    lcd.print("ALERTA CRITICO! ");
  } else {
    lcd.print("Limite: ");
    lcd.print(tempMax, 0);
    lcd.print("C   ");
  }

  delay(2000);
}
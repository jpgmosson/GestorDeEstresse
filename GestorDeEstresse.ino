/**
 * Projeto: Sistema Ciberfisico de Monitoramento Termico (TDE)
 * Disciplina: Performance em Sistemas Ciberfisicos (PUCPR)
 * Autor: João Pedro Gadens Mosson
 */

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

const char *ssid = "joao";
const char *password = "jjjhhhmmm";

#define SENSOR_PIN 4
#define PIN_RELE 12
#define PIN_BUZZER 26
#define PIN_SERVO 19
#define PIN_LDR_ANALOG 34

// --- CALIBRAÇÃO DO LDR (VALOR BRUTO) ---
int LIMITE_LDR = 2000;  // Valor que separa o "Gabinete Fechado" do "Gabinete Aberto"

OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);
LiquidCrystal_I2C lcd(0x27, 16, 2);
AsyncWebServer server(80);
Servo escotilha;

float tempAtual = 0.0;
float tempMax = 0.0;
int rawLDR = 0;  // Leitura direta do ADC (0 a 4095)
volatile unsigned long hw_uptime_seconds = 0;

bool sistemaAtivo = false;
bool gabineteViolado = false;

TaskHandle_t TaskSensorHandle;
TaskHandle_t TaskControlHandle;
TaskHandle_t TaskDisplayHandle;
TaskHandle_t TaskSegurancaHandle;

hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  hw_uptime_seconds++;
  portEXIT_CRITICAL_ISR(&timerMux);
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>TDE - Performance Mosson</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    :root { --primary: #2c3e50; --success: #2e7d32; --danger: #c62828; --warning: #f39c12; --bg: #f4f7f6; }
    body { font-family: 'Segoe UI', Tahoma, sans-serif; background: var(--bg); color: #333; margin: 0; padding: 20px; }
    .header { text-align: center; margin-bottom: 20px; }
    .tabs { display: flex; justify-content: center; gap: 10px; margin-bottom: 20px; }
    .tab-btn { padding: 10px 20px; border: none; background: #ddd; border-radius: 5px; cursor: pointer; font-weight: bold; transition: 0.3s; }
    .tab-btn.active { background: var(--primary); color: white; }
    .tab-content { display: none; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); max-width: 800px; margin: 0 auto; }
    .tab-content.active { display: block; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; text-align: center; }
    .card { padding: 20px; border-radius: 10px; background: #f9f9f9; border: 1px solid #eee; }
    .temp-value { font-size: 3rem; font-weight: bold; color: var(--primary); }
    .status { padding: 10px; border-radius: 5px; font-weight: bold; margin-top: 10px; }
    .normal { background: #e8f5e9; color: var(--success); }
    .alerta { background: #ffebee; color: var(--danger); animation: blink 1s infinite; }
    .chart-container { position: relative; height: 250px; width: 100%; margin-top: 20px; border: 1px solid #ddd; border-radius: 10px; padding: 10px; background: white;}
    @keyframes blink { 50% { opacity: 0.7; } }
  </style>
</head>
<body>
  <div class="header"><h1>Painel Ciberfísico - ESP32</h1></div>

  <div class="tabs">
    <button class="tab-btn active" onclick="openTab('monitor')">Monitoramento</button>
    <button class="tab-btn" onclick="openTab('stats')">Performance (Nerds)</button>
    <button class="tab-btn" onclick="openTab('about')">Sobre</button>
  </div>

  <div id="monitor" class="tab-content active">
    <div class="grid">
      <div class="card">
        <h3>Temperatura</h3>
        <div id="temp-box" class="temp-value"><span id="valor">--</span>°C</div>
        <p>Limite Setado: <span id="limite">--</span>°C</p>
      </div>
      <div class="card">
        <h3>Status Térmico</h3>
        <div id="status-box" class="status normal">SISTEMA ESTÁVEL</div>
        <p>Ventoinha: <span id="rele-status">Desligada</span></p>
        <p>Escotilha: <span id="servo-status">Fechada</span></p>
      </div>
      <div class="card">
        <h3>Segurança (LDR)</h3>
        <div id="seguranca-box" class="status normal">GABINETE SEGURO</div>
        <p>Valor Atual: <span id="luz-status" style="font-weight: bold; color: var(--warning); font-size: 1.5em;">--</span></p>
      </div>
    </div>
    
    <div class="chart-container"><canvas id="tempChart"></canvas></div>
    <div class="chart-container"><canvas id="luzChart"></canvas></div>
  </div>

  <div id="stats" class="tab-content">
    <h2>Análise de Recursos do Sistema</h2>
    <div class="grid">
      <div class="card"><h4>RAM Livre</h4><div class="temp-value"><span id="heap">--</span> KB</div></div>
      <div class="card"><h4>Pico (Min RAM)</h4><div class="temp-value"><span id="min-heap">--</span> KB</div></div>
      <div class="card"><h4>CPU</h4><div class="temp-value"><span id="cpu">--</span> MHz</div></div>
      <div class="card"><h4>Uptime</h4><div class="temp-value"><span id="uptime">--</span> s</div></div>
      <div class="card"><h4>Tasks SO</h4><div class="temp-value"><span id="tasks">--</span></div></div>
      <div class="card"><h4>Prio App</h4><div style="font-size: 1.3rem; font-weight: bold; color: var(--primary); margin-top: 15px;"><span id="prio">--</span></div></div>
    </div>
  </div>

  <div id="about" class="tab-content">
    <h2>Informações Acadêmicas</h2>
    <ul>
      <li><strong>Instituição:</strong> PUCPR</li>
      <li><strong>Curso:</strong> Engenharia de Software</li>
      <li><strong>Equipe:</strong> João Pedro Gadens Mosson, João Pedro Magri Pozzan, Eduardo Skoroboatei Gomes, André Murilo Pinz Gomes, Nicole Harumi Futikami</li>
    </ul>
  </div>

  <script>
    function openTab(tabId) {
      document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));
      document.querySelectorAll('.tab-btn').forEach(btn => btn.classList.remove('active'));
      document.getElementById(tabId).classList.add('active');
      event.currentTarget.classList.add('active');
    }

    const ctxTemp = document.getElementById('tempChart').getContext('2d');
    const tempChart = new Chart(ctxTemp, {
      type: 'line',
      data: { labels: [], datasets: [{ label: 'Temperatura (°C)', borderColor: '#2c3e50', backgroundColor: 'rgba(44, 62, 80, 0.2)', data: [], fill: true, tension: 0.4 }] },
      options: { responsive: true, maintainAspectRatio: false }
    });

    const ctxLuz = document.getElementById('luzChart').getContext('2d');
    const luzChart = new Chart(ctxLuz, {
      type: 'line',
      data: { labels: [], datasets: [{ label: 'Luminosidade (ADC Bruto)', borderColor: '#f39c12', backgroundColor: 'rgba(243, 156, 18, 0.2)', data: [], fill: true, tension: 0.4 }] },
      options: { responsive: true, maintainAspectRatio: false }
    });

    setInterval(function() {
      fetch('/data').then(res => res.json()).then(json => {
        document.getElementById("valor").innerHTML = json.temp.toFixed(1);
        document.getElementById("limite").innerHTML = json.max.toFixed(1);
        document.getElementById("luz-status").innerHTML = json.rawLDR;
        
        if(json.temp > json.max) {
          document.getElementById("status-box").innerHTML = "ALERTA TÉRMICO!";
          document.getElementById("status-box").className = "status alerta";
          document.getElementById("rele-status").innerHTML = "<strong>LIGADA</strong>";
          document.getElementById("servo-status").innerHTML = "<strong>Aberta (0°)</strong>";
        } else {
          document.getElementById("status-box").innerHTML = "SISTEMA ESTÁVEL";
          document.getElementById("status-box").className = "status normal";
          document.getElementById("rele-status").innerHTML = "Desligada";
          document.getElementById("servo-status").innerHTML = "Fechada (90°)";
        }

        if(json.violado) {
          document.getElementById("seguranca-box").innerHTML = "ALERTA: VIOLADO!";
          document.getElementById("seguranca-box").className = "status alerta";
        } else {
          document.getElementById("seguranca-box").innerHTML = "GABINETE SEGURO";
          document.getElementById("seguranca-box").className = "status normal";
        }

        const now = new Date();
        const timeLabel = now.getHours() + ':' + now.getMinutes() + ':' + now.getSeconds();
        
        if(tempChart.data.labels.length > 15) { tempChart.data.labels.shift(); tempChart.data.datasets[0].data.shift(); }
        tempChart.data.labels.push(timeLabel);
        tempChart.data.datasets[0].data.push(json.temp);
        tempChart.update();

        if(luzChart.data.labels.length > 15) { luzChart.data.labels.shift(); luzChart.data.datasets[0].data.shift(); }
        luzChart.data.labels.push(timeLabel);
        luzChart.data.datasets[0].data.push(json.rawLDR);
        luzChart.update();
      });

      fetch('/stats').then(res => res.json()).then(json => {
        document.getElementById("heap").innerHTML = (json.heap / 1024).toFixed(1);
        document.getElementById("min-heap").innerHTML = (json.min_heap / 1024).toFixed(1);
        document.getElementById("cpu").innerHTML = json.cpu;
        document.getElementById("uptime").innerHTML = json.uptime;
        document.getElementById("tasks").innerHTML = json.tasks;
        document.getElementById("prio").innerHTML = json.prio_app;
      });
    }, 2000);
  </script>
</body></html>)rawliteral";

void TaskSensor(void *pvParameters) {
  for (;;) {
    sensors.requestTemperatures();
    tempAtual = sensors.getTempCByIndex(0);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void TaskControl(void *pvParameters) {
  for (;;) {
    if (tempAtual > tempMax && tempMax > 0) {
      if (!sistemaAtivo) {
        escotilha.write(0);
        vTaskDelay(pdMS_TO_TICKS(600));
        digitalWrite(PIN_RELE, HIGH);
        sistemaAtivo = true;
      }
      tone(PIN_BUZZER, 1500);
      vTaskDelay(pdMS_TO_TICKS(300));
      noTone(PIN_BUZZER);
      digitalWrite(PIN_BUZZER, HIGH);
      vTaskDelay(pdMS_TO_TICKS(100));
    } else {
      if (sistemaAtivo) {
        digitalWrite(PIN_RELE, LOW);
        vTaskDelay(pdMS_TO_TICKS(1000));
        escotilha.write(90);
        sistemaAtivo = false;
      }
      digitalWrite(PIN_RELE, LOW);
      noTone(PIN_BUZZER);
      digitalWrite(PIN_BUZZER, HIGH);
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}

void TaskSeguranca(void *pvParameters) {
  for (;;) {
    rawLDR = analogRead(PIN_LDR_ANALOG);

    // Lógica do Alarme (Ajuste o < ou > dependendo de como o seu módulo funciona fisicamente)
    if (rawLDR < LIMITE_LDR) {
      gabineteViolado = true;
    } else {
      gabineteViolado = false;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void TaskDisplay(void *pvParameters) {
  for (;;) {
    lcd.setCursor(0, 0);
    lcd.print("Temp: ");
    lcd.print(tempAtual, 1);
    lcd.print("C   ");

    lcd.setCursor(0, 1);
    if (tempAtual > tempMax && tempMax > 0) {
      lcd.print("ALERTA CRITICO! ");
    } else if (gabineteViolado) {
      lcd.print("GABINETE ABERTO!");
    } else {
      lcd.print("Max:  ");
      lcd.print(tempMax, 0);
      lcd.print("C   ");
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_RELE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_RELE, LOW);
  digitalWrite(PIN_BUZZER, HIGH);

  pinMode(PIN_LDR_ANALOG, INPUT);

  escotilha.setPeriodHertz(50);
  escotilha.attach(PIN_SERVO, 500, 2400);
  escotilha.write(90);

  lcd.init();
  lcd.backlight();
  sensors.begin();

  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, 1000000, true, 0);

  lcd.setCursor(0, 0);
  lcd.print("Aguardando Setup");
  Serial.println("\n[SISTEMA CIBERFISICO - TDE PUCPR]");
  Serial.println("Defina o limite termico (Ex: 30.5):");

  while (tempMax <= 0) {
    if (Serial.available() > 0) tempMax = Serial.parseFloat();
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  lcd.clear();
  lcd.print("Conectando WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
  }

  Serial.println("\nWiFi Conectado!");
  Serial.print("IP para acessar o Dashboard: ");
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.print("IP: ");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<200> doc;
    doc["temp"] = tempAtual;
    doc["max"] = tempMax;
    doc["rawLDR"] = rawLDR;
    doc["violado"] = gabineteViolado;
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/stats", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<300> doc;
    doc["heap"] = ESP.getFreeHeap();
    doc["min_heap"] = ESP.getMinFreeHeap();
    doc["cpu"] = ESP.getCpuFreqMHz();

    portENTER_CRITICAL(&timerMux);
    doc["uptime"] = hw_uptime_seconds;
    portEXIT_CRITICAL(&timerMux);

    doc["tasks"] = uxTaskGetNumberOfTasks();
    doc["prio_app"] = "Task_Controle (Prio 2)";

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.begin();

  xTaskCreatePinnedToCore(TaskSensor, "Task_Sensor", 2048, NULL, 1, &TaskSensorHandle, 1);
  xTaskCreatePinnedToCore(TaskControl, "Task_Controle", 2048, NULL, 2, &TaskControlHandle, 0);
  xTaskCreatePinnedToCore(TaskDisplay, "Task_LCD", 2048, NULL, 1, &TaskDisplayHandle, 1);
  xTaskCreatePinnedToCore(TaskSeguranca, "Task_Segur", 2048, NULL, 1, &TaskSegurancaHandle, 1);
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
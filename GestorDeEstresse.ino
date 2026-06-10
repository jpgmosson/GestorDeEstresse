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
#include <ESP32Servo.h> // Biblioteca do Servo

const char* ssid = "joao";
const char* password = "jjjhhhmmm";

#define SENSOR_PIN 4
#define PIN_RELE 12     
#define PIN_BUZZER 26 
#define PIN_SERVO 19 // <-- Servo Motor agora no D19

OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);
LiquidCrystal_I2C lcd(0x27, 16, 2);
AsyncWebServer server(80);
Servo escotilha; // Objeto da escotilha

float tempAtual = 0.0;
float tempMax = 0.0;
volatile unsigned long hw_uptime_seconds = 0; 
bool sistemaAtivo = false; // Controle da máquina de estados (Escotilha -> Ventoinha)

TaskHandle_t TaskSensorHandle;
TaskHandle_t TaskControlHandle;
TaskHandle_t TaskDisplayHandle;

hw_timer_t * timer = NULL;
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
    :root { --primary: #2c3e50; --success: #2e7d32; --danger: #c62828; --bg: #f4f7f6; }
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
    .chart-container { position: relative; height: 300px; width: 100%; margin-top: 20px; }
    @keyframes blink { 50% { opacity: 0.7; } }
  </style>
</head>
<body>
  <div class="header">
    <h1>Painel Ciberfísico - ESP32</h1>
  </div>

  <div class="tabs">
    <button class="tab-btn active" onclick="openTab('monitor')">Monitoramento</button>
    <button class="tab-btn" onclick="openTab('stats')">Performance (Nerds)</button>
    <button class="tab-btn" onclick="openTab('about')">Sobre</button>
  </div>

  <div id="monitor" class="tab-content active">
    <div class="grid">
      <div class="card">
        <h3>Temperatura DS18B20</h3>
        <div id="temp-box" class="temp-value"><span id="valor">--</span>°C</div>
        <p>Limite Setado: <span id="limite">--</span>°C</p>
      </div>
      <div class="card">
        <h3>Status do Atuador</h3>
        <div id="status-box" class="status normal">SISTEMA ESTÁVEL</div>
        <p>Relé Ventoinha: <span id="rele-status">Desligado</span></p>
        <p>Escotilha: <span id="servo-status">Fechada</span></p>
      </div>
    </div>
    <div class="chart-container">
      <canvas id="tempChart"></canvas>
    </div>
  </div>

  <div id="stats" class="tab-content">
    <h2>Análise de Recursos do Sistema</h2>
    <div class="grid">
      <div class="card"><h4>Memória RAM Atual</h4><div class="temp-value"><span id="heap">--</span> KB</div></div>
      <div class="card"><h4>Pico de Consumo</h4><div class="temp-value"><span id="min-heap">--</span> KB</div><p style="font-size: 0.8em; margin-top:5px;">Histórico de menor RAM livre</p></div>
      <div class="card"><h4>Frequência da CPU</h4><div class="temp-value"><span id="cpu">--</span> MHz</div></div>
      <div class="card"><h4>Uptime (Hardware)</h4><div class="temp-value"><span id="uptime">--</span> s</div></div>
      <div class="card"><h4>Threads do SO</h4><div class="temp-value"><span id="tasks">--</span></div><p style="font-size: 0.8em; margin-top:5px;">Total no FreeRTOS</p></div>
      <div class="card"><h4>Task mais Crítica</h4><div style="font-size: 1.3rem; font-weight: bold; color: var(--primary); margin-top: 15px;"><span id="prio">--</span></div><p style="font-size: 0.8em; margin-top:5px;">Preempção habilitada no Core 0</p></div>
    </div>
    <p style="text-align:center; margin-top: 20px; font-size: 0.9em; color:#666;">Sistema rodando sob FreeRTOS. Multithreading ativo com escalonamento preemptivo.</p>
  </div>

  <div id="about" class="tab-content">
    <h2>Informações Acadêmicas</h2>
    <ul>
      <li><strong>Instituição:</strong> Pontifícia Universidade Católica do Paraná (PUCPR)</li>
      <li><strong>Curso:</strong> Engenharia de Software</li>
      <li><strong>Disciplina:</strong> Performance em Sistemas Ciberfísicos</li>
      <li><strong>Equipe:</strong></li>
      <li>João Pedro Gadens Mosson</li>
      <li>João Pedro Magri Pozzan</li>
      <li>Eduardo Skoroboatei Gomes</li>
      <li>André Murilo Pinz Gomes</li>
      <li>Nicole Harumi Futikami</li>
      <li><strong>GitHub:</strong> https://github.com/jpgmosson/GestorDeEstresse</li>
      <li><strong>Semestre:</strong> 3º Período - 2026/1</li>
    </ul>
  </div>

  <script>
    function openTab(tabId) {
      document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));
      document.querySelectorAll('.tab-btn').forEach(btn => btn.classList.remove('active'));
      document.getElementById(tabId).classList.add('active');
      event.currentTarget.classList.add('active');
    }

    const ctx = document.getElementById('tempChart').getContext('2d');
    const tempChart = new Chart(ctx, {
      type: 'line',
      data: { labels: [], datasets: [{ label: 'Temperatura Atual (°C)', borderColor: '#2c3e50', backgroundColor: 'rgba(44, 62, 80, 0.2)', data: [], fill: true, tension: 0.4 }] },
      options: { responsive: true, maintainAspectRatio: false, scales: { y: { beginAtZero: false } } }
    });

    setInterval(function() {
      fetch('/data').then(res => res.json()).then(json => {
        document.getElementById("valor").innerHTML = json.temp.toFixed(1);
        document.getElementById("limite").innerHTML = json.max.toFixed(1);
        
        const statusBox = document.getElementById("status-box");
        const releStatus = document.getElementById("rele-status");
        const servoStatus = document.getElementById("servo-status");
        
        if(json.temp > json.max) {
          statusBox.innerHTML = "ALERTA: ATUADOR ATIVADO!";
          statusBox.className = "status alerta";
          releStatus.innerHTML = "<strong>LIGADO (12V)</strong>";
          servoStatus.innerHTML = "<strong>Aberta (90°)</strong>";
        } else {
          statusBox.innerHTML = "SISTEMA ESTÁVEL";
          statusBox.className = "status normal";
          releStatus.innerHTML = "Desligado";
          servoStatus.innerHTML = "Fechada";
        }

        const now = new Date();
        const timeLabel = now.getHours() + ':' + now.getMinutes() + ':' + now.getSeconds();
        if(tempChart.data.labels.length > 15) { tempChart.data.labels.shift(); tempChart.data.datasets[0].data.shift(); }
        tempChart.data.labels.push(timeLabel);
        tempChart.data.datasets[0].data.push(json.temp);
        tempChart.update();
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
  for(;;) {
    sensors.requestTemperatures();
    tempAtual = sensors.getTempCByIndex(0);
    vTaskDelay(pdMS_TO_TICKS(1000)); 
  }
}

void TaskControl(void *pvParameters) {
  for(;;) {
    if (tempAtual > tempMax && tempMax > 0) {
      // 1. Lógica de Abertura (Abre escotilha, depois liga ventoinha)
      if (!sistemaAtivo) {
        escotilha.write(90); // Gira o servo para 90 graus
        vTaskDelay(pdMS_TO_TICKS(600)); // Espera a engrenagem chegar na posição
        digitalWrite(PIN_RELE, HIGH); // Liga a ventoinha
        sistemaAtivo = true;
      }
      
      // Apito contínuo de alerta térmico
      tone(PIN_BUZZER, 1500); 
      vTaskDelay(pdMS_TO_TICKS(300));
      noTone(PIN_BUZZER);
      digitalWrite(PIN_BUZZER, HIGH); 
      vTaskDelay(pdMS_TO_TICKS(100));

    } else {
      // 2. Lógica de Fechamento (Desliga ventoinha, depois fecha escotilha)
      if (sistemaAtivo) {
        digitalWrite(PIN_RELE, LOW); // Corta energia da ventoinha
        vTaskDelay(pdMS_TO_TICKS(1000)); // Espera a hélice parar de rodar por inércia
        escotilha.write(0); // Fecha a escotilha
        sistemaAtivo = false;
      }
      
      digitalWrite(PIN_RELE, LOW);    
      noTone(PIN_BUZZER);
      digitalWrite(PIN_BUZZER, HIGH); 
      vTaskDelay(pdMS_TO_TICKS(500)); 
    }
  }
}

void TaskDisplay(void *pvParameters) {
  for(;;) {
    lcd.setCursor(0, 0);
    lcd.print("Temp: ");
    lcd.print(tempAtual, 1);
    lcd.print("C   ");

    lcd.setCursor(0, 1);
    if (tempAtual > tempMax && tempMax > 0) {
      lcd.print("ALERTA CRITICO! ");
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

  // Configuração do Servo Motor
  escotilha.setPeriodHertz(50); // Frequência do SG90
  escotilha.attach(PIN_SERVO, 500, 2400); // Acopla o D19
  escotilha.write(0); // Força a posição fechada logo no boot

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
    if (Serial.available() > 0) {
      tempMax = Serial.parseFloat();
    }
    vTaskDelay(pdMS_TO_TICKS(100)); 
  }
  
  Serial.print("Limite gravado: "); Serial.println(tempMax);

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
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
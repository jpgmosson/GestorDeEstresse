/**
 * Projeto: Sistema Ciberfisico de Monitoramento Termico (TDE)
 * Disciplina: Performance em Sistemas Ciberfisicos (PUCPR)
 * Autor: João Pedro Gadens Mosson
 * Versão Final: LDR Corrigido, Super Aba de Performance
 */

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <LittleFS.h>

const char *ssid = "joao";
const char *password = "jjjhhhmmm";

#define SENSOR_PIN 4
#define PIN_RELE 12
#define PIN_BUZZER 26
#define PIN_SERVO 19
#define PIN_LDR_ANALOG 34

// LDR Invertido: Quarto = 1700. Lanterna/Fogo = 0.
// Dispara quando for MENOR que 1000.
int LIMITE_LDR = 1000;

OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);
LiquidCrystal_I2C lcd(0x27, 16, 2);
AsyncWebServer server(80);
Servo escotilha;

float tempAtual = 0.0;
float tempMax = 30.0;
int rawLDR = 0;
volatile unsigned long hw_uptime_seconds = 0;

bool sistemaAtivo = false;
bool gabineteViolado = false;

bool last_alarme_termico = false;
bool last_gabinete_violado = false;

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

void logEvent(String tipo, String mensagem) {
  File f = LittleFS.open("/logs.csv", "a");
  if (f) {
    f.printf("%lu,%s,%s,%.1f,%d\n", hw_uptime_seconds, tipo.c_str(), mensagem.c_str(), tempAtual, rawLDR);
    f.close();
  }
}

// O BLOCO HTML COMEÇA AQUI
const char index_html[] PROGMEM = R"=====(
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
    .tabs { display: flex; justify-content: center; gap: 10px; margin-bottom: 20px; flex-wrap: wrap;}
    .tab-btn { padding: 10px 20px; border: none; background: #ddd; border-radius: 5px; cursor: pointer; font-weight: bold; transition: 0.3s; }
    .tab-btn.active { background: var(--primary); color: white; }
    .tab-content { display: none; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); max-width: 1000px; margin: 0 auto; }
    .tab-content.active { display: block; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; text-align: center; }
    .card { padding: 15px; border-radius: 10px; background: #f9f9f9; border: 1px solid #eee; }
    .temp-value { font-size: 2.5rem; font-weight: bold; color: var(--primary); margin-top: 10px;}
    .status { padding: 10px; border-radius: 5px; font-weight: bold; margin-top: 10px; }
    .normal { background: #e8f5e9; color: var(--success); }
    .alerta { background: #ffebee; color: var(--danger); animation: blink 1s infinite; }
    .chart-container { position: relative; height: 250px; width: 100%; margin-top: 20px; border: 1px solid #ddd; border-radius: 10px; padding: 10px; background: white;}
    input[type=number] { padding: 8px; border: 1px solid #ccc; border-radius: 4px; width: 80px; font-weight:bold; }
    .btn-action { padding: 8px 15px; background: var(--primary); color: white; border: none; border-radius: 4px; cursor: pointer; font-weight:bold; }
    .console-box { background: #1e1e1e; color: #0f0; font-family: monospace; width: 100%; height: 130px; border: none; padding: 10px; border-radius: 5px; resize: none; font-size: 12px; box-sizing: border-box; margin-top: 10px; }
    @keyframes blink { 50% { opacity: 0.7; } }
    .team-list { list-style-type: square; padding-left: 20px; text-align: left; margin: 10px 0; }
    .team-list li { margin-bottom: 5px; }
  </style>
</head>
<body>
  <div class="header"><h1>Painel Ciberfísico - ESP32</h1></div>

  <div class="tabs">
    <button class="tab-btn active" onclick="openTab('monitor')">Monitoramento</button>
    <button class="tab-btn" onclick="openTab('stats')">Análise de Recursos</button>
    <button class="tab-btn" onclick="openTab('logs')">Sistema de Logs</button>
    <button class="tab-btn" onclick="openTab('about')">Sobre</button>
  </div>

  <div id="monitor" class="tab-content active">
    <div class="grid">
      <div class="card">
        <h3>Temperatura</h3>
        <div id="temp-box" class="temp-value" style="font-size: 3rem;"><span id="valor">--</span>°C</div>
        <p style="margin-bottom: 5px;">Limite Setado: <strong><span id="limite">--</span>°C</strong></p>
        <div style="margin-top:10px;">
          <input type="number" id="novoLimite" step="0.5" placeholder="Ex: 30.5">
          <button class="btn-action" onclick="setLimit()">Salvar</button>
        </div>
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
    <h2>Métricas de Desempenho e Hardware</h2>
    <div class="grid" style="grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));">
      <div class="card"><h4 style="margin:0;">RAM Total</h4><div class="temp-value"><span id="total-heap">--</span> KB</div></div>
      <div class="card"><h4 style="margin:0;">RAM Livre</h4><div class="temp-value"><span id="heap">--</span> KB</div></div>
      <div class="card"><h4 style="margin:0;">Pico (Min RAM)</h4><div class="temp-value"><span id="min-heap">--</span> KB</div></div>
      <div class="card"><h4 style="margin:0;">Espaço Livre Prog.</h4><div class="temp-value"><span id="sketch">--</span> KB</div></div>
      <div class="card"><h4 style="margin:0;">Memória Flash</h4><div class="temp-value"><span id="flash">--</span> MB</div></div>
      <div class="card"><h4 style="margin:0;">Modelo do Chip</h4><div style="font-size: 1.6rem; font-weight: bold; color: var(--primary); margin-top: 15px;"><span id="model">--</span></div></div>
      <div class="card"><h4 style="margin:0;">Núcleos CPU</h4><div class="temp-value"><span id="cores">--</span></div></div>
      <div class="card"><h4 style="margin:0;">Freq. CPU</h4><div class="temp-value"><span id="cpu">--</span> MHz</div></div>
      <div class="card"><h4 style="margin:0;">Sinal Wi-Fi (RSSI)</h4><div class="temp-value"><span id="rssi">--</span> dBm</div></div>
      <div class="card"><h4 style="margin:0;">Uptime</h4><div class="temp-value"><span id="uptime">--</span> s</div></div>
      <div class="card"><h4 style="margin:0;">Tasks (FreeRTOS)</h4><div class="temp-value"><span id="tasks">--</span></div></div>
      <div class="card"><h4 style="margin:0;">Prio. Aplicação</h4><div style="font-size: 1.2rem; font-weight: bold; color: var(--primary); margin-top: 15px;"><span id="prio">--</span></div></div>
    </div>
  </div>

  <div id="logs" class="tab-content">
    <div class="card" style="text-align:center; padding:20px; margin-bottom: 20px; background: #e8f4f8;">
        <h3>💾 Exportação de Histórico (LittleFS)</h3>
        <p>O sistema grava intercorrências na memória Flash interna. Faça o download para auditoria.</p>
        <a href="/logs.csv" download>
          <button class="btn-action" style="font-size:1.1rem; padding: 10px 20px; margin-top: 10px;">📥 Baixar Histórico (.CSV)</button>
        </a>
    </div>

    <h2>Console em Tempo Real</h2>
    <div class="grid">
      <div class="card" style="padding: 10px;">
        <h4 style="margin:0;">[DS18B20] Sensor Térmico</h4>
        <textarea id="log-temp" class="console-box" readonly></textarea>
      </div>
      <div class="card" style="padding: 10px;">
        <h4 style="margin:0;">[LDR] Sensor Óptico</h4>
        <textarea id="log-ldr" class="console-box" readonly></textarea>
      </div>
      <div class="card" style="padding: 10px;">
        <h4 style="margin:0;">[SERVO] Escotilha</h4>
        <textarea id="log-servo" class="console-box" readonly></textarea>
      </div>
      <div class="card" style="padding: 10px;">
        <h4 style="margin:0;">[RELÉ] Ventoinha</h4>
        <textarea id="log-rele" class="console-box" readonly></textarea>
      </div>
    </div>
  </div>

  <div id="about" class="tab-content">
    <h2>Informações Acadêmicas</h2>
    <ul>
      <li><strong>Instituição:</strong> PUCPR</li>
      <li><strong>Curso:</strong> Engenharia de Software</li>
      <li><strong>Repositório:</strong> <a href="https://github.com/jpgmosson/GestorDeEstresse" target="_blank" style="color: var(--primary); font-weight: bold;">Acessar GitHub do Projeto</a></li>
      <li><strong>Equipe de Desenvolvimento:</strong>
        <ul class="team-list">
          <li>João Pedro Gadens Mosson</li>
          <li>João Pedro Magri Pozzan</li>
          <li>Eduardo Skoroboatei Gomes</li>
          <li>André Murilo Pinz Gomes</li>
          <li>Nicole Harumi Futikami</li>
        </ul>
      </li>
    </ul>
  </div>

  <script>
    const openTab = (tabId) => {
      document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));
      document.querySelectorAll('.tab-btn').forEach(btn => btn.classList.remove('active'));
      document.getElementById(tabId).classList.add('active');
      event.currentTarget.classList.add('active');
    };

    const setLimit = () => {
      const v = document.getElementById('novoLimite').value;
      if(v && v > 0) {
        fetch('/setTemp?val=' + v).then(res => {
          if(res.ok) { alert('Novo limite térmico salvo na Flash com sucesso!'); document.getElementById('novoLimite').value='';}
        });
      }
    };

    const ctxTemp = document.getElementById('tempChart').getContext('2d');
    const tempChart = new Chart(ctxTemp, { type: 'line', data: { labels: [], datasets: [{ label: 'Temperatura (°C)', borderColor: '#2c3e50', backgroundColor: 'rgba(44, 62, 80, 0.2)', data: [], fill: true, tension: 0.4 }] }, options: { responsive: true, maintainAspectRatio: false } });

    const ctxLuz = document.getElementById('luzChart').getContext('2d');
    const luzChart = new Chart(ctxLuz, { type: 'line', data: { labels: [], datasets: [{ label: 'Luminosidade (ADC Bruto)', borderColor: '#f39c12', backgroundColor: 'rgba(243, 156, 18, 0.2)', data: [], fill: true, tension: 0.4 }] }, options: { responsive: true, maintainAspectRatio: false } });

    setInterval(() => {
      fetch('/data').then(res => res.json()).then(json => {
        document.getElementById("valor").innerHTML = json.temp.toFixed(1);
        document.getElementById("limite").innerHTML = json.max.toFixed(1);
        document.getElementById("luz-status").innerHTML = json.rawLDR;
        
        let stRele = "Desligada";
        let stServo = "Fechada";

        if(json.temp > json.max) {
          document.getElementById("status-box").innerHTML = "ALERTA TÉRMICO!";
          document.getElementById("status-box").className = "status alerta";
          document.getElementById("rele-status").innerHTML = "<strong>LIGADA</strong>";
          document.getElementById("servo-status").innerHTML = "<strong>Aberta (0°)</strong>";
          stRele = "Ligada";
          stServo = "Aberta";
        } else {
          document.getElementById("status-box").innerHTML = "SISTEMA ESTÁVEL";
          document.getElementById("status-box").className = "status normal";
          document.getElementById("rele-status").innerHTML = "Desligada";
          document.getElementById("servo-status").innerHTML = "Fechada (90°)";
        }

        if(json.violado) {
          document.getElementById("seguranca-box").innerHTML = "ALERTA: FOGO/VIOLAÇÃO!";
          document.getElementById("seguranca-box").className = "status alerta";
        } else {
          document.getElementById("seguranca-box").innerHTML = "GABINETE SEGURO";
          document.getElementById("seguranca-box").className = "status normal";
        }

        const now = new Date();
        const timeLabel = now.getHours() + ':' + now.getMinutes() + ':' + now.getSeconds();
        
        const logT = document.getElementById('log-temp');
        logT.value += `[${timeLabel}] Leitura: ${json.temp.toFixed(1)}°C\n`;
        logT.scrollTop = logT.scrollHeight;

        const logL = document.getElementById('log-ldr');
        logL.value += `[${timeLabel}] ADC: ${json.rawLDR} | ${json.violado ? 'ALERTA' : 'SEGURO'}\n`;
        logL.scrollTop = logL.scrollHeight;

        const logS = document.getElementById('log-servo');
        logS.value += `[${timeLabel}] Posição: ${stServo}\n`;
        logS.scrollTop = logS.scrollHeight;

        const logR = document.getElementById('log-rele');
        logR.value += `[${timeLabel}] Status: ${stRele}\n`;
        logR.scrollTop = logR.scrollHeight;

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
        document.getElementById("total-heap").innerHTML = (json.total_heap / 1024).toFixed(1);
        document.getElementById("heap").innerHTML = (json.heap / 1024).toFixed(1);
        document.getElementById("min-heap").innerHTML = (json.min_heap / 1024).toFixed(1);
        document.getElementById("cpu").innerHTML = json.cpu;
        document.getElementById("cores").innerHTML = json.cores;
        document.getElementById("model").innerHTML = json.model;
        document.getElementById("uptime").innerHTML = json.uptime;
        document.getElementById("tasks").innerHTML = json.tasks;
        document.getElementById("prio").innerHTML = json.prio_app;
        document.getElementById("flash").innerHTML = (json.flash / 1048576).toFixed(1);
        document.getElementById("sketch").innerHTML = (json.sketch / 1024).toFixed(1);
        document.getElementById("rssi").innerHTML = json.rssi;
      });
    }, 2000);
  </script>
</body></html>
)=====";
// O BLOCO HTML TERMINA AQUI

void TaskSensor(void *pvParameters) {
  for (;;) {
    sensors.requestTemperatures();
    tempAtual = sensors.getTempCByIndex(0);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void TaskControl(void *pvParameters) {
  for (;;) {
    bool current_alarme_termico = (tempAtual > tempMax && tempMax > 0);

    if (current_alarme_termico && !last_alarme_termico) {
      logEvent("WARNING", "Alerta Termico Acionado");
    } else if (!current_alarme_termico && last_alarme_termico) {
      logEvent("INFO", "Sistema Termico Normalizado");
    }
    last_alarme_termico = current_alarme_termico;

    if (current_alarme_termico) {
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

    // Lógica RIGOROSA: Abaixo de 1000 significa luz FORTE (Lanterna ou Fogo)
    gabineteViolado = (rawLDR < LIMITE_LDR);

    if (gabineteViolado && !last_gabinete_violado) {
      logEvent("ERROR", "Luz Extrema Detectada no Gabinete");
    } else if (!gabineteViolado && last_gabinete_violado) {
      logEvent("INFO", "Gabinete Escuro e Seguro");
    }
    last_gabinete_violado = gabineteViolado;

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
  lcd.print("Iniciando FS...");
  Serial.println("\n[SISTEMA CIBERFISICO - TDE PUCPR]");

  if (!LittleFS.begin(true)) {
    Serial.println("Erro Crítico: Falha ao montar o sistema de arquivos (LittleFS).");
  } else {
    File arquivo = LittleFS.open("/config.txt", "r");
    if (arquivo) {
      tempMax = arquivo.readString().toFloat();
      arquivo.close();
      Serial.println("Limite restaurado da Flash: " + String(tempMax) + "C");
    } else {
      Serial.println("Primeira inicializacao. Usando limite padrao: 30.0C");
    }
  }

  logEvent("INFO", "Sistema Iniciado (Boot)");

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

  // ROTA DE ESTATÍSTICAS EXPANDIDA (CUIDA DE TODOS OS 12 CARDS)
  server.on("/stats", HTTP_GET, [](AsyncWebServerRequest *request) {
    StaticJsonDocument<512> doc;
    doc["total_heap"] = ESP.getHeapSize();
    doc["heap"] = ESP.getFreeHeap();
    doc["min_heap"] = ESP.getMinFreeHeap();
    doc["cpu"] = ESP.getCpuFreqMHz();
    doc["cores"] = ESP.getChipCores();
    doc["model"] = ESP.getChipModel();
    doc["flash"] = ESP.getFlashChipSize();
    doc["sketch"] = ESP.getFreeSketchSpace();
    doc["rssi"] = WiFi.RSSI();

    portENTER_CRITICAL(&timerMux);
    doc["uptime"] = hw_uptime_seconds;
    portEXIT_CRITICAL(&timerMux);

    doc["tasks"] = uxTaskGetNumberOfTasks();
    doc["prio_app"] = "Task_Controle (Prio 2)";

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/setTemp", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("val")) {
      String val = request->getParam("val")->value();
      tempMax = val.toFloat();

      File f = LittleFS.open("/config.txt", "w");
      if (f) {
        f.print(val);
        f.close();
      }

      logEvent("INFO", "Novo Limite Termico via WEB: " + val + "C");
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Erro: Parametro ausente");
    }
  });

  server.on("/logs.csv", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/logs.csv", "text/csv", true);
  });

  server.begin();

  xTaskCreatePinnedToCore(TaskSensor, "Task_Sensor", 4096, NULL, 1, &TaskSensorHandle, 1);
  xTaskCreatePinnedToCore(TaskControl, "Task_Controle", 4096, NULL, 2, &TaskControlHandle, 0);
  xTaskCreatePinnedToCore(TaskDisplay, "Task_LCD", 4096, NULL, 1, &TaskDisplayHandle, 1);
  xTaskCreatePinnedToCore(TaskSeguranca, "Task_Segur", 4096, NULL, 1, &TaskSegurancaHandle, 1);
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
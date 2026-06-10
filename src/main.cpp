// ============================================================
//  HarvestEye — IoT ESP32
//  Disciplina: Disruptive Architectures (FIAP)
//  Global Solution 2026/1 — Space Connect
//  Integração: CaneOrbit API (Java — Render)
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// ============ CONFIGURAÇÕES ============
const char* SSID     = "Wokwi-GUEST";
const char* PASSWORD = "";

// API Java em produção (Render)
const char* API_BASE = "https://caneorbis-api-java.onrender.com";

// ID do dispositivo — obter após cadastrar via POST /api/dispositivos
const int DEVICE_ID = 22;

// ============ PINOS ============
#define DHT_PIN     4   // ENTRADA 1 — DHT22 (temperatura + umidade do ar)
#define SOIL_PIN   34   // ENTRADA 2 — Joystick VRX simula sensor de umidade do solo
#define PH_PIN     35   // ENTRADA 3 — Potenciômetro simula sensor de pH do solo

#define LED_VALID_G 25  // SAÍDA 1 — LED verde:    POST à API ok
#define LED_VALID_R 26  // SAÍDA 2 — LED vermelho: pisca = conectando | fixo = sem API

// ============ OBJETOS ============
DHT               dht(DHT_PIN, DHT22);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer         server(80);

// ============ ESTADO GLOBAL ============
bool    apiOk        = false;
String  deviceName   = "Sonda";
float   soilMoisture = 0.0;
float   temperature  = 0.0;
float   airHumidity  = 0.0;
float   phSolo       = 7.0;
String  riskLevel    = "unknown";

unsigned long lastSensorRead = 0;
unsigned long lastApiPost    = 0;
unsigned long lastBlinkTime  = 0;
bool          blinkState     = false;

const long SENSOR_INTERVAL = 2000;
const long API_INTERVAL    = 10000;
const long BLINK_INTERVAL  = 500;   // pisca a cada 500ms enquanto conecta Wi-Fi

// ============ DECLARAÇÕES ANTECIPADAS ============
void setValidationLED(bool ok);
void updateBlinkLED();
void calcularRisco();
void lcdPrint(String linha1, String linha2);
void updateDisplay();
void readSensors();
void sendSensorData();
void handleDashboard();
void handleGetLeitura();
void handleGetStatus();
void handlePostConfig();

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("--- Inicializando o Sistema HarvestEye ---");

  // --- Pinos de saída ---
  pinMode(LED_VALID_G, OUTPUT);
  pinMode(LED_VALID_R, OUTPUT);

  // Estado inicial: vermelho piscando (conectando)
  digitalWrite(LED_VALID_G, LOW);
  digitalWrite(LED_VALID_R, HIGH);

  // --- DHT22 ---
  dht.begin();

  // --- LCD ---
  lcd.init();
  lcd.backlight();
  delay(200);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("HarvestEye Ativo");
  delay(2000);

  // --- Wi-Fi (não bloqueante) ---
  Serial.println("Conectando ao Wi-Fi...");
  WiFi.begin(SSID, PASSWORD);
  lcdPrint("Conectando...", "Wi-Fi: aguardando");

  // --- WebServer ---
  server.on("/",               HTTP_GET,  handleDashboard);
  server.on("/sensor/leitura", HTTP_GET,  handleGetLeitura);
  server.on("/sensor/status",  HTTP_GET,  handleGetStatus);
  server.on("/sensor/config",  HTTP_POST, handlePostConfig);
  server.begin();
  Serial.println("WebServer iniciado na porta 80");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  server.handleClient();

  unsigned long now = millis();

  // Piscar LED vermelho enquanto Wi-Fi não conectar
  updateBlinkLED();

  // Quando Wi-Fi conectar pela primeira vez, atualiza LED e LCD
  static bool wifiWasConnected = false;
  if (WiFi.status() == WL_CONNECTED && !wifiWasConnected) {
    wifiWasConnected = true;
    if (!apiOk) digitalWrite(LED_VALID_R, HIGH); // fixo até confirmar API
    Serial.println("Wi-Fi conectado: " + WiFi.localIP().toString());
    lcdPrint("Wi-Fi OK", WiFi.localIP().toString().substring(0, 16));
    delay(1500);
  }

  // Leitura periódica dos sensores
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    readSensors();
    calcularRisco();
    updateDisplay();
    lastSensorRead = now;
  }

  // Envio periódico à API CaneOrbit
  if (WiFi.status() == WL_CONNECTED && now - lastApiPost >= API_INTERVAL) {
    sendSensorData();
    lastApiPost = now;
  }
}

// ============================================================
//  LED — STATUS DA API
// ============================================================
void setValidationLED(bool ok) {
  digitalWrite(LED_VALID_G, ok ? HIGH : LOW);
  digitalWrite(LED_VALID_R, ok ? LOW  : HIGH);
}

// Pisca o LED vermelho enquanto Wi-Fi está conectando
void updateBlinkLED() {
  if (apiOk) return;
  if (WiFi.status() == WL_CONNECTED) return;

  unsigned long now = millis();
  if (now - lastBlinkTime >= BLINK_INTERVAL) {
    blinkState = !blinkState;
    digitalWrite(LED_VALID_R, blinkState ? HIGH : LOW);
    digitalWrite(LED_VALID_G, LOW);
    lastBlinkTime = now;
  }
}

// ============================================================
//  LCD — INTERFACE LOCAL
// ============================================================
void lcdPrint(String linha1, String linha2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(linha1.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(linha2.substring(0, 16));
}

void updateDisplay() {
  String statusStr = riskLevel == "green"  ? "[OK]" :
                     riskLevel == "yellow" ? "[AT]" :
                     riskLevel == "red"    ? "[RI]" : "[--]";

  // Linha 1: umidade do solo + temperatura
  String linha1 = "S:" + String((int)soilMoisture) + "% T:" + String((int)temperature) + "C";

  // Linha 2: pH + status de risco
  String linha2 = "pH:" + String(phSolo, 1) + " " + statusStr;

  lcdPrint(linha1, linha2);
}

// ============================================================
//  SENSORES
// ============================================================
void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperature = t;
  if (!isnan(h)) airHumidity = h;

  // Solo — joystick VRX: ADC 0–4095 → 0–100%
  int rawSoil  = analogRead(SOIL_PIN);
  soilMoisture = map(rawSoil, 4095, 0, 0, 100);

  // pH — potenciômetro: ADC 0–4095 → pH 0.0–14.0
  int rawPh = analogRead(PH_PIN);
  phSolo    = rawPh * 14.0 / 4095.0;

  Serial.printf("[Sensores] Solo: %.1f%% | Temp: %.1f°C | UmAr: %.1f%% | pH: %.1f\n",
                soilMoisture, temperature, airHumidity, phSolo);
}

// ============================================================
//  CÁLCULO DE RISCO LOCAL
//  pH ideal para cana-de-açúcar: 6.0–7.5
// ============================================================
void calcularRisco() {
  bool phCritico = (phSolo < 5.5 || phSolo > 8.5);
  bool phAtencao = (phSolo < 6.0 || phSolo > 7.5);
  bool soloSeco  = (soilMoisture < 20);
  bool soloAtenc = (soilMoisture < 40);
  bool tempAlta  = (temperature  > 38);
  bool tempAtenc = (temperature  > 33);

  if (soloSeco || tempAlta || phCritico) {
    riskLevel = "red";
  } else if (soloAtenc || tempAtenc || phAtencao) {
    riskLevel = "yellow";
  } else {
    riskLevel = "green";
  }
}

// ============================================================
//  COMUNICAÇÃO COM A API — POST /api/leituras
//
//  Body enviado:
//  {
//    "idDispositivo": 1,
//    "umidadeSolo":   45.5,
//    "temperatura":   28.3,
//    "phSolo":        6.2
//  }
//
//  Resposta 200/201 → LED verde acende
// ============================================================
void sendSensorData() {
  HTTPClient http;
  http.begin(String(API_BASE) + "/api/leituras");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(35000); // Render free tier pode demorar para acordar

  JsonDocument doc;
  doc["idDispositivo"] = DEVICE_ID;
  doc["umidadeSolo"]   = round(soilMoisture * 10) / 10.0;
  doc["temperatura"]   = round(temperature  * 10) / 10.0;
  doc["phSolo"]        = round(phSolo       * 10) / 10.0;

  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  Serial.println("[API] POST /api/leituras → HTTP " + String(code));
  Serial.println("[API] Body: " + body);

  // LED verde = sucesso (200 ou 201), vermelho = falha
  apiOk = (code == 200 || code == 201);
  setValidationLED(apiOk);

  http.end();
}

// ============================================================
//  WEBSERVER — ENDPOINTS
// ============================================================
void handleDashboard() {
  String wifiStr  = WiFi.status() == WL_CONNECTED
                    ? "Conectado (" + WiFi.localIP().toString() + ")"
                    : "Desconectado";
  String apiStr   = apiOk ? "&#9989; Conectado" : "&#128308; Aguardando envio";
  String riscoStr = riskLevel == "green"  ? "&#128994; Baixo"   :
                    riskLevel == "yellow" ? "&#129000; Atencao" :
                    riskLevel == "red"    ? "&#128308; Alto"    : "Desconhecido";

  String html =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta http-equiv='refresh' content='5'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>HarvestEye</title>"
    "<style>body{font-family:sans-serif;padding:20px;background:#f5f5f5}"
    ".card{background:#fff;border-radius:8px;padding:16px;margin:8px 0;border:1px solid #ddd}"
    "h1{color:#1B4D3E}h3{color:#555;margin:4px 0}.val{font-size:1.4em;font-weight:bold;color:#1B4D3E}"
    "</style></head><body>"
    "<h1>&#127807; HarvestEye</h1>"
    "<div class='card'><h3>Rede</h3><p>" + wifiStr + "</p></div>"
    "<div class='card'><h3>API CaneOrbit</h3>"
    "<p>" + apiStr + " &nbsp; (Device ID: <b>" + String(DEVICE_ID) + "</b>)</p></div>"
    "<div class='card'><h3>Sensores</h3>"
    "<p>Umidade do solo: <span class='val'>" + String(soilMoisture, 1) + "%</span></p>"
    "<p>Temperatura: <span class='val'>" + String(temperature, 1) + "&deg;C</span></p>"
    "<p>Umidade do ar: <span class='val'>" + String(airHumidity, 1) + "%</span></p>"
    "<p>pH do solo: <span class='val'>" + String(phSolo, 1) + "</span></p></div>"
    "<div class='card'><h3>Risco da Lavoura</h3>"
    "<p>Nivel: <span class='val'>" + riscoStr + "</span></p></div>"
    "<p style='color:#aaa;font-size:12px'>Atualiza a cada 5s</p>"
    "</body></html>";

  server.send(200, "text/html", html);
}

// EP 1 — GET /sensor/leitura
void handleGetLeitura() {
  JsonDocument doc;
  doc["idDispositivo"] = DEVICE_ID;
  doc["umidadeSolo"]   = soilMoisture;
  doc["temperatura"]   = temperature;
  doc["umidadeAr"]     = airHumidity;
  doc["phSolo"]        = phSolo;
  doc["timestamp"]     = millis();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// EP 2 — GET /sensor/status
void handleGetStatus() {
  JsonDocument doc;
  doc["idDispositivo"] = DEVICE_ID;
  doc["apiConectada"]  = apiOk;
  doc["risco"]         = riskLevel;
  doc["wifi"]          = WiFi.status() == WL_CONNECTED;
  doc["wifiRSSI"]      = WiFi.RSSI();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// EP 3 — POST /sensor/config
void handlePostConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"body ausente\"}");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"JSON invalido\"}");
    return;
  }

  Serial.println("[Config] Recebida: " + server.arg("plain"));
  server.send(200, "application/json", "{\"status\":\"ok\",\"mensagem\":\"configuracao aplicada\"}");
}
// ============================================================
//  HarvestEye — IoT ESP32
//  Disciplina: Disruptive Architectures (FIAP)
//  Global Solution 2026/1 — Space Connect
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// ============ CONFIGURAÇÕES ============
const char* SSID      = "Wokwi-GUEST";
const char* PASSWORD  = "";

const char* API_BASE  = "https://sua-api.railway.app";
const char* DEVICE_MAC = "AA:BB:CC:DD:EE:FF";

// ============ PINOS ============
#define DHT_PIN       4   // ENTRADA 1 — DHT22 (temperatura + umidade do ar)
#define SOIL_PIN      34  // ENTRADA 2 — Joystick VRX simula sensor capacitivo de umidade do solo
                          //             Em hardware físico: substituir por sensor YL-69 ou similar

#define LED_VALID_G   25  // SAÍDA 1 — LED verde:    dispositivo registrado e enviando dados
#define LED_VALID_R   26  // SAÍDA 2 — LED vermelho: pisca = conectando Wi-Fi | fixo = aguardando registro

// ============ OBJETOS ============
DHT               dht(DHT_PIN, DHT22);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer         server(80);

// ============ ESTADO GLOBAL ============
bool    deviceValidated = false;
String  deviceFieldId   = "";
String  deviceName      = "Sonda";
float   soilMoisture    = 0.0;
float   temperature     = 0.0;
float   airHumidity     = 0.0;
String  riskLevel       = "unknown";
String  recommendation  = "";

unsigned long lastSensorRead = 0;
unsigned long lastApiPoll    = 0;
unsigned long lastBlinkTime  = 0;
bool          blinkState     = false;

const long SENSOR_INTERVAL   = 2000;
const long API_POLL_INTERVAL = 10000;
const long BLINK_INTERVAL    = 500;   // pisca a cada 500ms enquanto conecta Wi-Fi

// ============ DECLARAÇÕES ANTECIPADAS ============
void setValidationLED(bool validated);
void updateBlinkLED();
void lcdPrint(String linha1, String linha2);
void updateDisplay();
void readSensors();
void pollDeviceStatus();
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
    // Wi-Fi conectado mas ainda sem validação: LED vermelho FIXO (para de piscar)
    if (!deviceValidated) {
      digitalWrite(LED_VALID_R, HIGH);
    }
    Serial.println("Wi-Fi conectado: " + WiFi.localIP().toString());
    lcdPrint("Wi-Fi OK", WiFi.localIP().toString().substring(0, 16));
    delay(1500);
  }

  // Leitura periódica dos sensores
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    readSensors();
    updateDisplay();
    lastSensorRead = now;
  }

  // Poll periódico da API
  if (WiFi.status() == WL_CONNECTED && now - lastApiPoll >= API_POLL_INTERVAL) {
    pollDeviceStatus();
    if (deviceValidated) {
      sendSensorData();
    }
    lastApiPoll = now;
  }
}

// ============================================================
//  LED — VALIDAÇÃO
// ============================================================

// Saída 1 (verde) + Saída 2 (vermelho)
void setValidationLED(bool validated) {
  digitalWrite(LED_VALID_G, validated ? HIGH : LOW);
  digitalWrite(LED_VALID_R, validated ? LOW  : HIGH);
}

// Pisca o LED vermelho enquanto Wi-Fi está conectando
void updateBlinkLED() {
  if (deviceValidated) return; // se validado, LEDs já estão fixos
  if (WiFi.status() == WL_CONNECTED) return; // Wi-Fi conectado = LED fixo, não pisca

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
  if (!deviceValidated) {
    lcdPrint("Ag. registro...", "S:" + String((int)soilMoisture) + "% T:" + String((int)temperature) + "C");
  } else {
    String statusStr = riskLevel == "green"  ? "[OK]" :
                       riskLevel == "yellow" ? "[AT]" : "[RI]";
    lcdPrint(deviceName.substring(0, 11) + " " + statusStr,
             "S:" + String((int)soilMoisture) + "% T:" + String((int)temperature) + "C");
  }
}

// ============================================================
//  SENSORES
// ============================================================
void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperature = t;
  if (!isnan(h)) airHumidity = h;

  int raw = analogRead(SOIL_PIN);
  soilMoisture = map(raw, 4095, 0, 0, 100);

  Serial.printf("[Sensores] Solo: %.1f%% | Temp: %.1f°C | UmAr: %.1f%%\n",
                soilMoisture, temperature, airHumidity);
}

// ============================================================
//  COMUNICAÇÃO COM A API
// ============================================================
void pollDeviceStatus() {
  HTTPClient http;
  http.begin(String(API_BASE) + "/device/status?mac=" + DEVICE_MAC);
  int code = http.GET();
  Serial.println("[API] GET /device/status → HTTP " + String(code));

  if (code == 200) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());

    bool validated = doc["validado"] | false;
    if (validated != deviceValidated) {
      deviceValidated = validated;
      setValidationLED(deviceValidated);
      Serial.println(deviceValidated ? "[LED] VERDE — Dispositivo validado" : "[LED] VERMELHO — Aguardando registro");
    }

    if (deviceValidated) {
      deviceFieldId  = doc["fieldId"]      | "";
      deviceName     = doc["nome"]         | "Sonda";
      riskLevel      = doc["risco"]        | "unknown";
      recommendation = doc["recomendacao"] | "";
      Serial.println("[Risco] " + riskLevel + " | " + recommendation);
    }
  }
  http.end();
}

void sendSensorData() {
  HTTPClient http;
  http.begin(String(API_BASE) + "/sensor/leitura");
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["mac"]          = DEVICE_MAC;
  doc["fieldId"]      = deviceFieldId;
  doc["umidade_solo"] = soilMoisture;
  doc["temperatura"]  = temperature;
  doc["umidade_ar"]   = airHumidity;

  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  Serial.println("[API] POST /sensor/leitura → HTTP " + String(code));
  http.end();
}

// ============================================================
//  WEBSERVER — ENDPOINTS
// ============================================================
void handleDashboard() {
  String wifiStr   = WiFi.status() == WL_CONNECTED ? "Conectado (" + WiFi.localIP().toString() + ")" : "Desconectado";
  String validStr  = deviceValidated ? "&#9989; Validado" : "&#128308; Aguardando registro";
  String riscoStr  = riskLevel == "green"  ? "&#128994; Baixo"   :
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
    "<div class='card'><h3>Dispositivo</h3>"
    "<p>" + validStr + " &nbsp; <b>" + deviceName + "</b></p></div>"
    "<div class='card'><h3>Sensores</h3>"
    "<p>Umidade do solo: <span class='val'>" + String(soilMoisture, 1) + "%</span></p>"
    "<p>Temperatura: <span class='val'>" + String(temperature, 1) + "&deg;C</span></p>"
    "<p>Umidade do ar: <span class='val'>" + String(airHumidity, 1) + "%</span></p></div>"
    "<div class='card'><h3>Risco da Lavoura</h3>"
    "<p>Nivel: <span class='val'>" + riscoStr + "</span></p>"
    + (recommendation.length() > 0 ? "<p><i>" + recommendation + "</i></p>" : "") +
    "</div>"
    "<p style='color:#aaa;font-size:12px'>Atualiza a cada 5s</p>"
    "</body></html>";

  server.send(200, "text/html", html);
}

// EP 1 — GET /sensor/leitura
void handleGetLeitura() {
  JsonDocument doc;
  doc["mac"]          = DEVICE_MAC;
  doc["umidade_solo"] = soilMoisture;
  doc["temperatura"]  = temperature;
  doc["umidade_ar"]   = airHumidity;
  doc["timestamp"]    = millis();

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// EP 2 — GET /sensor/status
void handleGetStatus() {
  JsonDocument doc;
  doc["mac"]          = DEVICE_MAC;
  doc["validado"]     = deviceValidated;
  doc["nome"]         = deviceName;
  doc["fieldId"]      = deviceFieldId;
  doc["risco"]        = riskLevel;
  doc["recomendacao"] = recommendation;
  doc["wifi"]         = WiFi.status() == WL_CONNECTED;

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
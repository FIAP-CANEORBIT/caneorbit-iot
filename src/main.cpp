#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

const char* SSID     = "Wokwi-GUEST";
const char* PASSWORD = "";
const char* API_BASE = "https://caneorbis-api-dotnet.onrender.com";
const int DEVICE_ID  = 68;

#define DHT_PIN     4  
#define SOIL_PIN   34  
#define PH_PIN     35   
#define LED_VALID_G 25  
#define LED_VALID_R 26  

DHT               dht(DHT_PIN, DHT22);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WebServer         server(80);

bool    apiOk        = false;
String  deviceName   = "Sonda";
float   soilMoisture = 0.0;
float   temperature  = 0.0;
float   airHumidity  = 0.0;
float   phSolo       = 7.0;
String  riskLevel    = "unknown";

unsigned long lastSensorRead = 0;
unsigned long lastBlinkTime  = 0;
bool          blinkState     = false;

const long SENSOR_INTERVAL = 2000;
const long API_INTERVAL    = 10000; 
const long BLINK_INTERVAL  = 500;  

void setValidationLED(bool ok);
void updateBlinkLED();
void calcularRisco();
void lcdPrint(String linha1, String linha2);
void updateDisplay();
void readSensors();
int  postLeitura();
void sendSensorData();
void handleDashboard();
void handleGetLeitura();
void handleGetStatus();
void handlePostConfig();
void taskEnvioAPI(void *pvParameters);

void setup() {
  Serial.begin(115200);

  pinMode(LED_VALID_G, OUTPUT);
  pinMode(LED_VALID_R, OUTPUT);

  digitalWrite(LED_VALID_G, LOW);
  digitalWrite(LED_VALID_R, HIGH);

  dht.begin();

  lcd.init();
  lcd.backlight();
  delay(200);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("HarvestEye Ativo");
  delay(2000);

  WiFi.begin(SSID, PASSWORD);
  lcdPrint("Conectando...", "Wi-Fi: aguardando");

  server.on("/",           HTTP_GET,  handleDashboard);
  server.on("/sensor/leitura", HTTP_GET,  handleGetLeitura);
  server.on("/sensor/status",  HTTP_GET,  handleGetStatus);
  server.on("/sensor/config",  HTTP_POST, handlePostConfig);
  server.begin();

  xTaskCreatePinnedToCore(taskEnvioAPI, "TaskAPI", 8192, NULL, 1, NULL, 0);
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  updateBlinkLED();

  static bool wifiWasConnected = false;
  if (WiFi.status() == WL_CONNECTED && !wifiWasConnected) {
    wifiWasConnected = true;
    if (!apiOk) digitalWrite(LED_VALID_R, HIGH); 
    lcdPrint("Wi-Fi OK", WiFi.localIP().toString().substring(0, 16));
    delay(1500);
  }

  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    readSensors();
    calcularRisco();
    updateDisplay();
    lastSensorRead = now;
  }
}

void taskEnvioAPI(void *pvParameters) {
  for(;;) {
    vTaskDelay(pdMS_TO_TICKS(API_INTERVAL));
    if (WiFi.status() == WL_CONNECTED) {
      sendSensorData();
    }
  }
}

void setValidationLED(bool ok) {
  digitalWrite(LED_VALID_G, ok ? HIGH : LOW);
  digitalWrite(LED_VALID_R, ok ? LOW  : HIGH);
}

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

  String linha1 = "S:" + String((int)soilMoisture) + "% T:" + String((int)temperature) + "C";
  String linha2 = "pH:" + String(phSolo, 1) + " " + statusStr;

  lcdPrint(linha1, linha2);
}

void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperature = t;
  if (!isnan(h)) airHumidity = h;

  int rawSoil  = analogRead(SOIL_PIN);
  soilMoisture = map(rawSoil, 4095, 0, 0, 100);

  int rawPh = analogRead(PH_PIN);
  phSolo    = rawPh * 14.0 / 4095.0;

  Serial.printf("[Sensores] Solo: %.1f%% | Temp: %.1f°C | UmAr: %.1f%% | pH: %.1f\n",
                soilMoisture, temperature, airHumidity, phSolo);
}

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

int postLeitura() {
  WiFiClientSecure client;
  client.setInsecure();
  
  HTTPClient http;
  
  String url = String(API_BASE) + "/api/LeituraSensor";
  if (!http.begin(client, url)) {
    Serial.println("[API] Erro ao instanciar conexao");
    return -1;
  }
  
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");
  http.setTimeout(15000); 

  float umidadeEnvio = round((soilMoisture < 0 ? 0 : soilMoisture) * 10) / 10.0;
  float tempEnvio    = round((temperature  < 0 ? 0 : temperature)  * 10) / 10.0;
  float phEnvio      = round(phSolo * 10) / 10.0;

  JsonDocument doc;
  doc["idDispositivo"] = DEVICE_ID;
  doc["vlUmidadeSolo"] = umidadeEnvio;
  doc["vlTemperatura"] = tempEnvio;
  doc["vlPhSolo"]      = phEnvio;

  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  Serial.println("[API] POST → HTTP " + String(code));
  
  if (code >= 400) {
    Serial.println("[API] Erro detalhado: " + http.getString());
  }

  http.end();
  return code;
}

void sendSensorData() {
  int code = postLeitura();
  apiOk = (code == 200 || code == 201);
  setValidationLED(apiOk);
}

void handleDashboard() {
  String wifiStr  = WiFi.status() == WL_CONNECTED
                    ? "Conectado (" + WiFi.localIP().toString() + ")"
                    : "Desconectado";
  String apiStr   = apiOk ? "&#9989; Sincronizado" : "&#128308; Aguardando envio/Erro";
  String riscoStr = riskLevel == "green"  ? "&#128994; Baixo"   :
                    riskLevel == "yellow" ? "&#129000; Atencao" :
                    riskLevel == "red"    ? "&#128308; Alto"    : "Desconhecido";

  String html =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta http-equiv='refresh' content='5'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>HarvestEye Dashboard</title>"
    "<style>body{font-family:sans-serif;padding:20px;background:#f5f5f5}"
    ".card{background:#fff;border-radius:8px;padding:16px;margin:8px 0;border:1px solid #ddd}"
    "h1{color:#1B4D3E}h3{color:#555;margin:4px 0}.val{font-size:1.4em;font-weight:bold;color:#1B4D3E}"
    "</style></head><body>"
    "<h1>&#127807; HarvestEye Monitor</h1>"
    "<div class='card'><h2>Rede</h2><p>" + wifiStr + "</p></div>"
    "<div class='card'><h3>API CaneOrbit</h3>"
    "<p>" + apiStr + " &nbsp; (Device ID: <b>" + String(DEVICE_ID) + "</b>)</p></div>"
    "<div class='card'><h3>Sensores</h3>"
    "<p>Umidade do solo: <span class='val'>" + String(soilMoisture, 1) + "%</span></p>"
    "<p>Temperatura: <span class='val'>" + String(temperature, 1) + "&deg;C</span></p>"
    "<p>Umidade do ar: <span class='val'>" + String(airHumidity, 1) + "%</span></p>"
    "<p>pH do solo: <span class='val'>" + String(phSolo, 1) + "</span></p></div>"
    "<div class='card'><h3>Risco da Lavoura</h3>"
    "<p>Nivel: <span class='val'>" + riscoStr + "</span></p></div>"
    "<p style='color:#aaa;font-size:12px'>Atualiza localmente a cada 5s</p>"
    "</body></html>";

  server.send(200, "text/html", html);
}

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
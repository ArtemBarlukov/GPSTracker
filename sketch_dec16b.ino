#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

// ======== GPS ========
TinyGPSPlus gps;
SoftwareSerial gpsSerial(D2, D3);   // GPS TX → D2, GPS RX → D3

// ======== SIM800L ========
SoftwareSerial sim800(D6, D5);      // SIM800 TX → D6, RX → D5
String ownerNumber = "+79503834599";

// ======== Wi-Fi ========
const char* WIFI_SSID = "BarArt";
const char* WIFI_PASS = "12344321";

// Твой Flask сервер (IP компьютера!)
String serverURL = "http://10.135.41.248:5000/update";

// ======== AUTO SEND (SMS + HTTP) ========
bool autoSend = false;
unsigned long lastAutoSend = 0;
const unsigned long AUTO_INTERVAL = 3000;

// ======== QUIET HTTP SEND (только HTTP, без SMS) ========
unsigned long lastQuietSend = 0;
const unsigned long QUIET_INTERVAL = 10000;


// -----------------------------------------------------
//  ОТПРАВКА HTTP POST НА FLASK
// -----------------------------------------------------
void sendToServer(float lat, float lon, int sat) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] WiFi not connected");
    return;
  }

  WiFiClient client;
  HTTPClient http;

  http.begin(client, serverURL);
  http.addHeader("Content-Type", "application/json");

// Сборка JSON
  String json = "{";
  json += "\"lat\":" + String(lat, 6) + ",";
  json += "\"lon\":" + String(lon, 6) + ",";
  json += "\"sat\":" + String(sat);
  json += "}";

  int code = http.POST(json);

  Serial.print("[HTTP] POST code: ");
  Serial.println(code);

  if (code == 200) {
    Serial.println("[HTTP] Success");
  } else {
    Serial.println("[HTTP] Error");
  }

  http.end();
}


// -----------------------------------------------------
//  ОТПРАВКА SMS
// -----------------------------------------------------
bool sendSMS(String text) {

  Serial.println("[SMS] Init...");
  sim800.listen();
  delay(200);

  sim800.println("AT+CMGF=1"); // Перевод в текстовый режим 
  delay(300);

  sim800.println("AT+CSMP=17,167,0,0");
  delay(300);

  sim800.print("AT+CMGS=\"");
  sim800.print(ownerNumber);
  sim800.println("\"");
  delay(400);

  sim800.print(text);
  delay(200);

  sim800.write(26); // CTRL+Z
  Serial.println("[SMS] Sending...");

  unsigned long t = millis();
  while (millis() - t < 7000) {
    if (sim800.available()) {
      String r = sim800.readString();
      Serial.println(r);

      if (r.indexOf("OK") > 0 || r.indexOf("+CMGS:") > 0) return true;
      if (r.indexOf("ERROR") > 0) return false;
    }
  }
  Serial.println("[SMS] TIMEOUT");
  return false;
}


// -----------------------------------------------------
//  ОТПРАВКА GPS: SMS + HTTP (для ручных запросов и авторежима)
// -----------------------------------------------------
void sendGPS_SMS() {

  if (!gps.location.isValid()) {
    sendSMS("NO FIX");
    return;
  }

  float lat = gps.location.lat();
  float lon = gps.location.lng();
  int   sat = gps.satellites.value();

  // ---------- SMS ----------
  String msg = "GPS TRACKER\n";
  msg += "LAT: " + String(lat, 6) + "\n";
  msg += "LON: " + String(lon, 6) + "\n";
  msg += "SAT: " + String(sat) + "\n";
  msg += "Google Maps:\nhttps://maps.google.com/?q=";
  msg += String(lat, 6) + "," + String(lon, 6);

  sendSMS(msg);

  // ---------- HTTP POST (для Telegram-сервера) ----------
  sendToServer(lat, lon, sat);
}


// -----------------------------------------------------
//  ТОЛЬКО ОТПРАВКА НА СЕРВЕР (без SMS)
// -----------------------------------------------------
void sendGPS_HTTP_Only() {
  
  if (!gps.location.isValid()) {
    Serial.println("[AUTO] No GPS fix, skipping");
    return;
  }

  float lat = gps.location.lat();
  float lon = gps.location.lng();
  int   sat = gps.satellites.value();

  Serial.println("[AUTO] Sending to server...");
  sendToServer(lat, lon, sat);
}


// -----------------------------------------------------
//  ОБРАБОТКА ВХОДЯЩИХ SMS
// -----------------------------------------------------
void checkIncomingSMS() {

  sim800.listen();
  if (!sim800.available()) return;

  String sms = sim800.readString();
  sms.toLowerCase();

  Serial.println("[SMS RX]:");
  Serial.println(sms);

  if (sms.indexOf("gps?") >= 0) {
    sendGPS_SMS();
    return;
  }

  if (sms.indexOf("autoon") >= 0) {
    autoSend = true;
    sendSMS("AUTO MODE: ON");
    // Сразу отправим текущие координаты при включении
    if (gps.location.isValid()) {
      sendGPS_HTTP_Only();
    }
    return;
  }

  if (sms.indexOf("autooff") >= 0) {
    autoSend = false;
    sendSMS("AUTO MODE: OFF");
    return;
  }
}


// -----------------------------------------------------
//  SETUP
// -----------------------------------------------------
void setup() {
  Serial.begin(115200);

  gpsSerial.begin(9600);
  sim800.begin(9600);

  Serial.println("=== SYSTEM READY ===");

  // SIM800 Init
  sim800.listen();
  sim800.println("AT");
  delay(500);
  while (sim800.available()) Serial.write(sim800.read());

  sim800.println("AT+CNMI=1,2,0,0,0");
  delay(300);

  // Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK!");
}


// -----------------------------------------------------
//  LOOP
// -----------------------------------------------------
void loop() {

  // GPS update
  while (gpsSerial.available())
    gps.encode(gpsSerial.read());

  // Входящие SMS
  checkIncomingSMS();

  // Команды из Serial
  if (Serial.available()) {
    String cmd = Serial.readString();
    cmd.trim();

    if (cmd.equalsIgnoreCase("gps?")) {
      sendGPS_SMS();
      return;
    }

    if (cmd.equalsIgnoreCase("gpscord")) {
      if (gps.location.isValid()) {
        Serial.println("LAT: " + String(gps.location.lat(), 6));
        Serial.println("LON: " + String(gps.location.lng(), 6));
        Serial.println("SAT: " + String(gps.satellites.value()));
      } else {
        Serial.println("NO FIX");
      }
      return;
    }

    if (cmd.equalsIgnoreCase("autoon")) {
      autoSend = true;
      Serial.println("AUTO MODE ON");
      // Сразу отправим текущие координаты
      if (gps.location.isValid()) {
        sendGPS_HTTP_Only();
      }
      return;
    }

    if (cmd.equalsIgnoreCase("autooff")) {
      autoSend = false;
      Serial.println("AUTO MODE OFF");
      return;
    }
  }

  // ===== Авто-отправка на сервер раз в 5 минут =====
  if (autoSend && millis() - lastAutoSend >= AUTO_INTERVAL) {
    lastAutoSend = millis();
    Serial.println("[AUTO] Auto-send triggered");
    sendGPS_HTTP_Only();  // Отправляем только HTTP, без SMS
  }

  // ===== ТИХАЯ ОТПРАВКА НА FLASK (работает всегда) =====
  if (gps.location.isValid() && millis() - lastQuietSend >= QUIET_INTERVAL) {
    lastQuietSend = millis();
    // В тихом режиме просто отправляем данные
    sendToServer(gps.location.lat(),
                 gps.location.lng(),
                 gps.satellites.value());
  }

  delay(100);
}

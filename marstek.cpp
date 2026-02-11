#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <ModbusTCPClient.h>
#include <HTTPClient.h>

/* =========================================================
   WLAN
   ========================================================= */
const char* WIFI_SSID = "DEIN_WLAN";
const char* WIFI_PASS = "PASSWORT";

/* =========================================================
   myPV SmartMeter (ModbusTCP)
   → SmartMeter = SERVER
   → ESP32      = CLIENT
   ========================================================= */
IPAddress SMARTMETER_IP(192,168,1,50);
const uint16_t SMARTMETER_PORT = 502;
ModbusTCPClient modbus;

/* =========================================================
   Marstek Venus E (UDP Open API)
   ========================================================= */
IPAddress MARSTEK_IP(192,168,1,60);
const uint16_t MARSTEK_PORT = 30000;
WiFiUDP udp;

/* =========================================================
   InfluxDB / Home Assistant
   ========================================================= */
const char* INFLUX_URL = "http://192.168.1.10:8086/write?db=pv";
const char* HA_URL     = "http://192.168.1.10:8123/api/states/sensor.pv";

/* =========================================================
   Timing
   ========================================================= */
unsigned long lastControlMs = 0;
unsigned long lastLogMs     = 0;

/* =========================================================
   Regelparameter
   ========================================================= */
#define GRID_TARGET_W     0
#define MAX_CHARGE_W   4000
#define MAX_DISCHARGE_W 4000

/* =========================================================
   Modbus Register (myPV Dokumentation!)
   ========================================================= */
#define REG_GRID_POWER 30001  // Netzleistung (+ Bezug / − Einspeisung)

/* =========================================================
   Hilfsfunktionen
   ========================================================= */

/* -------- Netzleistung lesen (kritisch!) -------- */
int readGridPower() {
  if (!modbus.connected()) {
    modbus.begin(SMARTMETER_IP, SMARTMETER_PORT);
  }

  uint16_t raw;
  if (!modbus.holdingRegisterRead(1, REG_GRID_POWER, raw)) {
    // FEHLER → wir regeln trotzdem weiter mit letztem Wert
    return 0;
  }

  // myPV liefert signed 16 bit
  return (int16_t)raw;
}

/* -------- Marstek Leistung setzen -------- */
void setMarstekPower(int powerW) {

  // Begrenzen – Sicherheit!
  powerW = constrain(powerW, -MAX_CHARGE_W, MAX_DISCHARGE_W);

  StaticJsonDocument<256> doc;
  doc["id"] = 1;
  doc["method"] = "ES.SetMode";

  JsonObject params = doc.createNestedObject("params");
  params["id"] = 0;

  JsonObject cfg = params.createNestedObject("config");
  cfg["mode"] = "Passive";

  JsonObject passive = cfg.createNestedObject("passive_cfg");
  passive["power"]   = powerW;
  passive["cd_time"] = 15;   // wichtig: Watchdog im Speicher

  char buffer[256];
  serializeJson(doc, buffer);

  udp.beginPacket(MARSTEK_IP, MARSTEK_PORT);
  udp.write((uint8_t*)buffer, strlen(buffer));
  udp.endPacket();
}

/* -------- Logging (unkritisch!) -------- */
void logData(int gridPower) {
  HTTPClient http;
  http.begin(INFLUX_URL);
  http.POST("grid_power value=" + String(gridPower));
  http.end();
}

/* =========================================================
   SETUP
   ========================================================= */
void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  udp.begin(40000); // lokaler UDP-Port
}

/* =========================================================
   LOOP
   ========================================================= */
void loop() {
  unsigned long now = millis();

  /* ================== REGELUNG (1 s) ================== */
  if (now - lastControlMs >= 1000) {
    lastControlMs = now;

    int gridPower = readGridPower();

    // Ziel: Netzleistung = 0 W
    int batteryPower = -gridPower;

    setMarstekPower(batteryPower);
  }

  /* ================== LOGGING (10 s) ================== */
  if (now - lastLogMs >= 10000) {
    lastLogMs = now;

    int gridPower = readGridPower();
    logData(gridPower);
  }
}

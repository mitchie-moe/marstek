/************************************************************
 * ESP32 PV-Überschussregelung
 * + Telemetrie zu InfluxDB & Home Assistant (alle 10 s)
 ************************************************************/

#include <Ethernet.h>
#include <ArduinoModbus.h>
#include <HTTPClient.h>

/**************** Netzwerk ****************/

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x32, 0x01 };
IPAddress ip(192,168,1,50);

IPAddress smartMeterIP(192,168,1,20);
IPAddress venusIP(192,168,1,30);

/**************** InfluxDB ****************/

const char* INFLUX_URL = "http://192.168.1.100:8086/api/v2/write?org=home&bucket=energy&precision=s";
const char* INFLUX_TOKEN = "INFLUX_TOKEN";

/**************** Home Assistant ****************/

const char* HA_URL = "http://192.168.1.10:8123/api/states/sensor.esp32_pv_controller";
const char* HA_TOKEN = "HA_LONG_LIVED_TOKEN";

/**************** Regelparameter ****************/

const int LOOP_TIME_MS = 100;
const int LOG_INTERVAL_MS = 10000;

const int DEADZONE_W = 80;
const int MAX_CHARGE_W = 2500;
const int MAX_DISCHARGE_W = 2500;
const int RAMP_W = 300;

/**************** Laufzeit ****************/

int currentPowerSetpoint = 0;
int lastGridPower = 0;

unsigned long lastLogTime = 0;

/**************** Setup ****************/

void setup() {
  Ethernet.begin(mac, ip);
  delay(1000);

  ModbusTCPClient.begin(smartMeterIP);

  Serial.begin(115200);
  Serial.println("ESP32 Speicherregelung + Telemetrie gestartet");
}

/**************** Loop ****************/

void loop() {

  if (!ModbusTCPClient.connected()) {
    ModbusTCPClient.begin(smartMeterIP);
    delay(50);
  }

  /******** Netzleistung lesen ********/

  ModbusTCPClient.requestFrom(
    1,
    HOLDING_REGISTERS,
    30001,
    1
  );

  int gridPower = ModbusTCPClient.read();
  lastGridPower = gridPower;

  /******** Zielwert berechnen ********/

  int targetPower = 0;

  if (gridPower < -DEADZONE_W) {
    targetPower = constrain(-gridPower, 0, MAX_CHARGE_W);
  }
  else if (gridPower > DEADZONE_W) {
    targetPower = -constrain(gridPower, 0, MAX_DISCHARGE_W);
  }

  /******** Rampe ********/

  if (targetPower > currentPowerSetpoint + RAMP_W)
    currentPowerSetpoint += RAMP_W;
  else if (targetPower < currentPowerSetpoint - RAMP_W)
    currentPowerSetpoint -= RAMP_W;
  else
    currentPowerSetpoint = targetPower;

  /******** Speicher steuern ********/

  sendPowerToVenus(currentPowerSetpoint);

  /******** Telemetrie alle 10 s ********/

  if (millis() - lastLogTime > LOG_INTERVAL_MS) {
    lastLogTime = millis();
    sendToInflux(lastGridPower, currentPowerSetpoint);
    sendToHomeAssistant(lastGridPower, currentPowerSetpoint);
  }

  delay(LOOP_TIME_MS);
}

/**************** Venus-E ****************/

void sendPowerToVenus(int powerW) {

  HTTPClient http;
  String url = "http://" + venusIP.toString() + "/api/setPower";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String payload = "{ \"power\": " + String(powerW) + " }";
  http.POST(payload);
  http.end();
}

/**************** InfluxDB ****************/

void sendToInflux(int gridW, int storageW) {

  HTTPClient http;
  http.begin(INFLUX_URL);
  http.addHeader("Authorization", String("Token ") + INFLUX_TOKEN);
  http.addHeader("Content-Type", "text/plain");

  // Line Protocol
  String line =
    "pv_controller,device=esp32 "
    "grid_power=" + String(gridW) + "," +
    "storage_power=" + String(storageW);

  http.POST(line);
  http.end();
}

/**************** Home Assistant ****************/

void sendToHomeAssistant(int gridW, int storageW) {

  HTTPClient http;
  http.begin(HA_URL);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  http.addHeader("Content-Type", "application/json");

  String payload =
    "{"
    "\"state\": \"" + String(gridW) + "\","
    "\"attributes\": {"
      "\"storage_power\": " + String(storageW) + ","
      "\"unit_of_measurement\": \"W\","
      "\"friendly_name\": \"ESP32 PV Controller\""
    "}"
    "}";

  http.PUT(payload);
  http.end();
}

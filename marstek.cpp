#include <Ethernet.h>
#include <ArduinoModbus.h>
#include <HTTPClient.h>

// -------- Netzwerk --------
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x01 };
IPAddress ip(192,168,1,50);
IPAddress mypv(192,168,1,20);
IPAddress venus(192,168,1,30);

// -------- Parameter --------
const int LOOP_MS = 100;
const int DEADZONE = 80;
const int MAX_PWR = 2500;
const int RAMP = 300;

int currentPower = 0;

// -------- Setup --------
void setup() {
  Ethernet.begin(mac, ip);
  ModbusTCPClient.begin(mypv);
}

// -------- Regel-Loop --------
void loop() {
  if (!ModbusTCPClient.connected()) {
    ModbusTCPClient.begin(mypv);
    delay(50);
  }

  // Register-Beispiel: Netzleistung
  // NEGATIV = Einspeisung
  ModbusTCPClient.requestFrom(1, HOLDING_REGISTERS, 30001, 1);
  int gridPower = ModbusTCPClient.read();

  int targetPower = 0;

  if (gridPower < -DEADZONE) {
    targetPower = constrain(-gridPower, 0, MAX_PWR);
  }
  else if (gridPower > DEADZONE) {
    targetPower = -constrain(gridPower, 0, MAX_PWR);
  }

  // Rampenbegrenzung
  if (targetPower > currentPower + RAMP)
    currentPower += RAMP;
  else if (targetPower < currentPower - RAMP)
    currentPower -= RAMP;
  else
    currentPower = targetPower;

  sendToVenus(currentPower);
  delay(LOOP_MS);
}

// -------- Venus-E Steuerung --------
void sendToVenus(int power) {
  HTTPClient http;
  String url = "http://" + venus.toString() + "/api/power";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"power\":" + String(power) + "}";
  http.POST(payload);
  http.end();
}

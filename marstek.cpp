/****************************************************
 * ESP32 PV-Überschussregelung
 *
 * Priorität:
 * 1. Speicher (Venus-E) regelt Netzleistung auf 0 W
 * 2. AC•THOR regelt separat auf -250 W
 *
 * Der ESP32:
 * - liest Netzleistung vom myPV SmartMeter (Modbus TCP)
 * - berechnet Lade-/Entladeleistung für den Speicher
 * - sendet Sollleistung an den Venus-E
 *
 * KEIN Home Assistant
 * KEINE Cloud
 * Minimalste Latenz
 ****************************************************/

#include <Ethernet.h>
#include <ArduinoModbus.h>
#include <HTTPClient.h>

/************ Netzwerkkonfiguration ************/

// MAC-Adresse frei wählbar (muss eindeutig sein)
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x32, 0x01 };

// Feste IP für ESP32 (empfohlen!)
IPAddress ip(192, 168, 1, 50);

// IPs der Zielgeräte
IPAddress smartMeterIP(192, 168, 1, 20);
IPAddress venusIP(192, 168, 1, 30);

/************ Regelparameter ************/

// Regelzyklus (ms)
// 100 ms = sehr schnell, aber stabil
const int LOOP_TIME_MS = 100;

// Totband um 0 W, um Oszillation zu vermeiden
const int DEADZONE_W = 80;

// Maximale Lade- / Entladeleistung des Speichers
const int MAX_CHARGE_W = 2500;
const int MAX_DISCHARGE_W = 2500;

// Rampenbegrenzung (wie schnell Leistung geändert wird)
const int RAMP_W_PER_CYCLE = 300;

// Minimaler SOC (wird hier NICHT ausgelesen, nur vorbereitet)
const int MIN_SOC = 10;

/************ Laufzeitvariablen ************/

// Aktuell gesetzte Speicherleistung
// positiv = Laden
// negativ = Entladen
int currentPowerSetpoint = 0;

/************ Setup ************/

void setup() {
  // Ethernet starten
  Ethernet.begin(mac, ip);

  // Kurze Pause für sauberen Netzwerkstart
  delay(1000);

  // Modbus TCP Client starten
  ModbusTCPClient.begin(smartMeterIP);

  // Optional: Debug über USB
  Serial.begin(115200);
  Serial.println("ESP32 Speicherregelung gestartet");
}

/************ Hauptregel-Loop ************/

void loop() {

  // Sicherstellen, dass Modbus verbunden ist
  if (!ModbusTCPClient.connected()) {
    ModbusTCPClient.begin(smartMeterIP);
    delay(50);
  }

  /************ 1. Netzleistung lesen ************/

  // Register 30001 (Beispiel!)
  // NEGATIV = Einspeisung
  // POSITIV = Netzbezug
  ModbusTCPClient.requestFrom(
    1,                    // Slave-ID
    HOLDING_REGISTERS,
    30001,                // Startregister
    1                     // Anzahl Register
  );

  int gridPowerW = ModbusTCPClient.read();

  /************ 2. Ziel-Leistung berechnen ************/

  int targetPower = 0;

  // Fall 1: Einspeisung → Speicher laden
  if (gridPowerW < -DEADZONE_W) {
    targetPower = constrain(
      -gridPowerW,         // Einspeisung positiv machen
      0,
      MAX_CHARGE_W
    );
  }

  // Fall 2: Netzbezug → Speicher entladen
  else if (gridPowerW > DEADZONE_W) {
    targetPower = -constrain(
      gridPowerW,
      0,
      MAX_DISCHARGE_W
    );
  }

  // Fall 3: Im Totband → nichts ändern
  else {
    targetPower = 0;
  }

  /************ 3. Rampenbegrenzung ************/

  if (targetPower > currentPowerSetpoint + RAMP_W_PER_CYCLE) {
    currentPowerSetpoint += RAMP_W_PER_CYCLE;
  }
  else if (targetPower < currentPowerSetpoint - RAMP_W_PER_CYCLE) {
    currentPowerSetpoint -= RAMP_W_PER_CYCLE;
  }
  else {
    currentPowerSetpoint = targetPower;
  }

  /************ 4. Sollwert an Venus-E senden ************/

  sendPowerToVenus(currentPowerSetpoint);

  /************ 5. Warten bis nächster Zyklus ************/
  delay(LOOP_TIME_MS);
}

/************ Venus-E API-Aufruf ************/

void sendPowerToVenus(int powerW) {

  HTTPClient http;

  // Beispiel-Endpoint – ggf. anpassen!
  String url = "http://" + venusIP.toString() + "/api/setPower";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // JSON Payload:
  // power > 0  → Laden
  // power < 0  → Entladen
  String payload = "{ \"power\": " + String(powerW) + " }";

  http.POST(payload);
  http.end();

  // Debug
  Serial.print("Grid: ");
  Serial.print(powerW);
  Serial.println(" W an Venus-E gesendet");
}

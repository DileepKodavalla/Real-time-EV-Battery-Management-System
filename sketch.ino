#define BLYNK_TEMPLATE_ID "TMPL3x5qeWMsN"
#define BLYNK_TEMPLATE_NAME "EV BMS"
#define BLYNK_PRINT Serial

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

// --- BLYNK & WIFI CREDENTIALS ---
char auth[] = "BL5dFAOptaxqW2xKkixr0Icgs5NniuzO";
char ssid[] = "Wokwi-GUEST";
char pass[] = "";

// --- PIN DEFINITIONS (Matched to diagram.json) ---
const int POT1_PIN = 34; // Cell 1 (pot4)
const int POT2_PIN = 35; // Cell 2 (pot1)
const int POT3_PIN = 32; // Cell 3 (pot2)
const int POT4_PIN = 33; // Cell 4 (pot3)

const int LED_RED_PIN    = 2;  // led1
const int LED_YELLOW_PIN = 5;  // led2
const int LED_GREEN_PIN  = 4;  // led3
const int BUZZER_PIN     = 18; // bz1
const int RELAY_PIN      = 19; // relay1 

// --- LCD CONFIGURATION ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- SYSTEM CONSTANTS & THRESHOLDS ---
const float CELL_MIN_SAFE_V = 2.20; 
const float CELL_MAX_SAFE_V = 3.20; 
const float IMBALANCE_MINOR = 2.0;  
const float IMBALANCE_CRIT  = 5.0;  

// Timing intervals (milliseconds)
const unsigned long SAMPLE_INTERVAL     = 200;   
const unsigned long TELEMETRY_INTERVAL  = 1000;  
const unsigned long DISPLAY_INTERVAL    = 3000; 
const unsigned long DEBOUNCE_DELAY      = 1000; 
const unsigned long FREEZE_TIMEOUT      = 15000; 
const unsigned long WIFI_CHECK_INTERVAL = 10000; 

// --- FSM & RUNTIME MODES ---
enum BatteryState {
  STATE_HEALTHY,
  STATE_MINOR_IMBALANCE,
  STATE_CRITICAL_IMBALANCE,
  STATE_PACK_FAILURE
};

enum RuntimeMode {
  MODE_NORMAL,
  MODE_DEGRADED,
  MODE_FAILSAFE,
  MODE_SHUTDOWN
};

BatteryState currentState = STATE_HEALTHY;
BatteryState lastRenderedState = STATE_HEALTHY; 
RuntimeMode currentRuntimeMode = MODE_NORMAL;
RuntimeMode lastRenderedMode = MODE_NORMAL;

// --- CLOUD EVENT TRACKING & EXECUTIVE METRICS ---
RuntimeMode lastCloudReportedMode = MODE_NORMAL;
BatteryState lastCloudReportedState = STATE_HEALTHY;
bool cloudSyncPending = false;
unsigned long lastWiFiCheckTime = 0;
int wifiSignalRSSI = 0;
int riskScore = 0;

// --- GLOBAL VARIABLES & FAULT TRACKERS ---
float cellVoltages[4] = {0.0, 0.0, 0.0, 0.0};
float previousVoltages[4] = {0.0, 0.0, 0.0, 0.0};
unsigned long lastChangeTime[4] = {0, 0, 0, 0};

float avgVoltage = 0.0;
float imbalancePercent = 0.0;
int strongestCellIndex = 0;
int weakestCellIndex = 0;

unsigned long lastSampleTime = 0;
unsigned long lastTelemetryTime = 0;
unsigned long lastDisplayToggle = 0;
unsigned long lastRelayToggleTime = 0;
bool lastRelayState = LOW; 
int currentDisplayPage = 0; 

bool sensorDisconnected = false;
bool adcFrozen = false;
bool voltageFault = false;
int faultyCell = 0;

bool prevCritImb = false;
bool prevPackFail = false;
bool prevAdcFrozen = false;

void setup() {
  Serial.begin(115200);

  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_YELLOW_PIN, LOW);
  digitalWrite(LED_RED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RELAY_PIN, LOW); 

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Executive BMS");
  lcd.setCursor(0, 1);
  lcd.print("Connecting WiFi");

  WiFi.begin(ssid, pass);
  Blynk.config(auth);
  
  delay(1500);
  lcd.clear();
}

void loop() {
  unsigned long currentMillis = millis();

  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }

  if (currentMillis - lastWiFiCheckTime >= WIFI_CHECK_INTERVAL) {
    lastWiFiCheckTime = currentMillis;
    handleWiFiReconnection();
  }

  if (currentMillis - lastSampleTime >= SAMPLE_INTERVAL) {
    lastSampleTime = currentMillis;
    readAndProcessSensors(currentMillis);
    evaluateBMSState(currentMillis);
    evaluateFaults(currentMillis);
    calculateRiskScore();
    checkAndLogFaults(currentMillis);
    updateActuators(currentMillis);
    checkCloudTriggers(); 
  }

  if (currentMillis - lastTelemetryTime >= TELEMETRY_INTERVAL) {
    lastTelemetryTime = currentMillis;
    sendTelemetry();
  }

  updateDisplay(currentMillis);
}

void handleWiFiReconnection() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    cloudSyncPending = true; 
  } else {
    wifiSignalRSSI = WiFi.RSSI();
    if (cloudSyncPending && Blynk.connected()) {
      syncCloudEventQueue();
      cloudSyncPending = false;
    }
  }
}

void readAndProcessSensors(unsigned long currentMillis) {
  sensorDisconnected = false;
  adcFrozen = false;

  for (int i = 0; i < 4; i++) {
    int rawVal = 0;
    if (i == 0) rawVal = analogRead(POT1_PIN);
    else if (i == 1) rawVal = analogRead(POT2_PIN);
    else if (i == 2) rawVal = analogRead(POT3_PIN);
    else if (i == 3) rawVal = analogRead(POT4_PIN);

    float calculatedV = (rawVal / 4095.0) * 3.30;

    if (calculatedV < 0.02 || calculatedV > 3.28) {
      sensorDisconnected = true;
    }

    if (abs(calculatedV - previousVoltages[i]) > 0.015) {
      for (int j = 0; j < 4; j++) {
        lastChangeTime[j] = currentMillis;
      }
      previousVoltages[i] = calculatedV;
    } else if (currentMillis - lastChangeTime[i] > FREEZE_TIMEOUT) {
      adcFrozen = true;
    }

    cellVoltages[i] = calculatedV;
  }

  float sum = 0.0;
  float minV = cellVoltages[0];
  float maxV = cellVoltages[0];
  strongestCellIndex = 0;
  weakestCellIndex = 0;

  for (int i = 0; i < 4; i++) {
    sum += cellVoltages[i];
    if (cellVoltages[i] > maxV) {
      maxV = cellVoltages[i];
      strongestCellIndex = i;
    }
    if (cellVoltages[i] < minV) {
      minV = cellVoltages[i];
      weakestCellIndex = i;
    }
  }
  avgVoltage = sum / 4.0;

  if (avgVoltage > 0.0) {
    imbalancePercent = ((maxV - minV) / avgVoltage) * 100.0;
  } else {
    imbalancePercent = 0.0;
  }
}

void evaluateBMSState(unsigned long currentMillis) {
  voltageFault = false;
  faultyCell = 0;

  for (int i = 0; i < 4; i++) {
    if (cellVoltages[i] < CELL_MIN_SAFE_V || cellVoltages[i] > CELL_MAX_SAFE_V) {
      voltageFault = true;
      faultyCell = i + 1;
      break;
    }
  }

  if (voltageFault || sensorDisconnected) {
    currentState = STATE_PACK_FAILURE;
  } else if (imbalancePercent >= IMBALANCE_CRIT) {
    currentState = STATE_CRITICAL_IMBALANCE;
  } else if (imbalancePercent >= IMBALANCE_MINOR) {
    currentState = STATE_MINOR_IMBALANCE;
  } else {
    currentState = STATE_HEALTHY;
  }
}

void evaluateFaults(unsigned long currentMillis) {
  if (currentState == STATE_PACK_FAILURE) {
    currentRuntimeMode = MODE_SHUTDOWN;
  } else if (adcFrozen) {
    currentRuntimeMode = MODE_FAILSAFE;
  } else if (currentState == STATE_CRITICAL_IMBALANCE || currentState == STATE_MINOR_IMBALANCE) {
    currentRuntimeMode = MODE_DEGRADED;
  } else {
    currentRuntimeMode = MODE_NORMAL;
  }
}

// --- RISK ANALYSIS ENGINE ---
void calculateRiskScore() {
  if (currentRuntimeMode == MODE_SHUTDOWN) {
    riskScore = 100;
  } else if (currentRuntimeMode == MODE_FAILSAFE) {
    riskScore = 75;
  } else if (currentRuntimeMode == MODE_DEGRADED) {
    riskScore = (imbalancePercent >= IMBALANCE_CRIT) ? 60 : 35;
  } else {
    riskScore = 5;
  }
}

void checkCloudTriggers() {
  bool stateChanged = (currentState != lastCloudReportedState || currentRuntimeMode != lastCloudReportedMode);
  bool anomalyDetected = (currentState == STATE_CRITICAL_IMBALANCE || currentState == STATE_PACK_FAILURE || adcFrozen);

  if (stateChanged || anomalyDetected) {
    lastCloudReportedState = currentState;
    lastCloudReportedMode = currentRuntimeMode;

    if (Blynk.connected()) {
      transmitToCloud();
    } else {
      cloudSyncPending = true; 
    }
  }
}

void transmitToCloud() {
  Blynk.virtualWrite(V0, avgVoltage);
  Blynk.virtualWrite(V1, imbalancePercent);
  Blynk.virtualWrite(V2, currentRuntimeMode);
  Blynk.virtualWrite(V3, wifiSignalRSSI);
  Blynk.virtualWrite(V4, currentState);
  Blynk.virtualWrite(V6, riskScore);
  Blynk.virtualWrite(V7, strongestCellIndex + 1); // Stream Strongest Cell Number (1-4)
  Blynk.virtualWrite(V8, weakestCellIndex + 1);   // Stream Weakest Cell Number (1-4)
}

void syncCloudEventQueue() {
  transmitToCloud();
  Serial.println("[CLOUD SYNC] Executive event queue synchronized.");
}

void checkAndLogFaults(unsigned long currentMillis) {
  bool isCritImb = (currentState == STATE_CRITICAL_IMBALANCE);
  if (isCritImb && !prevCritImb) {
    logFault(currentMillis, "ERR_CRITICAL_IMBALANCE", 0);
    prevCritImb = true;
  } else if (!isCritImb) {
    prevCritImb = false;
  }

  bool isPackFail = (currentState == STATE_PACK_FAILURE);
  if (isPackFail && !prevPackFail) {
    if (sensorDisconnected) {
      logFault(currentMillis, "ERR_SENSOR_DISCONNECT", faultyCell > 0 ? faultyCell : 1);
    } else {
      logFault(currentMillis, "ERR_PACK_FAILURE", faultyCell);
    }
    prevPackFail = true;
  } else if (!isPackFail) {
    prevPackFail = false;
  }

  if (adcFrozen && !prevAdcFrozen) {
    logFault(currentMillis, "ERR_ADC_FROZEN", 0);
    prevAdcFrozen = true;
  } else if (!adcFrozen) {
    prevAdcFrozen = false;
  }
}

void updateActuators(unsigned long currentMillis) {
  bool requestedTrip = false;

  switch (currentRuntimeMode) {
    case MODE_NORMAL:
      digitalWrite(LED_GREEN_PIN, HIGH);
      digitalWrite(LED_YELLOW_PIN, LOW);
      digitalWrite(LED_RED_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);
      requestedTrip = false; 
      break;

    case MODE_DEGRADED:
      digitalWrite(LED_GREEN_PIN, LOW);
      digitalWrite(LED_YELLOW_PIN, HIGH);
      if (currentState == STATE_CRITICAL_IMBALANCE) {
        digitalWrite(LED_RED_PIN, HIGH);
        digitalWrite(BUZZER_PIN, (currentMillis / 500) % 2); 
      } else {
        digitalWrite(LED_RED_PIN, LOW);
        digitalWrite(BUZZER_PIN, LOW);
      }
      requestedTrip = false; 
      break;

    case MODE_FAILSAFE:
      digitalWrite(LED_GREEN_PIN, LOW);
      digitalWrite(LED_YELLOW_PIN, HIGH);
      digitalWrite(LED_RED_PIN, HIGH);
      digitalWrite(BUZZER_PIN, (currentMillis / 300) % 2); 
      requestedTrip = false; 
      break;

    case MODE_SHUTDOWN:
      digitalWrite(LED_GREEN_PIN, LOW);
      digitalWrite(LED_YELLOW_PIN, LOW);
      digitalWrite(LED_RED_PIN, HIGH);
      digitalWrite(BUZZER_PIN, (currentMillis / 200) % 2); 
      requestedTrip = true; 
      break;
  }

  bool targetRelayState = requestedTrip ? HIGH : LOW; 

  if (targetRelayState != lastRelayState) {
    if (requestedTrip) {
      lastRelayState = targetRelayState;
      digitalWrite(RELAY_PIN, lastRelayState);
      lastRelayToggleTime = currentMillis;
    } 
    else {
      if (currentMillis - lastRelayToggleTime >= DEBOUNCE_DELAY) {
        lastRelayState = targetRelayState;
        digitalWrite(RELAY_PIN, lastRelayState);
        lastRelayToggleTime = currentMillis;
      }
    }
  }
}

void updateDisplay(unsigned long currentMillis) {
  if (currentState != lastRenderedState || currentRuntimeMode != lastRenderedMode) {
    lastRenderedState = currentState;
    lastRenderedMode = currentRuntimeMode;
    lcd.clear();
  }

  if (currentRuntimeMode == MODE_FAILSAFE) {
    lcd.setCursor(0, 0);
    lcd.print("MODE: FAILSAFE  ");
    lcd.setCursor(0, 1);
    lcd.print("ERR: ADC FROZEN ");
    return;
  }

  if (currentRuntimeMode == MODE_SHUTDOWN || currentState == STATE_PACK_FAILURE) {
    lcd.setCursor(0, 0);
    lcd.print("PACK FAILURE    "); 
    lcd.setCursor(0, 1);
    lcd.print("SHUTDOWN ACTIVE ");
    return; 
  }

  if (currentState == STATE_CRITICAL_IMBALANCE) {
    lcd.setCursor(0, 0);
    lcd.print("WARN: CRIT IMBAL");
    lcd.setCursor(0, 1);
    lcd.print("W:C");
    lcd.print(weakestCellIndex + 1);
    lcd.print(" V:");
    lcd.print(cellVoltages[weakestCellIndex], 2);
    lcd.print("V   ");
    return;
  }

  if (currentMillis - lastDisplayToggle >= DISPLAY_INTERVAL) {
    lastDisplayToggle = currentMillis;
    currentDisplayPage = !currentDisplayPage; 
    lcd.clear(); 
  }

  if (currentDisplayPage == 0) {
    lcd.setCursor(0, 0);
    lcd.print("C1:");
    lcd.print(cellVoltages[0], 2);
    lcd.print("V C2:");
    lcd.print(cellVoltages[1], 2);
    lcd.print("V   ");

    lcd.setCursor(0, 1);
    lcd.print("C3:");
    lcd.print(cellVoltages[2], 2);
    lcd.print("V C4:");
    lcd.print(cellVoltages[3], 2);
    lcd.print("V   ");
  } 
  else {
    lcd.setCursor(0, 0);
    lcd.print("A:");
    lcd.print(avgVoltage, 2);
    lcd.print("V I:");
    lcd.print(imbalancePercent, 1);
    lcd.print("%      ");

    lcd.setCursor(0, 1);
    lcd.print("Risk Score: ");
    lcd.print(riskScore);
    lcd.print("  ");
  }
}

void logFault(unsigned long timestamp, const char* errorCode, int cellNum) {
  Serial.println("\n\n==================================================");
  Serial.println("         [EXECUTIVE BMS FAULT LOGGED]             ");
  Serial.println("==================================================");
  Serial.print("  -> Timestamp : T = "); Serial.print(timestamp); Serial.println(" ms");
  Serial.print("  -> Error Code: "); Serial.println(errorCode);
  if (cellNum > 0) {
    Serial.print("  -> Offender  : Cell "); Serial.println(cellNum);
  }
  Serial.println("==================================================\n\n");
  
  if (Blynk.connected()) {
    String conditionLine = "";
    String directiveLine = "";

    String codeStr = String(errorCode);
    if (codeStr == "ERR_CRITICAL_IMBALANCE") {
      conditionLine = "[WARNING: CRIT IMBALANCE] Imbalance >= 5.0%";
      directiveLine = "-> DIRECTIVE: Inspect balancing bleed resistors & check cell drift.";
    } else if (codeStr == "ERR_SENSOR_DISCONNECT") {
      conditionLine = "[SHUTDOWN: SENSOR FAULT] Open circuit on Cell " + String(cellNum);
      directiveLine = "-> DIRECTIVE: Verify analog pin wiring harness & cell terminals.";
    } else if (codeStr == "ERR_ADC_FROZEN") {
      conditionLine = "[FAILSAFE: ADC FROZEN] Static readings > 15s";
      directiveLine = "-> DIRECTIVE: Verify I2C bus & sensor health.";
    } else if (codeStr == "ERR_PACK_FAILURE") {
      conditionLine = "[CRITICAL: PACK FAILURE] Cell voltage out of bounds";
      directiveLine = "-> DIRECTIVE: Emergency shutdown active. Check cell voltage bounds.";
    }

    // Stack Condition on Line 1 and Directive on Line 2 with a clean newline
    String terminalPayload = conditionLine + "\n" + directiveLine + "\n-----------------------------------";
    Blynk.virtualWrite(V5, terminalPayload);
  }
}

void sendTelemetry() {
  Serial.print("TELEMETRY >> ");
  Serial.print("C1:"); Serial.print(cellVoltages[0], 2); Serial.print("V | ");
  Serial.print("C2:"); Serial.print(cellVoltages[1], 2); Serial.print("V | ");
  Serial.print("C3:"); Serial.print(cellVoltages[2], 2); Serial.print("V | ");
  Serial.print("C4:"); Serial.print(cellVoltages[3], 2); Serial.print("V | ");
  Serial.print("Avg:"); Serial.print(avgVoltage, 2); Serial.print("V | ");
  Serial.print("Imb:"); Serial.print(imbalancePercent, 1); Serial.print("% | ");
  Serial.print("Strongest: C"); Serial.print(strongestCellIndex + 1); Serial.print(" | ");
  Serial.print("Weakest: C"); Serial.print(weakestCellIndex + 1); Serial.print(" | ");
  Serial.print("Risk:"); Serial.print(riskScore); Serial.print(" | ");
  
  Serial.print("Mode:");
  if (currentRuntimeMode == MODE_NORMAL) Serial.print("NORMAL");
  else if (currentRuntimeMode == MODE_DEGRADED) Serial.print("DEGRADED");
  else if (currentRuntimeMode == MODE_FAILSAFE) Serial.print("FAILSAFE");
  else if (currentRuntimeMode == MODE_SHUTDOWN) Serial.print("SHUTDOWN");

  Serial.print(" | State:");
  if (currentState == STATE_HEALTHY) Serial.println("HEALTHY");
  else if (currentState == STATE_MINOR_IMBALANCE) Serial.println("MINOR_IMBALANCE");
  else if (currentState == STATE_CRITICAL_IMBALANCE) Serial.println("CRITICAL_IMBALANCE");
  else if (currentState == STATE_PACK_FAILURE) Serial.println("PACK_FAILURE");
}
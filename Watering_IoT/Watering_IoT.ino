/*
ESP32 Smart Watering System
Features:
- Manual Mode (via V-One) with timeout
- Emergency Stop
- Fuzzy logic pump control:
    Fuzzy 1 = 20s pulse per 60s cycle
    Fuzzy 2 = 40s pulse per 60s cycle
- Telemetry reporting every 60s
- LED indicators: Green = pump ON, Red = pump OFF
*/

#include "VOneMqttClient.h"
#include "DHT.h"

// ===== DEVICE IDS =====
const char* DHT11Sensor = "993a0fea-2be0-4427-9f60-3363c68b2f07";   
const char* RainSensor = "7065fa0a-0958-4610-8e82-f3bd1530ae72";       
const char* MoistureSensor = "1a08cd4e-e79f-4bd3-911b-c28798e09112";  
const char* RelayWaterPump = "8ae703d6-c543-4e83-9cdc-9a0c5f3ceb98";

// ===== PIN CONFIG =====
const int DHTPIN = 21;
const int MOISTURE_PIN = 35;
const int RAIN_PIN = 23;
const int RELAY_PIN = 32;
const int LED_Pin_R = 18;
const int LED_Pin_G = 19;

#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

VOneMqttClient voneClient;

// ===== SOIL SENSOR CONFIG =====
int MinDepth = 4095; // Dry
int MaxDepth = 2170; // Wet

// ===== MANUAL OVERRIDE =====
const unsigned long MANUAL_TIMEOUT = 30000; // 30s for demo
unsigned long manualStartTime = 0;

// ===== CONTROL STATE =====
enum ControlMode {
  AUTO_MODE,
  MANUAL_MODE,
  EMERGENCY_STOP
};
ControlMode mode = AUTO_MODE;

// ===== GLOBALS =====
unsigned long lastMsg = 0;
bool pumpState = false;
bool lastPumpState = false;

// ===== FUZZY LOGIC =====
int fuzzyState = 0;
bool pulseRunning = false;
unsigned long pulseEndTime = 0;
unsigned long cycleStart = 0;

const unsigned long TELEMETRY_CYCLE = 60000; // 60s
const unsigned long FUZZY1_PULSE = 500;    // 20s
const unsigned long FUZZY2_PULSE = 1200;    // 40s

bool manualLock = false; // Prevent AUTO when manual or emergency

// ===== WIFI =====
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void syncTime() {
  configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");
  Serial.print("Waiting for NTP time sync");
  time_t now = time(nullptr);

  while (now < 100000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }

  Serial.println("\nTime synchronized");
}

// ===== V-ONE ACTUATOR CALLBACK =====
void onActuatorReceived(const char* deviceId, const char* payload) {
  if (strcmp(deviceId, RelayWaterPump) != 0) return;

  JSONVar cmd = JSON.parse(payload);
  if (JSON.typeof(cmd) == "undefined") return;

  bool userCmd = (bool)cmd["Relay"];

  if (userCmd) {
    // Manual ON
    mode = MANUAL_MODE;
    pumpState = true;
    manualStartTime = millis() + MANUAL_TIMEOUT;
    manualLock = true;
    Serial.println("👆 MANUAL MODE: Pump Forced ON");
  } else {
    // Emergency OFF
    mode = EMERGENCY_STOP;
    pumpState = false;
    pulseRunning = false;
    manualLock = true;
    Serial.println("🚨 EMERGENCY STOP ACTIVATED");
  }
}

// ===== APPLY PUMP AND LED =====
void applyPump() {
  digitalWrite(RELAY_PIN, pumpState ? HIGH : LOW);
  digitalWrite(LED_Pin_G, pumpState ? HIGH : LOW);
  digitalWrite(LED_Pin_R, pumpState ? LOW : HIGH);

  if (pumpState != lastPumpState) {
    voneClient.publishTelemetryData(RelayWaterPump, "Status", pumpState);
    lastPumpState = pumpState;
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  setup_wifi();
  syncTime();
  
  voneClient.setup();
  voneClient.registerActuatorCallback(onActuatorReceived);

  dht.begin();
  pinMode(MOISTURE_PIN, INPUT);
  pinMode(RAIN_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_Pin_R, OUTPUT);
  pinMode(LED_Pin_G, OUTPUT);

  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_Pin_R, LOW);
  digitalWrite(LED_Pin_G, LOW);

  applyPump();

  Serial.println("----------Smart Watering System----------");
  delay(2000);
}

// ===== LOOP =====
void loop() {
  unsigned long now = millis();
  static bool statusPublished = false;

  if (!voneClient.connected()) {
    voneClient.reconnect();
    statusPublished = false;
  }

  if (voneClient.connected() && !statusPublished) {
    voneClient.publishDeviceStatusEvent(DHT11Sensor, true);
    voneClient.publishDeviceStatusEvent(RainSensor, true);
    voneClient.publishDeviceStatusEvent(MoistureSensor, true);
    voneClient.publishDeviceStatusEvent(RelayWaterPump, true);
    statusPublished = true;
  }
  voneClient.loop();

  // ===== READ FUZZY STATE =====
  if (Serial.available() > 0) {
    String incoming = Serial.readStringUntil('\n');
    incoming.trim();
    if (incoming.length() > 0) {
      fuzzyState = incoming.toInt();
    }
  }

  // ===== CONTROL LOGIC =====
  if (mode == EMERGENCY_STOP) {
    pumpState = false;
    pulseRunning = false;
    manualLock = true;
  } 
  else if (mode == MANUAL_MODE) {
    pumpState = true;
    manualLock = true;
    if (now > manualStartTime ) {
      mode = AUTO_MODE;
      manualLock = false;
      Serial.println("⏰ Manual Timeout Expired: Returning to AUTO MODE");
    }
  }
  else if (mode == AUTO_MODE && !manualLock) {
    // Start new cycle
    if (now - cycleStart >= TELEMETRY_CYCLE) {
      cycleStart = now;
      pulseRunning = false;

      if (fuzzyState == 1) {
        pulseEndTime = now + FUZZY1_PULSE;
        pulseRunning = true;
        Serial.println("🟡 FUZZY 1: Pulse started");
      } else if (fuzzyState == 2) {
        pulseEndTime = now + FUZZY2_PULSE;
        pulseRunning = true;
        Serial.println("🟢 FUZZY 2: Pulse started");
      }
    }

    // Apply pulse
    if (pulseRunning) {
      if (now < pulseEndTime) {
        pumpState = true;
      } else {
        pumpState = false;
        pulseRunning = false;
        Serial.println("⏹ Pulse ended");
      }
    } else {
      pumpState = false;
    }
  }

  // ===== APPLY OUTPUT =====
  applyPump();

  // ===== TELEMETRY =====
  if (now - lastMsg > TELEMETRY_CYCLE) {
    lastMsg = now;

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    JSONVar payloadObject;
    payloadObject["Humidity"] = humidity;
    payloadObject["Temperature"] = temperature;
    voneClient.publishTelemetryData(DHT11Sensor, payloadObject);

    float sensorValue = analogRead(MOISTURE_PIN);
    int soilPercent = map(sensorValue, MinDepth, MaxDepth, 0, 100);
    soilPercent = constrain(soilPercent, 0, 100);
    voneClient.publishTelemetryData(MoistureSensor, "Soil moisture", soilPercent);

    bool raining = digitalRead(RAIN_PIN) == LOW;
    voneClient.publishTelemetryData(RainSensor, "Raining", raining ? 1 : 0);

    Serial.println("----------------------------");
    Serial.print("Temperature: "); Serial.println(temperature);
    Serial.print("Humidity: "); Serial.println(humidity);
    Serial.print("Soil Moisture: "); Serial.print(soilPercent); Serial.println("%");
    Serial.print("Rain Status: "); Serial.println(raining ? "YES" : "NO");
    Serial.print("Pump Status: "); Serial.println(pumpState ? "ON" : "OFF");

    JSONVar serialPayload;
    serialPayload["Temperature"] = temperature;
    serialPayload["Humidity"] = humidity;
    serialPayload["Soil_moisture"] = soilPercent;
    serialPayload["Raining"] = raining ? 1 : 0;
    serialPayload["Pump"] = pumpState ? "ON" : "OFF";
    Serial.println(JSON.stringify(serialPayload));
  }
}
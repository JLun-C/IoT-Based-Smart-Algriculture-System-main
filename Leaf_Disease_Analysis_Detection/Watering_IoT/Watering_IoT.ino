/*
ESP32 Smart Watering System 
*/

#include "VOneMqttClient.h"
#include "DHT.h"

//define device id
const char* DHT11Sensor = "993a0fea-2be0-4427-9f60-3363c68b2f07";   
const char* RainSensor = "7065fa0a-0958-4610-8e82-f3bd1530ae72";       
const char* MoistureSensor = "1a08cd4e-e79f-4bd3-911b-c28798e09112";  
// const char* RedLED = "023dcf2e-0725-45ae-b816-08d8bfd21ea3";
// const char* GreenLED = "9dd79b06-a36a-4c89-9b5a-8b0573536bdd";
const char* RelayWaterPump = "8ae703d6-c543-4e83-9cdc-9a0c5f3ceb98";

/* ================= PIN CONFIG ================= */
const int DHTPIN = 21;   // DHT11 sensor
const int MOISTURE_PIN = 35;   // Soil moisture sensor
const int RAIN_PIN = 23;  // Rain sensor
const int RELAY_PIN = 32;  // Relay module (pump)
const int LED_Pin_R = 18;
const int LED_Pin_G  = 19;

#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

VOneMqttClient voneClient;

// unsigned long lastMsg = 0;

int MinDepth = 4095;       // Dry soil value
int MaxDepth = 2170;       // Wet soil value
int dryThreshold = 80;   // pump turns ON below this

// Set the manual override timeout (10 minutes in milliseconds)
const unsigned long MANUAL_TIMEOUT = 30000; // 10 * 60 * 1000

// Variable to store the timestamp when the manual button was pressed
unsigned long manualStartTime = 0;


/* ================= CONTROL STATE ================= */
enum ControlMode {
  AUTO_MODE,
  MANUAL_MODE,
  EMERGENCY_STOP
};

ControlMode mode = AUTO_MODE;
 

/* ================= GLOBAL ================= */
unsigned long lastMsg = 0;
bool pumpState = false;
bool lastPumpState = false;
bool manualOverride = false;
unsigned long lastActuatorCmdTime = 0;
const unsigned long manualOverrideTimeout = 300000;

/* Fuzzy */
int fuzzyState = 0;
unsigned long pulseStart = 0;
bool pulseActive = false;
bool manualLock = false;

/* ================= WIFI SETUP ================= */
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID); // Pulled from vonesetting.h

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

// void onActuatorReceived(const char* deviceId, const char* payload) {
//   Serial.print("Command received from V-One: ");
//   Serial.print(deviceId);
//   Serial.println("Payload: ");
//   Serial.print(payload);

//   // Parse JSON payload
//   JSONVar command = JSON.parse(payload);
//   if (JSON.typeof(command) == "undefined") {
//       Serial.println("Error parsing JSON!");
//       return;
//   }

//   // Check if command is for Water Pump
//   if (strcmp(deviceId, RelayWaterPump) == 0) {
//       manualOverride = true;                  // Enable manual override
//       lastActuatorCmdTime = millis();         // Reset override timeout

//       // Extract "Relay" value from JSON
//       if (command.hasOwnProperty("Relay")) {
//           bool relayCmd = (bool)command["Relay"];
//           pumpState = relayCmd;

//           digitalWrite(RELAY_PIN, pumpState ? HIGH : LOW);
//           digitalWrite(LED_Pin_G, pumpState ? HIGH : LOW);
//           digitalWrite(LED_Pin_R, pumpState ? LOW : HIGH);

//           Serial.println(pumpState ? "Pump turned ON" : "Pump turned OFF");
//       }
//   }
// }

/* ================= V-ONE CALLBACK ================= */
void onActuatorReceived(const char* deviceId, const char* payload) {
  if (strcmp(deviceId, RelayWaterPump) != 0) return;

  JSONVar cmd = JSON.parse(payload);
  if (JSON.typeof(cmd) == "undefined") return;

  bool userCmd = (bool)cmd["Relay"];

  if (userCmd) {
      // User turned it ON: Enter MANUAL mode and force pump ON
      mode = MANUAL_MODE;
      manualLock = true;
      pumpState = true;
      manualStartTime = millis();
      Serial.println("👆 MANUAL MODE: Pump Forced ON");
    } 
    // else {
    //   // User turned it OFF: If it was already ON, this is an EMERGENCY STOP
    //   if (pumpState == true) {
    //       mode = EMERGENCY_STOP;
    //       pumpState = false;
    //       Serial.println("🚨 EMERGENCY STOP: System Locked OFF");
    //   } else {
    //       // If it was already OFF, just stay in AUTO or Reset
    //       mode = AUTO_MODE;
    //       Serial.println("🤖 Returning to AUTO MODE");
    //   }
    // }
      else {
        // OFF = EMERGENCY STOP
        mode = EMERGENCY_STOP;
        pumpState = false;
        manualLock = true;
        Serial.println("🚨 EMERGENCY STOP ACTIVATED");
      }
}

/* ================= APPLY OUTPUT ================= */
void applyPump() {
  digitalWrite(RELAY_PIN, pumpState ? HIGH : LOW);
  digitalWrite(LED_Pin_G, pumpState ? HIGH : LOW);
  digitalWrite(LED_Pin_R, pumpState ? LOW : HIGH);

  if (pumpState != lastPumpState) {
    voneClient.publishTelemetryData(RelayWaterPump, "Status", pumpState);
    lastPumpState = pumpState;
  }
}

/* ================= SETUP ================= */
void setup() {
    setup_wifi();
    syncTime(); 
    voneClient.setup(); // Initializes NTP time and Secure Client
    voneClient.registerActuatorCallback(onActuatorReceived);

    dht.begin();

    pinMode(MOISTURE_PIN, INPUT);
    pinMode(RAIN_PIN, INPUT);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(LED_Pin_R, OUTPUT);
    pinMode(LED_Pin_G, OUTPUT);

    // Start with pump OFF
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(LED_Pin_R, LOW);
    digitalWrite(LED_Pin_G, LOW);

    applyPump();

    Serial.println("----------Smart Watering System----------");
    // Give NTP a moment to sync before loop starts
    delay(2000);
}

/* ================= LOOP ================= */
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

  /* ---------- RECEIVE FUZZY STATE ---------- */
  if (Serial.available() > 0) {
          String incoming = Serial.readStringUntil('\n');
          incoming.trim();
          if (incoming.length() > 0) {
              fuzzyState = incoming.toInt();
          }
  }
  
  /* ---------- CONTROL LOGIC ---------- */
  if (mode == EMERGENCY_STOP) {
    pumpState = false;
    manualLock = true;
  }
  else if (mode == MANUAL_MODE) {
    pumpState = true;
    manualLock = true; // lock AUTO
    if (now - manualStartTime >= MANUAL_TIMEOUT) {
        mode = AUTO_MODE;
        manualLock = false;
        Serial.println("⏰ Manual Timeout Expired: Returning to AUTO MODE");
    }
  }
  else if (mode == AUTO_MODE && !manualLock){
    // AUTO MODE (Processes Fuzzy Logic 0, 1, 2)
    if (fuzzyState == 2) {
        pumpState = true;
        pulseActive = false;
    } 
    else if (fuzzyState == 1) {
        if (!pulseActive) {
            pulseActive = true;
            pulseStart = now;
            Serial.println("🟡 FUZZY 1: Pulse started");
        }
        // Pump runs for 20 seconds, then stops
        if (now - pulseStart <= 20000) {
            pumpState = true;
        } else {
            pumpState = false;
            pulseActive = false;   // reset for next fuzzy 1
            Serial.println("🟡 FUZZY 1: Pulse ended");
        }
    } 
    else {
        pumpState = false;
        pulseActive = false;
    }
}

  /* ---------- APPLY ---------- */
  applyPump();

  // Telemetry Timer (Read every 5 seconds)   
  if (now - lastMsg > 60000) {
    lastMsg = now;

    // Read Sensors
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    JSONVar payloadObject;     
    payloadObject["Humidity"] = humidity;     
    payloadObject["Temperature"] = temperature;     
    voneClient.publishTelemetryData(DHT11Sensor, payloadObject); 

    // Read soil moisture
    float sensorValue = analogRead(MOISTURE_PIN);
    int soilPercent = map(sensorValue, MinDepth, MaxDepth, 0, 100);
    soilPercent = constrain(soilPercent, 0, 100);
    voneClient.publishTelemetryData(MoistureSensor, "Soil moisture", soilPercent);

    // Read rain sensor (assume LOW = rain, HIGH = no rain)
    bool raining = digitalRead(RAIN_PIN) == LOW;
    voneClient.publishTelemetryData(RainSensor, "Raining", raining ? 1 : 0);

    // Prints
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
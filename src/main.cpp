#include <Arduino.h>
#include <ArduinoJson.h>
#include <GEA2.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "Config.h"
#include "HomeAssistantBridge.h"
#include "gea2_addresses.h"

#ifdef MQTT_TLS
static WiFiClientSecure wifiClient;
#else
static WiFiClient wifiClient;
#endif
static PubSubClient mqttClient(wifiClient);
static HomeAssistantBridge bridge;
static GEA2 gea2;
unsigned long lastPeriodicRun = 0;
String topic;
String jsonValidAddresses; // JSON representation of valid addresses
bool resetMemory = false; // set to true to clear saved addresses
size_t lastSavedValidAddressCount = 0; // track how many were saved last time
std::vector<uint16_t> validAddresses; // dynamic list of gea2 addresses; 4-digit addresses fit in 16-bit
Preferences prefs;

static void connectToWifi() {
  if(WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.println("Connecting to WiFi...");

  unsigned retries = 0;
  while(WiFi.status() != WL_CONNECTED) {
    if(retries++ > 100) {
      Serial.println("WiFi connection failed, restarting...");
      ESP.restart();
    }

    digitalWrite(LED_WIFI, LOW);
    delay(100);
    Serial.print(".");
  }
}

static void configureWifi() {
  Serial.println("WiFi SSID: " + String(ssid));

  WiFi.begin(ssid, password);

  connectToWifi();

#ifdef MQTT_TLS
#ifdef MQTT_TLS_VERIFY
  X509List* cert = new X509List(CERT);
  wifiClient.setTrustAnchors(cert);
#else
  wifiClient.setInsecure();
#endif
#endif

  Serial.println("WiFi connected");
}

static void configureMqtt() {
  mqttClient.setServer(mqtt_server, mqtt_server_port);
}

static void connectToMqtt() {
  connectToWifi();
  digitalWrite(LED_WIFI, HIGH);

  if(!mqttClient.connected()) {
    digitalWrite(LED_MQTT, LOW);

    unsigned retries = 0;
    while(!mqttClient.connected()) {
      if(retries++ > 10) {
        Serial.println("MQTT connection failed, restarting...");
        ESP.restart();
      }

      Serial.print("Attempting MQTT connection...");

      if(mqttClient.connect("", mqttUser, mqttPassword)) {
        Serial.println("connected");
        digitalWrite(LED_MQTT, HIGH);
      }
      else {
        Serial.println("failed, rc=" + String(mqttClient.state()) + " will try again in 1 second");
        delay(1000);
      }
    }

    bridge.notifyMqttDisconnected();
  }
}

void clearPrefs() {
  prefs.begin("storage", false); // false = read/write
  prefs.clear(); // remove all keys in this namespace
  prefs.end();
}

String vectorToJson(const std::vector<uint16_t>& vec) {
  DynamicJsonDocument doc(1024); // adjust size if needed
  JsonArray array = doc.to<JsonArray>();

  for(uint16_t val : vec) {
    char buf[7];
    snprintf(buf, sizeof(buf), "0x%04X", val);
    array.add(buf); // add hex string
  }

  String output;
  serializeJson(array, output);
  return output;
}

std::vector<uint16_t> jsonToVector(String json) {
  // Convert valid addresses from JSON to vector
  std::vector<uint16_t> result;
  DynamicJsonDocument doc(1024);
  if(deserializeJson(doc, json) == DeserializationError::Ok) {
    JsonArray array = doc.as<JsonArray>();
    for(const char* s : array) {
      result.push_back(strtol(s, nullptr, 16)); // convert back from hex string
    }
  }
  else {
    Serial.println("Failed to parse valid addresses from JSON");
  }
  return result;
}

void saveAddressesIfNeeded() {
  size_t validAddressCount = validAddresses.size();
  if(validAddressCount > lastSavedValidAddressCount) {
    jsonValidAddresses = vectorToJson(validAddresses);

    // Save to Preferences
    prefs.begin("storage", false);
    prefs.putString("validAddresses", jsonValidAddresses);
    prefs.end();
    Serial.printf("Saved %u addresses to Preferences\n", validAddressCount);
    Serial.println(jsonValidAddresses);

    // Update last saved count
    lastSavedValidAddressCount = validAddressCount;

    // Publish to MQTT
    topic = String(deviceId) + "/valid_addresses";
    connectToMqtt();
    mqttClient.publish(topic.c_str(), jsonValidAddresses.c_str()); // <-- Publish to MQTT
  }
}

String readApplianceModel() {
  struct ModelNumber {
    char contents[32];
  };
  auto model_number = gea2.readERD<ModelNumber>(GEA2::broadcastAddress, 0x0001);
  if(model_number.status == GEA2::ReadStatus::success) {
    Serial.printf("Model Number: %.32s\n", model_number.value.contents);
    return model_number.value.contents;
  }
  else {
    Serial.printf("Failed to read Model Number\n");
    return "none";
  }
}

String readApplianceSerial() {
  struct SerialNumber {
    char contents[32];
  };
  auto serial_number = gea2.readERD<SerialNumber>(GEA2::broadcastAddress, 0x0002);
  if(serial_number.status == GEA2::ReadStatus::success) {
    Serial.printf("Serial Number: %.32s\n", serial_number.value.contents);
    return serial_number.value.contents;
  }
  else {
    Serial.printf("Failed to read Serial Number\n");
    return "none";
  }
}

String readApplianceType() {
  auto appliance_type = gea2.readERD<GEA2::U8>(GEA2::broadcastAddress, 0x0008);
  if(appliance_type.status == GEA2::ReadStatus::success) {
    Serial.printf("Appliance Type: %u\n", static_cast<unsigned int>(appliance_type.value.read()));
    char payload[4];
    snprintf(payload, sizeof(payload), "%u", static_cast<unsigned int>(appliance_type.value.read()));
    return String(payload);
  }
  else {
    Serial.printf("Failed to read Appliance Type\n");
    return "none";
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  pinMode(LED_HEARTBEAT, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_MQTT, OUTPUT);

  configureWifi();
  configureMqtt();

  Serial1.begin(GEA2::baud, SERIAL_8N1, D10, D9);
  gea2.begin(Serial1);
  bridge.begin(mqttClient, Serial1, deviceId);

  delay(5000); // wait 5 seconds to allow monitor to connect so we can see output from the start

  String applianceModel = readApplianceModel();
  String applianceSerial = readApplianceSerial();
  String applianceType = readApplianceType();

  if(applianceModel == "none" || applianceSerial == "none" || applianceType == "none") {
    Serial.println("Failed to read appliance info. Restarting in 30 seconds...");
    delay(30000);
    ESP.restart();
    // return;
  }

  // Load previously saved model info from Preferences
  prefs.begin("storage", true); // true = read-only
  String savedApplieanceModel = prefs.getString("applianceModel", "none");
  String savedApplianceSerial = prefs.getString("applianceSerial", "none");
  String savedApplianceType = prefs.getString("applianceType", "none");
  String jsonValidAddresses = prefs.getString("validAddresses", "[]");
  size_t lastReadLoop = prefs.getUInt("lastReadLoop", 0);
  prefs.end();

  // Check if model/serial/type changed
  if((savedApplieanceModel != applianceModel) || (savedApplianceSerial != applianceSerial) || (savedApplianceType != applianceType) || resetMemory) {
    Serial.println("Appliance model/serial/type changed. Resetting Memory.");
    clearPrefs();
    // Save new appliance info to Preferences
    prefs.begin("storage", false); // false = read/write
    prefs.putString("applianceModel", applianceModel);
    prefs.putString("applianceSerial", applianceSerial);
    prefs.putString("applianceType", applianceType);
    prefs.putString("validAddresses", "[]"); // reset valid addresses]);
    prefs.putUInt("lastReadLoop", 0); // reset step
    prefs.end();
    jsonValidAddresses = "[]";
    lastReadLoop = 0;
  }
  else {
    // Load previously saved valid addresses from JSON
    Serial.println("Loading previously saved valid addresses from Preferences:");
    Serial.println(jsonValidAddresses);
    validAddresses = jsonToVector(jsonValidAddresses);
    lastSavedValidAddressCount = validAddresses.size();
    Serial.printf("Loaded %u valid addresses from Preferences.\n", validAddresses.size());
  }

  // Publish appliance info to MQTT
  connectToMqtt();

  topic = String(deviceId) + "/model_number";
  mqttClient.publish(topic.c_str(), applianceModel.c_str()); // <-- Publish to MQTT
  topic = String(deviceId) + "/serial_number";
  mqttClient.publish(topic.c_str(), applianceSerial.c_str()); // <-- Publish to MQTT
  topic = String(deviceId) + "/appliance_type";
  mqttClient.publish(topic.c_str(), applianceType.c_str()); // <-- Publish to MQTT

  // Scan all remaining addresses
  for(size_t i = lastReadLoop; i < gea2AddressCount; i++) {
    auto address = gea2Addresses[i];
    auto result = gea2.readERD<GEA2::U32>(GEA2::broadcastAddress, address);
    digitalWrite(LED_HEARTBEAT, i % 2 < 1);
    if(result.status == GEA2::ReadStatus::success) {
      Serial.printf("Successfully read ERD 0x%04X: 0x%08X (%u / %u)\n", address, result.value.read(), i + 1, gea2AddressCount);
      validAddresses.push_back(address);
    }
    else {
      Serial.printf("Failed to read ERD 0x%04X (%u / %u)\n", address, i + 1, gea2AddressCount);
    }
    // Save progress intermittently to preferences since this takes so long
    if((i + 1) % 25 == 0) {
      // Save loop count to Preferences
      Serial.println("Saving Progress to Preferences");
      prefs.begin("storage", false); // false = read/write
      prefs.putUInt("lastReadLoop", i + 1);
      prefs.end();
      // Save discovered addresses to Preferences if new ones found
      saveAddressesIfNeeded();
    }
    // delay(5); // small delay to avoid overwhelming the bus
  }

  saveAddressesIfNeeded();
  // Save step to Preferences so the loop is skipped next time
  prefs.begin("storage", false); // false = read/write
  prefs.putUInt("lastReadLoop", gea2AddressCount);
  prefs.end();
  Serial.printf("Address scan complete: %u valid addresses found\n", validAddresses.size());

  // Convert valid addresses to JSON and publish to MQTT
  jsonValidAddresses = vectorToJson(validAddresses);
  topic = String(deviceId) + "/valid_addresses";
  connectToMqtt();
  mqttClient.publish(topic.c_str(), jsonValidAddresses.c_str()); // <-- Publish to MQTT
  Serial.println("JSON Payload:");
  Serial.println(jsonValidAddresses);
  Serial.println("Setup complete.");
}

void loop() {
  connectToMqtt();
  bridge.loop();
  digitalWrite(LED_HEARTBEAT, millis() % 1000 < 500);

  // Run code every 30 seconds
  if(millis() - lastPeriodicRun == 30000) {
    lastPeriodicRun = millis();

    // --- Your code to run every 30 seconds ---

    gea2.readERDAsync(
      GEA2::broadcastAddress, 0x0008, +[](GEA2::ReadStatus status, GEA2::U32 value) {
        if(status == GEA2::ReadStatus::success) {
          Serial.printf("Successfully read ERD 0x0008 asynchronously: 0x%08X\n", value.read());
        }
        else {
          Serial.printf("Failed to read ERD 0x0008 asynchronously\n");
        }
      });

    auto result = gea2.readERD<GEA2::U32>(GEA2::broadcastAddress, 0x0008);
    if(result.status == GEA2::ReadStatus::success) {
      Serial.printf("Successfully read ERD 0x0008: 0x%08X\n", result.value.read());
    }
    else {
      Serial.printf("Failed to read ERD 0x0008\n");
    }
    // -----------------------------------------
  }
}

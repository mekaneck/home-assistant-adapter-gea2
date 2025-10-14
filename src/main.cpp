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
  DynamicJsonDocument doc(2048); // adjust size if needed
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
  DynamicJsonDocument doc(2048);
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

void setUpErds() {
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
  if(0 < 1) {
  // if(lastReadLoop < gea2AddressCount) {
    // for(size_t i = lastReadLoop; i < gea2AddressCount; i++) {
    for(size_t i = 550; i < 620; i++) {
      auto address = gea2Addresses[i];
      Serial.printf("Scanning address %u / %u: 0x%04X\n", i + 1, gea2AddressCount, address);
      gea2.readERDAsync(
        GEA2::broadcastAddress, address, +[](GEA2::ReadStatus status, GEA2::U32 value) {
          if(status == GEA2::ReadStatus::success) {
            Serial.printf("Successfully read ERD: 0x%08X\n", value.read());
          }
          else {
            Serial.printf("Failed to read ERD asynchronously\n");
          }
        });

      delay(50); // small delay to avoid overwhelming the bus
      digitalWrite(LED_HEARTBEAT, i % 2 < 1);

      // auto result = gea2.readERD<GEA2::U32>(GEA2::broadcastAddress, address);
      // if(result.status == GEA2::ReadStatus::success) {
      //   Serial.printf("Successfully read ERD 0x%04X: 0x%08X (%u / %u)\n", address, result.value.read(), i + 1, gea2AddressCount);
      //   validAddresses.push_back(address);
      // }
      // else {
      //   Serial.printf("Failed to read ERD 0x%04X (%u / %u)\n", address, i + 1, gea2AddressCount);
      // }
      // Save progress intermittently to preferences since this takes so long
      if((i + 1) % 1000 == 0) {
        // Save loop count to Preferences
        Serial.println("Saving Progress to Preferences");
        prefs.begin("storage", false); // false = read/write
        prefs.putUInt("lastReadLoop", i + 1);
        prefs.end();
        // Save discovered addresses to Preferences if new ones found
        saveAddressesIfNeeded();
      }
    }
    // saveAddressesIfNeeded();
    // Save step to Preferences so the loop is skipped next time
    prefs.begin("storage", false); // false = read/write
    prefs.putUInt("lastReadLoop", gea2AddressCount);
    prefs.end();
    Serial.printf("Address scan complete: %u valid addresses found\n", validAddresses.size());
    Serial.println("Setup complete.");
  }
  else {
    Serial.println("Setup previously completed. No action needed.");
  }

  // Convert valid addresses to JSON and publish to MQTT
  jsonValidAddresses = vectorToJson(validAddresses);
  connectToMqtt();
  topic = String(deviceId) + "/valid_addresses";
  if(mqttClient.publish(topic.c_str(), jsonValidAddresses.c_str())) {
    Serial.println("Valid addresses published to MQTT:");
  }
  else {
    Serial.println("MQTT Publish failed; payload too large");
    Serial.println("Valid addresses not published to MQTT:");
  };
  Serial.println(jsonValidAddresses);
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
  bridge.begin(mqttClient, Serial1, deviceId);
  gea2.begin(Serial1);

  delay(5000); // wait 5 seconds to allow monitor to connect so we can see output from the start

  setUpErds();

  gea2.onPacketReceived(+[](const GEA2::Packet& packet) {
    const uint8_t* payload = packet.payload();
    size_t length = packet.payloadLength();

    // if (!payload || length < 3) return; // need at least status + ERD ID
    // Serial.print("Raw payload (length ");
    // Serial.print(length);
    // Serial.print("): ");

    // for (size_t i = 0; i < length; i++) {
    //     Serial.printf("%02X ", payload[i]);
    // }

    // Serial.println();

    if (!payload || length < 5) return; // must have at least source + status + ERD ID + size

    // Source
    uint8_t source = payload[0];

    // Status
    uint8_t status = payload[1];

    // ERD ID (big-endian)
    uint16_t erdId = (static_cast<uint16_t>(payload[2]) << 8) | payload[3];

    // Size of value
    uint8_t valueSize = payload[4];

    // Check that payload length matches expected size
    if (length < 5 + valueSize) {
        Serial.printf("Error: payload length (%d) is smaller than expected value size (%d)\n",
                      length, valueSize);
        return;
    }

    // Extract value bytes
    char hexStr[valueSize * 2 + 1]; // 2 hex chars per byte + null terminator
    for (size_t i = 0; i < valueSize; i++) {
        sprintf(&hexStr[i * 2], "%02X", payload[i + 5]);
    }
    hexStr[valueSize * 2] = '\0';

    // Convert value bytes to decimal (big-endian)
    uint64_t decimalValue = 0;
    for (size_t i = 0; i < valueSize; i++) {
        decimalValue = (decimalValue << 8) | payload[i + 5];
    }

    // Print all
    Serial.printf("Source: 0x%02X, Status: %s, ERD: 0x%04X, Value size: %d, Value: 0x%s (%llu)\n",
                  source,
                  status == 0 ? "FAILED" : "SUCCESS",
                  erdId,
                  valueSize,
                  hexStr,
                  decimalValue);


    // // Optional: publish to MQTT as JSON
    // DynamicJsonDocument doc(256);
    // doc["erdId"] = erdId;
    // doc["value"] = hexStr;

    // String jsonPayload;
    // serializeJson(doc, jsonPayload);
    // mqttClient.publish("esp32/erdUpdates", jsonPayload.c_str());
    
  });

  // for(auto address : validAddresses) {
  //   gea2.readERDAsync(
  //     GEA2::broadcastAddress, address, +[](GEA2::ReadStatus status, GEA2::U32 value) {
  //       if(status == GEA2::ReadStatus::success) {
  //         Serial.printf("Successfully read ERD 0x0035 asynchronously from 0x%02X: 0x%08X\n", address, value.read());
  //       }
  //       else {
  //         Serial.printf("Failed to read ERD 0x0035 asynchronously from 0x%02X\n", address);
  //       }
  //     });
  // }

  // gea2.sendPacket(GEA2::Packet(0xE4, GEA2::broadcastAddress, { 0x01 }));

  // gea2.onPacketReceived(+[](const GEA2::Packet& packet) {
  //   Serial.printf("Packet received from 0x%04X\n", packet.source());
  //   Serial.printf("  Payload (%u bytes)\n", packet.payloadLength());
  //   Serial.printf("Packet payload: 0x%0*X\n", packet.payloadLength() * 2, packet.payload());
  //   for(size_t i = 0; i < packet.payloadLength(); i++) {
  //     Serial.printf(" %02X", packet.payload()[i]);
  //   }
  //   Serial.println();
  // });
}

void loop() {
  // connectToMqtt();
  if(!mqttClient.connected()) {
    connectToMqtt(); // only reconnect if disconnected
  }
  mqttClient.loop();
  // bridge.loop();
  gea2.loop();
  digitalWrite(LED_HEARTBEAT, millis() % 500 < 250);
}

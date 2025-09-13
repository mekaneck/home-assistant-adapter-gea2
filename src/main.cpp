#include <Arduino.h>
#include <GEA2.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "Config.h"
#include "HomeAssistantBridge.h"

#ifdef MQTT_TLS
static WiFiClientSecure wifiClient;
#else
static WiFiClient wifiClient;
#endif
static PubSubClient mqttClient(wifiClient);
static HomeAssistantBridge bridge;
static GEA2 gea2;
unsigned long lastPeriodicRun = 0; // Place this at file scope (top of file or before setup())

static void connectToWifi()
{
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

static void configureWifi()
{
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

static void configureMqtt()
{
  mqttClient.setServer(mqtt_server, mqtt_server_port);
}

static void connectToMqtt()
{
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

void setup()
{
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

  connectToMqtt();

  struct ModelNumber {
    char contents[32];
  };
  auto model_number = gea2.readERD<ModelNumber>(GEA2::broadcastAddress, 0x0001);
  if(model_number.status == GEA2::ReadStatus::success) {
    String topic = String(deviceId) + "/model_number";
    mqttClient.publish(topic.c_str(), model_number.value.contents); // <-- Publish to MQTT
  }
  else {
    Serial.printf("Failed to read Model Number\n");
  }

  struct SerialNumber {
    char contents[32];
  };
  auto serial_number = gea2.readERD<SerialNumber>(GEA2::broadcastAddress, 0x0002);
  if(serial_number.status == GEA2::ReadStatus::success) {
    String topic = String(deviceId) + "/serial_number";
    // char payload[33]; // 32 chars + null terminator
    // snprintf(payload, sizeof(payload), "%.32s", serial_number.value.contents);
    mqttClient.publish(topic.c_str(), serial_number.value.contents); // <-- Publish to MQTT
  }
  else {
    Serial.printf("Failed to read Serial Number\n");
  }

  auto appliance_type = gea2.readERD<GEA2::U8>(GEA2::broadcastAddress, 0x0008);
  if(appliance_type.status == GEA2::ReadStatus::success) {
    String topic = String(deviceId) + "/appliance_type";
    char payload[4];
    snprintf(payload, sizeof(payload), "%u", static_cast<unsigned int>(appliance_type.value.read()));
    mqttClient.publish(topic.c_str(), payload); // <-- Publish to MQTT
  }
  else {
    Serial.printf("Failed to read Appliance Type\n");
  }

  auto personality = gea2.readERD<GEA2::U32>(GEA2::broadcastAddress, 0x0035);
  if(personality.status == GEA2::ReadStatus::success) {
    Serial.printf("Personality: %d\n", personality.value.read());
  }
  else {
    Serial.printf("Failed to read Personality\n");
  }
}

void loop()
{
  connectToMqtt();
  bridge.loop();
  digitalWrite(LED_HEARTBEAT, millis() % 1000 < 500);

  // Run code every 30 seconds
  if(millis() - lastPeriodicRun >= 30000) {
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

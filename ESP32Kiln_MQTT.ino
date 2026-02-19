#include <WiFi.h>
#include <PubSubClient.h>

extern double kiln_tmp1;
bool ha_discovery_sent = false;
const char* mqtt_server = "server adress";
const char* mqtt_user = "server user";
const char* mqtt_password = "server password";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long lastPublish = 0;
#define MQTT_INTERVAL 5000

bool mqtt_started = false;

// ----------------------------------

void mqtt_init_once() {
  if (mqtt_started) return;

  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setBufferSize(512);  // ← AJOUTEZ CETTE LIGNE
  mqtt_started = true;

  Serial.println("[MQTT] init");
}

// ----------------------------------

void mqtt_reconnect() {
  if (mqttClient.connected()) return;

  if (mqttClient.connect("PIDKiln", mqtt_user, mqtt_password)) {
    Serial.println("[MQTT] connecté");
      ha_discovery_sent = false;  // ← Permet de renvoyer le discovery
  }
}

// ----------------------------------
void mqtt_publish_temperature() {

  char buffer[128];

  snprintf(buffer, sizeof(buffer),
           "{\"kiln\":%.2f,\"set\":%.2f,\"internal\":%.2f,\"case\":%.2f}",
           kiln_temp,
           set_temp,
           int_temp,
           case_temp);

  mqttClient.publish("pidkiln/temperature", buffer, true);

  Serial.print("[MQTT] -> ");
  Serial.println(buffer);
}

// ----------------------------------

void mqtt_tick() {
  mqtt_init_once();
  mqtt_reconnect();
  mqttClient.loop();

  
    if (mqttClient.connected() && !ha_discovery_sent)
    {
        mqtt_publish_discovery();
        ha_discovery_sent = true;
        Serial.println("[MQTT] HA auto-discovery envoyé");
    }

  if (millis() - lastPublish > MQTT_INTERVAL) {
    lastPublish = millis();
    mqtt_publish_temperature();
  }
}
void mqtt_publish_discovery()
{
  char payload[256];

  // ---- Température Four ----
  snprintf(payload,sizeof(payload),
  "{\"name\":\"PIDKiln Temp Four\","
  "\"state_topic\":\"pidkiln/temperature\","
  "\"value_template\":\"{{ value_json.kiln }}\","
  "\"unit_of_measurement\":\"°C\","
  "\"device_class\":\"temperature\","
  "\"unique_id\":\"pidkiln_kiln\"}"
  );

  mqttClient.publish(
    "homeassistant/sensor/pidkiln/kiln/config",
    payload,
    true);

  // ---- Consigne ----
  snprintf(payload,sizeof(payload),
  "{\"name\":\"PIDKiln Consigne\","
  "\"state_topic\":\"pidkiln/temperature\","
  "\"value_template\":\"{{ value_json.set }}\","
  "\"unit_of_measurement\":\"°C\","
  "\"device_class\":\"temperature\","
  "\"unique_id\":\"pidkiln_set\"}"
  );

  mqttClient.publish(
    "homeassistant/sensor/pidkiln/set/config",
    payload,
    true);

  // ---- Temp interne ----
  snprintf(payload,sizeof(payload),
  "{\"name\":\"PIDKiln Temp Interne\","
  "\"state_topic\":\"pidkiln/temperature\","
  "\"value_template\":\"{{ value_json.internal }}\","
  "\"unit_of_measurement\":\"°C\","
  "\"device_class\":\"temperature\","
  "\"unique_id\":\"pidkiln_internal\"}"
  );

  mqttClient.publish(
    "homeassistant/sensor/pidkiln/internal/config",
    payload,
    true);

  // ---- Temp boitier ----
  snprintf(payload,sizeof(payload),
  "{\"name\":\"PIDKiln Temp Boitier\","
  "\"state_topic\":\"pidkiln/temperature\","
  "\"value_template\":\"{{ value_json.case }}\","
  "\"unit_of_measurement\":\"°C\","
  "\"device_class\":\"temperature\","
  "\"unique_id\":\"pidkiln_case\"}"
  );

  mqttClient.publish(
    "homeassistant/sensor/pidkiln/case/config",
    payload,
    true);

  Serial.println("[MQTT] discovery HA publié");
}


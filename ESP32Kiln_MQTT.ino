#include <WiFi.h>
#include <PubSubClient.h>

extern double kiln_tmp1;
extern PROGRAM_RUN_STATE Program_run_state;  // type défini dans ESP32Kiln.h

bool ha_discovery_sent = false;
const char* mqtt_server = "192.168.1.5";
const char* mqtt_user = "philochardbleu";
const char* mqtt_password = "Gouph123";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long lastPublish = 0;
#define MQTT_INTERVAL 5000

bool mqtt_started = false;

// ----------------------------------

// ----------------------------------
// Callback MQTT : reçoit les commandes de Home Assistant
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();

  Serial.print("[MQTT] <- ");
  Serial.print(topic);
  Serial.print(" : ");
  Serial.println(msg);

  if (String(topic) == "pidkiln/command") {
    if (msg == "START") {
      Serial.println("[MQTT] Démarrage demandé par HA");
      if (Program_run_state == 3) RESUME_Program();  // PR_PAUSED = 3
      else START_Program();
      mqttClient.publish("pidkiln/state", "ON", true);
    } else if (msg == "STOP") {
      Serial.println("[MQTT] Arrêt demandé par HA");
      END_Program();
      mqttClient.publish("pidkiln/state", "OFF", true);
    } else if (msg == "PAUSE") {
      Serial.println("[MQTT] Pause demandée par HA");
      PAUSE_Program();
      mqttClient.publish("pidkiln/state", "PAUSED", true);
    } else if (msg == "ABORT") {
      Serial.println("[MQTT] Abandon demandé par HA");
      ABORT_Program(5);  // PR_ERR_USER_ABORT = 5 - vérifier dans ESP32Kiln.h
      mqttClient.publish("pidkiln/state", "OFF", true);
    }
  }
}

// ----------------------------------
void mqtt_init_once() {
  if (mqtt_started) return;

  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setBufferSize(512);
  mqttClient.setCallback(mqtt_callback);
  mqtt_started = true;

  Serial.println("[MQTT] init");
}

// ----------------------------------

void mqtt_reconnect() {
  if (mqttClient.connected()) return;

  if (mqttClient.connect("PIDKiln", mqtt_user, mqtt_password)) {
    Serial.println("[MQTT] connecté");
    ha_discovery_sent = false;
    mqttClient.subscribe("pidkiln/command");
    Serial.println("[MQTT] abonné à pidkiln/command");
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

  // ---- Switch Start/Stop ----
  snprintf(payload, sizeof(payload),
    "{"
    "\"name\":\"PIDKiln Démarrage\","
    "\"command_topic\":\"pidkiln/command\","
    "\"state_topic\":\"pidkiln/state\","
    "\"payload_on\":\"START\","
    "\"payload_off\":\"STOP\","
    "\"state_on\":\"ON\","
    "\"state_off\":\"OFF\","
    "\"unique_id\":\"pidkiln_switch\","
    "\"retain\":true"
    "}"
  );
  mqttClient.publish("homeassistant/switch/pidkiln/start/config", payload, true);

  Serial.println("[MQTT] discovery HA publié");
}

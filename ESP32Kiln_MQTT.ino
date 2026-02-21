#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

// ---- Variables externes (définies dans ESP32Kiln.ino / ESP32Kiln.h) ----
extern double kiln_tmp1;
extern PROGRAM_RUN_STATE Program_run_state;
extern const char*       Prog_Run_Names[];
extern double            pid_out;
extern float             temp_incr;          // variation °C/min
extern int               Program_run_step;   // étape courante (0-based)
extern uint8_t           Program_run_size;   // nombre total d'étapes
extern char*             Program_run_name;   // nom du programme chargé
extern time_t            Program_run_start;  // timestamp démarrage
extern time_t            Program_run_end;    // timestamp fin estimée

// ---- Config MQTT ----
const char* mqtt_server   = "192.168.1.5";
const char* mqtt_user     = "philochardbleu";
const char* mqtt_password = "Gouph123";

WiFiClient    espClient;
PubSubClient  mqttClient(espClient);

unsigned long lastPublish       = 0;
bool          ha_discovery_sent = false;
bool          mqtt_started      = false;

#define MQTT_INTERVAL 5000

// ----------------------------------
// Callback : reçoit les commandes de Home Assistant
// ----------------------------------
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
      if (Program_run_state == PR_PAUSED) RESUME_Program();
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
      ABORT_Program(PR_ERR_USER_ABORT);
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
  char buffer[512];
  char start_str[20] = "";
  char end_str[20]   = "";

  // Formatage des timestamps si disponibles
  if (Program_run_start) {
    struct tm* t = localtime(&Program_run_start);
    strftime(start_str, sizeof(start_str), "%F %T", t);
  }
  if (Program_run_end) {
    struct tm* t = localtime(&Program_run_end);
    strftime(end_str, sizeof(end_str), "%F %T", t);
  }

  snprintf(buffer, sizeof(buffer),
    "{"
    "\"kiln\":%.2f,"
    "\"set\":%.2f,"
    "\"internal\":%.2f,"
    "\"case\":%.2f,"
    "\"heat\":%.1f,"
    "\"temp_rate\":%.2f,"
    "\"status\":\"%s\","
    "\"step\":%d,"
    "\"steps\":%d,"
    "\"prog_name\":\"%s\","
    "\"prog_start\":\"%s\","
    "\"prog_end\":\"%s\""
    "}",
    kiln_temp,
    set_temp,
    int_temp,
    case_temp,
    (pid_out * PID_WINDOW_DIVIDER / Prefs[PRF_PID_WINDOW].value.uint16) * 100.0,
    temp_incr,
    Prog_Run_Names[Program_run_state],
    Program_run_step + 1,
    Program_run_size,
    Program_run_name ? Program_run_name : "",
    start_str,
    end_str
  );

  mqttClient.publish("pidkiln/temperature", buffer, true);

  Serial.print("[MQTT] -> ");
  Serial.println(buffer);
}

// ----------------------------------
void mqtt_tick() {
  mqtt_init_once();
  mqtt_reconnect();
  mqttClient.loop();

  if (mqttClient.connected() && !ha_discovery_sent) {
    mqtt_publish_discovery();
    ha_discovery_sent = true;
    Serial.println("[MQTT] HA auto-discovery envoyé");
  }

  if (millis() - lastPublish > MQTT_INTERVAL) {
    lastPublish = millis();
    mqtt_publish_temperature();
  }
}

// ----------------------------------
void mqtt_publish_discovery() {
  char payload[256];

  // ---- Température four ----
  snprintf(payload, sizeof(payload),
    "{\"name\":\"PIDKiln Temp Four\","
    "\"state_topic\":\"pidkiln/temperature\","
    "\"value_template\":\"{{ value_json.kiln }}\","
    "\"unit_of_measurement\":\"°C\","
    "\"device_class\":\"temperature\","
    "\"unique_id\":\"pidkiln_kiln\"}");
  mqttClient.publish("homeassistant/sensor/pidkiln/kiln/config", payload, true);

  // ---- Consigne ----
  snprintf(payload, sizeof(payload),
    "{\"name\":\"PIDKiln Consigne\","
    "\"state_topic\":\"pidkiln/temperature\","
    "\"value_template\":\"{{ value_json.set }}\","
    "\"unit_of_measurement\":\"°C\","
    "\"device_class\":\"temperature\","
    "\"unique_id\":\"pidkiln_set\"}");
  mqttClient.publish("homeassistant/sensor/pidkiln/set/config", payload, true);

  // ---- Température interne ----
  snprintf(payload, sizeof(payload),
    "{\"name\":\"PIDKiln Temp Interne\","
    "\"state_topic\":\"pidkiln/temperature\","
    "\"value_template\":\"{{ value_json.internal }}\","
    "\"unit_of_measurement\":\"°C\","
    "\"device_class\":\"temperature\","
    "\"unique_id\":\"pidkiln_internal\"}");
  mqttClient.publish("homeassistant/sensor/pidkiln/internal/config", payload, true);

  // ---- Température boîtier ----
  snprintf(payload, sizeof(payload),
    "{\"name\":\"PIDKiln Temp Boitier\","
    "\"state_topic\":\"pidkiln/temperature\","
    "\"value_template\":\"{{ value_json.case }}\","
    "\"unit_of_measurement\":\"°C\","
    "\"device_class\":\"temperature\","
    "\"unique_id\":\"pidkiln_case\"}");
  mqttClient.publish("homeassistant/sensor/pidkiln/case/config", payload, true);

  // ---- Puissance PID % ----
  snprintf(payload, sizeof(payload),
    "{\"name\":\"PIDKiln Heat Output\","
    "\"state_topic\":\"pidkiln/temperature\","
    "\"value_template\":\"{{ value_json.heat }}\","
    "\"unit_of_measurement\":\"%\","
    "\"unique_id\":\"pidkiln_heat\"}");
  mqttClient.publish("homeassistant/sensor/pidkiln/heat/config", payload, true);

  // ---- Variation température °C/min ----
  snprintf(payload, sizeof(payload),
    "{\"name\":\"PIDKiln Variation Temp\","
    "\"state_topic\":\"pidkiln/temperature\","
    "\"value_template\":\"{{ value_json.temp_rate }}\","
    "\"unit_of_measurement\":\"°C/min\","
    "\"unique_id\":\"pidkiln_temp_rate\"}");
  mqttClient.publish("homeassistant/sensor/pidkiln/temp_rate/config", payload, true);

  // ---- Statut programme ----
  snprintf(payload, sizeof(payload),
    "{\"name\":\"PIDKiln Status\","
    "\"state_topic\":\"pidkiln/temperature\","
    "\"value_template\":\"{{ value_json.status }}\","
    "\"unique_id\":\"pidkiln_status\"}");
  mqttClient.publish("homeassistant/sensor/pidkiln/status/config", payload, true);

  // ---- Étape courante ----
  snprintf(payload, sizeof(payload),
    "{\"name\":\"PIDKiln Etape\","
    "\"state_topic\":\"pidkiln/temperature\","
    "\"value_template\":\"{{ value_json.step }} / {{ value_json.steps }}\","
    "\"unique_id\":\"pidkiln_step\"}");
  mqttClient.publish("homeassistant/sensor/pidkiln/step/config", payload, true);

  // ---- Nom du programme ----
  snprintf(payload, sizeof(payload),
    "{\"name\":\"PIDKiln Programme\","
    "\"state_topic\":\"pidkiln/temperature\","
    "\"value_template\":\"{{ value_json.prog_name }}\","
    "\"unique_id\":\"pidkiln_prog_name\"}");
  mqttClient.publish("homeassistant/sensor/pidkiln/prog_name/config", payload, true);

  // ---- Heure de démarrage ----
  snprintf(payload, sizeof(payload),
    "{\"name\":\"PIDKiln Heure Demarrage\","
    "\"state_topic\":\"pidkiln/temperature\","
    "\"value_template\":\"{{ value_json.prog_start }}\","
    "\"unique_id\":\"pidkiln_prog_start\"}");
  mqttClient.publish("homeassistant/sensor/pidkiln/prog_start/config", payload, true);

  // ---- Heure de fin estimée ----
  snprintf(payload, sizeof(payload),
    "{\"name\":\"PIDKiln Fin Estimee\","
    "\"state_topic\":\"pidkiln/temperature\","
    "\"value_template\":\"{{ value_json.prog_end }}\","
    "\"unique_id\":\"pidkiln_prog_end\"}");
  mqttClient.publish("homeassistant/sensor/pidkiln/prog_end/config", payload, true);

  // ---- Switch Start/Stop ----
  snprintf(payload, sizeof(payload),
    "{\"name\":\"PIDKiln Start Stop\","
    "\"command_topic\":\"pidkiln/command\","
    "\"state_topic\":\"pidkiln/state\","
    "\"payload_on\":\"START\","
    "\"payload_off\":\"STOP\","
    "\"state_on\":\"ON\","
    "\"state_off\":\"OFF\","
    "\"unique_id\":\"pidkiln_switch\","
    "\"retain\":true}");
  mqttClient.publish("homeassistant/switch/pidkiln/start/config", payload, true);

  Serial.println("[MQTT] discovery HA publié");
}

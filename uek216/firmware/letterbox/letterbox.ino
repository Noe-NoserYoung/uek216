// Letterbox – ESP32 + HC-SR04 -> MQTT -> Node-RED
// Modul 216, Noser Young
//
// Prinzip: Sensor liegt am Boden und misst nach oben (zum Deckel).
//   - Grosse Distanz (Deckel)          -> Briefkasten leer
//   - Timeout / < Schwelle (verdeckt)  -> Post vorhanden
// Gesendet wird bei Statuswechsel + regelmässig als Heartbeat.

#include <WiFi.h>
#include <ESP32MQTTClient.h>
#include "esp_idf_version.h"


// ---------- Konfiguration ----------
// ---------- WLAN / MQTT ----------
const char *WIFI_SSID = "WLAN NAME";
const char *WIFI_PASS = "WLAN PASSWORT";
const char *MQTT_URI  = "mqtt://164.92.239.169:1883";

const char *PUB_TOPIC = "lindenpark/letterbox/noseryoung";
const char *DEVICE_ID = "ESP-32-Post";

const int TRIG_PIN = 5;
const int ECHO_PIN = 18;   // Mit Spannungsteiler absichern (5 V -> 3.3 V)!

const float MAIL_THRESHOLD_CM = 5.0;   // kleiner als das = etwas liegt drauf
const int   STABLE_COUNT      = 3;     // so oft gleich, bevor Status wechselt
const unsigned long MEASURE_INTERVAL_MS = 2000;            // alle 2 s messen
const unsigned long HEARTBEAT_MS        = 5UL * 60 * 1000; // alle 5 min senden

ESP32MQTTClient client;
String clientId;   // global, damit der Text gültig bleibt

bool mailState  = false;   // bestätigter Status
bool stateKnown = false;   // erst nach STABLE_COUNT Messungen gültig
bool lastRaw    = false;
int  sameCount  = 0;
float lastDistance = -1;

volatile bool publishPending = false;   // wird auch im MQTT-Callback gesetzt
const char *pendingReason = "start";
unsigned long lastPublish = 0;
unsigned long lastMeasure = 0;

// ---------- WLAN ----------
void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WLAN verbinden");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  WiFi.setSleep(false);   // stabiler mit Handy-Hotspot
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ---------- Ultraschall ----------
// Einzelmessung in cm, -1 bei Timeout / ungültigem Wert
float measureOnce() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1;

  float distance = duration * 0.0343 / 2;
  if (distance < 2 || distance > 400) return -1;
  return distance;
}

// Mittelwert aus gültigen Messungen, -1 wenn keine gültig
float measureDistance() {
  const int SAMPLES = 5;
  float sum = 0;
  int valid = 0;
  for (int i = 0; i < SAMPLES; i++) {
    float d = measureOnce();
    if (d > 0) {
      sum += d;
      valid++;
    }
    delay(50);
  }
  return valid > 0 ? sum / valid : -1;
}

// ---------- Entscheidung ----------
// -1 bedeutet: Sensor verdeckt (oder defekt!) -> als "Post" gewertet
bool isMailDetected(float distance) {
  return distance < 0 || distance < MAIL_THRESHOLD_CM;
}

// Status nur wechseln, wenn STABLE_COUNT Messungen hintereinander gleich sind
void updateState(float distance) {
  bool raw = isMailDetected(distance);

  if (raw == lastRaw) {
    if (sameCount < STABLE_COUNT) sameCount++;
  } else {
    lastRaw = raw;
    sameCount = 1;
  }

  if (sameCount >= STABLE_COUNT && (!stateKnown || raw != mailState)) {
    mailState = raw;
    stateKnown = true;
    pendingReason = "change";
    publishPending = true;
    Serial.println(mailState ? ">> Status: POST VORHANDEN" : ">> Status: LEER");
  }
}

// ---------- MQTT senden ----------
bool publishStatus(const char *reason) {
  if (!client.isConnected()) return false;

  char buf[160];
  snprintf(buf, sizeof(buf),
           "{\"mail\":%s,\"distance\":%.1f,\"reason\":\"%s\",\"rssi\":%d,\"device\":\"%s\"}",
           mailState ? "true" : "false", lastDistance, reason,
           (int)WiFi.RSSI(), DEVICE_ID);

  // retain = true: Node-RED bekommt nach Neustart sofort den letzten Status
  bool ok = client.publish(PUB_TOPIC, std::string(buf), 0, true);
  Serial.print(ok ? "Gesendet: " : "Senden fehlgeschlagen: ");
  Serial.println(buf);
  return ok;
}

// ---------- MQTT-Callbacks (globale Funktionen, nicht static!) ----------
void onMqttConnect(esp_mqtt_client_handle_t c) {
  if (client.isMyTurn(c)) {
    Serial.println("MQTT verbunden");
    // nach (Neu-)Verbindung aktuellen Status nochmals senden
    if (stateKnown) {
      pendingReason = "reconnect";
      publishPending = true;
    }
  }
}

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
esp_err_t handleMQTT(esp_mqtt_event_handle_t event) {
  client.onEventCallback(event);
  return ESP_OK;
}
#else
void handleMQTT(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);
  client.onEventCallback(event);
}
#endif

// ---------- Arduino ----------
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  setup_wifi();

  // Client-ID eindeutig machen (MAC-Adresse anhängen)
  clientId = String(DEVICE_ID) + "-" + WiFi.macAddress();
  client.setURI(MQTT_URI);
  client.setMqttClientName(clientId.c_str());
  client.loopStart();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WLAN verloren");
    setup_wifi();
  }

  // 1) Messen + Status bestimmen
  if (millis() - lastMeasure >= MEASURE_INTERVAL_MS) {
    lastMeasure = millis();
    lastDistance = measureDistance();

    Serial.print("Distanz: ");
    if (lastDistance < 0) Serial.println("verdeckt / ungültig");
    else { Serial.print(lastDistance); Serial.println(" cm"); }

    updateState(lastDistance);
  }

  // 2) Heartbeat, damit Node-RED weiss, dass das Gerät lebt
  if (stateKnown && !publishPending && millis() - lastPublish >= HEARTBEAT_MS) {
    pendingReason = "heartbeat";
    publishPending = true;
  }

  // 3) Senden (bleibt "pending", bis es wirklich geklappt hat)
  if (publishPending && stateKnown) {
    if (publishStatus(pendingReason)) {
      publishPending = false;
      lastPublish = millis();
    }
  }

  delay(10);
}
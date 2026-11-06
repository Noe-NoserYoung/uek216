// Letterbox – Alarm-Gerät (Sirene + LED + Aus-Knopf) mit OTA-Update
// LED:    an, solange Post im Briefkasten ist
// Sirene: bei Alarm von Node-RED 30 s, dann automatisch aus
// Knopf:  schaltet die Sirene sofort aus

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESP32MQTTClient.h>
#include "esp_idf_version.h"

// ---------- WLAN / MQTT ----------
const char *WIFI_SSID = "WLAN NAME";
const char *WIFI_PASS = "PASSWORT";
const char *MQTT_URI  = "mqtt://164.92.239.169:1883";

const char *ALARM_TOPIC  = "lindenpark/letterbox/noseryoung/alarm";
const char *STATUS_TOPIC = "lindenpark/letterbox/noseryoung";
const char *DEVICE_ID    = "ESP-32-Alarm";

// ---------- OTA ----------
const char *OTA_HOSTNAME = "letterbox-alarm";
const char *OTA_PASSWORD = "OTA PASWORT";   // eigenes Passwort wählen

// ---------- Pins ----------
const int LED_PIN    = 2;
const int SIREN_PIN  = 4;
const int BUTTON_PIN = 13;   // Knopf zwischen Pin und GND

// ---------- Schaltlogik ----------
const bool SIREN_ACTIVE_LOW = false;
const bool LED_ACTIVE_LOW   = false;

const unsigned long ALARM_DURATION_MS = 30000;  // Sirene 30 s
const unsigned long BLINK_MS          = 500;    // Blinktempo Sirene
const unsigned long DEBOUNCE_MS       = 50;

ESP32MQTTClient client;
String clientId;   // global, damit der Text gültig bleibt

// Werden im MQTT-Callback gesetzt, im loop() verarbeitet
volatile bool alarmOnReceived  = false;
volatile bool alarmOffReceived = false;
volatile bool mailPresent      = false;   // Status vom Briefkasten

bool alarmActive = false;
unsigned long alarmStart = 0;

// Knopf
bool lastButtonReading = HIGH;
bool buttonState = HIGH;
unsigned long lastDebounce = 0;

// ---------- Ausgänge ----------
void writeOut(int pin, bool on, bool activeLow) {
  digitalWrite(pin, (on != activeLow) ? HIGH : LOW);
}

void setLed(bool on)   { writeOut(LED_PIN,   on, LED_ACTIVE_LOW); }
void setSiren(bool on) { writeOut(SIREN_PIN, on, SIREN_ACTIVE_LOW); }

void stopAlarm(const char *reason) {
  if (alarmActive) {
    Serial.print(">> SIRENE AUS (");
    Serial.print(reason);
    Serial.println(")");
  }
  alarmActive = false;
  setSiren(false);
}

// ---------- WLAN ----------
void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WLAN verbinden");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  WiFi.setSleep(false);
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ---------- OTA ----------
void setup_ota() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    // Sirene aus, sonst bleibt sie während des Updates hängen
    alarmActive = false;
    setSiren(false);
    Serial.println("OTA Start");
  });
  ArduinoOTA.onEnd([]() { Serial.println("\nOTA Ende"); });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    Serial.printf("OTA: %u%%\r", p * 100 / t);
  });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA Fehler [%u]\n", e); });

  ArduinoOTA.begin();
}

// ---------- MQTT ----------
void onMqttConnect(esp_mqtt_client_handle_t c) {
  if (!client.isMyTurn(c)) return;
  Serial.println("MQTT verbunden");

  // Alarm von Node-RED (oder Test-Button)
  client.subscribe(ALARM_TOPIC, [](const std::string &payload) {
    Serial.print("Alarm-Nachricht: ");
    Serial.println(payload.c_str());
    if (payload.find("\"alarm\":true") != std::string::npos)  alarmOnReceived = true;
    if (payload.find("\"alarm\":false") != std::string::npos) alarmOffReceived = true;
  });

  // Status vom Briefkasten -> steuert die LED
  client.subscribe(STATUS_TOPIC, [](const std::string &payload) {
    Serial.print("Status-Nachricht: ");
    Serial.println(payload.c_str());

    // Leerzeichen entfernen, damit auch {"mail": true} erkannt wird
    std::string p;
    for (char ch : payload) if (ch != ' ') p += ch;

    if (p.find("\"mail\":true") != std::string::npos) {
      mailPresent = true;
      Serial.println(">> Post vorhanden -> LED an");
    } else if (p.find("\"mail\":false") != std::string::npos) {
      mailPresent = false;
      alarmOffReceived = true;   // geleert -> auch Sirene aus
      Serial.println(">> Leer -> LED aus");
    }
  });
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

// ---------- Knopf ----------
bool buttonPressed() {
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonReading) lastDebounce = millis();
  lastButtonReading = reading;

  if (millis() - lastDebounce > DEBOUNCE_MS && reading != buttonState) {
    buttonState = reading;
    if (buttonState == LOW) return true;   // gedrückt
  }
  return false;
}

// ---------- Arduino ----------
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(SIREN_PIN, OUTPUT);
  setLed(false);
  setSiren(false);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  setup_wifi();
  setup_ota();

  clientId = String(DEVICE_ID) + "-" + WiFi.macAddress();
  client.setURI(MQTT_URI);
  client.setMqttClientName(clientId.c_str());
  client.loopStart();

  Serial.println("Bereit");
}

void loop() {
  ArduinoOTA.handle();

  if (WiFi.status() != WL_CONNECTED) setup_wifi();

  bool blink = (millis() / BLINK_MS) % 2 == 0;

  // 1) Knopf -> Sirene aus
  if (buttonPressed()) {
    Serial.println("Knopf gedrückt");
    stopAlarm("Knopf");
  }

  // 2) Sirene ein
  if (alarmOnReceived) {
    alarmOnReceived = false;
    alarmActive = true;
    alarmStart = millis();
    Serial.println(">> SIRENE EIN (30 s)");
  }

  // 3) Sirene aus per MQTT / geleert
  if (alarmOffReceived) {
    alarmOffReceived = false;
    stopAlarm("MQTT / geleert");
  }

  // 4) Nach 30 s automatisch aus
  if (alarmActive && millis() - alarmStart >= ALARM_DURATION_MS) {
    stopAlarm("30 s vorbei");
  }

  // 5) Ausgänge
  setLed(mailPresent);              // LED-Modul blinkt selbst
  setSiren(alarmActive && blink);   // Sirene piept nur bei Alarm

  delay(10);
}
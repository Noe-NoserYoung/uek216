# LB-Projekt «Letterbox» – Modul 216

Intelligenter Briefkasten für die Noser Young AG (Standort Lindenpark).
Ein ESP32 mit Ultraschallsensor erkennt, ob Post im Briefkasten liegt, und sendet den Status per MQTT.
Node-RED prüft jeden Werktag um 12:00, ob der Briefkasten geleert wurde, und löst sonst einen Alarm aus.
Ein zweiter ESP32 zeigt den Status mit einer Lampe an und schaltet bei Alarm eine Sirene ein.

**Team:** Yurii Myronenko, Cristiano Grgic, Noé Bouvard

## Struktur

| Ordner / Datei | Inhalt |
|---|---|
| `firmware/letterbox/` | Code ESP32 Briefkasten (HC-SR04) |
| `firmware/letterbox_alarm/` | Code ESP32 Alarm (Lampe, Sirene, Knopf, OTA) |
| `nodered/flows.json` | Export des Node-RED-Flows inkl. Dashboard |
| `docs/` | Dokumentation (PDF), Netzwerkplan, Verdrahtung, Komponenten |

## Hardware

| Gerät | Komponente | Pin |
|---|---|---|
| ESP32 Briefkasten | HC-SR04 VCC / GND | 3,3 V / GND |
| | HC-SR04 Trig | GPIO 5 |
| | HC-SR04 Echo | GPIO 18 |
| | Speisung | Powerbank (USB) |
| ESP32 Alarm | LED-Modul | GPIO 2 |
| | Sirene / Buzzer | GPIO 4 |
| | Taster (gegen GND, INPUT_PULLUP) | GPIO 13 |
| | Speisung | Laptop (USB) |

Schaltplan: `docs/verdrahtung.png`

## Installation

1. **Arduino IDE** mit Board-Paket *esp32* von Espressif installieren.
2. Bibliothek **ESP32MQTTClient** (Version 1.1.5) über den Library Manager installieren.
3. In beiden `.ino`-Dateien die Platzhalter ersetzen:
   - `WIFI_SSID` / `WIFI_PASS` – eigenes WLAN
   - `OTA_PASSWORD` – eigenes Passwort (nur ESP32 Alarm)
4. Code mit der Arduino IDE per USB hochladen.
   Beim ESP32 Alarm sind spätere Updates auch per OTA möglich (Netzwerk-Port `letterbox-alarm`).
5. **Node-RED:** Menü → Import → `nodered/flows.json` → Deploy.
   Benötigt die Palette `node-red-dashboard`.
6. Dashboard öffnen: http://164.92.239.169:1884/ui

## MQTT-Topics

| Topic | Richtung | Inhalt |
|---|---|---|
| `lindenpark/letterbox/noseryoung` | ESP32 Briefkasten → Broker | `{"mail":true,"distance":…}` |
| `lindenpark/letterbox/noseryoung/alarm` | Node-RED → ESP32 Alarm | `{"alarm":true, …}` |

Broker: `164.92.239.169:1883`

## Bekannte Probleme

- Nach einem Neustart von Node-RED ist die Zeit der letzten Leerung weg. Lösung: Persistent Context in Node-RED aktivieren.
- Der Sensor zeigt «verdeckt» auch, wenn ein Kabel locker ist. Das kann man nicht vom Fall «Post» unterscheiden.
- Mit einem Handy-Hotspot ist die Verbindung manchmal instabil.
- Node-RED nutzt die Uhrzeit des Servers. Die Kontrollzeit muss eventuell angepasst werden.
- MQTT läuft ohne TLS und ohne Anmeldung (siehe Dokumentation, Kapitel 8).

## Nächste Schritte

- Akku mit Deep Sleep statt Powerbank
- Benachrichtigung per Teams oder E-Mail
- Gewichtssensor (HX711) für eine sicherere Erkennung
- Weitere Firmen der Noser Gruppe (neues Topic `lindenpark/letterbox/<firma>`)

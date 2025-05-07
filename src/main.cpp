#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#include "version.h" // Versión del firmware


// Pines
const int ledPin = LED_BUILTIN;  // En ESP8266 suele ser GPIO2
const int configPin = 0;         // D3 (GPIO 0)

// Red y MQTT
WiFiClient espClient;
PubSubClient client(espClient);
WiFiManager wm;

// Configuración de MQTT
// Broker: test.mosquitto.org se usa por defecto si no se configura otro
char mqtt_server[40] = "test.mosquitto.org";
// Topico por defecto: proyecto_sensores/sensor1/mensaje
char mqtt_topic[64] = "proyecto_sensores/sensor1/mensaje";

WiFiManagerParameter custom_mqtt_server("server", "MQTT Broker", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_topic("topic", "MQTT Topic", mqtt_topic, 64);

bool shouldSaveConfig = false;
unsigned long lastHeartbeat = 0;

void saveConfigCallback() {
  Serial.println("Se ha modificado la configuración, se guardará...");
  shouldSaveConfig = true;
}

void loadConfig() {
  if (LittleFS.exists("/config.json")) {
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, configFile);
      if (!error) {
        strlcpy(mqtt_server, doc["mqtt_server"] | "test.mosquitto.org", sizeof(mqtt_server));
        strlcpy(mqtt_topic, doc["mqtt_topic"] | "proyecto_sensores/sensor1/mensaje", sizeof(mqtt_topic));
      }
      configFile.close();
    }
  }
}

void saveConfig() {
  StaticJsonDocument<256> doc;
  doc["mqtt_server"] = mqtt_server;
  doc["mqtt_topic"] = mqtt_topic;

  File configFile = LittleFS.open("/config.json", "w");
  if (configFile) {
    serializeJson(doc, configFile);
    configFile.close();
    Serial.println("Configuración guardada en /config.json");
  } else {
    Serial.println("Error guardando configuración");
  }
}

void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(ledPin, LOW);  // Enciende (activo en bajo)
    delay(delayMs);
    digitalWrite(ledPin, HIGH); // Apaga
    delay(delayMs);
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando al broker MQTT...");
    if (client.connect("ESP8266Client")) {
      Serial.println("Conectado");
    } else {
      Serial.print("Fallo, rc=");
      Serial.print(client.state());
      Serial.println(" intentando en 5s");
      delay(5000);
    }
  }
}

void setup() {
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH); // Apagado
  pinMode(configPin, INPUT_PULLUP);
  delay(100);

  Serial.begin(115200);
  delay(100);

  if (!LittleFS.begin()) {
    Serial.println("Fallo al montar LittleFS");
  }

  // Establece forzar el inicio del portal mediante un botón físico
  // Si el botón está presionado, forzamos la configuración
  //bool forceConfig = digitalRead(configPin) == LOW;

  bool forceConfig = true; // Forzar configuración para pruebas

  loadConfig();

  wm.setSaveConfigCallback(saveConfigCallback);
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_topic);

  // Agrega versión del firmware al footer HTML
  String versionFooter = "<div style='text-align:center;font-size:smaller;margin-top:20px;color:#666;'>Firmware: ";
  versionFooter += FIRMWARE_VERSION;
  versionFooter += "</div>";
  
  wm.setCustomHeadElement(versionFooter.c_str());
  
  

  if (forceConfig) {
    Serial.println("Entrando en modo configuración...");
    wm.setConfigPortalTimeout(30);
    digitalWrite(ledPin, LOW); // LED encendido
    wm.startConfigPortal("Sensor-Config");
    digitalWrite(ledPin, HIGH); // LED apagado al salir
  } else {
    if (!wm.autoConnect("Sensor-Config")) {
      Serial.println("Falló conexión WiFi, reiniciando...");
      ESP.restart();
    }
  }

  strlcpy(mqtt_server, custom_mqtt_server.getValue(), sizeof(mqtt_server));
  strlcpy(mqtt_topic, custom_mqtt_topic.getValue(), sizeof(mqtt_topic));

  if (shouldSaveConfig) {
    saveConfig();
  }

  Serial.println("WiFi conectado");
  Serial.print("IP local: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqtt_server, 1883);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  static unsigned long lastPublish = 0;
  unsigned long now = millis();

  // Publicación periódica
  if (now - lastPublish > 5000) { // cada 5 segundos
    String payload = "Hola desde ESP8266";
    client.publish(mqtt_topic, payload.c_str());
    Serial.println("Publicado: " + payload + " en el topic: " + mqtt_topic);

    blinkLED(1, 100); // Feedback visual
    lastPublish = now;
  }

  // Pulso de vida cada 2s
  if (now - lastHeartbeat > 2000) {
    digitalWrite(ledPin, LOW);
    delay(30);
    digitalWrite(ledPin, HIGH);
    lastHeartbeat = now;
  }
}

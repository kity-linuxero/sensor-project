#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#include "version.h" // Versión del firmware

// Pines
const int ledPin = LED_BUILTIN;  // En ESP8266 suele ser GPIO2

// Red y MQTT
WiFiClient espClient;
PubSubClient client(espClient);
WiFiManager wm;

// Configuración de MQTT
char mqtt_server[40] = "test.mosquitto.org";
char mqtt_topic[64] = "proyecto_sensores/sensor/";

WiFiManagerParameter custom_mqtt_server("server", "MQTT Broker", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_topic("topic", "MQTT Topic", mqtt_topic, 64);

bool shouldSaveConfig = false;
unsigned long lastHeartbeat = 0;

// Intervalo de sensado y publicación
unsigned int publish_interval = 10; // en segundos, por defecto 10
char mqtt_interval[6] = "10"; // WiFiManager requiere string

WiFiManagerParameter custom_mqtt_interval("interval", "Intervalo (segundos)", mqtt_interval, 6);

// ========================= FUNCIONES =========================
void saveConfigCallback() {
  Serial.println("Se ha modificado la configuración, se guardará...");
  shouldSaveConfig = true;
}

// Cargar configuración guardada
void loadConfig() {
  if (LittleFS.exists("/config.json")) {
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
      StaticJsonDocument<256> doc;
      // Si no encuentra el archivo, se crea uno nuevo con valores por defecto
      if (deserializeJson(doc, configFile) == DeserializationError::Ok) {
        strlcpy(mqtt_server, doc["mqtt_server"] | "test.mosquitto.org", sizeof(mqtt_server));
        strlcpy(mqtt_topic, doc["mqtt_topic"] | "proyecto_sensores/sensor1/mensaje", sizeof(mqtt_topic));

        publish_interval = doc["interval"] | 10;
        snprintf(mqtt_interval, sizeof(mqtt_interval), "%u", publish_interval);
        
        custom_mqtt_interval.setValue(mqtt_interval, sizeof(mqtt_interval));
        custom_mqtt_server.setValue(mqtt_server, sizeof(mqtt_server));
        custom_mqtt_topic.setValue(mqtt_topic, sizeof(mqtt_topic));
        
        
        //publish_interval = atoi(mqtt_interval);
      }
      
      //serializeJsonPretty(doc, Serial);
      configFile.close();
    }
  }
  
  Serial.println("Server: " + String(mqtt_server)+"\n Topic: " + String(mqtt_topic) + "\n Intervalo: " + String(publish_interval) + "s");
  delay(1000);
}

// Guardar configuración en el sistema de archivos
void saveConfig() {
  StaticJsonDocument<256> doc;
  doc["mqtt_server"] = mqtt_server;
  doc["mqtt_topic"] = mqtt_topic;
  doc["interval"] = publish_interval;


  File configFile = LittleFS.open("/config.json", "w");
  if (configFile) {
    serializeJson(doc, configFile);
    Serial.println(serializeJsonPretty(doc, Serial));
    configFile.close();
    Serial.println("Configuración guardada en /config.json");
  } else {
    Serial.println("Error guardando configuración");
  }
  Serial.println("Fin de la carga de configuración");
}

// Configuración de WiFi y MQTT
void setupWiFi() {
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_topic);
  wm.addParameter(&custom_mqtt_interval);

  wm.setConfigPortalTimeout(30);  // Mostrar el portal durante 30 segundos
  Serial.println("Entrando en modo configuración...");
  digitalWrite(ledPin, LOW); // LED encendido

  bool configResult = wm.startConfigPortal("Sensor-Config"); // Devuelve true si se conecta, false si expira
  digitalWrite(ledPin, HIGH); // LED apagado

  if (!configResult) {
    Serial.println("Timeout del portal. Intentando conectar con los datos guardados...");
    WiFi.begin(); // Usa las credenciales almacenadas por WiFiManager
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
      delay(500);
      Serial.print(".");
      retries++;
    }
    Serial.println();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi conectado tras el portal (o por timeout). IP:");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("No se pudo conectar a WiFi. Se reiniciará el dispositivo.");
    delay(3000);
    ESP.restart();
  }

  // Cargar parámetros personalizados
  strlcpy(mqtt_server, custom_mqtt_server.getValue(), sizeof(mqtt_server)); // Cargar el broker MQTT
  strlcpy(mqtt_topic, custom_mqtt_topic.getValue(), sizeof(mqtt_topic)); // Cargar el topic MQTT
  publish_interval = atoi(custom_mqtt_interval.getValue()); // Convertir string a entero
  if (publish_interval == 0) publish_interval = 10; // Si el intervalo es 0, se establece en 10 segundos

  if (shouldSaveConfig) saveConfig(); // Guardar configuración si se ha modificado
}

// Configuración y definición de servidor MQTT
void setupMQTT() {
  client.setServer(mqtt_server, 1883);
}

// Conexión al broker MQTT
void reconnectMQTT() {
  unsigned int intentos = 0; //Intentos de conexión
  while (!client.connected()) {
    Serial.print("Conectando al broker MQTT...");
    if (client.connect("ESP_RC_001")) {
      Serial.println("Conectado");
    } else {
      // Falla al conectar, espera 5 segundos y vuelve a intentar
      Serial.print("Fallo, rc=");
      Serial.print(client.state());
      Serial.println(" - Intentando en 5s");
      delay(5000);
      intentos++;
      if (intentos > 5) {
        Serial.println("No se pudo conectar al broker MQTT, reiniciando...");
        ESP.restart(); // Reiniciar si no se conecta después de varios intentos
      }
    }
  }
}

// Parpadeo del LED de estado
void heartbeatLED() {
  digitalWrite(ledPin, LOW);
  delay(30);
  digitalWrite(ledPin, HIGH);
}

// Publicar mensaje en el topic MQTT
void publishMessage() {
  String payload = "alive";

  // Armar los topics específicos
  String topicStatus = String(mqtt_topic) + "/status";

  client.publish(topicStatus.c_str(), payload.c_str());
  Serial.println("Publicado: " + payload + " en el topic: " + mqtt_topic);
}

// Sensores simulados (no implementados en este código)
float simulateTemperature() {
  return random(200, 350) / 10.0; // Temperaturas entre 20.0 y 35.0 °C
}

float simulateHumidity() {
  return random(300, 800) / 10.0; // Humedad entre 30.0% y 80.0%
}

// publica los datos de los sensores simulados en el topic MQTT
void publishSensorData() {
  float temp = simulateTemperature();
  float hum = simulateHumidity();

  // Armar los topics específicos
  String topicTemp = String(mqtt_topic) + "temp";
  String topicHum = String(mqtt_topic) + "hum";

  // Convertir a cadenas para enviar por MQTT
  char payloadTemp[16];
  dtostrf(temp, 4, 1, payloadTemp);  // float a string con 1 decimal

  char payloadHum[16];
  dtostrf(hum, 4, 1, payloadHum);

  // Publicar
  boolean ok1 = client.publish(topicTemp.c_str(), payloadTemp);
  boolean ok2 = client.publish(topicHum.c_str(), payloadHum);

  if (ok1 && ok2) {
    Serial.println("Publicado:");
    Serial.println(topicTemp + ": " + payloadTemp);
    Serial.println(topicHum + ": " + payloadHum);
  } else {
    Serial.println("Error al publicar temperatura o humedad");
  }
}




// ========================= SETUP Y LOOP =========================

// Configuración inicial
void setup() {
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH); // LED apagado por defecto

  Serial.begin(115200);
  Serial.println("\n\nIniciando...");
  Serial.print("Firmware: " FIRMWARE_VERSION);
  Serial.println(" | Build date: " __DATE__ " " __TIME__);
  delay(3000);

  if (!LittleFS.begin()) {
    Serial.println("Fallo al montar LittleFS");
  }

  loadConfig(); // Cargar configuración guardada
  setupWiFi(); // Configurar WiFi y MQTT topic
  setupMQTT(); // Configurar MQTT
}

// Bucle principal
void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  static unsigned long lastPublish = 0;
  unsigned long now = millis();

  if (now - lastPublish > publish_interval * 1000UL) {
    publishSensorData(); // Publicar datos de sensores simulados
    lastPublish = now;
  }

  if (now - lastHeartbeat > 2000) {
    heartbeatLED();
    lastHeartbeat = now;
    publishMessage();
  }
}

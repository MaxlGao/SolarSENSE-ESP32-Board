// Full suite of features: Bluetooth, MQTT, and sensing. Removed web server.
// ================================================================
// ================ Section 0: References =========================
// ================================================================

// https://randomnerdtutorials.com/esp32-mqtt-publish-subscribe-arduino-ide/
// https://randomnerdtutorials.com/testing-mosquitto-broker-and-client-on-raspbbery-pi/

/*  TABLE OF CONTENTS
 *  0 - References
 *  1 - Imports
 *  2 - Configurations
 *  3 - MQTT
 *  4 - Wifi
 *  5 - Sensing
 *  6 - Setup
 *  7 - Loop
 *  8 - Wire
 */
// ================================================================
// ================ Section 1: Imports ============================
// ================================================================

// Imports whose purpose is unclear:
#include <algorithm>
#include <arduino.h>
#include <iostream>
#include <Button2.h>            //https://github.com/LennartHennigs/Button2
#include <Wire.h>

// imported stuffs for pubsub/MQTT
#include <WiFi.h>
#include <PubSubClient.h>

// Imports for the sensors
#include <BH1750.h>             //https://github.com/claws/BH1750
#include <DHT12.h>              //https://github.com/xreef/DHT12_sensor_library

//Miscellaneous
#include "configuration.h"

//Bluetooth
#include "BluetoothSerial.h"

// ================================================================
// ================ Section 2: Configurations =====================
// ================================================================

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif
BluetoothSerial SerialBT;

#define PIDN

typedef enum {
  DHTxx_SENSOR_ID,
  BHT1750_SENSOR_ID,
  SOIL_SENSOR_ID,
  SALT_SENSOR_ID,
  VOLTAGE_SENSOR_ID,
} sensor_id_t;

const int freq = 5000;
const int ledChannel = 0;
const int resolution = 8;
const int ledPin = 5;

typedef struct {
  uint32_t timestamp;     /**< time is in milliseconds */
  float temperature;      /**< temperature is in degrees centigrade (Celsius) */
  float light;            /**< light in SI lux units */
  float pressure;         /**< pressure in hectopascal (hPa) */
  float humidity;         /**<  humidity in percent */
  float altitude;         /**<  altitude in m */
  float voltage;           /**< voltage in volts (V) */
  uint8_t soli;           //Percentage of soil
  uint8_t salt;           //Percentage of salt
}
higrow_sensors_event_t;
//AsyncWebServer      server(80);
BH1750              lightMeter(OB_BH1750_ADDRESS);  //0x23
DHT12               dht12(DHT12_PIN, true);
Button2             button(BOOT_PIN);
Button2             useButton(USER_BUTTON);

bool                has_lightSensor = true;
bool                has_dhtSensor   = true;
uint64_t            timestamp       = 0;

void deviceProbe(TwoWire &t); // what does this even do

// MQTT IP address
const char* mqtt_server = "192.168.43.214"; //<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< IMPORTANT

//sets up MQTT delivery point
WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50]; // variable for the message
int value = 0;

// ================================================================
// ================ Section 3: MQTT ===============================
// ================================================================

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;

  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  // Feel free to add more if statements to control more GPIOs with MQTT

  // If a message is received on the topic esp32/output, you check if the message is either "on" or "off".
  // Changes the output state according to the message
  if (String(topic) == "esp32/output") {
    Serial.print("Changing output to ");
    if (messageTemp == "on") {
      Serial.println("on");
      digitalWrite(ledPin, HIGH);
    }
    else if (messageTemp == "off") {
      Serial.println("off");
      digitalWrite(ledPin, LOW);
    }
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP8266Client")) {
      Serial.println("connected");
      // Subscribe
      // client.subscribe("esp32/output");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// Output Topic is subscribe
// ================================================================
// ================ Section 4: Wifi ===============================
// ================================================================

void setupWiFi() {
#ifdef SOFTAP_MODE
  Serial.print("Connect SSID:");
  Serial.print(WIFI_SSID);
  Serial.print(" Password:");
  Serial.println(WIFI_PASSWD);

  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi connect fail!");
    delay(3000);
    esp_restart();
  }
  Serial.print("WiFi connect success ! , ");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
#endif
  //same stuff as the MQTT test. no need to edit.
}

// ================================================================
// ================ Section 5: Sensing ============================
// ================================================================

bool get_higrow_sensors_event(sensor_id_t id, higrow_sensors_event_t &val) {
  memset(&val, 0, sizeof(higrow_sensors_event_t));
  switch (id) {
    case DHTxx_SENSOR_ID: {
        val.temperature = dht12.readTemperature();
        val.humidity = dht12.readHumidity();
        if (isnan(val.temperature)) {
          val.temperature = 0.0;
        }
        if (isnan(val.humidity)) {
          val.humidity = 0.0;
        }
      }
      break;
    case BHT1750_SENSOR_ID: {
        if (has_lightSensor) {
          val.light = lightMeter.readLightLevel();
        } else {
          val.light = 0;
        }
      }
      break;
    case SOIL_SENSOR_ID: {
        uint16_t soil = analogRead(SOIL_PIN);
        val.soli = map(soil, 0, 4095, 100, 0);
      }
      break;
    case SALT_SENSOR_ID: {
        uint8_t samples = 120;
        uint32_t humi = 0;
        uint16_t array[120];
        for (int i = 0; i < samples; i++) {
          array[i] = analogRead(SALT_PIN);
          delay(2);
        }
        std::sort(array, array + samples);
        for (int i = 1; i < samples - 1; i++) {
          humi += array[i];
        }
        humi /= samples - 2;
        val.salt = humi;
      }
      break;
    case VOLTAGE_SENSOR_ID: {
        int vref = 1100;
        uint16_t volt = analogRead(BAT_ADC);
        val.voltage = ((float)volt / 4095.0) * 6.6 * (vref);
      }
      break;
    default:
      break;
  }
  return true;
}

// ================================================================
// ================ Section 6: Setup ============================
// ================================================================

void setup() {
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  Serial.begin(115200);
  SerialBT.begin("Sensor Test"); //Bluetooth device name
  ledcSetup(ledChannel, freq, resolution);
  ledcAttachPin(5, ledChannel);


  //! Sensor power control pin , use deteced must set high
  pinMode(POWER_CTRL, OUTPUT);
  digitalWrite(POWER_CTRL, 1);
  delay(1000);

  Wire.begin(I2C_SDA, I2C_SCL);

  deviceProbe(Wire);

  dht12.begin();

  if (!lightMeter.begin()) {
    Serial.println(F("Could not find a valid BH1750 sensor, check wiring!"));
    has_lightSensor = false;
  }
  setupWiFi();

}

// ================================================================
// ================ Section 7: Loop ===============================
// ================================================================

void loop() {
  //MORE IMPORTSS
  if (!client.connected()) {
    reconnect();
  }
  if (Serial.available()) {
    SerialBT.write(Serial.read());
  }
  if (SerialBT.available()) {
    Serial.write(SerialBT.read());
  }
  client.loop();
  if (millis() - timestamp > 5000) {
    timestamp = millis();
    float ts = timestamp / 1000;
    char temp[16];
    dtostrf(ts, 1, 2, temp);
    client.publish("esp32/output", "Refreshed-------Number 2-----------------Time: ");
    client.publish("esp32/output", temp);
    higrow_sensors_event_t val = {0};

    get_higrow_sensors_event(BHT1750_SENSOR_ID, val);
    Serial.print("Light: ");
    Serial.println(val.light);
    dtostrf(val.light, 1, 2, temp);
    client.publish("esp32/output", "illumination");
    client.publish("esp32/output", temp);


    get_higrow_sensors_event(SOIL_SENSOR_ID, val);
    Serial.print("Soil: ");
    Serial.println(val.soli);
    dtostrf(val.soli, 1, 2, temp);
    client.publish("esp32/output", "Soil Value");
    client.publish("esp32/output", temp);


    get_higrow_sensors_event(SALT_SENSOR_ID, val);
    Serial.print("Salt: ");
    Serial.println(val.salt);
    dtostrf(val.salt, 1, 2, temp);
    client.publish("esp32/output", "Salt");
    client.publish("esp32/output", temp);

    get_higrow_sensors_event(VOLTAGE_SENSOR_ID, val);
    Serial.print("Battery: ");
    Serial.println(val.voltage);
    dtostrf(val.voltage, 1, 2, temp);
    client.publish("esp32/output", "Battery");
    client.publish("esp32/output", temp);

    get_higrow_sensors_event(DHTxx_SENSOR_ID, val);
    Serial.print("Temperature: ");
    Serial.println(val.temperature);
    Serial.print("Humidity: ");
    Serial.println(val.humidity);
    ledcWrite(0, 256 * val.humidity / 100); ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    dtostrf(val.temperature, 1, 2, temp);
    client.publish("esp32/output", "Temperature");
    client.publish("esp32/output", temp);
    dtostrf(val.humidity, 1, 2, temp);
    client.publish("esp32/output", "Humidity");
    client.publish("esp32/output", temp);
  }
}

// ================================================================
// ================ Section 8: Wire ===============================
// ================================================================

void deviceProbe(TwoWire &t) {
  uint8_t err, addr;
  int nDevices = 0;
  for (addr = 1; addr < 127; addr++) {
    t.beginTransmission(addr);
    err = t.endTransmission();
    if (err == 0) {
      Serial.print("I2C device found at address 0x");
      if (addr < 16)
        Serial.print("0");
      Serial.print(addr, HEX);
      Serial.println(" !");
      switch (addr) {
        case OB_BH1750_ADDRESS:
          has_dhtSensor = true;
          break;
        default:
          break;
      }
      nDevices++;
    } else if (err == 4) {
      Serial.print("Unknow error at address 0x");
      if (addr < 16)
        Serial.print("0");
      Serial.println(addr, HEX);
    }
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("done\n");
}

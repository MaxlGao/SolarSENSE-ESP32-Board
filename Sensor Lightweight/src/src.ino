// ================================================================
// ================ Section 0: References =========================
// ================================================================

// https://randomnerdtutorials.com/esp32-mqtt-publish-subscribe-arduino-ide/
// https://randomnerdtutorials.com/testing-mosquitto-broker-and-client-on-raspbbery-pi/
// https://randomnerdtutorials.com/esp32-mqtt-publish-subscribe-arduino-ide/
// ================================================================
// ================ Section 1: Imports ============================
// ================================================================

// Imports whose purpose is unclear:
#include <algorithm>
#include <arduino.h>
#include <iostream>
#include <Button2.h>            //https://github.com/LennartHennigs/Button2
#include <AsyncTCP.h>           //https://github.com/me-no-dev/AsyncTCP
#include <ESPDash.h>            //https://github.com/ayushsharma82/ESP-DASH
#include <Wire.h>

// Import for the Sensor Web Server (Not to be used in the final edition)
#include <ESPAsyncWebServer.h>  //https://github.com/me-no-dev/ESPAsyncWebServer

// imported stuffs for pubsub/MQTT
#include <WiFi.h>
#include <PubSubClient.h>

// Imports for the sensors
#include <BH1750.h>             //https://github.com/claws/BH1750
#include <DHT12.h>              //https://github.com/xreef/DHT12_sensor_library

//Miscellaneous
#include "configuration.h"

//Bluetooth
//#include "BluetoothSerial.h"

// ================================================================
// ================ Section 2: Configurations =====================
// ================================================================

//#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
//#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
//#endif  
//BluetoothSerial SerialBT;

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
AsyncWebServer      server(80);
ESPDash             dashboard(&server);
BH1750              lightMeter(OB_BH1750_ADDRESS);  //0x23
DHT12               dht12(DHT12_PIN, true);
Button2             button(BOOT_PIN);
Button2             useButton(USER_BUTTON);

bool                has_lightSensor = true;
bool                has_dhtSensor   = true;
uint64_t            timestamp       = 0;

Card *dhtTemperature    = new Card(&dashboard, TEMPERATURE_CARD, DASH_DHT_TEMPERATURE_STRING, "°C");
Card *dhtHumidity       = new Card(&dashboard, HUMIDITY_CARD, DASH_DHT_HUMIDITY_STRING, "%");
Card *illumination      = new Card(&dashboard, GENERIC_CARD, DASH_BH1750_LUX_STRING, "lx");
Card *soilValue         = new Card(&dashboard, GENERIC_CARD, DASH_SOIL_VALUE_STRING, "%");
Card *saltValue         = new Card(&dashboard, GENERIC_CARD, DASH_SALT_VALUE_STRING, "%");
Card *batteryValue      = new Card(&dashboard, GENERIC_CARD, DASH_BATTERY_STRING, "mV");

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
  //"message temporary" i assume
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

//  if (String(topic) == "esp32/output") {
//    Serial.print("Changing output to ");
//    if(messageTemp == "on"){
//      Serial.println("on");
//    }
//    else if(messageTemp == "off"){
//      Serial.println("off");
//    }
//  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
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

void setupWiFi(){
#ifdef SOFTAP_MODE
  Serial.println("Configuring access point...");
  uint8_t mac[6];
  char buff[128];
  WiFi.macAddress(mac);
  sprintf(buff, "T-Higrow-%02X:%02X", mac[4], mac[5]);
  WiFi.softAP(buff);
  Serial.printf("The hotspot has been established, please connect to the %s and output 192.168.4.1 in the browser to access the data page \n", buff);
#else
  WiFi.mode(WIFI_STA);

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
  server.begin();
  //same stuff as the MQTT test. no need to edit.
}

// ================================================================
// ================ Section 5: Sensing ============================
// ================================================================

bool get_higrow_sensors_event(sensor_id_t id, higrow_sensors_event_t &val){
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

void setup(){
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  Serial.begin(115200);
//  SerialBT.begin("ESP32test"); //Bluetooth device name
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

void loop(){
  //MORE IMPORTSS
  if (!client.connected()) {
    //reconnect();
  }
//  if (Serial.available()) {
//    SerialBT.write(Serial.read());
//  }
//  if (SerialBT.available()) {
//    Serial.write(SerialBT.read());
//  }
  client.loop();
    if (millis() - timestamp > 5000) {
        timestamp = millis();
        float ts = timestamp/1000;
        char temp[16];
        dtostrf(ts, 1, 2, temp);
        client.publish("esp32/output", "Refreshed-------Number 2-----------------Time: ");
        client.publish("esp32/output", temp);
        higrow_sensors_event_t val = {0};

        get_higrow_sensors_event(BHT1750_SENSOR_ID, val);
        illumination->update(val.light);
        Serial.print("Light: ");
        Serial.println(val.light);
        dtostrf(val.light, 1, 2, temp);
        client.publish("esp32/output", "illumination");
        client.publish("esp32/output", temp);

        
        get_higrow_sensors_event(SOIL_SENSOR_ID, val);
        soilValue->update(val.soli);
        Serial.print("Soil: ");
        Serial.println(val.soli);
        dtostrf(val.soli, 1, 2, temp);
        client.publish("esp32/output", "Soil Value");
        client.publish("esp32/output", temp);
        
        
        get_higrow_sensors_event(SALT_SENSOR_ID, val);
        saltValue->update(val.salt);
        Serial.print("Salt: ");
        Serial.println(val.salt);
        dtostrf(val.salt, 1, 2, temp);
        client.publish("esp32/output", "Salt");
        client.publish("esp32/output", temp);        
        
        get_higrow_sensors_event(VOLTAGE_SENSOR_ID, val);
        batteryValue->update(val.voltage);
        Serial.print("Battery: ");
        Serial.println(val.voltage);
        dtostrf(val.voltage, 1, 2, temp);
        client.publish("esp32/output", "Battery");
        client.publish("esp32/output", temp);
                
        get_higrow_sensors_event(DHTxx_SENSOR_ID, val);
        dhtTemperature->update(val.temperature);
        dhtHumidity->update(val.humidity);
        Serial.print("Temperature: ");
        Serial.println(val.temperature);
        Serial.print("Humidity: ");
        Serial.println(val.humidity);
        ledcWrite(0,256*val.humidity/100);////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        dtostrf(val.temperature, 1, 2, temp);   
        client.publish("esp32/output", "Temperature");
        client.publish("esp32/output", temp);
        dtostrf(val.humidity, 1, 2, temp);        
        client.publish("esp32/output", "Humidity");
        client.publish("esp32/output", temp);
        
        dashboard.sendUpdates();
        //looks like the only thing that sends the data over.
    }
}

// ================================================================
// ================ Section 8: Wire ===============================
// ================================================================

void deviceProbe(TwoWire &t){
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

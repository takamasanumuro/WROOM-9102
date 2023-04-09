#include <Arduino.h> // Allow to use Arduino functions
#include <WiFi.h> // Allow to connect to a WiFi network
#include <ArduinoOTA.h> // Allow to update the device over the air (OTA)
#include <ESPmDNS.h> // Allow to use mDNS to access the device by its hostname (e.g. http://esp32.local) instead of only its IP address
#include <WiFiUdp.h> // Allow to use UDP protocol
#include "ESPAsyncWebServer.h" // Allows to create a web server that can be accessed asynchronously
#include "Preferences.h" // Allows to store data in the non-volatile memory of the ESP32 based on namespace that contains key-value pairs
#include "OneWire.h" // Allows to use 1-wire protocol to communicate with supported devices
#include "DallasTemperature.h" // Allows to use Dallas Temperature ICs attached to the 1-wire bus
#define USE_OTA 1
#define USE_WIFI 1
#define USE_DALLAS 1
#define SSID "ARARIBOIA-NEW"
#define PASSWORD "mamadores"

using timer = uint32_t;
// TODO: Test task scheduling with FreeRTOS
// TODO: Test RemoteDebug library
// // TODO: Test EEPROM library
// TODO: Test physical I2C communication*
// TODO: Test bluetooth communication
// TODO: Test TCP communication
Preferences nvs_memory; // Create a Preferences object to store data in the non-volatile memory of the ESP32
AsyncWebServer server(80); // Create an AsyncWebServer object to create a web server that can be accessed asynchronously
uint16_t serverAccessCounter = 0; // Create a variable to store the number of times the web server was accessed

#if USE_DALLAS
OneWire oneWire(23); // Create a OneWire object to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
DallasTemperature sensors(&oneWire);  // Create a DallasTemperature object to pass our oneWire reference to Dallas Temperature
DeviceAddress thermalProbeOne, thermalProbeTwo; // Create arrays to hold 8-byte addresses for each device on the bus
#endif

void PrintDallasAddress(DeviceAddress device_address) {
#if USE_DALLAS
  for (uint8_t i = 0; i < 8; i++) {
    if (device_address[i] < 15) Serial.print("0");
    Serial.print(device_address[i], HEX);
  }
#endif
}

void DallasTemperatureSetup() {
#if USE_DALLAS
  sensors.begin();
  Serial.printf("Found %d devices\n", sensors.getDeviceCount());
  if (!sensors.getAddress(thermalProbeOne, 0)) {
    Serial.printf("Unable to find address for Device 0\n");
  } else {
    Serial.printf("Device 0 Address: ");
    PrintDallasAddress(thermalProbeOne);
    Serial.printf("\n");
  }
  if (!sensors.getAddress(thermalProbeTwo, 1)) {
    Serial.printf("Unable to find address for Device 1\n");
  } else {
    Serial.printf("Device 1 Address: ");
    PrintDallasAddress(thermalProbeTwo);
    Serial.printf("\n");
  }
#endif
}

void DallasRequestTemperatures() {
#if USE_DALLAS
  static timer last_request = 0;
  if (millis() - last_request > 1000) {
    Serial.printf("Number of devices: %d\n", sensors.getDeviceCount());
    sensors.requestTemperatures();
    for (uint8_t i = 0; i < sensors.getDeviceCount(); i++) {
      Serial.printf("Device %d Temperature: %f\n", i, sensors.getTempCByIndex(i));
    }
    last_request = millis();
  }
#endif
}

void RTOSBlink(void* param) {
  gpio_pad_select_gpio(GPIO_NUM_2); // Sets GPIO_NUM_2 to GPIO mode as pins can be configured to other modes
  esp_err_t result = gpio_set_direction(GPIO_NUM_2, GPIO_MODE_INPUT_OUTPUT);
  if (result != ESP_OK) {
     Serial.printf("Error setting GPIO direction: %d", result);
     vTaskDelete(NULL);
   }
  Serial.printf("Blink task started successfully");
  while (1) {
    bool level = !gpio_get_level(GPIO_NUM_2);
    gpio_set_level(GPIO_NUM_2, level);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// The RTOS task must take in a void parameter, then cast it to the appropriate type inside the function.
// The RTOS Task Creator must take in a void casted pointer to a global variable, which will be passed to the task.
// It must be a non stack allocated variable, otherwise it will be deleted after the function returns, causing a crash.
// The flow is as follows:
// Global variable -> void casted pointer on creation -> void casted pointer on task -> casted pointer on function
// Those tasks must represent cohesive functional blocks, with low coupling between them.

void StartRTOSBlink() {
  xTaskCreate(RTOSBlink, "Blink", 1000, NULL, 1, NULL);
}

void StartNVSMemory() {
  nvs_memory.begin("flash", false);
  serverAccessCounter = nvs_memory.getUShort("access_counter", 0);
  if (serverAccessCounter == 0) {
    Serial.printf("No access counter found. Initializing to 0\n");
    nvs_memory.putUShort("access_counter", 0);
  } else {
    Serial.printf("Access counter found. Value: %d\n", serverAccessCounter);
  }
}

void StoreNVSCounter() {
  nvs_memory.putUShort("access_counter", serverAccessCounter);
}

void ServerSetup() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Hello, client #" + String(serverAccessCounter));
    StoreNVSCounter();
    serverAccessCounter++;
  });
  server.begin();
}

void StartWifiConnection() {
#if (USE_WIFI)
  WiFi.begin(SSID, PASSWORD);	
  while (WiFi.status() != WL_CONNECTED) {
    static int counter = 0;
    delay(500);
    Serial.print(".");
    if (counter++ > 10) {
      Serial.println("Could not connect to WiFi");
      ESP.restart();
    }
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
#endif
}

/**
 * @brief Start the OTA protocol.
 * MUST be called after the WiFi connection is established, otherwise it will crash the ESP32.
 * Make sure to insert the correct network credentials for the connection
 * 
 */
void StartOTAProtocol() {
#if (USE_OTA) && (USE_WIFI)
  
  ArduinoOTA.setHostname("esp32"); // Set the hostname for the OTA service.
  ArduinoOTA.onStart([]() { // Method chain to set the OTA handlers.
    if (ArduinoOTA.getCommand() == U_FLASH) {
      Serial.println("Start updating sketch ...");
    } else {  
      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating SPIFFS ...");
    }
  }).onEnd([]() {
    Serial.println("\nEnd");
  }).onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  }).onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
#endif
}

void OTAHandler () {
#if (USE_OTA) && (USE_WIFI)
  ArduinoOTA.handle();
#endif
}

void setup() {
  Serial.begin(115200);
  StartWifiConnection();
  StartOTAProtocol();
  ServerSetup();
  StartNVSMemory();
  StartRTOSBlink();
  DallasTemperatureSetup();
}

void loop() {
  OTAHandler();
  DallasRequestTemperatures();
}
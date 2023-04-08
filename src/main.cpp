#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include "ESPAsyncWebServer.h"
#include "EEPROM.h"
#include "Preferences.h"

#define USE_OTA 1
#define USE_WIFI 1
#define SSID "ARARIBOIA-NEW"
#define PASSWORD "mamadores"

using timer = uint32_t;
// TODO: Test task scheduling with FreeRTOS
// TODO: Test RemoteDebug library
// // TODO: Test EEPROM library
// TODO: Test physical I2C communication*
// TODO: Test bluetooth communication
// TODO: Test TCP communication
Preferences nvs_memory;
AsyncWebServer server(80);
static uint16_t serverAccessCounter = 0;

//Blink LED with FreeRTOS

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
    Serial.printf("LED set to: %d\n", level);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

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
}

void loop() {
  OTAHandler();
}
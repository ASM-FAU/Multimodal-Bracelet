#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <time.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// Replace with your WiFi settings
const char* ssid = "enter your WiFi SSID";  // Input your SSID here
const char* password = "enter your WiFi password"; // Input your password here

// Replace with your MQTT server settings
const char* mqtt_server = "enter IP adress";
const int mqtt_port = 1883;
const char* mqtt_topic = "fmg_data";

// NTP server details
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

// Variable for storing the previous second for comparison
int prevSecond = -1;

struct tm timeinfo;
unsigned long startMillis;

// Variable to store the latest timestamp
unsigned long long latestTimestamp = 0;

// Pin definitions
#define FSR0_0 1
#define FSR0_1 2
#define FSR0_2 3
#define FSR0_3 4
#define FSR1_0 5
#define FSR1_1 6
#define FSR1_2 7
#define FSR1_3 10
#define FSR2_0 12
#define FSR2_1 13
#define FSR2_2 14
#define FSR2_3 15
#define MIX 17
#define E 42
#define S3 41
#define S2 40
#define S1 39
#define S0 38

// Number of sensors and values per sensor
#define NUM_SEN         6
#define VAL_PER_SEN     4
#define NUM_DATA        (NUM_SEN * VAL_PER_SEN)
#define NUM_DIREKT_READ 12
#define NUM_MIX         (NUM_DATA - NUM_DIREKT_READ)

uint8_t sensor_pins[NUM_DIREKT_READ] = {FSR0_0, FSR0_1, FSR0_2, FSR0_3, FSR1_0, FSR1_1, FSR1_2, FSR1_3, FSR2_0, FSR2_1, FSR2_2, FSR2_3};

float fmg_data[NUM_DATA] = {0};

// LED settings
#define PIN            18
#define NUMPIXELS      1
#define BRIGHTNESS     50
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);
int LED_flag = 0;

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);
  
  setup_pin();
  setup_led(); 
  setup_wifi(); // Connect to WiFi network
  
  client.setServer(mqtt_server, mqtt_port); // Set MQTT server
  client_connection();

  // Initialize NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  getLocalTime(&timeinfo); 
  int currentSecond = timeinfo.tm_sec;
  prevSecond = currentSecond;
  while (true) {      
    getLocalTime(&timeinfo); // Fetch the current time
    int currentSecond = timeinfo.tm_sec;
    if (currentSecond != prevSecond) {
      break; // Break the loop when the second changes
    }
  }
  startMillis = millis();   
}

void setup_pin() {
  // Initialize pin modes
  pinMode(FSR0_0, INPUT);
  pinMode(FSR0_1, INPUT);
  pinMode(FSR0_2, INPUT);
  pinMode(FSR0_3, INPUT);
  pinMode(FSR1_0, INPUT);
  pinMode(FSR1_1, INPUT);
  pinMode(FSR1_2, INPUT);
  pinMode(FSR1_3, INPUT);
  pinMode(FSR2_0, INPUT);
  pinMode(FSR2_1, INPUT);
  pinMode(FSR2_2, INPUT);
  pinMode(FSR2_3, INPUT);
  pinMode(MIX, INPUT);
  pinMode(E, OUTPUT);
  digitalWrite(E, HIGH);
  pinMode(S3, OUTPUT);
  digitalWrite(S3, LOW);
  pinMode(S1, OUTPUT);
  digitalWrite(S1, LOW);
  pinMode(S2, OUTPUT);
  digitalWrite(S2, LOW);
  pinMode(S0, OUTPUT);
  digitalWrite(S0, LOW);
}

void setup_led() {
  pixels.begin(); // Initialize the NeoPixel strip
  pixels.setBrightness(BRIGHTNESS); // Set brightness
  pixels.show(); // Turn off all LEDs on start
}

void led_colour() {
  // Set LED color based on connection status
  switch (LED_flag) {
    case 0:
      pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Red: initial state
      break;
    case 1:
      pixels.setPixelColor(0, pixels.Color(255, 255, 0)); // Yellow: WiFi connected
      break;
    case 2:
      pixels.setPixelColor(0, pixels.Color(0, 255, 0)); // Green: server connected
      break;
    case 3:
      pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Blue: data sending
      break;
  }
  pixels.show();  // Apply color change
}

void setup_wifi() {
  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");
  LED_flag = 1;
  led_colour(); 
}

void reconnect() {
  // Attempt to reconnect until successful
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect to the MQTT broker
    if (client.connect("mqttx_5384a150")) { // Client ID
      Serial.println("The client connection is successful");
      // Subscribe to the topic upon successful connection
      client.subscribe(mqtt_topic);
      // Send "Armband connected" message after successful connection
      const char* system_message = "Armband connected";
      client.publish(mqtt_topic, system_message);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in one seconds");
      // Wait 1 second before retrying
      delay(1000);
    }
  }
}

void client_connection() {
  // Check connection and reconnect if necessary
  if (!client.connected()) {
    Serial.print("client not connected");
    LED_flag = 1;
    led_colour(); // Change LED to yellow for reconnect logic
    reconnect(); // Call reconnect function
  } else {
    client.loop();
    LED_flag = 2;
    led_colour(); // Change LED to green
  }
}

void loop() {
  // Maintain network and MQTT connections
  if (WiFi.status() != WL_CONNECTED) {
  setup_wifi(); // Reconnect WiFi if necessary
  }

  if (!client.connected()) {
  reconnect(); // Try to reconnect to MQTT if necessary
  }
  client.loop(); // Maintain MQTT connection, handle incoming messages

  FSRReading();

  getLocalTime(&timeinfo);
  updateTimestamp(timeinfo);
  sendFmgDataToMQTT();
  delay(100); // Short delay for stability
}

void FSRReading() {
  // Read values from Force Sensitive Resistors (FSRs)
  Serial.println("Reading FSR values:");
  for (uint8_t sen = 0; sen < NUM_SEN; sen++) {
    Serial.print("Sensor ");
    Serial.print(sen);
    Serial.print(": ");
    for (uint8_t val = 0; val < VAL_PER_SEN; val++) {
      // Recalculate idx for global index, not distinguishing direct or mixed channel readings
      uint8_t Idx = sen * VAL_PER_SEN + val;
      
      if (Idx < NUM_DIREKT_READ) {
        // Direct readings
        fmg_data[Idx] = analogRead(sensor_pins[Idx]) * 0.08225 * (5.0 / 1023.0);
      } else {
        // Readings through mixed channel
        uint8_t mixChannel = Idx - NUM_DIREKT_READ;
        SetMixCh(mixChannel);
        fmg_data[Idx] = analogRead(MIX) * 0.08225 * (5.0 / 1023.0);
        digitalWrite(E, HIGH);
      }
      Serial.print(fmg_data[Idx]);
      Serial.print("V ");
    }
    Serial.println();
  }
}

void SetMixCh(byte Ch) {
  // Set multiplexer channels for mixed readings
  digitalWrite(E, LOW);
  digitalWrite(S0, bitRead(Ch, 0));
  digitalWrite(S1, bitRead(Ch, 1));
  digitalWrite(S2, bitRead(Ch, 2));
  digitalWrite(S3, bitRead(Ch, 3));
}

void sendFmgDataToMQTT() {
  // Calculate the total byte size needed for the data array and timestamp
  size_t dataSize = NUM_DATA * sizeof(float); // Byte size of Fmg data
  size_t timestampSize = sizeof(latestTimestamp); // Byte size of the timestamp
  size_t totalSize = dataSize + timestampSize; // Total byte size

  // Create a large enough byte array to store fmg_data and latestTimestamp
  byte dataBuffer[totalSize];

  // First, copy fmg_data into the buffer
  memcpy(dataBuffer, fmg_data, dataSize);

  // Then, copy latestTimestamp to the end of the buffer
  memcpy(dataBuffer + dataSize, &latestTimestamp, timestampSize);

  // Send binary data over MQTT
  if (!client.publish(mqtt_topic, dataBuffer, totalSize)) {
    Serial.println("Failed to publish FSR data");
  } else {
    Serial.println("FSR data published successfully");
  }
}

void updateTimestamp(struct tm &timeinfo) {
  // Construct the timestamp
  latestTimestamp = (unsigned long long)(timeinfo.tm_hour) * 10000000 +
                    (unsigned long long)(timeinfo.tm_min) * 100000 +
                    (unsigned long long)(timeinfo.tm_sec) * 1000 +
                    (unsigned long long)((millis() - startMillis) % 1000);
                    
  Serial.println(latestTimestamp); // Print the timestamp for verification
}

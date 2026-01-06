#include <ArduinoMqttClient.h>
#include <ArduinoJson.h>
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"

#if defined(ARDUINO_SAMD_MKRWIFI1010) || defined(ARDUINO_SAMD_NANO_33_IOT) || defined(ARDUINO_AVR_UNO_WIFI_REV2)
  #include <WiFiNINA.h>
#elif defined(ARDUINO_SAMD_MKR1000)
  #include <WiFi101.h>
#elif defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ARDUINO_PORTENTA_H7_M7) || defined(ARDUINO_NICLA_VISION) || defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_GIGA) || defined(ARDUINO_OPTA)
  #include <WiFi.h>
#elif defined(ARDUINO_PORTENTA_C33)
  #include <WiFiC3.h>
#elif defined(ARDUINO_UNOR4_WIFI)
  #include <WiFiS3.h>
#endif

#include "arduino_secrets.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
byte mac[6];  // WiFi MAC address

// Built-in LED Matrix (12x8)
ArduinoLEDMatrix matrix;

// Motor & Control Pins
int motor1_forward = 1;
int motor1_backward = 2;
int motor1_speed = 3;

int motor2_forward = 4;
int motor2_backward = 5;
int motor2_speed = 6;

int motor3_left = 7;
int motor3_right = 8;
int motor3_speed = 9;

int status_led = LED_BUILTIN;

int kicker_forward = 12;
int kicker_speed = 10;

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

const char broker[] = "your-broker-ip-or-domain";
int port = 1883;
const char topic[] = "rover/control"; // MQTT topic

// Speed control state
String lastCommand = "";
unsigned long lastCommandTime = 0;
int repeatCount = 0;
int baseSpeed = 255;
int maxSpeed = 255;

// Keep heart and hello (working bitmaps)
const uint32_t heart[] = {
  0x3184a444,
  0x44042081,
  0x100a0040
};

const uint32_t hello[] = {
  0x44447e44,
  0x447e4040,
  0x7e000000
};

// =============================
// Pixel-drawn symbols (clean)
// =============================

void drawCheckmark() {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.point(2, 5);
  matrix.point(3, 6);
  matrix.point(4, 7);
  matrix.point(6, 5);
  matrix.point(7, 4);
  matrix.point(8, 3);
  matrix.point(9, 2);
  matrix.endDraw();
}

void drawCross() {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  for (int i = 2; i < 10; i++) {
    matrix.point(i, i-1);   // diagonal ↘
    matrix.point(i, 8-i);   // diagonal ↙
  }
  matrix.endDraw();
}

void drawWifi() {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  // arcs (simplified wifi symbol)
  matrix.point(6, 2);
  matrix.point(5, 3); matrix.point(7, 3);
  matrix.point(4, 4); matrix.point(8, 4);
  matrix.point(3, 5); matrix.point(9, 5);
  matrix.point(6, 6);
  matrix.endDraw();
}

void drawBall() {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  // simple circle pattern
  matrix.point(5, 2); matrix.point(6, 2);
  matrix.point(4, 3); matrix.point(7, 3);
  matrix.point(4, 4); matrix.point(7, 4);
  matrix.point(5, 5); matrix.point(6, 5);
  matrix.endDraw();
}

void drawReady() {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textFont(Font_4x6);
  matrix.beginText(0, 1, 0xFFFFFF);
  matrix.println("RDY");
  matrix.endText();
  matrix.endDraw();
}

// Simple arrows using pixels
void drawArrow(String direction) {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  if (direction == "up") {
    matrix.point(6, 1);
    matrix.point(5, 2); matrix.point(7, 2);
    matrix.point(4, 3); matrix.point(8, 3);
    matrix.point(6, 4); matrix.point(6, 5); matrix.point(6, 6);
  } else if (direction == "down") {
    matrix.point(6, 2); matrix.point(6, 3); matrix.point(6, 4);
    matrix.point(4, 5); matrix.point(8, 5);
    matrix.point(5, 6); matrix.point(7, 6);
    matrix.point(6, 7);
  } else if (direction == "left") {
    matrix.point(2, 4);
    matrix.point(3, 3); matrix.point(3, 5);
    matrix.point(4, 2); matrix.point(4, 6);
    matrix.point(5, 4); matrix.point(6, 4); matrix.point(7, 4);
  } else if (direction == "right") {
    matrix.point(5, 4); matrix.point(6, 4); matrix.point(7, 4);
    matrix.point(8, 2); matrix.point(8, 6);
    matrix.point(9, 3); matrix.point(9, 5);
    matrix.point(10, 4);
  }
  matrix.endDraw();
}

// =============================
// SETUP & LOOP (unchanged logic)
// =============================

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    delay(10);
  }

  matrix.begin();
  matrix.loadFrame(heart);
  delay(1000);
  matrix.loadFrame(hello);
  delay(1000);

  pinMode(motor1_forward, OUTPUT);
  pinMode(motor1_backward, OUTPUT);
  pinMode(motor1_speed, OUTPUT);
  pinMode(motor2_forward, OUTPUT);
  pinMode(motor2_backward, OUTPUT);
  pinMode(motor2_speed, OUTPUT);
  pinMode(motor3_left, OUTPUT);
  pinMode(motor3_right, OUTPUT);
  pinMode(motor3_speed, OUTPUT);
  pinMode(status_led, OUTPUT);
  pinMode(kicker_forward, OUTPUT);
  pinMode(kicker_speed, OUTPUT);

  Serial.println("=== Arduino Network & MQTT Debug ===");
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  drawWifi();

  WiFi.begin(ssid, pass);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("\nWiFi Connected!");
  drawCheckmark();
  delay(1000);

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC Address: ");
  WiFi.macAddress(mac);
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 16) Serial.print("0");
    Serial.print(mac[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();

  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textSize(1);
  matrix.textFont(Font_4x6);
  matrix.beginText(0, 1, 0xFFFFFF);
  matrix.println("CONN");
  matrix.endText();
  matrix.endDraw();
  delay(1000);

  WiFiClient testClient;
  testClient.setTimeout(10000);
  if (testClient.connect(broker, port)) {
    Serial.println("TCP Connection: SUCCESS!");
    testClient.stop();
  } else {
    Serial.println("TCP Connection: FAILED!");
    drawCross();
    delay(1000);
    if (testClient.connect("8.8.8.8", 53)) {
      Serial.println("Internet connectivity OK.");
      testClient.stop();
    } else {
      Serial.println("No internet connectivity.");
      while (1) delay(1000);
    }
  }

  mqttClient.setConnectionTimeout(15000);
  mqttClient.setKeepAliveInterval(60000);

  String clientId = "Arduino-" + String(random(10000, 99999));
  mqttClient.setId(clientId.c_str());

  Serial.print("MQTT Client ID: ");
  Serial.println(clientId);

  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textSize(1);
  matrix.textFont(Font_4x6);
  matrix.beginText(0, 1, 0xFFFFFF);
  matrix.println("MQTT");
  matrix.endText();
  matrix.endDraw();

  if (mqttClient.connect(broker, port)) {
    Serial.println("MQTT Connection: SUCCESS!");
    matrix.beginDraw();
    matrix.stroke(0xFFFFFFFF);
    matrix.textSize(1);
    matrix.textFont(Font_4x6);
    matrix.beginText(2, 1, 0xFFFFFF);
    matrix.println("GO");
    matrix.endText();
    matrix.endDraw();

    mqttClient.subscribe(topic);
    mqttClient.beginMessage("rover/status");
    mqttClient.print("Arduino connected successfully!");
    mqttClient.endMessage();
  } else {
    Serial.print("MQTT Connection: FAILED (");
    Serial.print(mqttClient.connectError());
    Serial.println(")");
    drawCross();
    while (1) delay(1000);
  }

  mqttClient.onMessage(onMqttMessage);
  Serial.println("=== Setup Complete ===");

 // drawReady();
}

void loop() {
  if (!mqttClient.connected()) {
    Serial.println("MQTT disconnected. Attempting reconnect...");
    drawCross();
    if (mqttClient.connect(broker, port)) {
      Serial.println("Reconnected!");
      drawCheckmark();
      delay(500);
      //drawReady();
      mqttClient.subscribe(topic);
    } else {
      Serial.print("Reconnect failed: ");
      Serial.println(mqttClient.connectError());
      delay(5000);
    }
  }

  mqttClient.poll();

  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 30000) {
    mqttClient.beginMessage("rover/heartbeat");
    mqttClient.print(millis());
    mqttClient.endMessage();
    lastHeartbeat = millis();
    matrix.loadFrame(heart);
    delay(200);
   // drawReady();
  }
}

// =============================
// Motor & Command Handling
// =============================

void stopMotors() {
  digitalWrite(motor1_forward, LOW);
  digitalWrite(motor1_backward, LOW);
  digitalWrite(motor2_forward, LOW);
  digitalWrite(motor2_backward, LOW);
  digitalWrite(motor3_left, LOW);
  digitalWrite(motor3_right, LOW);
  analogWrite(motor1_speed, 0);
  analogWrite(motor2_speed, 0);
  analogWrite(motor3_speed, 0);
}

void kickBall() {
  drawBall();
  digitalWrite(kicker_forward, HIGH);
  analogWrite(kicker_speed, 180);
  delay(200);
  digitalWrite(kicker_forward, LOW);
  analogWrite(kicker_speed, 0);
  delay(300);
 // drawReady();
}

void displayCustomText(String text) {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textSize(1);
  matrix.textFont(Font_4x6);
  matrix.beginText(0, 1, 0xFFFFFF);
  matrix.println(text);
  matrix.endText();
  matrix.endDraw();
}

void displayScrollingText(String text) {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textScrollSpeed(50);
  matrix.textFont(Font_5x7);
  matrix.beginText(0, 1, 0xFFFFFF);
  matrix.println(text.c_str());
  matrix.endText(SCROLL_LEFT);
  matrix.endDraw();
}

void onMqttMessage(int messageSize) {
  String message = "";
  while (mqttClient.available()) {
    char c = (char)mqttClient.read();
    message += c;
  }

  Serial.print("📩 Raw message: ");
  Serial.println(message);

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("⚠️ JSON parse failed: ");
    Serial.println(error.c_str());
    drawCross();
    delay(500);
    // drawReady();
    return;
  }

  String command = doc["command"] | "";
  int speed = maxSpeed;
  command.toLowerCase();

  Serial.print("✅ Command: ");
  Serial.print(command);
  Serial.print(" | Speed: ");
  Serial.println(speed);

  unsigned long now = millis();
  if (command == lastCommand && (now - lastCommandTime < 2000)) {
    repeatCount++;
  } else {
    repeatCount = 1;
  }
  lastCommand = command;
  lastCommandTime = now;

  stopMotors();

  if (command == "forward") {
    drawArrow("up");
    digitalWrite(motor1_forward, HIGH);
    digitalWrite(motor2_forward, HIGH);
    analogWrite(motor1_speed, speed);
    analogWrite(motor2_speed, speed);
  } else if (command == "backward") {
    drawArrow("down");
    digitalWrite(motor1_backward, HIGH);
    digitalWrite(motor2_backward, HIGH);
    analogWrite(motor1_speed, speed);
    analogWrite(motor2_speed, speed);
  } else if (command == "rotate-left") {
    displayCustomText("RL");
    digitalWrite(motor1_forward, HIGH);
    digitalWrite(motor2_backward, HIGH);
    digitalWrite(motor3_right, HIGH);
    analogWrite(motor1_speed, speed);
    analogWrite(motor2_speed, speed);
    analogWrite(motor3_speed, speed);
  } else if (command == "rotate-right") {
    displayCustomText("RR");
    digitalWrite(motor1_backward, HIGH);
    digitalWrite(motor2_forward, HIGH);
    digitalWrite(motor3_left, HIGH);
    analogWrite(motor1_speed, speed);
    analogWrite(motor2_speed, speed);
    analogWrite(motor3_speed, speed);
  } else if (command == "left") {
    drawArrow("left");
    digitalWrite(motor1_forward, HIGH);
    digitalWrite(motor2_backward, HIGH);
    digitalWrite(motor3_left, HIGH);
    analogWrite(motor1_speed, speed * 0.55);
    analogWrite(motor2_speed, speed * 0.55);
    analogWrite(motor3_speed, speed);
  } else if (command == "right") {
    drawArrow("right");
    digitalWrite(motor1_backward, HIGH);
    digitalWrite(motor2_forward, HIGH);
    digitalWrite(motor3_right, HIGH);
    analogWrite(motor1_speed, speed * 0.55);
    analogWrite(motor2_speed, speed * 0.55);
    analogWrite(motor3_speed, speed);
  } else if (command == "stop") {
    stopMotors();
   // displayCustomText("STOP");
   matrix.beginDraw();
  matrix.clear();
  matrix.endDraw();
    delay(1000);
   // drawReady();
  } else if (command == "kicker") {
    kickBall();
  } else if (command == "gas")
  {
    displayCustomText("FAST");
    analogWrite(motor1_speed, maxSpeed);
    analogWrite(motor2_speed, maxSpeed);
    Serial.println("🚀 Boost mode");
  } 
  else if (command == "display") {
    // Custom display command - show text from JSON
    String displayMsg = doc["text"] | "TEST";
    displayCustomText(displayMsg);
    Serial.print("📺 Display: ");
    Serial.println(displayMsg);
  }
  else if (command == "scroll") {
    // Scrolling text command
    String scrollMsg = doc["text"] | "HELLO WORLD";
    displayScrollingText(scrollMsg);
    Serial.print("📜 Scrolling: ");
    Serial.println(scrollMsg);
  }
  else if (command == "pattern") {
    // Show predefined patterns
    String patternName = doc["name"] | "heart";
    if (patternName == "heart") {
      matrix.loadFrame(heart);
    }
    Serial.print("🎨 Pattern: ");
    Serial.println(patternName);
  }
  else {
    Serial.print("❓ Unknown command: ");
    Serial.println(command);
    delay(500);
  }
}

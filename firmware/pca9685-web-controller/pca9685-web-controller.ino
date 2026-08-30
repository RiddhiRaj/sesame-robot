#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

// Reuse the original project UI and movement definitions without modifying them.
#include "../movement-sequences.h"
#include "../captive-portal.h"

// Waveshare ESP32-S3-Zero access point.
static constexpr char AP_SSID[] = "Sesame-Controller";
static constexpr char AP_PASS[] = "12345678";
static constexpr char HOSTNAME[] = "sesame-robot";

// Waveshare ESP32-S3-Zero -> PCA9685.
static constexpr uint8_t I2C_SDA = 8;
static constexpr uint8_t I2C_SCL = 9;
static constexpr uint8_t PCA_ADDRESS = 0x40;

// PCA9685 registers.
static constexpr uint8_t MODE1 = 0x00;
static constexpr uint8_t MODE2 = 0x01;
static constexpr uint8_t LED0_ON_L = 0x06;
static constexpr uint8_t PRESCALE = 0xFE;

// Conservative pulse range proven during the first hardware test. Do not widen
// this until the assembled robot has been checked for collisions and stalls.
static constexpr uint16_t SERVO_MIN_US = 1000;
static constexpr uint16_t SERVO_MAX_US = 2000;
static constexpr uint16_t SERVO_PERIOD_US = 20000;  // 50 Hz

// Complex movements stay locked while mechanical calibration is unfinished.
// Manual sliders plus Rest and Stand remain available in the web portal.
static constexpr bool COMPLEX_MOVEMENTS_ENABLED = true;

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;
static constexpr uint16_t DNS_PORT = 53;

// Globals required by movement-sequences.h.
int frameDelay = 100;
int walkCycles = 10;
int motorCurrentDelay = 30;
String currentCommand = "";

String currentFaceName = "none";
int faceFps = 8;
bool pcaReady = false;
bool lastServoWriteOk = false;
unsigned long lastInputTime = 0;

// Channel order matches movement-sequences.h:
// CH0 R1, CH1 R2, CH2 L1, CH3 L2, CH4 R4, CH5 R3, CH6 L3, CH7 L4.
int8_t servoSubtrim[8] = {0, 0, 0, 0, 0, 0, 0, 0};
uint8_t lastCommandedAngle[8] = {90, 90, 90, 90, 90, 90, 90, 90};

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(PCA_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegister(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(PCA_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(PCA_ADDRESS, static_cast<uint8_t>(1)) != 1) return false;
  value = Wire.read();
  return true;
}

bool pcaPresent() {
  Wire.beginTransmission(PCA_ADDRESS);
  return Wire.endTransmission() == 0;
}

bool setChannelRaw(uint8_t channel, uint16_t onCount, uint16_t offCount) {
  if (channel >= 16) return false;

  const uint8_t base = LED0_ON_L + 4 * channel;
  Wire.beginTransmission(PCA_ADDRESS);
  Wire.write(base);
  Wire.write(onCount & 0xFF);
  Wire.write((onCount >> 8) & 0x0F);
  Wire.write(offCount & 0xFF);
  Wire.write((offCount >> 8) & 0x0F);
  return Wire.endTransmission() == 0;
}

bool disableChannelRaw(uint8_t channel) {
  if (channel >= 16) return false;

  const uint8_t base = LED0_ON_L + 4 * channel;
  Wire.beginTransmission(PCA_ADDRESS);
  Wire.write(base);
  Wire.write(0x00);
  Wire.write(0x00);
  Wire.write(0x00);
  Wire.write(0x10);  // Full-off bit.
  return Wire.endTransmission() == 0;
}

bool disableAllChannelsRaw() {
  bool ok = true;
  for (uint8_t channel = 0; channel < 16; ++channel) {
    if (!disableChannelRaw(channel)) ok = false;
  }
  return ok;
}

uint16_t pulseUsToCount(uint16_t pulseUs) {
  return static_cast<uint16_t>((static_cast<uint32_t>(pulseUs) * 4096U +
                                SERVO_PERIOD_US / 2U) /
                               SERVO_PERIOD_US);
}

bool initializePca9685() {
  pcaReady = false;
  if (!pcaPresent()) return false;

  uint8_t oldMode = 0;
  if (!readRegister(MODE1, oldMode)) return false;

  // Sleep while changing the prescaler. 25 MHz / (4096 * 50 Hz) - 1 = 121.
  if (!writeRegister(MODE1, (oldMode & 0x7F) | 0x10)) return false;
  if (!writeRegister(PRESCALE, 121)) return false;
  if (!writeRegister(MODE2, 0x04)) return false;  // Totem-pole outputs.
  if (!writeRegister(MODE1, oldMode & 0x6F)) return false;
  delay(5);
  if (!writeRegister(MODE1, (oldMode & 0x6F) | 0xA0)) return false;

  if (!disableAllChannelsRaw()) return false;
  pcaReady = true;
  return true;
}

bool ensurePcaReady() {
  if (pcaReady) return true;
  pcaReady = initializePca9685();
  return pcaReady;
}

void releaseAllServos() {
  if (!ensurePcaReady()) return;
  if (!disableAllChannelsRaw()) pcaReady = false;
}

void setServoAngle(uint8_t channel, int angle) {
  lastServoWriteOk = false;
  if (channel >= 8 || !ensurePcaReady()) return;

  const int logicalAngle = constrain(angle, 0, 180);
  // The R4/L4 horns were re-indexed at 90 degrees. Mirror the shifted lower
  // legs so Rest travels outward while Stand remains centered at 90 degrees.
  int movementAngle = logicalAngle;
  if (channel == R4) movementAngle = constrain(90 - logicalAngle, 0, 180);
  if (channel == L4) movementAngle = constrain(270 - logicalAngle, 0, 180);
  const int adjustedAngle = constrain(movementAngle + servoSubtrim[channel], 0, 180);
  const uint16_t pulseUs = map(adjustedAngle, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
  const uint16_t count = pulseUsToCount(pulseUs);

  lastServoWriteOk = setChannelRaw(channel, 0, count);
  if (!lastServoWriteOk) {
    pcaReady = false;
    return;
  }

  lastCommandedAngle[channel] = logicalAngle;
  if (motorCurrentDelay > 0) delay(motorCurrentDelay);
}

void loadCalibration() {
  preferences.begin("sesame-pca", true);
  for (uint8_t channel = 0; channel < 8; ++channel) {
    const String key = "trim" + String(channel);
    servoSubtrim[channel] = static_cast<int8_t>(
        constrain(preferences.getInt(key.c_str(), 0), -30, 30));
  }
  preferences.end();
}

void saveTrim(uint8_t channel) {
  preferences.begin("sesame-pca", false);
  const String key = "trim" + String(channel);
  preferences.putInt(key.c_str(), servoSubtrim[channel]);
  preferences.end();
}

// OLED/face compatibility stubs required by the original movement file.
void setFace(const String &faceName) { currentFaceName = faceName; }
void setFaceMode(FaceAnimMode mode) { (void)mode; }
void setFaceWithMode(const String &faceName, FaceAnimMode mode) {
  currentFaceName = faceName;
  (void)mode;
}
void enterIdle() {}
void exitIdle() {}

void delayWithFace(unsigned long ms) {
  const unsigned long start = millis();
  while (millis() - start < ms) {
    dnsServer.processNextRequest();
    server.handleClient();
    delay(1);
  }
}

bool pressingCheck(String cmd, int ms) {
  const unsigned long start = millis();
  while (millis() - start < static_cast<unsigned long>(ms)) {
    dnsServer.processNextRequest();
    server.handleClient();
    if (currentCommand != cmd) {
      runStandPose(1);
      return false;
    }
    delay(1);
  }
  return true;
}

void recordInput() { lastInputTime = millis(); }

bool commandAllowed(const String &command) {
  if (command == "rest" || command == "stand" || command == "stop") return true;
  return COMPLEX_MOVEMENTS_ENABLED;
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", index_html);
}

void handleCommandWeb() {
  if (server.hasArg("motor") && server.hasArg("value")) {
    const int motorNumber = server.arg("motor").toInt();
    const int namedIndex = servoNameToIndex(server.arg("motor"));
    const int angle = server.arg("value").toInt();

    int channel = -1;
    if (motorNumber >= 1 && motorNumber <= 8) channel = motorNumber - 1;
    else if (namedIndex >= 0) channel = namedIndex;

    if (channel < 0 || angle < 0 || angle > 180) {
      server.send(400, "text/plain", "Invalid motor or angle");
      return;
    }

    setServoAngle(channel, angle);
    recordInput();
    if (!lastServoWriteOk) {
      server.send(503, "text/plain", "PCA9685 is not responding");
      return;
    }
    server.send(200, "text/plain", "OK");
    return;
  }

  if (server.hasArg("stop")) {
    currentCommand = "";
    recordInput();
    server.send(200, "text/plain", "OK");
    return;
  }

  String command = "";
  if (server.hasArg("pose")) command = server.arg("pose");
  else if (server.hasArg("go")) command = server.arg("go");

  if (command.length() == 0) {
    server.send(400, "text/plain", "Bad arguments");
    return;
  }

  if (!commandAllowed(command)) {
    server.send(423, "text/plain",
                "Movement locked until mechanical calibration is complete");
    return;
  }

  currentCommand = command;
  recordInput();
  server.send(200, "text/plain", "OK");
}

void handleGetSettings() {
  String json = "{";
  json += "\"frameDelay\":" + String(frameDelay) + ",";
  json += "\"walkCycles\":" + String(walkCycles) + ",";
  json += "\"motorCurrentDelay\":" + String(motorCurrentDelay) + ",";
  json += "\"faceFps\":" + String(faceFps);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetSettings() {
  if (server.hasArg("frameDelay")) {
    frameDelay = constrain(server.arg("frameDelay").toInt(), 10, 1000);
  }
  if (server.hasArg("walkCycles")) {
    walkCycles = constrain(server.arg("walkCycles").toInt(), 1, 50);
  }
  if (server.hasArg("motorCurrentDelay")) {
    motorCurrentDelay = constrain(server.arg("motorCurrentDelay").toInt(), 0, 500);
  }
  server.send(200, "text/plain", "OK");
}

void handleGetCalibration() {
  String json = "{";
  json += "\"pcaReady\":" + String(pcaReady ? "true" : "false") + ",";
  json += "\"complexMovementsEnabled\":" +
          String(COMPLEX_MOVEMENTS_ENABLED ? "true" : "false") + ",";
  json += "\"pulseMinUs\":" + String(SERVO_MIN_US) + ",";
  json += "\"pulseMaxUs\":" + String(SERVO_MAX_US) + ",";
  json += "\"trim\":[";
  for (uint8_t i = 0; i < 8; ++i) {
    if (i > 0) json += ",";
    json += String(servoSubtrim[i]);
  }
  json += "],\"angles\":[";
  for (uint8_t i = 0; i < 8; ++i) {
    if (i > 0) json += ",";
    json += String(lastCommandedAngle[i]);
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleSetCalibration() {
  if (!server.hasArg("channel") || !server.hasArg("trim")) {
    server.send(400, "text/plain", "channel and trim are required");
    return;
  }

  const int channel = server.arg("channel").toInt();
  const int trim = server.arg("trim").toInt();
  if (channel < 0 || channel > 7 || trim < -30 || trim > 30) {
    server.send(400, "text/plain", "channel 0-7, trim -30 to 30");
    return;
  }

  servoSubtrim[channel] = static_cast<int8_t>(trim);
  saveTrim(channel);
  server.send(200, "text/plain", "Saved");
}

void handleRelease() {
  currentCommand = "";
  releaseAllServos();
  if (!pcaReady) {
    server.send(503, "text/plain", "PCA9685 is not responding");
    return;
  }
  server.send(200, "text/plain", "All servo outputs disabled");
}

void handleGetStatus() {
  String json = "{";
  json += "\"currentCommand\":\"" + currentCommand + "\",";
  json += "\"currentFace\":\"" + currentFaceName + "\",";
  json += "\"networkConnected\":false,";
  json += "\"pcaReady\":" + String(pcaReady ? "true" : "false") + ",";
  json += "\"calibrationLocked\":" +
          String(COMPLEX_MOVEMENTS_ENABLED ? "false" : "true") + ",";
  json += "\"apIP\":\"" + WiFi.softAPIP().toString() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

String extractJsonString(const String &body, const String &key) {
  const String marker = "\"" + key + "\"";
  int start = body.indexOf(marker);
  if (start < 0) return "";
  start = body.indexOf(':', start + marker.length());
  if (start < 0) return "";
  start = body.indexOf('\"', start + 1);
  if (start < 0) return "";
  const int end = body.indexOf('\"', start + 1);
  if (end < 0) return "";
  return body.substring(start + 1, end);
}

void handleApiCommand() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
    return;
  }

  const String command = extractJsonString(server.arg("plain"), "command");
  if (command.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"Missing command\"}");
    return;
  }

  if (!commandAllowed(command)) {
    server.send(423, "application/json",
                "{\"error\":\"Movement locked until calibration is complete\"}");
    return;
  }

  if (command == "stop") currentCommand = "";
  else currentCommand = command;
  recordInput();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void dispatchCurrentCommand() {
  if (currentCommand.length() == 0) return;

  const String command = currentCommand;
  if (command == "rest") {
    runRestPose();
    if (currentCommand == command) currentCommand = "";
  } else if (command == "stand") {
    runStandPose(1);
    if (currentCommand == command) currentCommand = "";
  } else if (command == "forward") runWalkPose();
  else if (command == "backward") runWalkBackward();
  else if (command == "left") runTurnLeft();
  else if (command == "right") runTurnRight();
  else if (command == "wave") runWavePose();
  else if (command == "dance") runDancePose();
  else if (command == "swim") runSwimPose();
  else if (command == "point") runPointPose();
  else if (command == "pushup") runPushupPose();
  else if (command == "bow") runBowPose();
  else if (command == "cute") runCutePose();
  else if (command == "freaky") runFreakyPose();
  else if (command == "worm") runWormPose();
  else if (command == "shake") runShakePose();
  else if (command == "shrug") runShrugPose();
  else if (command == "dead") runDeadPose();
  else if (command == "crab") runCrabPose();
  else currentCommand = "";
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println();
  Serial.println("Sesame PCA9685 web controller (no OLED)");

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  loadCalibration();

  if (initializePca9685()) {
    Serial.println("PCA9685 ready at 0x40; all outputs start OFF");
  } else {
    Serial.println("ERROR: PCA9685 not found at 0x40; motion commands are blocked");
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASS)) {
    Serial.println("ERROR: could not start Wi-Fi access point");
  }

  const IPAddress apIp = WiFi.softAPIP();
  Serial.print("Wi-Fi: ");
  Serial.println(AP_SSID);
  Serial.print("Portal: http://");
  Serial.println(apIp);

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://sesame-robot.local");
  }

  dnsServer.start(DNS_PORT, "*", apIp);
  server.on("/", handleRoot);
  server.on("/generate_204", handleRoot);
  server.on("/hotspot-detect.html", handleRoot);
  server.on("/connecttest.txt", handleRoot);
  server.on("/cmd", handleCommandWeb);
  server.on("/getSettings", handleGetSettings);
  server.on("/setSettings", handleSetSettings);
  server.on("/getCalibration", handleGetCalibration);
  server.on("/setCalibration", handleSetCalibration);
  server.on("/release", handleRelease);
  server.on("/api/status", handleGetStatus);
  server.on("/api/command", HTTP_POST, handleApiCommand);
  server.onNotFound(handleRoot);
  server.begin();

  lastInputTime = millis();
  Serial.println("Web controller ready");
  Serial.println("Calibration lock: manual sliders, Rest, and Stand only");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  dispatchCurrentCommand();
  delay(2);
}

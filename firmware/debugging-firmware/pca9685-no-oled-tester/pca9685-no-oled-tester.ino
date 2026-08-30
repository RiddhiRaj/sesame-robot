#include <Arduino.h>
#include <Wire.h>

// Waveshare ESP32-S3-Zero -> PCA9685
static constexpr uint8_t I2C_SDA = 8;
static constexpr uint8_t I2C_SCL = 9;
static constexpr uint8_t PCA_ADDRESS = 0x40;

// PCA9685 registers
static constexpr uint8_t MODE1 = 0x00;
static constexpr uint8_t MODE2 = 0x01;
static constexpr uint8_t LED0_ON_L = 0x06;
static constexpr uint8_t PRESCALE = 0xFE;

// Conservative first-test range. The original firmware uses wider endpoints,
// but 1000-2000 us is safer until every installed joint is calibrated.
static constexpr uint16_t SERVO_MIN_US = 1000;
static constexpr uint16_t SERVO_MAX_US = 2000;
static constexpr uint16_t SERVO_PERIOD_US = 20000;  // 50 Hz

// Same logical order as firmware/movement-sequences.h.
static const char *const SERVO_NAMES[8] = {
  "R1", "R2", "L1", "L2", "R4", "R3", "L3", "L4"
};

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

void setChannelRaw(uint8_t channel, uint16_t onCount, uint16_t offCount) {
  if (channel >= 16) return;

  const uint8_t base = LED0_ON_L + 4 * channel;
  Wire.beginTransmission(PCA_ADDRESS);
  Wire.write(base);
  Wire.write(onCount & 0xFF);
  Wire.write((onCount >> 8) & 0x0F);
  Wire.write(offCount & 0xFF);
  Wire.write((offCount >> 8) & 0x0F);
  Wire.endTransmission();
}

void disableChannel(uint8_t channel) {
  if (channel >= 16) return;

  const uint8_t base = LED0_ON_L + 4 * channel;
  Wire.beginTransmission(PCA_ADDRESS);
  Wire.write(base);
  Wire.write(0x00);  // ON_L
  Wire.write(0x00);  // ON_H
  Wire.write(0x00);  // OFF_L
  Wire.write(0x10);  // OFF_H full-off bit
  Wire.endTransmission();
}

void disableAllChannels() {
  for (uint8_t channel = 0; channel < 16; ++channel) {
    disableChannel(channel);
  }
}

uint16_t pulseUsToCount(uint16_t pulseUs) {
  return static_cast<uint16_t>((static_cast<uint32_t>(pulseUs) * 4096U +
                                SERVO_PERIOD_US / 2U) /
                               SERVO_PERIOD_US);
}

void moveServo(uint8_t channel, int angle) {
  if (channel >= 8) {
    Serial.println("ERROR: channel must be 0-7");
    return;
  }

  angle = constrain(angle, 0, 180);
  const uint16_t pulseUs = map(angle, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
  const uint16_t count = pulseUsToCount(pulseUs);
  setChannelRaw(channel, 0, count);

  Serial.printf("OK: CH%u (%s) -> %d deg, %u us, count %u\n",
                channel, SERVO_NAMES[channel], angle, pulseUs, count);
}

bool initializePca9685() {
  if (!pcaPresent()) return false;

  uint8_t oldMode = 0;
  if (!readRegister(MODE1, oldMode)) return false;

  // Sleep while changing the prescaler. 25 MHz / (4096 * 50 Hz) - 1 = 121.
  if (!writeRegister(MODE1, (oldMode & 0x7F) | 0x10)) return false;
  if (!writeRegister(PRESCALE, 121)) return false;
  if (!writeRegister(MODE2, 0x04)) return false;  // Totem-pole outputs
  if (!writeRegister(MODE1, oldMode & 0x6F)) return false;
  delay(5);
  if (!writeRegister(MODE1, (oldMode & 0x6F) | 0xA0)) return false;

  disableAllChannels();
  return true;
}

void scanI2c() {
  Serial.println("I2C scan:");
  uint8_t found = 0;

  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found 0x%02X%s\n", address,
                    address == PCA_ADDRESS ? " (PCA9685)" : "");
      ++found;
    }
  }

  if (found == 0) Serial.println("  no I2C devices found");
}

void printHelp() {
  Serial.println();
  Serial.println("Sesame PCA9685 no-OLED tester");
  Serial.println("Outputs start OFF; nothing moves automatically.");
  Serial.println("Commands:");
  Serial.println("  scan       - scan the I2C bus");
  Serial.println("  0,90       - move only channel 0 to 90 degrees");
  Serial.println("  all,90     - move channels 0-7 to 90 degrees, staggered");
  Serial.println("  off        - disable all 16 PCA outputs");
  Serial.println("  help       - show this help");
  Serial.println();
}

void processCommand(String input) {
  input.trim();
  if (input.length() == 0) return;

  if (input.equalsIgnoreCase("scan")) {
    scanI2c();
    return;
  }

  if (input.equalsIgnoreCase("off") || input.equalsIgnoreCase("stop")) {
    disableAllChannels();
    Serial.println("OK: all PCA9685 outputs disabled");
    return;
  }

  if (input.equalsIgnoreCase("help")) {
    printHelp();
    return;
  }

  const int comma = input.indexOf(',');
  if (comma < 0) {
    Serial.println("ERROR: use channel,angle (example: 0,90), scan, or off");
    return;
  }

  const String target = input.substring(0, comma);
  const int angle = constrain(input.substring(comma + 1).toInt(), 0, 180);

  if (target.equalsIgnoreCase("all")) {
    for (uint8_t channel = 0; channel < 8; ++channel) {
      moveServo(channel, angle);
      delay(100);
    }
    return;
  }

  if (target.length() != 1 || !isDigit(target[0])) {
    Serial.println("ERROR: channel must be 0-7");
    return;
  }

  moveServo(target.toInt(), angle);
}

void setup() {
  Serial.begin(115200);
  delay(1200);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  printHelp();
  scanI2c();

  if (!initializePca9685()) {
    Serial.println("ERROR: PCA9685 not responding at 0x40; no servo command will be sent.");
    return;
  }

  Serial.println("PCA9685 initialized at 50 Hz. All outputs remain OFF.");
}

void loop() {
  if (Serial.available()) {
    processCommand(Serial.readStringUntil('\n'));
  }
  delay(2);
}

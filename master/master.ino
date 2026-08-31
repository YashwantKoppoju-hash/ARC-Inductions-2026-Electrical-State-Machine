#include <Arduino.h>
#include <Servo.h>
#include <Wire.h>

#include <Protocol.h>

using Protocol::ControllerCommand;
using Protocol::SystemState;

// Confirm these assignments against schematic_v2 before hardware testing.
constexpr uint8_t PHOTORESISTOR_PIN = A0;
constexpr uint8_t GAS_SENSOR_PIN = A1;
constexpr uint8_t TEMPERATURE_PIN = A2;
constexpr uint8_t BUZZER_PIN = 7;
constexpr uint8_t SERVO_PIN = 6;

constexpr unsigned long COMMAND_POLL_INTERVAL_MS = 300;
constexpr float TEMPERATURE_EMERGENCY_THRESHOLD_C = 45.0f;
constexpr uint16_t GAS_ALERT_THRESHOLD = 180;
constexpr uint16_t GAS_CLEAR_THRESHOLD = 130;
constexpr uint16_t BLACKOUT_DROP_THRESHOLD = 200;  // Calibrate in hardware.
constexpr int SERVO_NORMAL_ANGLE = 0;
constexpr int SERVO_EMERGENCY_ANGLE = 180;

struct SensorData {
    uint16_t brightness = 0;
    uint16_t gasPpm = 0;
    float temperature = 0.0f;
};

struct ControllerInput {
    bool activationConfirmed = false;
    bool manualReset = false;
};

SensorData sensorData;
ControllerInput controllerInput;
SystemState currentState = SystemState::Standby;
Servo ventServo;

unsigned long lastCommandPoll = 0;
uint16_t brightnessBaseline = 0;
bool brightnessBaselineInitialized = false;

float getTemperature();
uint16_t getLux();
uint16_t getGasPpm();
void updateSensors();
void updateState();
void handleCommunication();
void executeStateAction();
void setServoAngle(int angle);
void setBuzzer(bool enabled);
void processCommand(ControllerCommand command);
bool isBlackoutDetected(uint16_t brightness);
void writeUint16(uint16_t value);

void setup() {
    pinMode(PHOTORESISTOR_PIN, INPUT);
    pinMode(GAS_SENSOR_PIN, INPUT);
    pinMode(TEMPERATURE_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    setBuzzer(false);
    ventServo.attach(SERVO_PIN);
    setServoAngle(SERVO_NORMAL_ANGLE);

    Wire.begin();
    Wire.setClock(100000L);
}

void loop() {
    // TemperatureEmergency is latched and requires a manual reset.
    if (currentState == SystemState::TemperatureEmergency) {
        handleCommunication();

        if (controllerInput.manualReset) {
            currentState = SystemState::Standby;
            controllerInput.manualReset = false;
        }

        executeStateAction();
        return;
    }

    updateSensors();
    handleCommunication();
    updateState();
    executeStateAction();
}

float getTemperature() {
    const int rawValue = analogRead(TEMPERATURE_PIN);
    const float voltage = rawValue * (5.0f / 1023.0f);

    // TMP36-style conversion. Confirm the actual temperature sensor before use.
    return (voltage - 0.5f) * 100.0f;
}

uint16_t getLux() {
    // The schematic currently provides an analog divider value. A calibrated
    // lux conversion can replace this raw ADC value during hardware testing.
    return static_cast<uint16_t>(analogRead(PHOTORESISTOR_PIN));
}

uint16_t getGasPpm() {
    // The gas sensor currently provides an analog value. A calibrated ppm
    // conversion can replace this raw ADC value during hardware testing.
    return static_cast<uint16_t>(analogRead(GAS_SENSOR_PIN));
}

void updateSensors() {
    sensorData.brightness = getLux();
    sensorData.gasPpm = getGasPpm();
    sensorData.temperature = getTemperature();
}

void updateState() {
    if (sensorData.temperature > TEMPERATURE_EMERGENCY_THRESHOLD_C) {
        currentState = SystemState::TemperatureEmergency;
        return;
    }

    if (controllerInput.activationConfirmed &&
        currentState == SystemState::Standby) {
        currentState = SystemState::ActiveMonitoring;
        controllerInput.activationConfirmed = false;
    }

    if (currentState == SystemState::Standby) {
        return;
    }

    const bool blackoutDetected = isBlackoutDetected(sensorData.brightness);
    const bool previouslyGasAlerted =
        currentState == SystemState::GasAlert ||
        currentState == SystemState::MultiFault;
    const uint16_t gasThreshold = previouslyGasAlerted
                                      ? GAS_CLEAR_THRESHOLD
                                      : GAS_ALERT_THRESHOLD;
    const bool gasAlert = sensorData.gasPpm > gasThreshold;

    if (gasAlert && blackoutDetected) {
        currentState = SystemState::MultiFault;
    } else if (gasAlert) {
        currentState = SystemState::GasAlert;
    } else if (blackoutDetected) {
        currentState = SystemState::BlackoutAlert;
    } else {
        currentState = SystemState::ActiveMonitoring;
    }
}

void handleCommunication() {
    const unsigned long now = millis();
    if (now - lastCommandPoll < COMMAND_POLL_INTERVAL_MS) {
        return;
    }
    lastCommandPoll = now;

    Wire.beginTransmission(Protocol::SlaveAddress);
    Wire.write(Protocol::PollRequest);
    Wire.write(static_cast<uint8_t>(currentState));
    writeUint16(sensorData.brightness);
    writeUint16(sensorData.gasPpm);

    const int16_t temperatureTenths =
        static_cast<int16_t>(sensorData.temperature * 10.0f);
    Wire.write(static_cast<uint8_t>(temperatureTenths & 0xFF));
    Wire.write(static_cast<uint8_t>((temperatureTenths >> 8) & 0xFF));

    if (Wire.endTransmission() != 0) {
        return;
    }

    const uint8_t bytesReceived = Wire.requestFrom(
        Protocol::SlaveAddress, static_cast<uint8_t>(1));
    if (bytesReceived != 1 || !Wire.available()) {
        return;
    }

    const uint8_t rawCommand = Wire.read();
    if (rawCommand <= static_cast<uint8_t>(ControllerCommand::ToggleDisplay)) {
        processCommand(static_cast<ControllerCommand>(rawCommand));
    }
}

void processCommand(ControllerCommand command) {
    switch (command) {
        case ControllerCommand::Activate:
            controllerInput.activationConfirmed = true;
            break;

        case ControllerCommand::ResetTemperatureEmergency:
            if (currentState == SystemState::TemperatureEmergency) {
                controllerInput.manualReset = true;
            }
            break;

        case ControllerCommand::ToggleDisplay:
        case ControllerCommand::None:
            // Display toggling is handled by the slave.
            break;
    }
}

bool isBlackoutDetected(uint16_t brightness) {
    if (!brightnessBaselineInitialized) {
        brightnessBaseline = brightness;
        brightnessBaselineInitialized = true;
        return false;
    }

    if (brightnessBaseline > brightness &&
        brightnessBaseline - brightness >= BLACKOUT_DROP_THRESHOLD) {
        return true;
    }

    // Follow stable lighting slowly, but never replace the baseline during a drop.
    brightnessBaseline = static_cast<uint16_t>(
        (static_cast<uint32_t>(brightnessBaseline) * 7 + brightness) / 8);
    return false;
}

void executeStateAction() {
    switch (currentState) {
        case SystemState::TemperatureEmergency:
            setServoAngle(SERVO_EMERGENCY_ANGLE);
            setBuzzer(false);
            break;

        case SystemState::MultiFault:
            setServoAngle(SERVO_NORMAL_ANGLE);
            setBuzzer(true);
            break;

        default:
            setServoAngle(SERVO_NORMAL_ANGLE);
            setBuzzer(false);
            break;
    }
}

void setServoAngle(int angle) {
    ventServo.write(angle);
}

void setBuzzer(bool enabled) {
    digitalWrite(BUZZER_PIN, enabled ? HIGH : LOW);
}

void writeUint16(uint16_t value) {
    Wire.write(static_cast<uint8_t>(value & 0xFF));
    Wire.write(static_cast<uint8_t>((value >> 8) & 0xFF));
}

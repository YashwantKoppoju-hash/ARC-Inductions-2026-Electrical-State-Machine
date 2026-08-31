#include <Arduino.h>

#include <Protocol.h>
#include <StateLogic.h>

using Logic::ControllerInput;
using Logic::SensorData;
using Protocol::ControllerCommand;
using Protocol::SystemState;

constexpr float TEMPERATURE_THRESHOLD = 45.0f;
constexpr uint16_t GAS_ALERT_THRESHOLD = 180;
constexpr uint16_t GAS_CLEAR_THRESHOLD = 130;
constexpr uint16_t BLACKOUT_THRESHOLD = 200;

uint16_t baseline = 0;
bool baselineInitialized = false;
uint16_t failures = 0;

void expect(bool condition, const char* testName) {
    if (condition) {
        Serial.print("PASS: ");
        Serial.println(testName);
    } else {
        Serial.print("FAIL: ");
        Serial.println(testName);
        ++failures;
    }
}

SystemState decide(SystemState currentState,
                   uint16_t brightness,
                   uint16_t gasPpm,
                   float temperature,
                   bool activation = false) {
    SensorData sensorData;
    sensorData.brightness = brightness;
    sensorData.gasPpm = gasPpm;
    sensorData.temperature = temperature;

    ControllerInput input;
    input.activationConfirmed = activation;
    input.manualReset = false;
    return Logic::decideNextState(
        currentState, sensorData, input, baseline, baselineInitialized,
        TEMPERATURE_THRESHOLD, GAS_ALERT_THRESHOLD, GAS_CLEAR_THRESHOLD,
        BLACKOUT_THRESHOLD);
}

void setup() {
    Serial.begin(9600);

    expect(static_cast<uint8_t>(SystemState::Standby) == 0,
           "state code Standby");
    expect(static_cast<uint8_t>(SystemState::TemperatureEmergency) == 4,
           "state code TemperatureEmergency");
    expect(static_cast<uint8_t>(ControllerCommand::Activate) == 1,
           "command code Activate");
    expect(static_cast<uint8_t>(ControllerCommand::ResetTemperatureEmergency) == 2,
           "command code ResetTemperatureEmergency");

    baseline = 0;
    baselineInitialized = false;
    expect(decide(SystemState::Standby, 500, 0, 25.0f) == SystemState::Standby,
           "standby remains inactive without activation");
    expect(decide(SystemState::Standby, 500, 0, 25.0f, true) ==
               SystemState::ActiveMonitoring,
           "activation enters active monitoring");

    baseline = 500;
    baselineInitialized = true;
    expect(decide(SystemState::ActiveMonitoring, 500, 181, 25.0f) ==
               SystemState::GasAlert,
           "gas threshold enters gas alert");

    baseline = 500;
    baselineInitialized = true;
    expect(decide(SystemState::GasAlert, 500, 150, 25.0f) ==
               SystemState::GasAlert,
           "gas hysteresis holds gas alert above clear threshold");
    expect(decide(SystemState::GasAlert, 500, 129, 25.0f) ==
               SystemState::ActiveMonitoring,
           "gas hysteresis clears below clear threshold");

    baseline = 1000;
    baselineInitialized = true;
    expect(decide(SystemState::ActiveMonitoring, 700, 0, 25.0f) ==
               SystemState::BlackoutAlert,
           "brightness drop enters blackout alert");

    baseline = 1000;
    baselineInitialized = true;
    expect(decide(SystemState::ActiveMonitoring, 700, 181, 25.0f) ==
               SystemState::MultiFault,
           "gas and blackout enter multi-fault");

    baseline = 1000;
    baselineInitialized = true;
    expect(decide(SystemState::MultiFault, 700, 181, 46.0f) ==
               SystemState::TemperatureEmergency,
           "temperature emergency has highest priority");

    baseline = 1000;
    baselineInitialized = true;
    expect(decide(SystemState::TemperatureEmergency, 1000, 0, 25.0f) ==
               SystemState::TemperatureEmergency,
           "temperature emergency remains latched");

    Serial.print("Failures: ");
    Serial.println(failures);
}

void loop() {}

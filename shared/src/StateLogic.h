#pragma once

#include <stdint.h>

#include <Protocol.h>

namespace Logic {

struct SensorData {
    uint16_t brightness = 0;
    uint16_t gasPpm = 0;
    float temperature = 0.0f;
};

struct ControllerInput {
    bool activationConfirmed = false;
    bool manualReset = false;
};

inline bool isBlackoutDetected(uint16_t brightness,
                               uint16_t& brightnessBaseline,
                               bool& baselineInitialized,
                               uint16_t blackoutDropThreshold) {
    if (!baselineInitialized) {
        brightnessBaseline = brightness;
        baselineInitialized = true;
        return false;
    }

    if (brightnessBaseline > brightness &&
        brightnessBaseline - brightness >= blackoutDropThreshold) {
        return true;
    }

    brightnessBaseline = static_cast<uint16_t>(
        (static_cast<uint32_t>(brightnessBaseline) * 7 + brightness) / 8);
    return false;
}

inline Protocol::SystemState decideNextState(
    Protocol::SystemState currentState,
    const SensorData& sensorData,
    const ControllerInput& controllerInput,
    uint16_t& brightnessBaseline,
    bool& baselineInitialized,
    float temperatureEmergencyThreshold,
    uint16_t gasAlertThreshold,
    uint16_t gasClearThreshold,
    uint16_t blackoutDropThreshold) {
    if (sensorData.temperature > temperatureEmergencyThreshold) {
        return Protocol::SystemState::TemperatureEmergency;
    }

    if (currentState == Protocol::SystemState::TemperatureEmergency) {
        return currentState;
    }

    if (currentState == Protocol::SystemState::Standby) {
        if (controllerInput.activationConfirmed) {
            return Protocol::SystemState::ActiveMonitoring;
        }
        return currentState;
    }

    const bool blackoutDetected = isBlackoutDetected(
        sensorData.brightness, brightnessBaseline, baselineInitialized,
        blackoutDropThreshold);
    const bool previouslyGasAlerted =
        currentState == Protocol::SystemState::GasAlert ||
        currentState == Protocol::SystemState::MultiFault;
    const uint16_t gasThreshold = previouslyGasAlerted
                                      ? gasClearThreshold
                                      : gasAlertThreshold;
    const bool gasAlert = sensorData.gasPpm > gasThreshold;

    if (gasAlert && blackoutDetected) {
        return Protocol::SystemState::MultiFault;
    }
    if (gasAlert) {
        return Protocol::SystemState::GasAlert;
    }
    if (blackoutDetected) {
        return Protocol::SystemState::BlackoutAlert;
    }
    return Protocol::SystemState::ActiveMonitoring;
}

}  // namespace Logic

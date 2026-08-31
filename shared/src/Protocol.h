#pragma once

#include <stdint.h>

namespace Protocol {

constexpr uint8_t SlaveAddress = 0x08;
constexpr uint8_t PollRequest = 0x01;
constexpr uint8_t PollPacketSize = 8;

enum class SystemState : uint8_t {
    Standby = 0,
    ActiveMonitoring = 1,
    GasAlert = 2,
    BlackoutAlert = 3,
    TemperatureEmergency = 4,
    MultiFault = 5
};

enum class ControllerCommand : uint8_t {
    None = 0,
    Activate = 1,
    ResetTemperatureEmergency = 2,
    ToggleDisplay = 3
};

}  // namespace Protocol

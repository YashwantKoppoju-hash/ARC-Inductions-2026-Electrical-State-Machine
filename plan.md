# Code Planning Outline

This document contains the plan for designing the code before implementation.

## 1. Master Arduino Code

This is the first section we will deal with: planning the code for the master Arduino.

The master will measure the physical quantities required by the project:

- Brightness
- Gas concentration in ppm
- Temperature

These readings will be represented using a `SensorData` structure rather than separate global variables:

```cpp
struct SensorData {
    int brightness;
    int gasPpm;
    float temperature;
};
```

The master-side functions and state-machine logic will operate on this sensor-data structure.

### Preliminary Master Functions

- `getTemperature()`
- `getLux()`
- `getGasPpm()`

Sensor readings will be stored in `SensorData`. Commands received from the slave will be represented separately using a controller-input structure, for example:

```cpp
struct ControllerInput {
    bool activationConfirmed;
    bool manualReset;
};
```

The controller state will use an enum rather than unexplained integer values:

```cpp
enum class SystemState {
    Standby,
    ActiveMonitoring,
    GasAlert,
    BlackoutAlert,
    TemperatureEmergency,
    MultiFault
};
```

A main state-decision function will use the current sensor readings and controller input to determine state transitions. Temperature emergency conditions will have the highest priority.

## 2. Slave Arduino Code

The second section will deal with planning the code for the slave Arduino, including its handling of the IR sensor/remote and user commands.

## 3. IR Sensor Parsing and LCD Connection

The third section will deal with parsing IR sensor information and managing the I2C connection between the slave Arduino and the LCD screen.

## 4. Master-Slave I2C Communication

The fourth section will deal with managing the I2C connection between the two Arduino boards, including the specific communication functions required.

The slave will not communicate spontaneously. The master will initiate communication by sending a command-list request. On receiving that request, the slave will send the pending command list or queued controller inputs to the master.

The master-side communication plan will include functions to:

- Initiate a command-list request from the slave.
- Receive the slave's response.
- Parse the received command list.
- Update the master-side `ControllerInput` structure.
- Check whether the slave command queue contains pending commands.

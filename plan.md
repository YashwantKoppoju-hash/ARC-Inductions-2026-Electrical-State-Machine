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

The master output side contains a servo motor and a piezo buzzer. Separate output functions will operate them, with the state logic invoking the appropriate behavior:

- Temperature Emergency: move the servo to 180 degrees.
- Multi-Fault: activate the piezo buzzer continuously.

The main loop will skip sensor updates and normal state updates while in the latched `TemperatureEmergency` state. It will continue communication so that a manual reset can be received, and it will continue the emergency output action.

## 2. Slave Arduino Code

The second section will deal with planning the code for the slave Arduino. The slave has the LCD screen attachment and IR sensor/remote, and will handle IR input and LCD output.

## 3. IR Sensor Parsing and LCD Connection

The third section will deal with parsing IR sensor information and managing the I2C connection between the slave Arduino and the LCD screen.

The LCD may share the Arduino Uno's single hardware I2C bus with the master-slave connection. This is electrically valid when every device has a unique I2C address. The slave will logically manage the LCD, while the master will communicate with the slave using the agreed protocol and will not address the LCD directly.

## 4. Master-Slave I2C Communication

The fourth section will deal with managing the I2C connection between the two Arduino boards, including the specific communication functions required.

The slave will not communicate spontaneously. The master will initiate communication by sending a command-list request. On receiving that request, the slave will send only the numeric command required, rather than transmitting a full command structure or text message. This reduces communication overhead.

Commands will use fixed-width numeric codes, represented by an enum such as:

```cpp
enum class ControllerCommand : uint8_t {
    None = 0,
    Activate = 1,
    ResetTemperatureEmergency = 2,
    ToggleDisplay = 3
};
```

The slave will return `None` when its command queue is empty. A command will be removed from the queue after it has been sent to the master.

The shared I2C bus does not provide electrical privacy for the LCD; logical ownership will be enforced by the communication protocol. The bus arrangement and device addresses must be verified before implementation.

The master-side communication plan will include functions to:

- Initiate a command-list request from the slave.
- Receive the slave's response.
- Parse the received command list.
- Update the master-side `ControllerInput` structure.
- Check whether the slave command queue contains pending commands.

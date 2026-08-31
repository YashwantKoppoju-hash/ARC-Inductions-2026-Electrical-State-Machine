# Planning Conversation Notes

## Scope

The project is an Arduino master-slave sensor monitoring system. We are currently planning the master Arduino code before implementation.

## Schematic and TODO

- The schematic was saved as `schematic_v1.png`.
- The apparent A4/A5 wiring issue is assumed to be fixed for planning purposes.
- The LCD may share the Arduino Uno's single hardware I2C bus with the master-slave connection.
- Shared I2C is electrically valid when all devices have unique addresses.
- The slave will logically manage the LCD, while the master communicates with the slave using the agreed protocol.
- Electrical isolation of the LCD remains a future TODO if physical bus separation is later required.

## Planning Sections

The code plan is divided into four sections:

1. Planning the master Arduino code.
2. Planning the slave Arduino code.
3. Planning IR sensor information parsing and the I2C connection with the LCD.
4. Planning the I2C connection between the two boards, including the specific communication functions.

## Master Sensor Data

The master measures three physical quantities:

- Brightness
- Gas concentration in ppm
- Temperature

Rather than using separate global variables, the readings will be grouped in a structure:

```cpp
struct SensorData {
    int brightness;
    int gasPpm;
    float temperature;
};
```

Preliminary sensor functions:

```cpp
getTemperature()
getLux()
getGasPpm()
```

## Controller Input and State

Commands from the slave will be kept separate from physical sensor readings:

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

A main state-decision function will use sensor readings and controller input to determine transitions. Temperature emergency has the highest priority.

## State Update and Emergency Handling

The normal loop should update sensors, evaluate state transitions, handle communication, and execute the current state's action.

`TemperatureEmergency` is currently the only latched emergency state because it requires a manual reset. Gas Alert, Blackout Alert, and Multi-Fault recover automatically.

During `TemperatureEmergency`:

- Sensor values are not updated.
- Normal state-update logic is skipped.
- Communication remains active so a manual reset can be received.
- The emergency action continues to execute.

```cpp
void loop() {
    if (currentState == SystemState::TemperatureEmergency) {
        handleCommunication();
        executeStateAction();
        return;
    }

    updateSensors();
    updateState();
    handleCommunication();
    executeStateAction();
}
```

## Master-Slave Communication

The slave will not communicate spontaneously. The master initiates a command-list request, and the slave responds with pending commands or queued controller inputs.

The master-side communication plan includes functions to:

- Initiate a command-list request.
- Receive the slave response.
- Parse the command list.
- Update `ControllerInput`.
- Check whether the slave command queue contains pending commands.

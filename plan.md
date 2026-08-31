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

The slave will use the `IRremote` library to decode IR button signals into numeric command codes. Decoded commands will be placed into a FIFO queue so that commands are processed in the order in which they were received.

The slave-side queue will require functions for enqueuing, dequeuing, checking whether it is empty, and checking whether it is full. The slave will also maintain a display mode so a button press during `ActiveMonitoring` can switch the LCD between brightness and gas-percentage information.

The slave will update the LCD according to the current state received from the master. Normal commands will be ignored during an emergency state, but the manual temperature-emergency reset command must remain accepted.

## 3. IR Sensor Parsing and LCD Connection

The third section will deal with parsing IR sensor information and managing the I2C connection between the slave Arduino and the LCD screen.

The LCD may share the Arduino Uno's single hardware I2C bus with the master-slave connection. This is electrically valid when every device has a unique I2C address. The slave will logically manage the LCD, while the master will communicate with the slave using the agreed protocol and will not address the LCD directly.

## 4. Master-Slave I2C Communication

The fourth section will deal with managing the I2C connection between the two Arduino boards, including the specific communication functions required.

The slave will not communicate spontaneously. The master will poll the slave every 300 ms using a non-blocking timer. The master will initiate a command-list request and include its current state in the same communication cycle.

The communication transaction will be:

```text
Master -> Slave: [poll request code, current state code]
Slave  -> Master: [next command code]
```

On receiving the request, the slave will store the master's current state, update the LCD if necessary, check its FIFO command queue, and send only the numeric command required. This reduces communication overhead while keeping the slave synchronized with the master.

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
- Send the current state code with the polling request.
- Receive the slave's response.
- Parse the received command list.
- Update the master-side `ControllerInput` structure.
- Check whether the slave command queue contains pending commands.

The slave-side communication plan will include functions to:

- Receive and store the master's current state.
- Check the FIFO command queue.
- Return one command code or `None` when the queue is empty.
- Remove a command from the queue after it has been returned.

### Orchestration Decisions

The master is the authority for `SystemState`. The slave mirrors the state received from the master and uses it to control valid IR commands and LCD behavior. The slave does not independently change the system state.

The master will send the current sensor telemetry along with the state, because the slave needs the readings to display brightness and gas information on the LCD. The compact communication cycle will be:

```text
Master -> Slave: [poll request, state, brightness, gasPpm, temperature]
Slave  -> Master: [next command]
```

For the wire format, values will use fixed-width integer representations to keep the packet size predictable. Brightness and gas concentration will use unsigned 16-bit values, and temperature will use a signed 16-bit value representing tenths of a degree Celsius. The exact byte order will be fixed during implementation.

The master will poll the slave every 300 ms using `millis()` rather than blocking delays. Each poll will send the state and latest telemetry, then request one command byte. The slave will return `None` when its queue is empty.

State priority will be evaluated in this order:

1. `TemperatureEmergency`
2. `MultiFault`
3. `GasAlert` and `BlackoutAlert`
4. `ActiveMonitoring` and `Standby`

`TemperatureEmergency` is latched. Once entered, the master will not update sensors or run normal state transitions. It will continue polling for the manual reset command and will keep the emergency output active. After a valid reset, normal sensor updates resume on the following loop.

Gas Alert will use hysteresis: it is entered when gas concentration is above 180 units and cleared only when it falls below 130 units. Multi-Fault is active when the gas-alert and blackout conditions are both true, except when the temperature emergency condition is true.

Blackout detection will compare the current brightness against a stable brightness baseline. A blackout is detected when the absolute decrease reaches a calibrated `BLACKOUT_DROP_THRESHOLD`. The baseline will not be updated while a blackout is active, preventing the low-light value from becoming the new reference.

The slave will accept one FIFO command per physical IR button press. IR repeat frames and rapid duplicate frames will be ignored through repeat handling and debounce timing. Unknown IR codes will not be queued. If the queue is full, new non-reset commands will be discarded to preserve existing FIFO order; reset handling must have reserved capacity so that a temperature emergency can always be cleared.

`ToggleDisplay` is a slave-local command: when received from the IR remote during `ActiveMonitoring`, it changes the LCD display mode immediately and is not placed in the master command queue. Only commands that require master-side processing, such as activation or emergency reset, are returned to the master.

Normal commands, such as activation and display toggling, will be ignored by the slave while the mirrored state is `TemperatureEmergency`. The temperature-emergency reset command remains valid. The LCD will show state-specific messages for alert states and will show either brightness or gas telemetry in `ActiveMonitoring`, according to the selected display mode.

Invalid command bytes will be ignored by the master. If an I2C transaction fails, the master will retain its current state and retry during the next 300 ms polling cycle; the slave will retain queued commands until a command is successfully returned. The slave may continue showing the last valid telemetry and state until a later valid packet is received.

The shared I2C bus requires careful role management. The master Arduino is the I2C controller for master-slave polling. The slave Arduino receives those requests but also controls the LCD as an I2C controller. I2C callbacks on the slave will only copy received data or prepare a one-byte response; LCD transactions will run in the slave's normal loop, never inside `onReceive()` or `onRequest()` callbacks.

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

## 2. Slave Arduino Code

The second section will deal with planning the code for the slave Arduino, including its handling of the IR sensor/remote and user commands.

## 3. IR Sensor Parsing and LCD Connection

The third section will deal with parsing IR sensor information and managing the I2C connection between the slave Arduino and the LCD screen.

## 4. Master-Slave I2C Communication

The fourth section will deal with managing the I2C connection between the two Arduino boards, including the specific communication functions required.

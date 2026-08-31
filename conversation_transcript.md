# Verbatim Planning Conversation

This file records the planning conversation verbatim from the point at which master Arduino code planning began.

## User

Alright, now we will be working on writing the code.
How this will work is you will not be writing directly.
First, we will plan things out.
Even for that, you will not plan it out, I will.
Only suggest when I say so

## Assistant

Understood. I’ll wait for your plan and only make suggestions when you ask.

## User

Okay first thing, we will have global variables. 
The global variables would be the ones indicating the actual values of the physical quantities that we are trying to measure.
The variables would be 
1) Brightness
2) Gas ppm concentration.
3) Temperature
Any functions will directly work on these global variables.

## Assistant

Understood. The global variables will represent:

1. `brightness`
2. `gasPpm`
3. `temperature`

Functions will read from and operate directly on these variables.

## User

There aren't any other physical quantities thst qe are measuring right?

## Assistant

Correct. Based on the schematic and requirements, those are the only physical quantities measured:

- Brightness
- Gas concentration
- Temperature

The IR remote provides commands, while the servo, LCD, and buzzer are outputs—not measured quantities.

## User

The ir sensor is managed via the slave arduino board, so ig that is fine.
Right now, we are working on the master board plan.

## Assistant

Understood. We’re currently planning only the master Arduino, which measures brightness, gas concentration, and temperature. The slave Arduino handles the IR sensor and user commands.

## User

Wait, first of all, is using a global variable a good idea, does it stand by oops principles??

## Assistant

Global variables are acceptable for a small Arduino state machine, but they do not strongly follow OOP principles because they expose shared mutable state and weaken encapsulation.

A better design would encapsulate the sensor readings inside a `SensorData` class or a master-controller class, with functions updating and reading the values through methods.

For this project, a simple `struct SensorData` passed to the state-machine functions is also clean:

```cpp
struct SensorData {
    int brightness;
    int gasPpm;
    float temperature;
};
```

So: globals are workable, but encapsulation through a data structure or class is more maintainable and OOP-friendly.

## User

Alright, good idea.
Like the struct idea.
Alright, put this first thing we planned in the plan.md file we want.
Mention explicitly that the first section we will deal with planning of the code in the master arduino. The second one in the slave, the third ir sensor information parsing and i2c connection with the lcd screen. The forth on how we will manage i2c connection between the two boards, what specific functions.
Btw also add this fixing of wiring of the screen to the to do list, currently it is on the same bus as the master slave connection between the two arduinos

## Assistant

Created `plan.md` with the four planning sections and updated `TODO.txt` with the LCD/I²C bus wiring task.

## User

Alright, let us keep working on master arduino  code

## Assistant

Understood. We’ll continue planning the master Arduino code.

## User

Alright, now we will have various functions.
Preliminarily, we need
1) getTemperature()
2) getLux()
3) getGasppm()
Now, we need a variable that will indicate the state that the arduino controller is.
Then we would have a main state decision function.
For transitioning from state 0 to state 1, we need confirmed input from the arduino controller, so we can manage that by constructing another struct? Suggest.
Also, I have looked at other codes, and interestingly, they have assigned an enum for the state. Gimme some insight on that

## Assistant

Yes, an additional structure for controller input is a good idea. It separates physical sensor readings from commands received from the slave Arduino.

```cpp
struct ControllerInput {
    bool activationConfirmed;
    bool manualReset;
};
```

The state should use an enum:

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

## User

Alright, cool.
We also would have to have functions that checks the queue on the slave arduino.
We can't have the slave arduino communicate directly.
Rather, when the master arduino initiates a command list request, then the slave sends.
Put it in the plan.md. The thing that I talked right now in the master slave communication part and the one before in the main one.

## Assistant

Updated `plan.md` with the master-side data/state design and the request-response command queue protocol between the master and slave.

## User

I was also thinking, when there is actually no issue, going through the decision function unnecessarily is a waste of time. This is because of msot of the time the arduino won't have an issue, only sometimes it might.
Therefore, it might be better to do the following.
We have a smaller check function.
If any one of the global values exceed, we ..
Oh i just realised that we can keep the same routine state check function,  make that smaller while keeping the state, and any communications we can put in another function once the state has been decided..
Also, if the arduino is already in a emergency state like temp and requires input, it should skip the state_update internal workings, so we can add another if statement if that is the case

## Assistant

That structure makes sense:

```cpp
void loop() {
    updateSensors();

    if (!isManualRecoveryState(currentState)) {
        updateState();
    }

    handleCommunication();
    executeStateAction();
}
```

## User

It should not update sensor values either when in an emergency state.
Damn

## Assistant

Correct. In a latched emergency state, sensor updates and state updates should both be skipped. Only communication and reset handling remain active.

## User

In any state, same thing right?

## Assistant

Only in states that are explicitly latched or require manual recovery, such as `TemperatureEmergency`.

## User

What are the latched emergency state

## Assistant

Currently, only `TemperatureEmergency` is a latched emergency state.

## User

Okay, so better to keep it in the void loop, rather than make a seperate function for it.

## Assistant

Yes. Since the condition is simple and central to the entire control flow, keeping it directly in `loop()` is clearer.

## User

Alright, what are the other functions that we would need to think about before proceeding forward.
Also, put all of the conversation that we had until now in a .md file.

## Assistant

Before implementation, we should plan these additional master-side functions:

- `setup()`
- `updateSensors()`
- `updateState()`
- `handleCommunication()`
- `requestCommandList()`
- `receiveCommandList()`
- `parseCommandList()`
- `executeStateAction()`
- `handleTemperatureReset()`
- `setOutputsForState()`
- `resetControllerInput()`

I recorded the conversation so far in `conversation_notes.md`.

## User

No I want our exact conversation in the .md fike

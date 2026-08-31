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

## User

Alright good enough.
Now, instead of sending the whole command unnecesarilly, let us just have the arduino 's send out numbers to each other for the exact command they need.

## Assistant

That is a cleaner approach. The slave can send a compact numeric command code instead of a full command structure.

For example:

```cpp
enum class ControllerCommand : uint8_t {
    None = 0,
    Activate = 1,
    ResetTemperatureEmergency = 2,
    ToggleDisplay = 3
};
```

The communication flow becomes:

1. Master requests pending command.
2. Slave checks its queue.
3. Slave sends one command number.
4. Master interprets that number.
5. Slave removes the command from its queue only after sending it.

## User

The reason we are sending numbers to each other is to decrease communication overhead

## Assistant

Exactly. Sending compact numeric command codes reduces:

- Number of bytes transferred
- I²C transmission time
- Parsing complexity
- RAM usage
- Chance of communication errors

The command enum should therefore use fixed-width values such as `uint8_t`, ensuring every command occupies exactly one byte.

## User

Now on the master board output side.
There are two devices attached to it.
A servo motor, and a Piezo buzzer.
We will have to make function to operate those, and these functions would be invoked in the decision function.

## Assistant

Yes. The master should have separate output functions for each device:

```cpp
void controlServo();
void controlBuzzer();
```

The state-decision logic can invoke them according to the current state:

- `TemperatureEmergency`: move the servo to 180°.
- `MultiFault`: turn the buzzer on continuously.
- Other states: keep the relevant output inactive or in its normal position.

It may also be useful to define explicit operations:

```cpp
void setServoAngle(int angle);
void setBuzzer(bool enabled);
```

## User

Alright, now thinking of what is going on in the slave arduino.
It has a lcd screen attachement and an ir Sensor right.
Also, this is schematic v2.
Upadte the plan.md file with accordance to our current discussions.
Commit with the docs: commit message

## Assistant

I’ll save this image as `schematic_v2.png`, update `plan.md` with the current master/slave design and numeric-command protocol, then commit and push using a `docs:` message.

## User

Alright, is th emaster arduino decently defined?

## Assistant

Yes—the master Arduino is decently defined at the architectural level.

It currently has:

- `SensorData` for brightness, gas ppm, and temperature.
- `ControllerInput` for commands from the slave.
- `SystemState` enum for state management.
- Sensor functions: `getTemperature()`, `getLux()`, `getGasPpm()`.
- `updateSensors()`, `updateState()`, and communication handling.
- Servo and buzzer output control.
- A latched `TemperatureEmergency` path.
- Numeric command codes to reduce I²C overhead.

Before writing code, we still need to specify:

- Exact Arduino pin assignments.
- Sensor conversion/calibration formulas.
- Complete state transition conditions.
- Servo and buzzer behavior in every state.
- I²C request timing and command encoding.
- Whether sensor readings use `int`, `float`, or another type.

## User

Okay, we will do that exactly when we code.
For now.
We focus on teh slave arduino.
What kind of communication would we need between the IR and remote, ie, how would teh ir sensor ditinguish between ifferent commands and report accurately, is there a library for this?

## Assistant

Yes. For an Arduino IR receiver and remote, the usual approach is to use an IR decoding library, such as `IRremote`.

The remote does not send plain command names. Each button sends a modulated infrared signal containing a numeric code. The IR receiver detects that signal, and the library decodes it into information such as protocol, address, command code, and raw data.

## User

Alright, sure, then we just use this library then.
Also, how would lcd screen communication work?

## Assistant

The LCD will communicate over I²C through its backpack module. The slave Arduino can use `Wire.h` for I²C communication and `LiquidCrystal_I2C.h` for LCD control.

The LCD’s I²C address must be identified first, commonly `0x27` or `0x3F`. Since the LCD shares the bus with the two Arduinos, all devices must have unique addresses. The slave will logically own and update the LCD, while the master communicates with the slave using its separate address.

## User

Alright cool.
Now, we need a FIFO queue and a messaging protocol with the master.
We also have to detail the messaging protocol from teh master

## Assistant

A FIFO queue is appropriate on the slave because IR commands must be preserved in the order they are received.

The master is the I²C controller and the slave is the I²C target. The master sends a one-byte request code, and the slave returns one command byte or `None` if its queue is empty.

## User

Every 300 ms should do right?

## Assistant

Yes, polling every 300 ms is reasonable for this project. Use a non-blocking `millis()` timer rather than delays.

## User

Also, the slave needs internal functions in order to change the display information every time a button is pressed, while it is in state 1.
Also would need logic to prevent it from responding if there is a emergency state.

## Assistant

The slave needs local display-management logic and must know the master’s current state before deciding whether a button is valid. It should allow display toggling only in `ActiveMonitoring`, ignore normal commands during emergency states, and continue accepting `ResetTemperatureEmergency`.

## User

So the master should actually send the state over to teh slave during the communication polling itself.

## Assistant

Yes. During each communication cycle, the master should send its current state to the slave so the slave can enforce the correct command and display behavior.

The transaction can be:

```text
Master → Slave: [request code, current state]
Slave  → Master: [next command]
```

## User

Alright add this to the docs

## Assistant

Updated `plan.md` with the FIFO queue, numeric messaging protocol, 300 ms polling, state synchronization, and slave display/emergency logic.

## User

Alright anything else, have we missed something fundamental on a orchestration level?

## Assistant

The biggest missing orchestration detail is that the slave needs sensor readings to display them. The master must send telemetry along with the state:

```text
Master → Slave: [request, state, brightness, gasPpm, temperature]
Slave  → Master: [next command]
```

Other important items are state ownership and priority, gas hysteresis, blackout baseline detection, IR debouncing, FIFO overflow, invalid commands, I²C failures, and the slave’s dual I²C role.

## User

Alright, Define them then, and add them to the docs.

## Assistant

I’ll formalize these orchestration decisions in `plan.md`, including telemetry transfer, state authority and priority, thresholds, queue behavior, error handling, and the shared-I²C role constraints.

## User

Alright, perform a commit.
And start building on the basis of the spec.
Also, push whatever chat we have right now to the conversation transcript

## Assistant

I’ll first inspect the repository state and current planning documents, then I’ll update the transcript, commit the documentation changes, and add the initial code scaffolding based on the agreed specification.

I’m going to add an initial implementation scaffold: shared protocol definitions, a master sketch, and a slave sketch. I’ll keep hardware-specific pin assignments and IR button codes clearly configurable so they can be finalized during coding.

## Assistant

The initial scaffolding is in place. I’ll now append the newer discussion to the transcript, review the diff for obvious issues, and then commit and push the documentation plus code together.

## Assistant

I found one orchestration issue in the scaffold: `ToggleDisplay` is a slave-local action, so the slave should change the LCD immediately instead of queueing that command for the master. I’ll correct that and tighten the I²C packet handling before committing.

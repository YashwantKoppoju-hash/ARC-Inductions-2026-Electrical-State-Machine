#include <Arduino.h>
#include <IRremote.hpp>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#include <Protocol.h>

using Protocol::ControllerCommand;
using Protocol::SystemState;

// Confirm these values against the final schematic and remote during testing.
constexpr uint8_t IR_RECEIVER_PIN = 2;
constexpr uint8_t LCD_ADDRESS = 0x27;
constexpr uint8_t LCD_COLUMNS = 16;
constexpr uint8_t LCD_ROWS = 2;
constexpr uint8_t COMMAND_QUEUE_CAPACITY = 8;
constexpr unsigned long IR_DEBOUNCE_MS = 150;

// Replace these example NEC command values with the decoded remote values.
constexpr uint8_t IR_ACTIVATE_CODE = 0x45;
constexpr uint8_t IR_RESET_CODE = 0x46;
constexpr uint8_t IR_TOGGLE_DISPLAY_CODE = 0x47;

enum class DisplayMode : uint8_t {
    Brightness,
    GasPercentage
};

struct MasterTelemetry {
    uint16_t brightness = 0;
    uint16_t gasPpm = 0;
    int16_t temperatureTenths = 0;
};

LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);
volatile SystemState mirroredState = SystemState::Standby;
volatile MasterTelemetry receivedTelemetry;
volatile bool packetReceived = false;
SystemState latestState = SystemState::Standby;
MasterTelemetry latestTelemetry;

ControllerCommand commandQueue[COMMAND_QUEUE_CAPACITY];
uint8_t queueHead = 0;
uint8_t queueTail = 0;
uint8_t queueCount = 0;
DisplayMode displayMode = DisplayMode::Brightness;
SystemState lastDisplayedState = SystemState::Standby;
DisplayMode lastDisplayedMode = DisplayMode::Brightness;
uint16_t lastDisplayedBrightness = 0;
uint16_t lastDisplayedGasPpm = 0;
unsigned long lastIrCommandTime = 0;

void receivePollPacket(int byteCount);
void sendNextCommand();
void processIrInput();
void updateLocalStateAndDisplay();
void updateDisplay();
void toggleDisplayMode();
bool enqueueCommand(ControllerCommand command);
ControllerCommand dequeueCommand();
bool isQueueEmpty();
bool isQueueFull();
void purgeDisallowedCommands();
ControllerCommand decodeIrCommand(uint8_t irCode);
bool commandAllowedInState(ControllerCommand command, SystemState state);
void printStateMessage(SystemState state);
void printTelemetry();
void printLine(const char* text);

void setup() {
    Wire.begin(Protocol::SlaveAddress);
    Wire.onReceive(receivePollPacket);
    Wire.onRequest(sendNextCommand);

    IrReceiver.begin(IR_RECEIVER_PIN, ENABLE_LED_FEEDBACK);

    lcd.init();
    lcd.backlight();
    updateDisplay();
}

void loop() {
    processIrInput();
    updateLocalStateAndDisplay();
}

void receivePollPacket(int byteCount) {
    if (byteCount < Protocol::PollPacketSize) {
        while (Wire.available()) {
            Wire.read();
        }
        return;
    }

    const uint8_t requestCode = Wire.read();
    const uint8_t rawState = Wire.read();
    if (requestCode != Protocol::PollRequest ||
        rawState > static_cast<uint8_t>(SystemState::MultiFault)) {
        while (Wire.available()) {
            Wire.read();
        }
        return;
    }

    uint8_t packet[Protocol::PollPacketSize - 2];
    for (uint8_t i = 0; i < sizeof(packet) && Wire.available(); ++i) {
        packet[i] = Wire.read();
    }
    while (Wire.available()) {
        Wire.read();
    }

    mirroredState = static_cast<SystemState>(rawState);
    receivedTelemetry.brightness =
        static_cast<uint16_t>(packet[0]) |
        (static_cast<uint16_t>(packet[1]) << 8);
    receivedTelemetry.gasPpm =
        static_cast<uint16_t>(packet[2]) |
        (static_cast<uint16_t>(packet[3]) << 8);
    receivedTelemetry.temperatureTenths =
        static_cast<int16_t>(static_cast<uint16_t>(packet[4]) |
                             (static_cast<uint16_t>(packet[5]) << 8));
    packetReceived = true;

    // Callbacks only copy data and maintain queue state. LCD I2C operations
    // happen later in loop(), never inside this callback.
    purgeDisallowedCommands();
}

void sendNextCommand() {
    // Keep the request callback short: return one byte and nothing else.
    const ControllerCommand command = dequeueCommand();
    Wire.write(static_cast<uint8_t>(command));
}

void processIrInput() {
    if (!IrReceiver.decode()) {
        return;
    }

    const bool isRepeat =
        (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) != 0;
    const ControllerCommand command =
        decodeIrCommand(IrReceiver.decodedIRData.command);
    const unsigned long now = millis();

    if (!isRepeat && command != ControllerCommand::None &&
        now - lastIrCommandTime >= IR_DEBOUNCE_MS &&
        commandAllowedInState(command, latestState)) {
        // Display toggling is entirely local to the slave and should not
        // consume an I2C queue entry intended for the master.
        if (command == ControllerCommand::ToggleDisplay) {
            toggleDisplayMode();
            lastIrCommandTime = now;
        } else if (enqueueCommand(command)) {
            lastIrCommandTime = now;
        }
    }

    IrReceiver.resume();
}

void updateLocalStateAndDisplay() {
    if (!packetReceived) {
        return;
    }

    noInterrupts();
    latestState = mirroredState;
    latestTelemetry.brightness = receivedTelemetry.brightness;
    latestTelemetry.gasPpm = receivedTelemetry.gasPpm;
    latestTelemetry.temperatureTenths = receivedTelemetry.temperatureTenths;
    packetReceived = false;
    interrupts();

    if (latestState == SystemState::TemperatureEmergency) {
        purgeDisallowedCommands();
    }

    const bool stateChanged = latestState != lastDisplayedState;
    const bool modeChanged = displayMode != lastDisplayedMode;
    const bool telemetryChanged =
        latestTelemetry.brightness != lastDisplayedBrightness ||
        latestTelemetry.gasPpm != lastDisplayedGasPpm;

    if (stateChanged || modeChanged || telemetryChanged) {
        updateDisplay();
    }
}

void updateDisplay() {
    if (latestState == SystemState::ActiveMonitoring) {
        if (displayMode == DisplayMode::Brightness) {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Brightness:");
            lcd.setCursor(0, 1);
            lcd.print(latestTelemetry.brightness);
        } else {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Gas ppm:");
            lcd.setCursor(0, 1);
            lcd.print(latestTelemetry.gasPpm);
        }
    } else {
        lcd.clear();
        printStateMessage(latestState);
    }

    lastDisplayedState = mirroredState;
    lastDisplayedMode = displayMode;
    lastDisplayedBrightness = latestTelemetry.brightness;
    lastDisplayedGasPpm = latestTelemetry.gasPpm;
}

void toggleDisplayMode() {
    if (latestState != SystemState::ActiveMonitoring) {
        return;
    }

    displayMode = displayMode == DisplayMode::Brightness
                      ? DisplayMode::GasPercentage
                      : DisplayMode::Brightness;
    updateDisplay();
}

bool enqueueCommand(ControllerCommand command) {
    if (command == ControllerCommand::None) {
        return false;
    }

    // Reserve one queue slot for the emergency reset command.
    const bool isReset = command == ControllerCommand::ResetTemperatureEmergency;
    if (queueCount >= COMMAND_QUEUE_CAPACITY ||
        (!isReset && queueCount >= COMMAND_QUEUE_CAPACITY - 1)) {
        return false;
    }

    commandQueue[queueTail] = command;
    queueTail = (queueTail + 1) % COMMAND_QUEUE_CAPACITY;
    ++queueCount;
    return true;
}

ControllerCommand dequeueCommand() {
    if (isQueueEmpty()) {
        return ControllerCommand::None;
    }

    const ControllerCommand command = commandQueue[queueHead];
    queueHead = (queueHead + 1) % COMMAND_QUEUE_CAPACITY;
    --queueCount;
    return command;
}

bool isQueueEmpty() {
    return queueCount == 0;
}

bool isQueueFull() {
    return queueCount >= COMMAND_QUEUE_CAPACITY;
}

void purgeDisallowedCommands() {
    if (mirroredState != SystemState::TemperatureEmergency) {
        return;
    }

    ControllerCommand retained[COMMAND_QUEUE_CAPACITY];
    uint8_t retainedCount = 0;
    while (!isQueueEmpty()) {
        const ControllerCommand command = dequeueCommand();
        if (command == ControllerCommand::ResetTemperatureEmergency &&
            retainedCount < COMMAND_QUEUE_CAPACITY) {
            retained[retainedCount++] = command;
        }
    }

    for (uint8_t i = 0; i < retainedCount; ++i) {
        enqueueCommand(retained[i]);
    }
}

ControllerCommand decodeIrCommand(uint8_t irCode) {
    if (irCode == IR_ACTIVATE_CODE) {
        return ControllerCommand::Activate;
    }
    if (irCode == IR_RESET_CODE) {
        return ControllerCommand::ResetTemperatureEmergency;
    }
    if (irCode == IR_TOGGLE_DISPLAY_CODE) {
        return ControllerCommand::ToggleDisplay;
    }
    return ControllerCommand::None;
}

bool commandAllowedInState(ControllerCommand command, SystemState state) {
    if (command == ControllerCommand::ResetTemperatureEmergency) {
        return state == SystemState::TemperatureEmergency;
    }
    if (state == SystemState::TemperatureEmergency ||
        state == SystemState::GasAlert ||
        state == SystemState::BlackoutAlert ||
        state == SystemState::MultiFault) {
        return false;
    }
    if (command == ControllerCommand::Activate) {
        return state == SystemState::Standby;
    }
    return command == ControllerCommand::ToggleDisplay &&
           state == SystemState::ActiveMonitoring;
}

void printStateMessage(SystemState state) {
    switch (state) {
        case SystemState::Standby:
            printLine("AWAITING RITUAL");
            break;
        case SystemState::GasAlert:
            printLine("TOXIC PURGE");
            break;
        case SystemState::BlackoutAlert:
            printLine("NOCTIS PROTOCOL");
            break;
        case SystemState::TemperatureEmergency:
            printLine("COOKED");
            break;
        case SystemState::MultiFault:
            lcd.setCursor(0, 0);
            lcd.print("MULTIPLE PROB.");
            lcd.setCursor(0, 1);
            lcd.print("DETECTED");
            break;
        case SystemState::ActiveMonitoring:
            printTelemetry();
            break;
    }
}

void printTelemetry() {
    lcd.setCursor(0, 0);
    lcd.print("Monitoring");
}

void printLine(const char* text) {
    lcd.setCursor(0, 0);
    lcd.print(text);
}

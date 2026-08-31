$ErrorActionPreference = 'Stop'

Set-StrictMode -Version Latest

enum SystemState {
    Standby = 0
    ActiveMonitoring = 1
    GasAlert = 2
    BlackoutAlert = 3
    TemperatureEmergency = 4
    MultiFault = 5
}

enum ControllerCommand {
    None = 0
    Activate = 1
    ResetTemperatureEmergency = 2
    ToggleDisplay = 3
}

$script:failures = 0

function Assert-Equal($Actual, $Expected, [string]$Name) {
    if ($Actual -eq $Expected) {
        Write-Output "PASS: $Name"
    } else {
        Write-Output "FAIL: $Name (expected $Expected, got $Actual)"
        $script:failures++
    }
}

function Get-NextState {
    param(
        [SystemState]$CurrentState,
        [uint16]$Brightness,
        [uint16]$GasPpm,
        [float]$Temperature,
        [bool]$ActivationConfirmed,
        [ref]$BrightnessBaseline,
        [ref]$BaselineInitialized
    )

    if ($Temperature -gt 45.0) {
        return [SystemState]::TemperatureEmergency
    }

    if ($CurrentState -eq [SystemState]::TemperatureEmergency) {
        return $CurrentState
    }

    if ($CurrentState -eq [SystemState]::Standby) {
        if ($ActivationConfirmed) {
            return [SystemState]::ActiveMonitoring
        }
        return $CurrentState
    }

    if (-not $BaselineInitialized.Value) {
        $BrightnessBaseline.Value = $Brightness
        $BaselineInitialized.Value = $true
        $blackoutDetected = $false
    } elseif (($BrightnessBaseline.Value -gt $Brightness) -and
              (($BrightnessBaseline.Value - $Brightness) -ge 200)) {
        $blackoutDetected = $true
    } else {
        $BrightnessBaseline.Value = [uint16](([uint32]$BrightnessBaseline.Value * 7 + $Brightness) / 8)
        $blackoutDetected = $false
    }

    $previouslyGasAlerted =
        $CurrentState -eq [SystemState]::GasAlert -or
        $CurrentState -eq [SystemState]::MultiFault
    $gasThreshold = if ($previouslyGasAlerted) { 130 } else { 180 }
    $gasAlert = $GasPpm -gt $gasThreshold

    if ($gasAlert -and $blackoutDetected) {
        return [SystemState]::MultiFault
    }
    if ($gasAlert) {
        return [SystemState]::GasAlert
    }
    if ($blackoutDetected) {
        return [SystemState]::BlackoutAlert
    }
    return [SystemState]::ActiveMonitoring
}

function New-PollPacket {
    param(
        [SystemState]$State,
        [uint16]$Brightness,
        [uint16]$GasPpm,
        [int16]$TemperatureTenths
    )

    $bytes = [System.Collections.Generic.List[byte]]::new()
    [void]$bytes.Add(1)
    [void]$bytes.Add([byte]$State)
    [void]$bytes.Add([byte]($Brightness -band 0xFF))
    [void]$bytes.Add([byte](($Brightness -shr 8) -band 0xFF))
    [void]$bytes.Add([byte]($GasPpm -band 0xFF))
    [void]$bytes.Add([byte](($GasPpm -shr 8) -band 0xFF))
    [uint16]$encodedTemperature = [uint16]$TemperatureTenths
    [void]$bytes.Add([byte]($encodedTemperature -band 0xFF))
    [void]$bytes.Add([byte](($encodedTemperature -shr 8) -band 0xFF))
    return $bytes.ToArray()
}

Assert-Equal ([byte][SystemState]::Standby) 0 'state code Standby'
Assert-Equal ([byte][SystemState]::TemperatureEmergency) 4 'state code TemperatureEmergency'
Assert-Equal ([byte][ControllerCommand]::Activate) 1 'command code Activate'
Assert-Equal ([byte][ControllerCommand]::ResetTemperatureEmergency) 2 'command code ResetTemperatureEmergency'

[uint16]$baseline = 0
[bool]$baselineInitialized = $false

Assert-Equal (Get-NextState Standby 500 0 25.0 $false ([ref]$baseline) ([ref]$baselineInitialized)) `
    ([SystemState]::Standby) 'standby remains inactive without activation'
Assert-Equal (Get-NextState Standby 500 0 25.0 $true ([ref]$baseline) ([ref]$baselineInitialized)) `
    ([SystemState]::ActiveMonitoring) 'activation enters active monitoring'

$baseline = 500
$baselineInitialized = $true
Assert-Equal (Get-NextState ActiveMonitoring 500 181 25.0 $false ([ref]$baseline) ([ref]$baselineInitialized)) `
    ([SystemState]::GasAlert) 'gas threshold enters gas alert'

$baseline = 500
$baselineInitialized = $true
Assert-Equal (Get-NextState GasAlert 500 150 25.0 $false ([ref]$baseline) ([ref]$baselineInitialized)) `
    ([SystemState]::GasAlert) 'gas hysteresis holds gas alert'
Assert-Equal (Get-NextState GasAlert 500 129 25.0 $false ([ref]$baseline) ([ref]$baselineInitialized)) `
    ([SystemState]::ActiveMonitoring) 'gas hysteresis clears below 130'

$baseline = 1000
$baselineInitialized = $true
Assert-Equal (Get-NextState ActiveMonitoring 700 0 25.0 $false ([ref]$baseline) ([ref]$baselineInitialized)) `
    ([SystemState]::BlackoutAlert) 'brightness drop enters blackout alert'

$baseline = 1000
$baselineInitialized = $true
Assert-Equal (Get-NextState ActiveMonitoring 700 181 25.0 $false ([ref]$baseline) ([ref]$baselineInitialized)) `
    ([SystemState]::MultiFault) 'gas and blackout enter multi-fault'

$baseline = 1000
$baselineInitialized = $true
Assert-Equal (Get-NextState MultiFault 700 181 46.0 $false ([ref]$baseline) ([ref]$baselineInitialized)) `
    ([SystemState]::TemperatureEmergency) 'temperature emergency has highest priority'
Assert-Equal (Get-NextState TemperatureEmergency 1000 0 25.0 $false ([ref]$baseline) ([ref]$baselineInitialized)) `
    ([SystemState]::TemperatureEmergency) 'temperature emergency remains latched'

$packet = New-PollPacket ActiveMonitoring 512 181 253
Assert-Equal $packet.Length 8 'poll packet is eight bytes'
Assert-Equal $packet[0] 1 'poll packet request code'
Assert-Equal $packet[1] 1 'poll packet state code'
Assert-Equal $packet[2] 0 'brightness low byte'
Assert-Equal $packet[3] 2 'brightness high byte'
Assert-Equal $packet[4] 181 'gas low byte'
Assert-Equal $packet[5] 0 'gas high byte'

Write-Output "Failures: $script:failures"
if ($script:failures -ne 0) {
    exit 1
}

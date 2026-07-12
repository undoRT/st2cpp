// ============================================================================
// INTERFACES
// ============================================================================

INTERFACE I_Device
    METHOD Init : BOOL
        VAR_INPUT
            name : STRING;
        END_VAR
    END_METHOD

    METHOD GetName : STRING
    END_METHOD

    METHOD Process : BOOL
    END_METHOD

    METHOD GetStatus : INT
    END_METHOD
END_INTERFACE

INTERFACE I_Communicate
    METHOD Send : BOOL
        VAR_INPUT
            data : ARRAY[0..127] OF BYTE;
            len : INT;
        END_VAR
    END_METHOD

    METHOD Receive : BOOL
        VAR_OUTPUT
            data : ARRAY[0..127] OF BYTE;
            len : INT;
        END_VAR
    END_METHOD
END_INTERFACE

// ============================================================================
// ABSTRACT BASE CLASS
// ============================================================================

FUNCTION_BLOCK ABSTRACT FB_Device IMPLEMENTS I_Device
    VAR
        m_name : STRING;
        m_status : INT;
        m_errorCount : INT;
    END_VAR

    // Implementation of I_Device interface
    METHOD Init : BOOL
        VAR_INPUT
            name : STRING;
        END_VAR
        m_name := name;
        m_status := 0;
        m_errorCount := 0;
        Init := TRUE;
    END_METHOD

    METHOD GetName : STRING
        GetName := m_name;
    END_METHOD

    METHOD GetStatus : INT
        GetStatus := m_status;
    END_METHOD

    METHOD ABSTRACT Process : BOOL
    END_METHOD

    METHOD LogError
        VAR_INPUT
            errorCode : INT;
        END_VAR
        m_errorCount := m_errorCount + 1;
        m_status := errorCode;
    END_METHOD

    METHOD GetErrorCount : INT
        GetErrorCount := m_errorCount;
    END_METHOD
END_FUNCTION_BLOCK

// ============================================================================
// DERIVED CLASS - SENSOR
// ============================================================================

FUNCTION_BLOCK FB_Sensor EXTENDS FB_Device
    VAR
        m_value : REAL;
        m_threshold : REAL;
        m_sampleCount : INT;
    END_VAR

    METHOD OVERRIDE Process : BOOL
        Process := TRUE;
    END_METHOD

    METHOD SetThreshold
        VAR_INPUT
            threshold : REAL;
        END_VAR
        m_threshold := threshold;
    END_METHOD

    METHOD ReadValue : REAL
        ReadValue := m_value;
    END_METHOD

    METHOD Sample : BOOL
        VAR_INPUT
            newValue : REAL;
        END_VAR
        m_sampleCount := m_sampleCount + 1;
        m_value := newValue;

        IF m_value > m_threshold AND m_threshold > 0 THEN
            SUPER^.LogError(100);
            Sample := FALSE;
        ELSE
            Sample := TRUE;
        END_IF
    END_METHOD

    METHOD GetSampleCount : INT
        GetSampleCount := m_sampleCount;
    END_METHOD
END_FUNCTION_BLOCK

// ============================================================================
// DERIVED CLASS - ACTUATOR (FINAL - cannot be further inherited)
// ============================================================================

FUNCTION_BLOCK FINAL FB_Actuator EXTENDS FB_Device IMPLEMENTS I_Communicate
    VAR
        m_isRunning : BOOL;
        m_power : REAL;
        m_commandQueue : ARRAY[0..9] OF INT;
        m_queueHead : INT;
        m_queueTail : INT;
    END_VAR

    METHOD OVERRIDE Process : BOOL
        VAR
            i : INT;
        END_VAR
        Process := TRUE;
    END_METHOD

    METHOD Start : BOOL
        m_isRunning := TRUE;
        Start := TRUE;
    END_METHOD

    METHOD Stop : BOOL
        m_isRunning := FALSE;
        Stop := TRUE;
    END_METHOD

    METHOD SetPower
        VAR_INPUT
            powerPercent : REAL;
        END_VAR
        m_power := powerPercent;
    END_METHOD

    METHOD OVERRIDE LogError
        VAR_INPUT
            errorCode : INT;
        END_VAR
        // Override of LogError method
        SUPER^.LogError(errorCode);
        m_isRunning := FALSE;
    END_METHOD

    // Implementation of I_Communicate
    METHOD Send : BOOL
        VAR_INPUT
            data : ARRAY[0..127] OF BYTE;
            len : INT;
        END_VAR
        // Simulated command send
        Send := TRUE;
    END_METHOD

    METHOD Receive : BOOL
        VAR_OUTPUT
            data : ARRAY[0..127] OF BYTE;
            len : INT;
        END_VAR
        Receive := TRUE;
    END_METHOD

    METHOD QueueCommand
        VAR_INPUT
            cmd : INT;
        END_VAR
        m_commandQueue[m_queueTail] := cmd;
        m_queueTail := (m_queueTail + 1) MOD 10;
    END_METHOD

    METHOD ProcessQueue : BOOL
        VAR
            cmd : INT;
        END_VAR
        IF m_queueHead <> m_queueTail THEN
            cmd := m_commandQueue[m_queueHead];
            m_queueHead := (m_queueHead + 1) MOD 10;
            ProcessQueue := TRUE;
        ELSE
            ProcessQueue := FALSE;
        END_IF
    END_METHOD
END_FUNCTION_BLOCK

// ============================================================================
// DERIVED CLASS - TEMPERATURE SENSOR (SPECIALIZATION)
// ============================================================================

FUNCTION_BLOCK FB_TempSensor EXTENDS FB_Sensor
    VAR
        m_tempCelsius : REAL;
        m_offset : REAL;
    END_VAR

    METHOD OVERRIDE Process : BOOL
        VAR
            converted : REAL;
        END_VAR

        // Call base class Process method
        SUPER^.Process();

        // Convert Celsius to normalized value
        converted := (m_tempCelsius + m_offset) / 100.0;

        // Call base class Sample method (FB_Sensor)
        SUPER^.Sample(converted);

        Process := TRUE;
    END_METHOD

    METHOD SetTemperature
        VAR_INPUT
            celsius : REAL;
        END_VAR
        m_tempCelsius := celsius;
        m_value := celsius;  // m_value is defined in FB_Sensor
    END_METHOD

    METHOD SetOffset
        VAR_INPUT
            offset : REAL;
        END_VAR
        m_offset := offset;
    END_METHOD

    METHOD GetCelsius : REAL
        GetCelsius := m_tempCelsius + m_offset;
    END_METHOD
END_FUNCTION_BLOCK

// ============================================================================
// MAIN PROGRAM FOR TESTING
// ============================================================================

PROGRAM Main
    VAR
        // Direct instantiation
        sensor : FB_Sensor;
        actuator : FB_Actuator;
        tempSensor : FB_TempSensor;

        // Polymorphic array
        i : INT;

        // Test variables
        result : BOOL;
        name : STRING;
        status : INT;
        value : REAL;
        data : ARRAY[0..127] OF BYTE;
    END_VAR

    // ========================================================================
    // SENSOR TEST
    // ========================================================================

    // Initialization
    sensor.Init("TemperatureSensor");
    sensor.SetThreshold(50.0);

    // Method usage
    FOR i := 0 TO 9 DO
        sensor.Sample(i * 10.0);  // 0, 10, 20, 30, 40, 50...
    END_FOR

    status := sensor.GetStatus();
    value := sensor.ReadValue();
    name := sensor.GetName();
    result := sensor.Process();

    // ========================================================================
    // ACTUATOR TEST
    // ========================================================================

    actuator.Init("MotorController");
    actuator.Start();
    actuator.SetPower(75.0);
    actuator.QueueCommand(100);
    actuator.QueueCommand(200);
    result := actuator.ProcessQueue();

    // ========================================================================
    // TEMP SENSOR TEST (inheritance chain: FB_TempSensor -> FB_Sensor -> FB_Device)
    // ========================================================================

    tempSensor.Init("OvenSensor");
    tempSensor.SetThreshold(80.0);
    tempSensor.SetOffset(2.5);
    tempSensor.SetTemperature(75.0);

    result := tempSensor.Process();
    value := tempSensor.ReadValue();
    value := tempSensor.GetCelsius();
    status := tempSensor.GetErrorCount();

    // ========================================================================
    // SUPER^ TEST
    // ========================================================================

    // In tempSensor, Process calls SUPER^.Process() and SUPER^.Sample()
    // This verifies base-class call chaining

    // ========================================================================
    // FINAL CLASS TEST (FB_Actuator cannot be inherited)
    // ========================================================================

    // The following would cause a compile error:
    // FUNCTION_BLOCK FB_MyActuator EXTENDS FB_Actuator
    // END_FUNCTION_BLOCK
END_PROGRAM
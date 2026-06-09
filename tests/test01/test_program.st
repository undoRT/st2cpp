(*
* test_program.st
* Comprehensive test program for st2cpp generator
* Tests: FUNCTION_BLOCK with methods, FUNCTION calls, GVLs, ENUMs, STRUCTs, ARRAYs
*)

(* ============================================================================
*  ENUM definitions
* ============================================================================
*)

TYPE TestState :
    (
    IDLE,
    RUNNING,
    COMPLETED := 10,
    ERROR := 99
    );
END_TYPE

TYPE OperationMode :
    (
    MODE_AUTO,
    MODE_MANUAL,
    MODE_MAINTENANCE
    );
END_TYPE

(* ============================================================================
*  STRUCT definitions (simple, without FB)
* ============================================================================
*)
TYPE Point3D :
    STRUCT
        XY : Point2D;
        Z : INT := 0;
    END_STRUCT;
END_TYPE

TYPE Point2D :
    STRUCT
        X : INT := 0;
        Y : INT := 0;
    END_STRUCT;
END_TYPE

TYPE MathEmbedded :
    STRUCT
        MathProcessor_ : MathProcessor;
        Point3D_ : Point3D;
    END_STRUCT;
END_TYPE

(* ============================================================================
*  GLOBAL VARIABLES
* -============================================================================
*)

VAR_GLOBAL
    GlobalCounter : INT := 0;
    GlobalFlag : BOOL := FALSE;
    GlobalPosition : Point2D;
    GlobalArray : ARRAY [0..4] OF INT := [10, 20, 30, 40, 50];
END_VAR

(* ============================================================================
*  FUNCTIONS
* -============================================================================
*)

FUNCTION Add : INT
    VAR_INPUT
        A : INT;
        B : INT;
    END_VAR
    Add := A + B;
END_FUNCTION

FUNCTION Multiply : INT
    VAR_INPUT
        A : INT := 2;
        B : INT := 3;
    END_VAR
    VAR_OUTPUT
        Result : INT;
    END_VAR
    Result := A * B;
    Multiply := Result;
END_FUNCTION

FUNCTION ComplexCalc : REAL
    VAR_INPUT
        Input : INT;
        Multiplier : INT := 5;
    END_VAR
    VAR_OUTPUT
        Output : REAL;
        Status : BOOL;
    END_VAR
    VAR
        Temp : REAL;
    END_VAR
    Temp := Input * Multiplier;
    Output := Temp + 100.5;
    Status := Temp > 100.0;
    ComplexCalc := Output;
END_FUNCTION

(* ============================================================================
*  FUNCTION BLOCK with multiple methods
* -============================================================================
*)

FUNCTION_BLOCK MathProcessor
    VAR_INPUT
        Enable : BOOL;
        Factor : INT := 10;
    END_VAR
    VAR_OUTPUT
        Result : INT;
        IsBusy : BOOL;
        LastError : INT;
    END_VAR
    VAR
        InternalValue : INT := 0;
        OperationCount : INT := 0;
        ArrayBuffer : ARRAY [0..5] OF INT := [0, 1, 2, 3, 4, 5];
    END_VAR

    METHOD Reset : INT
        VAR_OUTPUT
            PreviousValue : INT;
        END_VAR
        PreviousValue := InternalValue;
        InternalValue := 0;
        OperationCount := 0;
        LastError := 0;
        Reset := PreviousValue;
    END_METHOD

    METHOD Process : INT
        VAR_INPUT
            Value : INT;
            Mode : OperationMode := OperationMode.MODE_AUTO;
        END_VAR
        VAR_OUTPUT
            ProcessedValue : INT;
            IsValid : BOOL;
        END_VAR
        VAR
            Temp : INT;
        END_VAR

        IF NOT Enable THEN
            LastError := -1;
            ProcessedValue := InternalValue;
            IsValid := FALSE;
            Process := InternalValue;
            RETURN;
        END_IF;

        OperationCount := OperationCount + 1;

        CASE Mode OF
            OperationMode.MODE_AUTO :
            Temp := Value * Factor;
            OperationMode.MODE_MANUAL :
            Temp := Value + InternalValue;
            OperationMode.MODE_MAINTENANCE :
            Temp := Value - Factor;
        END_CASE;

        InternalValue := InternalValue + Temp;
        ProcessedValue := InternalValue;
        IsValid := TRUE;
        Process := InternalValue;
    END_METHOD

    METHOD GetStatistics : INT
        VAR_OUTPUT
            Count : INT;
            CurrentValue : INT;
            ArraySum : INT;
        END_VAR
        VAR
            I : INT;
        END_VAR
        Count := OperationCount;
        CurrentValue := InternalValue;
        ArraySum := 0;
        FOR I := 0 TO 5 DO
            ArraySum := ArraySum + ArrayBuffer[I];
        END_FOR;
        GetStatistics := ArraySum;
    END_METHOD

    METHOD AddToBuffer : BOOL
        VAR_INPUT
            Index : INT;
            Value : INT;
        END_VAR
        IF Index >= 0 AND Index <= 5 THEN
            ArrayBuffer[Index] := Value;
            AddToBuffer := TRUE;
        ELSE
            AddToBuffer := FALSE;
        END_IF;
    END_METHOD
END_FUNCTION_BLOCK

(* ============================================================================
*  FUNCTION BLOCK that uses another FB
* -============================================================================
*)

FUNCTION_BLOCK CompositeProcessor
    VAR_INPUT
        Enable : BOOL;
        InputValue : INT;
    END_VAR
    VAR_OUTPUT
        OutputValue : INT;
        StatusMessage : STRING;
    END_VAR
    VAR
        Math1 : MathProcessor;
        Math2 : MathProcessor;
        InternalState : TestState;
        CurrentPos : Point2D;
    END_VAR

    METHOD ProcessAll : INT
        VAR_OUTPUT
            Total : INT;
            Result1 : INT;
            Result2 : INT;
        END_VAR
        VAR
            Temp1 : INT;
            Temp2 : INT;
        END_VAR

        Math1.Enable := Enable;
        Math2.Enable := Enable;
        Math1.Factor := InputValue;
        Math2.Factor := InputValue * 2;

        Math1.Process(Value := InputValue, ProcessedValue => Temp1);
        Math2.Process(Value := InputValue * 2, ProcessedValue => Temp2);

        Result1 := Temp1;
        Result2 := Temp2;
        Total := Result1 + Result2;
        ProcessAll := Total;
    END_METHOD

    METHOD ResetAll : VOID
        Math1.Reset();
        Math2.Reset();
        InternalState := TestState.IDLE;
        CurrentPos.X := 0;
        CurrentPos.Y := 0;
    END_METHOD
END_FUNCTION_BLOCK

(* ============================================================================
*  PROGRAM Main
* -============================================================================
*)

PROGRAM MainTest
    VAR
        Processor : MathProcessor;
        Composite : CompositeProcessor;
        ResultValue : INT;
        Status : BOOL;
        CalcOutput : REAL;           // Per ComplexCalc Output
        CurrentVal : INT;            // Per GetStatistics CurrentValue
        ArraySumVal : INT;           // Per GetStatistics ArraySum
        TestArray : ARRAY [0..3] OF Point2D;
        CurrentMode : OperationMode;
        StateVar : TestState;
        ProcResult : INT;
        I : INT;
    END_VAR

    // Initialize
    Processor.Enable := TRUE;
    Processor.Factor := 5;
    Composite.Enable := TRUE;
    Composite.InputValue := 10;

    // Test method calls with various parameter styles
    Processor.Process(Value := 10, Mode := OperationMode.MODE_AUTO);
    Processor.Process(Value := 20);  // uses default MODE_AUTO
    Processor.Process(15, OperationMode.MODE_MANUAL);

    // Test function calls
    ResultValue := Add(25, 30);
    Multiply(Result := ResultValue);  // output binding
    ComplexCalc(Input := 100, Output => CalcOutput);  // CORRETTO

    // Test GetStatistics method
    Processor.GetStatistics(Count => ResultValue, CurrentValue => CurrentVal, ArraySum => ArraySumVal);  // CORRETTO

    // Test Reset method
    Processor.Reset(PreviousValue => ResultValue);

    // Test CompositeProcessor
    Composite.ProcessAll(Total => ProcResult);

    // Test global variables
    GlobalCounter := GlobalCounter + 1;
    GlobalPosition.X := GlobalPosition.X + 10;
    GlobalArray[2] := GlobalArray[2] + 5;

    // Test enum assignments
    StateVar := TestState.COMPLETED;
    CurrentMode := OperationMode.MODE_MAINTENANCE;

    // Test array of structs
    TestArray[0].X := 100;
    TestArray[0].Y := 200;
    TestArray[1].X := 300;
    TestArray[1].Y := 400;

    // Test conditionals
    IF Processor.Result > 100 THEN
        Status := TRUE;
    ELSE
        Status := FALSE;
    END_IF;

    // Test loop
    FOR I := 1 TO 10 DO
        ResultValue := ResultValue + I;
    END_FOR;

    // While loop with method call inside
    WHILE ResultValue < 1000 DO
        Processor.Process(Value := ResultValue);
        ResultValue := ResultValue + Processor.Result;
    END_WHILE;
END_PROGRAM
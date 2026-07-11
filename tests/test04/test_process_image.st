(*
* test_process_image.st
* Test file for Process Image functionality
* Tests: %I, %Q, %M addresses with different qualifiers
*)

TYPE TestEnum :
    (
    IDLE,
    RUNNING,
    STOPPED,
    ERROR
    );
END_TYPE

TYPE MotorStatus :
    STRUCT
        Running : BOOL;
        Speed : INT;
        Temp : REAL;
        Fault : BOOL;
    END_STRUCT;
END_TYPE

(* Global variables with AT addresses *)
VAR_GLOBAL
    StartButton AT %IX0.0 : BOOL := FALSE;
    StopButton AT %IX0.1 : BOOL := FALSE;
    Emergency AT %IX0.2 : BOOL := FALSE;

    MotorOutput AT %QX0.0 : BOOL := FALSE;
    AlarmLight AT %QX0.1 : BOOL := FALSE;
    StatusWord AT %QW2 : WORD := 16#0000;

    Counter AT %MW10 : INT := 0;
    Temperature AT %MW12 : INT := 25;
    Flags AT %MB20 : BYTE := 0;
    Status AT %ML22 : DWORD := 0;
END_VAR

(* Function block with process image access and methods *)
FUNCTION_BLOCK MotorController
    VAR_INPUT
        Enable : BOOL;
        Speed : INT;
    END_VAR

    VAR_OUTPUT
        Running : BOOL;
        Fault : BOOL;
    END_VAR

    VAR
        Timer : INT := 0;
    END_VAR

    METHOD CheckOverheat : BOOL
        VAR_INPUT
            Temp : INT;
        END_VAR
        VAR_OUTPUT
            Overheat : BOOL;
        END_VAR
        Overheat := Temp > 80;
        CheckOverheat := Overheat;
    END_METHOD
END_FUNCTION_BLOCK

(* Main program - NO methods allowed! *)
PROGRAM Main
    VAR
        Motor : MotorController;
        TempRead : INT;
        State : TestEnum := TestEnum.IDLE;
        Status : MotorStatus;
        CycleCounter : INT := 0;
        NewState : TestEnum := IDLE;
        Updated : BOOL := FALSE;
        Start : BOOL := FALSE;
        Stop : BOOL := FALSE;
        EStop : BOOL := FALSE;
        Increment : BOOL := FALSE;
        NewTemp : INT := 0;
    END_VAR

    (* Main program body *)
    (* Read inputs *)
    Start := StartButton;
    Stop := StopButton;
    EStop := Emergency;

    (* Check emergency *)
    IF Emergency THEN
        MotorOutput := FALSE;
        AlarmLight := TRUE;
        Flags := Flags OR 16#80;
    END_IF;

    (* Update outputs based on state *)
    IF NOT Emergency THEN
        IF State = TestEnum.RUNNING THEN
            MotorOutput := TRUE;
            AlarmLight := FALSE;
            StatusWord := 16#0001;
        ELSIF State = TestEnum.STOPPED THEN
            MotorOutput := FALSE;
            AlarmLight := TRUE;
            StatusWord := 16#0002;
        ELSE
            MotorOutput := FALSE;
            AlarmLight := FALSE;
            StatusWord := 16#0000;
        END_IF;
    END_IF;

    (* Process counter *)
    IF Increment THEN
        Counter := Counter + 1;
    END_IF;

    (* Update temperature *)
    Temperature := NewTemp;
    Status.Temp := INT_TO_REAL(NewTemp) / 10.0;

    (* Use the function block *)
    Motor(Enable := Start, Speed := 100);

    (* Update status *)
    Updated := TRUE;
END_PROGRAM

(* Another program with different AT address patterns - NO methods! *)
PROGRAM Diagnostic
    VAR
        ErrorCode AT %MW50 : INT := 0;
        ErrorTime AT %MD52 : DWORD := 0;
        ErrorFlag AT %MX54.0 : BOOL := FALSE;
        Status AT %MW60 : INT := 0;
        Code : INT := 0;
    END_VAR

    (* Set error *)
    ErrorCode := Code;
    ErrorTime := 0;
    ErrorFlag := TRUE;
    Status := 1;

    (* Clear error *)
    ErrorCode := 0;
    ErrorTime := 0;
    ErrorFlag := FALSE;
    Status := 0;
END_PROGRAM

(* Function that uses process image addresses *)
FUNCTION GetMotorSpeed : INT
    VAR_INPUT
        MotorID : INT;
    END_VAR
    VAR
        Speed AT %IW100 : INT := 0;
        Speed2 AT %IW102 : INT := 0;
    END_VAR
    (* Read speed from input addresses *)
    IF MotorID = 1 THEN
        GetMotorSpeed := Speed;
    ELSE
        GetMotorSpeed := Speed2;
    END_IF;
END_FUNCTION
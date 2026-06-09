(* Example: Math functions and pointer usage *)

VAR_GLOBAL
    MATH : INT := 10;
    PHYS : INT := 20;
    LANGUAGE : INT := 30;
    SETUP : INT := 40;
    LOCATION : INT := 50;
    data : ARRAY [7..16] OF INT := [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
    GBL_Accumulator : Accumulator;
END_VAR


TYPE structWithFb :
    STRUCT
        MemberName1 : INT := 10;
        StructTypeExample_ : StructTypeExample;
        GBL_Accumulator : Accumulator;
    END_STRUCT;
END_TYPE

TYPE MachineState : (
    IDLE,
    RUNNING,
    FAULTED := 10,
    MAINTENANCE
    );
END_TYPE

TYPE StructTypeExample2 :
    STRUCT
        MemberName1 : INT := 10;
        StructTypeExample_ : StructTypeExample;
    END_STRUCT;
END_TYPE

TYPE StructTypeExample :
    STRUCT
        MemberName1 : INT := 10;
        MemberName2 : INT := 20;
        MemberName3 : REAL := 3.14;
        machineState_ : MachineState := MachineState.IDLE;
        // ton_: TON;
    END_STRUCT;
END_TYPE


FUNCTION Dummy : REAL
    VAR_INPUT
        a : INT := 50;
        b : INT := 70;
    END_VAR
    VAR_OUTPUT
        c : INT := 10;
    END_VAR
END_FUNCTION

FUNCTION Add : INT
    VAR_INPUT
        a : INT;
        b : INT;
    END_VAR
    VAR_OUTPUT
        dataStr : ARRAY [0..9] OF StructTypeExample;
    END_VAR
    VAR
        macs : MachineState;
    END_VAR
    Add := a + b;
    macs := MachineState.RUNNING;
END_FUNCTION

FUNCTION fun1 : INT
    VAR_INPUT
        a : INT := 5;
        c : REAL;
        b : INT := 15;
    END_VAR
    VAR_OUTPUT
        result : INT := 10;
        result2 : REAL := 11.2;
        structTypeExample2_ : StructTypeExample2;
        dataOut : ARRAY [2..9] OF INT := [2, 3, 4, 5, 6, 7, 8, 9];
    END_VAR
    VAR_IN_OUT
        varInOut : INT;
        dataInOut : ARRAY [2..9] OF INT := [2, 3, 4, 5, 6, 7, 8, 9];
    END_VAR
    VAR
        temp : INT := 0;
        temp1 : INT;
    END_VAR
    temp := a + 10;
    result := temp;
END_FUNCTION

FUNCTION_BLOCK Accumulator
    VAR_INPUT
        value : INT := 10;
        reset : BOOL;
    END_VAR
    VAR_OUTPUT
        total : INT;
        overflow : BOOL;
        structTypeExample2_ : StructTypeExample2;
        data : ARRAY [0..9] OF INT := [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
        dataStr : ARRAY [0..9] OF StructTypeExample2;
    END_VAR
    VAR_IN_OUT
        varInOut : INT;
    END_VAR
    VAR
        pTotal : POINTER TO INT;
        limit : INT;
    END_VAR

    pTotal := ADR(total);
    limit := INT(32767);

    varInOut := varInOut;

    IF reset THEN
        total := 0;
        overflow := FALSE;
    ELSE
        IF (total + value) > limit THEN
            overflow := TRUE;
        ELSE
            total := total;
        END_IF;
    END_IF;
END_FUNCTION_BLOCK

FUNCTION_BLOCK MyCounter
    VAR_INPUT
        enable : BOOL;
        reset : BOOL;
    END_VAR
    VAR_OUTPUT
        count : INT;
    END_VAR
    VAR
        INTERNAL : INT;
    END_VAR

    METHOD GetValue : INT
        VAR_INPUT
            a : INT := 1;
            b : INT;
        END_VAR
        VAR_OUTPUT
            c : INT;
            d : INT := 2012;
        END_VAR
        GetValue := count;
    END_METHOD

    METHOD GetValue2 : INT
        VAR_INPUT
            a : INT;
            b : INT := 42;
        END_VAR
        VAR_OUTPUT
            c : INT;
            d : INT := 2012;
        END_VAR
        GetValue2 := count;
    END_METHOD

    METHOD Increment : INT
        VAR_INPUT
            step : INT := 1;
            step2 : INT := 1;
        END_VAR
        VAR_OUTPUT
            tryOut : INT := 10;
        END_VAR
        VAR_IN_OUT
            tryInOut : INT;
        END_VAR
        count := count + step;
        Increment := count;
    END_METHOD
END_FUNCTION_BLOCK

FUNCTION_BLOCK ArrayDemo
    VAR_INPUT
        a : INT := 100;
        b : INT := 101;
    END_VAR
    VAR_OUTPUT
        c : WORD := 10;
    END_VAR
    VAR
        data : ARRAY [0..9] OF INT := [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
        data1 : ARRAY [5..14] OF INT := [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
        i : INT;
        sum : INT;
    END_VAR

    sum := 0;
    FOR i := 0 TO 9 DO
        sum := sum + data[i];
    END_FOR;
END_FUNCTION_BLOCK

PROGRAM Main
    VAR
        acc : Accumulator;
        arrDem : ArrayDemo;
        run : BOOL;
        totalExt : INT;
        varInOutExt : INT;
        extFloat : REAL;
        structTypeExample2_ : StructTypeExample2;
        machineState_ : MachineState;
        counter : MYCOUNTER;
        data : ARRAY [2..9] OF INT := [2, 3, 4, 5, 6, 7, 8, 9];
        // ton_: TON;
    END_VAR

    // acc(10, FALSE);

    // ton := try;

    fun1(b := 10, c := 10, result => totalExt, varInOut := varInOutExt, dataInOut := data);
    // fun1(b := 10, c := 10, result => totalExt, varInOut := varInOutExt);

    // Add(10, 100);
    Dummy(b := 20);
    Dummy(30, 40);
    Dummy(30);

    CouNter.getValue();
    CouNter.getValue();
    CouNter.getValue2();
    CouNter.getValue();
    CouNter.getValue();
    CouNter.getValue2();
    counter.GetValue();
    counter.GetValue(10);

    arrDem(10);
    arrDem(a := 9);;
    // IF machineState_ = IDLE then
    //     totalExt := totalExt + 1;
    // END_IF

    counter.Increment(step2 := 10, step := 1, tryInOut := varInOutExt);
    counter.GetValue(10, 2);

    machineState_ := machineState_.IDLE;
    IF machineState_ = MachineState.IDLE THEN
        totalExt := totalExt + 1;
    END_IF

    IF machineState_ = structTypeExample2_.StructTypeExample_.machineState_ THEN
        totalExt := totalExt + 1;
    END_IF

    structTypeExample2_.StructTypeExample_.MemberName1 := 10;
    IF totalExt = structTypeExample2_.StructTypeExample_.MemberName1 THEN
        totalExt := totalExt + 1;
    END_IF

    IF totalExt = structTypeExample2_.MemberName1 THEN
        totalExt := totalExt + 1;
    END_IF

    WHILE run DO
        acc(value := 1, reset := FALSE, total => totalExt);
        // acc(10, false, totalExt, run, structTypeExample2_, varInOutExt);
        acc(value := 10, reset := FALSE, total => totalExt, overflow => run, structTypeExample2_ => structTypeExample2_, varInOut := varInOutExt);
        // acc(value := 10, false, total => totalExt, run, structTypeExample2_ => structTypeExample2_, varInOut:= varInOutExt); // mixed calls, should give error
        // acc(value := 10, total => totalExt, false, run, structTypeExample2_ => structTypeExample2_, varInOut:= varInOutExt); // mixed calls, should give error
        // acc(10, value := FALSE); // mixed calls, should give error
        IF acc.total > 100 THEN
            run := FALSE;
        END_IF;
    END_WHILE;
END_PROGRAM

(*
ST -> C++ TRANSPILER TORTURE TEST

Obiettivo:
- verificare tipi e literal
- verificare ENUM / STRUCT / alias
- verificare ARRAY complessi
- verificare inizializzazioni
- verificare espressioni
- verificare chiamate FUNCTION / FB
- verificare IF / CASE / FOR / WHILE / REPEAT
- verificare EXIT / CONTINUE / RETURN
- verificare LABEL / JMP
- verificare dot notation
- verificare casi di nesting complesso
*)


(*****************************************************************)
(* ENUM                                                          *)
(*****************************************************************)

TYPE E_State :
    (
    STATE_IDLE,
    STATE_STARTING := 5,
    STATE_RUNNING,
    STATE_ERROR := 100,
    STATE_STOPPING
    );
END_TYPE


TYPE E_Mode :
    (
    MODE_MANUAL := 0,
    MODE_AUTO := 10,
    MODE_SERVICE := 20
    );
END_TYPE


(*****************************************************************)
(* STRUCT                                                        *)
(*****************************************************************)

TYPE ST_Motor :
    STRUCT
        enabled : BOOL;
        state : E_State;
        position : LREAL;
        velocity : REAL;
        errorCode : UDINT;
        coordinates : ST_Vector3;
        history : ARRAY[-5..5] OF REAL;
    END_STRUCT
END_TYPE

TYPE ST_Vector3 :
    STRUCT
        x : REAL;
        y : REAL;
        z : REAL;
    END_STRUCT
END_TYPE


TYPE ST_Complex :
    STRUCT
        id : DINT;
        name : STRING;
        description : WSTRING;
        values : ARRAY[0..2, 0..3] OF INT;
        motor : ST_Motor;
    END_STRUCT
END_TYPE


(*****************************************************************)
(* TYPE ALIAS                                                    *)
(*****************************************************************)

TYPE T_Counter : DINT;
END_TYPE

TYPE T_Vector : ST_Vector3;
END_TYPE

TYPE T_Array : ARRAY[0..15] OF REAL;
END_TYPE


(*****************************************************************)
(* FUNCTION                                                      *)
(*****************************************************************)

FUNCTION ClampValue : REAL
    VAR_INPUT
        value : REAL;
        minValue : REAL;
        maxValue : REAL;
    END_VAR

    IF value < minValue THEN
        ClampValue := minValue;
    ELSIF value > maxValue THEN
        ClampValue := maxValue;
    ELSE
        ClampValue := value;
    END_IF;

END_FUNCTION


(*****************************************************************)
(* FUNCTION MULTIPLE INPUT / OUTPUT                              *)
(*****************************************************************)

FUNCTION ComplexCalculation : DINT
    VAR_INPUT
        A : DINT;
        B : DINT;
        C : DINT;
    END_VAR

    VAR_OUTPUT
        Sum : DINT;
        Difference : DINT;
    END_VAR

    VAR
        temp : DINT;
    END_VAR

    temp := (A + B) * C;

    Sum := A + B + C;
    Difference := A - B - C;

    ComplexCalculation := temp;

END_FUNCTION


(*****************************************************************)
(* FUNCTION BLOCK                                                *)
(*****************************************************************)

FUNCTION_BLOCK FB_TestMotor

    VAR_INPUT
        Enable : BOOL;
        TargetPosition : LREAL;
        MaxVelocity : REAL;
    END_VAR

    VAR_OUTPUT
        Done : BOOL;
        Error : BOOL;
        CurrentPosition : LREAL;
    END_VAR

    VAR
        internalCounter : DINT := 0;
        previousPosition : LREAL := 0.0;
    END_VAR


    IF NOT Enable THEN

        Done := FALSE;
        Error := FALSE;

    ELSE

        internalCounter := internalCounter + 1;

        IF CurrentPosition < TargetPosition THEN

            CurrentPosition :=
            CurrentPosition + MaxVelocity;

        ELSIF CurrentPosition > TargetPosition THEN

            CurrentPosition :=
            CurrentPosition - MaxVelocity;

        ELSE

            Done := TRUE;

        END_IF;

    END_IF;

    previousPosition := CurrentPosition;

END_FUNCTION_BLOCK


(*****************************************************************)
(* PROGRAM                                                       *)
(*****************************************************************)

PROGRAM TranspilerTortureTest


    (*****************************************************************)
    (* BASIC TYPES                                                   *)
    (*****************************************************************)

    VAR

        (* Signed integer *)

        sintValue : SINT := -10;
        intValue : INT := -1000;
        dintValue : DINT := -100000;
        lintValue : LINT := -1000000;


        (* Unsigned integer *)

        usintValue : USINT := 10;
        uintValue : UINT := 1000;
        udintValue : UDINT := 100000;
        ulintValue : ULINT := 1000000;


        (* Bit strings *)

        byteValue : BYTE := 16#FF;
        wordValue : WORD := 16#ABCD;
        dwordValue : DWORD := 16#12345678;


        (* Boolean *)

        boolTrue : BOOL := TRUE;
        boolFalse : BOOL := FALSE;


        (* Floating point *)

        realPositive : REAL := 123.456;
        realNegative : REAL := -123.456;

        realExponent1 : REAL := 1.5E+3;
        realExponent2 : REAL := -2.5E+2;

        lrealValue : LREAL := -123456.789;


        (*****************************************************************)
        (* STRING                                                        *)
        (*****************************************************************)

        stringDefault : STRING := 'Hello';

        stringSquare : STRING :=
        'Square brackets';

        stringRound : STRING :=
        'Round brackets';

        shortString : STRING :=
        'This string is too long';

        wideString : WSTRING :=
        "Wide String";


        (*****************************************************************)
        (* TIME                                                          *)
        (*****************************************************************)

        time1 : TIME := T#100ms;

        time2 : TIME :=
        T#1h30m;

        time3 : TIME :=
        T#1h30m15s;

        time4 : TIME :=
        TIME#1D12H30M15S250MS;


        (*****************************************************************)
        (* ENUM                                                          *)
        (*****************************************************************)

        state : E_State :=
        E_State.STATE_IDLE;

        mode : E_Mode :=
        E_Mode.MODE_AUTO;


        (*****************************************************************)
        (* STRUCT                                                        *)
        (*****************************************************************)

        vector : ST_Vector3;

        motor : ST_Motor;

        complexData : ST_Complex;


        (*****************************************************************)
        (* STATIC ARRAYS                                                 *)
        (*****************************************************************)

        arrayPositive :
        ARRAY[0..9] OF INT;

        arrayNegative :
        ARRAY[-5..5] OF DINT;

        matrix :
        ARRAY[0..3, 0..4] OF REAL;

        cube :
        ARRAY[0..2, 0..3, 0..4] OF DINT;

        stringArray :
        ARRAY[0..4] OF STRING;

        structArray :
        ARRAY[0..7] OF ST_Vector3;


        (*****************************************************************)
        (* INITIALIZED ARRAYS                                            *)
        (*****************************************************************)

        initializedArray :
        ARRAY[0..4] OF INT :=
        [1, 2, 3, 4, 5];

        negativeArray :
        ARRAY[0..4] OF INT :=
        [-1, -2, -3, -4, -5];

        repeatedArray :
        ARRAY[0..3] OF INT;


        (*****************************************************************)
        (* ALIAS                                                         *)
        (*****************************************************************)

        counter : T_Counter := 0;

        aliasVector : T_Vector;

        aliasArray : T_Array;


        (*****************************************************************)
        (* LOOP SUPPORT                                                  *)
        (*****************************************************************)

        i : DINT;
        j : DINT;
        k : DINT;

        startIndex : DINT := -5;
        endIndex : DINT := 5;

        dynamicStep : DINT := 2;


        (*****************************************************************)
        (* EXPRESSIONS                                                   *)
        (*****************************************************************)

        result : DINT := 0;

        expressionInitialization :
        DINT := (10 + 20) * 3;

        logicalResult : BOOL;


        (*****************************************************************)
        (* FUNCTION OUTPUTS                                              *)
        (*****************************************************************)

        functionResult : DINT;

        outputSum : DINT;

        outputDifference : DINT;


        (*****************************************************************)
        (* FB                                                            *)
        (*****************************************************************)

        MotorController : FB_TestMotor;

        motorDone : BOOL;

        motorError : BOOL;

        actualPosition : LREAL;

    END_VAR



    (*****************************************************************)
    (* BASIC EXPRESSIONS                                             *)
    (*****************************************************************)

    result :=
    ((10 + 5) * 3)
    - (100 MOD 7);


    logicalResult :=
    (result >= 10)
    AND
    (result <= 100)
    AND NOT boolFalse;



    (*****************************************************************)
    (* STRUCT ACCESS                                                 *)
    (*****************************************************************)

    vector.x := 10.0;
    vector.y := -20.0;
    vector.z := 30.0;


    motor.enabled := TRUE;

    motor.state :=
    E_State.STATE_STARTING;

    motor.position := 100.0;

    motor.coordinates.x := 1.0;
    motor.coordinates.y := 2.0;
    motor.coordinates.z := 3.0;


    complexData.motor.coordinates.x :=
    motor.coordinates.x;



    (*****************************************************************)
    (* ARRAY ACCESS                                                  *)
    (*****************************************************************)

    arrayPositive[0] := 10;

    arrayNegative[-5] := -100;

    matrix[1, 2] := 1.5;

    cube[1, 2, 3] := 999;

    structArray[0].x := 100.0;
    structArray[0].y := 200.0;
    structArray[0].z := 300.0;

    (*****************************************************************)
    (* CHAINED ASSIGNMENT                                            *)
    (*****************************************************************)
    i := j := k := 0;


    (*****************************************************************)
    (* IF / ELSIF / ELSE                                             *)
    (*****************************************************************)

    IF result < 0 THEN

        state :=
        E_State.STATE_ERROR;

    ELSIF result = 0 THEN

        state :=
        E_State.STATE_IDLE;

    ELSIF result > 100 THEN

        state :=
        E_State.STATE_STOPPING;

    ELSE

        state :=
        E_State.STATE_RUNNING;

    END_IF;



    (*****************************************************************)
    (* NESTED IF                                                     *)
    (*****************************************************************)

    IF boolTrue THEN

        IF result > 0 THEN

            IF mode = E_Mode.MODE_AUTO THEN

                counter :=
                counter + 1;

            ELSE

                counter :=
                counter - 1;

            END_IF;

        END_IF;

    END_IF;



    (*****************************************************************)
    (* CASE                                                          *)
    (*****************************************************************)

    CASE state OF

        E_State.STATE_IDLE :

        result := 0;


        E_State.STATE_STARTING :

        result := 1;


        E_State.STATE_RUNNING :

        result := 2;


        E_State.STATE_ERROR :

        result := -1;


    ELSE

        result := -999;

    END_CASE;



    (*****************************************************************)
    (* CASE WITH IF                                                  *)
    (*****************************************************************)

    CASE mode OF

        E_Mode.MODE_MANUAL :

        IF boolTrue THEN
            result := result + 1;
        END_IF;


        E_Mode.MODE_AUTO :

        IF result > 100 THEN
            result := 100;
        ELSE
            result := result + 10;
        END_IF;


    ELSE

        result := -1;

    END_CASE;



    (*****************************************************************)
    (* FOR IMPLICIT +1                                               *)
    (*****************************************************************)

    FOR i := 0 TO 10 DO

        arrayPositive[i] := DINT_TO_INT(i * 10);

    END_FOR;



    (*****************************************************************)
    (* FOR NEGATIVE STEP                                             *)
    (*****************************************************************)

    FOR i := 10 TO 0 BY -1 DO

        result :=
        result + i;

    END_FOR;



    (*****************************************************************)
    (* FOR POSITIVE STEP                                             *)
    (*****************************************************************)

    FOR i := 0 TO 10 BY 2 DO

        result :=
        result + i;

    END_FOR;



    (*****************************************************************)
    (* FOR VARIABLE STEP                                             *)
    (*****************************************************************)

    FOR i :=
    startIndex
    TO
    endIndex
    BY
    dynamicStep
    DO

        result :=
        result + i;

    END_FOR;



    (*****************************************************************)
    (* NESTED FOR                                                    *)
    (*****************************************************************)

    FOR i := 0 TO 2 DO

        FOR j := 0 TO 3 DO

            FOR k := 0 TO 4 DO

                cube[i, j, k] :=
                i * 100 +
                j * 10 +
                k;

            END_FOR;

        END_FOR;

    END_FOR;



    (*****************************************************************)
    (* FOR + CONTINUE + EXIT                                         *)
    (*****************************************************************)

    FOR i := 0 TO 100 DO

        IF i >= 20 THEN
            EXIT;
        END_IF;

        result :=
        result + i;

    END_FOR;



    (*****************************************************************)
    (* WHILE                                                         *)
    (*****************************************************************)

    i := 0;

    WHILE i < 10 DO

        i := i + 1;

        IF i = 7 THEN
            EXIT;
        END_IF;

    END_WHILE;



    (*****************************************************************)
    (* NESTED WHILE                                                  *)
    (*****************************************************************)

    i := 0;

    WHILE i < 3 DO

        j := 0;

        WHILE j < 3 DO

            result :=
            result + i * j;

            j := j + 1;

        END_WHILE;

        i := i + 1;

    END_WHILE;



    (*****************************************************************)
    (* REPEAT UNTIL                                                  *)
    (*****************************************************************)

    i := 0;

    REPEAT

        i := i + 1;

        result :=
        result + i;

    UNTIL
    i >= 10
END_REPEAT;



(*****************************************************************)
(* REPEAT + IF + WHILE                                           *)
(*****************************************************************)

i := 0;

REPEAT

    IF i MOD 2 = 0 THEN

        j := 0;

        WHILE j < 3 DO

            result :=
            result + j;

            j := j + 1;

        END_WHILE;

    END_IF;

    i := i + 1;

UNTIL
i >= 10
END_REPEAT;



(*****************************************************************)
(* FUNCTION CALL                                                 *)
(*****************************************************************)

realPositive :=
ClampValue(
realNegative,
-100.0,
100.0
);



(*****************************************************************)
(* FUNCTION CALL WITH NAMED PARAMETERS                           *)
(*****************************************************************)

realPositive :=
ClampValue(
value := realPositive,
minValue := -50.0,
maxValue := 50.0
);



(*****************************************************************)
(* FUNCTION INPUT + OUTPUT PARAMETERS                            *)
(*****************************************************************)

functionResult :=
ComplexCalculation(
A := 10,
B := result,
C := 3,
Sum => outputSum,
Difference => outputDifference
);



(*****************************************************************)
(* FUNCTION BLOCK CALL                                           *)
(*****************************************************************)

MotorController(
Enable := TRUE,
TargetPosition := 1000.0,
MaxVelocity := 10.0,
Done => motorDone,
Error => motorError,
CurrentPosition => actualPosition
);



(*****************************************************************)
(* FUNCTION RESULT INSIDE EXPRESSION                             *)
(*****************************************************************)

realPositive :=
ClampValue(
vector.x + vector.y * vector.z,
-1000.0,
1000.0
);



(*****************************************************************)
(* COMPLEX BOOLEAN EXPRESSION                                    *)
(*****************************************************************)

logicalResult :=
(
(result > 10)
AND
(result < 1000)
)
OR
(
(state = E_State.STATE_RUNNING)
AND NOT motorError
);



(*****************************************************************)
(* DEEP COMBINATION                                              *)
(*****************************************************************)

FOR i := 0 TO 3 DO

    CASE state OF

        E_State.STATE_IDLE :

        IF i MOD 2 = 0 THEN

            result :=
            result + i;

        ELSE

            result :=
            result - i;

        END_IF;


        E_State.STATE_RUNNING :

        j := 0;

        WHILE j < 5 DO

            IF j = 3 THEN
                EXIT;
            END_IF;

            result :=
            result + j;

            j := j + 1;

        END_WHILE;


        E_State.STATE_ERROR :

        REPEAT

            result :=
            result - 1;

        UNTIL
        result <= 0
    END_REPEAT;


ELSE

    result :=
    result + 100;

END_CASE;

END_FOR;

END_PROGRAM

// ============================================================================
// ENUM Definitions
// ============================================================================

TYPE Color :
    (
        Red,
        Green := 5,
        Blue,
        Yellow := 10
    );
END_TYPE

TYPE Status :
    (
        Idle := 0,
        Running := 1,
        Stopped := 2,
        Error := 3
    );
END_TYPE

// ============================================================================
// STRUCT Definitions
// ============================================================================

TYPE Point3D :
    STRUCT
        x : INT;
        y : INT;
        z : INT;
    END_STRUCT
END_TYPE

TYPE MathEmbedded :
    STRUCT
        MathProcessor_ : MathProcessor;
        Point3D_ : Point3D;
    END_STRUCT
END_TYPE

TYPE struWithFB :
    STRUCT
        tryStruct : MathEmbedded;
    END_STRUCT
END_TYPE

TYPE Matrix3x3 :
    STRUCT
        data : ARRAY[1..3, 1..3] OF INT;  // 2D array inside struct
    END_STRUCT
END_TYPE

TYPE MultiArrayStruct :
    STRUCT
        arr1d : ARRAY[0..4] OF INT;
        arr2d : ARRAY[1..3, 0..2] OF INT;  // 3x3 matrix
        arr3d : ARRAY[0..1, 2..3, 0..2] OF REAL;  // 2x2x3 cube
    END_STRUCT
END_TYPE

// ============================================================================
// Function Block Definitions
// ============================================================================

FUNCTION_BLOCK MathProcessor
    VAR_INPUT
        a : INT;
        b : INT;
    END_VAR
    
    VAR_OUTPUT
        sum : INT;
        product : INT;
    END_VAR
    
    VAR
        temp : INT;
    END_VAR
    
    VAR_TEMP
        local_calc : INT;
    END_VAR
    
    METHOD Add : INT
        VAR_INPUT
            x : INT;
            y : INT;
        END_VAR
        Add := x + y;
    END_METHOD
    
    METHOD Multiply : INT
        VAR_INPUT
            x : INT;
            y : INT;
        END_VAR
        Multiply := x * y;
    END_METHOD
END_FUNCTION_BLOCK

// ============================================================================
// Global Variables with Arrays
// ============================================================================

VAR_GLOBAL
    // 1D arrays
    simple_array : ARRAY[0..5] OF INT := [10, 20, 30, 40, 50, 60];
    color_array : ARRAY[1..3] OF Color := [Color.Red, Color.Green, Color.Blue];
    empty_array : ARRAY[-2..2] OF REAL;
    
    // 2D arrays
    matrix_2x3 : ARRAY[0..1, 0..2] OF INT := [[1, 2, 3], [4, 5, 6]];
    identity_3x3 : ARRAY[1..3, 1..3] OF INT := [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
    
    // 3D arrays
    cube_2x2x2 : ARRAY[0..1, 0..1, 0..1] OF INT;
    tensor : ARRAY[1..2, 2..3, 0..1] OF REAL := [[[1.1, 2.2], [3.3, 4.4]], [[5.5, 6.6], [7.7, 8.8]]];
    
    // Arrays of structs
    points : ARRAY[0..2] OF Point3D;
    
    // Arrays of function blocks
    processors : ARRAY[1..2] OF MathProcessor;
    
    // Complex nested arrays
    nested_array : ARRAY[0..1, 0..2] OF ARRAY[0..1] OF INT;  // Array of arrays
END_VAR

// ============================================================================
// Functions with Array Parameters
// ============================================================================

FUNCTION SumArray : INT
    VAR_INPUT
        arr : ARRAY[0..4] OF INT;
    END_VAR
    VAR
        i : INT;
        result : INT;
    END_VAR
    
    result := 0;
    FOR i := 0 TO 4 DO
        result := result + arr[i];
    END_FOR
    SumArray := result;
END_FUNCTION

FUNCTION MatrixMultiply : INT
    VAR_INPUT
        A : ARRAY[1..2, 1..2] OF INT;
        B : ARRAY[1..2, 1..2] OF INT;
        row : INT;
        col : INT;
    END_VAR
    VAR
        k : INT;
        result : INT;
    END_VAR
    
    result := 0;
    FOR k := 1 TO 2 DO
        result := result + A[row][k] * B[k][col];
    END_FOR
    MatrixMultiply := result;
END_FUNCTION

// ============================================================================
// Main Program
// ============================================================================

PROGRAM Main
    VAR
        counter : INT;
        status : Status;
        color : Color;
        point : Point3D;
        result : INT;
        i, j, k : INT;
        mat : ARRAY[1..3, 1..3] OF INT;
        cube : ARRAY[0..1, 0..1, 0..1] OF INT;
        proc : MathProcessor;
        mstruct : MultiArrayStruct;
    END_VAR
    
    // Test 1D arrays
    counter := simple_array[0] + simple_array[5];
    simple_array[2] := 100;
    empty_array[0] := 3.14;
    
    // Test enum arrays
    color := color_array[2];
    IF color = Color.Green THEN
        status := Status.Running;
    END_IF
    
    // Test 2D arrays
    matrix_2x3[0][1] := 99;
    result := matrix_2x3[1][2];
    
    mat[1][1] := 10;
    mat[2][2] := 20;
    mat[3][3] := 30;
    
    // Test 3D arrays
    cube[0][0][0] := 1;
    cube[0][0][1] := 2;
    cube[1][1][1] := 8;
    
    // Nested loops for 3D array initialization
    FOR i := 0 TO 1 DO
        FOR j := 0 TO 1 DO
            FOR k := 0 TO 1 DO
                cube[i][j][k] := i * 100 + j * 10 + k;
            END_FOR
        END_FOR
    END_FOR
    
    // Test array initialization inside struct
    mstruct.arr1d[0] := 100;
    mstruct.arr2d[2][1] := 42;
    mstruct.arr3d[0][2][1] := 3.14159;
    
    // Test arrays of structs
    points[0].x := 10;
    points[1].y := 20;
    points[2].z := 30;
    
    // Test arrays of function blocks
    processors[1](a:=7, b:=4, sum => result);


    processors[2](a:=7, b:=4, product => result);
    
    // Test nested array access
    nested_array[0][1] := 42;
    
    // Complex array expressions
    simple_array[simple_array[0]] := 999;
    mat[counter][2] := simple_array[3];
    
    // Test multidimensional array in expression
    IF matrix_2x3[1][1] > 50 THEN
        status := Status.Error;
    ELSE
        status := Status.Stopped;
    END_IF
    
    // Test using MathProcessor methods
    proc( a:= mat[1][1], b := mat[2][2], sum => result);
    
    // Test array bounds (will be checked in debug mode)
    // This would cause error in debug: simple_array[10] := 999;
END_PROGRAM
(* Named type aliases (TYPE Name : <type>; END_TYPE).

   Covers the two supported alias forms beside the existing ENUM and STRUCT
   declarations:
     - scalar alias    : TYPE MyInt : INT; END_TYPE
     - array type alias: TYPE MyCounter : ARRAY[0..9] OF INT; END_TYPE
   including aliases of user structs and arrays of user structs.

   An alias is NOT a new type: it resolves to the canonical underlying type, so
   both the semantic analyzer (symbol table) and the code generator emit the
   canonical C++ type (int16_t, STArray<...>, ...) and never the alias spelling.

   Build:  st2cpp type_aliases.st
   Check:  B is `STArray<Int16, 0, 9>`, G is `STArray<STArray<Float, 1, 4>, 1, 3>`
           and SA is `STArray<SENSOR, 0, 5>` in the generated header. *)

TYPE MyInt : INT; END_TYPE
TYPE MyReal : REAL; END_TYPE
TYPE MyCounter : ARRAY[0..9] OF INT; END_TYPE
TYPE MyGrid : ARRAY[1..3, 1..4] OF REAL; END_TYPE

TYPE Sensor : STRUCT
    id : MyInt;
    value : MyReal;
END_STRUCT END_TYPE

TYPE SensorArr : ARRAY[0..5] OF Sensor; END_TYPE

PROGRAM TypeAliasesDemo
    VAR
        a  : MyInt := 42;
        b  : MyCounter;
        g  : MyGrid;
        sa : SensorArr;
        s  : Sensor;
    END_VAR

    b[0] := a;
    g[2, 3] := 1.5;
    sa[0].id := a;
    s.id := a;
END_PROGRAM
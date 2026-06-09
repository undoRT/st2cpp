(* Example: Counter2 function block *)
FUNCTION_BLOCK Counter2
    VAR_INPUT
        enable : BOOL;
        reset : BOOL;
    END_VAR
    VAR_OUTPUT
        count : INT;
    END_VAR

    IF reset THEN
        count := 0;
    ELSIF enable THEN
        count := count + 1;
    END_IF;
END_FUNCTION_BLOCK
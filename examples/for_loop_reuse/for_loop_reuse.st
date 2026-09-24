(* FOR loop reuse of the declared control variable.

   The control variable `i` is declared in VAR. IEC 61131-3 reads the control
   variable's FINAL value after the loop, so the generated C++ must reuse the
   existing variable and NOT shadow it with `for (auto I = ...)`. Without this
   fix the loop counter would be a fresh `auto` variable that disappears at
   scope exit, leaving the declared `I` untouched.

   Build:  st2cpp for_loop_reuse.st
   Check:  the generated code contains `for (I = 0; I <= 10; I += 2)` — no
           `auto` in front of the loop variable. *)

PROGRAM ForLoopReuse
    VAR
        i : INT;
        sum : INT := 0;
    END_VAR

    sum := 0;
    FOR i := 0 TO 10 BY 2 DO
        sum := sum + i;
    END_FOR

    (* An undeclared control variable is still legal and keeps `auto`. *)
    FOR j := 0 TO 4 DO
        sum := sum + j;
    END_FOR
END_PROGRAM
(*
 * report.st
 *
 * A third function block, so the example has three programs to distribute over
 * its tasks instead of two.
*)

FUNCTION_BLOCK FB_Report
VAR_INPUT
    bVerbose : BOOL;
END_VAR
VAR_OUTPUT
    nLines : INT;
END_VAR

IF bVerbose THEN
   nLines := 10;
ELSE
   nLines := 1;
END_IF
END_FUNCTION_BLOCK

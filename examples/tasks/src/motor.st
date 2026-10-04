FUNCTION_BLOCK FB_Motor
VAR_INPUT
   nSetpoint : INT;
   bSteady   : BOOL;
END_VAR
VAR_OUTPUT
   nActual : INT;
END_VAR

IF bSteady THEN
   nActual := nSetpoint;
ELSE
   nActual := 0;
END_IF
END_FUNCTION_BLOCK

(*
    Esempio Structured Text per STruC++.
    Mappiamo gli I/O fisici nel PROGRAM e li passiamo al Function Block.
*)

FUNCTION_BLOCK FB_MirrorIO
VAR_INPUT
    Enable  : BOOL;
    DiStart : BOOL;
    DiStop  : BOOL;
END_VAR
VAR_OUTPUT
    QMotor : BOOL;
    QLamp  : BOOL;
END_VAR
VAR
    I_intern AT %I* : INT;
    Latched : BOOL := FALSE;
END_VAR

(* Gestiamo un semplice latch start/stop. *)
IF NOT Enable THEN
    Latched := FALSE;
ELSIF DiStop THEN
    Latched := FALSE;
ELSIF DiStart THEN
    Latched := TRUE;
END_IF;

QMotor := Latched;
QLamp  := Enable AND Latched;

END_FUNCTION_BLOCK

PROGRAM Main
VAR
    (* Ingressi fisici: area %I, bit-level. *)
    I_Enable AT %IX0.0 : BOOL;
    I_Start  AT %IX0.1 : BOOL;
    I_Stop   AT %IX0.2 : BOOL;

    (* Uscite fisiche: area %Q, bit-level. *)
    Q_Motor  AT %QX0.0 : BOOL;
    Q_Lamp   AT %QX0.1 : BOOL;

    (* Istanza del nostro Function Block. *)
    MirrorIo : FB_MirrorIO;
END_VAR

(* Eseguiamo il FB e copiamo le uscite logiche sulle uscite fisiche. *)
MirrorIo(
    Enable  := I_Enable,
    DiStart := I_Start,
    DiStop  := I_Stop
);

Q_Motor := MirrorIo.QMotor;
Q_Lamp  := MirrorIo.QLamp;

END_PROGRAM
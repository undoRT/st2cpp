// ============================================================================
// st2cpp - Semantic Analysis Demo
// ============================================================================
// Questo esempio verifica le quattro feature dell'analisi semantica (Fase 7):
//
//   1. INTERFACCE          : INTERFACE I_Counter + IMPLEMENTS →
//                           emissione della interfaccia e risoluzione dei
//                           metodi astratti nel codice generato.
//   2. EREDITARIETA' FB    : EXTENDS FB_Counter + METHOD OVERRIDE +
//                           SUPER^.Tick(...) → semanticBaseForFb() risolve la
//                           base reale e genera la catena FB_DOUBLECOUNTER
//                           che include e usa FB_COUNTER.
//   3. INDIRIZZI AT        : GVLs piazzate su %MW / %QW / %QX →
//                           resolvedATAddresses / allocazione Process Image.
//   4. TIPI UTENTE         : ENUM E_State e STRUCT S_Report usati in
//                           programmi, FB e chiamate cross-tipo.
//   5. CASE-INVARIANT       : ST è case-insensitive → gli identificatori si
//                           risolvono a prescindere dalla maiuscola
//                           (es. `Name` dichiarato, usato come `name`).
//
// Generazione (verifica "0 errors, 0 warnings"):
//   st2cpp --strict --project-style examples/semantics/test_semantics.st \
//          --output-dir examples/semantics/generated
// ============================================================================

// ---------------------------------------------------------------------------
// Tipi utente
// ---------------------------------------------------------------------------

TYPE E_State :
    ( Idle, Running, Fault, Done );
END_TYPE

TYPE S_Report :
    STRUCT
        Count : INT;
        State : E_State;
    END_STRUCT
END_TYPE

// ---------------------------------------------------------------------------
// Interfaccia astratta: un contatore incrementabile
// ---------------------------------------------------------------------------

INTERFACE I_Counter
    METHOD Tick : INT
        VAR_INPUT
            Increment : INT;
        END_VAR
    END_METHOD

    METHOD GetCount : INT
    END_METHOD
END_INTERFACE

// ---------------------------------------------------------------------------
// Implementazione concreta dell'interfaccia (IMPLEMENTS)
// ---------------------------------------------------------------------------

FUNCTION_BLOCK FB_Counter IMPLEMENTS I_Counter
    VAR
        count : INT;
        StepMax : INT;
        Name : STRING;
    END_VAR

    // Implementazione della interfaccia I_Counter
    METHOD Tick : INT
        VAR_INPUT
            Increment : INT;
        END_VAR
        count := count + Increment;
        // Case-insensitive: "Name" dichiarato come `Name`
        nAmE := "tick";
        Tick := count;
    END_METHOD

    METHOD GetCount : INT
        GetCount := count;
    END_METHOD

    METHOD SetName
        VAR_INPUT
            N : STRING;
        END_VAR
        Name := N;
    END_METHOD

    METHOD GetMax : INT
        // Il limite per i figli (visibile via ereditarieta')
        GetMax := StepMax;
    END_METHOD
END_FUNCTION_BLOCK

// ---------------------------------------------------------------------------
// FB derivato: EXTENDS + OVERRIDE + SUPER^
//   Tick e' virtual nella base → la semantica risolve la base FB_COUNTER e
//   il method OVERRIDE e' dispatchato via virtual.
// ---------------------------------------------------------------------------

FUNCTION_BLOCK FB_DoubleCounter EXTENDS FB_Counter
    METHOD OVERRIDE Tick : INT
        VAR_INPUT
            Increment : INT;
        END_VAR
        count := count + Increment;    // campo ereditato dalla base
        SUPER^.Tick(Increment);        // chiamata alla implementazione base
        Tick := count;
    END_METHOD
END_FUNCTION_BLOCK

// ---------------------------------------------------------------------------
// GVLs con indirizzi AT (Process Image + memoria)
// ---------------------------------------------------------------------------

VAR_GLOBAL
    CycleCount AT %MW50 : INT := 0;
    MasterStatus AT %MW52 : INT := 0;
    AlarmLamp   AT %QX0.5 : BOOL := FALSE;
END_VAR

// ---------------------------------------------------------------------------
// Programma principale: usa tutte le feature sopra
// ---------------------------------------------------------------------------

PROGRAM Main
    VAR
        dc : FB_DoubleCounter;
        simple : FB_Counter;
        report : S_Report;
        i : INT;
        total : INT;
    END_VAR

    // Identita'
    simple.SetName("simple");
    dc.SetName("double");

    // 3 cicli: ogni Tick(2) aggiunge 4 (2 nel child + 2 via SUPER^)
    FOR i := 1 TO 3 DO
        total := dc.Tick(2);
    END_FOR

    // Report tipizzato con ENUM + STRUCT (E_State risolto dal TypeSystem)
    report.Count := dc.GetCount();
    IF report.Count = 12 THEN
        report.State := Done;
        MasterStatus := 1;
    ELSE
        report.State := Fault;
    END_IF

    CycleCount := report.Count;
    IF total > 0 THEN
        AlarmLamp := FALSE;
    END_IF
END_PROGRAM
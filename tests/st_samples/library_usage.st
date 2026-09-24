// ============================================================================
// Uso di simboli provenienti dalle librerie esterne (examplelib + corelib):
//   - struct Channel (esterna, member Value/Limits: Limits e' corelib.Range)
//   - enum State (esterna) con enumeratori qualificati / non qualificati
//   - function Clamp (esterna, 3 parametri IN, ultimi due con default)
//   - FB TON (esterno, IN/PT e uscite Q/ET)
//   - constant MAX_CHANNELS (esterna)
//   - global gSystemState (esterna)
// ============================================================================

PROGRAM Main
    VAR
        ch : Channel;
        st : State;
        ton : TON;
        x : REAL;
        n : INT;
        done : BOOL;
        elapsed : TIME;
    END_VAR

    ch.Value := 8.5;
    ch.Limits.Min := 0.0;
    ch.Limits.Max := 10.0;

    st := State.FAULT;
    IF st = State.RUNNING THEN
        st := IDLE;
    END_IF

    x := Clamp(1.5, 0.0, 10.0);

    ton(IN := TRUE, PT := T#1s, Q => done, ET => elapsed);

    n := MAX_CHANNELS;
    st := gSystemState;
END_PROGRAM
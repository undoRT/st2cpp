(* ============================================================================
 *  main.st
 *
 *  Programma di esempio che consuma simboli dichiarati da due librerie esterne
 *  caricate tramite descrittori JSON (libraries/core.json e libraries/io.json,
 *  dichiarate in project.json):
 *
 *    - struct  AnalogChannel (io)   campo Range di tipo core::Range (cross-library)
 *    - enum    AlarmClass    (io)   enumeratori qualificati / non qualificati
 *    - global  gAlarm        (io)
 *    - constant MAX_AI_CHANNELS (io)
 *    - function ScaleAnalog  (io)   freeFunction bound
 *    - function NormalizeRange (io) staticMethod bound
 *    - FB      Averager      (io)   IN/OUT come Ton-style, step via cppBinding
 *
 *  Nessuno di questi simboli è definito nel progetto: vengono risolti dal
 *  SemanticAnalyzer tramite il LibraryRegistry costruito da project.json.
 * ============================================================================ *)

PROGRAM Main
    VAR
        ch : AnalogChannel;
        alm : AlarmClass;
        avg : Averager;
        x : REAL;
        n : INT;
        good : BOOL;
    END_VAR

    ch.Value := 8.5;
    ch.Enabled := TRUE;
    ch.Range.Min := 0.0;
    ch.Range.Max := 100.0;

    alm := AlarmClass.WARNING;
    alm := gAlarm;

    x := ScaleAnalog(8.5, 0.0, 100.0);
    x := NormalizeRange(8.5);

    n := MAX_AI_CHANNELS;

    avg(Enable := TRUE, Sample := 8.5, Avg => x, Valid => good);
END_PROGRAM
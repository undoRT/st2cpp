(*
 * counters.st
 *
 * Global counters, incremented by the PROGRAM bodies. They exist so the example
 * can *show* how many times each program was invoked, which is the whole point
 * of the task configuration: the call counts are what tells you whether a task
 * really ran several programs, and whether a program shared between two tasks
 * was invoked once per task or only once in total.
 *
 * SimpleGVLs.hpp is what trace.cpp reads to print them.
*)

VAR_GLOBAL
    nMainCalls : DINT := 0;
    nControlCalls : DINT := 0;
    nReportCalls : DINT := 0;
END_VAR

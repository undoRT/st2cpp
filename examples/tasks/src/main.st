(*
 * main.st
 *
 * The three PROGRAM POUs this example schedules. They are deliberately trivial
 * bodies: the interesting part is *which task calls which of them, and how
 * often*, which is what tasks.json declares.
*)

PROGRAM MAIN
VAR
    fbDrives : FB_Motor;
END_VAR

nMainCalls := nMainCalls + 1;
fbDrives(nSetpoint := 100);
END_PROGRAM

PROGRAM Control
VAR
    fbValve : FB_Valve;
END_VAR

nControlCalls := nControlCalls + 1;
fbValve(bOpen := TRUE);
END_PROGRAM

PROGRAM Report
VAR
    fbReport : FB_Report;
END_VAR

nReportCalls := nReportCalls + 1;
fbReport(bVerbose := FALSE);
END_PROGRAM

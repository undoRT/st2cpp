# Example: PLC tasks and the undoPLC runtime

Turns Structured Text **plus** a task configuration into a running PLC: one
undoPLC Master, one Worker for the cyclic task, calling `run()` on the programs
the task lists.

Deliberately small: one Master plus one Worker is two isolated CPUs, the minimum
that can actually be started, so **this example runs on an ordinary development
machine**, not only on a real-time box.

```bash
./build.sh              # transpile, compile, link, then run until Ctrl+C
./build.sh --no-run     # stop after linking
```

## Files

| File | Role |
| --- | --- |
| `src/counters.st` | `VAR_GLOBAL` counters incremented by the PROGRAM bodies |
| `src/motor.st` | `FB_Motor` |
| `src/valve.st` | `FB_Valve` |
| `src/report.st` | `FB_Report` |
| `src/main.st` | the three `PROGRAM` POUs: `MAIN`, `Control`, `Report` |
| `tasks.json` | which task runs which programs, how fast, at which priority |
| `trace.cpp` | **demo only**, not generated: prints the counters once a second |
| `build.sh` | drives all of the above |

## The configuration

```json
{
  "schema_version": "1.0",
  "plc_name": "Demo",
  "tasks": [
    { "name": "cycle", "plc": "Line1", "cycle_ms": 10, "priority": 40,
      "programs": ["MAIN", "Control", "Report"] }
  ]
}
```

One task, three programs. st2cpp derives the Master from it:

```
PLC 'Line1': 1 task(s) -> Master cycle 10 ms, priority 41
```

Master priority is the highest task priority plus one, because undoPLC runs the
Master above every Worker it drives.

## What the generated runtime does

```
== what each Worker calls, in the declared order ==
  Line1_cycle            _program_MAIN.run();
  Line1_cycle            _program_CONTROL.run();
  Line1_cycle            _program_REPORT.run();

== each task owns its own program instances ==
  Line1_cycle
      - _program_MAIN;
      - _program_CONTROL;
      - _program_REPORT;
```

Order is the order declared in `tasks.json`. The Worker declares a **member per
program** and calls the program on that member, so a program called from two
different tasks would be instantiated once in each task — two independent
objects, as in TwinCAT, which is why they cannot corrupt each other's state.

## Running it, and what to expect

Starting needs **root** and the isolated CPUs (one per Master plus one per task,
so 2 here; any two idle cores do). Both are verified at startup, and the runtime
refuses to start rather than run unprotected, naming what is missing:

```
[ERROR] This PLC needs 2 isolated CPUs but only 1 are available.
        Check your GRUB isolcpus= configuration.
```

```
[ERROR] Cannot pin the isolated CPUs to their nominal frequency.
          Root is needed to write cpufreq, and without it the cycle
          jitter is not bounded, so the PLC is not started.
```

Two things need root, and both are load-bearing: `SCHED_FIFO` thread priorities,
and writing the cpufreq governor to pin the isolated cores to their nominal
frequency. The second is not cosmetic — an unpinned governor lets Turbo Boost and
frequency transitions move the cycle time, so the PLC would still *look* fine
while its jitter stopped being bounded. Failing loudly beats running quietly
wrong.

Then `trace.cpp` prints the rate of each program once a second. All three are in
the same task, on a 10 ms cycle, so all three must read 100/s:

```
[trace] last second: MAIN 100/s, Control 100/s, Report 100/s   (totals: 193, 193, 193)
```

Expect **100 or 101**, not exactly 100 every time. The observer sleeps a flat
second, but that window is not phase-aligned with the 10 ms cycle, so it catches
either 100 or 101 increments depending on where its boundaries fall. Seeing 101
on one sample out of a dozen is the window, not the PLC: over the whole run the
totals settle at 100/s. The giveaway that something is genuinely wrong is a
number that is *missing* or *halved* — that would mean the `programs` list did not
translate the way you expected, which is the whole point of measuring instead of
assuming.

Startup and shutdown lines are prefixed `[Main]` and go straight to stdout; the
`undoPLC:` / `undoPRG:` lines come from undoPLC's deferred logger. That split is
deliberate. `logRT` only enqueues into a lock-free queue that gets drained from
inside `ioc.run()`, so anything logged before `ioc.run()` starts is not shown
until later and is **lost for good** if the program exits first — a startup
diagnostic that vanishes exactly when the next check fails would be worse than
none. For the same reason the `[Main] Termination signal ...` line on Ctrl+C
cannot use the logger: it runs on the `io_context` thread, which never registers
with it.

One more thing that looks wrong and is not: the `UndoSys: CPU ... locked`
lines can appear *after* `startup on core ...`, despite carrying earlier
timestamps. `logRT` writes to a per-thread lock-free queue and the drain order
is not the timestamp order, so lines from different threads may interleave
either way. Each thread's own lines stay in order, and the timestamps are the
authoritative sequence.

Two things in the log are worth checking, because they are the real-time setup
proving itself: the Master and the Worker land on **different** cores, and no
watchdog trip or cycle overrun is reported.

## Scaling it up

More tasks and more PLC instances work the same way, and cost one isolated CPU
each. The consequences worth knowing before you do:

- undoPLC drives every Worker from a **single** Master cycle, so a task declaring
  a slower `cycle_ms` is **not** slowed down — it still runs every Master cycle.
  Put a genuinely slow task on a PLC instance whose fastest task is that slow.
- The same program listed by two tasks runs **once per task**, at that task's
  rate, not once in total.
- The Master takes one CPU and every task another, so `boot_params`/`isolcpus`
  must cover all of them before the binary will start.

The schema and every rule it enforces: [`docs/16-task-configuration-spec.md`](../../docs/16-task-configuration-spec.md).

## Notes

- `--tasks` requires `--workspace` and `--project-style`, because `Runtime.cpp`
  includes the modular `Programs.hpp`.
- `build.sh` builds `libundoPLC.a` from the undoPLC sources if there is no
  installed one. Installing st2cpp with `-DST2CPP_BUNDLE_UNDOPLC=ON` (the
  default) puts it in `/usr/local/lib`.
- The generated `Runtime.cpp` owns `main()`, so `trace.cpp` deliberately has
  none: it starts its thread from a static initialiser.
- `trace.cpp` reads RT-written counters from an ordinary thread. That is fine for
  a demo and is *not* something to copy into a real PLC: sample through the
  process image or a diagnostic buffer instead.

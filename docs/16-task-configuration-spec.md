# Task Configuration — JSON v1.0 Specification

> Part of the [st2cpp documentation](README.md) (docs/16). Normative schema for
> the Task Configuration consumed by `TaskConfigLoader` / `--tasks`.

## 1. Scope

A **Task Configuration** is the JSON document that declares which cyclic task
runs which programs, how fast, at which priority, and pinned to which CPU core:

```
tasks.json → TaskConfigLoader → TaskConfig (validated)
           → groupByPlc()    → PlcGroup per PLC instance
           → RuntimeEmitter  → Runtime.cpp (undoPLC Master + Workers + main)
```

It answers the IEC 61131-3 / TwinCAT question *"what actually gets executed, and
when"*. It is deliberately separate from the [Project
Configuration](13-project-configuration-spec.md) (which libraries a project
uses): one project may declare many tasks, and a task owns no library
information at all.

This document specifies format version **1.0** and the validation rules applied
by `TaskConfigLoader`.

## 2. Formal structure

```
TaskConfig
  $schemaVersion   string       "1.0"          optional (defaults to "1.0")
  tasks            [TaskEntry]  required

TaskEntry
  name             string        required, non-empty, unique within its `plc`
  plc              string        required, non-empty; grouping key
  cycle_ms         integer       required, > 0
  priority         integer       required, 1..98
  cpu_affinity     integer       optional, -1 (default, automatic) or >= 0
  programs         [string]      optional, non-empty strings
```

Every member not marked `optional` is required; a missing required member is a
validation error. Unknown members are ignored.

## 2.1 Example

```json
{
  "$schemaVersion": "1.0",
  "tasks": [
    { "name": "motors",  "plc": "Line1", "cycle_ms": 10, "priority": 40, "programs": ["MAIN"] },
    { "name": "safety",  "plc": "Line1", "cycle_ms": 10, "priority": 30 },
    { "name": "aux",     "plc": "Line1", "cycle_ms": 50, "priority": 50, "cpu_affinity": 5,
      "programs": ["Control"] },
    { "name": "logic",   "plc": "Line2", "cycle_ms": 20, "priority": 40, "programs": ["Control"] }
  ]
}
```

`$schemaVersion` may be omitted: a document that does not declare it is read as
the version this loader implements, which keeps hand-written `tasks.json` free
of boilerplate. Declaring an unsupported version is an error.

## 3. Validation rules

| Rule | Error |
| --- | --- |
| Root must be a JSON object | `task configuration must be a JSON object` |
| `$schemaVersion`, if present, must be the string `"1.0"` | `$schemaVersion: unsupported schema version '…'` |
| `tasks` must exist and be an array | `tasks: must be an array` |
| `tasks[i]` must be an object | `tasks[i]: expected an object` |
| `name` must be a non-empty string | `tasks[i].name: must be a non-empty string` |
| `plc` must be a non-empty string | `tasks[i].plc: must be a non-empty string` |
| `cycle_ms` must be an integer > 0 | `tasks[i].cycle_ms: must be a positive number of milliseconds` |
| `priority` must be an integer in 1..98 | `tasks[i].priority: must be between 1 and 98` |
| `cpu_affinity` must be -1 or an integer >= 0 | `tasks[i].cpu_affinity: must be -1 (automatic) or a CPU number >= 0` |
| `programs` must be an array of non-empty strings | `tasks[i].programs[j]: must be a string` |
| `name` must be unique within its `plc` (case-insensitive) | `tasks[i].name: duplicate task name '…' in plc '…'` |

Validation collects **every** violation before failing, so one run reports the
whole set rather than the first problem.

### 3.1 Why `priority` stops at 98

undoPLC drives every Worker from a single Master cycle, and the Master is given
`max(task priorities) + 1` so that a task which overruns can never delay the
cycle supervising it (section 4.2). SCHED_FIFO tops out at 99, so the top
priority is reserved for the Master and a task may not claim it. A task at 99
would leave the Master with nothing above it.

### 3.2 Case sensitivity

`plc` is a key, not a label: `groupByPlc()` folds case and strips whitespace, so
`"Line1"`, `"line1"` and `" Line1 "` are one and the same PLC instance, the way
IEC 61131-3 identifiers are resolved elsewhere in the pipeline. `name` is folded
the same way for the duplicate check. Task and PLC names are *not* folded when
building C++ class names; `sanitize()` maps anything that is not
`[A-Za-z0-9_]` to `_`.

## 4. Mapping onto undoPLC

### 4.1 Master and Worker

- One `UndoMasterTaskBase` subclass per distinct `plc` value.
- One `UndoWorkerTaskBase` subclass per entry of `tasks`, named
  `<Plc>_<Task>`.
- Task classes are emitted in `st2cpp_generated`, so they cannot collide with
  the namespace the ST types are generated into (`undoCore` by default).

Each Worker owns its **own instance** of every program it lists. A program bound
to two tasks therefore gets two copies with independent state, matching TwinCAT,
where a PROGRAM called from two tasks is instantiated once per task.

### 4.2 Derived Master settings

| PlcGroup member | Value | Reason |
| --- | --- | --- |
| `cycleMs` | the **fastest** `cycle_ms` in the group | one Master cycle drives every Worker, so it must tick at least as fast as the fastest task |
| `masterPriority` | `max(priority) + 1` | the Master supervises the cycle and must preempt a task that overruns |
| `tasks` | indices in declaration order | the order the runtime creates, starts and runs the Workers |

Because all Workers of a Master share its cycle, a task declaring a slower
`cycle_ms` than its PLC's fastest is **not** slowed down: it still runs every
Master cycle. Declare the cycle on the task whose rate you care about.

### 4.3 CPU assignment

`UndoWorkerTaskBase` takes the core to pin to in its constructor and has no
"auto" mode, so the generated runtime performs the allocation:

- `cpu_affinity >= 0` is passed straight to the Worker constructor, and the core
  is recorded as taken so no automatic task can land on it.
- `cpu_affinity: -1` (the default) makes the runtime reserve the first CPU in
  `UndoSys::getInstance().getIsolatedCpu()` that no task of that PLC has taken,
  mirroring the resolution `UndoMasterTaskBase::start()` performs for the Master
  itself. The taken set is per PLC instance.
- `main()` refuses to start when the machine has fewer isolated CPUs than the
  configuration needs (one per Master plus one per task), and says so, instead of
  silently piling tasks onto one core.

This requires the target to be booted with `isolcpus=` (see
[`03-build-and-test.md`](03-build-and-test.md)).

## 5. CLI usage

`--tasks` requires `--workspace` and `--project-style`, because the generated
runtime `#include`s the modular project's `Programs.hpp`:

```bash
st2cpp --workspace ./my_plc --project-style \
       --tasks tasks.json --output-dir build
```

Output:

| File | Content |
| --- | --- |
| `Programs.hpp`, `Programs.cpp` | the PRG classes the runtime calls |
| `Runtime.cpp` | Master/Worker classes, `main()`, CPU resolution, shutdown |

Refused (with a diagnostic and no partial output):

- a task naming a `programs` entry that is not a `PROGRAM` POU of the
  workspace — every offending reference is reported, not just the first;
- `--tasks` without `--workspace` or `--project-style`;
- a `tasks.json` that fails section 3.

Passing the generated binary `--log2console` mirrors the undoPLC logger to
stdout; without it, the logger keeps its file sink.

## 6. Shutdown

`main()` installs an `asio::signal_set` for `SIGINT`/`SIGTERM` on the single
non-real-time thread, which stops every Master (and through it every Worker),
restores the isolated CPUs to powersave, and stops the `io_context`. The
generated `safeStopHandler()` overrides are placeholders that log: assign an
`undoCore::IoBus` with `setIoBus()` to drive real outputs to a safe state.

## 7. Relationship to the other configurations

| Document | Declares |
| --- | --- |
| [`12-library-descriptor-spec.md`](12-library-descriptor-spec.md) | what a library contains |
| [`13-project-configuration-spec.md`](13-project-configuration-spec.md) | which libraries a project uses |
| this document | which cyclic task runs which programs, when, at which priority and core |

The three are orthogonal and may all be present in one project.

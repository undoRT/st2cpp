# Examples

Every example under `examples/` is self-contained: it builds its own artifacts,
runs the pipeline and — where applicable — validates the generated C++ with a
real compiler.

## 1. `examples/external_library`

Demonstrates the **external-library pipeline** end to end, entirely through the
CLI (`--ext-libs`).

```
external_library/
  project.json            # {"libraries": [ "io", "core" ] }
  libraries/core.json     # generic geometry: struct Range, const MAX_AI_CHANNELS
  libraries/io.json       # fluid measurement: AlarmClass, AnalogChannel,
                          #   ScaleAnalog (freeFunction), NormalizeRange
                          #   (staticMethod), Averager (FB), gAlarm (global)
  main.st                 # PLC program consuming io:: and core:: symbols
  mock/core/core.hpp      # native C++ the core bindings must match
  mock/io/io.hpp          # native C++ the io bindings must match
  build.sh                # CLI + syntax-check, end to end
  README.md               # walkthrough + expected output
```

`build.sh` performs, out-of-source:

```bash
cmake -S <repo> -B <repo>/build -DBUILD_TESTS=OFF
cmake --build <repo>/build --target st2cpp -j
<repo>/build/st2cpp --workspace . --output-dir generated --ext-libs project.json -v
g++ -std=c++17 -fsyntax-only generated/main.cpp -I generated -I mock \
    -I <repo>/st2cpp_includes/undoCore/include
```

Expected successful run:

```
External libraries loaded from project.json: io, core
[workspace] Parsing /…/main.st … OK
[workspace] Semantic analysis passed with 0 errors, 0 warnings
[workspace] Generated generated/main.hpp, generated/main.cpp
[C++] Syntax-check OK
```

Notable generated-code facts (see the example README for the full listing):

- the ST program (POU) is emitted as `PROGRAM_MAIN`-style uppercase POU;
- `#include "core/core.hpp"` and `#include "io/io.hpp"` appear automatically;
- `CH.RANGE.MIN` → struct member access against the C++ `core::Range` type;
- `ALM := io::AlarmClass::WARNING` → namespace-qualified enum constant;
- `ALM := g_alarm` → `io` global binding `gAlarm` → `g_alarm` (uppercase ST
  names, verbatim `cppBinding.symbol`);
- `X := ScaleAnalog(chn.raw, SomeRealParam)` →
  `io::ScaleAnalog(...)` free function call;
- `X := NormalizeRange(1.0)` → static method `io::MathUtils::normalize(...)`;
- `AVG(ENABLE := TRUE)` → `AVG.set_ENABLE(true)`, `AVG.process()`,
  `AVG.get_AVG()` methods on the FB-binding instance;
- `N := MAX_AI_CHANNELS` → const binding `k_max_ai_channels`.

## 2. Per-feature demos (exploration/legacy)

The remaining folders under `examples/` are per-feature demos produced while
the pipeline matured; they are not wired into the CMake build and several still
expect a `st2cpp` binary copied into their folder (some contain stale binaries
and build artifacts — git-ignored in part).

| Folder | Input | Shows |
|--------|-------|-------|
| `arrays/` | `test_array.st` | array declarations, modular-project generation (GVLs/Functions/FunctionBlocks/Programs) |
| `counter/` | `counter.st`, `demo.st`, `counter2/` | counters + demo polling loop; modular output |
| `inheritance/` | `test_inheritance.st` | `EXTENDS` FB inheritance emitted as C++ classes (`test_inheritance.hpp/.cpp`) |
| `processImage/` | `test_process_image.st` | `AT %I/%Q/%M` mapping; self-contained CMAKE driver (`CMakeLists.txt` + `main.cpp`) that builds `plc_test` and runs it |
| `semantics/` | `test_semantics.st` | semantic-analysis driver (`main.cpp`) with assertions |
| `simple_program/` | `test_program.st` | minimal single-file translation + generated header/source |
| `simple_program_2/` | `simple_program_2.st` | single-file scratch (git-ignored) |

These are kept as documentation-by-example of each feature area. For a
maintained, reproducible flow prefer:

1. `examples/external_library` (fully scripted via `build.sh`), or
2. the GoogleTest integration suites in `tests/`.

## 3. Using the examples as integration tests

Because example runs compile the generated C++, they act as the project's
integration proof:

- type mappings, cross-library type reuse (`core.Range` inside `io.AnalogChannel`),
- C++ symbol fidelity (`cppBinding.symbol`),
- runtime header resolution (`undoCore/undoCore.hpp`),
- process-image accessor availability.

Run the full GoogleTest suite (which includes a `-syntax-only`-equivalent
compilation test) with `ctest` — see `03-build-and-test.md`.
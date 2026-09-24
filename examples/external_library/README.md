# Esempio `external_library` — librerie esterne via `--ext-libs`

Esempio end-to-end della pipeline "librerie esterne": al CLI `st2cpp` basta
passare **il JSON del progetto** (`--ext-libs project.json`): il compilatore
carica in automatico tutti i descrittori libreria dichiarati lì e li include
nella transpilazione.

```
main.st ──┐
          ├──> st2cpp --workspace . --ext-libs project.json ──> generated/main.{hpp,cpp}
project.json ──┘          │
                          └─ ProjectConfig → LibraryRegistry → Semantic → Codegen
```

## Contenuto

| File | Ruolo |
|------|-------|
| `project.json`        | una riga decide tutto: elenca `core` e `io` (con relative librerie) |
| `libraries/core.json` | descrittore della libreria `core` (struct `Range`) |
| `libraries/io.json`   | descrittore della libreria `io` (enum, struct, function, FB, constant, global) dipendente da `core ^1.0.0` |
| `main.st`             | programma ST che usa i simboli esterni |
| `mock/core/core.hpp`  | header C++ mock della libreria `core` (per il syntax-check) |
| `mock/io/io.hpp`      | header C++ mock della libreria `io` (idem) |
| `build.sh`            | compila il CLI, lo esegue con `--ext-libs project.json`, e verifica il C++ generato |

## Build ed esecuzione

```bash
./build.sh
```

ovvero, step per step:

```bash
cmake -S ../.. -B ../../build
cmake --build ../../build --target st2cpp
rm -rf generated
../../build/st2cpp --workspace . --output-dir generated --ext-libs project.json
g++ -std=c++17 -fsyntax-only generated/main.cpp -I generated -I mock -I ../../st2cpp_includes/undoCore/include
```

Nota: nessun `main.cpp`/`CMakeLists.txt` nell'esempio — il C++ non viene scritto
a mano, viene generato dal CLI a partire da `main.st` + `project.json`.

## Cosa dimostra

1. **`--ext-libs project.json`**: il CLI, senza elencare N librerie, prende
   `ProjectConfigLoader` → `ProjectLoader` → `LibraryRegistry` e risolve i
   simboli esterni durante l'analisi semantica (`analyze(tu, registry)`).
2. **Cross-library type reference**: `io.AnalogChannel.Range` è definito nella
   libreria `core` (dipendenza `^1.0.0` in `io.json`).
3. **Tutti i tipi di binding**, dal codegen:
   - struct/enum con namespace: `io::AnalogChannel CH{};`, `ALM = io::AlarmClass::WARNING;`
   - freeFunction/constant/global verbatim: `X = scale_analog(...);`, `N = kMaxAiChannels;`, `ALM = g_alarm;`
   - staticMethod: `X = io::MathUtils::normalize(...);`
   - FB: `AVG.set_ENABLE(true); ... AVG.process(); X = AVG.get_AVG();`
   - include automatici degli header delle librerie usate.
4. C++ generato **compila** contro gli header mock e la runtime `undoCore`.
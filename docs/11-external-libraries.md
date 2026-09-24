# External Libraries

External libraries let your ST reference symbols that live in **native C++
code**: free functions, static methods, types, constants, globals and FB-like
objects. The binding metadata is expressed in two JSON documents:

1. a **Library Descriptor** (`io.json`, `core.json`, …) — describes the C++
   surface and the ST-visible symbols of one library;
2. a **Project Configuration** (`project.json`) — lists the libraries the
   compilation should load and where their descriptors live.

## Data flow

```
project.json ──ProjectConfigLoader──▶ ProjectConfig
                                          │  (libraries: path + framework…)
                                          ▼
Library Descriptor(s) ──LibraryLoader──▶ LibraryDescriptor (validated)
                                          ▼
                          LibraryRegistry (ids, allOrdered, missingDependencies)
                                          │  SemanticAnalyzer::analyze(tu, registry)
                                          ▼
                          LibrarySymbolImporter ──▶ symbol table / BodyVisitor
                                          │
                                          ▼
                          CodeGenerator (bindings + per-namespace includes)
```

## Descriptor content (summary)

Normalized and validated by `LibraryLoader` (see
`12-library-descriptor-spec.md` for the complete schema):

| Section | Purpose |
|---------|---------|
| `framework` | `1.0` |
| `namespace` | the C++ namespace that owns every binding (`io`) |
| `cppHeader` | header to `#include` (`io/io.hpp`) |
| `typeMappings` | inline structs, enums, typedefs reusable from ST |
| `functionMappings` | `freeFunction`, `staticMethod` — ST call → `ns::symbol(args)` |
| `fbMappings` | FB-like class bindings with `inputs`/`outputs` and setter/getter names (e.g. `set_ENABLE`/`process`/`get_AVG`) |
| `scopeMappings` | free constants and global variables (`kMaxAiChannels` → `k_max_ai_channels`, `gAlarm` → `g_alarm`) |
| `dependencies` | ids of other descriptors required at load time |

Every binding records the **ST name** the analyzer resolves (e.g.
`ScaleAnalog`, `AVG.ENABLE`) and the **C++ symbol** actually emitted
(`namespace::ScaleAnalog`, `AVG.set_ENABLE`) — casing is preserved verbatim for
`cppBinding.symbol`.

## Registry

```cpp
class LibraryRegistry {
public:
    bool registerLibrary(LibraryDescriptor descriptor, std::string& error);
    const LibraryDescriptor* get(const std::string& id) const;
    size_t size() const;
    std::vector<std::string> ids() const;
    std::vector<const LibraryDescriptor*> all() const;
    std::vector<const LibraryDescriptor*> allOrdered() const;   // normalized id order
    std::vector<std::string> missingDependencies(const LibraryDescriptor& lib) const;
};
```

- `registerLibrary` rejects duplicate ids (error string set, returns `false`).
- Duplicate-name or conflicting bindings are diagnosed during loading.
- Descriptors that declare `dependencies` the registry cannot satisfy are
  reported (a `LibraryLoader::LoadResult` aggregates the failure paths; the CLI
  prints every error and exits `1`).

## Using the CLI

```bash
st2cpp --workspace . --ext-libs project.json --output-dir generated -v
```

- Loads `project.json` (`ProjectConfigLoader`), then each listed descriptor
  (`ProjectLoader::load`), registering everything into a `LibraryRegistry`.
- `-v` prints the loaded library ids.
- The registry is passed to `analyze(tu, registry, strictness)`; ST symbols not
  found locally resolve against the libraries. Generated code then:
  - `#include`s each namespace's header from `cppHeader`;
  -, emits `namespace::symbol`-qualified calls for functions/statics;
  - instantiates FB bindings inline (e.g. `AVG.set_ENABLE(true)` /
    `AVG.process()` / `AVG.get_AVG()`).

## Example layout (`examples/external_library`)

See `14-examples.md` for the walkthrough. In short:

```
examples/external_library/
  project.json           # {"libraries": [io, core]} — the --ext-libs input
  libraries/io.json      # fluid-measurement surface (enum, struct, fn, FB…)
  libraries/core.json    # generic geometry: struct Range, consts
  main.st                # PLC program using io:: + core:: symbols
  mock/core/core.hpp     # native C++ definitions the bindings must match
  mock/io/io.hpp         #   (stubbed for the syntax-check)
  build.sh               # CLI + syntax-check, end to end
```

## Guarantees and limits

- **Ordered output**: `allOrdered()` and per-namespace emission give stable,
  deterministic codegen.
- **Namespace isolation**: each library contributes its own C++ namespace; no
  cross-library symbol pollution that isn't declared in `dependencies`.
- **Transitive resolution is not automatic**: dependencies are declared and
  validated but the loader does not auto-load transitive descriptors; order
  `project.json` accordingly or register them first.
- **No descriptor auto-generation** from C++ headers — describe by hand (or
  with your tooling) per the schema.
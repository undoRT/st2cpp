# st2cpp Documentation

Technical documentation for **st2cpp**, the IEC 61131-3 Structured Text to C++
17 transpiler. Everything here is written against the current source tree
(`include/`, `src/`, `tests/`, `examples/`). The repository has **no external
runtime dependency**: the generated code targets the header-only `undoCore`
runtime shipped under `st2cpp_includes/`.

## Index

| # | Document | Contents |
| --- | ---------- | ---------- |
| — | [`01-overview.md`](01-overview.md) | What st2cpp is, the end-to-end pipeline, repository layout |
| — | [`02-architecture.md`](02-architecture.md) | Module map, build targets, dependency chains, AST model |
| — | [`03-build-and-test.md`](03-build-and-test.md) | Requirements, CMake options, targets, running the tests |
| — | [`04-command-line.md`](04-command-line.md) | Complete CLI reference (`--ext-libs`, exit codes, modes) |
| — | [`05-language-support.md`](05-language-support.md) | IEC 61131-3 coverage and known limitations |
| — | [`06-semantic-analysis.md`](06-semantic-analysis.md) | Two-pass analysis, symbol table, type checking, Strict vs Permissive |
| — | [`07-code-generation.md`](07-code-generation.md) | Single-file and modular project generation, C++ output shape |
| — | [`08-process-image.md`](08-process-image.md) | `AT %I/%Q/%M` mapping, `ProcessImageConfig`, auto-detection |
| — | [`09-runtime.md`](09-runtime.md) | The `undoCore` header-only runtime and the elementary-to-C++ mapping |
| — | [`10-json-module.md`](10-json-module.md) | `st2cpp::json`: DOM, parser, serializer, error model |
| — | [`11-external-libraries.md`](11-external-libraries.md) | External libraries: descriptors, project JSON, `--ext-libs` |
| — | [`12-library-descriptor-spec.md`](12-library-descriptor-spec.md) | Library Descriptor JSON v1.0 specification |
| — | [`13-project-configuration-spec.md`](13-project-configuration-spec.md) | Project Configuration JSON v1.0 specification |
| — | [`14-examples.md`](14-examples.md) | Walkthrough of every example under `examples/` |
| — | [`15-library-descriptor-export.md`](15-library-descriptor-export.md) | Descriptor JSON: import and export (`LibraryDescriptorBuilder`, ST → JSON) |

## Suggested reading order

1. `01-overview.md` — the big picture.
2. `02-architecture.md` — how the pieces fit together.
3. `04-command-line.md` — the daily driver.
4. `06-semantic-analysis.md` — internals of the analyzer.
5. `07-code-generation.md` + `09-runtime.md` — what the generated C++ looks like.
6. `11-external-libraries.md` + `12/13` — the external-library subsystem.
7. `15-library-descriptor-export.md` — the new ST → descriptor export direction.
8. `14-examples.md` — hands-on.

The two specification documents (`12`, `13`) are normative: they describe the
exact JSON formats and the validation rules enforced by the loaders.

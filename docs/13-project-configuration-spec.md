# Project Configuration — JSON v1.0 Specification

> Part of the [st2cpp documentation](README.md) (docs/13). Normative schema for
> the Project Configuration consumed by `ProjectConfigLoader` / `--ext-libs`.

## 1. Scope

A **Project Configuration** is the JSON document that declares which external
libraries a st2cpp project uses and where to find their descriptors:

```
Project JSON → ProjectConfigLoader → ProjectConfig (validated)
            → ProjectLoader → LibraryRegistry → SemanticAnalyzer
```

The configuration is deliberately separate from the Library Descriptor: the
descriptor describes *what a library contains*, the configuration describes
*which libraries a project wants on which versions*.

This document specifies format version **1.0** and the validation rules applied
by `ProjectConfigLoader` and `ProjectLoader`.

## 2. Formal structure

```
ProjectConfig
  $schemaVersion   string     "1.0"              required
  name             string     optional
  libraries        [LibraryEntry]  optional

LibraryEntry
  id        string   required, non-empty, unique (case-insensitive)
  version   string   optional  version constraint (see section 3)
  path      string   required, non-empty, relative or absolute
```

Every member not marked `optional` is required; a missing required member is a
validation error. Unknown members are ignored.

## 2.1 Example

```json
{
  "$schemaVersion": "1.0",
  "name": "MyProject",
  "libraries": [
    { "id": "corelib",  "version": "^1.0.0", "path": "libs/corelib/descriptor.json" },
    { "id": "examplelib", "version": "^1.0.0", "path": "../external/examplelib.json" }
  ]
}
```

## 2.2 Path resolution

- Absolute paths (POSIX `/` prefix, Windows drive `C:\` or UNC) are used
  verbatim.
- Relative paths are resolved **against the directory of the configuration
  file** (never blindly against the process working directory). The resolved
  value is stored in `LibraryEntry::resolvedPath`.
- `fromJson`/`fromString` have no base directory: they only validate and keep
  the declared `path`; `fromFile` additionally fills `resolvedPath`.

## 2.3 Loader API (`st2cpp::project::ProjectConfigLoader`)

- `fromJson(const JsonValue&)`, `fromString(const std::string&)`,
  `fromFile(const std::string&)` → `ProjectConfigLoadResult`
  (`std::optional<ProjectConfig>` + `std::vector<ProjectConfigError>`; `ok()`).
- `resolvePath(baseDir, path)` and `dirName(path)` are public helpers.
- Validation is not fail-fast: errors are collected and each carries a JSON
  path ("libraries[1].version") and a human-readable message.

## 3. Version constraints

Constraints reuse the Library Descriptor model
(`library::VersionConstraint`, `library::VersionClause`, `library::Version`),
so parsing and the matching semantics stay in one place.

The constraint evaluator implemented is the **minimal subset needed by the
project loader** and follows standard semantics:

- **Exact** `1.2.3` / `=1.2.3` — the version must be exactly `1.2.3`
  (prerelease compares lower than the same release).
- **Wildcard** `*`, `1.x`, `1.2.x` — non-wildcard components must match exactly.
- **Caret** `^1.2.3` — compatible with `>=1.2.3 <2.0.0`; `^0.2.3` —
  `>=0.2.3 <0.3.0`; `^0.0.3` — `>=0.0.3 <0.0.4` (locks the left-most non-zero
  segment).
- **Tilde** `~1.2.3` — `>=1.2.3 <1.3.0` (locks major and minor).
- **Relational** `>= / > / <= / <` — plain numerical comparison.
- A constraint is an **AND** of one or more clauses (`">=1.0.0 <2.0.0"`).

`ProjectLoader` checks the constraint with `VersionConstraint::matches(Version)`
against the descriptor's `version` and rejects the entry when unsatisfied.

## 4. Loading the libraries (`st2cpp::project::ProjectLoader`)

`ProjectLoader::load(const ProjectConfig&)` produces a
`ProjectLibraryResult` (a `library::LibraryRegistry` plus aggregated
`ProjectLibraryError{libraryId, path, message}`). Per entry it verifies:

1. the descriptor file exists and loads through `library::LibraryLoader` as a
   valid descriptor;
2. the descriptor `id` matches the configured `id` (case-insensitive);
3. when a `version` constraint is declared, the descriptor version satisfies it;
4. the id is not already registered (duplicate declaration is an error).

Descriptor `dependencies` are declared but **not resolved** by this loader:
full dependency resolution remains out of scope. The registry exposes
`LibraryRegistry::missingDependencies(lib)` to inspect which declared
dependencies are missing, and `LibraryRegistry::allOrdered()` for the
deterministic (uppercase-id) iteration order used by the semantic importer.

## 5. Semantic integration

The registry is handed to `SemanticAnalyzer::analyze(tu, registry, strictness)`.
`LibrarySymbolImporter` materializes the descriptor entities into the symbol
table's dedicated **external scope**; project-local declarations always shadow
external ones. When two libraries export the same symbol, the library with the
*lower* uppercase id wins and a warning diagnostic `ExternalSymbolCollision`
(1010) is emitted. See `include/semantic/LibrarySymbolImporter.h` for the
details.
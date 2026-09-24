# Language Support

Transpilation scope covers the core of IEC 61131-3 Structured Text. This page
lists what is supported and the known limitations. The token set in
`include/lexer/Token.h` is the authoritative reference for accepted keywords.

## Data types

**Elementary types** (`BaseType`, `include/ast/AST.h`):

- `BOOL`, `SINT`, `INT`, `DINT`, `LINT`, `USINT`, `UINT`, `UDINT`, `ULINT`
- `REAL`, `LREAL`
- `BYTE`, `WORD`, `DWORD`, `LWORD`
- `STRING`, `WSTRING`
- `TIME`, `DATE`, `DT`, `TOD`

Elementary types are mapped to the fixed-width aliases of the `undoCore`
runtime — see `09-runtime.md`.

**Derived types:**

- `TYPE … END_TYPE` blocks with:
  - **Aliases** — `MyInt : DINT;`
  - **Enumerations** — `Colour : (RED, GREEN, BLUE);` (also as direct
    `TYPE Colour : (A, B); END_TYPE`)
  - **Structures** — `TYPE T : STRUCT … END_STRUCT END_TYPE`
- **Arrays** — `ARRAY[lo..hi, lo..hi] OF <type>` in type declarations, variable
  declarations, and inline in expressions.
- **Pointers / references** — `POINTER TO <type>`, `REF_TO <type>`, with
  dereference (`^`) and address-of (`ADR()`); `__POINTER`-style aliases are not
  lexed, use `POINTER TO`.

## Program organization units

| POU | Notes |
|-----|-------|
| `FUNCTION` | return type, parameters, `VAR`, `VAR_INPUT`/`OUTPUT`/`IN_OUT`; body in the end |
| `FUNCTION_BLOCK` | interface + instance-based semantics; **inheritance** via `EXTENDS`, **interface implementation** via `IMPLEMENTS` |
| `PROGRAM` | same `VAR_*` interface as FBs; the top-level unit instantiating FBs |
| `METHOD` | `PUBLIC/PRIVATE/PROTECTED`, parameters, return type, `OVERRIDE`, `ABSTRACT`; `SUPER^` calls supported |
| `PROPERTY` | getter/setter with `FINAL`, rendered by the codegen as accessors backed by the member |
| `INTERFACE` | `INTERFACE Name EXTENDS Base END_INTERFACE` — contract for `IMPLEMENTS` |

`EXTENDS` and `IMPLEMENTS` on an FB generate real C++ inheritance
(`: public Base` / `: public Interface`). Base-class and interface resolution
is performed during semantic analysis and folded into the topological order.

## Variable sections

- `VAR / VAR_CONSTANT / VAR_INPUT / VAR_OUTPUT / VAR_IN_OUT / VAR_TEMP /
  VAR_EXTERNAL / VAR_GLOBAL`
- qualifiers `CONSTANT`, `RETAIN`
- `AT %I…/ %Q… / %M…` process-image placeholders (see `08-process-image.md`)
- default initializers (`x : INT := 5;`)
- multiple declarators (`a, b : BOOL;`)

Scope modifiers (`{PUBLIC}` etc.) and attributes (`{attribute «value»}` /
`{attribute := value}` blocks via `KW_ATTRIBUTE/END_ATTRIBUTE`) are tokenized
and preserved for codegen.

## Statements

- `IF … THEN … ELSIF … ELSE … END_IF`
- `CASE x OF … ELSE … END_CASE`
- `FOR i := a TO b [BY s] DO … END_FOR`
- `WHILE … DO … END_WHILE`
- `REPEAT … UNTIL … END_REPEAT`
- `EXIT`, `CONTINUE`, `RETURN`
- Assignment (`:=`), `&=`-family compound assignments
- POU / FB calls with named arguments (`fb(input := 1)`), function calls
- `ADR()`, `SIZEOF()`, `NOT / AND / OR / XOR / MOD`
- comparisons and arithmetic across integer/float types
- typed literals (`INT#5`), binary literals (`2#1010`), `TRUE/FALSE`

## Expressions

Literals, identifiers (case-insensitive resolution), member access (`.`),
dereference (`^`), array indexing, implicit casts guided by
`TypeSystem::CastResult` (e.g. `INT`→`DINT`, `REAL`→`LREAL` widening),
explicit `Type#value` literals.

## Known limitations

- **`VAR_EXTERNAL` linkage across files**: parsing and symbol modelling are
  supported; cross-translation-unit external-variable binding is only fully
  realized in project-style mode where units are merged.
- **`VAR_ACCESS` / `VAR_RETAIN` block scopes** over complex types are partially
  handled — verify against the generated C++.
- **`ATTRIBUTE` semantics** are preserved but the runtime does not emit C++
  attributes; silent semantic pass-through.
- **`REF_TO` and pointer arithmetic** beyond `ADR`/deref are not type-system
  tracked.
- **`STRING` bit/byte manipulation** and `WSTRING` full operations rely on the
  runtime; generated code stays standard C++ (`std::string`/`std::wstring`).
- **Method overloads** across the FB hierarchy are resolved statically per name.
- Language variants (ST editor dialects, iEC `RS`, `SR`, bistable FB text-style
  invocations) are not emulated — call the generated methods instead.

The analyzer runs Permissive by default so unresolvable constructs degrade to
legacy syntactic inference instead of aborting; `--strict` surfaces every
limitation as a hard error (see `06-semantic-analysis.md`).
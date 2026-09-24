# JSON Module (`st2cpp::json`)

A self-contained JSON DOM, parser and serializer living in
`include/json/JsonValue.h` / `src/json/JsonValue.cpp`. Zero external
dependencies — it exists so the external-library subsystem (`.json` files) needs
no third-party library at build time.

## Model — `struct JsonValue`

```cpp
enum class JsonType : uint8_t { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    bool     boolean = false;
    double   number = 0.0;
    std::string numberRaw;   // exact lexical form of a number
    std::string text;        // string contents (without quotes)
    std::vector<JsonValue> array;                          // Array
    std::vector<std::pair<std::string, JsonValue>> members; // Object (ordered)
};
```

- One struct discriminates on `type`; there is no subclassing.
- **Numbers keep both the parsed `double` and the exact lexical token
  (`numberRaw`)** so serializing a document round-trips values like `"8"`,
  `"0.0"` or `"1e3"` verbatim.
- Object members are an **ordered** vector of key/value pairs — order-sensitive
  round trips are preserved.

Predicates/accessors include `isNull()`, `isNumber()`, `asBoolean()`,
`asNumber()` (raw-text-aware, see `numberRaw`), `asString()`, typed member
getters, array/element access and object lookup. The header documents each
accessor's fallback behaviour on type mismatch.

## Parsing

```cpp
JsonValue parse(const std::string& text);
```

Accepts a full JSON document. On invalid input it **throws**:

```cpp
class JsonParseError : public std::runtime_error {
public:
    JsonParseError(const std::string& message, size_t line, size_t column);
    size_t line()   const noexcept;
    size_t column() const noexcept;
};
```

`what()` already embeds the position (`JSON error at <line>:<column>: …`);
`line()`/`column()` are 1-based for building your own diagnostics. The CLI
catches these and surfaces them in the `--ext-libs` error list together with
the failing file.

Supported JSON: primitives, strings with escapes (`\" \\ \/ \b \f \n \r \t`
and `\uXXXX`), numbers (int/float/exponent), arrays, nested objects. Trailing
garbage after the document (and comments) are treated as errors.

## Serialization

```cpp
std::string dump(const JsonValue& value, int indent = 0);
```

Dumps a DOM back to JSON. With `indent > 0` the output is pretty-printed with
`indent`-space steps; otherwise it is compact. Numbers serialize from
`numberRaw` when it is present, so parsed→dumped round trips keep the original
lexical form.

## Consumers

- `project/ProjectConfigLoader` — reads `project.json` and validates the
  Project Configuration schema (see `13-project-configuration-spec.md`).
- `library/LibraryLoader` — parses Library Descriptor documents and validates
  them (see `12-library-descriptor-spec.md`).

## Testing

`tests/json/JsonValueTest.cpp` covers parse/dump round trips, number lexical
preservation, escapes, nested structures and `JsonParseError` reporting
(line/column correctness).
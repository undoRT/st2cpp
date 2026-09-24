# Process Image

The process image is the PLC IEC 61131-3 concept of a memory-mapped I/O area:
inputs (`%I`), outputs (`%Q`) and markers/merkers (`%M`). st2cpp compiles
`AT %I/%Q/%M` declarations into byte-buffer-backed accessors.

## `ProcessImageConfig`

```cpp
struct ProcessImageConfig {
  size_t inputBytes    = 1024;  // Input buffer bytes
  size_t outputBytes   = 1024;  // Output buffer bytes
  size_t markerBytes   = 1024;  // Marker buffer bytes
  bool   autoDetect    = true;  // Auto-detect from addresses used
  bool   useGlobalPI   = false; // Project-style: shared process image
  std::string instanceName = "processImage";
};
```

Owned by `SemanticInfo`; the CLI exposes it through `--pi-*` options.

## Address mapping and accessors

`AT %M<uint32:offset>.0`-style markers map a head-address plus bit offset;
`AT %I*`, `AT %Q*` with an alignment to `uint32_t` pin the buffer at the
computed offset:

- Inputs (`%I`) → read from the **Input** buffer; getters read raw bytes and
  reinterpret them as the declared type.
- Outputs (`%Q`) → write through setters into the **Output** buffer.
- Markers (`%M`) → read/write through the Marker buffer (packed byte
  addressing; bit-level access via `.0` … `.7`).

The generated C++ declares the buffers (e.g.
`uint8_t processImageOutput[PI_OUTPUT_SIZE]`) plus typed accessors aligned to
the addresses the analyzer resolved, so the runtime/caller can DMA the buffers
in/out.

## Auto-detection (`--pi-auto`, default)

`ProcessImageAnalyzer::analyze(const TranslationUnit&)` scans every `AT %…`
placeholder and computes heights:

- The first/last `%I` and `%Q` byte offsets considered relative to
  `AT %I*`/`AT %Q*` header registration;
- `%M` marker usage (head + bit offset) contributes to the marker size.

If it can derive a configuration, the CLI overrides the defaults with it;
otherwise it warns and keeps the manual sizes. Disable with `--pi-no-auto`.

## CLI knobs

```
--pi-auto                      # auto-detect (default)
--pi-no-auto                   # manual sizes
--pi-input <bytes>             # default 1024
--pi-output <bytes>            # default 1024
--pi-marker <bytes>            # default 1024
```

Manual sizes are used verbatim when auto-detection is disabled; with
auto-detection on, the analyzer's recommendation wins and the manual values
become the fallback baseline.

## Runtime support

The `undoCore` runtime provides the byte-buffer primitives and forward
declarations used by the generated accessors. When process-image addresses are
absent, the buffers are still emitted (zero-sized or configured) so generated
code always links against the same shape.
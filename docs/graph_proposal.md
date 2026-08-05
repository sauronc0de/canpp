# Variable graph proposal

## Summary

Add an optional graph view that plots decoded DBC signal values against trace
time. A graph is created from one or more explicitly selected signals. The
core owns trace traversal and decoding; the renderer owns selection controls
and drawing. This keeps graph extraction usable by a future export command and
requires render dependencies to stay out of headless builds; the current
`externals` setup configures SDL/OpenGL/ImGui unconditionally, so the build
layout must be corrected as part of the GUI phase.

## Goals

- Let a user browse DBC signals, select several variables, and create a graph.
- Keep signals from different messages unambiguous, even when their names are
  identical.
- Plot physical decoded values with units, preserving source order and exact
  trace timestamps.
- Support plotting either the current `Session` selection or the complete
  trace.
- Make unavailable signal samples visible as gaps rather than fabricated data.
- Keep the core API testable without SDL, OpenGL, or a plotting library.

## Non-goals (MVP)

- Editing DBC files or defining signals without a DBC.
- A general-purpose formula/calculation language.
- Automatic decimation, interpolation, smoothing, or resampling.
- Persisting graph layouts, annotations, CSV export, or CLI graph commands.
- Plotting non-CAN protocols before they expose a variable catalog.

## Current architecture constraints

- `src/render/CMakeLists.txt` is currently empty and `src/app/CMakeLists.txt`
  only adds the trace CLI. The proposal therefore requires a new optional GUI
  target rather than extending an existing window.
- `externals/CMakeLists.txt` currently configures SDL/OpenGL/GLEW and ImGui
  unconditionally. All render-only discovery/fetch blocks—including SDL,
  ImGui, FreeType, and the plotting backend—must move behind `ENABLE_RENDER`
  (or be split into render-only targets) before headless builds can be
  dependency-free.
- `Session::open()` resets trace selection but does not clear the loaded DBC,
  while `load_dbc()` does not expose a revision. A GUI controller should own a
  monotonically increasing data-generation token, clear graph state after
  trace/DBC operations, increment the selection revision after every
  successful filter/reset mutation, and discard results whose token changed.

## User workflow

1. Open a communication trace and load a DBC as today.
2. Open **Graph**. If either prerequisite is missing, show the corresponding
   disabled/error state.
3. Browse signals grouped by DBC message. Search matches message or signal
   names. Each row shows signal name, CAN ID, extended status, and unit.
4. Check one or more signals. The selected list shows the full message-qualified
   identity; duplicate signal names are allowed.
5. Choose **Current selection** (the default) or **Full trace**.
6. Press **Create graph**. A graph panel is created with a shared time axis,
   one series per selected signal, legend entries, pan/zoom, reset, and a
   crosshair tooltip.
7. Changing filters, opening another trace, or loading another DBC invalidates
   the displayed data. The user must create the graph again; stale selections
   are not silently reused.

## Proposed UI

```text
+--------------------------- Graph: New ----------------------------+
| Search [ speed                              ] Source (o) Selection  |
|                                             ( ) Full trace          |
| Signals                                                           |
| v Engine (0x100, standard)                                      |
|   [x] Speed                         km/h                         |
|   [ ] Temperature                   degC                         |
| v Dashboard (0x200, standard)                                  |
|   [x] Speed                         km/h                         |
|                                                                   |
| Selected (2)   Engine.Speed [0x100]   Dashboard.Speed [0x200]    |
| [Select all] [Clear]                              [Create graph] |
+-------------------------------------------------------------------+

+------------------------------- Graph ------------------------------+
| [Reset view]  [x] Engine.Speed [km/h]  [x] Dashboard.Speed [km/h] |
|  value                                                             |
|     100 |             __/\__                                       |
|      50 | __/\___   /      \   ...                                 |
|       0 +------------------------------------------------ time    |
|         0                  5                  10 seconds           |
|                crosshair: t=4.250000s, Speed=72.0 km/h            |
+-------------------------------------------------------------------+
```

The sketch is illustrative. Series with incompatible units should still be
selectable, but the renderer should use separate Y axes or clearly label the
shared axis; automatic unit conversion is out of scope.

## Variable identity and catalog

`DbcDatabase::signal_names()` is not sufficient because signal names are only
unique within a message. Add a catalog descriptor (or equivalent API) with a
stable key:

```cpp
struct SignalKey {
    std::uint32_t can_id{};
    bool extended{};
    std::string message_name;
    std::string signal_name;

    friend bool operator==(const SignalKey&, const SignalKey&) = default;
};

struct SignalDescriptor {
    SignalKey key;
    std::string unit;
    double minimum{};
    double maximum{};
};
```

The key, not a display string, identifies a series. The renderer may display
`message_name.signal_name [0xID]`; it must retain `can_id`, `extended`, and the
signal name when making requests. A deterministic catalog order matching DBC
message/signal order is preferred.

## Proposed core API

Add `src/core/include/canpp/core/plot_data.hpp` and expose the following from
`Session`:

```cpp
enum class PlotSource { current_selection, full_trace };

struct PlotRequest {
    std::vector<protocol::can::SignalKey> variables;
    PlotSource source{PlotSource::current_selection};
};

struct PlotSample {
    std::uint64_t timestamp_ns{};
    std::optional<double> value; // nullopt means a gap
};

struct PlotSeries {
    protocol::can::SignalKey variable;
    std::string unit;
    std::vector<PlotSample> samples;
};

[[nodiscard]] std::vector<protocol::can::SignalDescriptor> plot_variables() const;
bool extract_plot_data(const PlotRequest&, std::vector<PlotSeries>& output,
                       std::string& error) const;
```

The exact namespace placement may follow existing core/protocol conventions,
but the request and returned series should be value types. `extract_plot_data`
should validate every requested key against the loaded DBC before changing
`output`; on failure it returns `false`, sets a specific error, and leaves the
caller’s previous graph intact.

## Extraction semantics

- `current_selection` walks `selection_` in its existing source-record order.
  `full_trace` walks every record in the reader, independent of filters.
- Each sample keeps the original `timestamp_ns`; convert to relative seconds
  only in the plotting layer to avoid precision loss in core data.
- For a matching CAN message, decode the requested signal to its physical
  value (`raw * factor + offset`) and append a value sample.
- For records of another message, truncated payloads, or inactive multiplexed
  signals, append a sample with `value == std::nullopt`. The renderer maps
  nullopt to a line break. This preserves time positions and does not invent
  values through interpolation.
- Returned series retain request order. Samples for all series use the same
  source-record traversal, so tooltips can align them by timestamp.
- Decode each CAN frame once per record and dispatch decoded values by its
  message-qualified key. Avoid calling `DbcDatabase::decode()` once per
  selected variable.
- An empty variable list, no open trace, no loaded DBC, unknown keys, and
  unreadable records are explicit errors. A valid trace with no records returns
  valid empty series.

## Render and plot architecture

`src/render/CMakeLists.txt` should define a render library only when
`ENABLE_RENDER` is enabled. The optional GUI application links that library,
`canpp::core`, SDL2/OpenGL, ImGui, and a plotting backend (ImPlot is the
current candidate). The CLI and core tests must not link any of these.

Suggested separation:

- `variable_selector`: catalog search, grouped checkboxes, source toggle, and
  request construction; no trace traversal.
- `plot_view`: owns graph-panel state (series visibility, view limits, reset,
  crosshair) and consumes `PlotSeries`; no DBC or `BinaryTraceReader` access.
- GUI controller: calls `Session::plot_variables()` and
  `Session::extract_plot_data()`, reports errors, and replaces graph data only
  after successful extraction.

For large traces, the MVP renders all extracted samples. A later phase should
add explicit min/max envelope decimation (without hiding spikes), preferably in
core or a dedicated data layer rather than in ImGui callbacks.

## Validation and error states

- No trace: `Open a communication trace before creating a graph.`
- No DBC: `Load a DBC before selecting variables.`
- No checked signals: disable **Create graph** and show `Select at least one
  signal.`
- Unknown/stale signal key: reject the request and identify the message, ID,
  and signal; never plot a different same-named signal.
- Reader/decode failure: keep the previous graph and show the record index and
  error. Do not silently skip corrupted records.
- A signal absent from a particular record is normal and is represented as a
  gap, not an error. A malformed record or reader I/O failure is an extraction
  error containing the source record index; if the decoder cannot distinguish
  these outcomes today, add a small status/result type rather than silently
  skipping all failed decodes.
- Reloading a DBC or trace clears checked keys and graph data. The controller
  tracks trace, DBC, and selection/filter revisions and only commits extracted
  data if all parts of the generation token are unchanged. This includes
  `reset`, ordinary filters, and range filters—not just opening a new file.

## Phased implementation

### Phase 1: core and MVP graph

1. Add message-qualified signal catalog descriptors and tests for duplicate
   names.
2. Add plot request/series types and `Session` extraction.
3. Move render-only dependency discovery in `externals/CMakeLists.txt` behind
   `ENABLE_RENDER`, define a render library in `src/render/CMakeLists.txt`, and
   add a minimal GUI entry point under `src/app/trace_gui` behind the option.
4. Add the plotting backend conditionally (ImPlot is a candidate; record its
   version/license and retain a no-render configuration), then implement the
   selector, source toggle, one graph panel, basic legend, pan/zoom, reset, and
   tooltips.
5. Document and smoke-test the GUI separately from headless CI.

### Phase 2: usability and scale

- Add extraction caching keyed by trace/DBC/selection/request.
- Add min/max decimation, unit-aware Y-axis grouping, graph tabs, and layout
  persistence.
- Add CSV export from the same `PlotSeries` data.

### Phase 3: analysis features

- Add saved graph definitions, scripted/CLI export, event markers,
  annotations, formulas, and additional protocol variable catalogs.

## Tests

Keep tests independent of rendering. Add a focused `plot_tests.cpp` fixture
with a temporary trace and DBC covering:

- two messages containing the same signal name and selecting each separately;
- factor/offset physical decoding and units;
- current-selection intersection versus full-trace extraction;
- nanosecond timestamp preservation and source order;
- missing messages, truncated payloads, and inactive multiplexed signals as
  gaps;
- empty requests, missing DBC/trace, unknown keys, and reader failures;
- empty traces and multiple requested series.

Add a render build smoke check when dependencies are available, while keeping
core tests runnable with `ENABLE_RENDER=OFF`.

## Risks and mitigations

- **Duplicate names:** use the full key in requests and legends; never resolve
  graph variables by name alone.
- **ImPlot dependency/licensing/fetch availability:** make it conditional and
  document the chosen version/license; preserve a build without rendering.
- **Large traces:** establish the no-interpolation semantics now, then add
  measured decimation in Phase 2.
- **Floating-point time precision:** retain integer nanoseconds through core and
  subtract a series/global origin before converting to `double` seconds.
- **Stale state after reload/filtering:** clear graph requests on trace/DBC
  reload, track controller-owned trace/DBC/selection generations, and replace
  data transactionally only when extraction completes for the current token.
- **Multiplexing and malformed payloads:** rely on decoder validity and encode
  unavailable values as gaps, with tests for each case.

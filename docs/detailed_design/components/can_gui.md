# `can_gui` Detailed Design

## Purpose

Provide the future interactive frontend over the same application services.

## Responsibilities

- manage GUI session state
- bind user interactions to query and navigation requests
- display raw and decoded data
- manage time navigation and filtering UX
- manage a full in-memory scanned row dataset and a separate current-match dataset

## Main Public Types

- `class GuiApplication`
- `class GuiSession`
- `class TraceTableViewModel`
- `class QueryPanelViewModel`
- `class TimelineViewModel`
- `class SignalPanelViewModel`

## Data Types

### `class GuiSession`

State:

- active query draft
- full scanned row dataset status
- current match result set
- selected event or message
- loaded trace metadata

### `struct GuiQueryState`

Fields:

- `QuerySpec querySpec`
- `std::uint64_t visibleStartTimestampNs`
- `std::uint64_t visibleEndTimestampNs`

## Public API

### `class GuiApplication`

- `int run()`
- `void update()`
- `void render()`

### `class TraceTableViewModel`

- `void startScan(const QuerySpec& querySpec)`
- `void startFilter(const QuerySpec& querySpec, FilterSource filterSource)`
- `void resetMatchesToFullDataset()`
- `std::span<const QueryResultRow> visibleRows() const`

### `class TimelineViewModel`

- `void zoomIn()`
- `void zoomOut()`
- `void moveToTimestamp(std::uint64_t timestampNs)`

## Main Internal Types

- `class ResultPageCache`
- `class SelectionState`
- `class FilterDraftBuilder`

## Internal Design

The GUI should separate:

- Dear ImGui widgets
- GUI view models
- application service requests

The GUI now uses a three-layer interaction model:

1. Scan phase:
   The selected trace file is read once into an in-memory vector of `QueryResultRow`.
   If scan-time decode is enabled, each stored row may also contain decoded DBC data.
2. Filter-from-full phase:
   The current filter draft is applied against the full in-memory dataset to rebuild the current match dataset without rereading the trace file.
3. Refine-current phase:
   Additional filters may be applied against the current match dataset only, allowing progressive narrowing until the user resets filters.

The full scanned dataset remains valid until the user requests a new scan. Resetting filters restores the current match dataset from the full scanned dataset. The GUI also presents a temporary action popup to distinguish file reads from in-memory filtering operations.

View models own GUI-facing state, the in-memory row datasets, and operation progress state for the frontend workflow.

## Performance Notes

- scanning may be long-running and should run asynchronously
- in-memory filtering avoids rereading the trace file for filter changes after a scan completes
- the full scanned dataset and the current match dataset are both GUI-owned RAM structures
- future cache and index services may still be used to support larger-than-memory workflows

## Verification

- view-model tests where practical
- smoke tests for scan, filter-from-full, refine-current, and reset journeys
- responsiveness checks on large traces

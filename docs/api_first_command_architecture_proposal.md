# API-first command architecture proposal

## Objective

Canpp must execute the same analysis sequence through every interface:

- interactive CLI/REPL;
- native terminal invocation with repeated arguments;
- script file or standard input;
- embedded C++ application;
- future TUI, Dear ImGui, web service, JSON/RPC, and automation client.

A command sequence such as:

```text
open trip.commtrace
load dbc vehicle.dbc
filter signal.VehicleSpeed > 80 original
print messages list grep "ADAS" limit 20
```

must have one parser/executor contract and identical session semantics in all
of those interfaces. Frontends must not duplicate filtering, DBC lookup,
pagination, history, or output-selection algorithms.

## Current state

The repository already has a reusable analysis core, but the command API is
currently trapped in the CLI:

```text
REPL line
  -> src/app/trace_cli/main.cpp::execute_line
  -> Session mutation or Session text-writing method
  -> CLI-specific grep/status/history/output logic
```

### Main issues

1. `Session` is both the stateful analysis model and a text renderer. Methods
   such as `print`, `print_dbc_messages`, and `status` write directly to an
   `std::ostream`, so another frontend would have to parse terminal text.
2. The CLI owns command parsing, argument validation, command dispatch,
   pagination/suffix parsing, output rules, history decisions, and `grep`.
3. The GUI calls `Session` directly and therefore does not share the command
   grammar or result model.
4. There is no argument or batch execution mode. The executable always starts
   a REPL; stdin redirection includes terminal-oriented behavior.
5. Command parsing currently has several different lexical paths (`split`,
   quote-aware token parsing, and raw expression extraction), so quoting rules
   depend on the command.
6. Command tests are mostly process/text assertions instead of direct tests of
   stateful command sequences and typed results.

## Target dependency direction

```text
protocol + trace libraries
          ↓
core: domain state, queries, typed trace/DBC/plot data
          ↓
application: command model, parser, executor, result model
          ↓
adapters: CLI REPL | argv | script | stdin | GUI | TUI | web | JSON
```

`core` must have no terminal, readline, shell, SDL, ImGui, HTTP, or JSON
parsing dependency. `application` must have no terminal or GUI dependency.
Only adapters format or transport results.

## Proposed modules

```text
src/core/
  include/canpp/core/
    session.hpp                # state and domain operations
    records.hpp                # typed trace/DBC row views
    query.hpp                  # existing trace query language
    plot_data.hpp              # existing plot request/result types

src/application/
  include/canpp/application/
    command.hpp                # command AST and shared options
    command_parser.hpp         # line and argv parser
    command_executor.hpp       # stateful dispatcher
    command_result.hpp         # typed result/error/snapshot
    command_registry.hpp       # help and completion metadata
    text_renderer.hpp          # compatibility terminal renderer
    json_renderer.hpp          # later, versioned structured output
  src/
    command_parser.cpp
    command_executor.cpp
    command_registry.cpp
    text_renderer.cpp

src/app/
  trace_cli/                   # terminal adapter only
  trace_gui/                   # ImGui adapter only
```

The `application` target should link `canpp::trace_core`; applications link
`canpp::application`. This is intentionally a small command/use-case layer,
not a second domain model.

## Core API: typed data instead of presentation

`Session` remains the owner of the loaded trace, DBC, current selection, and
domain algorithms. New code must query typed data rather than request text.

Representative types:

```cpp
struct Page {
    std::size_t limit{20};
    std::size_t offset{};
};

struct SessionSnapshot {
    std::uint64_t state_revision{};
    std::uint64_t trace_revision{};
    std::uint64_t dbc_revision{};
    std::uint64_t selection_revision{};
    bool trace_loaded{};
    bool dbc_loaded{};
    std::size_t trace_record_count{};
    std::size_t selection_count{};
};

struct DbcMessageInfo {
    std::uint32_t can_id{};
    bool extended{};
    std::string name;
    std::uint8_t payload_size{};
    std::size_t signal_count{};
};

struct DbcSignalInfo {
    protocol::can::SignalKey key; // message-qualified, never name-only
    std::uint8_t parent_payload_size{};
    std::uint16_t start_bit{};
    std::uint16_t bit_length{};
    protocol::can::DbcByteOrder byte_order{};
    bool is_signed{};
    double factor{};
    double offset{};
    double minimum{};
    double maximum{};
    std::string unit;
    MultiplexingInfo multiplexing;
    std::vector<EnumEntry> values;
};
```

Add typed `Session` read operations, for example:

```cpp
std::expected<SessionSnapshot, Error> snapshot() const;
std::expected<std::vector<DbcMessageInfo>, Error> dbc_messages(Page) const;
std::expected<std::vector<DbcSignalInfo>, Error> dbc_signals(Page) const;
std::expected<std::vector<TraceRow>, Error> trace_rows(TraceViewRequest) const;
std::expected<VariableTable, Error> variable_values(VariableRequest) const;
```

The exact names can follow repository conventions, but these APIs must return
values, not render into streams. Existing `Session::print_*` APIs should remain
temporarily as compatibility wrappers implemented from the typed APIs, then be
deprecated once every adapter has moved.

### State and revisions

Expose an immutable `SessionSnapshot` in every command result. Increment a
monotonic revision for trace changes, DBC changes, and selection changes. This
lets a future GUI, web request, or background export detect stale results
instead of displaying a plot/catalog based on a replaced trace.

Do **not** make multi-command scripts automatically transactional in the first
version. Scripts execute sequentially and stop on the first failure by default.
A true rollback transaction requires an explicit Session snapshot/restore
design and should be added only when needed.

## Shared command model

Define a typed command AST. Interfaces that already have structured input
(GUI, web, C++ callers) construct this AST directly; text interfaces parse into
it.

```cpp
struct OutputOptions {
    enum class Detail { full, list } detail{Detail::full};
    Page page{};
    std::optional<std::string> grep;
};

using Command = std::variant<
    OpenTrace,
    ImportCanAsc,
    LoadDbc,
    ResetSelection,
    FilterExpression,
    FilterRange,
    FilterCanId,
    FilterCanName,
    FilterCanSignal,
    SaveSelection,
    TracePrint,
    DbcStatus,
    DbcMessages,
    DbcVariables,
    DbcVariable,
    PlotVariables,
    ShowHistory,
    Exit>;
```

Every command owns only meaningful fields. For example, `DbcMessages` and
`DbcVariables` carry `OutputOptions`; `FilterExpression` carries its expression
and source (`current` or `original`); `TracePrint` carries a typed view/mode and
optional signal keys.

Avoid untyped maps, stringly-typed generic commands, or a public API that asks
GUI/web callers to fabricate shell command strings.

## Parse, execute, render

```text
CLI line / argv / script line / JSON request / GUI action
                         ↓
                    parser or codec
                         ↓
                    typed Command
                         ↓
             CommandExecutor(Session)
                         ↓
        typed CommandResult + SessionSnapshot
                         ↓
text renderer / JSON renderer / GUI model / web response
```

### Parser

A single quote-aware lexer/parser owns command grammar. It accepts:

- `parse_line(std::string_view line, SourceLocation)` for REPL and scripts;
- `parse_argv(std::span<const std::string_view>)` for `--command` input;
- later `decode_json_command(...)` for machine requests.

All parsing must use the same escaping, quoting, `list`, `grep`, `limit`, and
`offset rules. Expressions are passed as parser tokens or a well-defined raw
expression field; no command-specific reparsing in adapters.

### Executor

`CommandExecutor` is stateful only because it references a `Session`:

```cpp
class CommandExecutor {
public:
    explicit CommandExecutor(core::Session& session) noexcept;
    [[nodiscard]] CommandResult execute(const Command& command);
};
```

It performs semantic validation, calls `Session`, applies typed filtering and
pagination, updates the operation log, and returns data. It never writes to
stdout/stderr, reads terminal input, calls `system`, launches a GUI, or handles
signals.

### Result and errors

```cpp
struct Diagnostic {
    enum class Code { invalid_syntax, invalid_argument, no_trace, no_dbc,
                      not_found, io_error, unsupported, internal_error };
    Code code{};
    std::string message;
    std::optional<SourceLocation> location;
};

struct CommandResult {
    bool success{};
    bool exit_requested{};
    bool state_changed{};
    std::variant<std::monostate, StatusView, CatalogView, TraceTable,
                 VariableTable, ImportSummary, PlotSeries> data;
    std::vector<Diagnostic> diagnostics;
    core::SessionSnapshot snapshot;
};
```

A failed command must leave the Session unchanged unless the documented domain
operation itself is partially persistent; existing loading/filtering code
should be checked and made all-or-nothing at its boundary.

## Rendering and filtering

`TextRenderer` becomes the sole owner of current human-readable output. It
renders a `CommandResult` to an `std::ostream`, preserving today’s text format
where practical. `JsonRenderer` follows later with a versioned schema; CSV is
only for tabular results.

`grep` remains a text-output convenience for compatibility initially. It is
applied by the renderer to complete rendered lines, case-sensitively. It should
not be part of `Session`.

Structured filters (`where`) are a later typed predicate layer. They operate on
DBC descriptors or trace row fields **before** pagination/rendering. The
existing trace `Query` remains the foundation for trace-record filters;
metadata filtering requires its own typed field set. Do not implement a second
ad-hoc expression language.

## Interface adapters

### REPL

The CLI owns only prompt/readline completion, Ctrl+C cancellation, UI history,
and text rendering:

```cpp
while (read_line(line)) {
    render(executor.execute(parser.parse_line(line)));
}
```

Readline history is UI input history. It is separate from an optional typed
operation log. The core must not clear or persist terminal history.

### Native terminal and scripts

Add explicit noninteractive modes:

```bash
# Repeatable, ordered commands; shell handles each quoted command string.
Canpp --command 'open trip.commtrace' \
      --command 'load dbc vehicle.dbc' \
      --command 'filter signal.VehicleSpeed > 80 original' \
      --command 'print messages list grep ADAS limit 20'

# One command per logical line, using exactly the REPL grammar.
Canpp --script analysis.canpp

# Suitable for pipes and generated scripts. No banner or prompt.
Canpp --stdin < analysis.canpp
```

Rules:

- each route uses the shared parser and executor;
- commands run in source order against one Session;
- batch mode prints results to stdout and diagnostics to stderr;
- batch mode emits no greeting, prompt, or automatic status lines;
- stop on the first error and return a non-zero process code;
- a successful script returns zero; `exit` stops successfully;
- add `--continue-on-error` only after collecting use cases;
- `--format text` is the initial default; `--format json` arrives with the JSON
  renderer;
- launch/UI-only requests are adapter capabilities: CLI may launch a graph app,
  while other adapters can return `unsupported` without infecting core.

`--command` should be repeated rather than receiving arbitrary trailing argv.
It avoids ambiguity between process options and command tokens, preserves
spaces/expressions through shell quoting, and makes ordering explicit.

### ImGui / future TUI / web

A GUI action constructs `Command` values or calls lower-level typed Session
read APIs. It never parses text output. A web/RPC handler decodes a structured
request to the same `Command`, executes it, and serializes `CommandResult`.
Interactive visual concerns (window lifecycle, graph selection widgets,
cancellation display) remain in the GUI adapter.

## Command registry

Add one static registry describing every public command:

- canonical name and compatibility aliases;
- short help and syntax;
- argument metadata;
- whether it mutates Session state;
- whether it supports `OutputOptions`;
- completion candidates or providers;
- adapter capabilities required, if any.

The registry feeds `help`, generated documentation, readline completion,
script diagnostics, and test coverage. This prevents the current failure mode
where a command is added to REPL dispatch but is unavailable to a script or
future adapter.

## Migration plan

### Phase 0 — characterize current behavior

- Add focused tests for command parsing and state transitions currently hidden
  in `execute_line`.
- Record supported compatibility behavior for text output, errors, pagination,
  aliases, and history.
- No user-visible behavior change.

### Phase 1 — typed core read model

- Add typed Session results for status, DBC catalogs/details, trace rows, and
  variable tables.
- Move text formatting into a renderer while retaining old `print_*` wrappers.
- Add Session revisions/snapshots.
- Test typed data independently from terminal text.

### Phase 2 — shared parser and executor

- Add command AST, one lexer/parser, registry, executor, `CommandResult`, and
  renderer.
- Extract `execute_line` behavior into application layer without changing REPL
  grammar.
- Centralize command history policy and semantic validation.
- Add direct sequence tests: `open -> load dbc -> filter -> print`.

### Phase 3 — script/argv parity

- Make REPL a thin adapter over the shared executor.
- Add `--command`, `--script`, and `--stdin`.
- Add adapter-parity tests that compare state/result data and text output for
  the same command sequence through REPL, repeated `--command`, script file,
  and stdin batch.

### Phase 4 — structured output and other interfaces

- Add JSON rendering with a documented, versioned result schema.
- Add typed DBC/trace `where` predicates.
- Refactor ImGui to consume the shared typed APIs/results.
- Add cancellation tokens to long-running filters, exports, and plot
  extraction; the terminal Ctrl+C handler requests cancellation through the
  adapter rather than directly changing domain state.

### Phase 5 — cleanup

- Remove/deprecate direct stream-rendering methods only after all first-party
  interfaces use typed views.
- Add CSV exports for table result types and optional operation replay only if
  requirements justify them.

## Tests and acceptance criteria

1. **Parser:** quoted strings, expressions, aliases, suffix ordering, malformed
   input, and source locations.
2. **Executor:** direct C++ tests for stateful command sequences, errors that
   preserve state, pagination, duplicate signal names, DBC/trace absence, and
   history policy.
3. **Renderer:** golden tests for stable text and, later, JSON schema tests.
4. **Adapter parity:** the same sequence produces equal typed results/session
   snapshots across REPL, `--command`, `--script`, and `--stdin`.
5. **Registry contract:** each registered command has parser coverage, executor
   coverage, help text, and completion metadata.
6. **Build variants:** core/application tests work without readline or render
   dependencies; CLI integration tests remain conditional on the CLI target.

## Non-goals for the initial migration

- A generic plugin system.
- Automatic rollback for a multi-command script.
- A JSON parser implemented by hand.
- Web server, TUI, or RPC transport implementation.
- Breaking current CLI text grammar without an explicit compatibility decision.

## Recommended first implementation slice

Implement Phases 1 and 2 for the existing `open`, `load dbc`, `reset`,
`filter`, `print`, `dbc`, `status`, `history`, `save`, and `exit` commands;
leave terminal control (`Ctrl+C`, readline, prompt, shell GUI launch) in the
CLI adapter. Once direct command-executor tests pass, add `--command`,
`--script`, and `--stdin` as a small Phase 3 change rather than building a
second execution path.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <atomic>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "can_app/can_app.hpp"
#include "can_core/core_types.hpp"
#include "can_decode/decoder.hpp"

namespace can_gui
{
struct GuiQueryState
{
  can_core::QuerySpec querySpec;
  bool hasVisibleTimestampRange = false;
  bool hasVisibleOrdinalRange = false;
  std::uint64_t visibleStartTimestampNs = 0;
  std::uint64_t visibleEndTimestampNs = 0;
  std::uint64_t visibleStartOrdinal = 0;
  std::uint64_t visibleEndOrdinal = 0;
};

class GuiSession
{
public:
  std::string tracePath;
  std::string dbcPath;
  GuiQueryState queryState;
  can_app::RunSummary runSummary;
  can_core::TraceMetadata traceMetadata;
  std::size_t selectedRowIndex = 0;
  bool hasSelection = false;
  bool shouldDecodeMatches = false;
  bool autoRefresh = true;
  std::string statusMessage;
};

class TraceTableViewModel
{
public:
  enum class FilterSource
  {
    FullDataset,
    CurrentMatches,
  };

  enum class ActiveOperation
  {
    None,
    ScanFile,
    FilterFullDataset,
    FilterCurrentMatches,
  };

  struct ProgressSnapshot
  {
    std::uint64_t scannedEvents = 0;
    std::uint64_t matchedEvents = 0;
    std::uint64_t bytesParsed = 0;
    std::uint64_t totalBytes = 0;
  };

  void setRunOptions(can_app::RunOptions runOptions);
  void startScan(const GuiQueryState &queryState);
  void startFilter(const GuiQueryState &queryState, FilterSource filterSource);
  void resetMatchesToFullDataset();
  void cancelActiveOperation();
  [[nodiscard]] bool pollActiveOperation();
  [[nodiscard]] bool isOperationInProgress() const;
  [[nodiscard]] std::span<const can_app::QueryResultRow> visibleRows() const;
  [[nodiscard]] const can_app::RunSummary &runSummary() const;
  [[nodiscard]] const can_core::TraceMetadata &traceMetadata() const;
  [[nodiscard]] std::span<const std::uint8_t> visibleChannels() const;
  [[nodiscard]] bool hasDecodedRows() const;
  [[nodiscard]] bool hasFullDataset() const;
  [[nodiscard]] bool fullDatasetHasDecodedRows() const;
  [[nodiscard]] std::size_t fullRowCount() const;
  [[nodiscard]] bool wasLastOperationCancelled() const;
  [[nodiscard]] ActiveOperation activeOperation() const;
  [[nodiscard]] ProgressSnapshot progressSnapshot() const;
  [[nodiscard]] std::chrono::system_clock::time_point operationStartWallClock() const;
  [[nodiscard]] std::chrono::steady_clock::time_point operationStartSteadyClock() const;

private:
  struct SharedProgressState
  {
    std::atomic<std::uint64_t> scannedEvents{0};
    std::atomic<std::uint64_t> matchedEvents{0};
    std::atomic<std::uint64_t> bytesParsed{0};
    std::atomic<std::uint64_t> totalBytes{0};
  };

  struct RefreshSnapshot
  {
    std::vector<can_app::QueryResultRow> fullRows;
    std::vector<can_app::QueryResultRow> rows;
    can_app::RunSummary runSummary;
    can_core::TraceMetadata traceMetadata;
    std::vector<std::uint8_t> visibleChannels;
    bool hasDecodedRows = false;
    bool wasCancelled = false;
    bool shouldReplaceFullDataset = false;
    bool fullDatasetHasDecodedRows = false;
    ActiveOperation completedOperation = ActiveOperation::None;
  };

  can_app::CanApp canApp_;
  can_app::RunOptions runOptions_;
  std::vector<can_app::QueryResultRow> fullRows_;
  std::vector<can_app::QueryResultRow> rows_;
  can_app::RunSummary runSummary_;
  can_core::TraceMetadata traceMetadata_;
  std::vector<std::uint8_t> visibleChannels_;
  bool hasDecodedRows_ = false;
  bool hasFullDataset_ = false;
  bool fullDatasetHasDecodedRows_ = false;
  bool wasLastOperationCancelled_ = false;
  ActiveOperation activeOperation_ = ActiveOperation::None;
  ActiveOperation lastCompletedOperation_ = ActiveOperation::None;
  std::future<RefreshSnapshot> refreshFuture_;
  std::shared_ptr<std::atomic<bool>> cancellationFlag_;
  std::shared_ptr<SharedProgressState> progressState_;
  std::chrono::system_clock::time_point operationStartWallClock_{};
  std::chrono::steady_clock::time_point operationStartSteadyClock_{};
};

class QueryPanelViewModel
{
public:
  enum class ClauseMode
  {
    Must,
    Any,
    Exclude,
  };

  struct FilterRuleDraft
  {
    can_core::FilterField field = can_core::FilterField::CanId;
    can_core::FilterOperator filterOperator = can_core::FilterOperator::Equal;
    ClauseMode clauseMode = ClauseMode::Must;
    std::string valueText;
    bool enabled = true;
  };

  GuiQueryState buildQueryState() const;
  [[nodiscard]] can_app::RunOptions buildRunOptions() const;
  void resetToDefaults();

  std::string tracePath;
  std::string dbcPath;
  std::string timestampStartText;
  std::string timestampEndText;
  std::string ordinalStartText = "0";
  std::string ordinalEndText = "1999";
  std::string maxRowsText = "1000";
  bool enableTimestampRange = false;
  bool enableOrdinalRange = true;
  bool enableResultCap = true;
  bool shouldDecodeMatches = false;
  std::vector<FilterRuleDraft> rawRules;
  std::vector<FilterRuleDraft> decodedRules;
};

class TimelineViewModel
{
public:
  void setVisibleRange(std::uint64_t visibleStartTimestampNs, std::uint64_t visibleEndTimestampNs);
  void setBounds(std::uint64_t minimumTimestampNs, std::uint64_t maximumTimestampNs);
  void zoomIn();
  void zoomOut();
  void moveToTimestamp(std::uint64_t timestampNs);
  [[nodiscard]] std::uint64_t visibleStartTimestampNs() const;
  [[nodiscard]] std::uint64_t visibleEndTimestampNs() const;

private:
  void clampRange();

  std::uint64_t minimumTimestampNs_ = 0;
  std::uint64_t maximumTimestampNs_ = 0;
  std::uint64_t visibleStartTimestampNs_ = 0;
  std::uint64_t visibleEndTimestampNs_ = 0;
};

class SignalPanelViewModel
{
public:
  void setSelectedRow(const can_app::QueryResultRow *selectedRow);
  [[nodiscard]] const can_app::QueryResultRow *selectedRow() const;
  [[nodiscard]] std::span<const can_decode::DecodedSignal> visibleSignals() const;

private:
  const can_app::QueryResultRow *selectedRow_ = nullptr;
};

class GuiApplication
{
public:
  GuiApplication() = default;
  explicit GuiApplication(std::vector<std::string> arguments);

  int run();
  void update();
  void render();

private:
  bool initialize();
  void shutdown();
  void handlePendingRefresh();
  void renderMenuBar();
  void renderOverviewPanel();
  void renderQueryPanel();
  void renderTimelinePanel();
  void renderTraceTablePanel();
  void renderSignalPanel();
  void renderTransientActionPopup();
  void syncSessionFromDraft();
  void syncSelection();
  void showTransientActionPopup(const std::string &message);
  void triggerScanFile();
  void applyFiltersFromFullDataset();
  void applyFiltersToCurrentMatches();
  void resetFilters();
  void markFilterRefreshNeeded();
  void shiftOrdinalWindow(std::int64_t delta);
  void refreshDevelopDataFiles();
  void applySelectedDevelopFiles(bool shouldMarkRefresh);

  std::vector<std::string> arguments_;
  GuiSession guiSession_;
  QueryPanelViewModel queryPanelViewModel_;
  TraceTableViewModel traceTableViewModel_;
  TimelineViewModel timelineViewModel_;
  SignalPanelViewModel signalPanelViewModel_;
  std::vector<std::filesystem::path> availableTraceFiles_;
  std::vector<std::filesystem::path> availableDbcFiles_;
  int selectedTraceFileIndex_ = -1;
  int selectedDbcFileIndex_ = -1;
  bool isRunning_ = true;
  bool isInitialized_ = false;
  bool isRefreshPending_ = false;
  std::string transientPopupMessage_;
  std::chrono::steady_clock::time_point transientPopupExpiry_{};
  std::chrono::steady_clock::time_point lastEditTime_{};
};
} // namespace can_gui

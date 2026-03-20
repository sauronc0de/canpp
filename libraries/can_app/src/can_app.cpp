#include "can_app/can_app.hpp"

#include <memory>
#include <utility>

#include "can_core/core_types.hpp"
#include "can_dbc/database.hpp"
#include "can_export/exporter.hpp"
#include "can_query/query_executor.hpp"
#include "can_readers_text/text_trace_reader.hpp"

namespace can_app
{
namespace
{
class CallbackResultSink : public can_query::IResultSink
{
public:
  explicit CallbackResultSink(QueryResultCallback queryResultCallback)
    : queryResultCallback_(std::move(queryResultCallback))
  {
  }

  void onMatch(const can_query::QueryMatch &queryMatch) override
  {
    QueryResultRow queryResultRow;
    queryResultRow.ordinal = queryMatch.ordinal;
    queryResultRow.canEvent = queryMatch.canEvent;
    queryResultRow.decodedMessage = queryMatch.decodedMessage;
    queryResultCallback_(queryResultRow);
  }

private:
  QueryResultCallback queryResultCallback_;
};

class ExportingResultSink : public can_query::IResultSink
{
public:
  ExportingResultSink(QueryResultCallback queryResultCallback, const std::optional<can_export::ExportRequest> &exportRequest)
    : queryResultCallback_(std::move(queryResultCallback))
  {
    if(exportRequest.has_value())
    {
      isExportEnabled_ = exporter_.open(*exportRequest);
      exportMode_ = exportRequest->exportMode;
      if(!isExportEnabled_)
      {
        exportSummary_ = exporter_.close();
      }
    }
  }

  ~ExportingResultSink() override
  {
    finalize();
  }

  void onMatch(const can_query::QueryMatch &queryMatch) override
  {
    QueryResultRow queryResultRow;
    queryResultRow.ordinal = queryMatch.ordinal;
    queryResultRow.canEvent = queryMatch.canEvent;
    queryResultRow.decodedMessage = queryMatch.decodedMessage;
    queryResultCallback_(queryResultRow);

    if(!isExportEnabled_)
    {
      return;
    }

    if(exportMode_ == can_export::ExportMode::RawCsv)
    {
      if(!exporter_.writeRaw(queryMatch.canEvent))
      {
        exportSummary_ = exporter_.close();
        isExportEnabled_ = false;
      }
      return;
    }

    if(queryMatch.decodedMessage.has_value())
    {
      if(!exporter_.writeDecoded(*queryMatch.decodedMessage, queryMatch.canEvent))
      {
        exportSummary_ = exporter_.close();
        isExportEnabled_ = false;
      }
    }
  }

  [[nodiscard]] can_export::ExportSummary exportSummary() const
  {
    return exportSummary_;
  }

  void finalize()
  {
    if(!isExportEnabled_)
    {
      return;
    }

    exportSummary_ = exporter_.close();
    isExportEnabled_ = false;
  }

private:
  QueryResultCallback queryResultCallback_;
  can_export::Exporter exporter_;
  can_export::ExportMode exportMode_ = can_export::ExportMode::RawCsv;
  can_export::ExportSummary exportSummary_;
  bool isExportEnabled_ = false;
};

std::unique_ptr<can_reader_api::ITraceReader> createReader(const can_reader_api::SourceDescriptor &sourceDescriptor)
{
  const can_readers_text::CandumpReaderFactory candumpReaderFactory;
  if(candumpReaderFactory.canOpen(sourceDescriptor))
  {
    return candumpReaderFactory.create();
  }

  const can_readers_text::CsvTraceReaderFactory csvTraceReaderFactory;
  if(csvTraceReaderFactory.canOpen(sourceDescriptor))
  {
    return csvTraceReaderFactory.create();
  }

  const can_readers_text::AscTraceReaderFactory ascTraceReaderFactory;
  if(ascTraceReaderFactory.canOpen(sourceDescriptor))
  {
    return ascTraceReaderFactory.create();
  }

  return nullptr;
}

can_reader_api::SourceDescriptor makeSourceDescriptor(const RunOptions &runOptions)
{
  can_reader_api::SourceDescriptor sourceDescriptor;
  sourceDescriptor.path = runOptions.tracePath;
  const std::size_t extensionSeparator = runOptions.tracePath.find_last_of('.');
  if(extensionSeparator != std::string::npos)
  {
    sourceDescriptor.extension = runOptions.tracePath.substr(extensionSeparator);
  }
  return sourceDescriptor;
}

can_core::QuerySpec makeQuerySpec(const RunOptions &runOptions)
{
  can_core::QuerySpec querySpec;
  querySpec.shouldDecode = runOptions.shouldDecodeMatches;
  querySpec.shouldReturnRaw = true;

  if(runOptions.canIdFilter.has_value())
  {
    can_core::Predicate predicate;
    predicate.field = can_core::FilterField::CanId;
    predicate.filterOperator = can_core::FilterOperator::Equal;
    predicate.value = static_cast<std::uint64_t>(*runOptions.canIdFilter);
    querySpec.rawFilter = can_core::FilterExpr::makePredicate(std::move(predicate));
  }

  return querySpec;
}
} // namespace

RunSummary CanApp::run(const RunOptions &runOptions, const QueryResultCallback &queryResultCallback) const
{
  RunSummary runSummary;
  const can_reader_api::SourceDescriptor sourceDescriptor = makeSourceDescriptor(runOptions);
  std::unique_ptr<can_reader_api::ITraceReader> traceReader = createReader(sourceDescriptor);
  if(traceReader == nullptr)
  {
    runSummary.errorInfo.code = can_core::ErrorCode::UnsupportedFormat;
    runSummary.errorInfo.message = "No text reader is available for the provided trace path";
    return runSummary;
  }

  can_reader_api::ReaderOptions readerOptions;
  if(!traceReader->open(sourceDescriptor, readerOptions))
  {
    runSummary.errorInfo.code = can_core::ErrorCode::IoFailure;
    runSummary.errorInfo.message = "Unable to open the trace source";
    return runSummary;
  }

  can_dbc::Database database;
  const can_dbc::Database *databasePointer = nullptr;
  if(runOptions.dbcPath.has_value())
  {
    can_dbc::DbcLoader dbcLoader;
    const can_dbc::LoadResult loadResult = dbcLoader.loadFromFile(*runOptions.dbcPath);
    if(loadResult.hasError())
    {
      traceReader->close();
      runSummary.errorInfo = loadResult.errorInfo;
      return runSummary;
    }

    database = loadResult.database;
    databasePointer = &database;
  }

  can_decode::Decoder decoder(databasePointer);
  can_query::QueryPlanner queryPlanner;
  const can_query::CompiledQuery compiledQuery = queryPlanner.compile(makeQuerySpec(runOptions));

  can_query::QueryExecutionOptions queryExecutionOptions;
  queryExecutionOptions.shouldDecodeMatches = runOptions.shouldDecodeMatches;

  ExportingResultSink callbackResultSink(queryResultCallback, runOptions.exportRequest);
  can_query::QueryExecutor queryExecutor(&decoder);
  const can_query::QuerySummary querySummary =
    queryExecutor.execute(compiledQuery, *traceReader, callbackResultSink, queryExecutionOptions);
  callbackResultSink.finalize();

  traceReader->close();
  runSummary.scannedEvents = querySummary.scannedEvents;
  runSummary.matchedEvents = querySummary.matchedEvents;
  runSummary.errorInfo = querySummary.errorInfo;
  if(!runSummary.hasError() && callbackResultSink.exportSummary().hasError())
  {
    runSummary.errorInfo = callbackResultSink.exportSummary().errorInfo;
  }
  return runSummary;
}
} // namespace can_app

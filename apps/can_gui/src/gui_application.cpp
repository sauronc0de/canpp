#include "can_gui/gui_application.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>
#include <utility>

#include <GL/glew.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

namespace can_gui
{
namespace
{
constexpr std::size_t kInputBufferSize = 256U;
constexpr std::size_t kMaximumVisibleRows = 5000U;
constexpr std::uint64_t kDefaultOrdinalWindowSize = 2000U;
constexpr int kWindowWidth = 1600;
constexpr int kWindowHeight = 960;
constexpr auto kRefreshDebounce = std::chrono::milliseconds(250);

SDL_Window *g_window = nullptr;
SDL_GLContext g_glContext = nullptr;

std::optional<std::uint64_t> parseUnsignedInteger(std::string_view valueText)
{
  if(valueText.empty())
  {
    return std::nullopt;
  }

  std::uint64_t parsedValue = 0;
  const char *begin = valueText.data();
  const char *end = valueText.data() + valueText.size();
  if(valueText.size() > 2U && valueText[0] == '0' && (valueText[1] == 'x' || valueText[1] == 'X'))
  {
    begin += 2;
    const auto [pointer, errorCode] = std::from_chars(begin, end, parsedValue, 16);
    if(errorCode != std::errc{} || pointer != end)
    {
      return std::nullopt;
    }
    return parsedValue;
  }

  const auto [pointer, errorCode] = std::from_chars(begin, end, parsedValue, 10);
  if(errorCode != std::errc{} || pointer != end)
  {
    return std::nullopt;
  }

  return parsedValue;
}

std::optional<double> parseDouble(std::string_view valueText)
{
  if(valueText.empty())
  {
    return std::nullopt;
  }

  char *endPointer = nullptr;
  const std::string value(valueText);
  const double parsedValue = std::strtod(value.c_str(), &endPointer);
  if(endPointer != value.c_str() + value.size())
  {
    return std::nullopt;
  }

  return parsedValue;
}

bool inputText(const char *label, std::string &value, ImGuiInputTextFlags inputTextFlags = 0)
{
  std::array<char, kInputBufferSize> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
  if(!ImGui::InputText(label, buffer.data(), buffer.size(), inputTextFlags))
  {
    return false;
  }

  value = buffer.data();
  return true;
}

can_core::FilterExpr makePredicateExpr(
  can_core::FilterField field,
  can_core::FilterOperator filterOperator,
  can_core::FilterValue value)
{
  can_core::Predicate predicate;
  predicate.field = field;
  predicate.filterOperator = filterOperator;
  predicate.value = std::move(value);
  return can_core::FilterExpr::makePredicate(std::move(predicate));
}

std::string trimCopy(std::string_view text)
{
  std::size_t begin = 0U;
  while(begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0)
  {
    ++begin;
  }

  std::size_t end = text.size();
  while(end > begin && std::isspace(static_cast<unsigned char>(text[end - 1U])) != 0)
  {
    --end;
  }

  return std::string(text.substr(begin, end - begin));
}

bool isStringField(can_core::FilterField field)
{
  return field == can_core::FilterField::MessageName || field == can_core::FilterField::SignalName;
}

bool isDecodedField(can_core::FilterField field)
{
  return field == can_core::FilterField::MessageName || field == can_core::FilterField::SignalName ||
    field == can_core::FilterField::SignalValue;
}

const char *filterFieldLabel(can_core::FilterField field)
{
  switch(field)
  {
  case can_core::FilterField::TimestampNs:
    return "Timestamp";
  case can_core::FilterField::CanId:
    return "CAN ID";
  case can_core::FilterField::Channel:
    return "Channel";
  case can_core::FilterField::FrameType:
    return "Frame";
  case can_core::FilterField::MessageName:
    return "Message";
  case can_core::FilterField::SignalName:
    return "Signal";
  case can_core::FilterField::SignalValue:
    return "Signal Value";
  }

  return "Unknown";
}

const char *filterOperatorLabel(can_core::FilterOperator filterOperator)
{
  switch(filterOperator)
  {
  case can_core::FilterOperator::Equal:
    return "=";
  case can_core::FilterOperator::NotEqual:
    return "!=";
  case can_core::FilterOperator::Less:
    return "<";
  case can_core::FilterOperator::LessOrEqual:
    return "<=";
  case can_core::FilterOperator::Greater:
    return ">";
  case can_core::FilterOperator::GreaterOrEqual:
    return ">=";
  case can_core::FilterOperator::Contains:
    return "contains";
  }

  return "?";
}

can_core::FilterOperator defaultFilterOperator(can_core::FilterField field)
{
  return isStringField(field) ? can_core::FilterOperator::Contains : can_core::FilterOperator::Equal;
}

std::optional<can_core::FilterValue> parseFilterValue(can_core::FilterField field, std::string_view valueText)
{
  const std::string trimmedValue = trimCopy(valueText);
  if(trimmedValue.empty())
  {
    return std::nullopt;
  }

  switch(field)
  {
  case can_core::FilterField::TimestampNs:
  case can_core::FilterField::CanId:
  case can_core::FilterField::Channel:
    return parseUnsignedInteger(trimmedValue);
  case can_core::FilterField::FrameType:
  {
    std::string lowerValue = trimmedValue;
    std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
    if(lowerValue == "can" || lowerValue == "can20" || lowerValue == "2.0")
    {
      return static_cast<std::uint64_t>(static_cast<std::uint8_t>(can_core::FrameType::Can20));
    }
    if(lowerValue == "fd" || lowerValue == "canfd" || lowerValue == "can fd")
    {
      return static_cast<std::uint64_t>(static_cast<std::uint8_t>(can_core::FrameType::CanFd));
    }
    return std::nullopt;
  }
  case can_core::FilterField::MessageName:
  case can_core::FilterField::SignalName:
    return trimmedValue;
  case can_core::FilterField::SignalValue:
    return parseDouble(trimmedValue);
  }

  return std::nullopt;
}

struct ExpandedRuleClause
{
  QueryPanelViewModel::ClauseMode clauseMode = QueryPanelViewModel::ClauseMode::Must;
  std::string valueText;
};

std::vector<ExpandedRuleClause> expandRuleClauses(const QueryPanelViewModel::FilterRuleDraft &rule)
{
  std::vector<ExpandedRuleClause> expandedClauses;
  const std::string trimmedValue = trimCopy(rule.valueText);
  if(trimmedValue.empty())
  {
    return expandedClauses;
  }

  if(trimmedValue.find('&') == std::string::npos && trimmedValue.find('|') == std::string::npos &&
    trimmedValue.find(',') == std::string::npos)
  {
    expandedClauses.push_back({rule.clauseMode, trimmedValue});
    return expandedClauses;
  }

  const bool containsAnd = trimmedValue.find('&') != std::string::npos;
  const bool containsOr = trimmedValue.find('|') != std::string::npos || trimmedValue.find(',') != std::string::npos;
  if(containsAnd && containsOr)
  {
    expandedClauses.push_back({rule.clauseMode, trimmedValue});
    return expandedClauses;
  }

  QueryPanelViewModel::ClauseMode defaultMode = rule.clauseMode;
  if(containsOr && rule.clauseMode != QueryPanelViewModel::ClauseMode::Exclude)
  {
    defaultMode = QueryPanelViewModel::ClauseMode::Any;
  }

  std::size_t tokenStart = 0U;
  const auto flushToken = [&](std::size_t tokenEnd)
  {
    std::string token = trimCopy(std::string_view(trimmedValue).substr(tokenStart, tokenEnd - tokenStart));
    if(token.empty())
    {
      return;
    }

    QueryPanelViewModel::ClauseMode tokenMode = defaultMode;
    if(!token.empty() && (token.front() == '!' || token.front() == '-'))
    {
      tokenMode = QueryPanelViewModel::ClauseMode::Exclude;
      token.erase(token.begin());
      token = trimCopy(token);
    }

    if(!token.empty())
    {
      expandedClauses.push_back({tokenMode, std::move(token)});
    }
  };

  for(std::size_t index = 0U; index < trimmedValue.size(); ++index)
  {
    const char character = trimmedValue[index];
    if(character == '&' || character == '|' || character == ',')
    {
      flushToken(index);
      tokenStart = index + 1U;
    }
  }
  flushToken(trimmedValue.size());
  return expandedClauses;
}

std::optional<can_core::FilterExpr> buildRuleFilter(
  const std::vector<QueryPanelViewModel::FilterRuleDraft> &rules,
  bool *requiresDecode = nullptr)
{
  std::vector<can_core::FilterExpr> mustClauses;
  std::vector<can_core::FilterExpr> anyClauses;
  std::vector<can_core::FilterExpr> excludeClauses;

  for(const QueryPanelViewModel::FilterRuleDraft &rule : rules)
  {
    if(!rule.enabled)
    {
      continue;
    }

    for(const ExpandedRuleClause &expandedClause : expandRuleClauses(rule))
    {
      const auto parsedValue = parseFilterValue(rule.field, expandedClause.valueText);
      if(!parsedValue.has_value())
      {
        continue;
      }

      if(requiresDecode != nullptr && isDecodedField(rule.field))
      {
        *requiresDecode = true;
      }

      can_core::FilterExpr predicateExpr = makePredicateExpr(rule.field, rule.filterOperator, *parsedValue);
      switch(expandedClause.clauseMode)
      {
      case QueryPanelViewModel::ClauseMode::Must:
        mustClauses.push_back(std::move(predicateExpr));
        break;
      case QueryPanelViewModel::ClauseMode::Any:
        anyClauses.push_back(std::move(predicateExpr));
        break;
      case QueryPanelViewModel::ClauseMode::Exclude:
      {
        can_core::FilterExpr notExpr;
        notExpr.logicalOperator = can_core::LogicalOperator::Not;
        notExpr.children.push_back(std::move(predicateExpr));
        excludeClauses.push_back(std::move(notExpr));
        break;
      }
      }
    }
  }

  std::vector<can_core::FilterExpr> children;
  children.reserve(mustClauses.size() + excludeClauses.size() + (anyClauses.empty() ? 0U : 1U));
  for(can_core::FilterExpr &mustClause : mustClauses)
  {
    children.push_back(std::move(mustClause));
  }
  if(!anyClauses.empty())
  {
    can_core::FilterExpr orExpr;
    orExpr.logicalOperator = can_core::LogicalOperator::Or;
    orExpr.children = std::move(anyClauses);
    children.push_back(std::move(orExpr));
  }
  for(can_core::FilterExpr &excludeClause : excludeClauses)
  {
    children.push_back(std::move(excludeClause));
  }

  if(children.empty())
  {
    return std::nullopt;
  }
  if(children.size() == 1U)
  {
    return std::move(children.front());
  }

  can_core::FilterExpr filterExpr;
  filterExpr.logicalOperator = can_core::LogicalOperator::And;
  filterExpr.children = std::move(children);
  return filterExpr;
}

const char *frameTypeLabel(can_core::FrameType frameType)
{
  switch(frameType)
  {
  case can_core::FrameType::Can20:
    return "CAN 2.0";
  case can_core::FrameType::CanFd:
    return "CAN FD";
  }

  return "Unknown";
}

std::string formatCanId(std::uint32_t canId)
{
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "0x%X", canId);
  return buffer;
}

std::string formatPayload(const can_core::CanEvent &canEvent)
{
  std::ostringstream stream;
  stream << std::hex << std::uppercase;
  for(std::size_t index = 0; index < canEvent.dlc; ++index)
  {
    if(index != 0U)
    {
      stream << ' ';
    }
    stream.width(2);
    stream.fill('0');
    stream << static_cast<unsigned int>(canEvent.payload[index]);
  }
  return stream.str();
}

std::string formatSignalValue(const can_decode::DecodedSignalValue &decodedSignalValue)
{
  return std::visit(
    [](const auto &value)
    {
      std::ostringstream stream;
      stream << value;
      return stream.str();
    },
    decodedSignalValue);
}

std::string formatSignalDisplay(const can_decode::DecodedSignal &decodedSignal)
{
  std::string formattedValue = formatSignalValue(decodedSignal.value);
  if(decodedSignal.valueDescription.has_value())
  {
    formattedValue += " (";
    formattedValue += *decodedSignal.valueDescription;
    formattedValue += ")";
  }

  if(!decodedSignal.unit.empty())
  {
    formattedValue += ' ';
    formattedValue += decodedSignal.unit;
  }

  return formattedValue;
}

bool hasAnyTraceSource(const QueryPanelViewModel &queryPanelViewModel)
{
  return !queryPanelViewModel.tracePath.empty();
}

void textUnformatted(std::string_view text)
{
  ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

[[maybe_unused]] std::string lowerExtension(const std::filesystem::path &path)
{
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return extension;
}

[[maybe_unused]] bool isCompatibleTraceExtension(const std::filesystem::path &path)
{
  const std::string extension = lowerExtension(path);
  return extension == ".asc" || extension == ".csv" || extension == ".log" || extension == ".candump" || extension == ".txt";
}

[[maybe_unused]] bool isCompatibleDbcExtension(const std::filesystem::path &path)
{
  return lowerExtension(path) == ".dbc";
}

std::span<const can_core::FilterField> availableRawFields()
{
  static constexpr std::array<can_core::FilterField, 4> fields = {
    can_core::FilterField::CanId,
    can_core::FilterField::TimestampNs,
    can_core::FilterField::Channel,
    can_core::FilterField::FrameType,
  };
  return fields;
}

std::span<const can_core::FilterField> availableDecodedFields()
{
  static constexpr std::array<can_core::FilterField, 3> fields = {
    can_core::FilterField::MessageName,
    can_core::FilterField::SignalName,
    can_core::FilterField::SignalValue,
  };
  return fields;
}

std::span<const can_core::FilterOperator> availableOperators(can_core::FilterField field)
{
  static constexpr std::array<can_core::FilterOperator, 6> numericOperators = {
    can_core::FilterOperator::Equal,
    can_core::FilterOperator::NotEqual,
    can_core::FilterOperator::Less,
    can_core::FilterOperator::LessOrEqual,
    can_core::FilterOperator::Greater,
    can_core::FilterOperator::GreaterOrEqual,
  };
  static constexpr std::array<can_core::FilterOperator, 3> stringOperators = {
    can_core::FilterOperator::Contains,
    can_core::FilterOperator::Equal,
    can_core::FilterOperator::NotEqual,
  };
  return isStringField(field) ? std::span<const can_core::FilterOperator>(stringOperators)
                              : std::span<const can_core::FilterOperator>(numericOperators);
}

int fieldIndexFor(can_core::FilterField field, std::span<const can_core::FilterField> fields)
{
  for(std::size_t index = 0U; index < fields.size(); ++index)
  {
    if(fields[index] == field)
    {
      return static_cast<int>(index);
    }
  }
  return 0;
}

int operatorIndexFor(can_core::FilterOperator filterOperator, std::span<const can_core::FilterOperator> operators)
{
  for(std::size_t index = 0U; index < operators.size(); ++index)
  {
    if(operators[index] == filterOperator)
    {
      return static_cast<int>(index);
    }
  }
  return 0;
}

bool renderRuleEditor(
  const char *sectionId,
  const char *emptyLabel,
  std::vector<QueryPanelViewModel::FilterRuleDraft> &rules,
  std::span<const can_core::FilterField> availableFields)
{
  bool wasEdited = false;
  ImGui::PushID(sectionId);

  if(rules.empty())
  {
    ImGui::TextDisabled("%s", emptyLabel);
  }

  for(std::size_t index = 0U; index < rules.size(); ++index)
  {
    QueryPanelViewModel::FilterRuleDraft &rule = rules[index];
    ImGui::PushID(static_cast<int>(index));

    if(ImGui::Checkbox("##enabled", &rule.enabled))
    {
      wasEdited = true;
    }
    ImGui::SameLine();

    const char *clauseLabels[] = {"Show", "Any Of", "Hide"};
    int clauseModeIndex = static_cast<int>(rule.clauseMode);
    ImGui::SetNextItemWidth(90.0F);
    if(ImGui::Combo("##mode", &clauseModeIndex, clauseLabels, IM_ARRAYSIZE(clauseLabels)))
    {
      rule.clauseMode = static_cast<QueryPanelViewModel::ClauseMode>(clauseModeIndex);
      wasEdited = true;
    }
    ImGui::SameLine();

    int fieldIndex = fieldIndexFor(rule.field, availableFields);
    std::vector<const char *> fieldLabels;
    fieldLabels.reserve(availableFields.size());
    for(const can_core::FilterField field : availableFields)
    {
      fieldLabels.push_back(filterFieldLabel(field));
    }
    ImGui::SetNextItemWidth(115.0F);
    if(ImGui::Combo("##field", &fieldIndex, fieldLabels.data(), static_cast<int>(fieldLabels.size())))
    {
      rule.field = availableFields[static_cast<std::size_t>(fieldIndex)];
      rule.filterOperator = defaultFilterOperator(rule.field);
      wasEdited = true;
    }
    ImGui::SameLine();

    const auto operators = availableOperators(rule.field);
    std::vector<const char *> operatorLabels;
    operatorLabels.reserve(operators.size());
    for(const can_core::FilterOperator filterOperator : operators)
    {
      operatorLabels.push_back(filterOperatorLabel(filterOperator));
    }
    int operatorIndex = operatorIndexFor(rule.filterOperator, operators);
    ImGui::SetNextItemWidth(90.0F);
    if(ImGui::Combo("##operator", &operatorIndex, operatorLabels.data(), static_cast<int>(operatorLabels.size())))
    {
      rule.filterOperator = operators[static_cast<std::size_t>(operatorIndex)];
      wasEdited = true;
    }
    ImGui::SameLine();

    ImGui::SetNextItemWidth(-34.0F);
    if(inputText("##value", rule.valueText))
    {
      wasEdited = true;
    }
    ImGui::SameLine();
    if(ImGui::Button("X"))
    {
      rules.erase(rules.begin() + static_cast<std::ptrdiff_t>(index));
      ImGui::PopID();
      ImGui::PopID();
      return true;
    }
    ImGui::PopID();
  }

  if(ImGui::Button((std::string("Add Rule##") + sectionId).c_str()))
  {
    const can_core::FilterField defaultField = availableFields.empty() ? can_core::FilterField::CanId : availableFields.front();
    rules.push_back({defaultField, defaultFilterOperator(defaultField), QueryPanelViewModel::ClauseMode::Must, "", true});
    wasEdited = true;
  }
  ImGui::PopID();
  return wasEdited;
}

bool renderCompactRangeInputs(
  const char *label,
  bool &enabled,
  std::string &startValue,
  std::string &endValue,
  const char *startHint,
  const char *endHint)
{
  bool wasEdited = false;
  if(ImGui::Checkbox(label, &enabled))
  {
    wasEdited = true;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!enabled);
  ImGui::SetNextItemWidth(110.0F);
  if(inputText(startHint, startValue))
  {
    wasEdited = true;
  }
  ImGui::SameLine();
  ImGui::TextUnformatted("to");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110.0F);
  if(inputText(endHint, endValue))
  {
    wasEdited = true;
  }
  ImGui::EndDisabled();
  return wasEdited;
}
} // namespace

void TraceTableViewModel::setRunOptions(can_app::RunOptions runOptions)
{
  runOptions_ = std::move(runOptions);
}

void TraceTableViewModel::startRefresh(const can_core::QuerySpec &querySpec)
{
  cancelRefresh();

  can_app::RunOptions runOptions = runOptions_;
  runOptions.rawFilter.reset();
  runOptions.decodedFilter.reset();
  runOptions.shouldDecodeMatches = querySpec.shouldDecode;
  if(!querySpec.rawFilter.children.empty() || querySpec.rawFilter.predicate.has_value())
  {
    runOptions.rawFilter = querySpec.rawFilter;
  }
  if(querySpec.decodedFilter.has_value())
  {
    runOptions.decodedFilter = querySpec.decodedFilter;
  }

  cancellationFlag_ = std::make_shared<std::atomic<bool>>(false);
  runOptions.shouldCancel = cancellationFlag_.get();
  refreshFuture_ = std::async(
    std::launch::async,
    [this, runOptions]() mutable
    {
      RefreshSnapshot refreshSnapshot;
      refreshSnapshot.runSummary = canApp_.run(
        runOptions,
        [&refreshSnapshot](const can_app::QueryResultRow &queryResultRow)
        {
          if(refreshSnapshot.rows.size() < kMaximumVisibleRows)
          {
            refreshSnapshot.rows.push_back(queryResultRow);
          }

          refreshSnapshot.traceMetadata.startTimestampNs = refreshSnapshot.rows.size() == 1U
            ? queryResultRow.canEvent.timestampNs
            : std::min(refreshSnapshot.traceMetadata.startTimestampNs, queryResultRow.canEvent.timestampNs);
          refreshSnapshot.traceMetadata.endTimestampNs =
            std::max(refreshSnapshot.traceMetadata.endTimestampNs, queryResultRow.canEvent.timestampNs);

          if(std::find(
               refreshSnapshot.visibleChannels.begin(),
               refreshSnapshot.visibleChannels.end(),
               queryResultRow.canEvent.channel) == refreshSnapshot.visibleChannels.end())
          {
            refreshSnapshot.visibleChannels.push_back(queryResultRow.canEvent.channel);
            std::sort(refreshSnapshot.visibleChannels.begin(), refreshSnapshot.visibleChannels.end());
          }

          refreshSnapshot.hasDecodedRows = refreshSnapshot.hasDecodedRows || queryResultRow.decodedMessage.has_value();
        });

      refreshSnapshot.traceMetadata.sourcePath = runOptions.tracePath;
      refreshSnapshot.traceMetadata.eventCount = refreshSnapshot.runSummary.matchedEvents;
      refreshSnapshot.traceMetadata.sourceFormat =
        runOptions.tracePath.substr(runOptions.tracePath.find_last_of('.') == std::string::npos
            ? runOptions.tracePath.size()
            : runOptions.tracePath.find_last_of('.'));
      refreshSnapshot.wasCancelled = refreshSnapshot.runSummary.wasCancelled;
      return refreshSnapshot;
    });
}

void TraceTableViewModel::cancelRefresh()
{
  if(cancellationFlag_ != nullptr)
  {
    cancellationFlag_->store(true);
  }
}

bool TraceTableViewModel::pollRefresh()
{
  if(!refreshFuture_.valid())
  {
    return false;
  }

  if(refreshFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
  {
    return false;
  }

  RefreshSnapshot refreshSnapshot = refreshFuture_.get();
  cancellationFlag_.reset();
  wasLastRefreshCancelled_ = refreshSnapshot.wasCancelled;
  if(refreshSnapshot.wasCancelled)
  {
    return true;
  }

  rows_ = std::move(refreshSnapshot.rows);
  runSummary_ = refreshSnapshot.runSummary;
  traceMetadata_ = std::move(refreshSnapshot.traceMetadata);
  visibleChannels_ = std::move(refreshSnapshot.visibleChannels);
  hasDecodedRows_ = refreshSnapshot.hasDecodedRows;
  return true;
}

bool TraceTableViewModel::isRefreshInProgress() const
{
  return refreshFuture_.valid() &&
    refreshFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
}

std::span<const can_app::QueryResultRow> TraceTableViewModel::visibleRows() const
{
  return rows_;
}

const can_app::RunSummary &TraceTableViewModel::runSummary() const
{
  return runSummary_;
}

const can_core::TraceMetadata &TraceTableViewModel::traceMetadata() const
{
  return traceMetadata_;
}

std::span<const std::uint8_t> TraceTableViewModel::visibleChannels() const
{
  return visibleChannels_;
}

bool TraceTableViewModel::hasDecodedRows() const
{
  return hasDecodedRows_;
}

bool TraceTableViewModel::wasLastRefreshCancelled() const
{
  return wasLastRefreshCancelled_;
}

GuiQueryState QueryPanelViewModel::buildQueryState() const
{
  GuiQueryState guiQueryState;
  bool requiresDecode = false;
  guiQueryState.querySpec.shouldDecode = shouldDecodeMatches;
  guiQueryState.querySpec.shouldReturnRaw = true;
  std::vector<FilterRuleDraft> effectiveRawRules = rawRules;

  if(enableTimestampRange)
  {
    const auto timestampStart = parseUnsignedInteger(timestampStartText);
    const auto timestampEnd = parseUnsignedInteger(timestampEndText);
    if(timestampStart.has_value())
    {
      effectiveRawRules.push_back(
        {can_core::FilterField::TimestampNs,
         can_core::FilterOperator::GreaterOrEqual,
         ClauseMode::Must,
         std::to_string(*timestampStart),
         true});
      guiQueryState.visibleStartTimestampNs = *timestampStart;
    }
    if(timestampEnd.has_value())
    {
      effectiveRawRules.push_back(
        {can_core::FilterField::TimestampNs,
         can_core::FilterOperator::LessOrEqual,
         ClauseMode::Must,
         std::to_string(*timestampEnd),
         true});
      guiQueryState.visibleEndTimestampNs = *timestampEnd;
    }
  }

  if(enableOrdinalRange)
  {
    if(const auto startOrdinal = parseUnsignedInteger(ordinalStartText); startOrdinal.has_value())
    {
      guiQueryState.visibleStartOrdinal = *startOrdinal;
    }
    if(const auto endOrdinal = parseUnsignedInteger(ordinalEndText); endOrdinal.has_value())
    {
      guiQueryState.visibleEndOrdinal = *endOrdinal;
    }
  }

  if(const auto rawFilter = buildRuleFilter(effectiveRawRules, &requiresDecode); rawFilter.has_value())
  {
    guiQueryState.querySpec.rawFilter = *rawFilter;
  }
  guiQueryState.querySpec.decodedFilter = buildRuleFilter(decodedRules, &requiresDecode);
  guiQueryState.querySpec.shouldDecode = guiQueryState.querySpec.shouldDecode || requiresDecode;
  return guiQueryState;
}

can_app::RunOptions QueryPanelViewModel::buildRunOptions() const
{
  can_app::RunOptions runOptions;
  runOptions.tracePath = tracePath;
  if(!dbcPath.empty())
  {
    runOptions.dbcPath = dbcPath;
  }
  runOptions.shouldDecodeMatches = shouldDecodeMatches;
  if(enableOrdinalRange)
  {
    runOptions.startOrdinal = parseUnsignedInteger(ordinalStartText);
    runOptions.endOrdinal = parseUnsignedInteger(ordinalEndText);
  }
  if(enableResultCap)
  {
    if(const auto maxResultRows = parseUnsignedInteger(maxRowsText); maxResultRows.has_value())
    {
      runOptions.maxResultRows = static_cast<std::size_t>(*maxResultRows);
    }
  }
  return runOptions;
}

void QueryPanelViewModel::resetToDefaults()
{
  rawRules.clear();
  decodedRules.clear();
  rawRules.push_back({can_core::FilterField::CanId, can_core::FilterOperator::Equal, ClauseMode::Must, "", true});
  decodedRules.push_back(
    {can_core::FilterField::MessageName, can_core::FilterOperator::Contains, ClauseMode::Must, "", true});
}

void TimelineViewModel::setVisibleRange(std::uint64_t visibleStartTimestampNs, std::uint64_t visibleEndTimestampNs)
{
  visibleStartTimestampNs_ = visibleStartTimestampNs;
  visibleEndTimestampNs_ = visibleEndTimestampNs;
  clampRange();
}

void TimelineViewModel::setBounds(std::uint64_t minimumTimestampNs, std::uint64_t maximumTimestampNs)
{
  minimumTimestampNs_ = minimumTimestampNs;
  maximumTimestampNs_ = std::max(minimumTimestampNs, maximumTimestampNs);
  clampRange();
}

void TimelineViewModel::zoomIn()
{
  const std::uint64_t currentSpan = visibleEndTimestampNs_ > visibleStartTimestampNs_
    ? visibleEndTimestampNs_ - visibleStartTimestampNs_
    : 0;
  const std::uint64_t center = visibleStartTimestampNs_ + currentSpan / 2U;
  const std::uint64_t newHalfSpan = std::max<std::uint64_t>(1U, currentSpan / 4U);
  visibleStartTimestampNs_ = center > newHalfSpan ? center - newHalfSpan : minimumTimestampNs_;
  visibleEndTimestampNs_ = center + newHalfSpan;
  clampRange();
}

void TimelineViewModel::zoomOut()
{
  const std::uint64_t currentSpan = visibleEndTimestampNs_ > visibleStartTimestampNs_
    ? visibleEndTimestampNs_ - visibleStartTimestampNs_
    : 0;
  const std::uint64_t center = visibleStartTimestampNs_ + currentSpan / 2U;
  const std::uint64_t newHalfSpan = std::max<std::uint64_t>(1U, currentSpan);
  visibleStartTimestampNs_ = center > newHalfSpan ? center - newHalfSpan : minimumTimestampNs_;
  visibleEndTimestampNs_ = center + newHalfSpan;
  clampRange();
}

void TimelineViewModel::moveToTimestamp(std::uint64_t timestampNs)
{
  const std::uint64_t currentSpan = visibleEndTimestampNs_ > visibleStartTimestampNs_
    ? visibleEndTimestampNs_ - visibleStartTimestampNs_
    : 0;
  const std::uint64_t halfSpan = std::max<std::uint64_t>(1U, currentSpan / 2U);
  visibleStartTimestampNs_ = timestampNs > halfSpan ? timestampNs - halfSpan : minimumTimestampNs_;
  visibleEndTimestampNs_ = timestampNs + halfSpan;
  clampRange();
}

std::uint64_t TimelineViewModel::visibleStartTimestampNs() const
{
  return visibleStartTimestampNs_;
}

std::uint64_t TimelineViewModel::visibleEndTimestampNs() const
{
  return visibleEndTimestampNs_;
}

void TimelineViewModel::clampRange()
{
  visibleStartTimestampNs_ = std::max(visibleStartTimestampNs_, minimumTimestampNs_);
  visibleEndTimestampNs_ = std::max(visibleEndTimestampNs_, visibleStartTimestampNs_);
  if(maximumTimestampNs_ < visibleStartTimestampNs_)
  {
    visibleStartTimestampNs_ = maximumTimestampNs_;
  }
  if(maximumTimestampNs_ < visibleEndTimestampNs_)
  {
    visibleEndTimestampNs_ = maximumTimestampNs_;
  }
}

void SignalPanelViewModel::setSelectedRow(const can_app::QueryResultRow *selectedRow)
{
  selectedRow_ = selectedRow;
}

const can_app::QueryResultRow *SignalPanelViewModel::selectedRow() const
{
  return selectedRow_;
}

std::span<const can_decode::DecodedSignal> SignalPanelViewModel::visibleSignals() const
{
  if(selectedRow_ == nullptr || !selectedRow_->decodedMessage.has_value())
  {
    return {};
  }

  return selectedRow_->decodedMessage->signals;
}

GuiApplication::GuiApplication(std::vector<std::string> arguments)
  : arguments_(std::move(arguments))
{
  queryPanelViewModel_.resetToDefaults();
  if(arguments_.size() > 1U)
  {
    queryPanelViewModel_.tracePath = arguments_[1];
  }

  for(std::size_t index = 2U; index + 1U < arguments_.size(); ++index)
  {
    if(arguments_[index] == "--dbc")
    {
      queryPanelViewModel_.dbcPath = arguments_[index + 1U];
      break;
    }
  }
}

int GuiApplication::run()
{
  if(!initialize())
  {
    shutdown();
    return 1;
  }

  while(isRunning_)
  {
    SDL_Event event;
    while(SDL_PollEvent(&event) != 0)
    {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if(event.type == SDL_QUIT)
      {
        isRunning_ = false;
      }
      if(event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
        event.window.windowID == SDL_GetWindowID(g_window))
      {
        isRunning_ = false;
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    update();
    render();

    ImGui::Render();
    int displayWidth = 0;
    int displayHeight = 0;
    SDL_GL_GetDrawableSize(g_window, &displayWidth, &displayHeight);
    glViewport(0, 0, displayWidth, displayHeight);
    glClearColor(0.07F, 0.08F, 0.10F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(g_window);
  }

  shutdown();
  return 0;
}

void GuiApplication::update()
{
  handlePendingRefresh();
  if(traceTableViewModel_.pollRefresh())
  {
    guiSession_.runSummary = traceTableViewModel_.runSummary();
    guiSession_.traceMetadata = traceTableViewModel_.traceMetadata();

    if(traceTableViewModel_.wasLastRefreshCancelled())
    {
      guiSession_.statusMessage = "Query cancelled.";
    }
    else if(guiSession_.runSummary.hasError())
    {
      guiSession_.statusMessage = guiSession_.runSummary.errorInfo.message;
    }
    else
    {
      const std::size_t visibleRowCount = traceTableViewModel_.visibleRows().size();
      guiSession_.statusMessage = "Showing " + std::to_string(visibleRowCount) + " rows from " +
        std::to_string(guiSession_.runSummary.matchedEvents) + " matches in the current scope (" +
        std::to_string(guiSession_.runSummary.scannedEvents) + " scanned).";
      if(guiSession_.runSummary.matchedEvents > kMaximumVisibleRows)
      {
        guiSession_.statusMessage += " Display is capped to the first " + std::to_string(kMaximumVisibleRows) + " rows.";
      }
    }

    const auto rows = traceTableViewModel_.visibleRows();
    if(!rows.empty())
    {
      timelineViewModel_.setBounds(rows.front().canEvent.timestampNs, rows.back().canEvent.timestampNs);
      if(guiSession_.queryState.visibleStartTimestampNs == 0 && guiSession_.queryState.visibleEndTimestampNs == 0)
      {
        timelineViewModel_.setVisibleRange(rows.front().canEvent.timestampNs, rows.back().canEvent.timestampNs);
      }
      else
      {
        timelineViewModel_.setVisibleRange(
          guiSession_.queryState.visibleStartTimestampNs,
          guiSession_.queryState.visibleEndTimestampNs);
      }
    }

    guiSession_.hasSelection = false;
    guiSession_.selectedRowIndex = 0;
    signalPanelViewModel_.setSelectedRow(nullptr);
  }
  syncSelection();
}

void GuiApplication::render()
{
  renderMenuBar();
  renderOverviewPanel();
  renderQueryPanel();
  renderTimelinePanel();
  renderTraceTablePanel();
  renderSignalPanel();
}

bool GuiApplication::initialize()
{
  if(queryPanelViewModel_.rawRules.empty() && queryPanelViewModel_.decodedRules.empty())
  {
    queryPanelViewModel_.resetToDefaults();
  }

  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
  {
    guiSession_.statusMessage = std::string("SDL initialization failed: ") + SDL_GetError();
    return false;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  g_window = SDL_CreateWindow(
    "CAN Trace Explorer",
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    kWindowWidth,
    kWindowHeight,
    SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
  if(g_window == nullptr)
  {
    guiSession_.statusMessage = std::string("Window creation failed: ") + SDL_GetError();
    return false;
  }
  SDL_MaximizeWindow(g_window);

  g_glContext = SDL_GL_CreateContext(g_window);
  if(g_glContext == nullptr)
  {
    guiSession_.statusMessage = std::string("OpenGL context creation failed: ") + SDL_GetError();
    return false;
  }

  SDL_GL_MakeCurrent(g_window, g_glContext);
  SDL_GL_SetSwapInterval(1);

  const GLenum glewError = glewInit();
  if(glewError != GLEW_OK)
  {
    guiSession_.statusMessage = std::string("GLEW initialization failed: ") +
      reinterpret_cast<const char *>(glewGetErrorString(glewError));
    return false;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsDark();

  if(!ImGui_ImplSDL2_InitForOpenGL(g_window, g_glContext))
  {
    guiSession_.statusMessage = "ImGui SDL2 backend initialization failed";
    return false;
  }
  if(!ImGui_ImplOpenGL3_Init("#version 330"))
  {
    guiSession_.statusMessage = "ImGui OpenGL backend initialization failed";
    return false;
  }

  isInitialized_ = true;
  lastEditTime_ = std::chrono::steady_clock::now();
  refreshDevelopDataFiles();
  syncSessionFromDraft();
  if(hasAnyTraceSource(queryPanelViewModel_))
  {
    guiSession_.statusMessage =
      "Trace and DBC inputs are ready. Press Run Query to start, or enable Auto refresh after narrowing the draft.";
  }
  else
  {
    guiSession_.statusMessage = "Enter a trace path to start exploring the capture.";
  }

  return true;
}

void GuiApplication::shutdown()
{
  traceTableViewModel_.cancelRefresh();

  if(isInitialized_)
  {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    isInitialized_ = false;
  }

  if(g_glContext != nullptr)
  {
    SDL_GL_DeleteContext(g_glContext);
    g_glContext = nullptr;
  }

  if(g_window != nullptr)
  {
    SDL_DestroyWindow(g_window);
    g_window = nullptr;
  }

  SDL_Quit();
}

void GuiApplication::handlePendingRefresh()
{
  if(!isRefreshPending_ || !guiSession_.autoRefresh)
  {
    return;
  }

  if(std::chrono::steady_clock::now() - lastEditTime_ < kRefreshDebounce)
  {
    return;
  }

  if(traceTableViewModel_.isRefreshInProgress())
  {
    traceTableViewModel_.cancelRefresh();
  }
  refreshResults();
}

void GuiApplication::refreshResults()
{
  syncSessionFromDraft();
  if(guiSession_.tracePath.empty())
  {
    guiSession_.statusMessage = "Trace path is required.";
    return;
  }

  const GuiQueryState draftQueryState = queryPanelViewModel_.buildQueryState();
  if((draftQueryState.querySpec.decodedFilter.has_value() || draftQueryState.querySpec.shouldDecode) &&
    queryPanelViewModel_.dbcPath.empty())
  {
    guiSession_.statusMessage = "A DBC path is required for decoded queries or decoded result display.";
    return;
  }

  traceTableViewModel_.setRunOptions(queryPanelViewModel_.buildRunOptions());
  if(traceTableViewModel_.isRefreshInProgress())
  {
    traceTableViewModel_.cancelRefresh();
  }
  traceTableViewModel_.startRefresh(draftQueryState.querySpec);
  guiSession_.statusMessage = "Query running for the current scope...";
  isRefreshPending_ = false;
}

void GuiApplication::renderMenuBar()
{
  if(ImGui::BeginMainMenuBar())
  {
    if(ImGui::BeginMenu("File"))
    {
      if(ImGui::MenuItem("Refresh", "Ctrl+R"))
      {
        refreshResults();
      }
      if(ImGui::MenuItem("Quit"))
      {
        isRunning_ = false;
      }
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

void GuiApplication::renderOverviewPanel()
{
  ImGui::SetNextWindowPos(ImVec2(16.0F, 40.0F), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(520.0F, 180.0F), ImGuiCond_FirstUseEver);
  if(!ImGui::Begin("Session Overview"))
  {
    ImGui::End();
    return;
  }

  ImGui::TextWrapped("%s", guiSession_.statusMessage.c_str());
  ImGui::Separator();
  ImGui::Text("Trace: %s", guiSession_.tracePath.empty() ? "<none>" : guiSession_.tracePath.c_str());
  ImGui::Text("DBC: %s", guiSession_.dbcPath.empty() ? "<none>" : guiSession_.dbcPath.c_str());
  ImGui::Text("Scanned: %" PRIu64, guiSession_.runSummary.scannedEvents);
  ImGui::Text("Matches: %" PRIu64, guiSession_.runSummary.matchedEvents);
  ImGui::Text("Visible rows: %zu", traceTableViewModel_.visibleRows().size());
  ImGui::Text("Decode available: %s", traceTableViewModel_.hasDecodedRows() ? "yes" : "no");
  ImGui::Text("Query state: %s", traceTableViewModel_.isRefreshInProgress() ? "running" : "idle");
  if(traceTableViewModel_.isRefreshInProgress())
  {
    if(ImGui::Button("Cancel Running Query"))
    {
      traceTableViewModel_.cancelRefresh();
    }
  }
  ImGui::End();
}

void GuiApplication::renderQueryPanel()
{
  ImGui::SetNextWindowPos(ImVec2(16.0F, 230.0F), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(520.0F, 610.0F), ImGuiCond_FirstUseEver);
  if(!ImGui::Begin("Query Builder"))
  {
    ImGui::End();
    return;
  }

  bool wasEdited = false;
#if defined(CAN_GUI_ENABLE_DEVELOP_DATA)
  if(!availableTraceFiles_.empty() || !availableDbcFiles_.empty())
  {
    ImGui::SeparatorText("Develop Data");
    if(ImGui::Button("Rescan data folder"))
    {
      refreshDevelopDataFiles();
      wasEdited = true;
    }

    if(!availableTraceFiles_.empty())
    {
      std::vector<std::string> traceFileNames;
      std::vector<const char *> traceFileLabels;
      traceFileNames.reserve(availableTraceFiles_.size());
      traceFileLabels.reserve(availableTraceFiles_.size());
      for(const std::filesystem::path &tracePath : availableTraceFiles_)
      {
        traceFileNames.push_back(tracePath.filename().string());
      }
      for(const std::string &traceFileName : traceFileNames)
      {
        traceFileLabels.push_back(traceFileName.c_str());
      }
      if(ImGui::Combo("Trace sample", &selectedTraceFileIndex_, traceFileLabels.data(), static_cast<int>(traceFileLabels.size())))
      {
        applySelectedDevelopFiles(false);
        wasEdited = true;
      }
    }
    else
    {
      ImGui::TextDisabled("No compatible trace files found in %s.", CAN_GUI_DEVELOP_DATA_PATH);
    }

    if(!availableDbcFiles_.empty())
    {
      std::vector<std::string> dbcFileNames;
      std::vector<const char *> dbcFileLabels;
      dbcFileNames.reserve(availableDbcFiles_.size());
      dbcFileLabels.reserve(availableDbcFiles_.size());
      for(const std::filesystem::path &dbcPath : availableDbcFiles_)
      {
        dbcFileNames.push_back(dbcPath.filename().string());
      }
      for(const std::string &dbcFileName : dbcFileNames)
      {
        dbcFileLabels.push_back(dbcFileName.c_str());
      }
      if(ImGui::Combo("DBC sample", &selectedDbcFileIndex_, dbcFileLabels.data(), static_cast<int>(dbcFileLabels.size())))
      {
        applySelectedDevelopFiles(false);
        wasEdited = true;
      }
    }
    else
    {
      ImGui::TextDisabled("No DBC files found in %s.", CAN_GUI_DEVELOP_DATA_PATH);
    }
  }
#endif

  ImGui::SetNextItemWidth(-1.0F);
  wasEdited = inputText("Trace path", queryPanelViewModel_.tracePath) || wasEdited;
  ImGui::SetNextItemWidth(-1.0F);
  wasEdited = inputText("DBC path", queryPanelViewModel_.dbcPath) || wasEdited;

  ImGui::SeparatorText("Execution");
  if(ImGui::Checkbox("Decode matches", &queryPanelViewModel_.shouldDecodeMatches))
  {
    wasEdited = true;
  }
  ImGui::SameLine();
  if(ImGui::Checkbox("Auto refresh", &guiSession_.autoRefresh))
  {
    wasEdited = true;
  }

  ImGui::SeparatorText("Rules");
  ImGui::TextDisabled("Show=require, Any Of=OR group, Hide=NOT. Text examples: RLS_01 & KL15, RLS_01 | KL15, RLS_01 & !KL15");

  const std::string rawHeader = "Raw Trace Rules (" + std::to_string(queryPanelViewModel_.rawRules.size()) + ")";
  if(ImGui::CollapsingHeader(rawHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
  {
    wasEdited = renderRuleEditor("raw", "No raw rules yet.", queryPanelViewModel_.rawRules, availableRawFields()) || wasEdited;
    ImGui::Spacing();
  }

  const std::string decodedHeader = "Decoded Rules (" + std::to_string(queryPanelViewModel_.decodedRules.size()) + ")";
  if(ImGui::CollapsingHeader(decodedHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
  {
    wasEdited = renderRuleEditor(
                  "decoded",
                  "No decoded rules yet.",
                  queryPanelViewModel_.decodedRules,
                  availableDecodedFields()) ||
      wasEdited;
    ImGui::Spacing();
  }

  if(ImGui::Button("Reset Rules"))
  {
    queryPanelViewModel_.resetToDefaults();
    wasEdited = true;
  }

  ImGui::SeparatorText("Window");
  wasEdited = renderCompactRangeInputs(
                "Timestamp",
                queryPanelViewModel_.enableTimestampRange,
                queryPanelViewModel_.timestampStartText,
                queryPanelViewModel_.timestampEndText,
                "##timestamp_start",
                "##timestamp_end") ||
    wasEdited;

  wasEdited = renderCompactRangeInputs(
                "Ordinal",
                queryPanelViewModel_.enableOrdinalRange,
                queryPanelViewModel_.ordinalStartText,
                queryPanelViewModel_.ordinalEndText,
                "##ordinal_start",
                "##ordinal_end") ||
    wasEdited;

  if(ImGui::Checkbox("Row cap", &queryPanelViewModel_.enableResultCap))
  {
    wasEdited = true;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!queryPanelViewModel_.enableResultCap);
  ImGui::SetNextItemWidth(120.0F);
  wasEdited = inputText("##max_rows", queryPanelViewModel_.maxRowsText) || wasEdited;
  ImGui::EndDisabled();

  if(ImGui::Button("Previous Scope"))
  {
    shiftOrdinalWindow(-1);
  }
  ImGui::SameLine();
  if(ImGui::Button("Next Scope"))
  {
    shiftOrdinalWindow(1);
  }
  ImGui::SameLine();
  if(ImGui::Button("Reset Scope"))
  {
    queryPanelViewModel_.enableOrdinalRange = true;
    queryPanelViewModel_.ordinalStartText = "0";
    queryPanelViewModel_.ordinalEndText = std::to_string(kDefaultOrdinalWindowSize - 1U);
    markRefreshNeeded();
  }

  ImGui::Spacing();
  if(ImGui::Button("Run Query", ImVec2(-1.0F, 0.0F)))
  {
    refreshResults();
  }
  if(traceTableViewModel_.isRefreshInProgress())
  {
    if(ImGui::Button("Cancel Query", ImVec2(-1.0F, 0.0F)))
    {
      traceTableViewModel_.cancelRefresh();
    }
  }

  if(wasEdited)
  {
    markRefreshNeeded();
  }

  ImGui::End();
}

void GuiApplication::renderTimelinePanel()
{
  ImGui::SetNextWindowPos(ImVec2(550.0F, 40.0F), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(1034.0F, 180.0F), ImGuiCond_FirstUseEver);
  if(!ImGui::Begin("Timeline"))
  {
    ImGui::End();
    return;
  }

  ImGui::Text("Visible range: %" PRIu64 " ns to %" PRIu64 " ns",
    timelineViewModel_.visibleStartTimestampNs(),
    timelineViewModel_.visibleEndTimestampNs());

  const auto rows = traceTableViewModel_.visibleRows();
  if(rows.empty())
  {
    ImGui::TextDisabled("Run a query to inspect timestamps.");
    ImGui::End();
    return;
  }

  if(ImGui::Button("Zoom In"))
  {
    timelineViewModel_.zoomIn();
    queryPanelViewModel_.enableTimestampRange = true;
    queryPanelViewModel_.timestampStartText = std::to_string(timelineViewModel_.visibleStartTimestampNs());
    queryPanelViewModel_.timestampEndText = std::to_string(timelineViewModel_.visibleEndTimestampNs());
    markRefreshNeeded();
  }
  ImGui::SameLine();
  if(ImGui::Button("Zoom Out"))
  {
    timelineViewModel_.zoomOut();
    queryPanelViewModel_.enableTimestampRange = true;
    queryPanelViewModel_.timestampStartText = std::to_string(timelineViewModel_.visibleStartTimestampNs());
    queryPanelViewModel_.timestampEndText = std::to_string(timelineViewModel_.visibleEndTimestampNs());
    markRefreshNeeded();
  }
  ImGui::SameLine();
  if(ImGui::Button("Reset Range"))
  {
    queryPanelViewModel_.enableTimestampRange = false;
    markRefreshNeeded();
  }

  const float minimumTimestamp = static_cast<float>(rows.front().canEvent.timestampNs);
  const float maximumTimestamp = static_cast<float>(rows.back().canEvent.timestampNs);
  float visibleRange[2] = {
    static_cast<float>(timelineViewModel_.visibleStartTimestampNs()),
    static_cast<float>(timelineViewModel_.visibleEndTimestampNs())};
  if(ImGui::DragFloatRange2(
      "Visible window",
      &visibleRange[0],
      &visibleRange[1],
      std::max(1.0F, (maximumTimestamp - minimumTimestamp) / 200.0F),
      minimumTimestamp,
      maximumTimestamp,
      "Start %.0f",
      "End %.0f"))
  {
    queryPanelViewModel_.enableTimestampRange = true;
    queryPanelViewModel_.timestampStartText = std::to_string(static_cast<std::uint64_t>(visibleRange[0]));
    queryPanelViewModel_.timestampEndText = std::to_string(static_cast<std::uint64_t>(visibleRange[1]));
    markRefreshNeeded();
  }

  ImGui::Text("Channels in current result:");
  for(const std::uint8_t channel : traceTableViewModel_.visibleChannels())
  {
    ImGui::SameLine();
    ImGui::Text("[%u]", static_cast<unsigned int>(channel));
  }

  ImGui::End();
}

void GuiApplication::renderTraceTablePanel()
{
  ImGui::SetNextWindowPos(ImVec2(550.0F, 230.0F), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(1034.0F, 620.0F), ImGuiCond_FirstUseEver);
  if(!ImGui::Begin("Trace Table"))
  {
    ImGui::End();
    return;
  }

  const auto rows = traceTableViewModel_.visibleRows();
  if(rows.empty())
  {
    ImGui::TextDisabled("No rows to display.");
    ImGui::End();
    return;
  }

  ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
    ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
  if(ImGui::BeginTable("trace_table", 7, tableFlags))
  {
    ImGui::TableSetupColumn("Ordinal");
    ImGui::TableSetupColumn("Timestamp");
    ImGui::TableSetupColumn("CAN ID");
    ImGui::TableSetupColumn("Channel");
    ImGui::TableSetupColumn("Frame");
    ImGui::TableSetupColumn("Payload");
    ImGui::TableSetupColumn("Decoded");
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while(clipper.Step())
    {
      for(int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
      {
        const can_app::QueryResultRow &queryResultRow = rows[static_cast<std::size_t>(rowIndex)];
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        const bool isSelected = guiSession_.hasSelection && guiSession_.selectedRowIndex == static_cast<std::size_t>(rowIndex);
        char ordinalLabel[64];
        std::snprintf(ordinalLabel, sizeof(ordinalLabel), "%" PRIu64, queryResultRow.ordinal);
        if(ImGui::Selectable(ordinalLabel, isSelected, ImGuiSelectableFlags_SpanAllColumns))
        {
          guiSession_.selectedRowIndex = static_cast<std::size_t>(rowIndex);
          guiSession_.hasSelection = true;
          syncSelection();
          timelineViewModel_.moveToTimestamp(queryResultRow.canEvent.timestampNs);
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%" PRIu64, queryResultRow.canEvent.timestampNs);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(formatCanId(queryResultRow.canEvent.canId).c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%u", static_cast<unsigned int>(queryResultRow.canEvent.channel));
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(frameTypeLabel(queryResultRow.canEvent.frameType));
        ImGui::TableSetColumnIndex(5);
        ImGui::TextUnformatted(formatPayload(queryResultRow.canEvent).c_str());
        ImGui::TableSetColumnIndex(6);
        if(queryResultRow.decodedMessage.has_value())
        {
          textUnformatted(queryResultRow.decodedMessage->messageName);
        }
        else
        {
          ImGui::TextDisabled("-");
        }
      }
    }

    ImGui::EndTable();
  }

  ImGui::End();
}

void GuiApplication::renderSignalPanel()
{
  ImGui::SetNextWindowPos(ImVec2(16.0F, 860.0F), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(1568.0F, 90.0F), ImGuiCond_FirstUseEver);
  if(!ImGui::Begin("Selection Details"))
  {
    ImGui::End();
    return;
  }

  const can_app::QueryResultRow *selectedRow = signalPanelViewModel_.selectedRow();
  if(selectedRow == nullptr)
  {
    ImGui::TextDisabled("Select a row to inspect raw and decoded details.");
    ImGui::End();
    return;
  }

  ImGui::Text("Ordinal: %" PRIu64 " | Timestamp: %" PRIu64 " | CAN ID: %s | Channel: %u | Frame: %s",
    selectedRow->ordinal,
    selectedRow->canEvent.timestampNs,
    formatCanId(selectedRow->canEvent.canId).c_str(),
    static_cast<unsigned int>(selectedRow->canEvent.channel),
    frameTypeLabel(selectedRow->canEvent.frameType));
  ImGui::TextWrapped("Payload: %s", formatPayload(selectedRow->canEvent).c_str());

  if(selectedRow->decodedMessage.has_value())
  {
    ImGui::Separator();
    ImGui::Text(
      "Decoded message: %.*s",
      static_cast<int>(selectedRow->decodedMessage->messageName.size()),
      selectedRow->decodedMessage->messageName.data());
    for(const can_decode::DecodedSignal &decodedSignal : signalPanelViewModel_.visibleSignals())
    {
      const std::string formattedSignal = formatSignalDisplay(decodedSignal);
      ImGui::BulletText(
        "%.*s = %s",
        static_cast<int>(decodedSignal.name.size()),
        decodedSignal.name.data(),
        formattedSignal.c_str());
    }
  }
  else
  {
    ImGui::TextDisabled("No decoded message is available for the selected row.");
  }

  ImGui::End();
}

void GuiApplication::syncSessionFromDraft()
{
  guiSession_.tracePath = queryPanelViewModel_.tracePath;
  guiSession_.dbcPath = queryPanelViewModel_.dbcPath;
  guiSession_.shouldDecodeMatches = queryPanelViewModel_.shouldDecodeMatches;
  guiSession_.queryState = queryPanelViewModel_.buildQueryState();
}

void GuiApplication::syncSelection()
{
  const auto rows = traceTableViewModel_.visibleRows();
  if(!guiSession_.hasSelection || guiSession_.selectedRowIndex >= rows.size())
  {
    guiSession_.hasSelection = false;
    signalPanelViewModel_.setSelectedRow(nullptr);
    return;
  }

  signalPanelViewModel_.setSelectedRow(&rows[guiSession_.selectedRowIndex]);
}

void GuiApplication::markRefreshNeeded()
{
  lastEditTime_ = std::chrono::steady_clock::now();
  isRefreshPending_ = true;
}

void GuiApplication::shiftOrdinalWindow(std::int64_t delta)
{
  const std::uint64_t startOrdinal = parseUnsignedInteger(queryPanelViewModel_.ordinalStartText).value_or(0U);
  const std::uint64_t endOrdinal = parseUnsignedInteger(queryPanelViewModel_.ordinalEndText)
    .value_or(startOrdinal + kDefaultOrdinalWindowSize - 1U);
  const std::uint64_t windowSize = endOrdinal >= startOrdinal ? (endOrdinal - startOrdinal + 1U) : kDefaultOrdinalWindowSize;

  std::int64_t nextStartOrdinal = static_cast<std::int64_t>(startOrdinal) + delta * static_cast<std::int64_t>(windowSize);
  if(nextStartOrdinal < 0)
  {
    nextStartOrdinal = 0;
  }

  const std::uint64_t normalizedStartOrdinal = static_cast<std::uint64_t>(nextStartOrdinal);
  queryPanelViewModel_.enableOrdinalRange = true;
  queryPanelViewModel_.ordinalStartText = std::to_string(normalizedStartOrdinal);
  queryPanelViewModel_.ordinalEndText = std::to_string(normalizedStartOrdinal + windowSize - 1U);
  markRefreshNeeded();
}

void GuiApplication::refreshDevelopDataFiles()
{
#if defined(CAN_GUI_ENABLE_DEVELOP_DATA)
  availableTraceFiles_.clear();
  availableDbcFiles_.clear();
  selectedTraceFileIndex_ = -1;
  selectedDbcFileIndex_ = -1;

  const std::filesystem::path dataDirectory(CAN_GUI_DEVELOP_DATA_PATH);
  if(!std::filesystem::exists(dataDirectory) || !std::filesystem::is_directory(dataDirectory))
  {
    return;
  }

  for(const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(dataDirectory))
  {
    if(!entry.is_regular_file())
    {
      continue;
    }

    const std::filesystem::path path = entry.path();
    if(isCompatibleTraceExtension(path))
    {
      availableTraceFiles_.push_back(path);
    }
    if(isCompatibleDbcExtension(path))
    {
      availableDbcFiles_.push_back(path);
    }
  }

  std::sort(availableTraceFiles_.begin(), availableTraceFiles_.end());
  std::sort(availableDbcFiles_.begin(), availableDbcFiles_.end());

  if(selectedTraceFileIndex_ < 0 && !availableTraceFiles_.empty())
  {
    selectedTraceFileIndex_ = 0;
  }
  if(selectedDbcFileIndex_ < 0 && !availableDbcFiles_.empty())
  {
    selectedDbcFileIndex_ = 0;
  }

  if(queryPanelViewModel_.tracePath.empty() && selectedTraceFileIndex_ >= 0)
  {
    queryPanelViewModel_.tracePath = availableTraceFiles_[static_cast<std::size_t>(selectedTraceFileIndex_)].string();
  }
  if(queryPanelViewModel_.dbcPath.empty() && selectedDbcFileIndex_ >= 0)
  {
    queryPanelViewModel_.dbcPath = availableDbcFiles_[static_cast<std::size_t>(selectedDbcFileIndex_)].string();
  }
#endif
}

void GuiApplication::applySelectedDevelopFiles(bool shouldMarkRefresh)
{
#if defined(CAN_GUI_ENABLE_DEVELOP_DATA)
  if(selectedTraceFileIndex_ >= 0 && static_cast<std::size_t>(selectedTraceFileIndex_) < availableTraceFiles_.size())
  {
    queryPanelViewModel_.tracePath = availableTraceFiles_[static_cast<std::size_t>(selectedTraceFileIndex_)].string();
  }
  if(selectedDbcFileIndex_ >= 0 && static_cast<std::size_t>(selectedDbcFileIndex_) < availableDbcFiles_.size())
  {
    queryPanelViewModel_.dbcPath = availableDbcFiles_[static_cast<std::size_t>(selectedDbcFileIndex_)].string();
  }
  if(shouldMarkRefresh)
  {
    markRefreshNeeded();
  }
#else
  (void)shouldMarkRefresh;
#endif
}
} // namespace can_gui

#include "can_gui/gui_application.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cinttypes>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>
#include <type_traits>
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
  case can_core::FilterField::FrameType: {
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
  const auto flushToken = [&](std::size_t tokenEnd) {
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
      case QueryPanelViewModel::ClauseMode::Exclude: {
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
      [](const auto &value) {
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

std::string formatByteCount(std::uint64_t byteCount)
{
  static constexpr std::array<const char *, 4> units = {"B", "KB", "MB", "GB"};
  double normalizedValue = static_cast<double>(byteCount);
  std::size_t unitIndex = 0U;
  while(normalizedValue >= 1024.0 && unitIndex + 1U < units.size())
  {
    normalizedValue /= 1024.0;
    ++unitIndex;
  }

  std::ostringstream stream;
  stream.setf(std::ios::fixed, std::ios::floatfield);
  stream.precision(unitIndex == 0U ? 0 : 1);
  stream << normalizedValue << ' ' << units[unitIndex];
  return stream.str();
}

std::string formatDuration(std::chrono::seconds duration)
{
  const auto totalSeconds = duration.count();
  const auto hours = totalSeconds / 3600;
  const auto minutes = (totalSeconds % 3600) / 60;
  const auto seconds = totalSeconds % 60;

  std::ostringstream stream;
  stream << std::setfill('0');
  if(hours > 0)
  {
    stream << std::setw(2) << hours << ':';
  }
  stream << std::setw(2) << minutes << ':' << std::setw(2) << seconds;
  return stream.str();
}

std::string formatClockTime(std::chrono::system_clock::time_point timePoint)
{
  if(timePoint.time_since_epoch().count() == 0)
  {
    return "--:--:--";
  }

  const std::time_t rawTime = std::chrono::system_clock::to_time_t(timePoint);
  std::tm timeInfo{};
#if defined(_WIN32)
  localtime_s(&timeInfo, &rawTime);
#else
  localtime_r(&rawTime, &timeInfo);
#endif
  std::ostringstream stream;
  stream << std::put_time(&timeInfo, "%H:%M:%S");
  return stream.str();
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

std::string normalizeForCaseInsensitiveCompare(std::string_view value)
{
  std::string normalizedValue(value);
  std::transform(normalizedValue.begin(), normalizedValue.end(), normalizedValue.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return normalizedValue;
}

bool isEmptyFilter(const can_core::FilterExpr &filterExpr)
{
  return !filterExpr.predicate.has_value() && filterExpr.children.empty();
}

std::optional<double> toDouble(const can_core::FilterValue &filterValue)
{
  return std::visit(
      [](const auto &value) -> std::optional<double> {
        using ValueType = std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<ValueType, std::monostate>)
        {
          return std::nullopt;
        }
        else if constexpr(std::is_same_v<ValueType, bool>)
        {
          return value ? 1.0 : 0.0;
        }
        else if constexpr(std::is_same_v<ValueType, std::uint64_t> || std::is_same_v<ValueType, std::int64_t> ||
                          std::is_same_v<ValueType, double>)
        {
          return static_cast<double>(value);
        }
        else
        {
          return std::nullopt;
        }
      },
      filterValue);
}

std::optional<std::string_view> toStringView(const can_core::FilterValue &filterValue)
{
  if(const auto *value = std::get_if<std::string>(&filterValue))
  {
    return *value;
  }

  return std::nullopt;
}

bool compareNumeric(double leftValue, double rightValue, can_core::FilterOperator filterOperator)
{
  switch(filterOperator)
  {
  case can_core::FilterOperator::Equal:
    return leftValue == rightValue;
  case can_core::FilterOperator::NotEqual:
    return leftValue != rightValue;
  case can_core::FilterOperator::Less:
    return leftValue < rightValue;
  case can_core::FilterOperator::LessOrEqual:
    return leftValue <= rightValue;
  case can_core::FilterOperator::Greater:
    return leftValue > rightValue;
  case can_core::FilterOperator::GreaterOrEqual:
    return leftValue >= rightValue;
  case can_core::FilterOperator::Contains:
    return false;
  }

  return false;
}

bool compareString(std::string_view leftValue, std::string_view rightValue, can_core::FilterOperator filterOperator)
{
  const std::string normalizedLeftValue = normalizeForCaseInsensitiveCompare(leftValue);
  const std::string normalizedRightValue = normalizeForCaseInsensitiveCompare(rightValue);

  switch(filterOperator)
  {
  case can_core::FilterOperator::Equal:
    return normalizedLeftValue == normalizedRightValue;
  case can_core::FilterOperator::NotEqual:
    return normalizedLeftValue != normalizedRightValue;
  case can_core::FilterOperator::Contains:
    return normalizedLeftValue.find(normalizedRightValue) != std::string::npos;
  case can_core::FilterOperator::Less:
  case can_core::FilterOperator::LessOrEqual:
  case can_core::FilterOperator::Greater:
  case can_core::FilterOperator::GreaterOrEqual:
    return false;
  }

  return false;
}

bool evaluateRawPredicate(const can_core::Predicate &predicate, const can_core::CanEvent &canEvent)
{
  switch(predicate.field)
  {
  case can_core::FilterField::TimestampNs:
  {
    const auto predicateValue = toDouble(predicate.value);
    return predicateValue.has_value() &&
           compareNumeric(static_cast<double>(canEvent.timestampNs), *predicateValue, predicate.filterOperator);
  }
  case can_core::FilterField::CanId:
  {
    const auto predicateValue = toDouble(predicate.value);
    return predicateValue.has_value() &&
           compareNumeric(static_cast<double>(canEvent.canId), *predicateValue, predicate.filterOperator);
  }
  case can_core::FilterField::Channel:
  {
    const auto predicateValue = toDouble(predicate.value);
    return predicateValue.has_value() &&
           compareNumeric(static_cast<double>(canEvent.channel), *predicateValue, predicate.filterOperator);
  }
  case can_core::FilterField::FrameType:
  {
    const auto predicateValue = toDouble(predicate.value);
    return predicateValue.has_value() &&
           compareNumeric(static_cast<double>(static_cast<std::uint8_t>(canEvent.frameType)), *predicateValue, predicate.filterOperator);
  }
  case can_core::FilterField::MessageName:
  case can_core::FilterField::SignalName:
  case can_core::FilterField::SignalValue:
    return true;
  }

  return false;
}

bool evaluateRawFilter(const can_core::FilterExpr &filterExpr, const can_core::CanEvent &canEvent)
{
  if(isEmptyFilter(filterExpr))
  {
    return true;
  }

  if(filterExpr.predicate.has_value())
  {
    return evaluateRawPredicate(*filterExpr.predicate, canEvent);
  }

  if(filterExpr.logicalOperator == can_core::LogicalOperator::Not)
  {
    if(filterExpr.children.empty())
    {
      return true;
    }

    return !evaluateRawFilter(filterExpr.children.front(), canEvent);
  }

  const bool initialValue = filterExpr.logicalOperator == can_core::LogicalOperator::And;
  bool result = initialValue;
  for(const can_core::FilterExpr &child : filterExpr.children)
  {
    const bool childValue = evaluateRawFilter(child, canEvent);
    if(filterExpr.logicalOperator == can_core::LogicalOperator::And)
    {
      result = result && childValue;
      if(!result)
      {
        return false;
      }
    }
    else
    {
      result = result || childValue;
      if(result)
      {
        return true;
      }
    }
  }

  return result;
}

bool matchesSignalName(std::string_view signalName, const can_decode::DecodedMessage &decodedMessage, can_core::FilterOperator filterOperator)
{
  for(const can_decode::DecodedSignal &decodedSignal : decodedMessage.signals)
  {
    if(compareString(decodedSignal.name, signalName, filterOperator))
    {
      return true;
    }
  }

  return false;
}

bool matchesSignalValue(double signalValue, const can_decode::DecodedMessage &decodedMessage, can_core::FilterOperator filterOperator)
{
  for(const can_decode::DecodedSignal &decodedSignal : decodedMessage.signals)
  {
    const auto decodedValue = std::visit(
        [](const auto &value) -> double { return static_cast<double>(value); },
        decodedSignal.value);

    if(compareNumeric(decodedValue, signalValue, filterOperator))
    {
      return true;
    }
  }

  return false;
}

bool evaluateDecodedPredicate(const can_core::Predicate &predicate, const can_decode::DecodedMessage &decodedMessage)
{
  switch(predicate.field)
  {
  case can_core::FilterField::MessageName:
  {
    const auto predicateValue = toStringView(predicate.value);
    return predicateValue.has_value() && compareString(decodedMessage.messageName, *predicateValue, predicate.filterOperator);
  }
  case can_core::FilterField::SignalName:
  {
    const auto predicateValue = toStringView(predicate.value);
    return predicateValue.has_value() && matchesSignalName(*predicateValue, decodedMessage, predicate.filterOperator);
  }
  case can_core::FilterField::SignalValue:
  {
    const auto predicateValue = toDouble(predicate.value);
    return predicateValue.has_value() && matchesSignalValue(*predicateValue, decodedMessage, predicate.filterOperator);
  }
  case can_core::FilterField::TimestampNs:
  case can_core::FilterField::CanId:
  case can_core::FilterField::Channel:
  case can_core::FilterField::FrameType:
    return true;
  }

  return false;
}

bool evaluateDecodedFilter(const can_core::FilterExpr &filterExpr, const can_decode::DecodedMessage &decodedMessage)
{
  if(isEmptyFilter(filterExpr))
  {
    return true;
  }

  if(filterExpr.predicate.has_value())
  {
    return evaluateDecodedPredicate(*filterExpr.predicate, decodedMessage);
  }

  if(filterExpr.logicalOperator == can_core::LogicalOperator::Not)
  {
    if(filterExpr.children.empty())
    {
      return true;
    }

    return !evaluateDecodedFilter(filterExpr.children.front(), decodedMessage);
  }

  const bool initialValue = filterExpr.logicalOperator == can_core::LogicalOperator::And;
  bool result = initialValue;
  for(const can_core::FilterExpr &child : filterExpr.children)
  {
    const bool childValue = evaluateDecodedFilter(child, decodedMessage);
    if(filterExpr.logicalOperator == can_core::LogicalOperator::And)
    {
      result = result && childValue;
      if(!result)
      {
        return false;
      }
    }
    else
    {
      result = result || childValue;
      if(result)
      {
        return true;
      }
    }
  }

  return result;
}

bool rowMatchesQuery(const can_app::QueryResultRow &queryResultRow, const GuiQueryState &queryState)
{
  if(queryState.hasVisibleOrdinalRange &&
     queryState.visibleEndOrdinal >= queryState.visibleStartOrdinal)
  {
    if(queryResultRow.ordinal < queryState.visibleStartOrdinal || queryResultRow.ordinal > queryState.visibleEndOrdinal)
    {
      return false;
    }
  }

  if(!evaluateRawFilter(queryState.querySpec.rawFilter, queryResultRow.canEvent))
  {
    return false;
  }

  if(queryState.querySpec.decodedFilter.has_value())
  {
    if(!queryResultRow.decodedMessage.has_value())
    {
      return false;
    }

    return evaluateDecodedFilter(*queryState.querySpec.decodedFilter, *queryResultRow.decodedMessage);
  }

  return true;
}

std::vector<std::uint8_t> collectVisibleChannels(std::span<const can_app::QueryResultRow> rows)
{
  std::vector<std::uint8_t> visibleChannels;
  for(const can_app::QueryResultRow &queryResultRow : rows)
  {
    if(std::find(visibleChannels.begin(), visibleChannels.end(), queryResultRow.canEvent.channel) == visibleChannels.end())
    {
      visibleChannels.push_back(queryResultRow.canEvent.channel);
    }
  }

  std::sort(visibleChannels.begin(), visibleChannels.end());
  return visibleChannels;
}

bool rowsHaveDecodedData(std::span<const can_app::QueryResultRow> rows)
{
  return std::any_of(rows.begin(), rows.end(), [](const can_app::QueryResultRow &queryResultRow) {
    return queryResultRow.decodedMessage.has_value();
  });
}

can_core::TraceMetadata buildTraceMetadata(std::span<const can_app::QueryResultRow> rows, std::string_view sourcePath)
{
  can_core::TraceMetadata traceMetadata;
  traceMetadata.sourcePath = std::string(sourcePath);
  const std::size_t extensionSeparator = traceMetadata.sourcePath.find_last_of('.');
  traceMetadata.sourceFormat = traceMetadata.sourcePath.substr(
      extensionSeparator == std::string::npos ? traceMetadata.sourcePath.size() : extensionSeparator);
  traceMetadata.eventCount = rows.size();
  if(rows.empty())
  {
    return traceMetadata;
  }

  traceMetadata.startTimestampNs = rows.front().canEvent.timestampNs;
  traceMetadata.endTimestampNs = rows.front().canEvent.timestampNs;
  for(const can_app::QueryResultRow &queryResultRow : rows)
  {
    traceMetadata.startTimestampNs = std::min(traceMetadata.startTimestampNs, queryResultRow.canEvent.timestampNs);
    traceMetadata.endTimestampNs = std::max(traceMetadata.endTimestampNs, queryResultRow.canEvent.timestampNs);
  }
  return traceMetadata;
}

const char *operationLabel(TraceTableViewModel::ActiveOperation activeOperation)
{
  switch(activeOperation)
  {
  case TraceTableViewModel::ActiveOperation::None:
    return "idle";
  case TraceTableViewModel::ActiveOperation::ScanFile:
    return "scanning file";
  case TraceTableViewModel::ActiveOperation::FilterFullDataset:
    return "filtering full rows";
  case TraceTableViewModel::ActiveOperation::FilterCurrentMatches:
    return "filtering current matches";
  }

  return "idle";
}
} // namespace

void TraceTableViewModel::setRunOptions(can_app::RunOptions runOptions)
{
  runOptions_ = std::move(runOptions);
}

void TraceTableViewModel::startScan(const GuiQueryState &queryState)
{
  cancelActiveOperation();

  activeOperation_ = ActiveOperation::ScanFile;
  operationStartWallClock_ = std::chrono::system_clock::now();
  operationStartSteadyClock_ = std::chrono::steady_clock::now();
  cancellationFlag_ = std::make_shared<std::atomic<bool>>(false);
  progressState_ = std::make_shared<SharedProgressState>();
  const std::shared_ptr<SharedProgressState> progressState = progressState_;
  can_app::RunOptions runOptions = runOptions_;
  runOptions.rawFilter.reset();
  runOptions.decodedFilter.reset();
  runOptions.startOrdinal.reset();
  runOptions.endOrdinal.reset();
  runOptions.maxResultRows.reset();
  runOptions.shouldDecodeMatches = queryState.querySpec.shouldDecode;
  runOptions.shouldCancel = cancellationFlag_.get();
  runOptions.progressCallback = [progressState](const can_query::QueryProgress &queryProgress) {
    if(progressState == nullptr)
    {
      return;
    }

    progressState->scannedEvents.store(queryProgress.scannedEvents);
    progressState->matchedEvents.store(queryProgress.matchedEvents);
    progressState->bytesParsed.store(queryProgress.bytesParsed);
    progressState->totalBytes.store(queryProgress.totalBytes);
  };

  refreshFuture_ = std::async(
      std::launch::async,
      [this, runOptions, queryState, progressState]() mutable {
        RefreshSnapshot refreshSnapshot;
        refreshSnapshot.shouldReplaceFullDataset = true;
        refreshSnapshot.completedOperation = ActiveOperation::ScanFile;
        refreshSnapshot.runSummary = canApp_.run(
            runOptions,
            [&refreshSnapshot](const can_app::QueryResultRow &queryResultRow) {
              refreshSnapshot.fullRows.push_back(queryResultRow);
            });

        refreshSnapshot.wasCancelled = refreshSnapshot.runSummary.wasCancelled;
        refreshSnapshot.fullDatasetHasDecodedRows = rowsHaveDecodedData(refreshSnapshot.fullRows);
        if(!refreshSnapshot.wasCancelled && !refreshSnapshot.runSummary.hasError())
        {
          for(const can_app::QueryResultRow &queryResultRow : refreshSnapshot.fullRows)
          {
            if(rowMatchesQuery(queryResultRow, queryState))
            {
              refreshSnapshot.rows.push_back(queryResultRow);
            }
          }

          refreshSnapshot.runSummary.scannedEvents = refreshSnapshot.fullRows.size();
          refreshSnapshot.runSummary.matchedEvents = refreshSnapshot.rows.size();
          refreshSnapshot.traceMetadata = buildTraceMetadata(refreshSnapshot.rows, runOptions.tracePath);
          refreshSnapshot.visibleChannels = collectVisibleChannels(refreshSnapshot.rows);
          refreshSnapshot.hasDecodedRows = rowsHaveDecodedData(refreshSnapshot.rows);
        }

        if(progressState != nullptr)
        {
          progressState->scannedEvents.store(refreshSnapshot.runSummary.scannedEvents);
          progressState->matchedEvents.store(refreshSnapshot.runSummary.matchedEvents);
          if(progressState->totalBytes.load() > 0U)
          {
            progressState->bytesParsed.store(progressState->totalBytes.load());
          }
        }
        return refreshSnapshot;
      });
}

void TraceTableViewModel::startFilter(const GuiQueryState &queryState, FilterSource filterSource)
{
  cancelActiveOperation();

  activeOperation_ = filterSource == FilterSource::FullDataset ? ActiveOperation::FilterFullDataset
                                                               : ActiveOperation::FilterCurrentMatches;
  operationStartWallClock_ = std::chrono::system_clock::now();
  operationStartSteadyClock_ = std::chrono::steady_clock::now();
  cancellationFlag_ = std::make_shared<std::atomic<bool>>(false);
  progressState_ = std::make_shared<SharedProgressState>();
  const std::shared_ptr<SharedProgressState> progressState = progressState_;
  const std::shared_ptr<std::atomic<bool>> cancellationFlag = cancellationFlag_;
  const std::vector<can_app::QueryResultRow> *sourceRows =
      filterSource == FilterSource::FullDataset ? &fullRows_ : &rows_;
  const std::string tracePath = runOptions_.tracePath;
  if(progressState != nullptr)
  {
    progressState->scannedEvents.store(0U);
    progressState->matchedEvents.store(0U);
    progressState->bytesParsed.store(0U);
    progressState->totalBytes.store(sourceRows->size());
  }
  refreshFuture_ = std::async(
      std::launch::async,
      [queryState, filterSource, sourceRows, tracePath, progressState, cancellationFlag]() {
        RefreshSnapshot refreshSnapshot;
        refreshSnapshot.completedOperation = filterSource == FilterSource::FullDataset
                                                 ? ActiveOperation::FilterFullDataset
                                                 : ActiveOperation::FilterCurrentMatches;
        refreshSnapshot.runSummary.scannedEvents = 0U;
        refreshSnapshot.runSummary.matchedEvents = 0U;
        refreshSnapshot.rows.reserve(sourceRows->size());

        for(const can_app::QueryResultRow &queryResultRow : *sourceRows)
        {
          if(cancellationFlag != nullptr && cancellationFlag->load())
          {
            refreshSnapshot.wasCancelled = true;
            refreshSnapshot.runSummary.wasCancelled = true;
            return refreshSnapshot;
          }

          ++refreshSnapshot.runSummary.scannedEvents;
          if(rowMatchesQuery(queryResultRow, queryState))
          {
            refreshSnapshot.rows.push_back(queryResultRow);
            ++refreshSnapshot.runSummary.matchedEvents;
          }

          if(progressState != nullptr)
          {
            progressState->scannedEvents.store(refreshSnapshot.runSummary.scannedEvents);
            progressState->matchedEvents.store(refreshSnapshot.runSummary.matchedEvents);
            progressState->bytesParsed.store(refreshSnapshot.runSummary.scannedEvents);
          }
        }

        refreshSnapshot.traceMetadata = buildTraceMetadata(refreshSnapshot.rows, tracePath);
        refreshSnapshot.visibleChannels = collectVisibleChannels(refreshSnapshot.rows);
        refreshSnapshot.hasDecodedRows = rowsHaveDecodedData(refreshSnapshot.rows);
        refreshSnapshot.fullDatasetHasDecodedRows = rowsHaveDecodedData(*sourceRows);
        return refreshSnapshot;
      });
}

void TraceTableViewModel::resetMatchesToFullDataset()
{
  rows_ = fullRows_;
  runSummary_.scannedEvents = fullRows_.size();
  runSummary_.matchedEvents = rows_.size();
  runSummary_.wasCancelled = false;
  runSummary_.errorInfo = {};
  traceMetadata_ = buildTraceMetadata(rows_, runOptions_.tracePath);
  visibleChannels_ = collectVisibleChannels(rows_);
  hasDecodedRows_ = rowsHaveDecodedData(rows_);
}

void TraceTableViewModel::cancelActiveOperation()
{
  if(cancellationFlag_ != nullptr)
  {
    cancellationFlag_->store(true);
  }
}

bool TraceTableViewModel::pollActiveOperation()
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
  wasLastOperationCancelled_ = refreshSnapshot.wasCancelled;
  lastCompletedOperation_ = refreshSnapshot.completedOperation;
  activeOperation_ = ActiveOperation::None;
  if(refreshSnapshot.wasCancelled)
  {
    return true;
  }

  if(refreshSnapshot.shouldReplaceFullDataset)
  {
    fullRows_ = std::move(refreshSnapshot.fullRows);
    hasFullDataset_ = !refreshSnapshot.runSummary.hasError();
    fullDatasetHasDecodedRows_ = refreshSnapshot.fullDatasetHasDecodedRows;
  }
  rows_ = std::move(refreshSnapshot.rows);
  runSummary_ = refreshSnapshot.runSummary;
  traceMetadata_ = std::move(refreshSnapshot.traceMetadata);
  visibleChannels_ = std::move(refreshSnapshot.visibleChannels);
  hasDecodedRows_ = refreshSnapshot.hasDecodedRows;
  return true;
}

bool TraceTableViewModel::isOperationInProgress() const
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

bool TraceTableViewModel::hasFullDataset() const
{
  return hasFullDataset_;
}

bool TraceTableViewModel::fullDatasetHasDecodedRows() const
{
  return fullDatasetHasDecodedRows_;
}

std::size_t TraceTableViewModel::fullRowCount() const
{
  return fullRows_.size();
}

bool TraceTableViewModel::wasLastOperationCancelled() const
{
  return wasLastOperationCancelled_;
}

TraceTableViewModel::ActiveOperation TraceTableViewModel::activeOperation() const
{
  return activeOperation_;
}

TraceTableViewModel::ProgressSnapshot TraceTableViewModel::progressSnapshot() const
{
  if(progressState_ == nullptr)
  {
    return {};
  }

  ProgressSnapshot progressSnapshot;
  progressSnapshot.scannedEvents = progressState_->scannedEvents.load();
  progressSnapshot.matchedEvents = progressState_->matchedEvents.load();
  progressSnapshot.bytesParsed = progressState_->bytesParsed.load();
  progressSnapshot.totalBytes = progressState_->totalBytes.load();
  return progressSnapshot;
}

std::chrono::system_clock::time_point TraceTableViewModel::operationStartWallClock() const
{
  return operationStartWallClock_;
}

std::chrono::steady_clock::time_point TraceTableViewModel::operationStartSteadyClock() const
{
  return operationStartSteadyClock_;
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
    guiQueryState.hasVisibleTimestampRange = true;
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
    guiQueryState.hasVisibleOrdinalRange = true;
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
  if(traceTableViewModel_.pollActiveOperation())
  {
    guiSession_.runSummary = traceTableViewModel_.runSummary();
    guiSession_.traceMetadata = traceTableViewModel_.traceMetadata();

    if(traceTableViewModel_.wasLastOperationCancelled())
    {
      guiSession_.statusMessage = "Action cancelled.";
    }
    else if(guiSession_.runSummary.hasError())
    {
      guiSession_.statusMessage = guiSession_.runSummary.errorInfo.message;
    }
    else
    {
      guiSession_.statusMessage = "Full rows in RAM: " + std::to_string(traceTableViewModel_.fullRowCount()) +
                                  " | Current matches: " + std::to_string(traceTableViewModel_.visibleRows().size()) + ".";
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
  renderTransientActionPopup();
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
        "Trace and DBC inputs are ready. Press Scan File to load rows into RAM, then filter from memory.";
  }
  else
  {
    guiSession_.statusMessage = "Enter a trace path to start exploring the capture.";
  }

  return true;
}

void GuiApplication::shutdown()
{
  traceTableViewModel_.cancelActiveOperation();

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

  applyFiltersFromFullDataset();
}

void GuiApplication::triggerScanFile()
{
  syncSessionFromDraft();
  if(guiSession_.tracePath.empty())
  {
    guiSession_.statusMessage = "Trace path is required.";
    return;
  }

  const GuiQueryState draftQueryState = queryPanelViewModel_.buildQueryState();
  if(draftQueryState.querySpec.shouldDecode && queryPanelViewModel_.dbcPath.empty())
  {
    guiSession_.statusMessage = "A DBC path is required when scan-time decode is enabled.";
    return;
  }

  traceTableViewModel_.setRunOptions(queryPanelViewModel_.buildRunOptions());
  if(traceTableViewModel_.isOperationInProgress())
  {
    traceTableViewModel_.cancelActiveOperation();
  }
  traceTableViewModel_.startScan(draftQueryState);
  guiSession_.statusMessage = "Reading trace file into RAM...";
  showTransientActionPopup("Reading trace file into RAM...");
  isRefreshPending_ = false;
}

void GuiApplication::applyFiltersFromFullDataset()
{
  syncSessionFromDraft();
  if(!traceTableViewModel_.hasFullDataset())
  {
    guiSession_.statusMessage = "Scan the file first to build the full in-memory row list.";
    return;
  }

  const GuiQueryState draftQueryState = queryPanelViewModel_.buildQueryState();
  if(draftQueryState.querySpec.shouldDecode && !traceTableViewModel_.fullDatasetHasDecodedRows())
  {
    guiSession_.statusMessage = "Current RAM dataset has no decoded rows. Scan the file again with Decode matches enabled.";
    return;
  }

  if(traceTableViewModel_.isOperationInProgress())
  {
    traceTableViewModel_.cancelActiveOperation();
  }
  traceTableViewModel_.startFilter(draftQueryState, TraceTableViewModel::FilterSource::FullDataset);
  guiSession_.statusMessage = "Filtering from full rows in RAM...";
  showTransientActionPopup("Filtering from full rows in RAM...");
  isRefreshPending_ = false;
}

void GuiApplication::applyFiltersToCurrentMatches()
{
  syncSessionFromDraft();
  if(traceTableViewModel_.visibleRows().empty())
  {
    guiSession_.statusMessage = "There are no current match rows to refine.";
    return;
  }

  const GuiQueryState draftQueryState = queryPanelViewModel_.buildQueryState();
  if(draftQueryState.querySpec.shouldDecode && !traceTableViewModel_.hasDecodedRows())
  {
    guiSession_.statusMessage = "Current match rows have no decoded data. Scan the file again with Decode matches enabled.";
    return;
  }

  if(traceTableViewModel_.isOperationInProgress())
  {
    traceTableViewModel_.cancelActiveOperation();
  }
  traceTableViewModel_.startFilter(draftQueryState, TraceTableViewModel::FilterSource::CurrentMatches);
  guiSession_.statusMessage = "Filtering only the current match rows in RAM...";
  showTransientActionPopup("Filtering only the current match rows in RAM...");
  isRefreshPending_ = false;
}

void GuiApplication::resetFilters()
{
  queryPanelViewModel_.resetToDefaults();
  queryPanelViewModel_.enableTimestampRange = false;
  queryPanelViewModel_.enableOrdinalRange = false;
  if(traceTableViewModel_.hasFullDataset())
  {
    traceTableViewModel_.resetMatchesToFullDataset();
    guiSession_.runSummary = traceTableViewModel_.runSummary();
    guiSession_.traceMetadata = traceTableViewModel_.traceMetadata();
    guiSession_.statusMessage = "Filters reset. Showing the full in-memory row list.";
    showTransientActionPopup("Restored the full in-memory row list.");
    guiSession_.hasSelection = false;
    guiSession_.selectedRowIndex = 0;
    signalPanelViewModel_.setSelectedRow(nullptr);
  }
  else
  {
    guiSession_.statusMessage = "Filters reset. Scan the file to build the full in-memory row list.";
  }
  isRefreshPending_ = false;
}

void GuiApplication::renderMenuBar()
{
  if(ImGui::BeginMainMenuBar())
  {
    if(ImGui::BeginMenu("File"))
    {
      if(ImGui::MenuItem("Scan File", "Ctrl+R"))
      {
        triggerScanFile();
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
  ImGui::SetNextWindowSize(ImVec2(520.0F, 235.0F), ImGuiCond_FirstUseEver);
  if(!ImGui::Begin("Session Overview"))
  {
    ImGui::End();
    return;
  }

  const auto currentWallClock = std::chrono::system_clock::now();
  ImGui::TextWrapped("%s", guiSession_.statusMessage.c_str());
  ImGui::Separator();
  ImGui::Text("Trace: %s", guiSession_.tracePath.empty() ? "<none>" : guiSession_.tracePath.c_str());
  ImGui::Text("DBC: %s", guiSession_.dbcPath.empty() ? "<none>" : guiSession_.dbcPath.c_str());
  ImGui::Text("Full rows in RAM: %zu", traceTableViewModel_.fullRowCount());
  ImGui::Text("Current matches: %zu", traceTableViewModel_.visibleRows().size());
  ImGui::Text("Last filter scanned rows: %" PRIu64, guiSession_.runSummary.scannedEvents);
  ImGui::Text("Last filter matches: %" PRIu64, guiSession_.runSummary.matchedEvents);
  ImGui::Text("Visible rows: %zu", traceTableViewModel_.visibleRows().size());
  ImGui::Text("Decode available: %s", traceTableViewModel_.hasDecodedRows() ? "yes" : "no");
  ImGui::Text("Full dataset decoded: %s", traceTableViewModel_.fullDatasetHasDecodedRows() ? "yes" : "no");
  ImGui::Text("Action state: %s", operationLabel(traceTableViewModel_.activeOperation()));
  if(traceTableViewModel_.isOperationInProgress())
  {
    const TraceTableViewModel::ProgressSnapshot progressSnapshot = traceTableViewModel_.progressSnapshot();
    const auto refreshStartSteadyClock = traceTableViewModel_.operationStartSteadyClock();
    const auto refreshStartWallClock = traceTableViewModel_.operationStartWallClock();
    float progressFraction = 0.0F;
    std::string progressLabel = "Starting action...";
    std::optional<std::chrono::system_clock::time_point> estimatedFinishTime;
    if(traceTableViewModel_.activeOperation() == TraceTableViewModel::ActiveOperation::ScanFile &&
       progressSnapshot.totalBytes > 0U)
    {
      const std::uint64_t parsedBytes = std::min(progressSnapshot.bytesParsed, progressSnapshot.totalBytes);
      progressFraction =
          static_cast<float>(static_cast<double>(parsedBytes) / static_cast<double>(progressSnapshot.totalBytes));
      progressLabel = formatByteCount(parsedBytes) + " / " + formatByteCount(progressSnapshot.totalBytes) + " parsed";
    }
    else if(progressSnapshot.totalBytes > 0U)
    {
      const std::uint64_t parsedRows = std::min(progressSnapshot.bytesParsed, progressSnapshot.totalBytes);
      progressFraction =
          static_cast<float>(static_cast<double>(parsedRows) / static_cast<double>(progressSnapshot.totalBytes));

      if(traceTableViewModel_.activeOperation() == TraceTableViewModel::ActiveOperation::FilterFullDataset)
      {
        progressLabel =
            "Filtering full rows: " + std::to_string(parsedRows) + " / " + std::to_string(progressSnapshot.totalBytes);
      }
      else if(traceTableViewModel_.activeOperation() == TraceTableViewModel::ActiveOperation::FilterCurrentMatches)
      {
        progressLabel =
            "Refining current matches: " + std::to_string(parsedRows) + " / " + std::to_string(progressSnapshot.totalBytes);
      }
      else
      {
        progressLabel = std::to_string(parsedRows) + " / " + std::to_string(progressSnapshot.totalBytes) + " rows checked";
      }
    }

    if(progressFraction > 0.0001F)
    {
      const auto elapsed = std::chrono::steady_clock::now() - refreshStartSteadyClock;
      const double elapsedSeconds =
          std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
      const double estimatedRemainingSeconds =
          std::max(0.0, (elapsedSeconds / static_cast<double>(progressFraction)) - elapsedSeconds);
      estimatedFinishTime = currentWallClock + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                                   std::chrono::duration<double>(estimatedRemainingSeconds));
      ImGui::Text(
          "Now: %s | ETA finish: %s | Elapsed: %s",
          formatClockTime(currentWallClock).c_str(),
          formatClockTime(*estimatedFinishTime).c_str(),
          formatDuration(std::chrono::duration_cast<std::chrono::seconds>(elapsed)).c_str());
    }
    else
    {
      const auto elapsed = std::chrono::steady_clock::now() - refreshStartSteadyClock;
      ImGui::Text(
          "Now: %s | Started: %s | Elapsed: %s",
          formatClockTime(currentWallClock).c_str(),
          formatClockTime(refreshStartWallClock).c_str(),
          formatDuration(std::chrono::duration_cast<std::chrono::seconds>(elapsed)).c_str());
    }

    ImGui::ProgressBar(progressFraction, ImVec2(-1.0F, 0.0F), progressLabel.c_str());
    ImGui::Text("Live: scanned=%" PRIu64 " matched=%" PRIu64, progressSnapshot.scannedEvents, progressSnapshot.matchedEvents);
    if(ImGui::Button("Cancel Running Action"))
    {
      traceTableViewModel_.cancelActiveOperation();
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
  ImGui::TextDisabled("Workflow: Scan File -> Apply From Full Rows -> Refine Current Matches when needed.");
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

  if(ImGui::Button("Reset Filters"))
  {
    resetFilters();
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
    markFilterRefreshNeeded();
  }

  ImGui::Spacing();
  if(ImGui::Button("Scan File", ImVec2(-1.0F, 0.0F)))
  {
    triggerScanFile();
  }
  if(ImGui::Button("Apply From Full Rows", ImVec2(-1.0F, 0.0F)))
  {
    applyFiltersFromFullDataset();
  }
  if(ImGui::Button("Refine Current Matches", ImVec2(-1.0F, 0.0F)))
  {
    applyFiltersToCurrentMatches();
  }
  if(traceTableViewModel_.isOperationInProgress())
  {
    if(ImGui::Button("Cancel Action", ImVec2(-1.0F, 0.0F)))
    {
      traceTableViewModel_.cancelActiveOperation();
    }
  }

  if(wasEdited)
  {
    markFilterRefreshNeeded();
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
    ImGui::TextDisabled("Scan the file and apply a filter to inspect timestamps.");
    ImGui::End();
    return;
  }

  if(ImGui::Button("Zoom In"))
  {
    timelineViewModel_.zoomIn();
    queryPanelViewModel_.enableTimestampRange = true;
    queryPanelViewModel_.timestampStartText = std::to_string(timelineViewModel_.visibleStartTimestampNs());
    queryPanelViewModel_.timestampEndText = std::to_string(timelineViewModel_.visibleEndTimestampNs());
    markFilterRefreshNeeded();
  }
  ImGui::SameLine();
  if(ImGui::Button("Zoom Out"))
  {
    timelineViewModel_.zoomOut();
    queryPanelViewModel_.enableTimestampRange = true;
    queryPanelViewModel_.timestampStartText = std::to_string(timelineViewModel_.visibleStartTimestampNs());
    queryPanelViewModel_.timestampEndText = std::to_string(timelineViewModel_.visibleEndTimestampNs());
    markFilterRefreshNeeded();
  }
  ImGui::SameLine();
  if(ImGui::Button("Reset Range"))
  {
    queryPanelViewModel_.enableTimestampRange = false;
    markFilterRefreshNeeded();
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
    markFilterRefreshNeeded();
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
  const std::vector<std::size_t> filteredRowIndices = filteredTraceRowIndices();
  if(filteredRowIndices.empty())
  {
    ImGui::TextDisabled(rows.empty() ? "No rows to display." : "All current rows are hidden by the column filter.");
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
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);

    ImGui::TableSetColumnIndex(0);
    ImGui::TableHeader("Ordinal");
    ImGui::TableSetColumnIndex(1);
    ImGui::TableHeader("Timestamp");
    ImGui::TableSetColumnIndex(2);
    ImGui::TableHeader("CAN ID");
    if(ImGui::BeginPopupContextItem("can_id_column_filter"))
    {
      bool didChangeColumnFilter = false;
      std::vector<std::uint32_t> availableCanIds;
      availableCanIds.reserve(rows.size());
      for(const can_app::QueryResultRow &queryResultRow : rows)
      {
        if(std::find(availableCanIds.begin(), availableCanIds.end(), queryResultRow.canEvent.canId) == availableCanIds.end())
        {
          availableCanIds.push_back(queryResultRow.canEvent.canId);
        }
      }
      std::sort(availableCanIds.begin(), availableCanIds.end());
      if(ImGui::MenuItem("Enable All CAN IDs"))
      {
        traceTableColumnFilterState_.disabledCanIds.clear();
        didChangeColumnFilter = true;
      }
      ImGui::Separator();
      for(const std::uint32_t canId : availableCanIds)
      {
        if(ImGui::PushID(static_cast<int>(canId)); true)
        {
          bool isEnabled = traceTableColumnFilterState_.disabledCanIds.find(canId) ==
                           traceTableColumnFilterState_.disabledCanIds.end();
          const std::string label = formatCanId(canId);
          if(ImGui::Checkbox(label.c_str(), &isEnabled))
          {
            didChangeColumnFilter = true;
            if(isEnabled)
            {
              traceTableColumnFilterState_.disabledCanIds.erase(canId);
            }
            else
            {
              traceTableColumnFilterState_.disabledCanIds.insert(canId);
            }
          }
          ImGui::PopID();
        }
      }
      if(didChangeColumnFilter)
      {
        clearTraceTableSelection();
      }
      ImGui::EndPopup();
    }
    ImGui::TableSetColumnIndex(3);
    ImGui::TableHeader("Channel");
    ImGui::TableSetColumnIndex(4);
    ImGui::TableHeader("Frame");
    ImGui::TableSetColumnIndex(5);
    ImGui::TableHeader("Payload");
    ImGui::TableSetColumnIndex(6);
    ImGui::TableHeader("Decoded");
    if(ImGui::BeginPopupContextItem("decoded_column_filter"))
    {
      bool didChangeColumnFilter = false;
      if(ImGui::MenuItem("Show All"))
      {
        traceTableColumnFilterState_.showDecodedRows = true;
        traceTableColumnFilterState_.showUndecodedRows = true;
        didChangeColumnFilter = true;
      }
      if(ImGui::Checkbox("Decoded rows", &traceTableColumnFilterState_.showDecodedRows))
      {
        didChangeColumnFilter = true;
      }
      if(ImGui::Checkbox("Undecoded rows", &traceTableColumnFilterState_.showUndecodedRows))
      {
        didChangeColumnFilter = true;
      }
      if(didChangeColumnFilter)
      {
        clearTraceTableSelection();
      }
      ImGui::EndPopup();
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(filteredRowIndices.size()));
    while(clipper.Step())
    {
      for(int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
      {
        const std::size_t baseRowIndex = filteredRowIndices[static_cast<std::size_t>(rowIndex)];
        const can_app::QueryResultRow &queryResultRow = rows[baseRowIndex];
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        const bool isSelected = guiSession_.hasSelection && guiSession_.selectedRowIndex == baseRowIndex;
        char ordinalLabel[64];
        std::snprintf(ordinalLabel, sizeof(ordinalLabel), "%" PRIu64, queryResultRow.ordinal);
        if(ImGui::Selectable(ordinalLabel, isSelected, ImGuiSelectableFlags_SpanAllColumns))
        {
          guiSession_.selectedRowIndex = baseRowIndex;
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

void GuiApplication::renderTransientActionPopup()
{
  if(transientPopupMessage_.empty())
  {
    return;
  }

  if(std::chrono::steady_clock::now() >= transientPopupExpiry_)
  {
    transientPopupMessage_.clear();
    return;
  }

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 24.0F, viewport->WorkPos.y + 24.0F),
      ImGuiCond_Always,
      ImVec2(1.0F, 0.0F));
  ImGui::SetNextWindowBgAlpha(0.90F);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                           ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
  if(ImGui::Begin("Action Status Popup", nullptr, flags))
  {
    ImGui::TextUnformatted(transientPopupMessage_.c_str());
  }
  ImGui::End();
}

std::vector<std::size_t> GuiApplication::filteredTraceRowIndices() const
{
  std::vector<std::size_t> filteredRowIndices;
  const auto rows = traceTableViewModel_.visibleRows();
  filteredRowIndices.reserve(rows.size());
  for(std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
  {
    if(passesTraceColumnFilters(rows[rowIndex]))
    {
      filteredRowIndices.push_back(rowIndex);
    }
  }
  return filteredRowIndices;
}

bool GuiApplication::passesTraceColumnFilters(const can_app::QueryResultRow &queryResultRow) const
{
  if(traceTableColumnFilterState_.disabledCanIds.find(queryResultRow.canEvent.canId) !=
     traceTableColumnFilterState_.disabledCanIds.end())
  {
    return false;
  }

  if(queryResultRow.decodedMessage.has_value())
  {
    return traceTableColumnFilterState_.showDecodedRows;
  }

  return traceTableColumnFilterState_.showUndecodedRows;
}

void GuiApplication::clearTraceTableSelection()
{
  guiSession_.hasSelection = false;
  guiSession_.selectedRowIndex = 0;
  signalPanelViewModel_.setSelectedRow(nullptr);
}

void GuiApplication::showTransientActionPopup(const std::string &message)
{
  transientPopupMessage_ = message;
  transientPopupExpiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
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

  if(!passesTraceColumnFilters(rows[guiSession_.selectedRowIndex]))
  {
    clearTraceTableSelection();
    return;
  }

  signalPanelViewModel_.setSelectedRow(&rows[guiSession_.selectedRowIndex]);
}

void GuiApplication::markFilterRefreshNeeded()
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
  markFilterRefreshNeeded();
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
    markFilterRefreshNeeded();
  }
#else
  (void)shouldMarkRefresh;
#endif
}
} // namespace can_gui

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace can_core
{
enum class FilterField
{
  TimestampNs,
  CanId,
  Channel,
  FrameType,
  MessageName,
  SignalName,
  SignalValue,
};

enum class FilterOperator
{
  Equal,
  NotEqual,
  Less,
  LessOrEqual,
  Greater,
  GreaterOrEqual,
  Contains,
};

enum class LogicalOperator
{
  And,
  Or,
  Not,
};

using FilterValue = std::variant<std::monostate, bool, std::uint64_t, std::int64_t, double, std::string>;

struct Predicate
{
  FilterField field = FilterField::CanId;
  FilterOperator filterOperator = FilterOperator::Equal;
  FilterValue value;
};

struct FilterExpr
{
  std::optional<Predicate> predicate;
  LogicalOperator logicalOperator = LogicalOperator::And;
  std::vector<FilterExpr> children;

  static FilterExpr makePredicate(Predicate predicateValue)
  {
    FilterExpr filterExpr;
    filterExpr.predicate = std::move(predicateValue);
    return filterExpr;
  }

  [[nodiscard]] bool isLeaf() const
  {
    return predicate.has_value();
  }
};
} // namespace can_core


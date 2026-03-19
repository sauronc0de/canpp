#pragma once

#include <optional>

#include "can_core/context_request.hpp"
#include "can_core/filter_expr.hpp"

namespace can_core
{
struct QuerySpec
{
  FilterExpr rawFilter;
  std::optional<FilterExpr> decodedFilter;
  ContextRequest contextRequest;
  bool shouldDecode = false;
  bool shouldReturnRaw = true;
};

[[nodiscard]] bool requiresDecode(const QuerySpec &querySpec);
} // namespace can_core


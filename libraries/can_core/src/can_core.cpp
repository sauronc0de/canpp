#include "can_core/core_types.hpp"

namespace can_core
{
bool isValidCanId(std::uint32_t canId)
{
  constexpr std::uint32_t kStandardCanIdLimit = 0x7FF;
  constexpr std::uint32_t kExtendedCanIdLimit = 0x1FFFFFFF;
  return canId <= kStandardCanIdLimit || canId <= kExtendedCanIdLimit;
}

bool requiresDecode(const QuerySpec &querySpec)
{
  return querySpec.shouldDecode || querySpec.decodedFilter.has_value();
}
} // namespace can_core

namespace
{
constexpr can_core::CanEvent kExampleEvent{};
static_assert(kExampleEvent.payload.size() == 64, "CanEvent payload must support CAN FD");
static_assert(sizeof(kExampleEvent.payload) == 64, "CanEvent payload layout must remain fixed");
} // namespace


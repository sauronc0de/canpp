#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "can_core/core_types.hpp"

TEST_CASE("can_core validates CAN identifiers across supported ranges")
{
  CHECK(can_core::isValidCanId(0x000U));
  CHECK(can_core::isValidCanId(0x7FFU));
  CHECK(can_core::isValidCanId(0x1FFFFFFFU));
  CHECK_FALSE(can_core::isValidCanId(0x20000000U));
}

TEST_CASE("can_core exposes payload views using the active dlc")
{
  can_core::CanEvent canEvent;
  canEvent.dlc = 3;
  canEvent.payload[0] = 0x11U;
  canEvent.payload[1] = 0x22U;
  canEvent.payload[2] = 0x33U;
  canEvent.payload[3] = 0x44U;

  REQUIRE(canEvent.hasPayload());

  const auto payloadView = canEvent.payloadView();
  REQUIRE(payloadView.size() == 3U);
  CHECK(payloadView[0] == 0x11U);
  CHECK(payloadView[1] == 0x22U);
  CHECK(payloadView[2] == 0x33U);
}

TEST_CASE("can_core query model reports when decoding is required")
{
  can_core::QuerySpec querySpec;
  CHECK_FALSE(can_core::requiresDecode(querySpec));

  querySpec.shouldDecode = true;
  CHECK(can_core::requiresDecode(querySpec));

  querySpec.shouldDecode = false;
  querySpec.decodedFilter = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::MessageName, can_core::FilterOperator::Equal, std::string("VehicleStatus")});
  CHECK(can_core::requiresDecode(querySpec));
}

TEST_CASE("can_core predicate filters are leaf expressions")
{
  const can_core::Predicate predicate{
    can_core::FilterField::CanId, can_core::FilterOperator::Equal, std::uint64_t{0x123U}};
  const can_core::FilterExpr filterExpr = can_core::FilterExpr::makePredicate(predicate);

  REQUIRE(filterExpr.isLeaf());
  REQUIRE(filterExpr.predicate.has_value());
  CHECK(filterExpr.children.empty());
  CHECK(std::get<std::uint64_t>(filterExpr.predicate->value) == 0x123U);
}

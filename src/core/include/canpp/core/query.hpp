#pragma once

#include "canpp/protocol/can/dbc.hpp"
#include "canpp/trace/types.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace canpp::core {

// A parsed, side-effect-free query expression. Query objects are move-only and
// can be evaluated repeatedly against records in source order.
class Query {
public:
    struct Node;

    Query();
    ~Query();
    Query(Query&&) noexcept;
    Query& operator=(Query&&) noexcept;
    Query(const Query&) = delete;
    Query& operator=(const Query&) = delete;

    [[nodiscard]] static std::optional<Query> parse(std::string_view text, std::string& error);
    [[nodiscard]] bool matches(const trace::Record& record,
                                const protocol::can::DbcDatabase& dbc) const;

private:
    explicit Query(std::unique_ptr<Node> root);
    std::unique_ptr<Node> root_;
};

} // namespace canpp::core

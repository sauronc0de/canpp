#include "canpp/core/query.hpp"

#include "canpp/protocol/can/can_record.hpp"

#include <charconv>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace canpp::core {
namespace {

using Value = std::variant<double, std::int64_t, std::uint64_t, std::string, bool>;

struct Token {
    enum class Kind { end, identifier, number, string, lparen, rparen, eq, ne, lt, le, gt, ge, and_op, or_op, not_op };
    Kind kind{Kind::end};
    std::string text;
    std::size_t position{};
};

class Lexer {
public:
    explicit Lexer(std::string_view text) : text_(text) {}

    Token next(std::string& error) {
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\t' ||
                                             text_[position_] == '\r' || text_[position_] == '\n')) {
            ++position_;
        }
        const auto start = position_;
        if (position_ == text_.size()) {
            return {Token::Kind::end, {}, start};
        }
        const char character = text_[position_++];
        switch (character) {
        case '(':
            return {Token::Kind::lparen, {}, start};
        case ')':
            return {Token::Kind::rparen, {}, start};
        case '!':
            if (position_ < text_.size() && text_[position_] == '=') {
                ++position_;
                return {Token::Kind::ne, {}, start};
            }
            return {Token::Kind::not_op, {}, start};
        case '=':
            if (position_ < text_.size() && text_[position_] == '=') {
                ++position_;
                return {Token::Kind::eq, {}, start};
            }
            return fail("expected '=='", start, error);
        case '<':
            if (position_ < text_.size() && text_[position_] == '=') {
                ++position_;
                return {Token::Kind::le, {}, start};
            }
            return {Token::Kind::lt, {}, start};
        case '>':
            if (position_ < text_.size() && text_[position_] == '=') {
                ++position_;
                return {Token::Kind::ge, {}, start};
            }
            return {Token::Kind::gt, {}, start};
        case '&':
            if (position_ < text_.size() && text_[position_] == '&') {
                ++position_;
                return {Token::Kind::and_op, {}, start};
            }
            return fail("expected '&&'", start, error);
        case '|':
            if (position_ < text_.size() && text_[position_] == '|') {
                ++position_;
                return {Token::Kind::or_op, {}, start};
            }
            return fail("expected '||'", start, error);
        case '\'':
        case '"': {
            const char quote = character;
            std::string value;
            while (position_ < text_.size()) {
                const char current = text_[position_++];
                if (current == quote) {
                    return {Token::Kind::string, std::move(value), start};
                }
                if (current == '\\') {
                    if (position_ == text_.size()) {
                        return fail("unterminated escape", start, error);
                    }
                    const char escaped = text_[position_++];
                    switch (escaped) {
                    case 'n': value.push_back('\n'); break;
                    case 'r': value.push_back('\r'); break;
                    case 't': value.push_back('\t'); break;
                    case '\\': value.push_back('\\'); break;
                    case '\'': value.push_back('\''); break;
                    case '"': value.push_back('"'); break;
                    default: value.push_back(escaped); break;
                    }
                } else {
                    value.push_back(current);
                }
            }
            return fail("unterminated string", start, error);
        }
        default:
            break;
        }
        if (std::isalpha(static_cast<unsigned char>(character)) || character == '_') {
            while (position_ < text_.size()) {
                const char current = text_[position_];
                if (!std::isalnum(static_cast<unsigned char>(current)) && current != '_' && current != '.') {
                    break;
                }
                ++position_;
            }
            return {Token::Kind::identifier,
                    std::string(text_.substr(start, position_ - start)), start};
        }
        const bool signed_number = (character == '-' || character == '+') && position_ < text_.size() &&
                                    (std::isdigit(static_cast<unsigned char>(text_[position_])) ||
                                     text_[position_] == '.');
        if (std::isdigit(static_cast<unsigned char>(character)) || character == '.' || signed_number) {
            while (position_ < text_.size()) {
                const char current = text_[position_];
                if (!std::isalnum(static_cast<unsigned char>(current)) && current != '.' && current != '+' &&
                    current != '-') {
                    break;
                }
                ++position_;
            }
            const auto value = std::string(text_.substr(start, position_ - start));
            if (value.size() > 2U && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
                std::uint64_t parsed{};
                const auto result = std::from_chars(value.data() + 2, value.data() + value.size(), parsed, 16);
                if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
                    return fail("invalid number", start, error);
                }
            } else {
                char* end = nullptr;
                (void)std::strtod(value.c_str(), &end);
                if (end == value.c_str() || *end != '\0') {
                    return fail("invalid number", start, error);
                }
            }
            return {Token::Kind::number, value, start};
        }
        return fail("unexpected character", start, error);
    }

private:
    Token fail(const char* message, std::size_t position, std::string& error) const {
        error = std::string(message) + " at position " + std::to_string(position);
        return {Token::Kind::end, {}, position};
    }
    std::string_view text_;
    std::size_t position_{};
};

} // namespace

struct Query::Node {
    enum class Kind { literal, reference, unary_not, compare, and_op, or_op };
    enum class Compare { eq, ne, lt, le, gt, ge };
    Kind kind{Kind::literal};
    Compare compare{Compare::eq};
    Value literal{false};
    std::string reference;
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
};

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : lexer_(text) { advance(); }

    std::unique_ptr<Query::Node> parse(std::string& error) {
        auto root = parse_or(error);
        if (!root || !error.empty()) {
            return nullptr;
        }
        if (token_.kind != Token::Kind::end) {
            error = "unexpected token at position " + std::to_string(token_.position);
            return nullptr;
        }
        return root;
    }

private:
    void advance() {
        std::string ignored;
        token_ = lexer_.next(ignored);
        // Lexer errors are retained by the parser on the next parse operation.
        if (token_.kind == Token::Kind::end && !ignored.empty()) {
            lexer_error_ = std::move(ignored);
        }
    }

    bool okay(std::string& error) {
        if (!lexer_error_.empty()) {
            error = std::move(lexer_error_);
            return false;
        }
        return true;
    }

    std::unique_ptr<Query::Node> parse_or(std::string& error) {
        auto node = parse_and(error);
        while (node && okay(error) && token_.kind == Token::Kind::or_op) {
            advance();
            auto rhs = parse_and(error);
            if (!rhs) return nullptr;
            auto parent = std::make_unique<Query::Node>();
            parent->kind = Query::Node::Kind::or_op;
            parent->left = std::move(node);
            parent->right = std::move(rhs);
            node = std::move(parent);
        }
        return node;
    }

    std::unique_ptr<Query::Node> parse_and(std::string& error) {
        auto node = parse_comparison(error);
        while (node && okay(error) && token_.kind == Token::Kind::and_op) {
            advance();
            auto rhs = parse_comparison(error);
            if (!rhs) return nullptr;
            auto parent = std::make_unique<Query::Node>();
            parent->kind = Query::Node::Kind::and_op;
            parent->left = std::move(node);
            parent->right = std::move(rhs);
            node = std::move(parent);
        }
        return node;
    }

    std::unique_ptr<Query::Node> parse_comparison(std::string& error) {
        auto node = parse_unary(error);
        if (!node || !okay(error)) return nullptr;
        Query::Node::Compare op{};
        bool comparison = true;
        switch (token_.kind) {
        case Token::Kind::eq: op = Query::Node::Compare::eq; break;
        case Token::Kind::ne: op = Query::Node::Compare::ne; break;
        case Token::Kind::lt: op = Query::Node::Compare::lt; break;
        case Token::Kind::le: op = Query::Node::Compare::le; break;
        case Token::Kind::gt: op = Query::Node::Compare::gt; break;
        case Token::Kind::ge: op = Query::Node::Compare::ge; break;
        default: comparison = false; break;
        }
        if (!comparison) return node;
        advance();
        auto rhs = parse_unary(error);
        if (!rhs) return nullptr;
        auto parent = std::make_unique<Query::Node>();
        parent->kind = Query::Node::Kind::compare;
        parent->compare = op;
        parent->left = std::move(node);
        parent->right = std::move(rhs);
        return parent;
    }

    std::unique_ptr<Query::Node> parse_unary(std::string& error) {
        if (token_.kind == Token::Kind::not_op) {
            advance();
            auto node = parse_unary(error);
            if (!node) return nullptr;
            auto result = std::make_unique<Query::Node>();
            result->kind = Query::Node::Kind::unary_not;
            result->left = std::move(node);
            return result;
        }
        if (token_.kind == Token::Kind::lparen) {
            advance();
            auto node = parse_or(error);
            if (!node) return nullptr;
            if (token_.kind != Token::Kind::rparen) {
                error = "expected ')' at position " + std::to_string(token_.position);
                return nullptr;
            }
            advance();
            return node;
        }
        if (token_.kind == Token::Kind::number) {
            const auto& text = token_.text;
            const bool hexadecimal = text.size() > 2U && text[0] == '0' &&
                                     (text[1] == 'x' || text[1] == 'X');
            const bool integer = hexadecimal ||
                                 text.find_first_of(".eE") == std::string::npos;
            auto node = std::make_unique<Query::Node>();
            if (hexadecimal) {
                std::uint64_t parsed{};
                const auto result = std::from_chars(text.data() + 2, text.data() + text.size(), parsed, 16);
                if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
                    error = "invalid number at position " + std::to_string(token_.position);
                    return nullptr;
                }
                node->literal = parsed;
            } else if (integer) {
                const bool negative = !text.empty() && text.front() == '-';
                const auto* first = text.data() + (text.starts_with('+') ? 1U : 0U);
                if (negative) {
                    std::int64_t parsed{};
                    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 10);
                    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
                        error = "invalid number at position " + std::to_string(token_.position);
                        return nullptr;
                    }
                    node->literal = parsed;
                } else {
                    std::uint64_t parsed{};
                    const auto result = std::from_chars(first, text.data() + text.size(), parsed, 10);
                    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
                        error = "invalid number at position " + std::to_string(token_.position);
                        return nullptr;
                    }
                    node->literal = parsed;
                }
            } else {
                char* end = nullptr;
                const auto number = std::strtod(text.c_str(), &end);
                if (end == text.c_str() || *end != '\0' || !std::isfinite(number)) {
                    error = "invalid number at position " + std::to_string(token_.position);
                    return nullptr;
                }
                node->literal = number;
            }
            advance();
            return node;
        }
        if (token_.kind == Token::Kind::string) {
            auto node = std::make_unique<Query::Node>();
            node->literal = token_.text;
            advance();
            return node;
        }
        if (token_.kind == Token::Kind::identifier) {
            auto node = std::make_unique<Query::Node>();
            if (token_.text == "true" || token_.text == "false") {
                node->literal = token_.text == "true";
            } else if (token_.text == "signal" || token_.text == "message" || token_.text == "record") {
                error = "incomplete reference at position " + std::to_string(token_.position);
                return nullptr;
            } else if (token_.text.rfind("signal.", 0U) == 0U || token_.text == "message.name" ||
                       token_.text == "message.id" || token_.text == "message.extended" ||
                       token_.text == "record.stream_id" || token_.text == "record.protocol" ||
                       token_.text == "record.direction" || token_.text == "timestamp_ns" || token_.text == "time") {
                node->kind = Query::Node::Kind::reference;
                node->reference = token_.text;
                if (node->reference.rfind("signal.", 0U) == 0U && node->reference.size() == 7U) {
                    error = "signal name is empty at position " + std::to_string(token_.position);
                    return nullptr;
                }
            } else {
                error = "unknown reference '" + token_.text + "' at position " +
                        std::to_string(token_.position);
                return nullptr;
            }
            advance();
            return node;
        }
        if (!okay(error)) return nullptr;
        error = "expected expression at position " + std::to_string(token_.position);
        return nullptr;
    }

    Lexer lexer_;
    Token token_;
    std::string lexer_error_;
};

std::optional<Value> reference_value(const std::string& reference,
                                     const trace::Record& record,
                                     const protocol::can::DbcDatabase& dbc) {
    if (reference == "timestamp_ns") return record.timestamp_ns;
    if (reference == "time") return static_cast<double>(record.timestamp_ns) / 1'000'000'000.0;
    if (reference == "record.stream_id") return static_cast<std::uint64_t>(record.stream_id);
    if (reference == "record.protocol") {
        switch (record.protocol) {
        case trace::ProtocolId::can: return std::string("can");
        case trace::ProtocolId::uart: return std::string("uart");
        case trace::ProtocolId::spi: return std::string("spi");
        case trace::ProtocolId::i2c: return std::string("i2c");
        case trace::ProtocolId::lin: return std::string("lin");
        case trace::ProtocolId::ethernet: return std::string("ethernet");
        case trace::ProtocolId::custom: return std::string("custom");
        default: return std::string("unknown");
        }
    }
    if (reference == "record.direction") {
        switch (record.direction) {
        case trace::Direction::rx: return std::string("rx");
        case trace::Direction::tx: return std::string("tx");
        case trace::Direction::bidirectional: return std::string("bidirectional");
        default: return std::string("unknown");
        }
    }
    const auto frame = protocol::can::decode(record);
    if (!frame) return std::nullopt;
    if (reference == "message.id") return static_cast<std::uint64_t>(frame->can_id);
    if (reference == "message.extended") return frame->extended;
    if (reference == "message.name") {
        if (const auto* message = dbc.find_message(frame->can_id, frame->extended); message != nullptr) {
            return message->name;
        }
        return frame->message_name;
    }
    if (reference.rfind("signal.", 0U) == 0U) {
        const auto name = reference.substr(7U);
        for (const auto& value : dbc.decode(frame->can_id, frame->extended, frame->data)) {
            if (value.name == name) return value.value;
        }
    }
    return std::nullopt;
}

std::optional<bool> boolean_value(const std::optional<Value>& value) {
    if (!value || !std::holds_alternative<bool>(*value)) return std::nullopt;
    return std::get<bool>(*value);
}

std::optional<bool> evaluate(const Query::Node& node,
                             const trace::Record& record,
                             const protocol::can::DbcDatabase& dbc) {
    if (node.kind == Query::Node::Kind::literal) {
        return boolean_value(node.literal);
    }
    if (node.kind == Query::Node::Kind::reference) {
        return boolean_value(reference_value(node.reference, record, dbc));
    }
    if (node.kind == Query::Node::Kind::unary_not) {
        const auto value = evaluate(*node.left, record, dbc);
        return value ? std::optional<bool>{!*value} : std::nullopt;
    }
    if (node.kind == Query::Node::Kind::and_op || node.kind == Query::Node::Kind::or_op) {
        const auto left = evaluate(*node.left, record, dbc);
        if (node.kind == Query::Node::Kind::and_op && left && !*left) return false;
        if (node.kind == Query::Node::Kind::or_op && left && *left) return true;

        // An unavailable operand is unknown, not false.  Evaluate the other
        // operand so a decisive value can still determine the result (false
        // for AND, true for OR), while retaining short-circuiting above.
        const auto right = evaluate(*node.right, record, dbc);
        if (node.kind == Query::Node::Kind::and_op) {
            if (right && !*right) return false;
            if (left && right) return *left && *right;
        } else {
            if (right && *right) return true;
            if (left && right) return *left || *right;
        }
        return std::nullopt;
    }
    const auto left = [&]() -> std::optional<Value> {
        if (node.left->kind == Query::Node::Kind::literal) return node.left->literal;
        if (node.left->kind == Query::Node::Kind::reference) return reference_value(node.left->reference, record, dbc);
        const auto result = evaluate(*node.left, record, dbc);
        return result ? std::optional<Value>{*result} : std::nullopt;
    }();
    const auto right = [&]() -> std::optional<Value> {
        if (node.right->kind == Query::Node::Kind::literal) return node.right->literal;
        if (node.right->kind == Query::Node::Kind::reference) return reference_value(node.right->reference, record, dbc);
        const auto result = evaluate(*node.right, record, dbc);
        return result ? std::optional<Value>{*result} : std::nullopt;
    }();
    if (!left || !right) return std::nullopt;

    // Integer values are kept separate from doubles so timestamps and IDs do
    // not lose precision when they exceed the exact range of a double.
    const auto integer_comparison = [](const Value& a, const Value& b) -> std::optional<int> {
        const auto signed_value = [](const Value& value) { return std::holds_alternative<std::int64_t>(value); };
        const auto unsigned_value = [](const Value& value) { return std::holds_alternative<std::uint64_t>(value); };
        if ((!signed_value(a) && !unsigned_value(a)) || (!signed_value(b) && !unsigned_value(b))) {
            return std::nullopt;
        }
        if (signed_value(a) && signed_value(b)) {
            const auto lhs = std::get<std::int64_t>(a);
            const auto rhs = std::get<std::int64_t>(b);
            return lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
        }
        if (unsigned_value(a) && unsigned_value(b)) {
            const auto lhs = std::get<std::uint64_t>(a);
            const auto rhs = std::get<std::uint64_t>(b);
            return lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
        }
        const auto& signed_operand = signed_value(a) ? a : b;
        const auto& unsigned_operand = unsigned_value(a) ? a : b;
        const auto signed_number = std::get<std::int64_t>(signed_operand);
        if (signed_number < 0) return signed_value(a) ? -1 : 1;
        const auto signed_as_unsigned = static_cast<std::uint64_t>(signed_number);
        const auto unsigned_number = std::get<std::uint64_t>(unsigned_operand);
        if (signed_as_unsigned == unsigned_number) return 0;
        const auto signed_is_less = signed_as_unsigned < unsigned_number;
        return signed_value(a) ? (signed_is_less ? -1 : 1) : (signed_is_less ? 1 : -1);
    };

    const auto is_numeric = [](const Value& value) {
        return std::holds_alternative<double>(value) || std::holds_alternative<std::int64_t>(value) ||
               std::holds_alternative<std::uint64_t>(value);
    };
    const auto has_double = std::holds_alternative<double>(*left) || std::holds_alternative<double>(*right);
    if (has_double) {
        if (!is_numeric(*left) || !is_numeric(*right)) return std::nullopt;
        const auto as_double = [](const Value& value) {
            if (std::holds_alternative<double>(value)) return std::get<double>(value);
            if (std::holds_alternative<std::int64_t>(value)) {
                return static_cast<double>(std::get<std::int64_t>(value));
            }
            return static_cast<double>(std::get<std::uint64_t>(value));
        };
        const auto lhs = as_double(*left);
        const auto rhs = as_double(*right);
        switch (node.compare) {
        case Query::Node::Compare::eq: return lhs == rhs;
        case Query::Node::Compare::ne: return lhs != rhs;
        case Query::Node::Compare::lt: return lhs < rhs;
        case Query::Node::Compare::le: return lhs <= rhs;
        case Query::Node::Compare::gt: return lhs > rhs;
        case Query::Node::Compare::ge: return lhs >= rhs;
        }
    }

    const auto integer_comparison_result = integer_comparison(*left, *right);
    if (integer_comparison_result) {
        switch (node.compare) {
        case Query::Node::Compare::eq: return *integer_comparison_result == 0;
        case Query::Node::Compare::ne: return *integer_comparison_result != 0;
        case Query::Node::Compare::lt: return *integer_comparison_result < 0;
        case Query::Node::Compare::le: return *integer_comparison_result <= 0;
        case Query::Node::Compare::gt: return *integer_comparison_result > 0;
        case Query::Node::Compare::ge: return *integer_comparison_result >= 0;
        }
    }

    if (std::holds_alternative<std::string>(*left) && std::holds_alternative<std::string>(*right)) {
        const auto& a = std::get<std::string>(*left);
        const auto& b = std::get<std::string>(*right);
        switch (node.compare) {
        case Query::Node::Compare::eq: return a == b;
        case Query::Node::Compare::ne: return a != b;
        case Query::Node::Compare::lt: return a < b;
        case Query::Node::Compare::le: return a <= b;
        case Query::Node::Compare::gt: return a > b;
        case Query::Node::Compare::ge: return a >= b;
        }
    }
    if (std::holds_alternative<bool>(*left) && std::holds_alternative<bool>(*right)) {
        const auto a = std::get<bool>(*left);
        const auto b = std::get<bool>(*right);
        if (node.compare == Query::Node::Compare::eq) return a == b;
        if (node.compare == Query::Node::Compare::ne) return a != b;
    }
    return std::nullopt;
}

} // namespace

Query::Query() = default;
Query::~Query() = default;
Query::Query(Query&&) noexcept = default;
Query& Query::operator=(Query&&) noexcept = default;
Query::Query(std::unique_ptr<Node> root) : root_(std::move(root)) {}

std::optional<Query> Query::parse(std::string_view text, std::string& error) {
    if (text.empty()) {
        error = "empty expression at position 0";
        return std::nullopt;
    }
    Parser parser(text);
    auto root = parser.parse(error);
    if (!root) return std::nullopt;
    return Query(std::move(root));
}

bool Query::matches(const trace::Record& record, const protocol::can::DbcDatabase& dbc) const {
    return root_ != nullptr && evaluate(*root_, record, dbc).value_or(false);
}

} // namespace canpp::core

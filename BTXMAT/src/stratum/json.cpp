#include "stratum/json.hpp"

#include <charconv>
#include <cctype>
#include <sstream>

namespace superhero::stratum {
namespace {

struct Parser {
    std::string_view text;
    size_t pos{0};

    void skip_ws() {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    }

    bool consume(char c) {
        skip_ws();
        if (pos < text.size() && text[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }

    std::optional<std::string> parse_string() {
        skip_ws();
        if (pos >= text.size() || text[pos] != '"') return std::nullopt;
        ++pos;
        std::string out;
        while (pos < text.size()) {
            char c = text[pos++];
            if (c == '"') return out;
            if (c == '\\' && pos < text.size()) {
                c = text[pos++];
                if (c == 'n') out.push_back('\n');
                else if (c == 'r') out.push_back('\r');
                else if (c == 't') out.push_back('\t');
                else out.push_back(c);
            } else {
                out.push_back(c);
            }
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parse_value();
    std::optional<JsonValue> parse_object();
    std::optional<JsonValue> parse_array();
};

std::optional<JsonValue> Parser::parse_array() {
    if (!consume('[')) return std::nullopt;
    JsonValue out;
    out.type = JsonValue::Type::Array;
    skip_ws();
    if (consume(']')) return out;
    while (true) {
        auto val = parse_value();
        if (!val) return std::nullopt;
        out.array_value.push_back(*val);
        skip_ws();
        if (consume(']')) break;
        if (!consume(',')) return std::nullopt;
    }
    return out;
}

std::optional<JsonValue> Parser::parse_object() {
    if (!consume('{')) return std::nullopt;
    JsonValue out;
    out.type = JsonValue::Type::Object;
    skip_ws();
    if (consume('}')) return out;
    while (true) {
        auto key = parse_string();
        if (!key || !consume(':')) return std::nullopt;
        auto val = parse_value();
        if (!val) return std::nullopt;
        out.object_value.emplace_back(*key, *val);
        skip_ws();
        if (consume('}')) break;
        if (!consume(',')) return std::nullopt;
    }
    return out;
}

std::optional<JsonValue> Parser::parse_value() {
    skip_ws();
    if (pos >= text.size()) return std::nullopt;
    if (text[pos] == '"') {
        auto s = parse_string();
        if (!s) return std::nullopt;
        JsonValue out;
        out.type = JsonValue::Type::String;
        out.string_value = *s;
        return out;
    }
    if (text[pos] == '{') return parse_object();
    if (text[pos] == '[') return parse_array();
    if (text.compare(pos, 4, "true") == 0) {
        pos += 4;
        JsonValue out;
        out.type = JsonValue::Type::Bool;
        out.bool_value = true;
        return out;
    }
    if (text.compare(pos, 5, "false") == 0) {
        pos += 5;
        JsonValue out;
        out.type = JsonValue::Type::Bool;
        out.bool_value = false;
        return out;
    }
    if (text.compare(pos, 4, "null") == 0) {
        pos += 4;
        return JsonValue{};
    }
    const size_t start = pos;
    if (text[pos] == '-') ++pos;
    while (pos < text.size() && (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.')) ++pos;
    double d = 0;
    auto sv = text.substr(start, pos - start);
    std::from_chars(sv.data(), sv.data() + sv.size(), d);
    JsonValue out;
    if (sv.find('.') == std::string_view::npos) {
        out.type = JsonValue::Type::Int;
        out.int_value = static_cast<int64_t>(d);
    } else {
        out.type = JsonValue::Type::Number;
        out.number_value = d;
    }
    return out;
}

void stringify_impl(std::ostringstream& out, const JsonValue& value) {
    switch (value.type) {
        case JsonValue::Type::Null:
            out << "null";
            break;
        case JsonValue::Type::Bool:
            out << (value.bool_value ? "true" : "false");
            break;
        case JsonValue::Type::Int:
            out << value.int_value;
            break;
        case JsonValue::Type::Number:
            out << value.number_value;
            break;
        case JsonValue::Type::String:
            out << '"';
            for (char c : value.string_value) {
                if (c == '"' || c == '\\') out << '\\';
                out << c;
            }
            out << '"';
            break;
        case JsonValue::Type::Array:
            out << '[';
            for (size_t i = 0; i < value.array_value.size(); ++i) {
                if (i) out << ',';
                stringify_impl(out, value.array_value[i]);
            }
            out << ']';
            break;
        case JsonValue::Type::Object:
            out << '{';
            for (size_t i = 0; i < value.object_value.size(); ++i) {
                if (i) out << ',';
                out << '"';
                for (char c : value.object_value[i].first) {
                    if (c == '"' || c == '\\') out << '\\';
                    out << c;
                }
                out << '"';
                out << ':';
                stringify_impl(out, value.object_value[i].second);
            }
            out << '}';
            break;
    }
}

}  // namespace

std::optional<JsonValue> parse_json(std::string_view text) {
    Parser p{text};
    return p.parse_value();
}

std::string stringify_json(const JsonValue& value) {
    std::ostringstream out;
    stringify_impl(out, value);
    return out.str();
}

std::string stringify_message(const std::vector<std::pair<std::string, JsonValue>>& object) {
    JsonValue root;
    root.type = JsonValue::Type::Object;
    root.object_value = object;
    return stringify_json(root);
}

const JsonValue* json_get(const JsonValue& value, std::string_view key) {
    if (value.type != JsonValue::Type::Object) return nullptr;
    for (const auto& [k, v] : value.object_value) {
        if (k == key) return &v;
    }
    return nullptr;
}

std::string json_string(const JsonValue& value) {
    if (value.type == JsonValue::Type::String) return value.string_value;
    return {};
}

int64_t json_int(const JsonValue& value) {
    if (value.type == JsonValue::Type::Int) return value.int_value;
    if (value.type == JsonValue::Type::Number) return static_cast<int64_t>(value.number_value);
    return 0;
}

bool json_bool(const JsonValue& value) {
    if (value.type == JsonValue::Type::Bool) return value.bool_value;
    return false;
}

std::string json_error_message(const JsonValue& error_value) {
    if (error_value.type == JsonValue::Type::String) return error_value.string_value;
    if (error_value.type == JsonValue::Type::Object) {
        const int64_t code = json_get(error_value, "code") ? json_int(*json_get(error_value, "code")) : 0;
        const std::string msg = json_get(error_value, "message") ? json_string(*json_get(error_value, "message")) : "unknown pool error";
        if (code != 0) return msg + " (code " + std::to_string(code) + ")";
        return msg;
    }
    if (error_value.type != JsonValue::Type::Array || error_value.array_value.size() < 2) return "unknown pool error";
    const int64_t code = json_int(error_value.array_value[0]);
    const std::string msg = json_string(error_value.array_value[1]);
    if (msg.empty()) return "pool error code " + std::to_string(code);
    return msg + " (code " + std::to_string(code) + ")";
}

}  // namespace superhero::stratum

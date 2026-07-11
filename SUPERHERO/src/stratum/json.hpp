#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace superhero::stratum {

struct JsonValue {
    enum class Type { Null, Bool, Number, Int, String, Array, Object } type{Type::Null};
    bool bool_value{false};
    double number_value{0};
    int64_t int_value{0};
    std::string string_value;
    std::vector<JsonValue> array_value;
    std::vector<std::pair<std::string, JsonValue>> object_value;
};

std::optional<JsonValue> parse_json(std::string_view text);
std::string stringify_json(const JsonValue& value);
std::string stringify_message(const std::vector<std::pair<std::string, JsonValue>>& object);

const JsonValue* json_get(const JsonValue& value, std::string_view key);
std::string json_string(const JsonValue& value);
int64_t json_int(const JsonValue& value);
bool json_bool(const JsonValue& value);
std::string json_error_message(const JsonValue& error_value);

}  // namespace superhero::stratum

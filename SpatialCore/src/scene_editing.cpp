#include "hss/scene_editing.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#include <type_traits>

namespace hss::scene_editing {
namespace {

struct Json;
using Array = std::vector<Json>;
using Object = std::map<std::string, Json, std::less<>>;

struct Json {
  using Value =
      std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;
  Value value{nullptr};

  Json() = default;
  Json(std::nullptr_t) : value(nullptr) {}
  Json(bool v) : value(v) {}
  Json(double v) : value(v) {}
  Json(std::string v) : value(std::move(v)) {}
  Json(const char *v) : value(std::string(v)) {}
  Json(Array v) : value(std::move(v)) {}
  Json(Object v) : value(std::move(v)) {}

  [[nodiscard]] bool is_null() const {
    return std::holds_alternative<std::nullptr_t>(value);
  }
  [[nodiscard]] bool is_bool() const {
    return std::holds_alternative<bool>(value);
  }
  [[nodiscard]] bool is_number() const {
    return std::holds_alternative<double>(value);
  }
  [[nodiscard]] bool is_string() const {
    return std::holds_alternative<std::string>(value);
  }
  [[nodiscard]] bool is_array() const {
    return std::holds_alternative<Array>(value);
  }
  [[nodiscard]] bool is_object() const {
    return std::holds_alternative<Object>(value);
  }
  [[nodiscard]] bool &boolean() { return std::get<bool>(value); }
  [[nodiscard]] double &number() { return std::get<double>(value); }
  [[nodiscard]] std::string &string() { return std::get<std::string>(value); }
  [[nodiscard]] Array &array() { return std::get<Array>(value); }
  [[nodiscard]] Object &object() { return std::get<Object>(value); }
  [[nodiscard]] const bool &boolean() const { return std::get<bool>(value); }
  [[nodiscard]] const double &number() const { return std::get<double>(value); }
  [[nodiscard]] const std::string &string() const {
    return std::get<std::string>(value);
  }
  [[nodiscard]] const Array &array() const { return std::get<Array>(value); }
  [[nodiscard]] const Object &object() const { return std::get<Object>(value); }
};

class JsonParser {
public:
  explicit JsonParser(std::string_view input) : input_(input) {}

  Json parse() {
    skip_ws();
    Json result = parse_value(0U);
    skip_ws();
    if (position_ != input_.size())
      fail("trailing JSON bytes");
    return result;
  }

private:
  [[noreturn]] void fail(const char *message) const {
    throw std::invalid_argument(message);
  }

  void skip_ws() {
    while (position_ < input_.size() &&
           (input_[position_] == ' ' || input_[position_] == '\n' ||
            input_[position_] == '\r' || input_[position_] == '\t'))
      ++position_;
  }

  Json parse_value(size_t depth) {
    if (depth > 64U || position_ >= input_.size())
      fail("invalid JSON depth/value");
    const char c = input_[position_];
    if (c == '{')
      return parse_object(depth + 1U);
    if (c == '[')
      return parse_array(depth + 1U);
    if (c == '"')
      return Json(parse_string());
    if (c == 't' && take("true"))
      return Json(true);
    if (c == 'f' && take("false"))
      return Json(false);
    if (c == 'n' && take("null"))
      return Json(nullptr);
    if (c == '-' || (c >= '0' && c <= '9'))
      return Json(parse_number());
    fail("invalid JSON token");
  }

  bool take(std::string_view text) {
    if (input_.substr(position_, text.size()) != text)
      return false;
    position_ += text.size();
    return true;
  }

  static unsigned hex_digit(char c) {
    if (c >= '0' && c <= '9')
      return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f')
      return static_cast<unsigned>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
      return static_cast<unsigned>(c - 'A' + 10);
    throw std::invalid_argument("invalid unicode escape");
  }

  uint32_t parse_hex4() {
    if (position_ + 4U > input_.size())
      fail("truncated unicode escape");
    uint32_t value = 0U;
    for (size_t i = 0U; i < 4U; ++i)
      value = value * 16U + hex_digit(input_[position_++]);
    return value;
  }

  static void append_utf8(uint32_t cp, std::string *out) {
    if (cp <= 0x7fU) {
      out->push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ffU) {
      out->push_back(static_cast<char>(0xc0U | (cp >> 6U)));
      out->push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
    } else if (cp <= 0xffffU) {
      out->push_back(static_cast<char>(0xe0U | (cp >> 12U)));
      out->push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3fU)));
      out->push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
    } else {
      out->push_back(static_cast<char>(0xf0U | (cp >> 18U)));
      out->push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3fU)));
      out->push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3fU)));
      out->push_back(static_cast<char>(0x80U | (cp & 0x3fU)));
    }
  }

  std::string parse_string() {
    if (input_[position_++] != '"')
      fail("expected string");
    std::string result;
    while (position_ < input_.size()) {
      const unsigned char c = static_cast<unsigned char>(input_[position_++]);
      if (c == '"')
        return result;
      if (c < 0x20U)
        fail("control character in string");
      if (c != '\\') {
        result.push_back(static_cast<char>(c));
        continue;
      }
      if (position_ >= input_.size())
        fail("truncated escape");
      const char escaped = input_[position_++];
      switch (escaped) {
      case '"': result.push_back('"'); break;
      case '\\': result.push_back('\\'); break;
      case '/': result.push_back('/'); break;
      case 'b': result.push_back('\b'); break;
      case 'f': result.push_back('\f'); break;
      case 'n': result.push_back('\n'); break;
      case 'r': result.push_back('\r'); break;
      case 't': result.push_back('\t'); break;
      case 'u': {
        uint32_t cp = parse_hex4();
        if (cp >= 0xd800U && cp <= 0xdbffU) {
          if (position_ + 2U > input_.size() || input_[position_] != '\\' ||
              input_[position_ + 1U] != 'u')
            fail("unpaired high surrogate");
          position_ += 2U;
          const uint32_t low = parse_hex4();
          if (low < 0xdc00U || low > 0xdfffU)
            fail("invalid low surrogate");
          cp = 0x10000U + ((cp - 0xd800U) << 10U) + (low - 0xdc00U);
        } else if (cp >= 0xdc00U && cp <= 0xdfffU) {
          fail("unpaired low surrogate");
        }
        append_utf8(cp, &result);
        break;
      }
      default: fail("invalid escape");
      }
    }
    fail("unterminated string");
  }

  double parse_number() {
    const size_t start = position_;
    if (input_[position_] == '-')
      ++position_;
    if (position_ >= input_.size())
      fail("truncated number");
    if (input_[position_] == '0') {
      ++position_;
      if (position_ < input_.size() && input_[position_] >= '0' &&
          input_[position_] <= '9')
        fail("leading zero");
    } else {
      if (input_[position_] < '1' || input_[position_] > '9')
        fail("invalid number");
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9')
        ++position_;
    }
    if (position_ < input_.size() && input_[position_] == '.') {
      ++position_;
      const size_t fraction = position_;
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9')
        ++position_;
      if (fraction == position_)
        fail("empty fraction");
    }
    if (position_ < input_.size() &&
        (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size() &&
          (input_[position_] == '+' || input_[position_] == '-'))
        ++position_;
      const size_t exponent = position_;
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9')
        ++position_;
      if (exponent == position_)
        fail("empty exponent");
    }
    double result = 0.0;
    const std::string_view token = input_.substr(start, position_ - start);
    bool negative = false;
    size_t token_position = 0U;
    if (token[token_position] == '-') {
      negative = true;
      ++token_position;
    }
    long double significand = 0.0L;
    int fractional_digits = 0;
    bool after_decimal = false;
    int exponent = 0;
    while (token_position < token.size()) {
      const char c = token[token_position++];
      if (c == '.') {
        after_decimal = true;
        continue;
      }
      if (c == 'e' || c == 'E')
        break;
      significand = significand * 10.0L + static_cast<long double>(c - '0');
      if (after_decimal)
        ++fractional_digits;
    }
    if (token_position < token.size()) {
      bool exponent_negative = false;
      if (token[token_position] == '+' || token[token_position] == '-') {
        exponent_negative = token[token_position] == '-';
        ++token_position;
      }
      while (token_position < token.size()) {
        const int digit = token[token_position++] - '0';
        if (exponent <= 10000)
          exponent = std::min(10000, exponent * 10 + digit);
      }
      if (exponent_negative)
        exponent = -exponent;
    }
    const long double scaled =
        significand * std::pow(10.0L, static_cast<long double>(exponent -
                                                                fractional_digits));
    result = static_cast<double>(negative ? -scaled : scaled);
    if (!std::isfinite(result))
      fail("nonfinite or invalid number");
    return result;
  }

  Json parse_array(size_t depth) {
    ++position_;
    skip_ws();
    Array result;
    if (position_ < input_.size() && input_[position_] == ']') {
      ++position_;
      return Json(std::move(result));
    }
    for (;;) {
      result.push_back(parse_value(depth));
      skip_ws();
      if (position_ >= input_.size())
        fail("unterminated array");
      if (input_[position_] == ']') {
        ++position_;
        return Json(std::move(result));
      }
      if (input_[position_++] != ',')
        fail("expected array comma");
      skip_ws();
    }
  }

  Json parse_object(size_t depth) {
    ++position_;
    skip_ws();
    Object result;
    if (position_ < input_.size() && input_[position_] == '}') {
      ++position_;
      return Json(std::move(result));
    }
    for (;;) {
      if (position_ >= input_.size() || input_[position_] != '"')
        fail("expected object key");
      std::string key = parse_string();
      skip_ws();
      if (position_ >= input_.size() || input_[position_++] != ':')
        fail("expected object colon");
      skip_ws();
      Json value = parse_value(depth);
      if (!result.emplace(std::move(key), std::move(value)).second)
        fail("duplicate object key");
      skip_ws();
      if (position_ >= input_.size())
        fail("unterminated object");
      if (input_[position_] == '}') {
        ++position_;
        return Json(std::move(result));
      }
      if (input_[position_++] != ',')
        fail("expected object comma");
      skip_ws();
    }
  }

  std::string_view input_;
  size_t position_{0U};
};

void append_escaped(std::string_view input, std::string *out) {
  out->push_back('"');
  constexpr char hex[] = "0123456789abcdef";
  for (const unsigned char c : input) {
    switch (c) {
    case '"': out->append("\\\""); break;
    case '\\': out->append("\\\\"); break;
    case '\b': out->append("\\b"); break;
    case '\f': out->append("\\f"); break;
    case '\n': out->append("\\n"); break;
    case '\r': out->append("\\r"); break;
    case '\t': out->append("\\t"); break;
    default:
      if (c < 0x20U) {
        out->append("\\u00");
        out->push_back(hex[c >> 4U]);
        out->push_back(hex[c & 0x0fU]);
      } else {
        out->push_back(static_cast<char>(c));
      }
    }
  }
  out->push_back('"');
}

void serialize(const Json &json, std::string *out) {
  if (json.is_null()) {
    out->append("null");
  } else if (json.is_bool()) {
    out->append(json.boolean() ? "true" : "false");
  } else if (json.is_number()) {
    std::array<char, 64U> buffer{};
    const auto converted = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), json.number(),
        std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (converted.ec != std::errc{})
      throw std::runtime_error("number serialization failed");
    out->append(buffer.data(), converted.ptr);
  } else if (json.is_string()) {
    append_escaped(json.string(), out);
  } else if (json.is_array()) {
    out->push_back('[');
    bool first = true;
    for (const Json &item : json.array()) {
      if (!first)
        out->push_back(',');
      first = false;
      serialize(item, out);
    }
    out->push_back(']');
  } else {
    out->push_back('{');
    bool first = true;
    for (const auto &[key, value] : json.object()) {
      if (!first)
        out->push_back(',');
      first = false;
      append_escaped(key, out);
      out->push_back(':');
      serialize(value, out);
    }
    out->push_back('}');
  }
}

std::string serialize(const Json &json) {
  std::string result;
  serialize(json, &result);
  return result;
}

const Json *member(const Object &object, std::string_view name) {
  const auto found = object.find(name);
  return found == object.end() ? nullptr : &found->second;
}

Json *member(Object &object, std::string_view name) {
  const auto found = object.find(name);
  return found == object.end() ? nullptr : &found->second;
}

bool exact_keys(const Object &object,
                std::initializer_list<std::string_view> names) {
  if (object.size() != names.size())
    return false;
  for (const std::string_view name : names)
    if (object.find(name) == object.end())
      return false;
  return true;
}

bool valid_uuid(std::string_view id) {
  if (id.size() != 36U)
    return false;
  bool nonzero = false;
  for (size_t i = 0U; i < id.size(); ++i) {
    if (i == 8U || i == 13U || i == 18U || i == 23U) {
      if (id[i] != '-')
        return false;
      continue;
    }
    if (!((id[i] >= '0' && id[i] <= '9') ||
          (id[i] >= 'a' && id[i] <= 'f')))
      return false;
    nonzero = nonzero || id[i] != '0';
  }
  return nonzero && id[14] >= '1' && id[14] <= '8' &&
         (id[19] == '8' || id[19] == '9' || id[19] == 'a' ||
          id[19] == 'b');
}

bool valid_utf8(std::string_view bytes) {
  size_t index = 0U;
  while (index < bytes.size()) {
    const uint8_t first = static_cast<uint8_t>(bytes[index]);
    if (first <= 0x7fU) {
      ++index;
      continue;
    }
    size_t count = 0U;
    uint32_t codepoint = 0U;
    uint32_t minimum = 0U;
    if (first >= 0xc2U && first <= 0xdfU) {
      count = 1U; codepoint = first & 0x1fU; minimum = 0x80U;
    } else if (first >= 0xe0U && first <= 0xefU) {
      count = 2U; codepoint = first & 0x0fU; minimum = 0x800U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
      count = 3U; codepoint = first & 0x07U; minimum = 0x10000U;
    } else {
      return false;
    }
    if (count > bytes.size() - index - 1U)
      return false;
    for (size_t offset = 1U; offset <= count; ++offset) {
      const uint8_t continuation = static_cast<uint8_t>(bytes[index + offset]);
      if ((continuation & 0xc0U) != 0x80U)
        return false;
      codepoint = (codepoint << 6U) | (continuation & 0x3fU);
    }
    if (codepoint < minimum || codepoint > 0x10ffffU ||
        (codepoint >= 0xd800U && codepoint <= 0xdfffU))
      return false;
    index += count + 1U;
  }
  return true;
}

bool parse_uint64(std::string_view text, uint64_t *value) {
  if (text.empty() || (text.size() > 1U && text.front() == '0') ||
      text.size() > 20U)
    return false;
  uint64_t parsed = 0U;
  for (const char c : text) {
    if (c < '0' || c > '9')
      return false;
    const uint64_t digit = static_cast<uint64_t>(c - '0');
    if (parsed > (std::numeric_limits<uint64_t>::max() - digit) / 10U)
      return false;
    parsed = parsed * 10U + digit;
  }
  *value = parsed;
  return true;
}

std::string deterministic_uuid(std::string_view command_id,
                               std::string_view role, size_t index = 0U) {
  std::string seed(command_id);
  seed.push_back('|');
  seed.append(role);
  seed.push_back('|');
  seed.append(std::to_string(index));
  auto digest = command::sha256(seed);
  digest[6] = static_cast<uint8_t>((digest[6] & 0x0fU) | 0x50U);
  digest[8] = static_cast<uint8_t>((digest[8] & 0x3fU) | 0x80U);
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(36U);
  for (size_t i = 0U; i < 16U; ++i) {
    if (i == 4U || i == 6U || i == 8U || i == 10U)
      result.push_back('-');
    result.push_back(hex[digest[i] >> 4U]);
    result.push_back(hex[digest[i] & 0x0fU]);
  }
  return result;
}

using P3 = std::array<double, 3U>;
using P2 = std::array<double, 2U>;

bool vec3(const Json &json, P3 *out, double bound) {
  if (!json.is_array() || json.array().size() != 3U)
    return false;
  for (size_t i = 0U; i < 3U; ++i) {
    if (!json.array()[i].is_number() ||
        !std::isfinite(json.array()[i].number()) ||
        std::abs(json.array()[i].number()) > bound)
      return false;
    (*out)[i] = json.array()[i].number();
  }
  return true;
}

bool vec2(const Json &json, P2 *out, double bound) {
  if (!json.is_array() || json.array().size() != 2U)
    return false;
  for (size_t i = 0U; i < 2U; ++i) {
    if (!json.array()[i].is_number() ||
        !std::isfinite(json.array()[i].number()) ||
        std::abs(json.array()[i].number()) > bound)
      return false;
    (*out)[i] = json.array()[i].number();
  }
  return true;
}

Json json_vec3(const P3 &p) {
  return Json(Array{Json(p[0]), Json(p[1]), Json(p[2])});
}

double squared_distance(const P3 &a, const P3 &b) {
  double result = 0.0;
  for (size_t i = 0U; i < 3U; ++i) {
    const double d = a[i] - b[i];
    result += d * d;
  }
  return result;
}

Json provenance(std::string geometry_origin, std::string semantic_origin,
                const std::vector<std::string> &support_ids,
                std::string quality = "provisional") {
  Array supports;
  for (const std::string &id : support_ids)
    supports.emplace_back(id);
  return Json(Object{{"geometry_origin", Json(std::move(geometry_origin))},
                     {"quality_state", Json(std::move(quality))},
                     {"semantic_origin", Json(std::move(semantic_origin))},
                     {"support_ids", Json(std::move(supports))},
                     {"uncertainty_m", Json(nullptr)}});
}

struct CommandData {
  std::string command_id;
  std::string project_id;
  std::string expected_revision_text;
  uint64_t expected_revision{0U};
  std::string event_sequence_text;
  uint64_t event_sequence{0U};
  std::vector<std::string> targets;
  std::string type;
  const Object *payload{nullptr};
};

bool string_member(const Object &object, std::string_view key,
                   std::string *out) {
  const Json *value = member(object, key);
  if (value == nullptr || !value->is_string())
    return false;
  *out = value->string();
  return true;
}

bool uuid_array(const Json &json, std::vector<std::string> *out,
                size_t minimum, size_t maximum) {
  if (!json.is_array() || json.array().size() < minimum ||
      json.array().size() > maximum)
    return false;
  std::unordered_set<std::string> seen;
  for (const Json &item : json.array()) {
    if (!item.is_string() || !valid_uuid(item.string()) ||
        !seen.insert(item.string()).second)
      return false;
    out->push_back(item.string());
  }
  return true;
}

bool valid_string_array(const Json &json) {
  if (!json.is_array())
    return false;
  for (const Json &item : json.array())
    if (!item.is_string() || !valid_uuid(item.string()))
      return false;
  return true;
}

bool command_base(const Json &json, CommandData *out) {
  if (!json.is_object())
    return false;
  const Object &object = json.object();
  if (!exact_keys(object, {"schema_version", "command_id", "project_id",
                           "expected_scene_revision", "event_sequence",
                           "targets", "user_intent", "type", "payload"}))
    return false;
  std::string schema;
  std::string intent;
  if (!string_member(object, "schema_version", &schema) || schema != "1.1" ||
      !string_member(object, "command_id", &out->command_id) ||
      !valid_uuid(out->command_id) ||
      !string_member(object, "project_id", &out->project_id) ||
      !valid_uuid(out->project_id) ||
      !string_member(object, "expected_scene_revision",
                     &out->expected_revision_text) ||
      !parse_uint64(out->expected_revision_text, &out->expected_revision) ||
      !string_member(object, "event_sequence", &out->event_sequence_text) ||
      !parse_uint64(out->event_sequence_text, &out->event_sequence) ||
      !string_member(object, "user_intent", &intent) || intent.empty() ||
      !string_member(object, "type", &out->type))
    return false;
  const Json *targets = member(object, "targets");
  const Json *payload = member(object, "payload");
  if (targets == nullptr || payload == nullptr || !payload->is_object() ||
      !uuid_array(*targets, &out->targets, 0U, 64U))
    return false;
  out->payload = &payload->object();
  return true;
}

bool one_of(std::string_view value,
            std::initializer_list<std::string_view> choices) {
  return std::find(choices.begin(), choices.end(), value) != choices.end();
}

bool validate_payload(const CommandData &command, const Options &options,
                      DynamicExclusion *exclusion) {
  const Object &p = *command.payload;
  const auto exact_targets = [&](size_t minimum, size_t maximum) {
    return command.targets.size() >= minimum && command.targets.size() <= maximum;
  };
  if (command.type == "mark_corner") {
    if (!exact_targets(0U, 64U) ||
        !exact_keys(p, {"point_m", "support_ids", "pending_ray"}))
      return false;
    const Json *point = member(p, "point_m");
    const Json *supports = member(p, "support_ids");
    const Json *ray = member(p, "pending_ray");
    if (point == nullptr || supports == nullptr || ray == nullptr ||
        !valid_string_array(*supports))
      return false;
    P3 ignored{};
    const bool has_point = !point->is_null() && vec3(*point, &ignored, options.maximum_coordinate_m);
    bool has_ray = false;
    if (!ray->is_null()) {
      if (!ray->is_object() ||
          !exact_keys(ray->object(), {"origin_m", "direction_unit"}))
        return false;
      const Json *origin = member(ray->object(), "origin_m");
      const Json *direction = member(ray->object(), "direction_unit");
      P3 o{}, d{};
      if (origin == nullptr || direction == nullptr ||
          !vec3(*origin, &o, options.maximum_coordinate_m) ||
          !vec3(*direction, &d, 1.0))
        return false;
      const double norm = std::sqrt(squared_distance(d, P3{}));
      has_ray = std::abs(norm - 1.0) <= 1e-6;
    }
    return has_point != has_ray && (has_point || has_ray) &&
           (!has_point || !supports->array().empty());
  }
  if (command.type == "split_edge") {
    const Json *value = member(p, "edge_parameter");
    return exact_targets(1U, 1U) && exact_keys(p, {"edge_parameter"}) &&
           value != nullptr && value->is_number() && value->number() > 0.0 &&
           value->number() < 1.0;
  }
  if (command.type == "merge_edges") {
    const Json *value = member(p, "preserve_source_lineage");
    return exact_targets(2U, 64U) &&
           exact_keys(p, {"preserve_source_lineage"}) && value != nullptr &&
           value->is_bool() && value->boolean();
  }
  if (command.type == "mark_protrusion" || command.type == "mark_recess") {
    if (!exact_targets(1U, 1U) ||
        !exact_keys(p, {"parent_edge_id", "ordered_points_m", "support_ids",
                        "preserve_topology"}))
      return false;
    std::string parent;
    if (!string_member(p, "parent_edge_id", &parent) || !valid_uuid(parent) ||
        parent != command.targets.front())
      return false;
    const Json *points = member(p, "ordered_points_m");
    const Json *supports = member(p, "support_ids");
    const Json *preserve = member(p, "preserve_topology");
    if (points == nullptr || !points->is_array() || points->array().size() < 4U ||
        points->array().size() > 64U || supports == nullptr ||
        !valid_string_array(*supports) || preserve == nullptr ||
        !preserve->is_bool() || !preserve->boolean())
      return false;
    for (const Json &point : points->array()) {
      P3 ignored{};
      if (!vec3(point, &ignored, options.maximum_coordinate_m))
        return false;
    }
    return true;
  }
  if (command.type == "mark_obstruction") {
    if (!exact_targets(0U, 64U) ||
        !exact_keys(p, {"footprint_m", "attachment", "fixed"}))
      return false;
    const Json *footprint = member(p, "footprint_m");
    const Json *fixed = member(p, "fixed");
    std::string attachment;
    if (footprint == nullptr || !footprint->is_array() ||
        footprint->array().size() < 3U || footprint->array().size() > 4096U ||
        !string_member(p, "attachment", &attachment) ||
        !one_of(attachment, {"detached", "attached", "unresolved"}) ||
        fixed == nullptr || !fixed->is_bool())
      return false;
    for (const Json &point : footprint->array()) {
      P2 ignored{};
      if (!vec2(point, &ignored, options.maximum_coordinate_m))
        return false;
    }
    return true;
  }
  if (command.type == "classify_object") {
    if (!exact_targets(1U, 1U) ||
        !exact_keys(p, {"label", "semantic_origin", "confidence"}))
      return false;
    std::string label, origin;
    const Json *confidence = member(p, "confidence");
    return string_member(p, "label", &label) && !label.empty() &&
           string_member(p, "semantic_origin", &origin) &&
           one_of(origin, {"user", "external_proposal", "local_model"}) &&
           confidence != nullptr &&
           (confidence->is_null() ||
            (confidence->is_number() && confidence->number() >= 0.0 &&
             confidence->number() <= 1.0));
  }
  if (command.type == "set_dimension") {
    if (!exact_targets(0U, 64U) ||
        !exact_keys(p, {"endpoint_a", "endpoint_b", "value", "unit", "source",
                        "uncertainty_m", "mode", "weight"}))
      return false;
    std::string a, b, unit, source, mode;
    const Json *value = member(p, "value");
    const Json *uncertainty = member(p, "uncertainty_m");
    const Json *weight = member(p, "weight");
    return string_member(p, "endpoint_a", &a) && valid_uuid(a) &&
           string_member(p, "endpoint_b", &b) && valid_uuid(b) && a != b &&
           value != nullptr && value->is_number() && value->number() > 0.0 &&
           string_member(p, "unit", &unit) && one_of(unit, {"m", "cm", "mm"}) &&
           string_member(p, "source", &source) &&
           one_of(source, {"tape", "laser", "reference_survey", "guess"}) &&
           uncertainty != nullptr &&
           (uncertainty->is_null() ||
            (uncertainty->is_number() && uncertainty->number() >= 0.0)) &&
           string_member(p, "mode", &mode) &&
           one_of(mode, {"exact_model", "soft_measurement"}) &&
           weight != nullptr && weight->is_number() && weight->number() > 0.0;
  }
  if (command.type == "protect_feature") {
    const Json *preserve = member(p, "preserve_topology");
    const Json *lock = member(p, "lock_position");
    return exact_targets(1U, 1U) &&
           exact_keys(p, {"preserve_topology", "lock_position"}) &&
           preserve != nullptr && preserve->is_bool() && preserve->boolean() &&
           lock != nullptr && lock->is_bool() && !lock->boolean();
  }
  if (command.type == "lock_position") {
    const Json *position = member(p, "position_m");
    const Json *confirmation = member(p, "explicit_confirmation");
    P3 ignored{};
    return exact_targets(1U, 1U) &&
           exact_keys(p, {"position_m", "explicit_confirmation"}) &&
           position != nullptr && vec3(*position, &ignored, options.maximum_coordinate_m) &&
           confirmation != nullptr && confirmation->is_bool() &&
           confirmation->boolean();
  }
  if (command.type == "unlock_position") {
    std::string id;
    return exact_targets(1U, 1U) && exact_keys(p, {"constraint_id"}) &&
           string_member(p, "constraint_id", &id) && valid_uuid(id);
  }
  if (command.type == "mark_unknown") {
    std::string reason;
    return exact_targets(0U, 64U) && exact_keys(p, {"reason"}) &&
           string_member(p, "reason", &reason) && !reason.empty();
  }
  if (command.type == "exclude_dynamic_region") {
    if (!exact_targets(0U, 64U) ||
        !exact_keys(p, {"min_m", "max_m", "start_sequence", "end_sequence",
                        "reason"}))
      return false;
    const Json *min_value = member(p, "min_m");
    const Json *max_value = member(p, "max_m");
    std::string start, end, reason;
    if (min_value == nullptr || max_value == nullptr || exclusion == nullptr ||
        !vec3(*min_value, &exclusion->min_m, options.maximum_coordinate_m) ||
        !vec3(*max_value, &exclusion->max_m, options.maximum_coordinate_m) ||
        !string_member(p, "start_sequence", &start) ||
        !parse_uint64(start, &exclusion->start_sequence) ||
        !string_member(p, "end_sequence", &end) ||
        !parse_uint64(end, &exclusion->end_sequence) ||
        !string_member(p, "reason", &reason) || reason.empty() ||
        exclusion->start_sequence > exclusion->end_sequence)
      return false;
    for (size_t i = 0U; i < 3U; ++i)
      if (exclusion->min_m[i] > exclusion->max_m[i])
        return false;
    exclusion->command_id = command.command_id;
    exclusion->project_id = command.project_id;
    return true;
  }
  if (command.type == "undo" || command.type == "redo") {
    std::string original;
    return exact_targets(0U, 0U) && exact_keys(p, {"original_command_id"}) &&
           string_member(p, "original_command_id", &original) &&
           valid_uuid(original) && original != command.command_id;
  }
  return false;
}

bool provenance_shape(const Json &json) {
  if (!json.is_object() ||
      !exact_keys(json.object(), {"geometry_origin", "semantic_origin",
                                  "support_ids", "uncertainty_m",
                                  "quality_state"}))
    return false;
  std::string geometry, semantic, quality;
  const Json *supports = member(json.object(), "support_ids");
  const Json *uncertainty = member(json.object(), "uncertainty_m");
  return string_member(json.object(), "geometry_origin", &geometry) &&
         one_of(geometry, {"observed", "fitted", "user_measured", "inferred",
                           "unknown"}) &&
         string_member(json.object(), "semantic_origin", &semantic) &&
         one_of(semantic,
                {"platform", "local_model", "user", "imported", "unknown"}) &&
         supports != nullptr && valid_string_array(*supports) &&
         uncertainty != nullptr &&
         (uncertainty->is_null() ||
          (uncertainty->is_number() && uncertainty->number() >= 0.0)) &&
         string_member(json.object(), "quality_state", &quality) &&
         one_of(quality, {"provisional", "needs_review", "ready", "invalid"});
}

struct SceneView {
  Json root;
  Object *object{nullptr};
  uint64_t revision{0U};
  std::string project_id;
  std::unordered_map<std::string, size_t> node_index;
  std::unordered_map<std::string, size_t> edge_index;
  std::unordered_map<std::string, size_t> feature_index;
  std::unordered_map<std::string, size_t> object_index;
  std::unordered_map<std::string, size_t> constraint_index;
};

bool unique_entity_ids(const Array &array,
                       std::unordered_map<std::string, size_t> *index) {
  index->clear();
  index->reserve(array.size());
  for (size_t i = 0U; i < array.size(); ++i) {
    if (!array[i].is_object())
      return false;
    std::string id;
    if (!string_member(array[i].object(), "id", &id) || !valid_uuid(id) ||
        !index->emplace(id, i).second)
      return false;
  }
  return true;
}

P2 project_point(const P3 &point, const P3 &origin, const P3 &u,
                 const P3 &v) {
  P2 result{};
  for (size_t i = 0U; i < 3U; ++i) {
    result[0] += (point[i] - origin[i]) * u[i];
    result[1] += (point[i] - origin[i]) * v[i];
  }
  return result;
}

double cross2(const P2 &a, const P2 &b, const P2 &c) {
  return (b[0] - a[0]) * (c[1] - a[1]) -
         (b[1] - a[1]) * (c[0] - a[0]);
}

bool on_segment(const P2 &a, const P2 &b, const P2 &p, double epsilon) {
  return std::abs(cross2(a, b, p)) <= epsilon &&
         p[0] >= std::min(a[0], b[0]) - epsilon &&
         p[0] <= std::max(a[0], b[0]) + epsilon &&
         p[1] >= std::min(a[1], b[1]) - epsilon &&
         p[1] <= std::max(a[1], b[1]) + epsilon;
}

int orientation(double value, double epsilon) {
  if (value > epsilon)
    return 1;
  if (value < -epsilon)
    return -1;
  return 0;
}

bool segments_intersect(const P2 &a, const P2 &b, const P2 &c, const P2 &d,
                        double epsilon) {
  const int o1 = orientation(cross2(a, b, c), epsilon);
  const int o2 = orientation(cross2(a, b, d), epsilon);
  const int o3 = orientation(cross2(c, d, a), epsilon);
  const int o4 = orientation(cross2(c, d, b), epsilon);
  // Proper crossings require strictly opposite sides. A tolerance-zero
  // orientation may be collinear only with the infinite supporting line;
  // finite endpoint contact is checked by on_segment below.
  if (o1 * o2 < 0 && o3 * o4 < 0)
    return true;
  return (o1 == 0 && on_segment(a, b, c, epsilon)) ||
         (o2 == 0 && on_segment(a, b, d, epsilon)) ||
         (o3 == 0 && on_segment(c, d, a, epsilon)) ||
         (o4 == 0 && on_segment(c, d, b, epsilon));
}

double signed_area(const std::vector<P2> &ring) {
  if (ring.size() < 3U)
    return 0.0;
  const P2 origin = ring.front();
  double sum = 0.0;
  double correction = 0.0;
  for (size_t i = 0U; i < ring.size(); ++i) {
    const P2 &a = ring[i];
    const P2 &b = ring[(i + 1U) % ring.size()];
    const double term = (a[0] - origin[0]) * (b[1] - origin[1]) -
                        (b[0] - origin[0]) * (a[1] - origin[1]);
    const double y = term - correction;
    const double t = sum + y;
    correction = (t - sum) - y;
    sum = t;
  }
  return 0.5 * sum;
}

bool simple_ring(const std::vector<P2> &ring, double epsilon) {
  if (ring.size() < 3U)
    return false;
  for (size_t i = 0U; i < ring.size(); ++i) {
    const size_t next_i = (i + 1U) % ring.size();
    const double dx = ring[i][0] - ring[next_i][0];
    const double dy = ring[i][1] - ring[next_i][1];
    if (dx * dx + dy * dy <= epsilon * epsilon)
      return false;
    for (size_t j = i + 1U; j < ring.size(); ++j) {
      const size_t next_j = (j + 1U) % ring.size();
      if (i == j || next_i == j || next_j == i)
        continue;
      if (segments_intersect(ring[i], ring[next_i], ring[j], ring[next_j],
                             epsilon))
        return false;
    }
  }
  return std::abs(signed_area(ring)) > epsilon * epsilon;
}

bool point_in_ring(const P2 &point, const std::vector<P2> &ring,
                   double epsilon) {
  bool inside = false;
  for (size_t i = 0U, j = ring.size() - 1U; i < ring.size(); j = i++) {
    if (on_segment(ring[j], ring[i], point, epsilon))
      return false;
    const bool crosses = ((ring[i][1] > point[1]) != (ring[j][1] > point[1])) &&
                         (point[0] <
                          (ring[j][0] - ring[i][0]) *
                                  (point[1] - ring[i][1]) /
                                  (ring[j][1] - ring[i][1]) +
                              ring[i][0]);
    if (crosses)
      inside = !inside;
  }
  return inside;
}

bool basis_vectors(const Object &scene, P3 *origin, P3 *u, P3 *v, P3 *n,
                   const Options &options) {
  const Json *basis = member(scene, "basis");
  if (basis == nullptr || !basis->is_object() ||
      !exact_keys(basis->object(), {"handedness", "up", "floor_origin_m",
                                    "floor_u", "floor_v", "floor_n"}))
    return false;
  std::string handedness, up;
  const Json *origin_json = member(basis->object(), "floor_origin_m");
  const Json *u_json = member(basis->object(), "floor_u");
  const Json *v_json = member(basis->object(), "floor_v");
  const Json *n_json = member(basis->object(), "floor_n");
  if (!string_member(basis->object(), "handedness", &handedness) ||
      handedness != "right" || !string_member(basis->object(), "up", &up) ||
      up != "+Y" || origin_json == nullptr || u_json == nullptr ||
      v_json == nullptr || n_json == nullptr ||
      !vec3(*origin_json, origin, options.maximum_coordinate_m) ||
      !vec3(*u_json, u, 1.0) || !vec3(*v_json, v, 1.0) ||
      !vec3(*n_json, n, 1.0))
    return false;
  const auto dot = [](const P3 &a, const P3 &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };
  const P3 cross{(*u)[1] * (*v)[2] - (*u)[2] * (*v)[1],
                 (*u)[2] * (*v)[0] - (*u)[0] * (*v)[2],
                 (*u)[0] * (*v)[1] - (*u)[1] * (*v)[0]};
  return std::abs(dot(*u, *u) - 1.0) <= 1e-6 &&
         std::abs(dot(*v, *v) - 1.0) <= 1e-6 &&
         std::abs(dot(*n, *n) - 1.0) <= 1e-6 &&
         std::abs(dot(*u, *v)) <= 1e-6 &&
         std::abs(dot(*u, *n)) <= 1e-6 &&
         std::abs(dot(*v, *n)) <= 1e-6 && squared_distance(cross, *n) <= 1e-12;
}

bool scene_array(Object &scene, std::string_view name, Array **array) {
  Json *value = member(scene, name);
  if (value == nullptr || !value->is_array())
    return false;
  *array = &value->array();
  return true;
}

bool validate_scene(SceneView *scene, const Options &options,
                    std::string *diagnostic) {
  const auto reject = [&](std::string reason) {
    *diagnostic = std::move(reason);
    return false;
  };
  if (!scene->root.is_object())
    return reject("scene root is not an object");
  scene->object = &scene->root.object();
  Object &root = *scene->object;
  if (!exact_keys(root, {"schema_version", "project_id", "scene_revision",
                         "source_is_synthetic", "units", "basis", "epochs",
                         "nodes", "edges", "surfaces", "features", "regions",
                         "objects", "constraints", "issues", "quality_state"}))
    return reject("scene root does not match schema fields");
  std::string schema, revision, units, quality;
  if (!string_member(root, "schema_version", &schema) || schema != "1.1" ||
      !string_member(root, "project_id", &scene->project_id) ||
      !valid_uuid(scene->project_id) ||
      !string_member(root, "scene_revision", &revision) ||
      !parse_uint64(revision, &scene->revision) ||
      !string_member(root, "units", &units) || units != "m" ||
      !string_member(root, "quality_state", &quality) ||
      !one_of(quality, {"provisional", "needs_review", "ready", "invalid"}))
    return reject("scene identity/revision/unit fields are invalid");
  const Json *synthetic = member(root, "source_is_synthetic");
  const Json *epochs = member(root, "epochs");
  const Json *surfaces = member(root, "surfaces");
  if (synthetic == nullptr || !synthetic->is_bool() || epochs == nullptr ||
      !epochs->is_array() || surfaces == nullptr || !surfaces->is_array())
    return reject("scene source/epoch/surface containers are invalid");
  for (const Json &epoch : epochs->array()) {
    if (!epoch.is_object() ||
        !exact_keys(epoch.object(), {"capture_id", "session_epoch",
                                     "transform_revision", "relationship_state",
                                     "T_project_world"}))
      return reject("epoch fields do not match schema");
    std::string capture, transform_revision, relationship;
    uint64_t ignored_revision = 0U;
    const Json *session_epoch = member(epoch.object(), "session_epoch");
    const Json *transform = member(epoch.object(), "T_project_world");
    if (!string_member(epoch.object(), "capture_id", &capture) ||
        !valid_uuid(capture) || session_epoch == nullptr ||
        !session_epoch->is_number() || session_epoch->number() < 0.0 ||
        std::floor(session_epoch->number()) != session_epoch->number() ||
        !string_member(epoch.object(), "transform_revision", &transform_revision) ||
        !parse_uint64(transform_revision, &ignored_revision) ||
        !string_member(epoch.object(), "relationship_state", &relationship) ||
        !one_of(relationship, {"unresolved", "accepted"}) || transform == nullptr)
      return reject("epoch semantic validation failed");
    if (transform->is_null()) {
      if (relationship == "accepted")
        return reject("accepted epoch relationship requires a transform");
    } else {
      if (!transform->is_array() || transform->array().size() != 16U)
        return reject("epoch transform must contain 16 finite values");
      std::array<double, 16U> matrix{};
      for (size_t i = 0U; i < matrix.size(); ++i) {
        if (!transform->array()[i].is_number())
          return reject("epoch transform contains a nonnumber");
        matrix[i] = transform->array()[i].number();
      }
      if (std::abs(matrix[3]) > 1e-8 || std::abs(matrix[7]) > 1e-8 ||
          std::abs(matrix[11]) > 1e-8 || std::abs(matrix[15] - 1.0) > 1e-8)
        return reject("epoch transform has an invalid homogeneous row");
      for (size_t column = 0U; column < 3U; ++column) {
        double norm = 0.0;
        for (size_t row = 0U; row < 3U; ++row)
          norm += matrix[column * 4U + row] * matrix[column * 4U + row];
        if (std::abs(norm - 1.0) > 1e-6)
          return reject("epoch transform rotation is not orthonormal");
      }
    }
  }
  P3 floor_origin{}, floor_u{}, floor_v{}, floor_n{};
  if (!basis_vectors(root, &floor_origin, &floor_u, &floor_v, &floor_n, options))
    return reject("scene basis is not finite right-handed orthonormal");

  Array *nodes = nullptr, *edges = nullptr, *features = nullptr,
        *regions = nullptr, *objects = nullptr, *constraints = nullptr,
        *issues = nullptr;
  if (!scene_array(root, "nodes", &nodes) || !scene_array(root, "edges", &edges) ||
      !scene_array(root, "features", &features) ||
      !scene_array(root, "regions", &regions) ||
      !scene_array(root, "objects", &objects) ||
      !scene_array(root, "constraints", &constraints) ||
      !scene_array(root, "issues", &issues))
    return reject("scene entity containers are invalid");
  const size_t total_entities = nodes->size() + edges->size() + features->size() +
                                regions->size() + objects->size() +
                                constraints->size() + issues->size();
  if (total_entities > options.max_entities)
    return reject("scene entity limit exceeded");
  if (!unique_entity_ids(*nodes, &scene->node_index) ||
      !unique_entity_ids(*edges, &scene->edge_index) ||
      !unique_entity_ids(*features, &scene->feature_index) ||
      !unique_entity_ids(*objects, &scene->object_index) ||
      !unique_entity_ids(*constraints, &scene->constraint_index))
    return reject("scene has invalid or duplicate entity IDs");

  std::unordered_map<std::string, size_t> surface_index, region_index,
      issue_index;
  if (!unique_entity_ids(surfaces->array(), &surface_index) ||
      !unique_entity_ids(*regions, &region_index) ||
      !unique_entity_ids(*issues, &issue_index))
    return reject("scene has invalid or duplicate surface/region/issue IDs");

  std::unordered_set<std::string> all_ids;
  for (const auto *index : {&scene->node_index, &scene->edge_index,
                            &scene->feature_index, &scene->object_index,
                            &scene->constraint_index, &surface_index,
                            &region_index, &issue_index})
    for (const auto &[id, ignored] : *index) {
      (void)ignored;
      if (!all_ids.insert(id).second)
        return reject("scene reuses an ID across entity collections");
    }

  std::unordered_map<std::string, P3> positions;
  positions.reserve(nodes->size());
  for (const Json &node : *nodes) {
    if (!exact_keys(node.object(), {"id", "position_m", "provenance",
                                    "position_locked"}))
      return reject("node fields do not match schema");
    std::string id;
    const Json *position = member(node.object(), "position_m");
    const Json *prov = member(node.object(), "provenance");
    const Json *locked = member(node.object(), "position_locked");
    P3 p{};
    if (!string_member(node.object(), "id", &id) || position == nullptr ||
        !vec3(*position, &p, options.maximum_coordinate_m) || prov == nullptr ||
        !provenance_shape(*prov) || locked == nullptr || !locked->is_bool())
      return reject("node semantic validation failed");
    positions.emplace(id, p);
  }
  for (const Json &edge : *edges) {
    if (!exact_keys(edge.object(), {"id", "start_node_id", "end_node_id",
                                    "surface_ids", "parent_edge_ids",
                                    "protected_topology", "provenance"}))
      return reject("edge fields do not match schema");
    std::string start, end;
    const Json *surface_ids = member(edge.object(), "surface_ids");
    const Json *parents = member(edge.object(), "parent_edge_ids");
    const Json *protected_value = member(edge.object(), "protected_topology");
    const Json *prov = member(edge.object(), "provenance");
    if (!string_member(edge.object(), "start_node_id", &start) ||
        !string_member(edge.object(), "end_node_id", &end) || start == end ||
        !positions.contains(start) || !positions.contains(end) ||
        squared_distance(positions.at(start), positions.at(end)) <=
            options.geometry_epsilon_m * options.geometry_epsilon_m ||
        surface_ids == nullptr || !valid_string_array(*surface_ids) ||
        parents == nullptr || !valid_string_array(*parents) ||
        protected_value == nullptr || !protected_value->is_bool() ||
        prov == nullptr || !provenance_shape(*prov))
      return reject("edge references or geometry are invalid");
    for (const Json &surface_id : surface_ids->array())
      if (!surface_index.contains(surface_id.string()))
        return reject("edge references a missing surface");
  }
  for (const Json &surface : surfaces->array()) {
    if (!exact_keys(surface.object(), {"id", "kind", "normal_unit", "offset_m",
                                       "boundary_node_ids", "mesh_asset",
                                       "fit_residual_m", "provenance"}))
      return reject("surface fields do not match schema");
    std::string kind;
    const Json *normal = member(surface.object(), "normal_unit");
    const Json *offset = member(surface.object(), "offset_m");
    const Json *boundary = member(surface.object(), "boundary_node_ids");
    const Json *mesh_asset = member(surface.object(), "mesh_asset");
    const Json *residual = member(surface.object(), "fit_residual_m");
    const Json *prov = member(surface.object(), "provenance");
    if (!string_member(surface.object(), "kind", &kind) ||
        !one_of(kind, {"plane", "mesh_patch"}) || normal == nullptr ||
        offset == nullptr || boundary == nullptr || !valid_string_array(*boundary) ||
        mesh_asset == nullptr || residual == nullptr ||
        !(residual->is_null() ||
          (residual->is_number() && residual->number() >= 0.0)) ||
        prov == nullptr || !provenance_shape(*prov))
      return reject("surface semantic validation failed");
    for (const Json &node_id : boundary->array())
      if (!scene->node_index.contains(node_id.string()))
        return reject("surface boundary references a missing node");
    if (kind == "plane") {
      P3 parsed_normal{};
      if (!vec3(*normal, &parsed_normal, 1.0) || !offset->is_number() ||
          std::abs(std::sqrt(squared_distance(parsed_normal, P3{})) - 1.0) >
              1e-6 ||
          !(mesh_asset->is_null() ||
            (mesh_asset->is_string() && !mesh_asset->string().empty())))
        return reject("plane surface parameters are inconsistent");
    } else if (!normal->is_null() || !offset->is_null() ||
               !(mesh_asset->is_null() ||
                 (mesh_asset->is_string() && !mesh_asset->string().empty()))) {
      return reject("mesh patch parameters are inconsistent");
    }
  }
  for (const Json &feature : *features) {
    if (!exact_keys(feature.object(), {"id", "kind", "edge_ids", "surface_ids",
                                       "attachment", "protected_topology",
                                       "provenance"}))
      return reject("feature fields do not match schema");
    std::string kind, attachment;
    const Json *edge_ids = member(feature.object(), "edge_ids");
    const Json *surface_ids = member(feature.object(), "surface_ids");
    const Json *protected_value = member(feature.object(), "protected_topology");
    const Json *prov = member(feature.object(), "provenance");
    if (!string_member(feature.object(), "kind", &kind) ||
        !one_of(kind, {"protrusion", "recess", "fixed_obstruction", "opening",
                       "overhead", "ceiling_patch", "unknown"}) ||
        !string_member(feature.object(), "attachment", &attachment) ||
        !one_of(attachment, {"attached", "detached", "unresolved"}) ||
        edge_ids == nullptr || !valid_string_array(*edge_ids) ||
        surface_ids == nullptr || !valid_string_array(*surface_ids) ||
        protected_value == nullptr || !protected_value->is_bool() ||
        prov == nullptr || !provenance_shape(*prov))
      return reject("feature semantic validation failed");
    for (const Json &id : edge_ids->array())
      if (!scene->edge_index.contains(id.string()))
        return reject("feature references a missing edge");
    for (const Json &id : surface_ids->array())
      if (!surface_index.contains(id.string()))
        return reject("feature references a missing surface");
  }

  for (Json &region : *regions) {
    if (!region.is_object() ||
        !exact_keys(region.object(), {"id", "outer_node_ids", "hole_node_ids",
                                      "area_m2", "area_policy", "view",
                                      "cut_height_m", "provenance"}))
      return reject("region fields do not match schema");
    const Json *outer = member(region.object(), "outer_node_ids");
    const Json *holes = member(region.object(), "hole_node_ids");
    const Json *area = member(region.object(), "area_m2");
    const Json *cut = member(region.object(), "cut_height_m");
    const Json *prov = member(region.object(), "provenance");
    std::string policy, view;
    if (outer == nullptr || !outer->is_array() || outer->array().size() < 3U ||
        holes == nullptr || !holes->is_array() || area == nullptr ||
        !(area->is_null() || (area->is_number() && area->number() >= 0.0)) ||
        !string_member(region.object(), "area_policy", &policy) ||
        !one_of(policy, {"internal_finished_face", "net_fixed_obstructions"}) ||
        !string_member(region.object(), "view", &view) ||
        !one_of(view, {"floor_footprint", "architectural_cut"}) ||
        cut == nullptr || !cut->is_number() || cut->number() < 0.0 ||
        prov == nullptr || !provenance_shape(*prov))
      return reject("region semantic validation failed");
    std::vector<P2> outer_ring;
    std::unordered_set<std::string> outer_ids;
    for (const Json &id : outer->array()) {
      if (!id.is_string() || !positions.contains(id.string()) ||
          !outer_ids.insert(id.string()).second)
        return reject("region outer ring has missing/duplicate nodes");
      outer_ring.push_back(project_point(positions.at(id.string()), floor_origin,
                                         floor_u, floor_v));
    }
    if (!simple_ring(outer_ring, options.geometry_epsilon_m) ||
        signed_area(outer_ring) <= 0.0)
      return reject("region outer ring is open, self-intersecting, or clockwise");
    double computed_area = signed_area(outer_ring);
    for (const Json &hole : holes->array()) {
      if (!hole.is_array() || hole.array().size() < 3U)
        return reject("region hole is malformed");
      std::vector<P2> hole_ring;
      std::unordered_set<std::string> hole_ids;
      for (const Json &id : hole.array()) {
        if (!id.is_string() || !positions.contains(id.string()) ||
            !hole_ids.insert(id.string()).second)
          return reject("region hole has missing/duplicate nodes");
        hole_ring.push_back(project_point(positions.at(id.string()), floor_origin,
                                          floor_u, floor_v));
      }
      if (!simple_ring(hole_ring, options.geometry_epsilon_m) ||
          signed_area(hole_ring) >= 0.0 ||
          !point_in_ring(hole_ring.front(), outer_ring,
                         options.geometry_epsilon_m))
        return reject("region hole is invalid, outside, or not clockwise");
      computed_area += signed_area(hole_ring);
    }
    if (area->is_number() && std::abs(area->number() - computed_area) >
                                 std::max(1e-8, std::abs(computed_area) * 1e-8))
      return reject("region area does not match validated rings");
  }

  for (const Json &object : *objects) {
    if (!exact_keys(object.object(), {"id", "label", "surface_ids", "mesh_asset",
                                      "footprint_m", "provenance"}))
      return reject("object fields do not match schema");
    std::string label;
    const Json *surface_ids = member(object.object(), "surface_ids");
    const Json *mesh_asset = member(object.object(), "mesh_asset");
    const Json *footprint = member(object.object(), "footprint_m");
    const Json *prov = member(object.object(), "provenance");
    if (!string_member(object.object(), "label", &label) || label.empty() ||
        surface_ids == nullptr || !valid_string_array(*surface_ids) ||
        mesh_asset == nullptr ||
        !(mesh_asset->is_null() ||
          (mesh_asset->is_string() && !mesh_asset->string().empty())) ||
        footprint == nullptr || prov == nullptr || !provenance_shape(*prov))
      return reject("object semantic validation failed");
    if (!footprint->is_null()) {
      if (!footprint->is_array() || footprint->array().size() < 3U)
        return reject("object footprint is malformed");
      std::vector<P2> ring;
      for (const Json &point : footprint->array()) {
        P2 parsed{};
        if (!vec2(point, &parsed, options.maximum_coordinate_m))
          return reject("object footprint has invalid coordinates");
        ring.push_back(parsed);
      }
      if (!simple_ring(ring, options.geometry_epsilon_m))
        return reject("object footprint self-intersects or degenerates");
    }
    for (const Json &id : surface_ids->array())
      if (!surface_index.contains(id.string()))
        return reject("object references a missing surface");
  }

  for (const Json &constraint : *constraints) {
    if (!exact_keys(constraint.object(), {"id", "kind", "target_ids",
                                          "source_command_id", "value", "unit",
                                          "hard", "weight", "source", "status"}))
      return reject("constraint fields do not match schema");
    std::string kind, source_command, unit, source, status;
    const Json *targets = member(constraint.object(), "target_ids");
    const Json *value = member(constraint.object(), "value");
    const Json *hard = member(constraint.object(), "hard");
    const Json *weight = member(constraint.object(), "weight");
    if (!string_member(constraint.object(), "kind", &kind) ||
        !one_of(kind, {"distance", "position_lock", "orthogonal", "parallel",
                       "topology_protection"}) ||
        targets == nullptr || !valid_string_array(*targets) ||
        !string_member(constraint.object(), "source_command_id", &source_command) ||
        !valid_uuid(source_command) || value == nullptr ||
        !(value->is_null() || value->is_number() ||
          (value->is_array() && value->array().size() == 3U)) ||
        !string_member(constraint.object(), "unit", &unit) ||
        !one_of(unit, {"m", "rad", "none"}) || hard == nullptr ||
        !hard->is_bool() || weight == nullptr || !weight->is_number() ||
        weight->number() < 0.0 ||
        !string_member(constraint.object(), "source", &source) ||
        !one_of(source, {"sensor_fit", "tape", "laser", "reference_survey",
                         "guess", "user_intent"}) ||
        !string_member(constraint.object(), "status", &status) ||
        !one_of(status, {"active", "conflict", "superseded", "removed"}))
      return reject("constraint semantic validation failed");
    if (value->is_array()) {
      P3 parsed{};
      if (!vec3(*value, &parsed, options.maximum_coordinate_m))
        return reject("constraint vector value is invalid");
    }
    std::vector<std::string> target_ids;
    for (const Json &target : targets->array())
      target_ids.push_back(target.string());
    if (kind == "distance" &&
        (target_ids.size() != 2U || target_ids[0] == target_ids[1] ||
         !scene->node_index.contains(target_ids[0]) ||
         !scene->node_index.contains(target_ids[1]) || !value->is_number() ||
         value->number() <= 0.0 || unit != "m"))
      return reject("distance constraint targets/value are invalid");
    if (kind == "position_lock" &&
        (target_ids.size() != 1U ||
         !scene->node_index.contains(target_ids[0]) || !value->is_array() ||
         unit != "m" || !hard->boolean()))
      return reject("position lock constraint is invalid");
    if (kind == "topology_protection" &&
        (target_ids.size() != 1U || !all_ids.contains(target_ids[0]) ||
         !value->is_null() || unit != "none" || !hard->boolean()))
      return reject("topology protection constraint is invalid");
  }
  for (const Json &issue : *issues) {
    if (!exact_keys(issue.object(), {"id", "code", "target_ids",
                                     "scene_revision", "severity", "reason",
                                     "action", "state", "source_sequence"}))
      return reject("issue fields do not match schema");
    std::string code, issue_revision, severity, reason, action, state,
        source_sequence;
    uint64_t parsed_revision = 0U, parsed_sequence = 0U;
    const Json *targets = member(issue.object(), "target_ids");
    if (!string_member(issue.object(), "code", &code) || code.empty() ||
        targets == nullptr || !valid_string_array(*targets) ||
        !string_member(issue.object(), "scene_revision", &issue_revision) ||
        !parse_uint64(issue_revision, &parsed_revision) ||
        parsed_revision > scene->revision ||
        !string_member(issue.object(), "severity", &severity) ||
        !one_of(severity, {"info", "warning", "critical"}) ||
        !string_member(issue.object(), "reason", &reason) || reason.empty() ||
        !string_member(issue.object(), "action", &action) || action.empty() ||
        !string_member(issue.object(), "state", &state) ||
        !one_of(state, {"active", "suppressed", "resolved"}) ||
        !string_member(issue.object(), "source_sequence", &source_sequence) ||
        !parse_uint64(source_sequence, &parsed_sequence))
      return reject("issue semantic validation failed");
    for (const Json &target : targets->array())
      if (!all_ids.contains(target.string()))
        return reject("issue references a noncurrent target");
  }
  return true;
}

Array &array_member(Object &object, std::string_view key) {
  Json *value = member(object, key);
  if (value == nullptr || !value->is_array())
    throw std::invalid_argument("missing array member");
  return value->array();
}

Object *find_entity(Array &array, std::string_view id) {
  for (Json &entity : array) {
    if (!entity.is_object())
      continue;
    std::string current;
    if (string_member(entity.object(), "id", &current) && current == id)
      return &entity.object();
  }
  return nullptr;
}

const Object *find_entity(const Array &array, std::string_view id) {
  for (const Json &entity : array) {
    if (!entity.is_object())
      continue;
    std::string current;
    if (string_member(entity.object(), "id", &current) && current == id)
      return &entity.object();
  }
  return nullptr;
}

bool erase_entity(Array &array, std::string_view id) {
  const auto found = std::find_if(array.begin(), array.end(), [&](const Json &entity) {
    std::string current;
    return entity.is_object() && string_member(entity.object(), "id", &current) &&
           current == id;
  });
  if (found == array.end())
    return false;
  array.erase(found);
  return true;
}

P3 node_position(const Object &node, const Options &options) {
  P3 result{};
  const Json *position = member(node, "position_m");
  if (position == nullptr || !vec3(*position, &result, options.maximum_coordinate_m))
    throw std::invalid_argument("invalid node position");
  return result;
}

Json make_node(std::string id, const P3 &position,
               const std::vector<std::string> &supports,
               std::string geometry_origin = "fitted") {
  return Json(Object{{"id", Json(std::move(id))},
                     {"position_locked", Json(false)},
                     {"position_m", json_vec3(position)},
                     {"provenance",
                      provenance(std::move(geometry_origin), "user", supports)}});
}

Json make_edge(std::string id, std::string start, std::string end,
               std::vector<std::string> parents,
               const std::vector<std::string> &surfaces,
               const std::vector<std::string> &supports, bool protected_topology) {
  Array parent_json, surface_json;
  for (std::string &parent : parents)
    parent_json.emplace_back(std::move(parent));
  for (const std::string &surface : surfaces)
    surface_json.emplace_back(surface);
  return Json(Object{{"end_node_id", Json(std::move(end))},
                     {"id", Json(std::move(id))},
                     {"parent_edge_ids", Json(std::move(parent_json))},
                     {"protected_topology", Json(protected_topology)},
                     {"provenance", provenance("fitted", "user", supports)},
                     {"start_node_id", Json(std::move(start))},
                     {"surface_ids", Json(std::move(surface_json))}});
}

std::vector<std::string> json_ids(const Json &json) {
  std::vector<std::string> result;
  if (!json.is_array())
    return result;
  for (const Json &item : json.array())
    if (item.is_string())
      result.push_back(item.string());
  return result;
}

enum class SolveState { kApplied, kConflict, kRejected };
struct SolveResult {
  SolveState state{SolveState::kRejected};
  std::vector<std::string> conflict_ids;
  std::string diagnostic;
};

struct Equation {
  std::string id;
  std::string kind;
  size_t first{0U};
  size_t second{0U};
  P3 locked{};
  double value{0.0};
  double weight{1.0};
  bool hard{true};
};

bool gaussian_solve(std::vector<double> matrix, std::vector<double> rhs,
                    size_t dimension, std::vector<double> *solution) {
  solution->assign(dimension, 0.0);
  for (size_t column = 0U; column < dimension; ++column) {
    size_t pivot = column;
    double pivot_value = std::abs(matrix[column * dimension + column]);
    for (size_t row = column + 1U; row < dimension; ++row) {
      const double candidate = std::abs(matrix[row * dimension + column]);
      if (candidate > pivot_value) {
        pivot_value = candidate;
        pivot = row;
      }
    }
    if (!std::isfinite(pivot_value) || pivot_value < 1e-12)
      return false;
    if (pivot != column) {
      for (size_t c = column; c < dimension; ++c)
        std::swap(matrix[column * dimension + c],
                  matrix[pivot * dimension + c]);
      std::swap(rhs[column], rhs[pivot]);
    }
    const double diagonal = matrix[column * dimension + column];
    for (size_t row = column + 1U; row < dimension; ++row) {
      const double factor = matrix[row * dimension + column] / diagonal;
      if (factor == 0.0)
        continue;
      matrix[row * dimension + column] = 0.0;
      for (size_t c = column + 1U; c < dimension; ++c)
        matrix[row * dimension + c] -=
            factor * matrix[column * dimension + c];
      rhs[row] -= factor * rhs[column];
    }
  }
  for (size_t reverse = dimension; reverse-- > 0U;) {
    double value = rhs[reverse];
    for (size_t c = reverse + 1U; c < dimension; ++c)
      value -= matrix[reverse * dimension + c] * (*solution)[c];
    const double diagonal = matrix[reverse * dimension + reverse];
    if (!std::isfinite(value) || std::abs(diagonal) < 1e-12)
      return false;
    (*solution)[reverse] = value / diagonal;
    if (!std::isfinite((*solution)[reverse]))
      return false;
  }
  return true;
}

bool collect_equations(Object &scene, const Options &options,
                       std::vector<Object *> *nodes,
                       std::unordered_map<std::string, size_t> *node_indices,
                       std::vector<Equation> *equations,
                       std::string *diagnostic) {
  Array &node_array = array_member(scene, "nodes");
  if (node_array.size() * 3U > options.max_solver_scalars) {
    *diagnostic = "local solver scalar bound exceeded";
    return false;
  }
  for (size_t i = 0U; i < node_array.size(); ++i) {
    Object &node = node_array[i].object();
    std::string id;
    if (!string_member(node, "id", &id))
      return false;
    node_indices->emplace(id, i);
    nodes->push_back(&node);
  }
  const Array &constraints = array_member(scene, "constraints");
  std::map<std::tuple<std::string, size_t, size_t>, double> seen_distances;
  std::map<std::pair<size_t, size_t>, double> seen_locks;
  for (const Json &constraint_json : constraints) {
    const Object &constraint = constraint_json.object();
    std::string id, kind, status;
    if (!string_member(constraint, "id", &id) ||
        !string_member(constraint, "kind", &kind) ||
        !string_member(constraint, "status", &status) || status != "active")
      continue;
    const Json *hard_json = member(constraint, "hard");
    const Json *weight_json = member(constraint, "weight");
    const Json *targets_json = member(constraint, "target_ids");
    const Json *value_json = member(constraint, "value");
    if (hard_json == nullptr || weight_json == nullptr || targets_json == nullptr ||
        value_json == nullptr || !hard_json->is_bool() || !weight_json->is_number() ||
        !targets_json->is_array())
      return false;
    if (kind != "distance" && kind != "position_lock") {
      if (hard_json->boolean()) {
        *diagnostic = "active hard constraint kind is unsupported by local solver";
        return false;
      }
      continue;
    }
    Equation equation;
    equation.id = id;
    equation.kind = kind;
    equation.hard = hard_json->boolean();
    equation.weight = weight_json->number();
    if (kind == "distance") {
      if (targets_json->array().size() != 2U || !value_json->is_number() ||
          value_json->number() <= 0.0)
        return false;
      const std::string &a = targets_json->array()[0].string();
      const std::string &b = targets_json->array()[1].string();
      if (!node_indices->contains(a) || !node_indices->contains(b) || a == b)
        return false;
      equation.first = node_indices->at(a);
      equation.second = node_indices->at(b);
      equation.value = value_json->number();
      if (equation.first > equation.second)
        std::swap(equation.first, equation.second);
      if (equation.hard) {
        const auto key = std::make_tuple(kind, equation.first, equation.second);
        const auto existing = seen_distances.find(key);
        if (existing != seen_distances.end()) {
          if (std::abs(existing->second - equation.value) >
              options.feasibility_tolerance) {
            *diagnostic = "contradictory hard distances share endpoints";
            return false;
          }
          continue;
        }
        seen_distances.emplace(key, equation.value);
      }
    } else {
      if (targets_json->array().size() != 1U || value_json->is_null() ||
          !value_json->is_array())
        return false;
      const std::string &target = targets_json->array()[0].string();
      if (!node_indices->contains(target) ||
          !vec3(*value_json, &equation.locked, options.maximum_coordinate_m))
        return false;
      equation.first = node_indices->at(target);
      for (size_t axis = 0U; axis < 3U; ++axis) {
        const auto key = std::make_pair(equation.first, axis);
        const auto existing = seen_locks.find(key);
        if (existing != seen_locks.end() &&
            std::abs(existing->second - equation.locked[axis]) >
                options.feasibility_tolerance) {
          *diagnostic = "contradictory position locks share a coordinate";
          return false;
        }
        seen_locks[key] = equation.locked[axis];
      }
    }
    equations->push_back(std::move(equation));
  }
  return true;
}

SolveResult solve_constraints(Object &scene, const Options &options) {
  std::vector<Object *> nodes;
  std::unordered_map<std::string, size_t> node_indices;
  std::vector<Equation> equations;
  std::string diagnostic;
  if (!collect_equations(scene, options, &nodes, &node_indices, &equations,
                         &diagnostic))
    return {SolveState::kConflict, {}, diagnostic};
  const size_t variable_count = nodes.size() * 3U;
  std::vector<double> original(variable_count, 0.0);
  for (size_t i = 0U; i < nodes.size(); ++i) {
    const P3 p = node_position(*nodes[i], options);
    for (size_t axis = 0U; axis < 3U; ++axis)
      original[3U * i + axis] = p[axis];
  }
  std::vector<double> x = original;

  auto evaluate = [&](const std::vector<double> &state,
                      std::vector<double> *hard_values,
                      std::vector<std::vector<double>> *hard_rows,
                      std::vector<std::string> *hard_ids, double *objective,
                      std::vector<double> *gradient,
                      std::vector<double> *hessian) -> bool {
    hard_values->clear();
    hard_rows->clear();
    hard_ids->clear();
    *objective = 0.0;
    gradient->assign(variable_count, 0.0);
    hessian->assign(variable_count * variable_count, 0.0);
    for (size_t i = 0U; i < variable_count; ++i) {
      const double residual = state[i] - original[i];
      *objective += 0.5 * residual * residual;
      (*gradient)[i] += residual;
      (*hessian)[i * variable_count + i] += 1.0;
    }
    for (const Equation &equation : equations) {
      if (equation.kind == "distance") {
        P3 delta{};
        double norm_squared = 0.0;
        for (size_t axis = 0U; axis < 3U; ++axis) {
          delta[axis] = state[3U * equation.first + axis] -
                        state[3U * equation.second + axis];
          norm_squared += delta[axis] * delta[axis];
        }
        const double norm = std::sqrt(norm_squared);
        if (!std::isfinite(norm) || norm <= 1e-12)
          return false;
        const double residual = norm - equation.value;
        std::vector<double> row(variable_count, 0.0);
        for (size_t axis = 0U; axis < 3U; ++axis) {
          row[3U * equation.first + axis] = delta[axis] / norm;
          row[3U * equation.second + axis] = -delta[axis] / norm;
        }
        if (equation.hard) {
          hard_values->push_back(residual);
          hard_rows->push_back(std::move(row));
          hard_ids->push_back(equation.id);
        } else {
          const double weight = equation.weight;
          *objective += 0.5 * weight * residual * residual;
          for (size_t i = 0U; i < variable_count; ++i) {
            (*gradient)[i] += weight * residual * row[i];
            for (size_t j = 0U; j < variable_count; ++j)
              (*hessian)[i * variable_count + j] += weight * row[i] * row[j];
          }
        }
      } else {
        for (size_t axis = 0U; axis < 3U; ++axis) {
          std::vector<double> row(variable_count, 0.0);
          row[3U * equation.first + axis] = 1.0;
          const double residual =
              state[3U * equation.first + axis] - equation.locked[axis];
          hard_values->push_back(residual);
          hard_rows->push_back(std::move(row));
          hard_ids->push_back(equation.id);
        }
      }
    }
    return true;
  };

  for (uint32_t iteration = 0U; iteration < options.max_solver_iterations;
       ++iteration) {
    std::vector<double> c, gradient, hessian;
    std::vector<std::vector<double>> rows;
    std::vector<std::string> ids;
    double objective = 0.0;
    if (!evaluate(x, &c, &rows, &ids, &objective, &gradient, &hessian))
      return {SolveState::kRejected, ids, "degenerate distance endpoints"};
    std::vector<std::vector<double>> independent_rows;
    std::vector<double> independent_c;
    std::vector<std::string> independent_ids;
    std::vector<std::vector<double>> basis;
    for (size_t row_index = 0U; row_index < rows.size(); ++row_index) {
      std::vector<double> residual = rows[row_index];
      for (const std::vector<double> &q : basis) {
        const double projection =
            std::inner_product(residual.begin(), residual.end(), q.begin(), 0.0);
        for (size_t i = 0U; i < variable_count; ++i)
          residual[i] -= projection * q[i];
      }
      const double norm = std::sqrt(std::inner_product(
          residual.begin(), residual.end(), residual.begin(), 0.0));
      if (norm <= 1e-10) {
        if (std::abs(c[row_index]) > options.feasibility_tolerance)
          return {SolveState::kConflict, {ids[row_index]},
                  "rank-deficient inconsistent hard equalities"};
        continue;
      }
      for (double &value : residual)
        value /= norm;
      basis.push_back(std::move(residual));
      independent_rows.push_back(rows[row_index]);
      independent_c.push_back(c[row_index]);
      independent_ids.push_back(ids[row_index]);
    }
    const size_t constraint_count = independent_rows.size();
    const size_t system_size = variable_count + constraint_count;
    std::vector<double> kkt(system_size * system_size, 0.0);
    std::vector<double> rhs(system_size, 0.0);
    for (size_t i = 0U; i < variable_count; ++i) {
      for (size_t j = 0U; j < variable_count; ++j)
        kkt[i * system_size + j] = hessian[i * variable_count + j];
      kkt[i * system_size + i] += 1e-9;
      rhs[i] = -gradient[i];
    }
    for (size_t row = 0U; row < constraint_count; ++row) {
      for (size_t column = 0U; column < variable_count; ++column) {
        kkt[column * system_size + variable_count + row] =
            independent_rows[row][column];
        kkt[(variable_count + row) * system_size + column] =
            independent_rows[row][column];
      }
      rhs[variable_count + row] = -independent_c[row];
    }
    std::vector<double> solution;
    if (!gaussian_solve(std::move(kkt), std::move(rhs), system_size, &solution))
      return {SolveState::kConflict, independent_ids,
              "rank-revealing KKT solve found a singular system"};
    double step_max = 0.0;
    double violation_max = 0.0;
    for (size_t i = 0U; i < variable_count; ++i)
      step_max = std::max(step_max, std::abs(solution[i]));
    for (const double value : c)
      violation_max = std::max(violation_max, std::abs(value));
    if (violation_max <= options.feasibility_tolerance && step_max < 1e-8)
      break;
    if (step_max > 1.0) {
      const double scale = 1.0 / step_max;
      for (size_t i = 0U; i < variable_count; ++i)
        solution[i] *= scale;
    }
    double multiplier_max = 0.0;
    for (size_t i = variable_count; i < solution.size(); ++i)
      multiplier_max = std::max(multiplier_max, std::abs(solution[i]));
    const double rho = std::max(10.0, 2.0 * multiplier_max + 1.0);
    auto merit = [&](const std::vector<double> &state) {
      std::vector<double> values, g, h;
      std::vector<std::vector<double>> unused_rows;
      std::vector<std::string> unused_ids;
      double obj = 0.0;
      if (!evaluate(state, &values, &unused_rows, &unused_ids, &obj, &g, &h))
        return std::numeric_limits<double>::infinity();
      double merit_value = obj;
      for (double value : values)
        merit_value += rho * std::abs(value);
      return merit_value;
    };
    const double old_merit = merit(x);
    bool accepted = false;
    double alpha = 1.0;
    for (size_t line_search = 0U; line_search < 20U; ++line_search) {
      std::vector<double> candidate = x;
      for (size_t i = 0U; i < variable_count; ++i)
        candidate[i] += alpha * solution[i];
      if (merit(candidate) < old_merit - 1e-12) {
        x = std::move(candidate);
        accepted = true;
        break;
      }
      alpha *= 0.5;
    }
    if (!accepted)
      return {SolveState::kRejected, independent_ids,
              "constraint line search did not improve merit"};
    if (iteration + 1U == options.max_solver_iterations)
      return {SolveState::kRejected, independent_ids,
              "constraint iteration budget exhausted"};
  }

  std::vector<double> c, gradient, hessian;
  std::vector<std::vector<double>> rows;
  std::vector<std::string> ids;
  double objective = 0.0;
  if (!evaluate(x, &c, &rows, &ids, &objective, &gradient, &hessian))
    return {SolveState::kRejected, ids, "final constraint evaluation failed"};
  for (double value : c)
    if (std::abs(value) > options.feasibility_tolerance)
      return {SolveState::kConflict, ids, "hard equality is infeasible"};
  for (size_t i = 0U; i < nodes.size(); ++i) {
    P3 position{x[3U * i], x[3U * i + 1U], x[3U * i + 2U]};
    (*nodes[i])["position_m"] = json_vec3(position);
  }
  return {SolveState::kApplied, {}, "constraint solution feasible"};
}

Json make_constraint(std::string id, std::string kind,
                     const std::vector<std::string> &targets,
                     std::string command_id, Json value, std::string unit,
                     bool hard, double weight, std::string source,
                     std::string status = "active") {
  Array target_json;
  for (const std::string &target : targets)
    target_json.emplace_back(target);
  return Json(Object{{"hard", Json(hard)},
                     {"id", Json(std::move(id))},
                     {"kind", Json(std::move(kind))},
                     {"source", Json(std::move(source))},
                     {"source_command_id", Json(std::move(command_id))},
                     {"status", Json(std::move(status))},
                     {"target_ids", Json(std::move(target_json))},
                     {"unit", Json(std::move(unit))},
                     {"value", std::move(value)},
                     {"weight", Json(weight)}});
}

std::string source_for_dimension(std::string_view source) {
  return std::string(source);
}

double dimension_metres(double value, std::string_view unit) {
  if (unit == "m")
    return value;
  if (unit == "cm")
    return value / 100.0;
  return value / 1000.0;
}

Json make_issue(std::string id, std::string code,
                const std::vector<std::string> &targets, uint64_t revision,
                std::string severity, std::string reason, std::string action,
                std::string sequence) {
  Array target_json;
  for (const std::string &target : targets)
    target_json.emplace_back(target);
  return Json(Object{{"action", Json(std::move(action))},
                     {"code", Json(std::move(code))},
                     {"id", Json(std::move(id))},
                     {"reason", Json(std::move(reason))},
                     {"scene_revision", Json(std::to_string(revision))},
                     {"severity", Json(std::move(severity))},
                     {"source_sequence", Json(std::move(sequence))},
                     {"state", Json("active")},
                     {"target_ids", Json(std::move(target_json))}});
}

void replace_id_in_array(Json &json, std::string_view old_id,
                         const std::vector<std::string> &replacement) {
  if (!json.is_array())
    return;
  Array result;
  for (Json &item : json.array()) {
    if (item.is_string() && item.string() == old_id) {
      for (const std::string &id : replacement)
        result.emplace_back(id);
    } else {
      result.push_back(std::move(item));
    }
  }
  json = Json(std::move(result));
}

void replace_adjacent_nodes(Json &ring_json, std::string_view start,
                            std::string_view end,
                            const std::vector<std::string> &inserted) {
  if (!ring_json.is_array())
    return;
  Array &ring = ring_json.array();
  for (size_t i = 0U; i < ring.size(); ++i) {
    const size_t j = (i + 1U) % ring.size();
    if (!ring[i].is_string() || !ring[j].is_string())
      continue;
    bool forward = ring[i].string() == start && ring[j].string() == end;
    bool reverse = ring[i].string() == end && ring[j].string() == start;
    if (!forward && !reverse)
      continue;
    std::vector<std::string> values = inserted;
    if (reverse)
      std::reverse(values.begin(), values.end());
    Array result;
    result.reserve(ring.size() + values.size());
    for (size_t k = 0U; k < ring.size(); ++k) {
      result.push_back(ring[k]);
      if (k == i)
        for (const std::string &id : values)
          result.emplace_back(id);
    }
    ring = std::move(result);
    return;
  }
}

void remove_node_from_rings(Object &scene, std::string_view node_id) {
  for (Json &region : array_member(scene, "regions")) {
    Object &object = region.object();
    auto remove = [&](Json *ring) {
      if (ring == nullptr || !ring->is_array())
        return;
      Array &items = ring->array();
      items.erase(std::remove_if(items.begin(), items.end(), [&](const Json &item) {
                    return item.is_string() && item.string() == node_id;
                  }),
                  items.end());
    };
    remove(member(object, "outer_node_ids"));
    Json *holes = member(object, "hole_node_ids");
    if (holes != nullptr && holes->is_array())
      for (Json &hole : holes->array())
        remove(&hole);
  }
}

bool recompute_region_areas(Object &scene, const Options &options) {
  P3 origin{}, u{}, v{}, n{};
  if (!basis_vectors(scene, &origin, &u, &v, &n, options))
    return false;
  std::unordered_map<std::string, P3> positions;
  for (const Json &node : array_member(scene, "nodes")) {
    std::string id;
    if (!string_member(node.object(), "id", &id))
      return false;
    positions.emplace(id, node_position(node.object(), options));
  }
  for (Json &region : array_member(scene, "regions")) {
    Object &object = region.object();
    Json *outer = member(object, "outer_node_ids");
    Json *holes = member(object, "hole_node_ids");
    if (outer == nullptr || holes == nullptr || !outer->is_array() ||
        !holes->is_array())
      return false;
    std::vector<P2> outer_ring;
    for (const Json &id : outer->array()) {
      if (!id.is_string() || !positions.contains(id.string()))
        return false;
      outer_ring.push_back(project_point(positions.at(id.string()), origin, u, v));
    }
    if (!simple_ring(outer_ring, options.geometry_epsilon_m) ||
        signed_area(outer_ring) <= 0.0)
      return false;
    double area = signed_area(outer_ring);
    for (const Json &hole : holes->array()) {
      if (!hole.is_array())
        return false;
      std::vector<P2> hole_ring;
      for (const Json &id : hole.array()) {
        if (!id.is_string() || !positions.contains(id.string()))
          return false;
        hole_ring.push_back(project_point(positions.at(id.string()), origin, u, v));
      }
      if (!simple_ring(hole_ring, options.geometry_epsilon_m) ||
          signed_area(hole_ring) >= 0.0)
        return false;
      area += signed_area(hole_ring);
    }
    object["area_m2"] = Json(area);
  }
  return true;
}

struct MutationResult {
  command::Outcome outcome{command::Outcome::kRejected};
  std::string diagnostic;
  std::vector<std::string> issue_ids;
  std::vector<std::string> conflict_ids;
  std::string original_command_id;
};

bool insert_split(Object &scene, const CommandData &command, const P3 &position,
                  std::string_view edge_id, const Options &options,
                  std::string *diagnostic) {
  Array &nodes = array_member(scene, "nodes");
  Array &edges = array_member(scene, "edges");
  Object *edge = find_entity(edges, edge_id);
  if (edge == nullptr) {
    *diagnostic = "split target edge is not current";
    return false;
  }
  std::string start, end;
  if (!string_member(*edge, "start_node_id", &start) ||
      !string_member(*edge, "end_node_id", &end))
    return false;
  const Object *start_node = find_entity(nodes, start);
  const Object *end_node = find_entity(nodes, end);
  if (start_node == nullptr || end_node == nullptr)
    return false;
  const P3 a = node_position(*start_node, options);
  const P3 b = node_position(*end_node, options);
  if (squared_distance(position, a) <= options.geometry_epsilon_m *
                                          options.geometry_epsilon_m ||
      squared_distance(position, b) <= options.geometry_epsilon_m *
                                          options.geometry_epsilon_m) {
    *diagnostic = "split point is too close to an existing endpoint";
    return false;
  }
  std::vector<std::string> surfaces =
      json_ids(*member(*edge, "surface_ids"));
  std::vector<std::string> supports =
      json_ids(*member(member(*edge, "provenance")->object(), "support_ids"));
  const bool protected_topology = member(*edge, "protected_topology")->boolean();
  const std::string node_id = deterministic_uuid(command.command_id, "split-node");
  const std::string first_id = deterministic_uuid(command.command_id, "split-edge", 0U);
  const std::string second_id = deterministic_uuid(command.command_id, "split-edge", 1U);
  nodes.push_back(make_node(node_id, position, supports));
  Json first = make_edge(first_id, start, node_id, {std::string(edge_id)}, surfaces,
                         supports, protected_topology);
  Json second = make_edge(second_id, node_id, end, {std::string(edge_id)}, surfaces,
                          supports, protected_topology);
  erase_entity(edges, edge_id);
  edges.push_back(std::move(first));
  edges.push_back(std::move(second));
  for (Json &feature : array_member(scene, "features"))
    replace_id_in_array(*member(feature.object(), "edge_ids"), edge_id,
                        {first_id, second_id});
  for (Json &region : array_member(scene, "regions")) {
    replace_adjacent_nodes(*member(region.object(), "outer_node_ids"), start, end,
                           {node_id});
    Json *holes = member(region.object(), "hole_node_ids");
    for (Json &hole : holes->array())
      replace_adjacent_nodes(hole, start, end, {node_id});
  }
  return true;
}

MutationResult apply_mark_corner(Object &scene, const CommandData &command,
                                 uint64_t new_revision,
                                 const Options &options) {
  const Json &point_json = *member(*command.payload, "point_m");
  if (point_json.is_null()) {
    const std::string issue_id =
        deterministic_uuid(command.command_id, "pending-corner-issue");
    array_member(scene, "issues")
        .push_back(make_issue(issue_id, "HSS_PENDING_CORNER", command.targets,
                              new_revision, "warning",
                              "Corner ray has no supported metric point.",
                              "Capture another view or confirm a supported point.",
                              command.event_sequence_text));
    return {command::Outcome::kApplied, "pending corner retained without geometry",
            {issue_id}, {}, {}};
  }
  P3 point{};
  vec3(point_json, &point, options.maximum_coordinate_m);
  if (command.targets.size() == 1U &&
      find_entity(array_member(scene, "edges"), command.targets.front()) != nullptr) {
    std::string diagnostic;
    if (!insert_split(scene, command, point, command.targets.front(), options,
                      &diagnostic))
      return {command::Outcome::kRejected, diagnostic, {}, {}, {}};
    return {command::Outcome::kApplied, "supported corner inserted into edge", {}, {}, {}};
  }
  const std::vector<std::string> supports =
      json_ids(*member(*command.payload, "support_ids"));
  const std::string node_id = deterministic_uuid(command.command_id, "corner-node");
  array_member(scene, "nodes")
      .push_back(make_node(node_id, point, supports,
                          supports.empty() ? "inferred" : "fitted"));
  array_member(scene, "constraints")
      .push_back(make_constraint(
          deterministic_uuid(command.command_id, "corner-protection"),
          "topology_protection", {node_id}, command.command_id, Json(nullptr),
          "none", true, 0.0, "user_intent"));
  return {command::Outcome::kApplied, "supported standalone corner recorded", {}, {}, {}};
}

MutationResult apply_split(Object &scene, const CommandData &command,
                           const Options &options) {
  Array &nodes = array_member(scene, "nodes");
  const Object *edge =
      find_entity(array_member(scene, "edges"), command.targets.front());
  if (edge == nullptr)
    return {command::Outcome::kRejected, "split target edge is not current", {}, {}, {}};
  std::string start, end;
  string_member(*edge, "start_node_id", &start);
  string_member(*edge, "end_node_id", &end);
  const P3 a = node_position(*find_entity(nodes, start), options);
  const P3 b = node_position(*find_entity(nodes, end), options);
  const double t = member(*command.payload, "edge_parameter")->number();
  P3 point{};
  for (size_t axis = 0U; axis < 3U; ++axis)
    point[axis] = a[axis] + t * (b[axis] - a[axis]);
  std::string diagnostic;
  if (!insert_split(scene, command, point, command.targets.front(), options,
                    &diagnostic))
    return {command::Outcome::kRejected, diagnostic, {}, {}, {}};
  return {command::Outcome::kApplied, "edge split with explicit parent lineage", {}, {}, {}};
}

MutationResult apply_merge(Object &scene, const CommandData &command) {
  Array &edges = array_member(scene, "edges");
  Array &nodes = array_member(scene, "nodes");
  std::unordered_map<std::string, size_t> degree;
  std::vector<std::string> all_surfaces, all_parents, all_supports;
  for (const std::string &id : command.targets) {
    const Object *edge = find_entity(edges, id);
    if (edge == nullptr)
      return {command::Outcome::kRejected, "merge target edge is not current", {}, {}, {}};
    if (member(*edge, "protected_topology")->boolean())
      return {command::Outcome::kConflict,
              "merge would remove protected topology", {}, {id}, {}};
    std::string start, end;
    string_member(*edge, "start_node_id", &start);
    string_member(*edge, "end_node_id", &end);
    ++degree[start];
    ++degree[end];
    all_parents.push_back(id);
    const auto append_unique = [](std::vector<std::string> *destination,
                                  const std::vector<std::string> &source) {
      for (const std::string &value : source)
        if (std::find(destination->begin(), destination->end(), value) ==
            destination->end())
          destination->push_back(value);
    };
    append_unique(&all_surfaces, json_ids(*member(*edge, "surface_ids")));
    append_unique(&all_parents, json_ids(*member(*edge, "parent_edge_ids")));
    append_unique(&all_supports,
                  json_ids(*member(member(*edge, "provenance")->object(),
                                   "support_ids")));
  }
  std::vector<std::string> endpoints, interior;
  for (const auto &[id, count] : degree) {
    if (count == 1U)
      endpoints.push_back(id);
    else if (count == 2U)
      interior.push_back(id);
    else
      return {command::Outcome::kRejected,
              "merge targets do not form one nonbranching chain", {}, {}, {}};
  }
  if (endpoints.size() != 2U || degree.size() != command.targets.size() + 1U)
    return {command::Outcome::kRejected,
            "merge targets are disconnected or cyclic", {}, {}, {}};
  std::sort(endpoints.begin(), endpoints.end());
  std::sort(interior.begin(), interior.end());
  for (const std::string &node_id : interior) {
    const Object *node = find_entity(nodes, node_id);
    if (node == nullptr || member(*node, "position_locked")->boolean())
      return {command::Outcome::kConflict,
              "merge would remove a locked intermediate node", {}, {}, {}};
    for (const Json &feature : array_member(scene, "features")) {
      if (!member(feature.object(), "protected_topology")->boolean())
        continue;
      const std::vector<std::string> feature_edges =
          json_ids(*member(feature.object(), "edge_ids"));
      for (const std::string &target : command.targets)
        if (std::find(feature_edges.begin(), feature_edges.end(), target) !=
            feature_edges.end())
          return {command::Outcome::kConflict,
                  "merge would alter a protected feature chain", {}, {target}, {}};
    }
  }
  const std::string merged_id = deterministic_uuid(command.command_id, "merged-edge");
  for (const std::string &id : command.targets)
    erase_entity(edges, id);
  edges.push_back(make_edge(merged_id, endpoints[0], endpoints[1], all_parents,
                            all_surfaces, all_supports, false));
  for (Json &feature : array_member(scene, "features")) {
    Json *edge_ids = member(feature.object(), "edge_ids");
    bool contains = false;
    for (const std::string &id : command.targets)
      contains = contains || std::any_of(edge_ids->array().begin(), edge_ids->array().end(),
                                         [&](const Json &item) {
                                           return item.is_string() && item.string() == id;
                                         });
    if (contains) {
      Array retained;
      for (Json &item : edge_ids->array())
        if (!(item.is_string() &&
              std::find(command.targets.begin(), command.targets.end(), item.string()) !=
                  command.targets.end()))
          retained.push_back(std::move(item));
      retained.emplace_back(merged_id);
      *edge_ids = Json(std::move(retained));
    }
  }
  for (const std::string &node_id : interior) {
    remove_node_from_rings(scene, node_id);
    erase_entity(nodes, node_id);
  }
  return {command::Outcome::kApplied, "adjacent edges merged with retained lineage", {}, {}, {}};
}

MutationResult apply_boundary_feature(Object &scene, const CommandData &command,
                                      const Options &options) {
  Array &nodes = array_member(scene, "nodes");
  Array &edges = array_member(scene, "edges");
  Object *parent = find_entity(edges, command.targets.front());
  if (parent == nullptr)
    return {command::Outcome::kRejected, "parent boundary edge is not current", {}, {}, {}};
  if (member(*parent, "protected_topology")->boolean())
    return {command::Outcome::kConflict,
            "parent edge is already protected and cannot be replaced", {},
            {command.targets.front()}, {}};
  for (const Json &feature : array_member(scene, "features")) {
    if (!member(feature.object(), "protected_topology")->boolean())
      continue;
    const std::vector<std::string> feature_edges =
        json_ids(*member(feature.object(), "edge_ids"));
    if (std::find(feature_edges.begin(), feature_edges.end(),
                  command.targets.front()) != feature_edges.end())
      return {command::Outcome::kConflict,
              "parent edge belongs to a protected feature chain", {},
              {command.targets.front()}, {}};
  }
  std::string start, end;
  string_member(*parent, "start_node_id", &start);
  string_member(*parent, "end_node_id", &end);
  const P3 a = node_position(*find_entity(nodes, start), options);
  const P3 b = node_position(*find_entity(nodes, end), options);
  const P3 delta{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  const double length_squared = squared_distance(a, b);
  std::vector<P3> points;
  for (const Json &value : member(*command.payload, "ordered_points_m")->array()) {
    P3 p{};
    vec3(value, &p, options.maximum_coordinate_m);
    points.push_back(p);
  }
  auto parameter = [&](const P3 &p) {
    double numerator = 0.0;
    for (size_t axis = 0U; axis < 3U; ++axis)
      numerator += (p[axis] - a[axis]) * delta[axis];
    return numerator / length_squared;
  };
  auto line_distance = [&](const P3 &p) {
    const double t = parameter(p);
    P3 projected{};
    for (size_t axis = 0U; axis < 3U; ++axis)
      projected[axis] = a[axis] + t * delta[axis];
    return std::sqrt(squared_distance(p, projected));
  };
  double first_t = parameter(points.front());
  double last_t = parameter(points.back());
  if (line_distance(points.front()) > 1e-6 || line_distance(points.back()) > 1e-6 ||
      first_t < -1e-8 || first_t > 1.0 + 1e-8 || last_t < -1e-8 ||
      last_t > 1.0 + 1e-8 || std::abs(first_t - last_t) <= 1e-8)
    return {command::Outcome::kRejected,
            "feature chain endpoints must be distinct supported points on parent edge",
            {}, {}, {}};
  if (first_t > last_t)
    std::reverse(points.begin(), points.end());
  for (size_t i = 1U; i < points.size(); ++i)
    if (squared_distance(points[i - 1U], points[i]) <=
        options.geometry_epsilon_m * options.geometry_epsilon_m)
      return {command::Outcome::kRejected,
              "feature chain contains a zero-length segment", {}, {}, {}};

  std::vector<std::string> supports =
      json_ids(*member(*command.payload, "support_ids"));
  std::vector<std::string> surfaces =
      json_ids(*member(*parent, "surface_ids"));
  std::vector<std::string> point_ids;
  point_ids.reserve(points.size());
  for (size_t i = 0U; i < points.size(); ++i) {
    const bool at_start = squared_distance(points[i], a) <=
                          options.geometry_epsilon_m * options.geometry_epsilon_m;
    const bool at_end = squared_distance(points[i], b) <=
                        options.geometry_epsilon_m * options.geometry_epsilon_m;
    if (at_start) {
      point_ids.push_back(start);
    } else if (at_end) {
      point_ids.push_back(end);
    } else {
      std::string id = deterministic_uuid(command.command_id, "feature-node", i);
      nodes.push_back(make_node(id, points[i], supports,
                                supports.empty() ? "inferred" : "fitted"));
      point_ids.push_back(std::move(id));
    }
  }
  std::vector<std::string> chain_nodes;
  chain_nodes.push_back(start);
  for (const std::string &id : point_ids)
    if (chain_nodes.back() != id)
      chain_nodes.push_back(id);
  if (chain_nodes.back() != end)
    chain_nodes.push_back(end);
  std::vector<std::string> new_edge_ids;
  for (size_t i = 0U; i + 1U < chain_nodes.size(); ++i) {
    std::string edge_id = deterministic_uuid(command.command_id, "feature-edge", i);
    edges.push_back(make_edge(edge_id, chain_nodes[i], chain_nodes[i + 1U],
                              {command.targets.front()}, surfaces, supports, true));
    new_edge_ids.push_back(std::move(edge_id));
  }
  erase_entity(edges, command.targets.front());
  for (Json &feature : array_member(scene, "features"))
    replace_id_in_array(*member(feature.object(), "edge_ids"),
                        command.targets.front(), new_edge_ids);
  std::vector<std::string> inserted(point_ids.begin(), point_ids.end());
  if (!inserted.empty() && inserted.front() == start)
    inserted.erase(inserted.begin());
  if (!inserted.empty() && inserted.back() == end)
    inserted.pop_back();
  for (Json &region : array_member(scene, "regions")) {
    replace_adjacent_nodes(*member(region.object(), "outer_node_ids"), start, end,
                           inserted);
    for (Json &hole : member(region.object(), "hole_node_ids")->array())
      replace_adjacent_nodes(hole, start, end, inserted);
  }
  Array feature_edges;
  for (const std::string &id : new_edge_ids)
    feature_edges.emplace_back(id);
  Array surface_json;
  for (const std::string &id : surfaces)
    surface_json.emplace_back(id);
  const std::string feature_id = deterministic_uuid(command.command_id, "feature");
  array_member(scene, "features")
      .emplace_back(Object{{"attachment", Json("attached")},
                           {"edge_ids", Json(std::move(feature_edges))},
                           {"id", Json(feature_id)},
                           {"kind", Json(command.type == "mark_protrusion"
                                             ? "protrusion"
                                             : "recess")},
                           {"protected_topology", Json(true)},
                           {"provenance",
                            provenance(supports.empty() ? "inferred" : "fitted",
                                       "user", supports,
                                       supports.empty() ? "needs_review"
                                                        : "provisional")},
                           {"surface_ids", Json(std::move(surface_json))}});
  return {command::Outcome::kApplied,
          "ordered arbitrary-angle feature chain inserted and protected", {}, {}, {}};
}

MutationResult apply_obstruction(Object &scene, const CommandData &command,
                                 uint64_t new_revision,
                                 const Options &options) {
  std::vector<P2> footprint;
  for (const Json &point : member(*command.payload, "footprint_m")->array()) {
    P2 p{};
    vec2(point, &p, options.maximum_coordinate_m);
    footprint.push_back(p);
  }
  if (!simple_ring(footprint, options.geometry_epsilon_m))
    return {command::Outcome::kRejected,
            "obstruction footprint is degenerate or self-intersecting", {}, {}, {}};
  std::string attachment;
  string_member(*command.payload, "attachment", &attachment);
  const bool fixed = member(*command.payload, "fixed")->boolean();
  const std::string object_id = deterministic_uuid(command.command_id, "obstruction-object");
  Array footprint_json;
  for (const P2 &p : footprint)
    footprint_json.emplace_back(Array{Json(p[0]), Json(p[1])});
  array_member(scene, "objects")
      .emplace_back(Object{{"footprint_m", Json(std::move(footprint_json))},
                           {"id", Json(object_id)},
                           {"label", Json(fixed ? "fixed obstruction" : "obstruction")},
                           {"mesh_asset", Json(nullptr)},
                           {"provenance", provenance("inferred", "user", {})},
                           {"surface_ids", Json(Array{})}});

  if (!fixed)
    return {command::Outcome::kApplied,
            "nonfixed obstruction retained in the planning-object layer", {}, {}, {}};
  if (attachment == "attached") {
    const std::string issue_id =
        deterministic_uuid(command.command_id, "attached-obstruction-issue");
    array_member(scene, "issues")
        .push_back(make_issue(
            issue_id, "HSS_ATTACHED_OBSTRUCTION_TOPOLOGY_PENDING", {object_id},
            new_revision, "warning",
            "Attached obstruction footprint is retained but not spliced into the boundary.",
            "Mark the ordered boundary returns before using it for area.",
            command.event_sequence_text));
    return {command::Outcome::kApplied,
            "attached obstruction retained without unsupported boundary splice",
            {issue_id}, {}, {}};
  }

  P3 origin{}, u{}, v{}, n{};
  basis_vectors(scene, &origin, &u, &v, &n, options);
  if (signed_area(footprint) > 0.0)
    std::reverse(footprint.begin(), footprint.end());
  std::vector<std::string> node_ids, edge_ids;
  for (size_t i = 0U; i < footprint.size(); ++i) {
    P3 p{};
    for (size_t axis = 0U; axis < 3U; ++axis)
      p[axis] = origin[axis] + footprint[i][0] * u[axis] + footprint[i][1] * v[axis];
    std::string id = deterministic_uuid(command.command_id, "obstruction-node", i);
    array_member(scene, "nodes").push_back(make_node(id, p, {}, "inferred"));
    node_ids.push_back(std::move(id));
  }
  for (size_t i = 0U; i < node_ids.size(); ++i) {
    std::string id = deterministic_uuid(command.command_id, "obstruction-edge", i);
    array_member(scene, "edges")
        .push_back(make_edge(id, node_ids[i], node_ids[(i + 1U) % node_ids.size()],
                             {}, {}, {}, fixed));
    edge_ids.push_back(std::move(id));
  }
  Array feature_edges;
  for (const std::string &id : edge_ids)
    feature_edges.emplace_back(id);
  const std::string feature_id = deterministic_uuid(command.command_id, "obstruction-feature");
  array_member(scene, "features")
      .emplace_back(Object{{"attachment", Json(attachment)},
                           {"edge_ids", Json(std::move(feature_edges))},
                           {"id", Json(feature_id)},
                           {"kind", Json("fixed_obstruction")},
                           {"protected_topology", Json(fixed)},
                           {"provenance", provenance("inferred", "user", {})},
                           {"surface_ids", Json(Array{})}});
  std::vector<std::string> issue_ids;
  if (fixed && attachment == "detached") {
    bool inserted = false;
    for (Json &region : array_member(scene, "regions")) {
      Json *outer = member(region.object(), "outer_node_ids");
      std::vector<P2> outer_ring;
      for (const Json &id : outer->array()) {
        const Object *node = find_entity(array_member(scene, "nodes"), id.string());
        outer_ring.push_back(project_point(node_position(*node, options), origin, u, v));
      }
      if (point_in_ring(footprint.front(), outer_ring, options.geometry_epsilon_m)) {
        Array hole;
        for (const std::string &id : node_ids)
          hole.emplace_back(id);
        member(region.object(), "hole_node_ids")->array().push_back(Json(std::move(hole)));
        inserted = true;
        break;
      }
    }
    if (!inserted)
      return {command::Outcome::kRejected,
              "detached fixed obstruction is outside every accepted region", {}, {}, {}};
  } else if (attachment == "unresolved") {
    const std::string issue_id = deterministic_uuid(command.command_id, "obstruction-issue");
    array_member(scene, "issues")
        .push_back(make_issue(issue_id, "HSS_OBSTRUCTION_ATTACHMENT_UNRESOLVED",
                              {feature_id}, new_revision, "warning",
                              "Obstruction attachment is unresolved; it was not subtracted as a hole.",
                              "Capture the attachment boundary or classify it explicitly.",
                              command.event_sequence_text));
    issue_ids.push_back(issue_id);
  }
  return {command::Outcome::kApplied,
          fixed && attachment == "detached"
              ? "detached fixed obstruction added as a validated hole"
              : "obstruction recorded without unsupported area subtraction",
          issue_ids, {}, {}};
}

MutationResult apply_classification(Object &scene, const CommandData &command) {
  Object *object =
      find_entity(array_member(scene, "objects"), command.targets.front());
  if (object == nullptr)
    return {command::Outcome::kRejected,
            "classification target is not a current object", {}, {}, {}};
  std::string label, origin;
  string_member(*command.payload, "label", &label);
  string_member(*command.payload, "semantic_origin", &origin);
  const Json *confidence = member(*command.payload, "confidence");
  if (origin == "external_proposal" ||
      (confidence != nullptr && !confidence->is_null()))
    return {command::Outcome::kRejected,
            "scene schema cannot preserve external-origin confidence without loss",
            {}, {}, {}};
  (*object)["label"] = Json(label);
  Object &prov = member(*object, "provenance")->object();
  prov["semantic_origin"] = Json(origin);
  return {command::Outcome::kApplied,
          "semantic label updated without changing metric geometry", {}, {}, {}};
}

MutationResult apply_dimension(Object &scene, const CommandData &command,
                               const Options &options) {
  std::string a, b, unit, source, mode;
  string_member(*command.payload, "endpoint_a", &a);
  string_member(*command.payload, "endpoint_b", &b);
  string_member(*command.payload, "unit", &unit);
  string_member(*command.payload, "source", &source);
  string_member(*command.payload, "mode", &mode);
  if (find_entity(array_member(scene, "nodes"), a) == nullptr ||
      find_entity(array_member(scene, "nodes"), b) == nullptr)
    return {command::Outcome::kRejected,
            "dimension endpoints must be current scene nodes", {}, {}, {}};
  const double value = dimension_metres(member(*command.payload, "value")->number(), unit);
  const bool hard = mode == "exact_model";
  const std::string constraint_id = deterministic_uuid(command.command_id, "dimension");
  array_member(scene, "constraints")
      .push_back(make_constraint(constraint_id, "distance", {a, b},
                                 command.command_id, Json(value), "m", hard,
                                 member(*command.payload, "weight")->number(),
                                 source_for_dimension(source)));
  const SolveResult solved = solve_constraints(scene, options);
  if (solved.state == SolveState::kApplied)
    return {command::Outcome::kApplied,
            hard ? "exact-model distance equality solved"
                 : "soft measurement retained and fitted",
            {}, {}, {}};
  erase_entity(array_member(scene, "constraints"), constraint_id);
  return {solved.state == SolveState::kConflict ? command::Outcome::kConflict
                                                : command::Outcome::kRejected,
          solved.diagnostic, {},
          solved.conflict_ids.empty() ? std::vector<std::string>{constraint_id}
                                      : solved.conflict_ids,
          {}};
}

MutationResult apply_protection(Object &scene, const CommandData &command) {
  const std::string &target = command.targets.front();
  if (Object *feature = find_entity(array_member(scene, "features"), target)) {
    (*feature)["protected_topology"] = Json(true);
    for (const std::string &edge_id : json_ids(*member(*feature, "edge_ids")))
      if (Object *edge = find_entity(array_member(scene, "edges"), edge_id))
        (*edge)["protected_topology"] = Json(true);
  } else if (Object *edge = find_entity(array_member(scene, "edges"), target)) {
    (*edge)["protected_topology"] = Json(true);
  } else {
    return {command::Outcome::kRejected,
            "protect target must be a current feature or edge", {}, {}, {}};
  }
  array_member(scene, "constraints")
      .push_back(make_constraint(
          deterministic_uuid(command.command_id, "topology-protection"),
          "topology_protection", {target}, command.command_id, Json(nullptr),
          "none", true, 0.0, "user_intent"));
  return {command::Outcome::kApplied,
          "topology protected without locking coordinates", {}, {}, {}};
}

MutationResult apply_lock(Object &scene, const CommandData &command,
                          const Options &options) {
  Object *node = find_entity(array_member(scene, "nodes"), command.targets.front());
  if (node == nullptr)
    return {command::Outcome::kRejected,
            "lock target must be a current node", {}, {}, {}};
  P3 position{};
  vec3(*member(*command.payload, "position_m"), &position,
       options.maximum_coordinate_m);
  const std::string constraint_id = deterministic_uuid(command.command_id, "position-lock");
  array_member(scene, "constraints")
      .push_back(make_constraint(constraint_id, "position_lock",
                                 {command.targets.front()}, command.command_id,
                                 json_vec3(position), "m", true, 0.0,
                                 "user_intent"));
  const SolveResult solved = solve_constraints(scene, options);
  if (solved.state != SolveState::kApplied) {
    erase_entity(array_member(scene, "constraints"), constraint_id);
    return {solved.state == SolveState::kConflict ? command::Outcome::kConflict
                                                  : command::Outcome::kRejected,
            solved.diagnostic, {},
            solved.conflict_ids.empty() ? std::vector<std::string>{constraint_id}
                                        : solved.conflict_ids,
            {}};
  }
  (*node)["position_locked"] = Json(true);
  return {command::Outcome::kApplied,
          "node position locked by explicit hard equality", {}, {}, {}};
}

MutationResult apply_unlock(Object &scene, const CommandData &command) {
  Object *node = find_entity(array_member(scene, "nodes"), command.targets.front());
  std::string constraint_id;
  string_member(*command.payload, "constraint_id", &constraint_id);
  Object *constraint =
      find_entity(array_member(scene, "constraints"), constraint_id);
  if (node == nullptr || constraint == nullptr)
    return {command::Outcome::kRejected,
            "unlock target or constraint is not current", {}, {}, {}};
  std::string kind, status;
  string_member(*constraint, "kind", &kind);
  string_member(*constraint, "status", &status);
  const std::vector<std::string> targets =
      json_ids(*member(*constraint, "target_ids"));
  if (kind != "position_lock" || status != "active" || targets.size() != 1U ||
      targets.front() != command.targets.front())
    return {command::Outcome::kRejected,
            "constraint is not the active position lock for target", {}, {}, {}};
  (*constraint)["status"] = Json("removed");
  bool still_locked = false;
  for (const Json &candidate : array_member(scene, "constraints")) {
    std::string candidate_kind, candidate_status;
    string_member(candidate.object(), "kind", &candidate_kind);
    string_member(candidate.object(), "status", &candidate_status);
    const std::vector<std::string> candidate_targets =
        json_ids(*member(candidate.object(), "target_ids"));
    if (candidate_kind == "position_lock" && candidate_status == "active" &&
        candidate_targets.size() == 1U &&
        candidate_targets.front() == command.targets.front())
      still_locked = true;
  }
  (*node)["position_locked"] = Json(still_locked);
  return {command::Outcome::kApplied,
          "position lock removed by explicit recorded action", {}, {}, {}};
}

MutationResult apply_unknown(Object &scene, const CommandData &command,
                             uint64_t new_revision) {
  Array feature_edges, feature_surfaces;
  for (const std::string &target : command.targets) {
    if (find_entity(array_member(scene, "edges"), target) != nullptr)
      feature_edges.emplace_back(target);
    else {
      const Array &surfaces = array_member(scene, "surfaces");
      if (find_entity(surfaces, target) != nullptr)
        feature_surfaces.emplace_back(target);
    }
  }
  const std::string feature_id = deterministic_uuid(command.command_id, "unknown-feature");
  array_member(scene, "features")
      .emplace_back(Object{{"attachment", Json("unresolved")},
                           {"edge_ids", Json(std::move(feature_edges))},
                           {"id", Json(feature_id)},
                           {"kind", Json("unknown")},
                           {"protected_topology", Json(false)},
                           {"provenance",
                            provenance("unknown", "user", {}, "needs_review")},
                           {"surface_ids", Json(std::move(feature_surfaces))}});
  std::string reason;
  string_member(*command.payload, "reason", &reason);
  const std::string issue_id = deterministic_uuid(command.command_id, "unknown-issue");
  array_member(scene, "issues")
      .push_back(make_issue(issue_id, "HSS_UNKNOWN_REGION", command.targets,
                            new_revision, "warning", reason,
                            "Capture supporting evidence or retain as unknown.",
                            command.event_sequence_text));
  return {command::Outcome::kApplied,
          "unknown region recorded without generated metric geometry", {issue_id}, {}, {}};
}

MutationResult apply_command(Object &scene, const CommandData &command,
                             uint64_t new_revision, const Options &options) {
  if (command.type == "mark_corner")
    return apply_mark_corner(scene, command, new_revision, options);
  if (command.type == "split_edge")
    return apply_split(scene, command, options);
  if (command.type == "merge_edges")
    return apply_merge(scene, command);
  if (command.type == "mark_protrusion" || command.type == "mark_recess")
    return apply_boundary_feature(scene, command, options);
  if (command.type == "mark_obstruction")
    return apply_obstruction(scene, command, new_revision, options);
  if (command.type == "classify_object")
    return apply_classification(scene, command);
  if (command.type == "set_dimension")
    return apply_dimension(scene, command, options);
  if (command.type == "protect_feature")
    return apply_protection(scene, command);
  if (command.type == "lock_position")
    return apply_lock(scene, command, options);
  if (command.type == "unlock_position")
    return apply_unlock(scene, command);
  if (command.type == "mark_unknown")
    return apply_unknown(scene, command, new_revision);
  return {command::Outcome::kRejected,
          "command variant is valid but has no installed mutation consumer", {}, {}, {}};
}

std::string result_json(const CommandData &command, std::string state,
                        uint64_t revision,
                        const std::vector<std::string> &issue_ids,
                        const std::vector<std::string> &conflict_ids,
                        std::string diagnostic) {
  Array issues, conflicts;
  for (const std::string &id : issue_ids)
    issues.emplace_back(id);
  for (const std::string &id : conflict_ids)
    conflicts.emplace_back(id);
  return serialize(Json(Object{
      {"command_id", Json(command.command_id)},
      {"conflict_constraint_ids", Json(std::move(conflicts))},
      {"diagnostic", Json(std::move(diagnostic))},
      {"durability", Json("not_committed")},
      {"issue_ids", Json(std::move(issues))},
      {"journal_commit_id", Json(nullptr)},
      {"scene_revision", Json(std::to_string(revision))},
      {"schema_version", Json("1.1")},
      {"state", Json(std::move(state))},
  }));
}

std::string state_name(command::Outcome outcome) {
  switch (outcome) {
  case command::Outcome::kApplied: return "applied";
  case command::Outcome::kConflict: return "conflict";
  case command::Outcome::kRejected: return "rejected";
  }
  return "rejected";
}

command::SceneState gate_scene(const SceneView &scene) {
  command::SceneState result;
  result.project_id = scene.project_id;
  result.revision = scene.revision;
  const Object &root = *scene.object;
  for (std::string_view collection : {"nodes", "edges", "surfaces", "features",
                                      "regions", "objects", "constraints",
                                      "issues"}) {
    const Json *array = member(root, collection);
    if (array == nullptr || !array->is_array())
      continue;
    for (const Json &entity : array->array()) {
      if (!entity.is_object())
        continue;
      std::string id;
      if (string_member(entity.object(), "id", &id) && valid_uuid(id))
        result.current_targets.push_back({id, 0U});
    }
  }
  return result;
}

Status map_gate_status(command::Status status) {
  switch (status) {
  case command::Status::kApplied: return Status::kApplied;
  case command::Status::kStoredRetry: return Status::kStoredRetry;
  case command::Status::kConflict: return Status::kConflict;
  case command::Status::kRejected: return Status::kRejected;
  case command::Status::kDuplicateMismatch: return Status::kDuplicateMismatch;
  case command::Status::kProjectMismatch: return Status::kProjectMismatch;
  case command::Status::kStaleRevision: return Status::kStaleRevision;
  case command::Status::kFutureRevision: return Status::kFutureRevision;
  case command::Status::kTargetNotCurrent: return Status::kTargetNotCurrent;
  case command::Status::kTargetLineageChanged:
    return Status::kTargetLineageChanged;
  case command::Status::kResourceLimitExceeded:
    return Status::kResourceLimitExceeded;
  case command::Status::kInvalidOptions:
  case command::Status::kInvalidCommand: return Status::kInvalidCommand;
  case command::Status::kApplyFailure:
  case command::Status::kInvalidApplyResult: return Status::kInternalError;
  }
  return Status::kInternalError;
}

bool same_scene_payload_except_revision(const std::string &expected,
                                        const Json &current) {
  try {
    Json expected_json = JsonParser(expected).parse();
    if (!expected_json.is_object() || !current.is_object())
      return false;
    expected_json.object()["scene_revision"] =
        *member(current.object(), "scene_revision");
    return serialize(expected_json) == serialize(current);
  } catch (...) {
    return false;
  }
}


// Keep exact command-owned nonmetric annotations: target-independent unknowns
// and explicit unresolved protection intent when source lineage is unproven.
// Candidate membership naturally handles existing strict undo/redo. No mutable
// overlay sidecar is committed ahead of immutable scene publication.
bool collect_retained_unknowns(const Object &scene,
                              const std::vector<JournalRecord> &journal,
                              const CommandData &current,
                              uint64_t revision,
                              std::shared_ptr<const RetainedUnknowns> *output,
                              bool *resource_limit) {
  auto retained = std::make_shared<RetainedUnknowns>();
  // Index the validated candidate once. A linear scan for every journaled
  // annotation made the capacity regression needlessly quadratic per edit.
  using Index = std::map<std::string_view, const Json *, std::less<>>;
  const auto index_entities = [](const Array &items) {
    Index index;
    for (const Json &entity : items)
      index.emplace(member(entity.object(), "id")->string(), &entity);
    return index;
  };
  const auto find_entity = [](const Index &index, const std::string &id) -> const Json * {
    const auto found = index.find(id);
    return found == index.end() ? nullptr : found->second;
  };
  const Index features = index_entities(member(scene, "features")->array());
  const Index issues = index_entities(member(scene, "issues")->array());
  const Array &constraints = member(scene, "constraints")->array();
  size_t retained_constraints = 0U;
  const auto collect = [&](const CommandData &command) {
    if (command.type != "mark_unknown" || !command.targets.empty())
      return true;
    const auto *feature = find_entity(features, deterministic_uuid(command.command_id, "unknown-feature"));
    const auto *issue = find_entity(issues, deterministic_uuid(command.command_id, "unknown-issue"));
    if (!feature && !issue) // An accepted undo removed both entities.
      return true;
    if (!feature || !issue) return false;
    if (retained->size() >= kMaxRetainedUnknowns) {
      *resource_limit = true;
      return false;
    }
    // Compare the entire validated entity to the original command's output:
    // no targeted/foreign geometry, changed protection, or orphan reference
    // can become a target-independent retained annotation by appearance alone.
    Object expected{{"features", Json(Array{})}, {"issues", Json(Array{})}};
    (void)apply_unknown(expected, command, revision);
    Json expected_feature = member(expected, "features")->array().front();
    Json expected_issue = member(expected, "issues")->array().front();
    const Json *prior_revision = member(issue->object(), "scene_revision");
    if (!prior_revision)
      return false;
    expected_issue.object()["scene_revision"] = *prior_revision;
    // Protection of this command-created, nonmetric annotation is safe:
    // preserve only the exact topology-protection constraint targeting it,
    // never use this exception to rebase a measured node/edge/surface.
    RetainedUnknown item;
    const std::string feature_id = deterministic_uuid(command.command_id, "unknown-feature");
    for (const Json &constraint : constraints) {
      const auto targets = json_ids(*member(constraint.object(), "target_ids"));
      if (std::find(targets.begin(), targets.end(), feature_id) == targets.end())
        continue;
      std::string source_command;
      if (targets != std::vector<std::string>{feature_id} ||
          !string_member(constraint.object(), "source_command_id", &source_command))
        return false;
      bool authorized = current.command_id == source_command &&
          current.type == "protect_feature" && current.targets == targets;
      for (const auto &record : journal) {
        if (authorized || record.command_id != source_command ||
            record.command_type != "protect_feature" ||
            record.before_scene_revision == record.after_scene_revision) continue;
        const Json parsed = JsonParser(record.exact_command_utf8).parse();
        CommandData protection;
        authorized = command_base(parsed, &protection) && protection.targets == targets;
      }
      const Json expected_constraint = make_constraint(
          deterministic_uuid(source_command, "topology-protection"),
          "topology_protection", targets, source_command, Json(nullptr),
          "none", true, 0.0, "user_intent");
      if (!authorized || serialize(constraint) != serialize(expected_constraint)) return false;
      if (retained_constraints >= kMaxRetainedUnknowns) {
        *resource_limit = true;
        return false;
      }
      ++retained_constraints;
      item.protection_constraints_json.push_back(serialize(constraint));
    }
    expected_feature.object()["protected_topology"] =
        Json(!item.protection_constraints_json.empty());
    if (serialize(*feature) != serialize(expected_feature) ||
        serialize(*issue) != serialize(expected_issue))
      return false;
    item.feature_json = serialize(*feature);
    std::string *part = &item.issue_before_revision;
    *part = "{";
    bool first = true;
    for (const auto &[key, value] : issue->object()) {
      if (!first) *part += ',';
      first = false;
      *part += serialize(Json(key)) + ':';
      if (key == "scene_revision")
        part = &item.issue_after_revision;
      else
        *part += serialize(value);
    }
    *part += '}';
    retained->push_back(std::move(item));
    return true;
  };
  for (const auto &record : journal) {
    if (record.command_type != "mark_unknown" ||
        record.before_scene_revision == record.after_scene_revision)
      continue;
    const Json original = JsonParser(record.exact_command_utf8).parse();
    CommandData original_data;
    if (!command_base(original, &original_data) || !collect(original_data))
      return false;
  }
  if (!collect(current)) return false;

  // Protection of a source-derived target has no proven identity across a
  // rebuild. Stage a nonmetric unknown marker for that rebuild, not an active
  // constraint rebound to a coincident/new edge. The exact accepted command
  // and result remain in the journal. A pre-rebuild undo removes the active
  // constraint, so no marker is staged. Once exposed, the marker must match
  // its command-owned bytes before a later edit may retain it again.
  const Index constraint_index = index_entities(constraints);
  std::set<std::string> independent_features;
  for (const auto &item : *retained) {
    const Json feature = JsonParser(item.feature_json).parse();
    independent_features.insert(member(feature.object(), "id")->string());
  }
  const auto collect_protection = [&](const CommandData &command) {
    if (command.type != "protect_feature" || command.targets.size() != 1U ||
        independent_features.contains(command.targets.front()))
      return true;
    const std::string constraint_id = deterministic_uuid(command.command_id, "topology-protection");
    const std::string feature_id = deterministic_uuid(command.command_id, "protection-conflict-feature");
    const std::string issue_id = deterministic_uuid(command.command_id, "protection-conflict-issue");
    const Json *constraint = find_entity(constraint_index, constraint_id);
    const Json *feature = find_entity(features, feature_id);
    const Json *issue = find_entity(issues, issue_id);
    if (!constraint && !feature && !issue)
      return true; // Removed by an accepted reversal, not a live intent.
    if (retained->size() >= kMaxRetainedUnknowns) {
      *resource_limit = true;
      return false;
    }
    const Json expected_feature(Object{
        {"attachment", Json("unresolved")}, {"edge_ids", Json(Array{})},
        {"id", Json(feature_id)}, {"kind", Json("unknown")},
        {"protected_topology", Json(false)},
        {"provenance", provenance("unknown", "user", {}, "needs_review")},
        {"surface_ids", Json(Array{})}});
    Json expected_issue = make_issue(issue_id, "HSS_TARGET_LINEAGE_UNRESOLVED",
        {feature_id}, revision, "warning",
        "Accepted user protect_feature command " + command.command_id +
        " for historical target " + command.targets.front() +
        " requires review after source refinement: target lineage is unproven. "
        "No measured target was rebound and no protection is enforced on a replacement.",
        "Inspect the refined geometry and explicitly confirm a current target; "
        "retain this unresolved intent until its lineage is proven.", command.event_sequence_text);
    if (constraint) {
      const Json expected_constraint = make_constraint(constraint_id,
          "topology_protection", command.targets, command.command_id,
          Json(nullptr), "none", true, 0.0, "user_intent");
      if (feature || issue || serialize(*constraint) != serialize(expected_constraint))
        return false;
    } else {
      if (!feature || !issue)
        return false;
      expected_issue.object()["scene_revision"] = *member(issue->object(), "scene_revision");
      if (serialize(*feature) != serialize(expected_feature) ||
          serialize(*issue) != serialize(expected_issue))
        return false;
    }
    RetainedUnknown item;
    item.feature_json = serialize(expected_feature);
    std::string *part = &item.issue_before_revision;
    *part = "{";
    bool first = true;
    for (const auto &[key, value] : expected_issue.object()) {
      if (!first) *part += ',';
      first = false;
      *part += serialize(Json(key)) + ':';
      if (key == "scene_revision") part = &item.issue_after_revision;
      else *part += serialize(value);
    }
    *part += '}';
    retained->push_back(std::move(item));
    return true;
  };
  for (const auto &record : journal) {
    if (record.command_type != "protect_feature" ||
        record.before_scene_revision == record.after_scene_revision)
      continue;
    const Json original = JsonParser(record.exact_command_utf8).parse();
    CommandData original_data;
    if (!command_base(original, &original_data) || !collect_protection(original_data))
      return false;
  }
  if (!collect_protection(current)) return false;
  *output = std::move(retained);
  return true;
}

} // namespace

Processor::Processor(Options options)
    : options_(options),
      gate_(command::Options{options.max_command_bytes,
                             options.max_result_bytes,
                             options.max_records,
                             std::min(options.max_entities,
                                      command::kHardMaxTargets)}) {}

const std::vector<JournalRecord> &Processor::journal() const { return journal_; }

const JournalRecord *
Processor::find_journal_record(std::string_view command_id) const {
  const auto found = std::find_if(
      journal_.begin(), journal_.end(), [&](const JournalRecord &record) {
        return record.command_id == command_id;
      });
  return found == journal_.end() ? nullptr : &*found;
}

const std::vector<DynamicExclusion> &
Processor::pending_dynamic_exclusions() const {
  return dynamic_exclusions_;
}

ProcessResult Processor::process(std::string_view current_scene_json,
                                 std::string_view exact_command_utf8) {
  ProcessResult result;
  if (current_scene_json.empty() ||
      current_scene_json.size() > options_.max_scene_bytes ||
      !valid_utf8(current_scene_json)) {
    result.status = current_scene_json.size() > options_.max_scene_bytes
                        ? Status::kResourceLimitExceeded
                        : Status::kInvalidScene;
    return result;
  }
  if (exact_command_utf8.empty() ||
      exact_command_utf8.size() > options_.max_command_bytes ||
      !valid_utf8(exact_command_utf8)) {
    result.status = exact_command_utf8.size() > options_.max_command_bytes
                        ? Status::kResourceLimitExceeded
                        : Status::kInvalidCommand;
    return result;
  }

  SceneView accepted;
  Json command_json;
  CommandData command_data;
  DynamicExclusion exclusion;
  std::string scene_diagnostic;
  try {
    accepted.root = JsonParser(current_scene_json).parse();
    if (!validate_scene(&accepted, options_, &scene_diagnostic)) {
      result.status = Status::kInvalidScene;
      return result;
    }
    command_json = JsonParser(exact_command_utf8).parse();
    if (!command_base(command_json, &command_data) ||
        !validate_payload(command_data, options_, &exclusion)) {
      result.status = Status::kInvalidCommand;
      return result;
    }
  } catch (const std::bad_alloc &) {
    result.status = Status::kResourceLimitExceeded;
    return result;
  } catch (...) {
    result.status = Status::kInvalidCommand;
    return result;
  }

  command::CommandEnvelope envelope;
  envelope.command_id = command_data.command_id;
  envelope.project_id = command_data.project_id;
  envelope.expected_scene_revision = command_data.expected_revision_text;
  envelope.event_sequence = command_data.event_sequence_text;
  envelope.target_ids = command_data.targets;
  envelope.allow_stale_unchanged_lineage = false;

  command::SceneState gate_accepted = gate_scene(accepted);
  const std::string before_scene = serialize(accepted.root);
  std::string candidate_scene;
  std::shared_ptr<const RetainedUnknowns> retained_unknowns;
  MutationResult mutation;
  bool callback_ran = false;
  bool pending_exclusion = false;
  bool retention_resource_limit = false;

  command::SubmissionResult submitted = gate_.submit(
      envelope, exact_command_utf8, gate_accepted,
      [&](const command::CommandEnvelope &,
          command::SceneState &candidate_gate) -> command::ApplyResult {
        callback_ran = true;
        SceneView candidate;
        candidate.root = accepted.root;
        candidate.object = &candidate.root.object();
        const uint64_t new_revision = accepted.revision + 1U;

        if (command_data.type == "exclude_dynamic_region") {
          pending_exclusion = true;
          mutation = {command::Outcome::kRejected,
                      "dynamic exclusion validated and journaled; source consumer is not installed",
                      {}, {}, {}};
          const std::string exact_result =
              result_json(command_data, "superseded", accepted.revision, {}, {},
                          mutation.diagnostic);
          return {mutation.outcome, exact_result};
        }

        if (command_data.type == "undo" || command_data.type == "redo") {
          std::string original_id;
          string_member(*command_data.payload, "original_command_id", &original_id);
          const JournalRecord *original = find_journal_record(original_id);
          if (original == nullptr || original->before_scene_json.empty() ||
              original->after_scene_json.empty() ||
              original->before_scene_revision == original->after_scene_revision) {
            mutation = {command::Outcome::kRejected,
                        "referenced command is not an accepted reversible mutation",
                        {}, {}, original_id};
          } else {
            bool is_undone = false;
            const JournalRecord *last_reversal = nullptr;
            for (const JournalRecord &record : journal_) {
              if (record.original_command_id != original_id)
                continue;
              if (record.command_type == "undo") {
                is_undone = true;
                last_reversal = &record;
              } else if (record.command_type == "redo") {
                is_undone = false;
                last_reversal = &record;
              }
            }
            const bool wants_undo = command_data.type == "undo";
            if ((wants_undo && is_undone) || (!wants_undo && !is_undone)) {
              mutation = {command::Outcome::kRejected,
                          wants_undo ? "command is already undone"
                                     : "command is not currently undone",
                          {}, {}, original_id};
            } else {
              const std::string &expected_current =
                  wants_undo ? original->after_scene_json
                             : last_reversal->after_scene_json;
              if (!same_scene_payload_except_revision(expected_current,
                                                      accepted.root)) {
                mutation = {command::Outcome::kRejected,
                            "current topology diverged from recorded reversal lineage",
                            {}, {}, original_id};
              } else {
                const std::string &snapshot =
                    wants_undo ? original->before_scene_json
                               : original->after_scene_json;
                try {
                  candidate.root = JsonParser(snapshot).parse();
                  candidate.object = &candidate.root.object();
                  (*candidate.object)["scene_revision"] =
                      Json(std::to_string(new_revision));
                  mutation = {command::Outcome::kApplied,
                              wants_undo
                                  ? "compensating undo appended with original lineage"
                                  : "redo reapplied recorded mutation with verified lineage",
                              {}, {}, original_id};
                } catch (...) {
                  mutation = {command::Outcome::kRejected,
                              "recorded reversal snapshot is invalid", {}, {},
                              original_id};
                }
              }
            }
          }
        } else {
          (*candidate.object)["scene_revision"] =
              Json(std::to_string(new_revision));
          mutation =
              apply_command(*candidate.object, command_data, new_revision, options_);
        }

        if (mutation.outcome == command::Outcome::kApplied) {
          std::string validation_diagnostic;
          if (!recompute_region_areas(*candidate.object, options_) ||
              !validate_scene(&candidate, options_, &validation_diagnostic)) {
            mutation.outcome = command::Outcome::kRejected;
            mutation.diagnostic = validation_diagnostic.empty()
                                      ? "candidate geometry failed ring validation"
                                      : validation_diagnostic;
          }
        }
        if (mutation.outcome == command::Outcome::kApplied &&
            !collect_retained_unknowns(*candidate.object, journal_, command_data,
                                      new_revision, &retained_unknowns,
                                      &retention_resource_limit)) {
          // Invalid apply result prevents the gate from committing any record.
          // Map the known capacity condition below, without inventing an applied result.
          if (retention_resource_limit) return {command::Outcome::kRejected, {}};
          mutation.outcome = command::Outcome::kRejected;
          mutation.diagnostic = "retained unknown annotation limit or ownership conflict";
        }
        uint64_t result_revision = accepted.revision;
        if (mutation.outcome == command::Outcome::kApplied) {
          candidate_scene = serialize(candidate.root);
          candidate_gate = gate_scene(candidate);
          result_revision = new_revision;
        }
        const std::string exact_result = result_json(
            command_data, state_name(mutation.outcome), result_revision,
            mutation.issue_ids, mutation.conflict_ids, mutation.diagnostic);
        return {mutation.outcome, exact_result};
      });

  if (retention_resource_limit && !submitted.outcome) {
    result.status = Status::kResourceLimitExceeded;
    return result;
  }
  result.status = map_gate_status(submitted.status);
  result.command_sha256 = submitted.command_sha256;
  result.exact_result_json = std::move(submitted.result_utf8_bytes);

  if (submitted.status == command::Status::kDuplicateMismatch) {
    result.exact_result_json = result_json(
        command_data, "rejected", accepted.revision, {}, {},
        "command ID was reused with different exact UTF-8 bytes");
    return result;
  }
  if (!callback_ran && submitted.status != command::Status::kStoredRetry) {
    result.exact_result_json = result_json(
        command_data, "rejected", accepted.revision, {}, {},
        std::string("command gate rejected: ") + command::status_name(submitted.status));
    return result;
  }
  if (submitted.status == command::Status::kStoredRetry)
    return result;

  if (callback_ran && submitted.outcome.has_value()) {
    const uint64_t associated_revision =
        *submitted.outcome == command::Outcome::kApplied
            ? accepted.revision + 1U : accepted.revision;
    try {
      JournalRecord record;
      record.ordinal = static_cast<uint64_t>(journal_.size());
      record.command_id = command_data.command_id;
      record.command_type = command_data.type;
      record.original_command_id = mutation.original_command_id;
      record.event_sequence = command_data.event_sequence_text;
      record.exact_command_utf8 = std::string(exact_command_utf8);
      record.command_sha256 = submitted.command_sha256;
      record.exact_result_utf8 = result.exact_result_json;
      record.before_scene_revision = accepted.revision;
      record.after_scene_revision = associated_revision;
      record.before_scene_json = before_scene;
      record.after_scene_json = *submitted.outcome == command::Outcome::kApplied
                                    ? candidate_scene : before_scene;
      // Reserve/copy all fallible output before the no-throw semantic commit.
      if (journal_.size() == journal_.capacity())
        journal_.reserve(std::min(options_.max_records,
            std::max(journal_.size() + 1U, journal_.capacity() * 2U)));
      result.journal_record = record;
      if (pending_exclusion) {
        if (dynamic_exclusions_.size() == dynamic_exclusions_.capacity())
          dynamic_exclusions_.reserve(std::min(options_.max_records,
              std::max(dynamic_exclusions_.size() + 1U,
                       dynamic_exclusions_.capacity() * 2U)));
        result.pending_dynamic_exclusion = exclusion;
      }
      static_assert(std::is_nothrow_move_constructible_v<JournalRecord>);
      static_assert(std::is_nothrow_move_constructible_v<DynamicExclusion>);
      journal_.push_back(std::move(record));
      if (pending_exclusion)
        dynamic_exclusions_.push_back(std::move(exclusion));
      result.owns_new_record = true;
      result.recorded_outcome = submitted.outcome;
      result.associated_scene_revision = associated_revision;
    } catch (const std::bad_alloc &) {
      const bool restored = gate_.rollback_last_record(command_data.command_id,
          submitted.command_sha256, *submitted.outcome, associated_revision);
      result = ProcessResult{};
      result.status = restored ? Status::kResourceLimitExceeded : Status::kInternalError;
      return result;
    } catch (...) {
      (void)gate_.rollback_last_record(command_data.command_id,
          submitted.command_sha256, *submitted.outcome, associated_revision);
      result = ProcessResult{};
      result.status = Status::kInternalError;
      return result;
    }
    if (*submitted.outcome == command::Outcome::kApplied) {
      result.scene_changed = true;
      result.retained_unknowns = std::move(retained_unknowns);
      result.candidate_scene_json = std::move(candidate_scene);
    }
    if (pending_exclusion)
      result.status = Status::kPendingUnsupported;
  }
  return result;
}

bool Processor::rollback_last_process(const ProcessResult &result) noexcept {
  if (!result.owns_new_record || !result.recorded_outcome ||
      !result.journal_record || journal_.empty())
    return false;
  const auto &expected = *result.journal_record;
  const auto &last = journal_.back();
  if (last.ordinal != expected.ordinal || last.command_id != expected.command_id ||
      last.command_sha256 != result.command_sha256 ||
      last.after_scene_revision != result.associated_scene_revision)
    return false;
  if (result.pending_dynamic_exclusion &&
      (dynamic_exclusions_.empty() ||
       dynamic_exclusions_.back().command_id != expected.command_id))
    return false;
  if (!gate_.rollback_last_record(expected.command_id, result.command_sha256,
          *result.recorded_outcome, result.associated_scene_revision))
    return false;
  if (result.pending_dynamic_exclusion)
    dynamic_exclusions_.pop_back();
  journal_.pop_back();
  return true;
}

const char *status_name(Status status) {
  switch (status) {
  case Status::kApplied: return "applied";
  case Status::kStoredRetry: return "stored_retry";
  case Status::kConflict: return "conflict";
  case Status::kRejected: return "rejected";
  case Status::kPendingUnsupported: return "pending_unsupported";
  case Status::kInvalidScene: return "invalid_scene";
  case Status::kInvalidCommand: return "invalid_command";
  case Status::kDuplicateMismatch: return "duplicate_mismatch";
  case Status::kProjectMismatch: return "project_mismatch";
  case Status::kStaleRevision: return "stale_revision";
  case Status::kFutureRevision: return "future_revision";
  case Status::kTargetNotCurrent: return "target_not_current";
  case Status::kTargetLineageChanged: return "target_lineage_changed";
  case Status::kResourceLimitExceeded: return "resource_limit_exceeded";
  case Status::kInternalError: return "internal_error";
  }
  return "unknown";
}

std::optional<SceneCounts> scene_counts(std::string_view canonical_scene_json) {
  try {
    SceneView scene;
    scene.root = JsonParser(canonical_scene_json).parse();
    std::string diagnostic;
    if (!validate_scene(&scene, Options{}, &diagnostic))
      return std::nullopt;
    Array *nodes = nullptr;
    Array *edges = nullptr;
    Array *issues = nullptr;
    if (!scene_array(*scene.object, "nodes", &nodes) ||
        !scene_array(*scene.object, "edges", &edges) ||
        !scene_array(*scene.object, "issues", &issues))
      return std::nullopt;
    return SceneCounts{nodes->size(), edges->size(), issues->size()};
  } catch (...) {
    return std::nullopt;
  }
}

bool distance_constraint_derivative(const std::array<double, 3U> &a,
                                    const std::array<double, 3U> &b,
                                    std::array<double, 6U> *analytic) {
  if (analytic == nullptr)
    return false;
  double norm_squared = 0.0;
  P3 delta{};
  for (size_t i = 0U; i < 3U; ++i) {
    if (!std::isfinite(a[i]) || !std::isfinite(b[i]))
      return false;
    delta[i] = a[i] - b[i];
    norm_squared += delta[i] * delta[i];
  }
  const double norm = std::sqrt(norm_squared);
  if (!std::isfinite(norm) || norm <= 1e-12)
    return false;
  for (size_t i = 0U; i < 3U; ++i) {
    (*analytic)[i] = delta[i] / norm;
    (*analytic)[i + 3U] = -delta[i] / norm;
  }
  return true;
}

} // namespace hss::scene_editing

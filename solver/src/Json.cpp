#include "cfd/Json.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace cfd::json {
namespace {

class Parser {
 public:
  explicit Parser(const std::string& text) : text_(text) {}

  Value parse() {
    Value value = parse_value();
    whitespace();
    if (position_ != text_.size()) fail("unexpected trailing input");
    return value;
  }

 private:
  [[noreturn]] void fail(const std::string& message) const {
    throw Error(message + " at byte " + std::to_string(position_));
  }

  void whitespace() {
    while (position_ < text_.size() &&
           (text_[position_] == ' ' || text_[position_] == '\n' || text_[position_] == '\r' ||
            text_[position_] == '\t')) {
      ++position_;
    }
  }

  bool consume(char expected) {
    whitespace();
    if (position_ < text_.size() && text_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  void expect(char expected) {
    if (!consume(expected)) fail(std::string("expected '") + expected + "'");
  }

  Value parse_value() {
    whitespace();
    if (position_ >= text_.size()) fail("expected a JSON value");
    switch (text_[position_]) {
      case '{':
        return parse_object();
      case '[':
        return parse_array();
      case '"':
        return Value(parse_string());
      case 't':
        literal("true");
        return Value(true);
      case 'f':
        literal("false");
        return Value(false);
      case 'n':
        literal("null");
        return Value(nullptr);
      default:
        if (text_[position_] == '-' || (text_[position_] >= '0' && text_[position_] <= '9')) {
          return Value(parse_number());
        }
        fail("invalid JSON value");
    }
  }

  void literal(const char* token) {
    const std::string value(token);
    if (text_.compare(position_, value.size(), value) != 0) fail("invalid literal");
    position_ += value.size();
  }

  std::string parse_string() {
    expect('"');
    std::string output;
    while (position_ < text_.size()) {
      const char character = text_[position_++];
      if (character == '"') return output;
      if (static_cast<unsigned char>(character) < 0x20) fail("control character in string");
      if (character != '\\') {
        output.push_back(character);
        continue;
      }
      if (position_ >= text_.size()) fail("unterminated escape sequence");
      const char escaped = text_[position_++];
      switch (escaped) {
        case '"': output.push_back('"'); break;
        case '\\': output.push_back('\\'); break;
        case '/': output.push_back('/'); break;
        case 'b': output.push_back('\b'); break;
        case 'f': output.push_back('\f'); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case 'u': {
          if (position_ + 4 > text_.size()) fail("short unicode escape");
          unsigned code = 0;
          for (int i = 0; i < 4; ++i) {
            const char hex = text_[position_++];
            code <<= 4U;
            if (hex >= '0' && hex <= '9') code += static_cast<unsigned>(hex - '0');
            else if (hex >= 'a' && hex <= 'f') code += static_cast<unsigned>(hex - 'a' + 10);
            else if (hex >= 'A' && hex <= 'F') code += static_cast<unsigned>(hex - 'A' + 10);
            else fail("invalid unicode escape");
          }
          if (code <= 0x7f) output.push_back(static_cast<char>(code));
          else if (code <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0U | (code >> 6U)));
            output.push_back(static_cast<char>(0x80U | (code & 0x3fU)));
          } else {
            output.push_back(static_cast<char>(0xe0U | (code >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (code & 0x3fU)));
          }
          break;
        }
        default: fail("invalid escape sequence");
      }
    }
    fail("unterminated string");
  }

  double parse_number() {
    whitespace();
    const char* begin = text_.c_str() + position_;
    char* end = nullptr;
    const double result = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(result)) fail("invalid or non-finite number");
    position_ += static_cast<std::size_t>(end - begin);
    return result;
  }

  Value parse_object() {
    expect('{');
    Value::Object object;
    if (consume('}')) return Value(std::move(object));
    do {
      whitespace();
      if (position_ >= text_.size() || text_[position_] != '"') fail("expected object key");
      std::string key = parse_string();
      expect(':');
      if (!object.emplace(std::move(key), parse_value()).second) fail("duplicate object key");
    } while (consume(','));
    expect('}');
    return Value(std::move(object));
  }

  Value parse_array() {
    expect('[');
    Value::Array array;
    if (consume(']')) return Value(std::move(array));
    do {
      array.push_back(parse_value());
    } while (consume(','));
    expect(']');
    return Value(std::move(array));
  }

  const std::string& text_;
  std::size_t position_ = 0;
};

}  // namespace

std::size_t Value::size() const {
  if (is_object()) return std::get<Object>(data_).size();
  if (is_array()) return std::get<Array>(data_).size();
  throw Error("JSON value has no size");
}

const Value::Object& Value::object() const {
  if (!is_object()) throw Error("JSON value is not an object");
  return std::get<Object>(data_);
}

const Value::Array& Value::array() const {
  if (!is_array()) throw Error("JSON value is not an array");
  return std::get<Array>(data_);
}

const Value& Value::at(const std::string& key) const {
  const auto& values = object();
  auto found = values.find(key);
  if (found == values.end()) throw Error("missing JSON key '" + key + "'");
  return found->second;
}

const Value& Value::operator[](std::size_t index) const { return array().at(index); }

Value Value::parse(const std::string& text) { return Parser(text).parse(); }

template <>
bool Value::get<bool>() const {
  if (!std::holds_alternative<bool>(data_)) throw Error("JSON value is not boolean");
  return std::get<bool>(data_);
}

template <>
int Value::get<int>() const {
  if (!std::holds_alternative<double>(data_)) throw Error("JSON value is not numeric");
  const double value = std::get<double>(data_);
  if (std::floor(value) != value || value < std::numeric_limits<int>::min() ||
      value > std::numeric_limits<int>::max()) {
    throw Error("JSON number is not an integer");
  }
  return static_cast<int>(value);
}

template <>
double Value::get<double>() const {
  if (!std::holds_alternative<double>(data_)) throw Error("JSON value is not numeric");
  return std::get<double>(data_);
}

template <>
std::string Value::get<std::string>() const {
  if (!std::holds_alternative<std::string>(data_)) throw Error("JSON value is not a string");
  return std::get<std::string>(data_);
}

template <>
std::map<std::string, std::string> Value::get<std::map<std::string, std::string>>() const {
  std::map<std::string, std::string> result;
  for (const auto& [key, value] : object()) result.emplace(key, value.get<std::string>());
  return result;
}

}  // namespace cfd::json

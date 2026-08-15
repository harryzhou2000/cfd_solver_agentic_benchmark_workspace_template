#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace cfd::json {

class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class Value {
 public:
  using Object = std::map<std::string, Value>;
  using Array = std::vector<Value>;
  using Storage = std::variant<std::nullptr_t, bool, double, std::string, Object, Array>;

  Value() : data_(nullptr) {}
  explicit Value(Storage data) : data_(std::move(data)) {}

  bool is_null() const { return std::holds_alternative<std::nullptr_t>(data_); }
  bool is_object() const { return std::holds_alternative<Object>(data_); }
  bool is_array() const { return std::holds_alternative<Array>(data_); }
  std::size_t size() const;
  const Object& object() const;
  const Array& array() const;
  const Value& at(const std::string& key) const;
  const Value& operator[](std::size_t index) const;

  template <typename T>
  T get() const;

  static Value parse(const std::string& text);

 private:
  Storage data_;
};

template <>
bool Value::get<bool>() const;
template <>
int Value::get<int>() const;
template <>
double Value::get<double>() const;
template <>
std::string Value::get<std::string>() const;
template <>
std::map<std::string, std::string> Value::get<std::map<std::string, std::string>>() const;

}  // namespace cfd::json

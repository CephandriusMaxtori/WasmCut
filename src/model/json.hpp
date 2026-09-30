#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace wasmcut::json {

// A deliberately small JSON value. Wasmcut has no third-party dependencies, and
// project files only ever use objects, arrays, strings, numbers and booleans.
class Value {
public:
  enum class Kind {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
  };

  Value() = default;
  Value(bool value);
  Value(double value);
  Value(int value);
  Value(long long value);
  Value(unsigned long long value);
  Value(std::string value);
  Value(const char* value);

  [[nodiscard]] static Value array();
  [[nodiscard]] static Value object();

  [[nodiscard]] Kind kind() const noexcept {
    return kind_;
  }
  [[nodiscard]] bool is_null() const noexcept {
    return kind_ == Kind::Null;
  }
  [[nodiscard]] bool is_object() const noexcept {
    return kind_ == Kind::Object;
  }
  [[nodiscard]] bool is_array() const noexcept {
    return kind_ == Kind::Array;
  }
  [[nodiscard]] bool is_number() const noexcept {
    return kind_ == Kind::Number;
  }
  [[nodiscard]] bool is_string() const noexcept {
    return kind_ == Kind::String;
  }

  [[nodiscard]] bool as_bool(bool fallback = false) const noexcept;
  [[nodiscard]] double as_number(double fallback = 0.0) const noexcept;
  [[nodiscard]] long long as_integer(long long fallback = 0) const noexcept;
  [[nodiscard]] std::string as_string(std::string fallback = {}) const;

  [[nodiscard]] const Value* find(std::string_view key) const noexcept;
  [[nodiscard]] const std::vector<Value>& items() const noexcept;
  [[nodiscard]] const std::vector<std::pair<std::string, Value>>& members() const noexcept;

  void set(std::string key, Value value);
  void push_back(Value value);

  [[nodiscard]] std::string dump() const;

private:
  Kind kind_ = Kind::Null;
  bool boolean_ = false;
  double number_ = 0.0;
  std::string string_;
  std::vector<Value> array_;
  std::vector<std::pair<std::string, Value>> object_;
};

// Parses a JSON document. Returns false and fills `error` when the text is not
// valid JSON or when trailing garbage follows the value.
[[nodiscard]] bool parse(std::string_view text, Value& out, std::string& error);

// Escapes a string as a JSON string literal, including the surrounding quotes.
[[nodiscard]] std::string quote(std::string_view value);

// Formats a double without a trailing ".000000" style artefact, keeping enough
// precision for media timings.
[[nodiscard]] std::string number(double value);

}

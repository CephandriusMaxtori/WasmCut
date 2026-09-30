#include "model/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace wasmcut::json {
namespace {

const std::vector<Value>& empty_items() {
  static const std::vector<Value> value;
  return value;
}

const std::vector<std::pair<std::string, Value>>& empty_members() {
  static const std::vector<std::pair<std::string, Value>> value;
  return value;
}

void append_escaped(std::string& out, std::string_view value) {
  out += '"';
  for (const char raw : value) {
    const auto character = static_cast<unsigned char>(raw);
    switch (character) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (character < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", character);
          out += buffer;
        } else {
          out += raw;
        }
        break;
    }
  }
  out += '"';
}

void dump_into(const Value& value, std::string& out) {
  switch (value.kind()) {
    case Value::Kind::Null:
      out += "null";
      break;
    case Value::Kind::Boolean:
      out += value.as_bool() ? "true" : "false";
      break;
    case Value::Kind::Number:
      out += number(value.as_number());
      break;
    case Value::Kind::String:
      append_escaped(out, value.as_string());
      break;
    case Value::Kind::Array: {
      out += '[';
      bool first = true;
      for (const Value& item : value.items()) {
        if (!first) {
          out += ',';
        }
        first = false;
        dump_into(item, out);
      }
      out += ']';
      break;
    }
    case Value::Kind::Object: {
      // Object members keep insertion order, which makes output stable.
      out += '{';
      bool first = true;
      for (const auto& [name, value] : value.members()) {
        if (!first) {
          out += ',';
        }
        first = false;
        append_escaped(out, name);
        out += ':';
        dump_into(value, out);
      }
      out += '}';
      break;
    }
  }
}

}

Value::Value(bool value)
    : kind_(Kind::Boolean), boolean_(value) {}

Value::Value(double value)
    : kind_(Kind::Number), number_(value) {}

Value::Value(int value)
    : kind_(Kind::Number), number_(static_cast<double>(value)) {}

Value::Value(long long value)
    : kind_(Kind::Number), number_(static_cast<double>(value)) {}

Value::Value(unsigned long long value)
    : kind_(Kind::Number), number_(static_cast<double>(value)) {}

Value::Value(std::string value)
    : kind_(Kind::String), string_(std::move(value)) {}

Value::Value(const char* value)
    : kind_(Kind::String), string_(value != nullptr ? value : "") {}

Value Value::array() {
  Value result;
  result.kind_ = Kind::Array;
  return result;
}

Value Value::object() {
  Value result;
  result.kind_ = Kind::Object;
  return result;
}

bool Value::as_bool(bool fallback) const noexcept {
  if (kind_ == Kind::Boolean) {
    return boolean_;
  }
  if (kind_ == Kind::Number) {
    return number_ != 0.0;
  }
  return fallback;
}

double Value::as_number(double fallback) const noexcept {
  return kind_ == Kind::Number ? number_ : fallback;
}

long long Value::as_integer(long long fallback) const noexcept {
  if (kind_ != Kind::Number || !std::isfinite(number_)) {
    return fallback;
  }
  return static_cast<long long>(number_);
}

std::string Value::as_string(std::string fallback) const {
  return kind_ == Kind::String ? string_ : std::move(fallback);
}

const Value* Value::find(std::string_view key) const noexcept {
  if (kind_ != Kind::Object) {
    return nullptr;
  }
  for (const auto& [name, value] : object_) {
    if (name == key) {
      return &value;
    }
  }
  return nullptr;
}

const std::vector<Value>& Value::items() const noexcept {
  return kind_ == Kind::Array ? array_ : empty_items();
}

const std::vector<std::pair<std::string, Value>>& Value::members() const noexcept {
  return kind_ == Kind::Object ? object_ : empty_members();
}

void Value::set(std::string key, Value value) {
  kind_ = Kind::Object;
  for (auto& [name, existing] : object_) {
    if (name == key) {
      existing = std::move(value);
      return;
    }
  }
  object_.emplace_back(std::move(key), std::move(value));
}

void Value::push_back(Value value) {
  kind_ = Kind::Array;
  array_.push_back(std::move(value));
}

std::string Value::dump() const {
  std::string out;
  dump_into(*this, out);
  return out;
}

std::string quote(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 2);
  append_escaped(out, value);
  return out;
}

std::string number(double value) {
  if (!std::isfinite(value)) {
    return "0";
  }
  if (value == static_cast<double>(static_cast<long long>(value)) && std::fabs(value) < 1e15) {
    return std::to_string(static_cast<long long>(value));
  }
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%.10g", value);
  return buffer;
}

namespace {

class Parser {
public:
  Parser(std::string_view text, std::string& error)
      : text_(text), error_(error) {}

  bool run(Value& out) {
    skip_whitespace();
    if (!parse_value(out)) {
      return false;
    }
    skip_whitespace();
    if (position_ != text_.size()) {
      return fail("unexpected trailing characters");
    }
    return true;
  }

private:
  bool fail(const char* message) {
    if (error_.empty()) {
      error_ = std::string(message) + " at offset " + std::to_string(position_);
    }
    return false;
  }

  void skip_whitespace() {
    while (position_ < text_.size()) {
      const char character = text_[position_];
      if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
        ++position_;
      } else {
        break;
      }
    }
  }

  bool consume(char expected) {
    if (position_ < text_.size() && text_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  bool parse_value(Value& out) {
    if (position_ >= text_.size()) {
      return fail("unexpected end of input");
    }
    switch (text_[position_]) {
      case '{':
        return parse_object(out);
      case '[':
        return parse_array(out);
      case '"': {
        std::string value;
        if (!parse_string(value)) {
          return false;
        }
        out = Value(std::move(value));
        return true;
      }
      case 't':
        return parse_literal("true", Value(true), out);
      case 'f':
        return parse_literal("false", Value(false), out);
      case 'n':
        return parse_literal("null", Value(), out);
      default:
        return parse_number(out);
    }
  }

  bool parse_literal(std::string_view literal, Value value, Value& out) {
    if (text_.compare(position_, literal.size(), literal) != 0) {
      return fail("invalid literal");
    }
    position_ += literal.size();
    out = std::move(value);
    return true;
  }

  bool parse_number(Value& out) {
    const std::size_t start = position_;
    if (position_ < text_.size() && (text_[position_] == '-' || text_[position_] == '+')) {
      ++position_;
    }
    while (position_ < text_.size()) {
      const char character = text_[position_];
      if ((character >= '0' && character <= '9') || character == '.' || character == 'e' || character == 'E' ||
          character == '-' || character == '+') {
        ++position_;
      } else {
        break;
      }
    }
    if (start == position_) {
      return fail("expected a value");
    }
    const std::string token(text_.substr(start, position_ - start));
    char* end = nullptr;
    const double parsed = std::strtod(token.c_str(), &end);
    if (end == nullptr || *end != '\0') {
      position_ = start;
      return fail("malformed number");
    }
    out = Value(parsed);
    return true;
  }

  bool parse_string(std::string& out) {
    if (!consume('"')) {
      return fail("expected a string");
    }
    out.clear();
    while (position_ < text_.size()) {
      const char character = text_[position_++];
      if (character == '"') {
        return true;
      }
      if (character != '\\') {
        out += character;
        continue;
      }
      if (position_ >= text_.size()) {
        return fail("unterminated escape");
      }
      const char escape = text_[position_++];
      switch (escape) {
        case '"':
          out += '"';
          break;
        case '\\':
          out += '\\';
          break;
        case '/':
          out += '/';
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case 'u': {
          if (position_ + 4 > text_.size()) {
            return fail("truncated unicode escape");
          }
          unsigned int code = 0;
          for (int index = 0; index < 4; ++index) {
            const char digit = text_[position_++];
            code *= 16;
            if (digit >= '0' && digit <= '9') {
              code += static_cast<unsigned int>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
              code += static_cast<unsigned int>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
              code += static_cast<unsigned int>(digit - 'A' + 10);
            } else {
              return fail("invalid unicode escape");
            }
          }
          // Encode as UTF-8. Surrogate pairs are not recombined, which is
          // acceptable for project names produced by this editor.
          if (code < 0x80) {
            out += static_cast<char>(code);
          } else if (code < 0x800) {
            out += static_cast<char>(0xc0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3f));
          } else {
            out += static_cast<char>(0xe0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
          }
          break;
        }
        default:
          return fail("unknown escape");
      }
    }
    return fail("unterminated string");
  }

  bool parse_array(Value& out) {
    if (!consume('[')) {
      return fail("expected an array");
    }
    skip_whitespace();
    if (consume(']')) {
      out = Value::array();
      return true;
    }
    Value array;
    while (true) {
      skip_whitespace();
      Value item;
      if (!parse_value(item)) {
        return false;
      }
      array.push_back(std::move(item));
      skip_whitespace();
      if (consume(',')) {
        continue;
      }
      if (consume(']')) {
        out = std::move(array);
        return true;
      }
      return fail("expected ',' or ']'");
    }
  }

  bool parse_object(Value& out) {
    if (!consume('{')) {
      return fail("expected an object");
    }
    skip_whitespace();
    Value object = Value::object();
    if (consume('}')) {
      out = std::move(object);
      return true;
    }
    while (true) {
      skip_whitespace();
      std::string key;
      if (!parse_string(key)) {
        return false;
      }
      skip_whitespace();
      if (!consume(':')) {
        return fail("expected ':'");
      }
      skip_whitespace();
      Value item;
      if (!parse_value(item)) {
        return false;
      }
      object.set(std::move(key), std::move(item));
      skip_whitespace();
      if (consume(',')) {
        continue;
      }
      if (consume('}')) {
        out = std::move(object);
        return true;
      }
      return fail("expected ',' or '}'");
    }
  }

  std::string_view text_;
  std::string& error_;
  std::size_t position_ = 0;
};

}

bool parse(std::string_view text, Value& out, std::string& error) {
  error.clear();
  Parser parser(text, error);
  return parser.run(out);
}

}

// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_TYPE_PARSER_H_
#define V8_COMPILER_TURBOSHAFT_TYPE_PARSER_H_

#include <optional>

#include "src/compiler/turboshaft/types.h"

namespace v8::internal::compiler::turboshaft {

// TypeParser is used to construct a Type from a string literal.
// It's primary use is the %CheckTurboshaftTypeOf intrinsic, which allows
// mjsunit tests to check the static type of expressions. Typically the string
// has to have the format that Type::ToString() would produce.
//
// Examples: "Word32", "Word64[30, 100]", "Float32{-1.02}", "Float64{3.2, 17.8}"
class TypeParser {
 public:
  explicit TypeParser(const std::string_view& str, Zone* zone)
      : str_(str), zone_(zone) {}

  std::optional<Type> Parse() {
    std::optional<Type> type = ParseType();
    // Skip trailing whitespace.
    while (pos_ < str_.length() && str_[pos_] == ' ') ++pos_;
    if (pos_ < str_.length()) return std::nullopt;
    return type;
  }

 private:
  std::optional<Type> ParseType();

  template <typename T>
  std::optional<T> ParseRange() {
    if (!ConsumeIf("[")) return std::nullopt;
    auto from = ReadValue<typename T::value_type>();
    if (!from) return std::nullopt;
    if (!ConsumeIf(",")) return std::nullopt;
    auto to = ReadValue<typename T::value_type>();
    if (!to) return std::nullopt;
    if (!ConsumeIf("]")) return std::nullopt;
    if constexpr (!std::is_same_v<T, Word32Type> &&
                  !std::is_same_v<T, Word64Type>) {
      CHECK_LE(*from, *to);
    }
    return T::Range(*from, *to, zone_);
  }

  template <typename T>
  std::optional<T> ParseSet() {
    if (!ConsumeIf("{")) return std::nullopt;
    auto elements = ParseSetElements<typename T::value_type>();
    if (!elements) return std::nullopt;
    if (!ConsumeIf("}")) return std::nullopt;
    CHECK_LT(0, elements->size());
    CHECK_LE(elements->size(), T::kMaxSetSize);
    return T::Set(*elements, zone_);
  }

  template <typename T>
  std::optional<std::vector<T>> ParseSetElements() {
    std::vector<T> elements;
    if (IsNext("}")) return elements;
    while (true) {
      auto element_opt = ReadValue<T>();
      if (!element_opt) return std::nullopt;
      elements.push_back(*element_opt);

      if (IsNext("}")) break;
      if (!ConsumeIf(",")) return std::nullopt;
    }
    base::sort(elements);
    elements.erase(std::unique(elements.begin(), elements.end()),
                   elements.end());
    return elements;
  }

  bool ConsumeIf(const std::string_view& prefix) {
    if (IsNext(prefix)) {
      pos_ += prefix.length();
      return true;
    }
    return false;
  }

  bool IsNext(const std::string_view& prefix) {
    // Skip leading whitespace.
    while (pos_ < str_.length() && str_[pos_] == ' ') ++pos_;
    if (pos_ >= str_.length()) return false;
    size_t remaining_length = str_.length() - pos_;
    if (prefix.length() > remaining_length) return false;
    return str_.compare(pos_, prefix.length(), prefix, 0, prefix.length()) == 0;
  }

  template <typename T>
  std::optional<T> ReadValue() {
    T result;
    size_t read = 0;
    // TODO(nicohartmann@): Ideally we want to avoid this string construction
    // (e.g. using std::from_chars).
    std::string s(str_.cbegin() + pos_, str_.cend());
    if constexpr (std::is_same_v<T, uint32_t>) {
      result = static_cast<uint32_t>(std::stoul(s, &read));
    } else if constexpr (std::is_same_v<T, uint64_t>) {
      result = std::stoull(s, &read);
    } else if constexpr (std::is_same_v<T, float>) {
      result = std::stof(s, &read);
    } else if constexpr (std::is_same_v<T, double>) {
      result = std::stod(s, &read);
    }
    if (read == 0) return std::nullopt;
    pos_ += read;
    return result;
  }

  std::string_view str_;
  Zone* zone_;
  size_t pos_ = 0;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_TYPE_PARSER_H_

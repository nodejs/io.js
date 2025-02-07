// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/ls/json-parser.h"

#include <cctype>
#include <optional>

#include "src/torque/earley-parser.h"

namespace v8::internal::torque {

template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<ls::JsonValue>::id =
    ParseResultTypeId::kJsonValue;

template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::pair<std::string, ls::JsonValue>>::id =
        ParseResultTypeId::kJsonMember;

template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<ls::JsonValue>>::id =
        ParseResultTypeId::kStdVectorOfJsonValue;

template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<std::pair<std::string, ls::JsonValue>>>::id =
        ParseResultTypeId::kStdVectorOfJsonMember;

namespace ls {

using JsonMember = std::pair<std::string, JsonValue>;

template <bool value>
std::optional<ParseResult> MakeBoolLiteral(ParseResultIterator* child_results) {
  return ParseResult{JsonValue::From(value)};
}

std::optional<ParseResult> MakeNullLiteral(ParseResultIterator* child_results) {
  JsonValue result;
  result.tag = JsonValue::IS_NULL;
  return ParseResult{std::move(result)};
}

std::optional<ParseResult> MakeNumberLiteral(
    ParseResultIterator* child_results) {
  auto number = child_results->NextAs<std::string>();
  double d = std::stod(number.c_str());
  return ParseResult{JsonValue::From(d)};
}

std::optional<ParseResult> MakeStringLiteral(
    ParseResultIterator* child_results) {
  std::string literal = child_results->NextAs<std::string>();
  return ParseResult{JsonValue::From(StringLiteralUnquote(literal))};
}

std::optional<ParseResult> MakeArray(ParseResultIterator* child_results) {
  JsonArray array = child_results->NextAs<JsonArray>();
  return ParseResult{JsonValue::From(std::move(array))};
}

std::optional<ParseResult> MakeMember(ParseResultIterator* child_results) {
  JsonMember result;
  std::string key = child_results->NextAs<std::string>();
  result.first = StringLiteralUnquote(key);
  result.second = child_results->NextAs<JsonValue>();
  return ParseResult{std::move(result)};
}

std::optional<ParseResult> MakeObject(ParseResultIterator* child_results) {
  using MemberList = std::vector<JsonMember>;
  MemberList members = child_results->NextAs<MemberList>();

  JsonObject object;
  for (auto& member : members) object.insert(std::move(member));

  return ParseResult{JsonValue::From(std::move(object))};
}

class JsonGrammar : public Grammar {
  static bool MatchWhitespace(InputPosition* pos) {
    while (MatchChar(std::isspace, pos)) {
    }
    return true;
  }

  static bool MatchStringLiteral(InputPosition* pos) {
    InputPosition current = *pos;
    if (MatchString("\"", &current)) {
      while (
          (MatchString("\\", &current) && MatchAnyChar(&current)) ||
          MatchChar([](char c) { return c != '"' && c != '\n'; }, &current)) {
      }
      if (MatchString("\"", &current)) {
        *pos = current;
        return true;
      }
    }
    current = *pos;
    if (MatchString("'", &current)) {
      while (
          (MatchString("\\", &current) && MatchAnyChar(&current)) ||
          MatchChar([](char c) { return c != '\'' && c != '\n'; }, &current)) {
      }
      if (MatchString("'", &current)) {
        *pos = current;
        return true;
      }
    }
    return false;
  }

  static bool MatchHexLiteral(InputPosition* pos) {
    InputPosition current = *pos;
    MatchString("-", &current);
    if (MatchString("0x", &current) && MatchChar(std::isxdigit, &current)) {
      while (MatchChar(std::isxdigit, &current)) {
      }
      *pos = current;
      return true;
    }
    return false;
  }

  static bool MatchDecimalLiteral(InputPosition* pos) {
    InputPosition current = *pos;
    bool found_digit = false;
    MatchString("-", &current);
    while (MatchChar(std::isdigit, &current)) found_digit = true;
    MatchString(".", &current);
    while (MatchChar(std::isdigit, &current)) found_digit = true;
    if (!found_digit) return false;
    *pos = current;
    if ((MatchString("e", &current) || MatchString("E", &current)) &&
        (MatchString("+", &current) || MatchString("-", &current) || true) &&
        MatchChar(std::isdigit, &current)) {
      while (MatchChar(std::isdigit, &current)) {
      }
      *pos = current;
      return true;
    }
    return true;
  }

 public:
  JsonGrammar() : Grammar(&file) { SetWhitespace(MatchWhitespace); }

  Symbol trueLiteral = {Rule({Token("true")})};
  Symbol falseLiteral = {Rule({Token("false")})};
  Symbol nullLiteral = {Rule({Token("null")})};

  Symbol decimalLiteral = {
      Rule({Pattern(MatchDecimalLiteral)}, YieldMatchedInput),
      Rule({Pattern(MatchHexLiteral)}, YieldMatchedInput)};

  Symbol stringLiteral = {
      Rule({Pattern(MatchStringLiteral)}, YieldMatchedInput)};

  Symbol* elementList = List<JsonValue>(&value, Token(","));
  Symbol array = {Rule({Token("["), elementList, Token("]")})};

  Symbol member = {Rule({&stringLiteral, Token(":"), &value}, MakeMember)};
  Symbol* memberList = List<JsonMember>(&member, Token(","));
  Symbol object = {Rule({Token("{"), memberList, Token("}")})};

  Symbol value = {Rule({&trueLiteral}, MakeBoolLiteral<true>),
                  Rule({&falseLiteral}, MakeBoolLiteral<false>),
                  Rule({&nullLiteral}, MakeNullLiteral),
                  Rule({&decimalLiteral}, MakeNumberLiteral),
                  Rule({&stringLiteral}, MakeStringLiteral),
                  Rule({&object}, MakeObject),
                  Rule({&array}, MakeArray)};

  Symbol file = {Rule({&value})};
};

JsonParserResult ParseJson(const std::string& input) {
  // Torque needs a CurrentSourceFile scope during parsing.
  // As JSON lives in memory only, an unknown file scope is created.
  SourceFileMap::Scope source_map_scope("");
  TorqueMessages::Scope messages_scope;
  CurrentSourceFile::Scope unkown_file(SourceFileMap::AddSource("<json>"));

  JsonParserResult result;
  try {
    result.value = (*JsonGrammar().Parse(input)).Cast<JsonValue>();
  } catch (TorqueAbortCompilation&) {
    CHECK(!TorqueMessages::Get().empty());
    result.error = TorqueMessages::Get().front();
  }
  return result;
}

}  // namespace ls
}  // namespace v8::internal::torque

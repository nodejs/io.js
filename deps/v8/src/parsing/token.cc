// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "src/parsing/token.h"

namespace v8 {
namespace internal {

#define T(name, string, precedence) #name,
const char* const Token::name_[kNumTokens] = {TOKEN_LIST(T, T)};
#undef T


#define T(name, string, precedence) string,
const char* const Token::string_[kNumTokens] = {TOKEN_LIST(T, T)};
#undef T

constexpr uint8_t length(const char* str) {
  return str ? static_cast<uint8_t>(strlen(str)) : 0;
}
#define T(name, string, precedence) length(string),
const uint8_t Token::string_length_[kNumTokens] = {TOKEN_LIST(T, T)};
#undef T

#define T1(name, string, precedence) \
  ((Token::name == Token::kIn) ? 0 : precedence),
#define T2(name, string, precedence) precedence,
// precedence_[0] for accept_IN == false, precedence_[1] for accept_IN = true.
const int8_t Token::precedence_[2][kNumTokens] = {{TOKEN_LIST(T1, T1)},
                                                  {TOKEN_LIST(T2, T2)}};
#undef T2
#undef T1

#define KT(a, b, c) \
  IsPropertyNameBits::encode(Token::IsAnyIdentifier(a) || a == kEscapedKeyword),
#define KK(a, b, c) \
  IsKeywordBits::encode(true) | IsPropertyNameBits::encode(true),
const uint8_t Token::token_flags[] = {TOKEN_LIST(KT, KK)};
#undef KT
#undef KK

}  // namespace internal
}  // namespace v8

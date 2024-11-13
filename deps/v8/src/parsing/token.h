// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_TOKEN_H_
#define V8_PARSING_TOKEN_H_

#include "src/base/bit-field.h"
#include "src/base/bounds.h"
#include "src/base/logging.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

// TOKEN_LIST takes a list of 3 macros M, all of which satisfy the
// same signature M(name, string, precedence), where name is the
// symbolic token name, string is the corresponding syntactic symbol
// (or nullptr, for literals), and precedence is the precedence (or 0).
// The parameters are invoked for token categories as follows:
//
//   T: Non-keyword tokens
//   K: Keyword tokens

// IGNORE_TOKEN is a convenience macro that can be supplied as
// an argument (at any position) for a TOKEN_LIST call. It does
// nothing with tokens belonging to the respective category.

#define IGNORE_TOKEN(name, string, precedence)

/* Binary operators */
/* kAdd and kSub are at the end since they are UnaryOp */
#define BINARY_OP_TOKEN_LIST(T, E) \
  E(T, Nullish, "??", 3)           \
  E(T, Or, "||", 4)                \
  E(T, And, "&&", 5)               \
  E(T, BitOr, "|", 6)              \
  E(T, BitXor, "^", 7)             \
  E(T, BitAnd, "&", 8)             \
  E(T, Shl, "<<", 11)              \
  E(T, Sar, ">>", 11)              \
  E(T, Shr, ">>>", 11)             \
  E(T, Mul, "*", 13)               \
  E(T, Div, "/", 13)               \
  E(T, Mod, "%", 13)               \
  E(T, Exp, "**", 14)              \
  E(T, Add, "+", 12)               \
  E(T, Sub, "-", 12)

#define EXPAND_BINOP_ASSIGN_TOKEN(T, name, string, precedence) \
  T(kAssign##name, string "=", 2)

#define EXPAND_BINOP_TOKEN(T, name, string, precedence) \
  T(k##name, string, precedence)

#define TOKEN_LIST(T, K)                                                      \
                                                                              \
  /* BEGIN PropertyOrCall */                                                  \
  /* BEGIN Member */                                                          \
  /* BEGIN Template */                                                        \
  /* ES6 Template Literals */                                                 \
  T(kTemplateSpan, nullptr, 0)                                                \
  T(kTemplateTail, nullptr, 0)                                                \
  /* END Template */                                                          \
                                                                              \
  /* Punctuators (ECMA-262, section 7.7, page 15). */                         \
  /* BEGIN Property */                                                        \
  T(kPeriod, ".", 0)                                                          \
  T(kLeftBracket, "[", 0)                                                     \
  /* END Property */                                                          \
  /* END Member */                                                            \
  T(kQuestionPeriod, "?.", 0)                                                 \
  T(kLeftParen, "(", 0)                                                       \
  /* END PropertyOrCall */                                                    \
  T(kRightParen, ")", 0)                                                      \
  T(kRightBracket, "]", 0)                                                    \
  T(kLeftBrace, "{", 0)                                                       \
  T(kColon, ":", 0)                                                           \
  T(kEllipsis, "...", 0)                                                      \
  T(kConditional, "?", 3)                                                     \
  /* BEGIN AutoSemicolon */                                                   \
  T(kSemicolon, ";", 0)                                                       \
  T(kRightBrace, "}", 0)                                                      \
  /* End of source indicator. */                                              \
  T(kEos, "EOS", 0)                                                           \
  /* END AutoSemicolon */                                                     \
                                                                              \
  /* BEGIN ArrowOrAssignmentOp */                                             \
  T(kArrow, "=>", 0)                                                          \
  /* BEGIN AssignmentOp */                                                    \
  /* IsAssignmentOp() relies on this block of enum values being */            \
  /* contiguous and sorted in the same order! */                              \
  T(kInit, "=init", 2) /* AST-use only. */                                    \
  T(kAssign, "=", 2)                                                          \
  BINARY_OP_TOKEN_LIST(T, EXPAND_BINOP_ASSIGN_TOKEN)                          \
  /* END AssignmentOp */                                                      \
  /* END ArrowOrAssignmentOp */                                               \
                                                                              \
  /* Binary operators sorted by precedence. */                                \
  /* IsBinaryOp() relies on this block of enum values */                      \
  /* being contiguous and sorted in the same order! */                        \
  T(kComma, ",", 1)                                                           \
                                                                              \
  /* Unary operators, starting at kAdd in BINARY_OP_TOKEN_LIST  */            \
  /* IsUnaryOp() relies on this block of enum values */                       \
  /* being contiguous and sorted in the same order! */                        \
  BINARY_OP_TOKEN_LIST(T, EXPAND_BINOP_TOKEN)                                 \
                                                                              \
  T(kNot, "!", 0)                                                             \
  T(kBitNot, "~", 0)                                                          \
  K(kDelete, "delete", 0)                                                     \
  K(kTypeOf, "typeof", 0)                                                     \
  K(kVoid, "void", 0)                                                         \
                                                                              \
  /* BEGIN IsCountOp */                                                       \
  T(kInc, "++", 0)                                                            \
  T(kDec, "--", 0)                                                            \
  /* END IsCountOp */                                                         \
  /* END IsUnaryOrCountOp */                                                  \
                                                                              \
  /* Compare operators sorted by precedence. */                               \
  /* IsCompareOp() relies on this block of enum values */                     \
  /* being contiguous and sorted in the same order! */                        \
  T(kEq, "==", 9)                                                             \
  T(kEqStrict, "===", 9)                                                      \
  T(kNotEq, "!=", 9)                                                          \
  T(kNotEqStrict, "!==", 9)                                                   \
  T(kLessThan, "<", 10)                                                       \
  T(kGreaterThan, ">", 10)                                                    \
  T(kLessThanEq, "<=", 10)                                                    \
  T(kGreaterThanEq, ">=", 10)                                                 \
  K(kInstanceOf, "instanceof", 10)                                            \
  K(kIn, "in", 10)                                                            \
                                                                              \
  /* Keywords (ECMA-262, section 7.5.2, page 13). */                          \
  K(kBreak, "break", 0)                                                       \
  K(kCase, "case", 0)                                                         \
  K(kCatch, "catch", 0)                                                       \
  K(kContinue, "continue", 0)                                                 \
  K(kDebugger, "debugger", 0)                                                 \
  K(kDefault, "default", 0)                                                   \
  /* kDelete */                                                               \
  K(kDo, "do", 0)                                                             \
  K(kElse, "else", 0)                                                         \
  K(kFinally, "finally", 0)                                                   \
  K(kFor, "for", 0)                                                           \
  K(kFunction, "function", 0)                                                 \
  K(kIf, "if", 0)                                                             \
  /* kIn */                                                                   \
  /* kInstanceOf */                                                           \
  K(kNew, "new", 0)                                                           \
  K(kReturn, "return", 0)                                                     \
  K(kSwitch, "switch", 0)                                                     \
  K(kThrow, "throw", 0)                                                       \
  K(kTry, "try", 0)                                                           \
  /* kTypeOf */                                                               \
  K(kVar, "var", 0)                                                           \
  /* kVoid */                                                                 \
  K(kWhile, "while", 0)                                                       \
  K(kWith, "with", 0)                                                         \
  K(kThis, "this", 0)                                                         \
                                                                              \
  /* Literals (ECMA-262, section 7.8, page 16). */                            \
  K(kNullLiteral, "null", 0)                                                  \
  K(kTrueLiteral, "true", 0)                                                  \
  K(kFalseLiteral, "false", 0)                                                \
  T(kNumber, nullptr, 0)                                                      \
  T(kSmi, nullptr, 0)                                                         \
  T(kBigInt, nullptr, 0)                                                      \
  T(kString, nullptr, 0)                                                      \
                                                                              \
  /* BEGIN Callable */                                                        \
  K(kSuper, "super", 0)                                                       \
  /* BEGIN AnyIdentifier */                                                   \
  /* Identifiers (not keywords or future reserved words). */                  \
  /* TODO(rezvan): Add remaining contextual keywords (meta, target, as, from) \
   * to tokens. */                                                            \
  T(kIdentifier, nullptr, 0)                                                  \
  K(kGet, "get", 0)                                                           \
  K(kSet, "set", 0)                                                           \
  K(kUsing, "using", 0)                                                       \
  K(kOf, "of", 0)                                                             \
  K(kAccessor, "accessor", 0)                                                 \
  K(kAsync, "async", 0)                                                       \
  /* `await` is a reserved word in module code only */                        \
  K(kAwait, "await", 0)                                                       \
  K(kYield, "yield", 0)                                                       \
  K(kLet, "let", 0)                                                           \
  K(kStatic, "static", 0)                                                     \
  /* Future reserved words (ECMA-262, section 7.6.1.2). */                    \
  T(kFutureStrictReservedWord, nullptr, 0)                                    \
  T(kEscapedStrictReservedWord, nullptr, 0)                                   \
  /* END AnyIdentifier */                                                     \
  /* END Callable */                                                          \
  K(kEnum, "enum", 0)                                                         \
  K(kClass, "class", 0)                                                       \
  K(kConst, "const", 0)                                                       \
  K(kExport, "export", 0)                                                     \
  K(kExtends, "extends", 0)                                                   \
  K(kImport, "import", 0)                                                     \
  T(kPrivateName, nullptr, 0)                                                 \
                                                                              \
  /* Illegal token - not able to scan. */                                     \
  T(kIllegal, "ILLEGAL", 0)                                                   \
  T(kEscapedKeyword, nullptr, 0)                                              \
                                                                              \
  /* Scanner-internal use only. */                                            \
  T(kWhitespace, nullptr, 0)                                                  \
  T(kUninitialized, nullptr, 0)                                               \
  T(kRegExpLiteral, nullptr, 0)

class V8_EXPORT_PRIVATE Token {
 public:
  // All token values.
#define T(name, string, precedence) name,
  enum Value : uint8_t { TOKEN_LIST(T, T) kNumTokens };
#undef T

  // Returns a string corresponding to the C++ token name
  // (e.g. "kLessThan" for the token kLessThan).
  static const char* Name(Value token) {
    DCHECK_GT(kNumTokens, token);  // token is unsigned
    return name_[token];
  }

  using IsKeywordBits = base::BitField8<bool, 0, 1>;
  using IsPropertyNameBits = IsKeywordBits::Next<bool, 1>;

  // Predicates
  static bool IsKeyword(Value token) {
    return IsKeywordBits::decode(token_flags[token]);
  }

  static bool IsPropertyName(Value token) {
    return IsPropertyNameBits::decode(token_flags[token]);
  }

  V8_INLINE static bool IsValidIdentifier(Value token,
                                          LanguageMode language_mode,
                                          bool is_generator,
                                          bool disallow_await) {
    if (V8_LIKELY(base::IsInRange(token, kIdentifier, kAsync))) return true;
    if (token == kAwait) return !disallow_await;
    if (token == kYield) return !is_generator && is_sloppy(language_mode);
    return IsStrictReservedWord(token) && is_sloppy(language_mode);
  }

  static bool IsCallable(Value token) {
    return base::IsInRange(token, kSuper, kEscapedStrictReservedWord);
  }

  static bool IsAutoSemicolon(Value token) {
    return base::IsInRange(token, kSemicolon, kEos);
  }

  static bool IsAnyIdentifier(Value token) {
    return base::IsInRange(token, kIdentifier, kEscapedStrictReservedWord);
  }

  static bool IsStrictReservedWord(Value token) {
    return base::IsInRange(token, kYield, kEscapedStrictReservedWord);
  }

  static bool IsLiteral(Value token) {
    return base::IsInRange(token, kNullLiteral, kString);
  }

  static bool IsTemplate(Value token) {
    return base::IsInRange(token, kTemplateSpan, kTemplateTail);
  }

  static bool IsMember(Value token) {
    return base::IsInRange(token, kTemplateSpan, kLeftBracket);
  }

  static bool IsProperty(Value token) {
    return base::IsInRange(token, kPeriod, kLeftBracket);
  }

  static bool IsPropertyOrCall(Value token) {
    return base::IsInRange(token, kTemplateSpan, kLeftParen);
  }

  static bool IsArrowOrAssignmentOp(Value token) {
    return base::IsInRange(token, kArrow, kAssignSub);
  }

  static bool IsAssignmentOp(Value token) {
    return base::IsInRange(token, kInit, kAssignSub);
  }

  static bool IsLogicalAssignmentOp(Value token) {
    return base::IsInRange(token, kAssignNullish, kAssignAnd);
  }

  static bool IsBinaryOp(Value op) { return base::IsInRange(op, kComma, kSub); }

  static bool IsCompareOp(Value op) { return base::IsInRange(op, kEq, kIn); }

  static bool IsOrderedRelationalCompareOp(Value op) {
    return base::IsInRange(op, kLessThan, kGreaterThanEq);
  }

  static bool IsEqualityOp(Value op) {
    return base::IsInRange(op, kEq, kEqStrict);
  }

  static Value BinaryOpForAssignment(Value op) {
    DCHECK(base::IsInRange(op, kAssignNullish, kAssignSub));
    Value result = static_cast<Value>(op - kAssignNullish + kNullish);
    DCHECK(IsBinaryOp(result));
    return result;
  }

  static bool IsBitOp(Value op) {
    return base::IsInRange(op, kBitOr, kShr) || op == kBitNot;
  }

  static bool IsUnaryOp(Value op) { return base::IsInRange(op, kAdd, kVoid); }
  static bool IsCountOp(Value op) { return base::IsInRange(op, kInc, kDec); }
  static bool IsUnaryOrCountOp(Value op) {
    return base::IsInRange(op, kAdd, kDec);
  }
  static bool IsShiftOp(Value op) { return base::IsInRange(op, kShl, kShr); }

  // Returns a string corresponding to the JS token string
  // (.e., "<" for the token kLessThan) or nullptr if the token doesn't
  // have a (unique) string (e.g. a kIdentifier).
  static const char* String(Value token) {
    DCHECK_GT(kNumTokens, token);  // token is unsigned
    return string_[token];
  }

  static uint8_t StringLength(Value token) {
    DCHECK_GT(kNumTokens, token);  // token is unsigned
    return string_length_[token];
  }

  // Returns the precedence > 0 for binary and compare
  // operators; returns 0 otherwise.
  static int Precedence(Value token, bool accept_IN) {
    DCHECK_GT(kNumTokens, token);  // token is unsigned
    return precedence_[accept_IN][token];
  }

 private:
  static const char* const name_[kNumTokens];
  static const char* const string_[kNumTokens];
  static const uint8_t string_length_[kNumTokens];
  static const int8_t precedence_[2][kNumTokens];
  static const uint8_t token_flags[kNumTokens];
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_TOKEN_H_

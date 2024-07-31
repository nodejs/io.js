// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// For reference check out:
// https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling
//
// Note that we only have partial C++11 support yet.

#include "absl/debugging/internal/demangle.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#include "absl/base/config.h"

#if ABSL_INTERNAL_HAS_CXA_DEMANGLE
#include <cxxabi.h>
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

typedef struct {
  const char *abbrev;
  const char *real_name;
  // Number of arguments in <expression> context, or 0 if disallowed.
  int arity;
} AbbrevPair;

// List of operators from Itanium C++ ABI.
static const AbbrevPair kOperatorList[] = {
    // New has special syntax (not currently supported).
    {"nw", "new", 0},
    {"na", "new[]", 0},

    // Works except that the 'gs' prefix is not supported.
    {"dl", "delete", 1},
    {"da", "delete[]", 1},

    {"ps", "+", 1},  // "positive"
    {"ng", "-", 1},  // "negative"
    {"ad", "&", 1},  // "address-of"
    {"de", "*", 1},  // "dereference"
    {"co", "~", 1},

    {"pl", "+", 2},
    {"mi", "-", 2},
    {"ml", "*", 2},
    {"dv", "/", 2},
    {"rm", "%", 2},
    {"an", "&", 2},
    {"or", "|", 2},
    {"eo", "^", 2},
    {"aS", "=", 2},
    {"pL", "+=", 2},
    {"mI", "-=", 2},
    {"mL", "*=", 2},
    {"dV", "/=", 2},
    {"rM", "%=", 2},
    {"aN", "&=", 2},
    {"oR", "|=", 2},
    {"eO", "^=", 2},
    {"ls", "<<", 2},
    {"rs", ">>", 2},
    {"lS", "<<=", 2},
    {"rS", ">>=", 2},
    {"eq", "==", 2},
    {"ne", "!=", 2},
    {"lt", "<", 2},
    {"gt", ">", 2},
    {"le", "<=", 2},
    {"ge", ">=", 2},
    {"nt", "!", 1},
    {"aa", "&&", 2},
    {"oo", "||", 2},
    {"pp", "++", 1},
    {"mm", "--", 1},
    {"cm", ",", 2},
    {"pm", "->*", 2},
    {"pt", "->", 0},  // Special syntax
    {"cl", "()", 0},  // Special syntax
    {"ix", "[]", 2},
    {"qu", "?", 3},
    {"st", "sizeof", 0},  // Special syntax
    {"sz", "sizeof", 1},  // Not a real operator name, but used in expressions.
    {nullptr, nullptr, 0},
};

// List of builtin types from Itanium C++ ABI.
//
// Invariant: only one- or two-character type abbreviations here.
static const AbbrevPair kBuiltinTypeList[] = {
    {"v", "void", 0},
    {"w", "wchar_t", 0},
    {"b", "bool", 0},
    {"c", "char", 0},
    {"a", "signed char", 0},
    {"h", "unsigned char", 0},
    {"s", "short", 0},
    {"t", "unsigned short", 0},
    {"i", "int", 0},
    {"j", "unsigned int", 0},
    {"l", "long", 0},
    {"m", "unsigned long", 0},
    {"x", "long long", 0},
    {"y", "unsigned long long", 0},
    {"n", "__int128", 0},
    {"o", "unsigned __int128", 0},
    {"f", "float", 0},
    {"d", "double", 0},
    {"e", "long double", 0},
    {"g", "__float128", 0},
    {"z", "ellipsis", 0},

    {"De", "decimal128", 0},      // IEEE 754r decimal floating point (128 bits)
    {"Dd", "decimal64", 0},       // IEEE 754r decimal floating point (64 bits)
    {"Dc", "decltype(auto)", 0},
    {"Da", "auto", 0},
    {"Dn", "std::nullptr_t", 0},  // i.e., decltype(nullptr)
    {"Df", "decimal32", 0},       // IEEE 754r decimal floating point (32 bits)
    {"Di", "char32_t", 0},
    {"Du", "char8_t", 0},
    {"Ds", "char16_t", 0},
    {"Dh", "float16", 0},         // IEEE 754r half-precision float (16 bits)
    {nullptr, nullptr, 0},
};

// List of substitutions Itanium C++ ABI.
static const AbbrevPair kSubstitutionList[] = {
    {"St", "", 0},
    {"Sa", "allocator", 0},
    {"Sb", "basic_string", 0},
    // std::basic_string<char, std::char_traits<char>,std::allocator<char> >
    {"Ss", "string", 0},
    // std::basic_istream<char, std::char_traits<char> >
    {"Si", "istream", 0},
    // std::basic_ostream<char, std::char_traits<char> >
    {"So", "ostream", 0},
    // std::basic_iostream<char, std::char_traits<char> >
    {"Sd", "iostream", 0},
    {nullptr, nullptr, 0},
};

// State needed for demangling.  This struct is copied in almost every stack
// frame, so every byte counts.
typedef struct {
  int mangled_idx;                     // Cursor of mangled name.
  int out_cur_idx;                     // Cursor of output string.
  int prev_name_idx;                   // For constructors/destructors.
  unsigned int prev_name_length : 16;  // For constructors/destructors.
  signed int nest_level : 15;          // For nested names.
  unsigned int append : 1;             // Append flag.
  // Note: for some reason MSVC can't pack "bool append : 1" into the same int
  // with the above two fields, so we use an int instead.  Amusingly it can pack
  // "signed bool" as expected, but relying on that to continue to be a legal
  // type seems ill-advised (as it's illegal in at least clang).
} ParseState;

static_assert(sizeof(ParseState) == 4 * sizeof(int),
              "unexpected size of ParseState");

// One-off state for demangling that's not subject to backtracking -- either
// constant data, data that's intentionally immune to backtracking (steps), or
// data that would never be changed by backtracking anyway (recursion_depth).
//
// Only one copy of this exists for each call to Demangle, so the size of this
// struct is nearly inconsequential.
typedef struct {
  const char *mangled_begin;  // Beginning of input string.
  char *out;                  // Beginning of output string.
  int out_end_idx;            // One past last allowed output character.
  int recursion_depth;        // For stack exhaustion prevention.
  int steps;               // Cap how much work we'll do, regardless of depth.
  ParseState parse_state;  // Backtrackable state copied for most frames.
} State;

namespace {
// Prevent deep recursion / stack exhaustion.
// Also prevent unbounded handling of complex inputs.
class ComplexityGuard {
 public:
  explicit ComplexityGuard(State *state) : state_(state) {
    ++state->recursion_depth;
    ++state->steps;
  }
  ~ComplexityGuard() { --state_->recursion_depth; }

  // 256 levels of recursion seems like a reasonable upper limit on depth.
  // 128 is not enough to demagle synthetic tests from demangle_unittest.txt:
  // "_ZaaZZZZ..." and "_ZaaZcvZcvZ..."
  static constexpr int kRecursionDepthLimit = 256;

  // We're trying to pick a charitable upper-limit on how many parse steps are
  // necessary to handle something that a human could actually make use of.
  // This is mostly in place as a bound on how much work we'll do if we are
  // asked to demangle an mangled name from an untrusted source, so it should be
  // much larger than the largest expected symbol, but much smaller than the
  // amount of work we can do in, e.g., a second.
  //
  // Some real-world symbols from an arbitrary binary started failing between
  // 2^12 and 2^13, so we multiply the latter by an extra factor of 16 to set
  // the limit.
  //
  // Spending one second on 2^17 parse steps would require each step to take
  // 7.6us, or ~30000 clock cycles, so it's safe to say this can be done in
  // under a second.
  static constexpr int kParseStepsLimit = 1 << 17;

  bool IsTooComplex() const {
    return state_->recursion_depth > kRecursionDepthLimit ||
           state_->steps > kParseStepsLimit;
  }

 private:
  State *state_;
};
}  // namespace

// We don't use strlen() in libc since it's not guaranteed to be async
// signal safe.
static size_t StrLen(const char *str) {
  size_t len = 0;
  while (*str != '\0') {
    ++str;
    ++len;
  }
  return len;
}

// Returns true if "str" has at least "n" characters remaining.
static bool AtLeastNumCharsRemaining(const char *str, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (str[i] == '\0') {
      return false;
    }
  }
  return true;
}

// Returns true if "str" has "prefix" as a prefix.
static bool StrPrefix(const char *str, const char *prefix) {
  size_t i = 0;
  while (str[i] != '\0' && prefix[i] != '\0' && str[i] == prefix[i]) {
    ++i;
  }
  return prefix[i] == '\0';  // Consumed everything in "prefix".
}

static void InitState(State* state,
                      const char* mangled,
                      char* out,
                      size_t out_size) {
  state->mangled_begin = mangled;
  state->out = out;
  state->out_end_idx = static_cast<int>(out_size);
  state->recursion_depth = 0;
  state->steps = 0;

  state->parse_state.mangled_idx = 0;
  state->parse_state.out_cur_idx = 0;
  state->parse_state.prev_name_idx = 0;
  state->parse_state.prev_name_length = 0;
  state->parse_state.nest_level = -1;
  state->parse_state.append = true;
}

static inline const char *RemainingInput(State *state) {
  return &state->mangled_begin[state->parse_state.mangled_idx];
}

// Returns true and advances "mangled_idx" if we find "one_char_token"
// at "mangled_idx" position.  It is assumed that "one_char_token" does
// not contain '\0'.
static bool ParseOneCharToken(State *state, const char one_char_token) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (RemainingInput(state)[0] == one_char_token) {
    ++state->parse_state.mangled_idx;
    return true;
  }
  return false;
}

// Returns true and advances "mangled_cur" if we find "two_char_token"
// at "mangled_cur" position.  It is assumed that "two_char_token" does
// not contain '\0'.
static bool ParseTwoCharToken(State *state, const char *two_char_token) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (RemainingInput(state)[0] == two_char_token[0] &&
      RemainingInput(state)[1] == two_char_token[1]) {
    state->parse_state.mangled_idx += 2;
    return true;
  }
  return false;
}

// Returns true and advances "mangled_cur" if we find any character in
// "char_class" at "mangled_cur" position.
static bool ParseCharClass(State *state, const char *char_class) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (RemainingInput(state)[0] == '\0') {
    return false;
  }
  const char *p = char_class;
  for (; *p != '\0'; ++p) {
    if (RemainingInput(state)[0] == *p) {
      ++state->parse_state.mangled_idx;
      return true;
    }
  }
  return false;
}

static bool ParseDigit(State *state, int *digit) {
  char c = RemainingInput(state)[0];
  if (ParseCharClass(state, "0123456789")) {
    if (digit != nullptr) {
      *digit = c - '0';
    }
    return true;
  }
  return false;
}

// This function is used for handling an optional non-terminal.
static bool Optional(bool /*status*/) { return true; }

// This function is used for handling <non-terminal>+ syntax.
typedef bool (*ParseFunc)(State *);
static bool OneOrMore(ParseFunc parse_func, State *state) {
  if (parse_func(state)) {
    while (parse_func(state)) {
    }
    return true;
  }
  return false;
}

// This function is used for handling <non-terminal>* syntax. The function
// always returns true and must be followed by a termination token or a
// terminating sequence not handled by parse_func (e.g.
// ParseOneCharToken(state, 'E')).
static bool ZeroOrMore(ParseFunc parse_func, State *state) {
  while (parse_func(state)) {
  }
  return true;
}

// Append "str" at "out_cur_idx".  If there is an overflow, out_cur_idx is
// set to out_end_idx+1.  The output string is ensured to
// always terminate with '\0' as long as there is no overflow.
static void Append(State *state, const char *const str, const size_t length) {
  for (size_t i = 0; i < length; ++i) {
    if (state->parse_state.out_cur_idx + 1 <
        state->out_end_idx) {  // +1 for '\0'
      state->out[state->parse_state.out_cur_idx++] = str[i];
    } else {
      // signal overflow
      state->parse_state.out_cur_idx = state->out_end_idx + 1;
      break;
    }
  }
  if (state->parse_state.out_cur_idx < state->out_end_idx) {
    state->out[state->parse_state.out_cur_idx] =
        '\0';  // Terminate it with '\0'
  }
}

// We don't use equivalents in libc to avoid locale issues.
static bool IsLower(char c) { return c >= 'a' && c <= 'z'; }

static bool IsAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// Returns true if "str" is a function clone suffix.  These suffixes are used
// by GCC 4.5.x and later versions (and our locally-modified version of GCC
// 4.4.x) to indicate functions which have been cloned during optimization.
// We treat any sequence (.<alpha>+.<digit>+)+ as a function clone suffix.
// Additionally, '_' is allowed along with the alphanumeric sequence.
static bool IsFunctionCloneSuffix(const char *str) {
  size_t i = 0;
  while (str[i] != '\0') {
    bool parsed = false;
    // Consume a single [.<alpha> | _]*[.<digit>]* sequence.
    if (str[i] == '.' && (IsAlpha(str[i + 1]) || str[i + 1] == '_')) {
      parsed = true;
      i += 2;
      while (IsAlpha(str[i]) || str[i] == '_') {
        ++i;
      }
    }
    if (str[i] == '.' && IsDigit(str[i + 1])) {
      parsed = true;
      i += 2;
      while (IsDigit(str[i])) {
        ++i;
      }
    }
    if (!parsed)
      return false;
  }
  return true;  // Consumed everything in "str".
}

static bool EndsWith(State *state, const char chr) {
  return state->parse_state.out_cur_idx > 0 &&
         state->parse_state.out_cur_idx < state->out_end_idx &&
         chr == state->out[state->parse_state.out_cur_idx - 1];
}

// Append "str" with some tweaks, iff "append" state is true.
static void MaybeAppendWithLength(State *state, const char *const str,
                                  const size_t length) {
  if (state->parse_state.append && length > 0) {
    // Append a space if the output buffer ends with '<' and "str"
    // starts with '<' to avoid <<<.
    if (str[0] == '<' && EndsWith(state, '<')) {
      Append(state, " ", 1);
    }
    // Remember the last identifier name for ctors/dtors,
    // but only if we haven't yet overflown the buffer.
    if (state->parse_state.out_cur_idx < state->out_end_idx &&
        (IsAlpha(str[0]) || str[0] == '_')) {
      state->parse_state.prev_name_idx = state->parse_state.out_cur_idx;
      state->parse_state.prev_name_length = static_cast<unsigned int>(length);
    }
    Append(state, str, length);
  }
}

// Appends a positive decimal number to the output if appending is enabled.
static bool MaybeAppendDecimal(State *state, int val) {
  // Max {32-64}-bit unsigned int is 20 digits.
  constexpr size_t kMaxLength = 20;
  char buf[kMaxLength];

  // We can't use itoa or sprintf as neither is specified to be
  // async-signal-safe.
  if (state->parse_state.append) {
    // We can't have a one-before-the-beginning pointer, so instead start with
    // one-past-the-end and manipulate one character before the pointer.
    char *p = &buf[kMaxLength];
    do {  // val=0 is the only input that should write a leading zero digit.
      *--p = static_cast<char>((val % 10) + '0');
      val /= 10;
    } while (p > buf && val != 0);

    // 'p' landed on the last character we set.  How convenient.
    Append(state, p, kMaxLength - static_cast<size_t>(p - buf));
  }

  return true;
}

// A convenient wrapper around MaybeAppendWithLength().
// Returns true so that it can be placed in "if" conditions.
static bool MaybeAppend(State *state, const char *const str) {
  if (state->parse_state.append) {
    size_t length = StrLen(str);
    MaybeAppendWithLength(state, str, length);
  }
  return true;
}

// This function is used for handling nested names.
static bool EnterNestedName(State *state) {
  state->parse_state.nest_level = 0;
  return true;
}

// This function is used for handling nested names.
static bool LeaveNestedName(State *state, int16_t prev_value) {
  state->parse_state.nest_level = prev_value;
  return true;
}

// Disable the append mode not to print function parameters, etc.
static bool DisableAppend(State *state) {
  state->parse_state.append = false;
  return true;
}

// Restore the append mode to the previous state.
static bool RestoreAppend(State *state, bool prev_value) {
  state->parse_state.append = prev_value;
  return true;
}

// Increase the nest level for nested names.
static void MaybeIncreaseNestLevel(State *state) {
  if (state->parse_state.nest_level > -1) {
    ++state->parse_state.nest_level;
  }
}

// Appends :: for nested names if necessary.
static void MaybeAppendSeparator(State *state) {
  if (state->parse_state.nest_level >= 1) {
    MaybeAppend(state, "::");
  }
}

// Cancel the last separator if necessary.
static void MaybeCancelLastSeparator(State *state) {
  if (state->parse_state.nest_level >= 1 && state->parse_state.append &&
      state->parse_state.out_cur_idx >= 2) {
    state->parse_state.out_cur_idx -= 2;
    state->out[state->parse_state.out_cur_idx] = '\0';
  }
}

// Returns true if the identifier of the given length pointed to by
// "mangled_cur" is anonymous namespace.
static bool IdentifierIsAnonymousNamespace(State *state, size_t length) {
  // Returns true if "anon_prefix" is a proper prefix of "mangled_cur".
  static const char anon_prefix[] = "_GLOBAL__N_";
  return (length > (sizeof(anon_prefix) - 1) &&
          StrPrefix(RemainingInput(state), anon_prefix));
}

// Forward declarations of our parsing functions.
static bool ParseMangledName(State *state);
static bool ParseEncoding(State *state);
static bool ParseName(State *state);
static bool ParseUnscopedName(State *state);
static bool ParseNestedName(State *state);
static bool ParsePrefix(State *state);
static bool ParseUnqualifiedName(State *state);
static bool ParseSourceName(State *state);
static bool ParseLocalSourceName(State *state);
static bool ParseUnnamedTypeName(State *state);
static bool ParseNumber(State *state, int *number_out);
static bool ParseFloatNumber(State *state);
static bool ParseSeqId(State *state);
static bool ParseIdentifier(State *state, size_t length);
static bool ParseOperatorName(State *state, int *arity);
static bool ParseSpecialName(State *state);
static bool ParseCallOffset(State *state);
static bool ParseNVOffset(State *state);
static bool ParseVOffset(State *state);
static bool ParseAbiTags(State *state);
static bool ParseCtorDtorName(State *state);
static bool ParseDecltype(State *state);
static bool ParseType(State *state);
static bool ParseCVQualifiers(State *state);
static bool ParseBuiltinType(State *state);
static bool ParseFunctionType(State *state);
static bool ParseBareFunctionType(State *state);
static bool ParseClassEnumType(State *state);
static bool ParseArrayType(State *state);
static bool ParsePointerToMemberType(State *state);
static bool ParseTemplateParam(State *state);
static bool ParseTemplateParamDecl(State *state);
static bool ParseTemplateTemplateParam(State *state);
static bool ParseTemplateArgs(State *state);
static bool ParseTemplateArg(State *state);
static bool ParseBaseUnresolvedName(State *state);
static bool ParseUnresolvedName(State *state);
static bool ParseExpression(State *state);
static bool ParseExprPrimary(State *state);
static bool ParseExprCastValue(State *state);
static bool ParseQRequiresClauseExpr(State *state);
static bool ParseLocalName(State *state);
static bool ParseLocalNameSuffix(State *state);
static bool ParseDiscriminator(State *state);
static bool ParseSubstitution(State *state, bool accept_std);

// Implementation note: the following code is a straightforward
// translation of the Itanium C++ ABI defined in BNF with a couple of
// exceptions.
//
// - Support GNU extensions not defined in the Itanium C++ ABI
// - <prefix> and <template-prefix> are combined to avoid infinite loop
// - Reorder patterns to shorten the code
// - Reorder patterns to give greedier functions precedence
//   We'll mark "Less greedy than" for these cases in the code
//
// Each parsing function changes the parse state and returns true on
// success, or returns false and doesn't change the parse state (note:
// the parse-steps counter increases regardless of success or failure).
// To ensure that the parse state isn't changed in the latter case, we
// save the original state before we call multiple parsing functions
// consecutively with &&, and restore it if unsuccessful.  See
// ParseEncoding() as an example of this convention.  We follow the
// convention throughout the code.
//
// Originally we tried to do demangling without following the full ABI
// syntax but it turned out we needed to follow the full syntax to
// parse complicated cases like nested template arguments.  Note that
// implementing a full-fledged demangler isn't trivial (libiberty's
// cp-demangle.c has +4300 lines).
//
// Note that (foo) in <(foo) ...> is a modifier to be ignored.
//
// Reference:
// - Itanium C++ ABI
//   <https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling>

// <mangled-name> ::= _Z <encoding>
static bool ParseMangledName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  return ParseTwoCharToken(state, "_Z") && ParseEncoding(state);
}

// <encoding> ::= <(function) name> <bare-function-type>
//                [`Q` <requires-clause expr>]
//            ::= <(data) name>
//            ::= <special-name>
//
// NOTE: Based on http://shortn/_Hoq9qG83rx
static bool ParseEncoding(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  // Since the first two productions both start with <name>, attempt
  // to parse it only once to avoid exponential blowup of backtracking.
  //
  // We're careful about exponential blowup because <encoding> recursively
  // appears in other productions downstream of its first two productions,
  // which means that every call to `ParseName` would possibly indirectly
  // result in two calls to `ParseName` etc.
  if (ParseName(state)) {
    if (!ParseBareFunctionType(state)) {
      return true;  // <(data) name>
    }

    // Parsed: <(function) name> <bare-function-type>
    // Pending: [`Q` <requires-clause expr>]
    ParseQRequiresClauseExpr(state);  // restores state on failure
    return true;
  }

  if (ParseSpecialName(state)) {
    return true;  // <special-name>
  }
  return false;
}

// <name> ::= <nested-name>
//        ::= <unscoped-template-name> <template-args>
//        ::= <unscoped-name>
//        ::= <local-name>
static bool ParseName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (ParseNestedName(state) || ParseLocalName(state)) {
    return true;
  }

  // We reorganize the productions to avoid re-parsing unscoped names.
  // - Inline <unscoped-template-name> productions:
  //   <name> ::= <substitution> <template-args>
  //          ::= <unscoped-name> <template-args>
  //          ::= <unscoped-name>
  // - Merge the two productions that start with unscoped-name:
  //   <name> ::= <unscoped-name> [<template-args>]

  ParseState copy = state->parse_state;
  // "std<...>" isn't a valid name.
  if (ParseSubstitution(state, /*accept_std=*/false) &&
      ParseTemplateArgs(state)) {
    return true;
  }
  state->parse_state = copy;

  // Note there's no need to restore state after this since only the first
  // subparser can fail.
  return ParseUnscopedName(state) && Optional(ParseTemplateArgs(state));
}

// <unscoped-name> ::= <unqualified-name>
//                 ::= St <unqualified-name>
static bool ParseUnscopedName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (ParseUnqualifiedName(state)) {
    return true;
  }

  ParseState copy = state->parse_state;
  if (ParseTwoCharToken(state, "St") && MaybeAppend(state, "std::") &&
      ParseUnqualifiedName(state)) {
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <ref-qualifer> ::= R // lvalue method reference qualifier
//                ::= O // rvalue method reference qualifier
static inline bool ParseRefQualifier(State *state) {
  return ParseCharClass(state, "OR");
}

// <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <prefix>
//                   <unqualified-name> E
//               ::= N [<CV-qualifiers>] [<ref-qualifier>] <template-prefix>
//                   <template-args> E
static bool ParseNestedName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'N') && EnterNestedName(state) &&
      Optional(ParseCVQualifiers(state)) &&
      Optional(ParseRefQualifier(state)) && ParsePrefix(state) &&
      LeaveNestedName(state, copy.nest_level) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;
  return false;
}

// This part is tricky.  If we literally translate them to code, we'll
// end up infinite loop.  Hence we merge them to avoid the case.
//
// <prefix> ::= <prefix> <unqualified-name>
//          ::= <template-prefix> <template-args>
//          ::= <template-param>
//          ::= <substitution>
//          ::= # empty
// <template-prefix> ::= <prefix> <(template) unqualified-name>
//                   ::= <template-param>
//                   ::= <substitution>
static bool ParsePrefix(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  bool has_something = false;
  while (true) {
    MaybeAppendSeparator(state);
    if (ParseTemplateParam(state) ||
        ParseSubstitution(state, /*accept_std=*/true) ||
        ParseUnscopedName(state) ||
        (ParseOneCharToken(state, 'M') && ParseUnnamedTypeName(state))) {
      has_something = true;
      MaybeIncreaseNestLevel(state);
      continue;
    }
    MaybeCancelLastSeparator(state);
    if (has_something && ParseTemplateArgs(state)) {
      return ParsePrefix(state);
    } else {
      break;
    }
  }
  return true;
}

// <unqualified-name> ::= <operator-name> [<abi-tags>]
//                    ::= <ctor-dtor-name> [<abi-tags>]
//                    ::= <source-name> [<abi-tags>]
//                    ::= <local-source-name> [<abi-tags>]
//                    ::= <unnamed-type-name> [<abi-tags>]
//
// <local-source-name> is a GCC extension; see below.
static bool ParseUnqualifiedName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (ParseOperatorName(state, nullptr) || ParseCtorDtorName(state) ||
      ParseSourceName(state) || ParseLocalSourceName(state) ||
      ParseUnnamedTypeName(state)) {
    return ParseAbiTags(state);
  }
  return false;
}

// <abi-tags> ::= <abi-tag> [<abi-tags>]
// <abi-tag>  ::= B <source-name>
static bool ParseAbiTags(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;

  while (ParseOneCharToken(state, 'B')) {
    ParseState copy = state->parse_state;
    MaybeAppend(state, "[abi:");

    if (!ParseSourceName(state)) {
      state->parse_state = copy;
      return false;
    }
    MaybeAppend(state, "]");
  }

  return true;
}

// <source-name> ::= <positive length number> <identifier>
static bool ParseSourceName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  int length = -1;
  if (ParseNumber(state, &length) &&
      ParseIdentifier(state, static_cast<size_t>(length))) {
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <local-source-name> ::= L <source-name> [<discriminator>]
//
// References:
//   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=31775
//   https://gcc.gnu.org/viewcvs?view=rev&revision=124467
static bool ParseLocalSourceName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'L') && ParseSourceName(state) &&
      Optional(ParseDiscriminator(state))) {
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <unnamed-type-name> ::= Ut [<(nonnegative) number>] _
//                     ::= <closure-type-name>
// <closure-type-name> ::= Ul <lambda-sig> E [<(nonnegative) number>] _
// <lambda-sig>        ::= <(parameter) type>+
static bool ParseUnnamedTypeName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  // Type's 1-based index n is encoded as { "", n == 1; itoa(n-2), otherwise }.
  // Optionally parse the encoded value into 'which' and add 2 to get the index.
  int which = -1;

  // Unnamed type local to function or class.
  if (ParseTwoCharToken(state, "Ut") && Optional(ParseNumber(state, &which)) &&
      which <= std::numeric_limits<int>::max() - 2 &&  // Don't overflow.
      ParseOneCharToken(state, '_')) {
    MaybeAppend(state, "{unnamed type#");
    MaybeAppendDecimal(state, 2 + which);
    MaybeAppend(state, "}");
    return true;
  }
  state->parse_state = copy;

  // Closure type.
  which = -1;
  if (ParseTwoCharToken(state, "Ul") && DisableAppend(state) &&
      OneOrMore(ParseType, state) && RestoreAppend(state, copy.append) &&
      ParseOneCharToken(state, 'E') && Optional(ParseNumber(state, &which)) &&
      which <= std::numeric_limits<int>::max() - 2 &&  // Don't overflow.
      ParseOneCharToken(state, '_')) {
    MaybeAppend(state, "{lambda()#");
    MaybeAppendDecimal(state, 2 + which);
    MaybeAppend(state, "}");
    return true;
  }
  state->parse_state = copy;

  return false;
}

// <number> ::= [n] <non-negative decimal integer>
// If "number_out" is non-null, then *number_out is set to the value of the
// parsed number on success.
static bool ParseNumber(State *state, int *number_out) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  bool negative = false;
  if (ParseOneCharToken(state, 'n')) {
    negative = true;
  }
  const char *p = RemainingInput(state);
  uint64_t number = 0;
  for (; *p != '\0'; ++p) {
    if (IsDigit(*p)) {
      number = number * 10 + static_cast<uint64_t>(*p - '0');
    } else {
      break;
    }
  }
  // Apply the sign with uint64_t arithmetic so overflows aren't UB.  Gives
  // "incorrect" results for out-of-range inputs, but negative values only
  // appear for literals, which aren't printed.
  if (negative) {
    number = ~number + 1;
  }
  if (p != RemainingInput(state)) {  // Conversion succeeded.
    state->parse_state.mangled_idx += p - RemainingInput(state);
    if (number_out != nullptr) {
      // Note: possibly truncate "number".
      *number_out = static_cast<int>(number);
    }
    return true;
  }
  return false;
}

// Floating-point literals are encoded using a fixed-length lowercase
// hexadecimal string.
static bool ParseFloatNumber(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  const char *p = RemainingInput(state);
  for (; *p != '\0'; ++p) {
    if (!IsDigit(*p) && !(*p >= 'a' && *p <= 'f')) {
      break;
    }
  }
  if (p != RemainingInput(state)) {  // Conversion succeeded.
    state->parse_state.mangled_idx += p - RemainingInput(state);
    return true;
  }
  return false;
}

// The <seq-id> is a sequence number in base 36,
// using digits and upper case letters
static bool ParseSeqId(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  const char *p = RemainingInput(state);
  for (; *p != '\0'; ++p) {
    if (!IsDigit(*p) && !(*p >= 'A' && *p <= 'Z')) {
      break;
    }
  }
  if (p != RemainingInput(state)) {  // Conversion succeeded.
    state->parse_state.mangled_idx += p - RemainingInput(state);
    return true;
  }
  return false;
}

// <identifier> ::= <unqualified source code identifier> (of given length)
static bool ParseIdentifier(State *state, size_t length) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (!AtLeastNumCharsRemaining(RemainingInput(state), length)) {
    return false;
  }
  if (IdentifierIsAnonymousNamespace(state, length)) {
    MaybeAppend(state, "(anonymous namespace)");
  } else {
    MaybeAppendWithLength(state, RemainingInput(state), length);
  }
  state->parse_state.mangled_idx += length;
  return true;
}

// <operator-name> ::= nw, and other two letters cases
//                 ::= cv <type>  # (cast)
//                 ::= v  <digit> <source-name> # vendor extended operator
static bool ParseOperatorName(State *state, int *arity) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (!AtLeastNumCharsRemaining(RemainingInput(state), 2)) {
    return false;
  }
  // First check with "cv" (cast) case.
  ParseState copy = state->parse_state;
  if (ParseTwoCharToken(state, "cv") && MaybeAppend(state, "operator ") &&
      EnterNestedName(state) && ParseType(state) &&
      LeaveNestedName(state, copy.nest_level)) {
    if (arity != nullptr) {
      *arity = 1;
    }
    return true;
  }
  state->parse_state = copy;

  // Then vendor extended operators.
  if (ParseOneCharToken(state, 'v') && ParseDigit(state, arity) &&
      ParseSourceName(state)) {
    return true;
  }
  state->parse_state = copy;

  // Other operator names should start with a lower alphabet followed
  // by a lower/upper alphabet.
  if (!(IsLower(RemainingInput(state)[0]) &&
        IsAlpha(RemainingInput(state)[1]))) {
    return false;
  }
  // We may want to perform a binary search if we really need speed.
  const AbbrevPair *p;
  for (p = kOperatorList; p->abbrev != nullptr; ++p) {
    if (RemainingInput(state)[0] == p->abbrev[0] &&
        RemainingInput(state)[1] == p->abbrev[1]) {
      if (arity != nullptr) {
        *arity = p->arity;
      }
      MaybeAppend(state, "operator");
      if (IsLower(*p->real_name)) {  // new, delete, etc.
        MaybeAppend(state, " ");
      }
      MaybeAppend(state, p->real_name);
      state->parse_state.mangled_idx += 2;
      return true;
    }
  }
  return false;
}

// <special-name> ::= TV <type>
//                ::= TT <type>
//                ::= TI <type>
//                ::= TS <type>
//                ::= TH <type>  # thread-local
//                ::= Tc <call-offset> <call-offset> <(base) encoding>
//                ::= GV <(object) name>
//                ::= T <call-offset> <(base) encoding>
// G++ extensions:
//                ::= TC <type> <(offset) number> _ <(base) type>
//                ::= TF <type>
//                ::= TJ <type>
//                ::= GR <name>
//                ::= GA <encoding>
//                ::= Th <call-offset> <(base) encoding>
//                ::= Tv <call-offset> <(base) encoding>
//
// Note: we don't care much about them since they don't appear in
// stack traces.  The are special data.
static bool ParseSpecialName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'T') && ParseCharClass(state, "VTISH") &&
      ParseType(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "Tc") && ParseCallOffset(state) &&
      ParseCallOffset(state) && ParseEncoding(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "GV") && ParseName(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseOneCharToken(state, 'T') && ParseCallOffset(state) &&
      ParseEncoding(state)) {
    return true;
  }
  state->parse_state = copy;

  // G++ extensions
  if (ParseTwoCharToken(state, "TC") && ParseType(state) &&
      ParseNumber(state, nullptr) && ParseOneCharToken(state, '_') &&
      DisableAppend(state) && ParseType(state)) {
    RestoreAppend(state, copy.append);
    return true;
  }
  state->parse_state = copy;

  if (ParseOneCharToken(state, 'T') && ParseCharClass(state, "FJ") &&
      ParseType(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "GR") && ParseName(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "GA") && ParseEncoding(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseOneCharToken(state, 'T') && ParseCharClass(state, "hv") &&
      ParseCallOffset(state) && ParseEncoding(state)) {
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <call-offset> ::= h <nv-offset> _
//               ::= v <v-offset> _
static bool ParseCallOffset(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'h') && ParseNVOffset(state) &&
      ParseOneCharToken(state, '_')) {
    return true;
  }
  state->parse_state = copy;

  if (ParseOneCharToken(state, 'v') && ParseVOffset(state) &&
      ParseOneCharToken(state, '_')) {
    return true;
  }
  state->parse_state = copy;

  return false;
}

// <nv-offset> ::= <(offset) number>
static bool ParseNVOffset(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  return ParseNumber(state, nullptr);
}

// <v-offset>  ::= <(offset) number> _ <(virtual offset) number>
static bool ParseVOffset(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (ParseNumber(state, nullptr) && ParseOneCharToken(state, '_') &&
      ParseNumber(state, nullptr)) {
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <ctor-dtor-name> ::= C1 | C2 | C3 | CI1 <base-class-type> | CI2
// <base-class-type>
//                  ::= D0 | D1 | D2
// # GCC extensions: "unified" constructor/destructor.  See
// #
// https://github.com/gcc-mirror/gcc/blob/7ad17b583c3643bd4557f29b8391ca7ef08391f5/gcc/cp/mangle.c#L1847
//                  ::= C4 | D4
static bool ParseCtorDtorName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'C')) {
    if (ParseCharClass(state, "1234")) {
      const char *const prev_name =
          state->out + state->parse_state.prev_name_idx;
      MaybeAppendWithLength(state, prev_name,
                            state->parse_state.prev_name_length);
      return true;
    } else if (ParseOneCharToken(state, 'I') && ParseCharClass(state, "12") &&
               ParseClassEnumType(state)) {
      return true;
    }
  }
  state->parse_state = copy;

  if (ParseOneCharToken(state, 'D') && ParseCharClass(state, "0124")) {
    const char *const prev_name = state->out + state->parse_state.prev_name_idx;
    MaybeAppend(state, "~");
    MaybeAppendWithLength(state, prev_name,
                          state->parse_state.prev_name_length);
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <decltype> ::= Dt <expression> E  # decltype of an id-expression or class
//                                   # member access (C++0x)
//            ::= DT <expression> E  # decltype of an expression (C++0x)
static bool ParseDecltype(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;

  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'D') && ParseCharClass(state, "tT") &&
      ParseExpression(state) && ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  return false;
}

// <type> ::= <CV-qualifiers> <type>
//        ::= P <type>   # pointer-to
//        ::= R <type>   # reference-to
//        ::= O <type>   # rvalue reference-to (C++0x)
//        ::= C <type>   # complex pair (C 2000)
//        ::= G <type>   # imaginary (C 2000)
//        ::= U <source-name> <type>  # vendor extended type qualifier
//        ::= <builtin-type>
//        ::= <function-type>
//        ::= <class-enum-type>  # note: just an alias for <name>
//        ::= <array-type>
//        ::= <pointer-to-member-type>
//        ::= <template-template-param> <template-args>
//        ::= <template-param>
//        ::= <decltype>
//        ::= <substitution>
//        ::= Dp <type>          # pack expansion of (C++0x)
//        ::= Dv <num-elems> _   # GNU vector extension
//
static bool ParseType(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;

  // We should check CV-qualifers, and PRGC things first.
  //
  // CV-qualifiers overlap with some operator names, but an operator name is not
  // valid as a type.  To avoid an ambiguity that can lead to exponential time
  // complexity, refuse to backtrack the CV-qualifiers.
  //
  // _Z4aoeuIrMvvE
  //  => _Z 4aoeuI        rM  v     v   E
  //         aoeu<operator%=, void, void>
  //  => _Z 4aoeuI r Mv v              E
  //         aoeu<void void::* restrict>
  //
  // By consuming the CV-qualifiers first, the former parse is disabled.
  if (ParseCVQualifiers(state)) {
    const bool result = ParseType(state);
    if (!result) state->parse_state = copy;
    return result;
  }
  state->parse_state = copy;

  // Similarly, these tag characters can overlap with other <name>s resulting in
  // two different parse prefixes that land on <template-args> in the same
  // place, such as "C3r1xI...".  So, disable the "ctor-name = C3" parse by
  // refusing to backtrack the tag characters.
  if (ParseCharClass(state, "OPRCG")) {
    const bool result = ParseType(state);
    if (!result) state->parse_state = copy;
    return result;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "Dp") && ParseType(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseOneCharToken(state, 'U') && ParseSourceName(state) &&
      ParseType(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseBuiltinType(state) || ParseFunctionType(state) ||
      ParseClassEnumType(state) || ParseArrayType(state) ||
      ParsePointerToMemberType(state) || ParseDecltype(state) ||
      // "std" on its own isn't a type.
      ParseSubstitution(state, /*accept_std=*/false)) {
    return true;
  }

  if (ParseTemplateTemplateParam(state) && ParseTemplateArgs(state)) {
    return true;
  }
  state->parse_state = copy;

  // Less greedy than <template-template-param> <template-args>.
  if (ParseTemplateParam(state)) {
    return true;
  }

  if (ParseTwoCharToken(state, "Dv") && ParseNumber(state, nullptr) &&
      ParseOneCharToken(state, '_')) {
    return true;
  }
  state->parse_state = copy;

  return false;
}

// <CV-qualifiers> ::= [r] [V] [K]
// We don't allow empty <CV-qualifiers> to avoid infinite loop in
// ParseType().
static bool ParseCVQualifiers(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  int num_cv_qualifiers = 0;
  num_cv_qualifiers += ParseOneCharToken(state, 'r');
  num_cv_qualifiers += ParseOneCharToken(state, 'V');
  num_cv_qualifiers += ParseOneCharToken(state, 'K');
  return num_cv_qualifiers > 0;
}

// <builtin-type> ::= v, etc.  # single-character builtin types
//                ::= u <source-name> [I <type> E]
//                ::= Dd, etc.  # two-character builtin types
//
// Not supported:
//                ::= DF <number> _ # _FloatN (N bits)
//
// NOTE: [I <type> E] is a vendor extension (http://shortn/_FrINpH1XC5).
static bool ParseBuiltinType(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;

  for (const AbbrevPair *p = kBuiltinTypeList; p->abbrev != nullptr; ++p) {
    // Guaranteed only 1- or 2-character strings in kBuiltinTypeList.
    if (p->abbrev[1] == '\0') {
      if (ParseOneCharToken(state, p->abbrev[0])) {
        MaybeAppend(state, p->real_name);
        return true;  // ::= v, etc.  # single-character builtin types
      }
    } else if (p->abbrev[2] == '\0' && ParseTwoCharToken(state, p->abbrev)) {
      MaybeAppend(state, p->real_name);
      return true;  // ::= Dd, etc.  # two-character builtin types
    }
  }

  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'u') && ParseSourceName(state)) {
    copy = state->parse_state;
    if (ParseOneCharToken(state, 'I') && ParseType(state) &&
        ParseOneCharToken(state, 'E')) {
      return true;  // ::= u <source-name> I <type> E
    }
    state->parse_state = copy;
    return true;  // ::= u <source-name>
  }
  state->parse_state = copy;
  return false;
}

//  <exception-spec> ::= Do                # non-throwing
//                                           exception-specification (e.g.,
//                                           noexcept, throw())
//                   ::= DO <expression> E # computed (instantiation-dependent)
//                                           noexcept
//                   ::= Dw <type>+ E      # dynamic exception specification
//                                           with instantiation-dependent types
static bool ParseExceptionSpec(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;

  if (ParseTwoCharToken(state, "Do")) return true;

  ParseState copy = state->parse_state;
  if (ParseTwoCharToken(state, "DO") && ParseExpression(state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;
  if (ParseTwoCharToken(state, "Dw") && OneOrMore(ParseType, state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  return false;
}

// <function-type> ::= [exception-spec] F [Y] <bare-function-type> [O] E
static bool ParseFunctionType(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (Optional(ParseExceptionSpec(state)) && ParseOneCharToken(state, 'F') &&
      Optional(ParseOneCharToken(state, 'Y')) && ParseBareFunctionType(state) &&
      Optional(ParseOneCharToken(state, 'O')) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <bare-function-type> ::= <(signature) type>+
static bool ParseBareFunctionType(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  DisableAppend(state);
  if (OneOrMore(ParseType, state)) {
    RestoreAppend(state, copy.append);
    MaybeAppend(state, "()");
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <class-enum-type> ::= <name>
static bool ParseClassEnumType(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  return ParseName(state);
}

// <array-type> ::= A <(positive dimension) number> _ <(element) type>
//              ::= A [<(dimension) expression>] _ <(element) type>
static bool ParseArrayType(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'A') && ParseNumber(state, nullptr) &&
      ParseOneCharToken(state, '_') && ParseType(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseOneCharToken(state, 'A') && Optional(ParseExpression(state)) &&
      ParseOneCharToken(state, '_') && ParseType(state)) {
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <pointer-to-member-type> ::= M <(class) type> <(member) type>
static bool ParsePointerToMemberType(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'M') && ParseType(state) && ParseType(state)) {
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <template-param> ::= T_
//                  ::= T <parameter-2 non-negative number> _
//                  ::= TL <level-1> __
//                  ::= TL <level-1> _ <parameter-2 non-negative number> _
static bool ParseTemplateParam(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (ParseTwoCharToken(state, "T_")) {
    MaybeAppend(state, "?");  // We don't support template substitutions.
    return true;              // ::= T_
  }

  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'T') && ParseNumber(state, nullptr) &&
      ParseOneCharToken(state, '_')) {
    MaybeAppend(state, "?");  // We don't support template substitutions.
    return true;              // ::= T <parameter-2 non-negative number> _
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "TL") && ParseNumber(state, nullptr)) {
    if (ParseTwoCharToken(state, "__")) {
      MaybeAppend(state, "?");  // We don't support template substitutions.
      return true;              // ::= TL <level-1> __
    }

    if (ParseOneCharToken(state, '_') && ParseNumber(state, nullptr) &&
        ParseOneCharToken(state, '_')) {
      MaybeAppend(state, "?");  // We don't support template substitutions.
      return true;  // ::= TL <level-1> _ <parameter-2 non-negative number> _
    }
  }
  state->parse_state = copy;
  return false;
}

// <template-param-decl>
//   ::= Ty                                  # template type parameter
//   ::= Tk <concept name> [<template-args>] # constrained type parameter
//   ::= Tn <type>                           # template non-type parameter
//   ::= Tt <template-param-decl>* E         # template template parameter
//   ::= Tp <template-param-decl>            # template parameter pack
//
// NOTE: <concept name> is just a <name>: http://shortn/_MqJVyr0fc1
// TODO(b/324066279): Implement optional suffix for `Tt`:
// [Q <requires-clause expr>]
static bool ParseTemplateParamDecl(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;

  if (ParseTwoCharToken(state, "Ty")) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "Tk") && ParseName(state) &&
      Optional(ParseTemplateArgs(state))) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "Tn") && ParseType(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "Tt") &&
      ZeroOrMore(ParseTemplateParamDecl, state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "Tp") && ParseTemplateParamDecl(state)) {
    return true;
  }
  state->parse_state = copy;

  return false;
}

// <template-template-param> ::= <template-param>
//                           ::= <substitution>
static bool ParseTemplateTemplateParam(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  return (ParseTemplateParam(state) ||
          // "std" on its own isn't a template.
          ParseSubstitution(state, /*accept_std=*/false));
}

// <template-args> ::= I <template-arg>+ [Q <requires-clause expr>] E
static bool ParseTemplateArgs(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  DisableAppend(state);
  if (ParseOneCharToken(state, 'I') && OneOrMore(ParseTemplateArg, state) &&
      Optional(ParseQRequiresClauseExpr(state)) &&
      ParseOneCharToken(state, 'E')) {
    RestoreAppend(state, copy.append);
    MaybeAppend(state, "<>");
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <template-arg>  ::= <template-param-decl> <template-arg>
//                 ::= <type>
//                 ::= <expr-primary>
//                 ::= J <template-arg>* E        # argument pack
//                 ::= X <expression> E
static bool ParseTemplateArg(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'J') && ZeroOrMore(ParseTemplateArg, state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  // There can be significant overlap between the following leading to
  // exponential backtracking:
  //
  //   <expr-primary> ::= L <type> <expr-cast-value> E
  //                 e.g. L 2xxIvE 1                 E
  //   <type>         ==> <local-source-name> <template-args>
  //                 e.g. L 2xx               IvE
  //
  // This means parsing an entire <type> twice, and <type> can contain
  // <template-arg>, so this can generate exponential backtracking.  There is
  // only overlap when the remaining input starts with "L <source-name>", so
  // parse all cases that can start this way jointly to share the common prefix.
  //
  // We have:
  //
  //   <template-arg> ::= <type>
  //                  ::= <expr-primary>
  //
  // First, drop all the productions of <type> that must start with something
  // other than 'L'.  All that's left is <class-enum-type>; inline it.
  //
  //   <type> ::= <nested-name> # starts with 'N'
  //          ::= <unscoped-name>
  //          ::= <unscoped-template-name> <template-args>
  //          ::= <local-name> # starts with 'Z'
  //
  // Drop and inline again:
  //
  //   <type> ::= <unscoped-name>
  //          ::= <unscoped-name> <template-args>
  //          ::= <substitution> <template-args> # starts with 'S'
  //
  // Merge the first two, inline <unscoped-name>, drop last:
  //
  //   <type> ::= <unqualified-name> [<template-args>]
  //          ::= St <unqualified-name> [<template-args>] # starts with 'S'
  //
  // Drop and inline:
  //
  //   <type> ::= <operator-name> [<template-args>] # starts with lowercase
  //          ::= <ctor-dtor-name> [<template-args>] # starts with 'C' or 'D'
  //          ::= <source-name> [<template-args>] # starts with digit
  //          ::= <local-source-name> [<template-args>]
  //          ::= <unnamed-type-name> [<template-args>] # starts with 'U'
  //
  // One more time:
  //
  //   <type> ::= L <source-name> [<template-args>]
  //
  // Likewise with <expr-primary>:
  //
  //   <expr-primary> ::= L <type> <expr-cast-value> E
  //                  ::= LZ <encoding> E # cannot overlap; drop
  //                  ::= L <mangled_name> E # cannot overlap; drop
  //
  // By similar reasoning as shown above, the only <type>s starting with
  // <source-name> are "<source-name> [<template-args>]".  Inline this.
  //
  //   <expr-primary> ::= L <source-name> [<template-args>] <expr-cast-value> E
  //
  // Now inline both of these into <template-arg>:
  //
  //   <template-arg> ::= L <source-name> [<template-args>]
  //                  ::= L <source-name> [<template-args>] <expr-cast-value> E
  //
  // Merge them and we're done:
  //   <template-arg>
  //     ::= L <source-name> [<template-args>] [<expr-cast-value> E]
  if (ParseLocalSourceName(state) && Optional(ParseTemplateArgs(state))) {
    copy = state->parse_state;
    if (ParseExprCastValue(state) && ParseOneCharToken(state, 'E')) {
      return true;
    }
    state->parse_state = copy;
    return true;
  }

  // Now that the overlapping cases can't reach this code, we can safely call
  // both of these.
  if (ParseType(state) || ParseExprPrimary(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseOneCharToken(state, 'X') && ParseExpression(state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTemplateParamDecl(state) && ParseTemplateArg(state)) {
    return true;
  }
  state->parse_state = copy;

  return false;
}

// <unresolved-type> ::= <template-param> [<template-args>]
//                   ::= <decltype>
//                   ::= <substitution>
static inline bool ParseUnresolvedType(State *state) {
  // No ComplexityGuard because we don't copy the state in this stack frame.
  return (ParseTemplateParam(state) && Optional(ParseTemplateArgs(state))) ||
         ParseDecltype(state) || ParseSubstitution(state, /*accept_std=*/false);
}

// <simple-id> ::= <source-name> [<template-args>]
static inline bool ParseSimpleId(State *state) {
  // No ComplexityGuard because we don't copy the state in this stack frame.

  // Note: <simple-id> cannot be followed by a parameter pack; see comment in
  // ParseUnresolvedType.
  return ParseSourceName(state) && Optional(ParseTemplateArgs(state));
}

// <base-unresolved-name> ::= <source-name> [<template-args>]
//                        ::= on <operator-name> [<template-args>]
//                        ::= dn <destructor-name>
static bool ParseBaseUnresolvedName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;

  if (ParseSimpleId(state)) {
    return true;
  }

  ParseState copy = state->parse_state;
  if (ParseTwoCharToken(state, "on") && ParseOperatorName(state, nullptr) &&
      Optional(ParseTemplateArgs(state))) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "dn") &&
      (ParseUnresolvedType(state) || ParseSimpleId(state))) {
    return true;
  }
  state->parse_state = copy;

  return false;
}

// <unresolved-name> ::= [gs] <base-unresolved-name>
//                   ::= sr <unresolved-type> <base-unresolved-name>
//                   ::= srN <unresolved-type> <unresolved-qualifier-level>+ E
//                         <base-unresolved-name>
//                   ::= [gs] sr <unresolved-qualifier-level>+ E
//                         <base-unresolved-name>
static bool ParseUnresolvedName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;

  ParseState copy = state->parse_state;
  if (Optional(ParseTwoCharToken(state, "gs")) &&
      ParseBaseUnresolvedName(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "sr") && ParseUnresolvedType(state) &&
      ParseBaseUnresolvedName(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "sr") && ParseOneCharToken(state, 'N') &&
      ParseUnresolvedType(state) &&
      OneOrMore(/* <unresolved-qualifier-level> ::= */ ParseSimpleId, state) &&
      ParseOneCharToken(state, 'E') && ParseBaseUnresolvedName(state)) {
    return true;
  }
  state->parse_state = copy;

  if (Optional(ParseTwoCharToken(state, "gs")) &&
      ParseTwoCharToken(state, "sr") &&
      OneOrMore(/* <unresolved-qualifier-level> ::= */ ParseSimpleId, state) &&
      ParseOneCharToken(state, 'E') && ParseBaseUnresolvedName(state)) {
    return true;
  }
  state->parse_state = copy;

  return false;
}

// <expression> ::= <1-ary operator-name> <expression>
//              ::= <2-ary operator-name> <expression> <expression>
//              ::= <3-ary operator-name> <expression> <expression> <expression>
//              ::= cl <expression>+ E
//              ::= cp <simple-id> <expression>* E # Clang-specific.
//              ::= cv <type> <expression>      # type (expression)
//              ::= cv <type> _ <expression>* E # type (expr-list)
//              ::= st <type>
//              ::= <template-param>
//              ::= <function-param>
//              ::= <expr-primary>
//              ::= dt <expression> <unresolved-name> # expr.name
//              ::= pt <expression> <unresolved-name> # expr->name
//              ::= sp <expression>         # argument pack expansion
//              ::= sr <type> <unqualified-name> <template-args>
//              ::= sr <type> <unqualified-name>
// <function-param> ::= fp <(top-level) CV-qualifiers> _
//                  ::= fp <(top-level) CV-qualifiers> <number> _
//                  ::= fL <number> p <(top-level) CV-qualifiers> _
//                  ::= fL <number> p <(top-level) CV-qualifiers> <number> _
static bool ParseExpression(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (ParseTemplateParam(state) || ParseExprPrimary(state)) {
    return true;
  }

  ParseState copy = state->parse_state;

  // Object/function call expression.
  if (ParseTwoCharToken(state, "cl") && OneOrMore(ParseExpression, state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  // Clang-specific "cp <simple-id> <expression>* E"
  //   https://clang.llvm.org/doxygen/ItaniumMangle_8cpp_source.html#l04338
  if (ParseTwoCharToken(state, "cp") && ParseSimpleId(state) &&
      ZeroOrMore(ParseExpression, state) && ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  // Function-param expression (level 0).
  if (ParseTwoCharToken(state, "fp") && Optional(ParseCVQualifiers(state)) &&
      Optional(ParseNumber(state, nullptr)) && ParseOneCharToken(state, '_')) {
    return true;
  }
  state->parse_state = copy;

  // Function-param expression (level 1+).
  if (ParseTwoCharToken(state, "fL") && Optional(ParseNumber(state, nullptr)) &&
      ParseOneCharToken(state, 'p') && Optional(ParseCVQualifiers(state)) &&
      Optional(ParseNumber(state, nullptr)) && ParseOneCharToken(state, '_')) {
    return true;
  }
  state->parse_state = copy;

  // Parse the conversion expressions jointly to avoid re-parsing the <type> in
  // their common prefix.  Parsed as:
  // <expression> ::= cv <type> <conversion-args>
  // <conversion-args> ::= _ <expression>* E
  //                   ::= <expression>
  //
  // Also don't try ParseOperatorName after seeing "cv", since ParseOperatorName
  // also needs to accept "cv <type>" in other contexts.
  if (ParseTwoCharToken(state, "cv")) {
    if (ParseType(state)) {
      ParseState copy2 = state->parse_state;
      if (ParseOneCharToken(state, '_') && ZeroOrMore(ParseExpression, state) &&
          ParseOneCharToken(state, 'E')) {
        return true;
      }
      state->parse_state = copy2;
      if (ParseExpression(state)) {
        return true;
      }
    }
  } else {
    // Parse unary, binary, and ternary operator expressions jointly, taking
    // care not to re-parse subexpressions repeatedly. Parse like:
    //   <expression> ::= <operator-name> <expression>
    //                    [<one-to-two-expressions>]
    //   <one-to-two-expressions> ::= <expression> [<expression>]
    int arity = -1;
    if (ParseOperatorName(state, &arity) &&
        arity > 0 &&  // 0 arity => disabled.
        (arity < 3 || ParseExpression(state)) &&
        (arity < 2 || ParseExpression(state)) &&
        (arity < 1 || ParseExpression(state))) {
      return true;
    }
  }
  state->parse_state = copy;

  // sizeof type
  if (ParseTwoCharToken(state, "st") && ParseType(state)) {
    return true;
  }
  state->parse_state = copy;

  // Object and pointer member access expressions.
  if ((ParseTwoCharToken(state, "dt") || ParseTwoCharToken(state, "pt")) &&
      ParseExpression(state) && ParseType(state)) {
    return true;
  }
  state->parse_state = copy;

  // Pointer-to-member access expressions.  This parses the same as a binary
  // operator, but it's implemented separately because "ds" shouldn't be
  // accepted in other contexts that parse an operator name.
  if (ParseTwoCharToken(state, "ds") && ParseExpression(state) &&
      ParseExpression(state)) {
    return true;
  }
  state->parse_state = copy;

  // Parameter pack expansion
  if (ParseTwoCharToken(state, "sp") && ParseExpression(state)) {
    return true;
  }
  state->parse_state = copy;

  return ParseUnresolvedName(state);
}

// <expr-primary> ::= L <type> <(value) number> E
//                ::= L <type> <(value) float> E
//                ::= L <mangled-name> E
//                // A bug in g++'s C++ ABI version 2 (-fabi-version=2).
//                ::= LZ <encoding> E
//
// Warning, subtle: the "bug" LZ production above is ambiguous with the first
// production where <type> starts with <local-name>, which can lead to
// exponential backtracking in two scenarios:
//
// - When whatever follows the E in the <local-name> in the first production is
//   not a name, we backtrack the whole <encoding> and re-parse the whole thing.
//
// - When whatever follows the <local-name> in the first production is not a
//   number and this <expr-primary> may be followed by a name, we backtrack the
//   <name> and re-parse it.
//
// Moreover this ambiguity isn't always resolved -- for example, the following
// has two different parses:
//
//   _ZaaILZ4aoeuE1x1EvE
//   => operator&&<aoeu, x, E, void>
//   => operator&&<(aoeu::x)(1), void>
//
// To resolve this, we just do what GCC's demangler does, and refuse to parse
// casts to <local-name> types.
static bool ParseExprPrimary(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;

  // The "LZ" special case: if we see LZ, we commit to accept "LZ <encoding> E"
  // or fail, no backtracking.
  if (ParseTwoCharToken(state, "LZ")) {
    if (ParseEncoding(state) && ParseOneCharToken(state, 'E')) {
      return true;
    }

    state->parse_state = copy;
    return false;
  }

  // The merged cast production.
  if (ParseOneCharToken(state, 'L') && ParseType(state) &&
      ParseExprCastValue(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseOneCharToken(state, 'L') && ParseMangledName(state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  return false;
}

// <number> or <float>, followed by 'E', as described above ParseExprPrimary.
static bool ParseExprCastValue(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  // We have to be able to backtrack after accepting a number because we could
  // have e.g. "7fffE", which will accept "7" as a number but then fail to find
  // the 'E'.
  ParseState copy = state->parse_state;
  if (ParseNumber(state, nullptr) && ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  if (ParseFloatNumber(state) && ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  return false;
}

// Parses `Q <requires-clause expr>`.
// If parsing fails, applies backtracking to `state`.
//
// This function covers two symbols instead of one for convenience,
// because in LLVM's Itanium ABI mangling grammar, <requires-clause expr>
// always appears after Q.
//
// Does not emit the parsed `requires` clause to simplify the implementation.
// In other words, these two functions' mangled names will demangle identically:
//
// template <typename T>
// int foo(T) requires IsIntegral<T>;
//
// vs.
//
// template <typename T>
// int foo(T);
static bool ParseQRequiresClauseExpr(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  DisableAppend(state);

  // <requires-clause expr> is just an <expression>: http://shortn/_9E1Ul0rIM8
  if (ParseOneCharToken(state, 'Q') && ParseExpression(state)) {
    RestoreAppend(state, copy.append);
    return true;
  }

  // also restores append
  state->parse_state = copy;
  return false;
}

// <local-name> ::= Z <(function) encoding> E <(entity) name> [<discriminator>]
//              ::= Z <(function) encoding> E s [<discriminator>]
//
// Parsing a common prefix of these two productions together avoids an
// exponential blowup of backtracking.  Parse like:
//   <local-name> := Z <encoding> E <local-name-suffix>
//   <local-name-suffix> ::= s [<discriminator>]
//                       ::= <name> [<discriminator>]

static bool ParseLocalNameSuffix(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;

  if (MaybeAppend(state, "::") && ParseName(state) &&
      Optional(ParseDiscriminator(state))) {
    return true;
  }

  // Since we're not going to overwrite the above "::" by re-parsing the
  // <encoding> (whose trailing '\0' byte was in the byte now holding the
  // first ':'), we have to rollback the "::" if the <name> parse failed.
  if (state->parse_state.append) {
    state->out[state->parse_state.out_cur_idx - 2] = '\0';
  }

  return ParseOneCharToken(state, 's') && Optional(ParseDiscriminator(state));
}

static bool ParseLocalName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'Z') && ParseEncoding(state) &&
      ParseOneCharToken(state, 'E') && ParseLocalNameSuffix(state)) {
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <discriminator> := _ <(non-negative) number>
static bool ParseDiscriminator(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, '_') && ParseNumber(state, nullptr)) {
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <substitution> ::= S_
//                ::= S <seq-id> _
//                ::= St, etc.
//
// "St" is special in that it's not valid as a standalone name, and it *is*
// allowed to precede a name without being wrapped in "N...E".  This means that
// if we accept it on its own, we can accept "St1a" and try to parse
// template-args, then fail and backtrack, accept "St" on its own, then "1a" as
// an unqualified name and re-parse the same template-args.  To block this
// exponential backtracking, we disable it with 'accept_std=false' in
// problematic contexts.
static bool ParseSubstitution(State *state, bool accept_std) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (ParseTwoCharToken(state, "S_")) {
    MaybeAppend(state, "?");  // We don't support substitutions.
    return true;
  }

  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'S') && ParseSeqId(state) &&
      ParseOneCharToken(state, '_')) {
    MaybeAppend(state, "?");  // We don't support substitutions.
    return true;
  }
  state->parse_state = copy;

  // Expand abbreviations like "St" => "std".
  if (ParseOneCharToken(state, 'S')) {
    const AbbrevPair *p;
    for (p = kSubstitutionList; p->abbrev != nullptr; ++p) {
      if (RemainingInput(state)[0] == p->abbrev[1] &&
          (accept_std || p->abbrev[1] != 't')) {
        MaybeAppend(state, "std");
        if (p->real_name[0] != '\0') {
          MaybeAppend(state, "::");
          MaybeAppend(state, p->real_name);
        }
        ++state->parse_state.mangled_idx;
        return true;
      }
    }
  }
  state->parse_state = copy;
  return false;
}

// Parse <mangled-name>, optionally followed by either a function-clone suffix
// or version suffix.  Returns true only if all of "mangled_cur" was consumed.
static bool ParseTopLevelMangledName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (ParseMangledName(state)) {
    if (RemainingInput(state)[0] != '\0') {
      // Drop trailing function clone suffix, if any.
      if (IsFunctionCloneSuffix(RemainingInput(state))) {
        return true;
      }
      // Append trailing version suffix if any.
      // ex. _Z3foo@@GLIBCXX_3.4
      if (RemainingInput(state)[0] == '@') {
        MaybeAppend(state, RemainingInput(state));
        return true;
      }
      return false;  // Unconsumed suffix.
    }
    return true;
  }
  return false;
}

static bool Overflowed(const State *state) {
  return state->parse_state.out_cur_idx >= state->out_end_idx;
}

// The demangler entry point.
bool Demangle(const char* mangled, char* out, size_t out_size) {
  State state;
  InitState(&state, mangled, out, out_size);
  return ParseTopLevelMangledName(&state) && !Overflowed(&state) &&
         state.parse_state.out_cur_idx > 0;
}

std::string DemangleString(const char* mangled) {
  std::string out;
  int status = 0;
  char* demangled = nullptr;
#if ABSL_INTERNAL_HAS_CXA_DEMANGLE
  demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
#endif
  if (status == 0 && demangled != nullptr) {
    out.append(demangled);
    free(demangled);
  } else {
    out.append(mangled);
  }
  return out;
}

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

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

#include "absl/debugging/internal/demangle.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

#include "absl/base/config.h"
#include "absl/debugging/internal/demangle_rust.h"

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
    // New has special syntax.
    {"nw", "new", 0},
    {"na", "new[]", 0},

    // Special-cased elsewhere to support the optional gs prefix.
    {"dl", "delete", 1},
    {"da", "delete[]", 1},

    {"aw", "co_await", 1},

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
    {"ss", "<=>", 2},
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
    {"sZ", "sizeof...", 0},  // Special syntax
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

  // Conditionally compiled support for marking the position of the first
  // construct Demangle couldn't parse.  This preprocessor symbol is intended
  // for use by Abseil demangler maintainers only; its behavior is not part of
  // Abseil's public interface.
#ifdef ABSL_INTERNAL_DEMANGLE_RECORDS_HIGH_WATER_MARK
  int high_water_mark;  // Input position where parsing failed.
  bool too_complex;  // True if any guard.IsTooComplex() call returned true.
#endif
} State;

namespace {

#ifdef ABSL_INTERNAL_DEMANGLE_RECORDS_HIGH_WATER_MARK
void UpdateHighWaterMark(State *state) {
  if (state->high_water_mark < state->parse_state.mangled_idx) {
    state->high_water_mark = state->parse_state.mangled_idx;
  }
}

void ReportHighWaterMark(State *state) {
  // Write out the mangled name with the trouble point marked, provided that the
  // output buffer is large enough and the mangled name did not hit a complexity
  // limit (in which case the high water mark wouldn't point out an unparsable
  // construct, only the point where a budget ran out).
  const size_t input_length = std::strlen(state->mangled_begin);
  if (input_length + 6 > static_cast<size_t>(state->out_end_idx) ||
      state->too_complex) {
    if (state->out_end_idx > 0) state->out[0] = '\0';
    return;
  }
  const size_t high_water_mark = static_cast<size_t>(state->high_water_mark);
  std::memcpy(state->out, state->mangled_begin, high_water_mark);
  std::memcpy(state->out + high_water_mark, "--!--", 5);
  std::memcpy(state->out + high_water_mark + 5,
              state->mangled_begin + high_water_mark,
              input_length - high_water_mark);
  state->out[input_length + 5] = '\0';
}
#else
void UpdateHighWaterMark(State *) {}
void ReportHighWaterMark(State *) {}
#endif

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
  // 128 is not enough to demangle synthetic tests from demangle_unittest.txt:
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
    if (state_->recursion_depth > kRecursionDepthLimit ||
        state_->steps > kParseStepsLimit) {
#ifdef ABSL_INTERNAL_DEMANGLE_RECORDS_HIGH_WATER_MARK
      state_->too_complex = true;
#endif
      return true;
    }
    return false;
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
#ifdef ABSL_INTERNAL_DEMANGLE_RECORDS_HIGH_WATER_MARK
  state->high_water_mark = 0;
  state->too_complex = false;
#endif

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
    UpdateHighWaterMark(state);
    return true;
  }
  return false;
}

// Returns true and advances "mangled_idx" if we find "two_char_token"
// at "mangled_idx" position.  It is assumed that "two_char_token" does
// not contain '\0'.
static bool ParseTwoCharToken(State *state, const char *two_char_token) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (RemainingInput(state)[0] == two_char_token[0] &&
      RemainingInput(state)[1] == two_char_token[1]) {
    state->parse_state.mangled_idx += 2;
    UpdateHighWaterMark(state);
    return true;
  }
  return false;
}

// Returns true and advances "mangled_idx" if we find "three_char_token"
// at "mangled_idx" position.  It is assumed that "three_char_token" does
// not contain '\0'.
static bool ParseThreeCharToken(State *state, const char *three_char_token) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (RemainingInput(state)[0] == three_char_token[0] &&
      RemainingInput(state)[1] == three_char_token[1] &&
      RemainingInput(state)[2] == three_char_token[2]) {
    state->parse_state.mangled_idx += 3;
    UpdateHighWaterMark(state);
    return true;
  }
  return false;
}

// Returns true and advances "mangled_idx" if we find a copy of the
// NUL-terminated string "long_token" at "mangled_idx" position.
static bool ParseLongToken(State *state, const char *long_token) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  int i = 0;
  for (; long_token[i] != '\0'; ++i) {
    // Note that we cannot run off the end of the NUL-terminated input here.
    // Inside the loop body, long_token[i] is known to be different from NUL.
    // So if we read the NUL on the end of the input here, we return at once.
    if (RemainingInput(state)[i] != long_token[i]) return false;
  }
  state->parse_state.mangled_idx += i;
  UpdateHighWaterMark(state);
  return true;
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
      UpdateHighWaterMark(state);
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
static bool ParseConversionOperatorType(State *state);
static bool ParseSpecialName(State *state);
static bool ParseCallOffset(State *state);
static bool ParseNVOffset(State *state);
static bool ParseVOffset(State *state);
static bool ParseAbiTags(State *state);
static bool ParseCtorDtorName(State *state);
static bool ParseDecltype(State *state);
static bool ParseType(State *state);
static bool ParseCVQualifiers(State *state);
static bool ParseExtendedQualifier(State *state);
static bool ParseBuiltinType(State *state);
static bool ParseVendorExtendedType(State *state);
static bool ParseFunctionType(State *state);
static bool ParseBareFunctionType(State *state);
static bool ParseOverloadAttribute(State *state);
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
static bool ParseUnresolvedQualifierLevel(State *state);
static bool ParseUnionSelector(State* state);
static bool ParseFunctionParam(State* state);
static bool ParseBracedExpression(State *state);
static bool ParseExpression(State *state);
static bool ParseInitializer(State *state);
static bool ParseExprPrimary(State *state);
static bool ParseExprCastValueAndTrailingE(State *state);
static bool ParseQRequiresClauseExpr(State *state);
static bool ParseRequirement(State *state);
static bool ParseTypeConstraint(State *state);
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
//          ::= <decltype>
//          ::= <substitution>
//          ::= # empty
// <template-prefix> ::= <prefix> <(template) unqualified-name>
//                   ::= <template-param>
//                   ::= <substitution>
//                   ::= <vendor-extended-type>
static bool ParsePrefix(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  bool has_something = false;
  while (true) {
    MaybeAppendSeparator(state);
    if (ParseTemplateParam(state) || ParseDecltype(state) ||
        ParseSubstitution(state, /*accept_std=*/true) ||
        // Although the official grammar does not mention it, nested-names
        // shaped like Nu14__some_builtinIiE6memberE occur in practice, and it
        // is not clear what else a compiler is supposed to do when a
        // vendor-extended type has named members.
        ParseVendorExtendedType(state) ||
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
//                    ::= DC <source-name>+ E  # C++17 structured binding
//                    ::= F <source-name>  # C++20 constrained friend
//                    ::= F <operator-name>  # C++20 constrained friend
//
// <local-source-name> is a GCC extension; see below.
//
// For the F notation for constrained friends, see
// https://github.com/itanium-cxx-abi/cxx-abi/issues/24#issuecomment-1491130332.
static bool ParseUnqualifiedName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  if (ParseOperatorName(state, nullptr) || ParseCtorDtorName(state) ||
      ParseSourceName(state) || ParseLocalSourceName(state) ||
      ParseUnnamedTypeName(state)) {
    return ParseAbiTags(state);
  }

  // DC <source-name>+ E
  ParseState copy = state->parse_state;
  if (ParseTwoCharToken(state, "DC") && OneOrMore(ParseSourceName, state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  // F <source-name>
  // F <operator-name>
  if (ParseOneCharToken(state, 'F') && MaybeAppend(state, "friend ") &&
      (ParseSourceName(state) || ParseOperatorName(state, nullptr))) {
    return true;
  }
  state->parse_state = copy;

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
// <lambda-sig>        ::= <template-param-decl>* <(parameter) type>+
//
// For <template-param-decl>* in <lambda-sig> see:
//
// https://github.com/itanium-cxx-abi/cxx-abi/issues/31
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
      ZeroOrMore(ParseTemplateParamDecl, state) &&
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
    UpdateHighWaterMark(state);
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
    UpdateHighWaterMark(state);
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
    UpdateHighWaterMark(state);
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
  UpdateHighWaterMark(state);
  return true;
}

// <operator-name> ::= nw, and other two letters cases
//                 ::= cv <type>  # (cast)
//                 ::= li <source-name>  # C++11 user-defined literal
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
      EnterNestedName(state) && ParseConversionOperatorType(state) &&
      LeaveNestedName(state, copy.nest_level)) {
    if (arity != nullptr) {
      *arity = 1;
    }
    return true;
  }
  state->parse_state = copy;

  // Then user-defined literals.
  if (ParseTwoCharToken(state, "li") && MaybeAppend(state, "operator\"\" ") &&
      ParseSourceName(state)) {
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
      UpdateHighWaterMark(state);
      return true;
    }
  }
  return false;
}

// <operator-name> ::= cv <type>  # (cast)
//
// The name of a conversion operator is the one place where cv-qualifiers, *, &,
// and other simple type combinators are expected to appear in our stripped-down
// demangling (elsewhere they appear in function signatures or template
// arguments, which we omit from the output).  We make reasonable efforts to
// render simple cases accurately.
static bool ParseConversionOperatorType(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;

  // Scan pointers, const, and other easy mangling prefixes with postfix
  // demanglings.  Remember the range of input for later rescanning.
  //
  // See `ParseType` and the `switch` below for the meaning of each char.
  const char* begin_simple_prefixes = RemainingInput(state);
  while (ParseCharClass(state, "OPRCGrVK")) {}
  const char* end_simple_prefixes = RemainingInput(state);

  // Emit the base type first.
  if (!ParseType(state)) {
    state->parse_state = copy;
    return false;
  }

  // Then rescan the easy type combinators in reverse order to emit their
  // demanglings in the expected output order.
  while (begin_simple_prefixes != end_simple_prefixes) {
    switch (*--end_simple_prefixes) {
      case 'P':
        MaybeAppend(state, "*");
        break;
      case 'R':
        MaybeAppend(state, "&");
        break;
      case 'O':
        MaybeAppend(state, "&&");
        break;
      case 'C':
        MaybeAppend(state, " _Complex");
        break;
      case 'G':
        MaybeAppend(state, " _Imaginary");
        break;
      case 'r':
        MaybeAppend(state, " restrict");
        break;
      case 'V':
        MaybeAppend(state, " volatile");
        break;
      case 'K':
        MaybeAppend(state, " const");
        break;
    }
  }
  return true;
}

// <special-name> ::= TV <type>
//                ::= TT <type>
//                ::= TI <type>
//                ::= TS <type>
//                ::= TW <name>  # thread-local wrapper
//                ::= TH <name>  # thread-local initialization
//                ::= Tc <call-offset> <call-offset> <(base) encoding>
//                ::= GV <(object) name>
//                ::= GR <(object) name> [<seq-id>] _
//                ::= T <call-offset> <(base) encoding>
//                ::= GTt <encoding>  # transaction-safe entry point
//                ::= TA <template-arg>  # nontype template parameter object
// G++ extensions:
//                ::= TC <type> <(offset) number> _ <(base) type>
//                ::= TF <type>
//                ::= TJ <type>
//                ::= GR <name>  # without final _, perhaps an earlier form?
//                ::= GA <encoding>
//                ::= Th <call-offset> <(base) encoding>
//                ::= Tv <call-offset> <(base) encoding>
//
// Note: Most of these are special data, not functions that occur in stack
// traces.  Exceptions are TW and TH, which denote functions supporting the
// thread_local feature.  For these see:
//
// https://maskray.me/blog/2021-02-14-all-about-thread-local-storage
//
// For TA see https://github.com/itanium-cxx-abi/cxx-abi/issues/63.
static bool ParseSpecialName(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;

  if (ParseTwoCharToken(state, "TW")) {
    MaybeAppend(state, "thread-local wrapper routine for ");
    if (ParseName(state)) return true;
    state->parse_state = copy;
    return false;
  }

  if (ParseTwoCharToken(state, "TH")) {
    MaybeAppend(state, "thread-local initialization routine for ");
    if (ParseName(state)) return true;
    state->parse_state = copy;
    return false;
  }

  if (ParseOneCharToken(state, 'T') && ParseCharClass(state, "VTIS") &&
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

  // <special-name> ::= GR <(object) name> [<seq-id>] _  # modern standard
  //                ::= GR <(object) name>  # also recognized
  if (ParseTwoCharToken(state, "GR")) {
    MaybeAppend(state, "reference temporary for ");
    if (!ParseName(state)) {
      state->parse_state = copy;
      return false;
    }
    const bool has_seq_id = ParseSeqId(state);
    const bool has_underscore = ParseOneCharToken(state, '_');
    if (has_seq_id && !has_underscore) {
      state->parse_state = copy;
      return false;
    }
    return true;
  }

  if (ParseTwoCharToken(state, "GA") && ParseEncoding(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseThreeCharToken(state, "GTt") &&
      MaybeAppend(state, "transaction clone for ") && ParseEncoding(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseOneCharToken(state, 'T') && ParseCharClass(state, "hv") &&
      ParseCallOffset(state) && ParseEncoding(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "TA")) {
    bool append = state->parse_state.append;
    DisableAppend(state);
    if (ParseTemplateArg(state)) {
      RestoreAppend(state, append);
      MaybeAppend(state, "template parameter object");
      return true;
    }
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
//        ::= Dv <(elements) number> _ <type>  # GNU vector extension
//        ::= Dv <(bytes) expression> _ <type>
//        ::= Dk <type-constraint>  # constrained auto
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

  // GNU vector extension Dv <number> _ <type>
  if (ParseTwoCharToken(state, "Dv") && ParseNumber(state, nullptr) &&
      ParseOneCharToken(state, '_') && ParseType(state)) {
    return true;
  }
  state->parse_state = copy;

  // GNU vector extension Dv <expression> _ <type>
  if (ParseTwoCharToken(state, "Dv") && ParseExpression(state) &&
      ParseOneCharToken(state, '_') && ParseType(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "Dk") && ParseTypeConstraint(state)) {
    return true;
  }
  state->parse_state = copy;

  // For this notation see CXXNameMangler::mangleType in Clang's source code.
  // The relevant logic and its comment "not clear how to mangle this!" date
  // from 2011, so it may be with us awhile.
  return ParseLongToken(state, "_SUBSTPACK_");
}

// <qualifiers> ::= <extended-qualifier>* <CV-qualifiers>
// <CV-qualifiers> ::= [r] [V] [K]
//
// We don't allow empty <CV-qualifiers> to avoid infinite loop in
// ParseType().
static bool ParseCVQualifiers(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  int num_cv_qualifiers = 0;
  while (ParseExtendedQualifier(state)) ++num_cv_qualifiers;
  num_cv_qualifiers += ParseOneCharToken(state, 'r');
  num_cv_qualifiers += ParseOneCharToken(state, 'V');
  num_cv_qualifiers += ParseOneCharToken(state, 'K');
  return num_cv_qualifiers > 0;
}

// <extended-qualifier> ::= U <source-name> [<template-args>]
static bool ParseExtendedQualifier(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;

  if (!ParseOneCharToken(state, 'U')) return false;

  bool append = state->parse_state.append;
  DisableAppend(state);
  if (!ParseSourceName(state)) {
    state->parse_state = copy;
    return false;
  }
  Optional(ParseTemplateArgs(state));
  RestoreAppend(state, append);
  return true;
}

// <builtin-type> ::= v, etc.  # single-character builtin types
//                ::= <vendor-extended-type>
//                ::= Dd, etc.  # two-character builtin types
//                ::= DB (<number> | <expression>) _  # _BitInt(N)
//                ::= DU (<number> | <expression>) _  # unsigned _BitInt(N)
//                ::= DF <number> _  # _FloatN (N bits)
//                ::= DF <number> x  # _FloatNx
//                ::= DF16b  # std::bfloat16_t
//
// Not supported:
//                ::= [DS] DA <fixed-point-size>
//                ::= [DS] DR <fixed-point-size>
// because real implementations of N1169 fixed-point are scant.
static bool ParseBuiltinType(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;

  // DB (<number> | <expression>) _  # _BitInt(N)
  // DU (<number> | <expression>) _  # unsigned _BitInt(N)
  if (ParseTwoCharToken(state, "DB") ||
      (ParseTwoCharToken(state, "DU") && MaybeAppend(state, "unsigned "))) {
    bool append = state->parse_state.append;
    DisableAppend(state);
    int number = -1;
    if (!ParseNumber(state, &number) && !ParseExpression(state)) {
      state->parse_state = copy;
      return false;
    }
    RestoreAppend(state, append);

    if (!ParseOneCharToken(state, '_')) {
      state->parse_state = copy;
      return false;
    }

    MaybeAppend(state, "_BitInt(");
    if (number >= 0) {
      MaybeAppendDecimal(state, number);
    } else {
      MaybeAppend(state, "?");  // the best we can do for dependent sizes
    }
    MaybeAppend(state, ")");
    return true;
  }

  // DF <number> _  # _FloatN
  // DF <number> x  # _FloatNx
  // DF16b  # std::bfloat16_t
  if (ParseTwoCharToken(state, "DF")) {
    if (ParseThreeCharToken(state, "16b")) {
      MaybeAppend(state, "std::bfloat16_t");
      return true;
    }
    int number = 0;
    if (!ParseNumber(state, &number)) {
      state->parse_state = copy;
      return false;
    }
    MaybeAppend(state, "_Float");
    MaybeAppendDecimal(state, number);
    if (ParseOneCharToken(state, 'x')) {
      MaybeAppend(state, "x");
      return true;
    }
    if (ParseOneCharToken(state, '_')) return true;
    state->parse_state = copy;
    return false;
  }

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

  return ParseVendorExtendedType(state);
}

// <vendor-extended-type> ::= u <source-name> [<template-args>]
static bool ParseVendorExtendedType(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;

  ParseState copy = state->parse_state;
  if (ParseOneCharToken(state, 'u') && ParseSourceName(state) &&
      Optional(ParseTemplateArgs(state))) {
    return true;
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

// <function-type> ::=
//     [exception-spec] [Dx] F [Y] <bare-function-type> [<ref-qualifier>] E
//
// <ref-qualifier> ::= R | O
static bool ParseFunctionType(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  Optional(ParseExceptionSpec(state));
  Optional(ParseTwoCharToken(state, "Dx"));
  if (!ParseOneCharToken(state, 'F')) {
    state->parse_state = copy;
    return false;
  }
  Optional(ParseOneCharToken(state, 'Y'));
  if (!ParseBareFunctionType(state)) {
    state->parse_state = copy;
    return false;
  }
  Optional(ParseCharClass(state, "RO"));
  if (!ParseOneCharToken(state, 'E')) {
    state->parse_state = copy;
    return false;
  }
  return true;
}

// <bare-function-type> ::= <overload-attribute>* <(signature) type>+
//
// The <overload-attribute>* prefix is nonstandard; see the comment on
// ParseOverloadAttribute.
static bool ParseBareFunctionType(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  DisableAppend(state);
  if (ZeroOrMore(ParseOverloadAttribute, state) &&
      OneOrMore(ParseType, state)) {
    RestoreAppend(state, copy.append);
    MaybeAppend(state, "()");
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <overload-attribute> ::= Ua <name>
//
// The nonstandard <overload-attribute> production is sufficient to accept the
// current implementation of __attribute__((enable_if(condition, "message")))
// and future attributes of a similar shape.  See
// https://clang.llvm.org/docs/AttributeReference.html#enable-if and the
// definition of CXXNameMangler::mangleFunctionEncodingBareType in Clang's
// source code.
static bool ParseOverloadAttribute(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (ParseTwoCharToken(state, "Ua") && ParseName(state)) {
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <class-enum-type> ::= <name>
//                   ::= Ts <name>  # struct Name or class Name
//                   ::= Tu <name>  # union Name
//                   ::= Te <name>  # enum Name
//
// See http://shortn/_W3YrltiEd0.
static bool ParseClassEnumType(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;
  if (Optional(ParseTwoCharToken(state, "Ts") ||
               ParseTwoCharToken(state, "Tu") ||
               ParseTwoCharToken(state, "Te")) &&
      ParseName(state)) {
    return true;
  }
  state->parse_state = copy;
  return false;
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
    if (ParseExprCastValueAndTrailingE(state)) {
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
//                   ::= sr St <simple-id> <simple-id>  # nonstandard
//
// The last case is not part of the official grammar but has been observed in
// real-world examples that the GNU demangler (but not the LLVM demangler) is
// able to decode; see demangle_test.cc for one such symbol name.  The shape
// sr St <simple-id> <simple-id> was inferred by closed-box testing of the GNU
// demangler.
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
      OneOrMore(ParseUnresolvedQualifierLevel, state) &&
      ParseOneCharToken(state, 'E') && ParseBaseUnresolvedName(state)) {
    return true;
  }
  state->parse_state = copy;

  if (Optional(ParseTwoCharToken(state, "gs")) &&
      ParseTwoCharToken(state, "sr") &&
      OneOrMore(ParseUnresolvedQualifierLevel, state) &&
      ParseOneCharToken(state, 'E') && ParseBaseUnresolvedName(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "sr") && ParseTwoCharToken(state, "St") &&
      ParseSimpleId(state) && ParseSimpleId(state)) {
    return true;
  }
  state->parse_state = copy;

  return false;
}

// <unresolved-qualifier-level> ::= <simple-id>
//                              ::= <substitution> <template-args>
//
// The production <substitution> <template-args> is nonstandard but is observed
// in practice.  An upstream discussion on the best shape of <unresolved-name>
// has not converged:
//
// https://github.com/itanium-cxx-abi/cxx-abi/issues/38
static bool ParseUnresolvedQualifierLevel(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;

  if (ParseSimpleId(state)) return true;

  ParseState copy = state->parse_state;
  if (ParseSubstitution(state, /*accept_std=*/false) &&
      ParseTemplateArgs(state)) {
    return true;
  }
  state->parse_state = copy;
  return false;
}

// <union-selector> ::= _ [<number>]
//
// https://github.com/itanium-cxx-abi/cxx-abi/issues/47
static bool ParseUnionSelector(State *state) {
  return ParseOneCharToken(state, '_') && Optional(ParseNumber(state, nullptr));
}

// <function-param> ::= fp <(top-level) CV-qualifiers> _
//                  ::= fp <(top-level) CV-qualifiers> <number> _
//                  ::= fL <number> p <(top-level) CV-qualifiers> _
//                  ::= fL <number> p <(top-level) CV-qualifiers> <number> _
//                  ::= fpT  # this
static bool ParseFunctionParam(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;

  ParseState copy = state->parse_state;

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

  return ParseThreeCharToken(state, "fpT");
}

// <braced-expression> ::= <expression>
//                     ::= di <field source-name> <braced-expression>
//                     ::= dx <index expression> <braced-expression>
//                     ::= dX <expression> <expression> <braced-expression>
static bool ParseBracedExpression(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;

  ParseState copy = state->parse_state;

  if (ParseTwoCharToken(state, "di") && ParseSourceName(state) &&
      ParseBracedExpression(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "dx") && ParseExpression(state) &&
      ParseBracedExpression(state)) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "dX") &&
      ParseExpression(state) && ParseExpression(state) &&
      ParseBracedExpression(state)) {
    return true;
  }
  state->parse_state = copy;

  return ParseExpression(state);
}

// <expression> ::= <1-ary operator-name> <expression>
//              ::= <2-ary operator-name> <expression> <expression>
//              ::= <3-ary operator-name> <expression> <expression> <expression>
//              ::= pp_ <expression>  # ++e; pp <expression> is e++
//              ::= mm_ <expression>  # --e; mm <expression> is e--
//              ::= cl <expression>+ E
//              ::= cp <simple-id> <expression>* E # Clang-specific.
//              ::= so <type> <expression> [<number>] <union-selector>* [p] E
//              ::= cv <type> <expression>      # type (expression)
//              ::= cv <type> _ <expression>* E # type (expr-list)
//              ::= tl <type> <braced-expression>* E
//              ::= il <braced-expression>* E
//              ::= [gs] nw <expression>* _ <type> E
//              ::= [gs] nw <expression>* _ <type> <initializer>
//              ::= [gs] na <expression>* _ <type> E
//              ::= [gs] na <expression>* _ <type> <initializer>
//              ::= [gs] dl <expression>
//              ::= [gs] da <expression>
//              ::= dc <type> <expression>
//              ::= sc <type> <expression>
//              ::= cc <type> <expression>
//              ::= rc <type> <expression>
//              ::= ti <type>
//              ::= te <expression>
//              ::= st <type>
//              ::= at <type>
//              ::= az <expression>
//              ::= nx <expression>
//              ::= <template-param>
//              ::= <function-param>
//              ::= sZ <template-param>
//              ::= sZ <function-param>
//              ::= sP <template-arg>* E
//              ::= <expr-primary>
//              ::= dt <expression> <unresolved-name> # expr.name
//              ::= pt <expression> <unresolved-name> # expr->name
//              ::= sp <expression>         # argument pack expansion
//              ::= fl <binary operator-name> <expression>
//              ::= fr <binary operator-name> <expression>
//              ::= fL <binary operator-name> <expression> <expression>
//              ::= fR <binary operator-name> <expression> <expression>
//              ::= tw <expression>
//              ::= tr
//              ::= sr <type> <unqualified-name> <template-args>
//              ::= sr <type> <unqualified-name>
//              ::= u <source-name> <template-arg>* E  # vendor extension
//              ::= rq <requirement>+ E
//              ::= rQ <bare-function-type> _ <requirement>+ E
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

  // Preincrement and predecrement.  Postincrement and postdecrement are handled
  // by the operator-name logic later on.
  if ((ParseThreeCharToken(state, "pp_") ||
       ParseThreeCharToken(state, "mm_")) &&
      ParseExpression(state)) {
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

  // <expression> ::= so <type> <expression> [<number>] <union-selector>* [p] E
  //
  // https://github.com/itanium-cxx-abi/cxx-abi/issues/47
  if (ParseTwoCharToken(state, "so") && ParseType(state) &&
      ParseExpression(state) && Optional(ParseNumber(state, nullptr)) &&
      ZeroOrMore(ParseUnionSelector, state) &&
      Optional(ParseOneCharToken(state, 'p')) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  // <expression> ::= <function-param>
  if (ParseFunctionParam(state)) return true;
  state->parse_state = copy;

  // <expression> ::= tl <type> <braced-expression>* E
  if (ParseTwoCharToken(state, "tl") && ParseType(state) &&
      ZeroOrMore(ParseBracedExpression, state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  // <expression> ::= il <braced-expression>* E
  if (ParseTwoCharToken(state, "il") &&
      ZeroOrMore(ParseBracedExpression, state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  // <expression> ::= [gs] nw <expression>* _ <type> E
  //              ::= [gs] nw <expression>* _ <type> <initializer>
  //              ::= [gs] na <expression>* _ <type> E
  //              ::= [gs] na <expression>* _ <type> <initializer>
  if (Optional(ParseTwoCharToken(state, "gs")) &&
      (ParseTwoCharToken(state, "nw") || ParseTwoCharToken(state, "na")) &&
      ZeroOrMore(ParseExpression, state) && ParseOneCharToken(state, '_') &&
      ParseType(state) &&
      (ParseOneCharToken(state, 'E') || ParseInitializer(state))) {
    return true;
  }
  state->parse_state = copy;

  // <expression> ::= [gs] dl <expression>
  //              ::= [gs] da <expression>
  if (Optional(ParseTwoCharToken(state, "gs")) &&
      (ParseTwoCharToken(state, "dl") || ParseTwoCharToken(state, "da")) &&
      ParseExpression(state)) {
    return true;
  }
  state->parse_state = copy;

  // dynamic_cast, static_cast, const_cast, reinterpret_cast.
  //
  // <expression> ::= (dc | sc | cc | rc) <type> <expression>
  if (ParseCharClass(state, "dscr") && ParseOneCharToken(state, 'c') &&
      ParseType(state) && ParseExpression(state)) {
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

  // typeid(type)
  if (ParseTwoCharToken(state, "ti") && ParseType(state)) {
    return true;
  }
  state->parse_state = copy;

  // typeid(expression)
  if (ParseTwoCharToken(state, "te") && ParseExpression(state)) {
    return true;
  }
  state->parse_state = copy;

  // sizeof type
  if (ParseTwoCharToken(state, "st") && ParseType(state)) {
    return true;
  }
  state->parse_state = copy;

  // alignof(type)
  if (ParseTwoCharToken(state, "at") && ParseType(state)) {
    return true;
  }
  state->parse_state = copy;

  // alignof(expression), a GNU extension
  if (ParseTwoCharToken(state, "az") && ParseExpression(state)) {
    return true;
  }
  state->parse_state = copy;

  // noexcept(expression) appearing as an expression in a dependent signature
  if (ParseTwoCharToken(state, "nx") && ParseExpression(state)) {
    return true;
  }
  state->parse_state = copy;

  // sizeof...(pack)
  //
  // <expression> ::= sZ <template-param>
  //              ::= sZ <function-param>
  if (ParseTwoCharToken(state, "sZ") &&
      (ParseFunctionParam(state) || ParseTemplateParam(state))) {
    return true;
  }
  state->parse_state = copy;

  // sizeof...(pack) captured from an alias template
  //
  // <expression> ::= sP <template-arg>* E
  if (ParseTwoCharToken(state, "sP") && ZeroOrMore(ParseTemplateArg, state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  // Unary folds (... op pack) and (pack op ...).
  //
  // <expression> ::= fl <binary operator-name> <expression>
  //              ::= fr <binary operator-name> <expression>
  if ((ParseTwoCharToken(state, "fl") || ParseTwoCharToken(state, "fr")) &&
      ParseOperatorName(state, nullptr) && ParseExpression(state)) {
    return true;
  }
  state->parse_state = copy;

  // Binary folds (init op ... op pack) and (pack op ... op init).
  //
  // <expression> ::= fL <binary operator-name> <expression> <expression>
  //              ::= fR <binary operator-name> <expression> <expression>
  if ((ParseTwoCharToken(state, "fL") || ParseTwoCharToken(state, "fR")) &&
      ParseOperatorName(state, nullptr) && ParseExpression(state) &&
      ParseExpression(state)) {
    return true;
  }
  state->parse_state = copy;

  // tw <expression>: throw e
  if (ParseTwoCharToken(state, "tw") && ParseExpression(state)) {
    return true;
  }
  state->parse_state = copy;

  // tr: throw (rethrows an exception from the handler that caught it)
  if (ParseTwoCharToken(state, "tr")) return true;

  // Object and pointer member access expressions.
  //
  // <expression> ::= (dt | pt) <expression> <unresolved-name>
  if ((ParseTwoCharToken(state, "dt") || ParseTwoCharToken(state, "pt")) &&
      ParseExpression(state) && ParseUnresolvedName(state)) {
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

  // Vendor extended expressions
  if (ParseOneCharToken(state, 'u') && ParseSourceName(state) &&
      ZeroOrMore(ParseTemplateArg, state) && ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  // <expression> ::= rq <requirement>+ E
  //
  // https://github.com/itanium-cxx-abi/cxx-abi/issues/24
  if (ParseTwoCharToken(state, "rq") && OneOrMore(ParseRequirement, state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  // <expression> ::= rQ <bare-function-type> _ <requirement>+ E
  //
  // https://github.com/itanium-cxx-abi/cxx-abi/issues/24
  if (ParseTwoCharToken(state, "rQ") && ParseBareFunctionType(state) &&
      ParseOneCharToken(state, '_') && OneOrMore(ParseRequirement, state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  return ParseUnresolvedName(state);
}

// <initializer> ::= pi <expression>* E
//               ::= il <braced-expression>* E
//
// The il ... E form is not in the ABI spec but is seen in practice for
// braced-init-lists in new-expressions, which are standard syntax from C++11
// on.
static bool ParseInitializer(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;

  if (ParseTwoCharToken(state, "pi") && ZeroOrMore(ParseExpression, state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;

  if (ParseTwoCharToken(state, "il") &&
      ZeroOrMore(ParseBracedExpression, state) &&
      ParseOneCharToken(state, 'E')) {
    return true;
  }
  state->parse_state = copy;
  return false;
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

  if (ParseOneCharToken(state, 'L')) {
    // There are two special cases in which a literal may or must contain a type
    // without a value.  The first is that both LDnE and LDn0E are valid
    // encodings of nullptr, used in different situations.  Recognize LDnE here,
    // leaving LDn0E to be recognized by the general logic afterward.
    if (ParseThreeCharToken(state, "DnE")) return true;

    // The second special case is a string literal, currently mangled in C++98
    // style as LA<length + 1>_KcE.  This is inadequate to support C++11 and
    // later versions, and the discussion of this problem has not converged.
    //
    // https://github.com/itanium-cxx-abi/cxx-abi/issues/64
    //
    // For now the bare-type mangling is what's used in practice, so we
    // recognize this form and only this form if an array type appears here.
    // Someday we'll probably have to accept a new form of value mangling in
    // LA...E constructs.  (Note also that C++20 allows a wide range of
    // class-type objects as template arguments, so someday their values will be
    // mangled and we'll have to recognize them here too.)
    if (RemainingInput(state)[0] == 'A' /* an array type follows */) {
      if (ParseType(state) && ParseOneCharToken(state, 'E')) return true;
      state->parse_state = copy;
      return false;
    }

    // The merged cast production.
    if (ParseType(state) && ParseExprCastValueAndTrailingE(state)) {
      return true;
    }
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
static bool ParseExprCastValueAndTrailingE(State *state) {
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

  if (ParseFloatNumber(state)) {
    // <float> for ordinary floating-point types
    if (ParseOneCharToken(state, 'E')) return true;

    // <float> _ <float> for complex floating-point types
    if (ParseOneCharToken(state, '_') && ParseFloatNumber(state) &&
        ParseOneCharToken(state, 'E')) {
      return true;
    }
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

// <requirement> ::= X <expression> [N] [R <type-constraint>]
// <requirement> ::= T <type>
// <requirement> ::= Q <constraint-expression>
//
// <constraint-expression> ::= <expression>
//
// https://github.com/itanium-cxx-abi/cxx-abi/issues/24
static bool ParseRequirement(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;

  ParseState copy = state->parse_state;

  if (ParseOneCharToken(state, 'X') && ParseExpression(state) &&
      Optional(ParseOneCharToken(state, 'N')) &&
      // This logic backtracks cleanly if we eat an R but a valid type doesn't
      // follow it.
      (!ParseOneCharToken(state, 'R') || ParseTypeConstraint(state))) {
    return true;
  }
  state->parse_state = copy;

  if (ParseOneCharToken(state, 'T') && ParseType(state)) return true;
  state->parse_state = copy;

  if (ParseOneCharToken(state, 'Q') && ParseExpression(state)) return true;
  state->parse_state = copy;

  return false;
}

// <type-constraint> ::= <name>
static bool ParseTypeConstraint(State *state) {
  return ParseName(state);
}

// <local-name> ::= Z <(function) encoding> E <(entity) name> [<discriminator>]
//              ::= Z <(function) encoding> E s [<discriminator>]
//              ::= Z <(function) encoding> E d [<(parameter) number>] _ <name>
//
// Parsing a common prefix of these two productions together avoids an
// exponential blowup of backtracking.  Parse like:
//   <local-name> := Z <encoding> E <local-name-suffix>
//   <local-name-suffix> ::= s [<discriminator>]
//                       ::= d [<(parameter) number>] _ <name>
//                       ::= <name> [<discriminator>]

static bool ParseLocalNameSuffix(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;

  // <local-name-suffix> ::= d [<(parameter) number>] _ <name>
  if (ParseOneCharToken(state, 'd') &&
      (IsDigit(RemainingInput(state)[0]) || RemainingInput(state)[0] == '_')) {
    int number = -1;
    Optional(ParseNumber(state, &number));
    if (number < -1 || number > 2147483645) {
      // Work around overflow cases.  We do not expect these outside of a fuzzer
      // or other source of adversarial input.  If we do detect overflow here,
      // we'll print {default arg#1}.
      number = -1;
    }
    number += 2;

    // The ::{default arg#1}:: infix must be rendered before the lambda itself,
    // so print this before parsing the rest of the <local-name-suffix>.
    MaybeAppend(state, "::{default arg#");
    MaybeAppendDecimal(state, number);
    MaybeAppend(state, "}::");
    if (ParseOneCharToken(state, '_') && ParseName(state)) return true;

    // On late parse failure, roll back not only the input but also the output,
    // whose trailing NUL was overwritten.
    state->parse_state = copy;
    if (state->parse_state.append) {
      state->out[state->parse_state.out_cur_idx] = '\0';
    }
    return false;
  }
  state->parse_state = copy;

  // <local-name-suffix> ::= <name> [<discriminator>]
  if (MaybeAppend(state, "::") && ParseName(state) &&
      Optional(ParseDiscriminator(state))) {
    return true;
  }
  state->parse_state = copy;
  if (state->parse_state.append) {
    state->out[state->parse_state.out_cur_idx] = '\0';
  }

  // <local-name-suffix> ::= s [<discriminator>]
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

// <discriminator> := _ <digit>
//                 := __ <number (>= 10)> _
static bool ParseDiscriminator(State *state) {
  ComplexityGuard guard(state);
  if (guard.IsTooComplex()) return false;
  ParseState copy = state->parse_state;

  // Both forms start with _ so parse that first.
  if (!ParseOneCharToken(state, '_')) return false;

  // <digit>
  if (ParseDigit(state, nullptr)) return true;

  // _ <number> _
  if (ParseOneCharToken(state, '_') && ParseNumber(state, nullptr) &&
      ParseOneCharToken(state, '_')) {
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
        UpdateHighWaterMark(state);
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
      ReportHighWaterMark(state);
      return false;  // Unconsumed suffix.
    }
    return true;
  }

  ReportHighWaterMark(state);
  return false;
}

static bool Overflowed(const State *state) {
  return state->parse_state.out_cur_idx >= state->out_end_idx;
}

// The demangler entry point.
bool Demangle(const char* mangled, char* out, size_t out_size) {
  if (mangled[0] == '_' && mangled[1] == 'R') {
    return DemangleRustSymbolEncoding(mangled, out, out_size);
  }

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

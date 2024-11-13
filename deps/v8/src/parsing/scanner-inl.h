// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_SCANNER_INL_H_
#define V8_PARSING_SCANNER_INL_H_

#include "src/parsing/keywords-gen.h"
#include "src/parsing/scanner.h"
#include "src/strings/char-predicates-inl.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

// ----------------------------------------------------------------------------
// Keyword Matcher

#define KEYWORDS(KEYWORD_GROUP, KEYWORD)                  \
  KEYWORD_GROUP('a')                                      \
  KEYWORD("async", Token::kAsync)                         \
  KEYWORD("await", Token::kAwait)                         \
  KEYWORD_GROUP('b')                                      \
  KEYWORD("break", Token::kBreak)                         \
  KEYWORD_GROUP('c')                                      \
  KEYWORD("case", Token::kCase)                           \
  KEYWORD("catch", Token::kCatch)                         \
  KEYWORD("class", Token::kClass)                         \
  KEYWORD("const", Token::kConst)                         \
  KEYWORD("continue", Token::kContinue)                   \
  KEYWORD_GROUP('d')                                      \
  KEYWORD("debugger", Token::kDebugger)                   \
  KEYWORD("default", Token::kDefault)                     \
  KEYWORD("delete", Token::kDelete)                       \
  KEYWORD("do", Token::kDo)                               \
  KEYWORD_GROUP('e')                                      \
  KEYWORD("else", Token::kElse)                           \
  KEYWORD("enum", Token::kEnum)                           \
  KEYWORD("export", Token::kExport)                       \
  KEYWORD("extends", Token::kExtends)                     \
  KEYWORD_GROUP('f')                                      \
  KEYWORD("false", Token::kFalseLiteral)                  \
  KEYWORD("finally", Token::kFinally)                     \
  KEYWORD("for", Token::kFor)                             \
  KEYWORD("function", Token::kFunction)                   \
  KEYWORD_GROUP('g')                                      \
  KEYWORD("get", Token::kGet)                             \
  KEYWORD_GROUP('i')                                      \
  KEYWORD("if", Token::kIf)                               \
  KEYWORD("implements", Token::kFutureStrictReservedWord) \
  KEYWORD("import", Token::kImport)                       \
  KEYWORD("in", Token::kIn)                               \
  KEYWORD("instanceof", Token::kInstanceOf)               \
  KEYWORD("interface", Token::kFutureStrictReservedWord)  \
  KEYWORD_GROUP('l')                                      \
  KEYWORD("let", Token::kLet)                             \
  KEYWORD_GROUP('n')                                      \
  KEYWORD("new", Token::kNew)                             \
  KEYWORD("null", Token::kNullLiteral)                    \
  KEYWORD_GROUP('o')                                      \
  KEYWORD("of", Token::kOf)                               \
  KEYWORD_GROUP('p')                                      \
  KEYWORD("package", Token::kFutureStrictReservedWord)    \
  KEYWORD("private", Token::kFutureStrictReservedWord)    \
  KEYWORD("protected", Token::kFutureStrictReservedWord)  \
  KEYWORD("public", Token::kFutureStrictReservedWord)     \
  KEYWORD_GROUP('r')                                      \
  KEYWORD("return", Token::kReturn)                       \
  KEYWORD_GROUP('s')                                      \
  KEYWORD("set", Token::kSet)                             \
  KEYWORD("static", Token::kStatic)                       \
  KEYWORD("super", Token::kSuper)                         \
  KEYWORD("switch", Token::kSwitch)                       \
  KEYWORD_GROUP('t')                                      \
  KEYWORD("this", Token::kThis)                           \
  KEYWORD("throw", Token::kThrow)                         \
  KEYWORD("true", Token::kTrueLiteral)                    \
  KEYWORD("try", Token::kTry)                             \
  KEYWORD("typeof", Token::kTypeOf)                       \
  KEYWORD_GROUP('u')                                      \
  KEYWORD("using", Token::kUsing)                         \
  KEYWORD_GROUP('v')                                      \
  KEYWORD("var", Token::kVar)                             \
  KEYWORD("void", Token::kVoid)                           \
  KEYWORD_GROUP('w')                                      \
  KEYWORD("while", Token::kWhile)                         \
  KEYWORD("with", Token::kWith)                           \
  KEYWORD_GROUP('y')                                      \
  KEYWORD("yield", Token::kYield)

constexpr bool IsKeywordStart(char c) {
#define KEYWORD_GROUP_CHECK(ch) c == ch ||
#define KEYWORD_CHECK(keyword, token)
  return KEYWORDS(KEYWORD_GROUP_CHECK, KEYWORD_CHECK) /* || */ false;
#undef KEYWORD_GROUP_CHECK
#undef KEYWORD_CHECK
}

V8_INLINE Token::Value KeywordOrIdentifierToken(const uint8_t* input,
                                                int input_length) {
  DCHECK_GE(input_length, 1);
  return PerfectKeywordHash::GetToken(reinterpret_cast<const char*>(input),
                                      input_length);
}

// Recursive constexpr template magic to check if a character is in a given
// string.
template <int N>
constexpr bool IsInString(const char (&s)[N], char c, size_t i = 0) {
  return i >= N ? false : s[i] == c ? true : IsInString(s, c, i + 1);
}

inline constexpr bool CanBeKeywordCharacter(char c) {
  return IsInString(
#define KEYWORD_GROUP_CASE(ch)  // Nothing
#define KEYWORD(keyword, token) keyword
      // Use C string literal concatenation ("a" "b" becomes "ab") to build one
      // giant string containing all the keywords.
      KEYWORDS(KEYWORD_GROUP_CASE, KEYWORD)
#undef KEYWORD
#undef KEYWORD_GROUP_CASE
          ,
      c);
}

// Make sure tokens are stored as a single byte.
static_assert(sizeof(Token::Value) == 1);

// Get the shortest token that this character starts, the token may change
// depending on subsequent characters.
constexpr Token::Value GetOneCharToken(char c) {
  // clang-format off
  return
      c == '(' ? Token::kLeftParen :
      c == ')' ? Token::kRightParen :
      c == '{' ? Token::kLeftBrace :
      c == '}' ? Token::kRightBrace :
      c == '[' ? Token::kLeftBracket :
      c == ']' ? Token::kRightBracket :
      c == '?' ? Token::kConditional :
      c == ':' ? Token::kColon :
      c == ';' ? Token::kSemicolon :
      c == ',' ? Token::kComma :
      c == '.' ? Token::kPeriod :
      c == '|' ? Token::kBitOr :
      c == '&' ? Token::kBitAnd :
      c == '^' ? Token::kBitXor :
      c == '~' ? Token::kBitNot :
      c == '!' ? Token::kNot :
      c == '<' ? Token::kLessThan :
      c == '>' ? Token::kGreaterThan :
      c == '%' ? Token::kMod :
      c == '=' ? Token::kAssign :
      c == '+' ? Token::kAdd :
      c == '-' ? Token::kSub :
      c == '*' ? Token::kMul :
      c == '/' ? Token::kDiv :
      c == '#' ? Token::kPrivateName :
      c == '"' ? Token::kString :
      c == '\'' ? Token::kString :
      c == '`' ? Token::kTemplateSpan :
      c == '\\' ? Token::kIdentifier :
      // Whitespace or line terminator
      c == ' ' ? Token::kWhitespace :
      c == '\t' ? Token::kWhitespace :
      c == '\v' ? Token::kWhitespace :
      c == '\f' ? Token::kWhitespace :
      c == '\r' ? Token::kWhitespace :
      c == '\n' ? Token::kWhitespace :
      // IsDecimalDigit must be tested before IsAsciiIdentifier
      IsDecimalDigit(c) ? Token::kNumber :
      IsAsciiIdentifier(c) ? Token::kIdentifier :
      Token::kIllegal;
  // clang-format on
}

// Table of one-character tokens, by character (0x00..0x7F only).
static const constexpr Token::Value one_char_tokens[128] = {
#define CALL_GET_SCAN_FLAGS(N) GetOneCharToken(N),
    INT_0_TO_127_LIST(CALL_GET_SCAN_FLAGS)
#undef CALL_GET_SCAN_FLAGS
};

#undef KEYWORDS

V8_INLINE Token::Value Scanner::ScanIdentifierOrKeyword() {
  next().literal_chars.Start();
  return ScanIdentifierOrKeywordInner();
}

// Character flags for the fast path of scanning a keyword or identifier token.
enum class ScanFlags : uint8_t {
  kTerminatesLiteral = 1 << 0,
  // "Cannot" rather than "can" so that this flag can be ORed together across
  // multiple characters.
  kCannotBeKeyword = 1 << 1,
  kCannotBeKeywordStart = 1 << 2,
  kStringTerminator = 1 << 3,
  kIdentifierNeedsSlowPath = 1 << 4,
  kMultilineCommentCharacterNeedsSlowPath = 1 << 5,
};
constexpr uint8_t GetScanFlags(char c) {
  return
      // Keywords are all lowercase and only contain letters.
      // Note that non-identifier characters do not set this flag, so
      // that it plays well with kTerminatesLiteral.
      (IsAsciiIdentifier(c) && !CanBeKeywordCharacter(c)
           ? static_cast<uint8_t>(ScanFlags::kCannotBeKeyword)
           : 0) |
      (IsKeywordStart(c)
           ? 0
           : static_cast<uint8_t>(ScanFlags::kCannotBeKeywordStart)) |
      // Anything that isn't an identifier character will terminate the
      // literal, or at least terminates the literal fast path processing
      // (like an escape).
      (!IsAsciiIdentifier(c)
           ? static_cast<uint8_t>(ScanFlags::kTerminatesLiteral)
           : 0) |
      // Possible string termination characters.
      ((c == '\'' || c == '"' || c == '\n' || c == '\r' || c == '\\')
           ? static_cast<uint8_t>(ScanFlags::kStringTerminator)
           : 0) |
      // Escapes are processed on the slow path.
      (c == '\\' ? static_cast<uint8_t>(ScanFlags::kIdentifierNeedsSlowPath)
                 : 0) |
      // Newlines and * are interesting characters for multiline comment
      // scanning.
      (c == '\n' || c == '\r' || c == '*'
           ? static_cast<uint8_t>(
                 ScanFlags::kMultilineCommentCharacterNeedsSlowPath)
           : 0);
}
inline bool TerminatesLiteral(uint8_t scan_flags) {
  return (scan_flags & static_cast<uint8_t>(ScanFlags::kTerminatesLiteral));
}
inline bool CanBeKeyword(uint8_t scan_flags) {
  return !(scan_flags & static_cast<uint8_t>(ScanFlags::kCannotBeKeyword));
}
inline bool IdentifierNeedsSlowPath(uint8_t scan_flags) {
  return (scan_flags &
          static_cast<uint8_t>(ScanFlags::kIdentifierNeedsSlowPath));
}
inline bool MultilineCommentCharacterNeedsSlowPath(uint8_t scan_flags) {
  return (scan_flags & static_cast<uint8_t>(
                           ScanFlags::kMultilineCommentCharacterNeedsSlowPath));
}
inline bool MayTerminateString(uint8_t scan_flags) {
  return (scan_flags & static_cast<uint8_t>(ScanFlags::kStringTerminator));
}
// Table of precomputed scan flags for the 128 ASCII characters, for branchless
// flag calculation during the scan.
static constexpr const uint8_t character_scan_flags[128] = {
#define CALL_GET_SCAN_FLAGS(N) GetScanFlags(N),
    INT_0_TO_127_LIST(CALL_GET_SCAN_FLAGS)
#undef CALL_GET_SCAN_FLAGS
};

inline bool CharCanBeKeyword(base::uc32 c) {
  return static_cast<uint32_t>(c) < arraysize(character_scan_flags) &&
         CanBeKeyword(character_scan_flags[c]);
}

V8_INLINE Token::Value Scanner::ScanIdentifierOrKeywordInner() {
  DCHECK(IsIdentifierStart(c0_));
  bool escaped = false;
  bool can_be_keyword = true;

  static_assert(arraysize(character_scan_flags) == kMaxAscii + 1);
  if (V8_LIKELY(static_cast<uint32_t>(c0_) <= kMaxAscii)) {
    if (V8_LIKELY(c0_ != '\\')) {
      uint8_t scan_flags = character_scan_flags[c0_];
      DCHECK(!TerminatesLiteral(scan_flags));
      static_assert(static_cast<uint8_t>(ScanFlags::kCannotBeKeywordStart) ==
                    static_cast<uint8_t>(ScanFlags::kCannotBeKeyword) << 1);
      scan_flags >>= 1;
      // Make sure the shifting above doesn't set IdentifierNeedsSlowPath.
      // Otherwise we'll fall into the slow path after scanning the identifier.
      DCHECK(!IdentifierNeedsSlowPath(scan_flags));
      AddLiteralChar(static_cast<char>(c0_));
      AdvanceUntil([this, &scan_flags](base::uc32 c0) {
        if (V8_UNLIKELY(static_cast<uint32_t>(c0) > kMaxAscii)) {
          // A non-ascii character means we need to drop through to the slow
          // path.
          // TODO(leszeks): This would be most efficient as a goto to the slow
          // path, check codegen and maybe use a bool instead.
          scan_flags |=
              static_cast<uint8_t>(ScanFlags::kIdentifierNeedsSlowPath);
          return true;
        }
        uint8_t char_flags = character_scan_flags[c0];
        scan_flags |= char_flags;
        if (TerminatesLiteral(char_flags)) {
          return true;
        } else {
          AddLiteralChar(static_cast<char>(c0));
          return false;
        }
      });

      if (V8_LIKELY(!IdentifierNeedsSlowPath(scan_flags))) {
        if (!CanBeKeyword(scan_flags)) return Token::kIdentifier;
        // Could be a keyword or identifier.
        base::Vector<const uint8_t> chars =
            next().literal_chars.one_byte_literal();
        return KeywordOrIdentifierToken(chars.begin(), chars.length());
      }

      can_be_keyword = CanBeKeyword(scan_flags);
    } else {
      // Special case for escapes at the start of an identifier.
      escaped = true;
      base::uc32 c = ScanIdentifierUnicodeEscape();
      DCHECK(!IsIdentifierStart(Invalid()));
      if (c == '\\' || !IsIdentifierStart(c)) {
        return Token::kIllegal;
      }
      AddLiteralChar(c);
      can_be_keyword = CharCanBeKeyword(c);
    }
  }

  return ScanIdentifierOrKeywordInnerSlow(escaped, can_be_keyword);
}

V8_INLINE Token::Value Scanner::SkipWhiteSpace() {
  if (!IsWhiteSpaceOrLineTerminator(c0_)) return Token::kIllegal;

  if (!next().after_line_terminator && unibrow::IsLineTerminator(c0_)) {
    next().after_line_terminator = true;
  }

  // Advance as long as character is a WhiteSpace or LineTerminator.
  base::uc32 hint = ' ';
  AdvanceUntil([this, &hint](base::uc32 c0) {
    if (V8_LIKELY(c0 == hint)) return false;
    if (IsWhiteSpaceOrLineTerminator(c0)) {
      if (!next().after_line_terminator && unibrow::IsLineTerminator(c0)) {
        next().after_line_terminator = true;
      }
      hint = c0;
      return false;
    }
    return true;
  });

  return Token::kWhitespace;
}

V8_INLINE Token::Value Scanner::ScanSingleToken() {
  Token::Value token;
  do {
    next().location.beg_pos = source_pos();

    if (V8_LIKELY(static_cast<unsigned>(c0_) <= kMaxAscii)) {
      token = one_char_tokens[c0_];

      switch (token) {
        case Token::kLeftParen:
        case Token::kRightParen:
        case Token::kLeftBrace:
        case Token::kRightBrace:
        case Token::kLeftBracket:
        case Token::kRightBracket:
        case Token::kColon:
        case Token::kSemicolon:
        case Token::kComma:
        case Token::kBitNot:
        case Token::kIllegal:
          // One character tokens.
          return Select(token);

        case Token::kConditional:
          // ? ?. ?? ??=
          Advance();
          if (c0_ == '.') {
            Advance();
            if (!IsDecimalDigit(c0_)) return Token::kQuestionPeriod;
            PushBack('.');
          } else if (c0_ == '?') {
            return Select('=', Token::kAssignNullish, Token::kNullish);
          }
          return Token::kConditional;

        case Token::kString:
          return ScanString();

        case Token::kLessThan:
          // < <= << <<= <!--
          Advance();
          if (c0_ == '=') return Select(Token::kLessThanEq);
          if (c0_ == '<') return Select('=', Token::kAssignShl, Token::kShl);
          if (c0_ == '!') {
            token = ScanHtmlComment();
            continue;
          }
          return Token::kLessThan;

        case Token::kGreaterThan:
          // > >= >> >>= >>> >>>=
          Advance();
          if (c0_ == '=') return Select(Token::kGreaterThanEq);
          if (c0_ == '>') {
            // >> >>= >>> >>>=
            Advance();
            if (c0_ == '=') return Select(Token::kAssignSar);
            if (c0_ == '>') return Select('=', Token::kAssignShr, Token::kShr);
            return Token::kSar;
          }
          return Token::kGreaterThan;

        case Token::kAssign:
          // = == === =>
          Advance();
          if (c0_ == '=') return Select('=', Token::kEqStrict, Token::kEq);
          if (c0_ == '>') return Select(Token::kArrow);
          return Token::kAssign;

        case Token::kNot:
          // ! != !==
          Advance();
          if (c0_ == '=')
            return Select('=', Token::kNotEqStrict, Token::kNotEq);
          return Token::kNot;

        case Token::kAdd:
          // + ++ +=
          Advance();
          if (c0_ == '+') return Select(Token::kInc);
          if (c0_ == '=') return Select(Token::kAssignAdd);
          return Token::kAdd;

        case Token::kSub:
          // - -- --> -=
          Advance();
          if (c0_ == '-') {
            Advance();
            if (c0_ == '>' && next().after_line_terminator) {
              // For compatibility with SpiderMonkey, we skip lines that
              // start with an HTML comment end '-->'.
              token = SkipSingleHTMLComment();
              continue;
            }
            return Token::kDec;
          }
          if (c0_ == '=') return Select(Token::kAssignSub);
          return Token::kSub;

        case Token::kMul:
          // * *=
          Advance();
          if (c0_ == '*') return Select('=', Token::kAssignExp, Token::kExp);
          if (c0_ == '=') return Select(Token::kAssignMul);
          return Token::kMul;

        case Token::kMod:
          // % %=
          return Select('=', Token::kAssignMod, Token::kMod);

        case Token::kDiv:
          // /  // /* /=
          Advance();
          if (c0_ == '/') {
            base::uc32 c = Peek();
            if (c == '#' || c == '@') {
              Advance();
              Advance();
              token = SkipMagicComment(c);
              continue;
            }
            token = SkipSingleLineComment();
            continue;
          }
          if (c0_ == '*') {
            token = SkipMultiLineComment();
            continue;
          }
          if (c0_ == '=') return Select(Token::kAssignDiv);
          return Token::kDiv;

        case Token::kBitAnd:
          // & && &= &&=
          Advance();
          if (c0_ == '&') return Select('=', Token::kAssignAnd, Token::kAnd);
          if (c0_ == '=') return Select(Token::kAssignBitAnd);
          return Token::kBitAnd;

        case Token::kBitOr:
          // | || |= ||=
          Advance();
          if (c0_ == '|') return Select('=', Token::kAssignOr, Token::kOr);
          if (c0_ == '=') return Select(Token::kAssignBitOr);
          return Token::kBitOr;

        case Token::kBitXor:
          // ^ ^=
          return Select('=', Token::kAssignBitXor, Token::kBitXor);

        case Token::kPeriod:
          // . Number
          Advance();
          if (IsDecimalDigit(c0_)) return ScanNumber(true);
          if (c0_ == '.') {
            if (Peek() == '.') {
              Advance();
              Advance();
              return Token::kEllipsis;
            }
          }
          return Token::kPeriod;

        case Token::kTemplateSpan:
          Advance();
          return ScanTemplateSpan();

        case Token::kPrivateName:
          if (source_pos() == 0 && Peek() == '!') {
            token = SkipSingleLineComment();
            continue;
          }
          return ScanPrivateName();

        case Token::kWhitespace:
          token = SkipWhiteSpace();
          continue;

        case Token::kNumber:
          return ScanNumber(false);

        case Token::kIdentifier:
          return ScanIdentifierOrKeyword();

        default:
          UNREACHABLE();
      }
    }

    if (IsIdentifierStart(c0_) ||
        (CombineSurrogatePair() && IsIdentifierStart(c0_))) {
      return ScanIdentifierOrKeyword();
    }
    if (c0_ == kEndOfInput) {
      return source_->has_parser_error() ? Token::kIllegal : Token::kEos;
    }
    token = SkipWhiteSpace();

    // Continue scanning for tokens as long as we're just skipping whitespace.
  } while (token == Token::kWhitespace);

  return token;
}

void Scanner::Scan(TokenDesc* next_desc) {
  DCHECK_EQ(next_desc, &next());

  next_desc->token = ScanSingleToken();
  DCHECK_IMPLIES(has_parser_error(), next_desc->token == Token::kIllegal);
  next_desc->location.end_pos = source_pos();

#ifdef DEBUG
  SanityCheckTokenDesc(current());
  SanityCheckTokenDesc(next());
  SanityCheckTokenDesc(next_next());
  SanityCheckTokenDesc(next_next_next());
#endif
}

void Scanner::Scan() { Scan(next_); }

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_SCANNER_INL_H_

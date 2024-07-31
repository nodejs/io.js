// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Features shared by parsing and pre-parsing scanners.

#ifndef V8_PARSING_SCANNER_H_
#define V8_PARSING_SCANNER_H_

#include <algorithm>
#include <memory>

#include "src/base/logging.h"
#include "src/base/strings.h"
#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/parsing/literal-buffer.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/token.h"
#include "src/regexp/regexp-flags.h"
#include "src/strings/char-predicates.h"
#include "src/strings/unicode.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

class AstRawString;
class AstValueFactory;
class ExternalOneByteString;
class ExternalTwoByteString;
class ParserRecorder;
class RuntimeCallStats;
class Zone;

// ---------------------------------------------------------------------
// Buffered stream of UTF-16 code units, using an internal UTF-16 buffer.
// A code unit is a 16 bit value representing either a 16 bit code point
// or one part of a surrogate pair that make a single 21 bit code point.
class Utf16CharacterStream {
 public:
  static constexpr base::uc32 kEndOfInput = static_cast<base::uc32>(-1);

  virtual ~Utf16CharacterStream() = default;

  V8_INLINE void set_parser_error() {
    // source_pos() returns one previous position of the cursor.
    // Offset 1 cancels this out and makes it return exactly buffer_end_.
    buffer_cursor_ = buffer_end_ + 1;
    has_parser_error_ = true;
  }
  V8_INLINE void reset_parser_error_flag() { has_parser_error_ = false; }
  V8_INLINE bool has_parser_error() const { return has_parser_error_; }

  inline base::uc32 Peek() {
    if (V8_LIKELY(buffer_cursor_ < buffer_end_)) {
      return static_cast<base::uc32>(*buffer_cursor_);
    } else if (ReadBlockChecked(pos())) {
      return static_cast<base::uc32>(*buffer_cursor_);
    } else {
      return kEndOfInput;
    }
  }

  // Returns and advances past the next UTF-16 code unit in the input
  // stream. If there are no more code units it returns kEndOfInput.
  inline base::uc32 Advance() {
    base::uc32 result = Peek();
    buffer_cursor_++;
    return result;
  }

  // Returns and advances past the next UTF-16 code unit in the input stream
  // that meets the checks requirement. If there are no more code units it
  // returns kEndOfInput.
  template <typename FunctionType>
  V8_INLINE base::uc32 AdvanceUntil(FunctionType check) {
    while (true) {
      auto next_cursor_pos =
          std::find_if(buffer_cursor_, buffer_end_, [&check](uint16_t raw_c0_) {
            base::uc32 c0_ = static_cast<base::uc32>(raw_c0_);
            return check(c0_);
          });

      if (next_cursor_pos == buffer_end_) {
        buffer_cursor_ = buffer_end_;
        if (!ReadBlockChecked(pos())) {
          buffer_cursor_++;
          return kEndOfInput;
        }
      } else {
        buffer_cursor_ = next_cursor_pos + 1;
        return static_cast<base::uc32>(*next_cursor_pos);
      }
    }
  }

  // Go back one by one character in the input stream.
  // This undoes the most recent Advance().
  inline void Back() {
    // The common case - if the previous character is within
    // buffer_start_ .. buffer_end_ will be handles locally.
    // Otherwise, a new block is requested.
    if (V8_LIKELY(buffer_cursor_ > buffer_start_)) {
      buffer_cursor_--;
    } else {
      ReadBlockChecked(pos() - 1);
    }
  }

  inline size_t pos() const {
    return buffer_pos_ + (buffer_cursor_ - buffer_start_);
  }

  inline void Seek(size_t pos) {
    if (V8_LIKELY(pos >= buffer_pos_ &&
                  pos < (buffer_pos_ + (buffer_end_ - buffer_start_)))) {
      buffer_cursor_ = buffer_start_ + (pos - buffer_pos_);
    } else {
      ReadBlockChecked(pos);
    }
  }

  // Returns true if the stream could access the V8 heap after construction.
  bool can_be_cloned_for_parallel_access() const {
    return can_be_cloned() && !can_access_heap();
  }

  // Returns true if the stream can be cloned with Clone.
  // TODO(rmcilroy): Remove this once ChunkedStreams can be cloned.
  virtual bool can_be_cloned() const = 0;

  // Clones the character stream to enable another independent scanner to access
  // the same underlying stream.
  virtual std::unique_ptr<Utf16CharacterStream> Clone() const = 0;

  // Returns true if the stream could access the V8 heap after construction.
  virtual bool can_access_heap() const = 0;

  RuntimeCallStats* runtime_call_stats() const { return runtime_call_stats_; }
  void set_runtime_call_stats(RuntimeCallStats* runtime_call_stats) {
    runtime_call_stats_ = runtime_call_stats;
  }

 protected:
  Utf16CharacterStream(const uint16_t* buffer_start,
                       const uint16_t* buffer_cursor,
                       const uint16_t* buffer_end, size_t buffer_pos)
      : buffer_start_(buffer_start),
        buffer_cursor_(buffer_cursor),
        buffer_end_(buffer_end),
        buffer_pos_(buffer_pos) {}
  Utf16CharacterStream() : Utf16CharacterStream(nullptr, nullptr, nullptr, 0) {}

  bool ReadBlockChecked(size_t position) {
    // The callers of this method (Back/Back2/Seek) should handle the easy
    // case (seeking within the current buffer), and we should only get here
    // if we actually require new data.
    // (This is really an efficiency check, not a correctness invariant.)
    DCHECK(position < buffer_pos_ ||
           position >= buffer_pos_ + (buffer_end_ - buffer_start_));

    bool success = !has_parser_error() && ReadBlock(position);

    // Post-conditions: 1, We should always be at the right position.
    //                  2, Cursor should be inside the buffer.
    //                  3, We should have more characters available iff success.
    DCHECK_EQ(pos(), position);
    DCHECK_LE(buffer_cursor_, buffer_end_);
    DCHECK_LE(buffer_start_, buffer_cursor_);
    DCHECK_EQ(success, buffer_cursor_ < buffer_end_);
    return success;
  }

  // Read more data, and update buffer_*_ to point to it.
  // Returns true if more data was available.
  //
  // ReadBlock(position) may modify any of the buffer_*_ members, but must make
  // sure that the result of pos() becomes |position|.
  //
  // Examples:
  // - a stream could either fill a separate buffer. Then buffer_start_ and
  //   buffer_cursor_ would point to the beginning of the buffer, and
  //   buffer_pos would be the old pos().
  // - a stream with existing buffer chunks would set buffer_start_ and
  //   buffer_end_ to cover the full chunk, and then buffer_cursor_ would
  //   point into the middle of the buffer, while buffer_pos_ would describe
  //   the start of the buffer.
  virtual bool ReadBlock(size_t position) = 0;

  // Fields describing the location of the current buffer physically in memory,
  // and semantically within the source string.
  //
  //                  0              buffer_pos_   pos()
  //                  |                        |   |
  //                  v________________________v___v_____________
  //                  |                        |        |        |
  //   Source string: |                        | Buffer |        |
  //                  |________________________|________|________|
  //                                           ^   ^    ^
  //                                           |   |    |
  //                   Pointers:   buffer_start_   |    buffer_end_
  //                                         buffer_cursor_
  const uint16_t* buffer_start_;
  const uint16_t* buffer_cursor_;
  const uint16_t* buffer_end_;
  size_t buffer_pos_;
  RuntimeCallStats* runtime_call_stats_;
  bool has_parser_error_ = false;
};

// ----------------------------------------------------------------------------
// JavaScript Scanner.

class V8_EXPORT_PRIVATE Scanner {
 public:
  // Scoped helper for a re-settable bookmark.
  class V8_EXPORT_PRIVATE V8_NODISCARD BookmarkScope {
   public:
    explicit BookmarkScope(Scanner* scanner)
        : scanner_(scanner),
          bookmark_(kNoBookmark),
          had_parser_error_(scanner->has_parser_error()) {
      DCHECK_NOT_NULL(scanner_);
    }
    ~BookmarkScope() = default;
    BookmarkScope(const BookmarkScope&) = delete;
    BookmarkScope& operator=(const BookmarkScope&) = delete;

    void Set(size_t bookmark);
    void Apply();
    bool HasBeenSet() const;
    bool HasBeenApplied() const;

   private:
    static const size_t kNoBookmark;
    static const size_t kBookmarkWasApplied;

    Scanner* scanner_;
    size_t bookmark_;
    bool had_parser_error_;
  };

  // Sets the Scanner into an error state to stop further scanning and terminate
  // the parsing by only returning kIllegal tokens after that.
  V8_INLINE void set_parser_error() {
    if (!has_parser_error()) {
      c0_ = kEndOfInput;
      source_->set_parser_error();
      for (TokenDesc& desc : token_storage_) {
        if (desc.token != Token::kUninitialized) desc.token = Token::kIllegal;
      }
    }
  }
  V8_INLINE void reset_parser_error_flag() {
    source_->reset_parser_error_flag();
  }
  V8_INLINE bool has_parser_error() const {
    return source_->has_parser_error();
  }

  // Representation of an interval of source positions.
  struct Location {
    Location(int b, int e) : beg_pos(b), end_pos(e) { }
    Location() : beg_pos(0), end_pos(0) { }

    int length() const { return end_pos - beg_pos; }
    bool IsValid() const { return base::IsInRange(beg_pos, 0, end_pos); }

    static Location invalid() { return Location(-1, 0); }

    int beg_pos;
    int end_pos;
  };

  // -1 is outside of the range of any real source code.
  static constexpr base::uc32 kEndOfInput = Utf16CharacterStream::kEndOfInput;
  static constexpr base::uc32 kInvalidSequence = static_cast<base::uc32>(-1);

  static constexpr base::uc32 Invalid() { return Scanner::kInvalidSequence; }
  static bool IsInvalid(base::uc32 c);

  explicit Scanner(Utf16CharacterStream* source, UnoptimizedCompileFlags flags);

  void Initialize();

  // Returns the next token and advances input.
  Token::Value Next();
  // Returns the token following peek()
  Token::Value PeekAhead();
  // Returns the current token again.
  Token::Value current_token() const { return current().token; }

  // Returns the location information for the current token
  // (the token last returned by Next()).
  const Location& location() const { return current().location; }

  // This error is specifically an invalid hex or unicode escape sequence.
  bool has_error() const { return scanner_error_ != MessageTemplate::kNone; }
  MessageTemplate error() const { return scanner_error_; }
  const Location& error_location() const { return scanner_error_location_; }

  bool has_invalid_template_escape() const {
    return current().invalid_template_escape_message != MessageTemplate::kNone;
  }
  MessageTemplate invalid_template_escape_message() const {
    DCHECK(has_invalid_template_escape());
    return current().invalid_template_escape_message;
  }

  void clear_invalid_template_escape_message() {
    DCHECK(has_invalid_template_escape());
    current_->invalid_template_escape_message = MessageTemplate::kNone;
  }

  Location invalid_template_escape_location() const {
    DCHECK(has_invalid_template_escape());
    return current().invalid_template_escape_location;
  }

  // Similar functions for the upcoming token.

  // One token look-ahead (past the token returned by Next()).
  Token::Value peek() const { return next().token; }

  const Location& peek_location() const { return next().location; }

  bool literal_contains_escapes() const {
    return LiteralContainsEscapes(current());
  }

  bool next_literal_contains_escapes() const {
    return LiteralContainsEscapes(next());
  }

  const AstRawString* CurrentSymbol(AstValueFactory* ast_value_factory) const;

  const AstRawString* NextSymbol(AstValueFactory* ast_value_factory) const;
  const AstRawString* CurrentRawSymbol(
      AstValueFactory* ast_value_factory) const;

  double DoubleValue();
  base::Vector<const uint8_t> BigIntLiteral() const {
    return literal_one_byte_string();
  }

  const char* CurrentLiteralAsCString(Zone* zone) const;

  inline bool CurrentMatches(Token::Value token) const {
    DCHECK(Token::IsKeyword(token));
    return current().token == token;
  }

  template <size_t N>
  bool NextLiteralExactlyEquals(const char (&s)[N]) {
    DCHECK(next().CanAccessLiteral());
    // The length of the token is used to make sure the literal equals without
    // taking escape sequences (e.g., "use \x73trict") or line continuations
    // (e.g., "use \(newline) strict") into account.
    if (!is_next_literal_one_byte()) return false;
    if (peek_location().length() != N + 1) return false;

    base::Vector<const uint8_t> next = next_literal_one_byte_string();
    const char* chars = reinterpret_cast<const char*>(next.begin());
    return next.length() == N - 1 && strncmp(s, chars, N - 1) == 0;
  }

  template <size_t N>
  bool CurrentLiteralEquals(const char (&s)[N]) {
    DCHECK(current().CanAccessLiteral());
    if (!is_literal_one_byte()) return false;

    base::Vector<const uint8_t> current = literal_one_byte_string();
    const char* chars = reinterpret_cast<const char*>(current.begin());
    return current.length() == N - 1 && strncmp(s, chars, N - 1) == 0;
  }

  // Returns the location of the last seen octal literal.
  Location octal_position() const { return octal_pos_; }
  void clear_octal_position() {
    octal_pos_ = Location::invalid();
    octal_message_ = MessageTemplate::kNone;
  }
  MessageTemplate octal_message() const { return octal_message_; }

  // Returns the value of the last smi that was scanned.
  uint32_t smi_value() const { return current().smi_value_; }

  // Seek forward to the given position.  This operation does not
  // work in general, for instance when there are pushed back
  // characters, but works for seeking forward until simple delimiter
  // tokens, which is what it is used for.
  void SeekForward(int pos);

  // Returns true if there was a line terminator before the peek'ed token,
  // possibly inside a multi-line comment.
  bool HasLineTerminatorBeforeNext() const {
    return next().after_line_terminator;
  }

  bool HasLineTerminatorAfterNext() {
    Token::Value ensure_next_next = PeekAhead();
    USE(ensure_next_next);
    return next_next().after_line_terminator;
  }

  // Scans the input as a regular expression pattern, next token must be /(=).
  // Returns true if a pattern is scanned.
  bool ScanRegExpPattern();
  // Scans the input as regular expression flags. Returns the flags on success.
  base::Optional<RegExpFlags> ScanRegExpFlags();

  // Scans the input as a template literal
  Token::Value ScanTemplateContinuation() {
    DCHECK_EQ(next().token, Token::kRightBrace);
    DCHECK_EQ(source_pos() - 1, next().location.beg_pos);
    return ScanTemplateSpan();
  }

  template <typename IsolateT>
  Handle<String> SourceUrl(IsolateT* isolate) const;
  template <typename IsolateT>
  Handle<String> SourceMappingUrl(IsolateT* isolate) const;

  bool SawSourceMappingUrlMagicCommentAtSign() const {
    return saw_source_mapping_url_magic_comment_at_sign_;
  }

  bool SawMagicCommentCompileHintsAll() const {
    return saw_magic_comment_compile_hints_all_;
  }

  bool FoundHtmlComment() const { return found_html_comment_; }

  const Utf16CharacterStream* stream() const { return source_; }

 private:
  // Scoped helper for saving & restoring scanner error state.
  // This is used for tagged template literals, in which normally forbidden
  // escape sequences are allowed.
  class ErrorState;

  // The current and look-ahead token.
  struct TokenDesc {
    Location location = {0, 0};
    LiteralBuffer literal_chars;
    LiteralBuffer raw_literal_chars;
    Token::Value token = Token::kUninitialized;
    MessageTemplate invalid_template_escape_message = MessageTemplate::kNone;
    Location invalid_template_escape_location;
    uint32_t smi_value_ = 0;
    bool after_line_terminator = false;

#ifdef DEBUG
    bool CanAccessLiteral() const {
      return token == Token::kPrivateName || token == Token::kIllegal ||
             token == Token::kEscapedKeyword ||
             token == Token::kUninitialized || token == Token::kRegExpLiteral ||
             base::IsInRange(token, Token::kNumber, Token::kString) ||
             Token::IsAnyIdentifier(token) || Token::IsKeyword(token) ||
             base::IsInRange(token, Token::kTemplateSpan, Token::kTemplateTail);
    }
    bool CanAccessRawLiteral() const {
      return token == Token::kIllegal || token == Token::kUninitialized ||
             base::IsInRange(token, Token::kTemplateSpan, Token::kTemplateTail);
    }
#endif  // DEBUG
  };

  enum NumberKind {
    IMPLICIT_OCTAL,
    BINARY,
    OCTAL,
    HEX,
    DECIMAL,
    DECIMAL_WITH_LEADING_ZERO
  };

  inline bool IsValidBigIntKind(NumberKind kind) {
    return base::IsInRange(kind, BINARY, DECIMAL);
  }

  inline bool IsDecimalNumberKind(NumberKind kind) {
    return base::IsInRange(kind, DECIMAL, DECIMAL_WITH_LEADING_ZERO);
  }

  static const int kCharacterLookaheadBufferSize = 1;
  static const int kMaxAscii = 127;

  // Scans octal escape sequence. Also accepts "\0" decimal escape sequence.
  template <bool capture_raw>
  base::uc32 ScanOctalEscape(base::uc32 c, int length);

  // Call this after setting source_ to the input.
  void Init() {
    // Set c0_ (one character ahead)
    static_assert(kCharacterLookaheadBufferSize == 1);
    Advance();

    current_ = &token_storage_[0];
    next_ = &token_storage_[1];
    next_next_ = &token_storage_[2];

    found_html_comment_ = false;
    scanner_error_ = MessageTemplate::kNone;
  }

  void ReportScannerError(const Location& location, MessageTemplate error) {
    if (has_error()) return;
    scanner_error_ = error;
    scanner_error_location_ = location;
  }

  void ReportScannerError(int pos, MessageTemplate error) {
    if (has_error()) return;
    scanner_error_ = error;
    scanner_error_location_ = Location(pos, pos + 1);
  }

  // Seek to the next_ token at the given position.
  void SeekNext(size_t position);

  V8_INLINE void AddLiteralChar(base::uc32 c) {
    next().literal_chars.AddChar(c);
  }

  V8_INLINE void AddLiteralChar(char c) { next().literal_chars.AddChar(c); }

  V8_INLINE void AddRawLiteralChar(base::uc32 c) {
    next().raw_literal_chars.AddChar(c);
  }

  V8_INLINE void AddLiteralCharAdvance() {
    AddLiteralChar(c0_);
    Advance();
  }

  // Low-level scanning support.
  template <bool capture_raw = false>
  void Advance() {
    if (capture_raw) {
      AddRawLiteralChar(c0_);
    }
    c0_ = source_->Advance();
  }

  template <typename FunctionType>
  V8_INLINE void AdvanceUntil(FunctionType check) {
    c0_ = source_->AdvanceUntil(check);
  }

  bool CombineSurrogatePair() {
    DCHECK(!unibrow::Utf16::IsLeadSurrogate(kEndOfInput));
    if (unibrow::Utf16::IsLeadSurrogate(c0_)) {
      base::uc32 c1 = source_->Advance();
      DCHECK(!unibrow::Utf16::IsTrailSurrogate(kEndOfInput));
      if (unibrow::Utf16::IsTrailSurrogate(c1)) {
        c0_ = unibrow::Utf16::CombineSurrogatePair(c0_, c1);
        return true;
      }
      source_->Back();
    }
    return false;
  }

  void PushBack(base::uc32 ch) {
    DCHECK(IsInvalid(c0_) ||
           base::IsInRange(c0_, 0u, unibrow::Utf16::kMaxNonSurrogateCharCode));
    source_->Back();
    c0_ = ch;
  }

  base::uc32 Peek() const { return source_->Peek(); }

  inline Token::Value Select(Token::Value tok) {
    Advance();
    return tok;
  }

  inline Token::Value Select(base::uc32 next, Token::Value then,
                             Token::Value else_) {
    Advance();
    if (c0_ == next) {
      Advance();
      return then;
    } else {
      return else_;
    }
  }
  // Returns the literal string, if any, for the current token (the
  // token last returned by Next()). The string is 0-terminated.
  // Literal strings are collected for identifiers, strings, numbers as well
  // as for template literals. For template literals we also collect the raw
  // form.
  // These functions only give the correct result if the literal was scanned
  // when a LiteralScope object is alive.
  //
  // Current usage of these functions is unfortunately a little undisciplined,
  // and is_literal_one_byte() + is_literal_one_byte_string() is also
  // requested for tokens that do not have a literal. Hence, we treat any
  // token as a one-byte literal. E.g. Token::kFunction pretends to have a
  // literal "function".
  base::Vector<const uint8_t> literal_one_byte_string() const {
    DCHECK(current().CanAccessLiteral() || Token::IsKeyword(current().token) ||
           current().token == Token::kEscapedKeyword);
    return current().literal_chars.one_byte_literal();
  }
  base::Vector<const uint16_t> literal_two_byte_string() const {
    DCHECK(current().CanAccessLiteral() || Token::IsKeyword(current().token) ||
           current().token == Token::kEscapedKeyword);
    return current().literal_chars.two_byte_literal();
  }
  bool is_literal_one_byte() const {
    DCHECK(current().CanAccessLiteral() || Token::IsKeyword(current().token) ||
           current().token == Token::kEscapedKeyword);
    return current().literal_chars.is_one_byte();
  }
  // Returns the literal string for the next token (the token that
  // would be returned if Next() were called).
  base::Vector<const uint8_t> next_literal_one_byte_string() const {
    DCHECK(next().CanAccessLiteral());
    return next().literal_chars.one_byte_literal();
  }
  base::Vector<const uint16_t> next_literal_two_byte_string() const {
    DCHECK(next().CanAccessLiteral());
    return next().literal_chars.two_byte_literal();
  }
  bool is_next_literal_one_byte() const {
    DCHECK(next().CanAccessLiteral());
    return next().literal_chars.is_one_byte();
  }
  base::Vector<const uint8_t> raw_literal_one_byte_string() const {
    DCHECK(current().CanAccessRawLiteral());
    return current().raw_literal_chars.one_byte_literal();
  }
  base::Vector<const uint16_t> raw_literal_two_byte_string() const {
    DCHECK(current().CanAccessRawLiteral());
    return current().raw_literal_chars.two_byte_literal();
  }
  bool is_raw_literal_one_byte() const {
    DCHECK(current().CanAccessRawLiteral());
    return current().raw_literal_chars.is_one_byte();
  }

  template <bool capture_raw, bool unicode = false>
  base::uc32 ScanHexNumber(int expected_length);
  // Scan a number of any length but not bigger than max_value. For example, the
  // number can be 000000001, so it's very long in characters but its value is
  // small.
  template <bool capture_raw>
  base::uc32 ScanUnlimitedLengthHexNumber(base::uc32 max_value, int beg_pos);

  // Scans a single JavaScript token.
  V8_INLINE Token::Value ScanSingleToken();
  V8_INLINE void Scan();
  // Performance hack: pass through a pre-calculated "next()" value to avoid
  // having to re-calculate it in Scan. You'd think the compiler would be able
  // to hoist the next() calculation out of the inlined Scan method, but seems
  // that pointer aliasing analysis fails show that this is safe.
  V8_INLINE void Scan(TokenDesc* next_desc);

  V8_INLINE Token::Value SkipWhiteSpace();
  Token::Value SkipSingleHTMLComment();
  Token::Value SkipSingleLineComment();
  Token::Value SkipMagicComment(base::uc32 hash_or_at_sign);
  void TryToParseMagicComment(base::uc32 hash_or_at_sign);
  Token::Value SkipMultiLineComment();
  // Scans a possible HTML comment -- begins with '<!'.
  Token::Value ScanHtmlComment();

  bool ScanDigitsWithNumericSeparators(bool (*predicate)(base::uc32 ch),
                                       bool is_check_first_digit);
  bool ScanDecimalDigits(bool allow_numeric_separator);
  // Optimized function to scan decimal number as Smi.
  bool ScanDecimalAsSmi(uint64_t* value, bool allow_numeric_separator);
  bool ScanDecimalAsSmiWithNumericSeparators(uint64_t* value);
  bool ScanHexDigits();
  bool ScanBinaryDigits();
  bool ScanSignedInteger();
  bool ScanOctalDigits();
  bool ScanImplicitOctalDigits(int start_pos, NumberKind* kind);

  Token::Value ScanNumber(bool seen_period);
  V8_INLINE Token::Value ScanIdentifierOrKeyword();
  V8_INLINE Token::Value ScanIdentifierOrKeywordInner();
  Token::Value ScanIdentifierOrKeywordInnerSlow(bool escaped,
                                                bool can_be_keyword);

  Token::Value ScanString();
  Token::Value ScanPrivateName();

  // Scans an escape-sequence which is part of a string and adds the
  // decoded character to the current literal. Returns true if a pattern
  // is scanned.
  template <bool capture_raw>
  bool ScanEscape();

  // Decodes a Unicode escape-sequence which is part of an identifier.
  // If the escape sequence cannot be decoded the result is kBadChar.
  base::uc32 ScanIdentifierUnicodeEscape();
  // Helper for the above functions.
  template <bool capture_raw>
  base::uc32 ScanUnicodeEscape();

  Token::Value ScanTemplateSpan();

  // Return the current source position.
  int source_pos() {
    return static_cast<int>(source_->pos()) - kCharacterLookaheadBufferSize;
  }

  static bool LiteralContainsEscapes(const TokenDesc& token) {
    Location location = token.location;
    int source_length = (location.end_pos - location.beg_pos);
    if (token.token == Token::kString) {
      // Subtract delimiters.
      source_length -= 2;
    }
    return token.literal_chars.length() != source_length;
  }

#ifdef DEBUG
  void SanityCheckTokenDesc(const TokenDesc&) const;
#endif

  TokenDesc& next() { return *next_; }

  const TokenDesc& current() const { return *current_; }
  const TokenDesc& next() const { return *next_; }
  const TokenDesc& next_next() const { return *next_next_; }

  UnoptimizedCompileFlags flags_;

  TokenDesc* current_;    // desc for current token (as returned by Next())
  TokenDesc* next_;       // desc for next token (one token look-ahead)
  TokenDesc* next_next_;  // desc for the token after next (after PeakAhead())

  // Input stream. Must be initialized to an Utf16CharacterStream.
  Utf16CharacterStream* const source_;

  // One Unicode character look-ahead; c0_ < 0 at the end of the input.
  base::uc32 c0_;

  TokenDesc token_storage_[3];

  // Whether this scanner encountered an HTML comment.
  bool found_html_comment_;

  // Values parsed from magic comments.
  LiteralBuffer source_url_;
  LiteralBuffer source_mapping_url_;
  bool saw_source_mapping_url_magic_comment_at_sign_ = false;
  bool saw_magic_comment_compile_hints_all_ = false;

  // Last-seen positions of potentially problematic tokens.
  Location octal_pos_;
  MessageTemplate octal_message_;

  MessageTemplate scanner_error_;
  Location scanner_error_location_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_SCANNER_H_

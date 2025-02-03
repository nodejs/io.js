// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_EARLEY_PARSER_H_
#define V8_TORQUE_EARLEY_PARSER_H_

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "src/base/contextual.h"
#include "src/torque/source-positions.h"
#include "src/torque/utils.h"

namespace v8::internal::torque {

class Symbol;
class Item;

class ParseResultHolderBase {
 public:
  enum class TypeId;
  virtual ~ParseResultHolderBase() = default;
  template <class T>
  T& Cast();
  template <class T>
  const T& Cast() const;

 protected:
  explicit ParseResultHolderBase(TypeId type_id) : type_id_(type_id) {
    // MSVC wrongly complains about type_id_ being an unused private field.
    USE(type_id_);
  }

 private:
  const TypeId type_id_;
};

enum class ParseResultHolderBase::TypeId {
  kStdString,
  kBool,
  kInt32,
  kDouble,
  kIntegerLiteral,
  kStdVectorOfString,
  kExpressionPtr,
  kIdentifierPtr,
  kOptionalIdentifierPtr,
  kStatementPtr,
  kDeclarationPtr,
  kTypeExpressionPtr,
  kOptionalTypeExpressionPtr,
  kTryHandlerPtr,
  kNameAndTypeExpression,
  kEnumEntry,
  kStdVectorOfEnumEntry,
  kImplicitParameters,
  kOptionalImplicitParameters,
  kNameAndExpression,
  kAnnotation,
  kVectorOfAnnotation,
  kAnnotationParameter,
  kOptionalAnnotationParameter,
  kClassFieldExpression,
  kStructFieldExpression,
  kBitFieldDeclaration,
  kStdVectorOfNameAndTypeExpression,
  kStdVectorOfNameAndExpression,
  kStdVectorOfClassFieldExpression,
  kStdVectorOfStructFieldExpression,
  kStdVectorOfBitFieldDeclaration,
  kIncrementDecrementOperator,
  kOptionalStdString,
  kStdVectorOfStatementPtr,
  kStdVectorOfDeclarationPtr,
  kStdVectorOfStdVectorOfDeclarationPtr,
  kStdVectorOfExpressionPtr,
  kExpressionWithSource,
  kParameterList,
  kTypeList,
  kOptionalTypeList,
  kLabelAndTypes,
  kStdVectorOfLabelAndTypes,
  kStdVectorOfTryHandlerPtr,
  kOptionalStatementPtr,
  kOptionalExpressionPtr,
  kTypeswitchCase,
  kStdVectorOfTypeswitchCase,
  kStdVectorOfIdentifierPtr,
  kOptionalClassBody,
  kGenericParameter,
  kGenericParameters,

  kJsonValue,
  kJsonMember,
  kStdVectorOfJsonValue,
  kStdVectorOfJsonMember,
};

using ParseResultTypeId = ParseResultHolderBase::TypeId;

template <class T>
class ParseResultHolder : public ParseResultHolderBase {
 public:
  explicit ParseResultHolder(T value)
      : ParseResultHolderBase(id), value_(std::move(value)) {}

 private:
  V8_EXPORT_PRIVATE static const TypeId id;
  friend class ParseResultHolderBase;
  T value_;
};

template <class T>
T& ParseResultHolderBase::Cast() {
  CHECK_EQ(ParseResultHolder<T>::id, type_id_);
  return static_cast<ParseResultHolder<T>*>(this)->value_;
}

template <class T>
const T& ParseResultHolderBase::Cast() const {
  CHECK_EQ(ParseResultHolder<T>::id, type_id_);
  return static_cast<const ParseResultHolder<T>*>(this)->value_;
}

class ParseResult {
 public:
  template <class T>
  explicit ParseResult(T x) : value_(new ParseResultHolder<T>(std::move(x))) {}

  template <class T>
  const T& Cast() const& {
    return value_->Cast<T>();
  }
  template <class T>
  T& Cast() & {
    return value_->Cast<T>();
  }
  template <class T>
  T&& Cast() && {
    return std::move(value_->Cast<T>());
  }

 private:
  std::unique_ptr<ParseResultHolderBase> value_;
};

using InputPosition = const char*;

struct MatchedInput {
  MatchedInput(InputPosition begin, InputPosition end, SourcePosition pos)
      : begin(begin), end(end), pos(pos) {}
  InputPosition begin;
  InputPosition end;
  SourcePosition pos;
  std::string ToString() const { return {begin, end}; }
};

class ParseResultIterator {
 public:
  explicit ParseResultIterator(std::vector<ParseResult> results,
                               MatchedInput matched_input)
      : results_(std::move(results)), matched_input_(matched_input) {}

  ParseResultIterator(const ParseResultIterator&) = delete;
  ParseResultIterator& operator=(const ParseResultIterator&) = delete;

  ParseResult Next() {
    CHECK_LT(i_, results_.size());
    return std::move(results_[i_++]);
  }
  template <class T>
  T NextAs() {
    return std::move(Next().Cast<T>());
  }
  bool HasNext() const { return i_ < results_.size(); }

  const MatchedInput& matched_input() const { return matched_input_; }

 private:
  std::vector<ParseResult> results_;
  size_t i_ = 0;
  MatchedInput matched_input_;
};

struct LexerResult {
  std::vector<Symbol*> token_symbols;
  std::vector<MatchedInput> token_contents;
};

using Action =
    std::optional<ParseResult> (*)(ParseResultIterator* child_results);

inline std::optional<ParseResult> DefaultAction(
    ParseResultIterator* child_results) {
  if (!child_results->HasNext()) return std::nullopt;
  return child_results->Next();
}

template <class T, Action action>
inline Action AsSingletonVector() {
  return [](ParseResultIterator* child_results) -> std::optional<ParseResult> {
    auto result = action(child_results);
    if (!result) return result;
    return ParseResult{std::vector<T>{(*result).Cast<T>()}};
  };
}

// A rule of the context-free grammar. Each rule can have an action attached to
// it, which is executed after the parsing is finished.
class Rule final {
 public:
  explicit Rule(std::vector<Symbol*> right_hand_side,
                Action action = DefaultAction)
      : right_hand_side_(std::move(right_hand_side)), action_(action) {}

  Symbol* left() const {
    DCHECK_NOT_NULL(left_hand_side_);
    return left_hand_side_;
  }
  const std::vector<Symbol*>& right() const { return right_hand_side_; }

  void SetLeftHandSide(Symbol* left_hand_side) {
    DCHECK_NULL(left_hand_side_);
    left_hand_side_ = left_hand_side;
  }

  V8_EXPORT_PRIVATE std::optional<ParseResult> RunAction(
      const Item* completed_item, const LexerResult& tokens) const;

 private:
  Symbol* left_hand_side_ = nullptr;
  std::vector<Symbol*> right_hand_side_;
  Action action_;
};

// A Symbol represents a terminal or a non-terminal of the grammar.
// It stores the list of rules, which have this symbol as the
// left-hand side.
// Terminals have an empty list of rules, they are created by the Lexer
// instead of from rules.
// Symbols need to reside at stable memory addresses, because the addresses are
// used in the parser.
class Symbol {
 public:
  Symbol() = default;
  Symbol(std::initializer_list<Rule> rules) { *this = rules; }

  // Disallow copying and moving to ensure Symbol has a stable address.
  Symbol(const Symbol&) = delete;
  Symbol& operator=(const Symbol&) = delete;

  V8_EXPORT_PRIVATE Symbol& operator=(std::initializer_list<Rule> rules);

  bool IsTerminal() const { return rules_.empty(); }
  Rule* rule(size_t index) const { return rules_[index].get(); }
  size_t rule_number() const { return rules_.size(); }

  void AddRule(const Rule& rule) {
    rules_.push_back(std::make_unique<Rule>(rule));
    rules_.back()->SetLeftHandSide(this);
  }

  V8_EXPORT_PRIVATE std::optional<ParseResult> RunAction(
      const Item* item, const LexerResult& tokens);

 private:
  std::vector<std::unique_ptr<Rule>> rules_;
};

// Items are the core datastructure of Earley's algorithm.
// They consist of a (partially) matched rule, a marked position inside of the
// right-hand side of the rule (traditionally written as a dot) and an input
// range from {start} to {pos} that matches the symbols of the right-hand side
// that are left of the mark. In addition, they store a child and a left-sibling
// pointer to reconstruct the AST in the end.
class Item {
 public:
  Item(const Rule* rule, size_t mark, size_t start, size_t pos)
      : rule_(rule), mark_(mark), start_(start), pos_(pos) {
    DCHECK_LE(mark_, right().size());
  }

  // A complete item has the mark at the right end, which means the input range
  // matches the complete rule.
  bool IsComplete() const {
    DCHECK_LE(mark_, right().size());
    return mark_ == right().size();
  }

  // The symbol right after the mark is expected at {pos} for this item to
  // advance.
  Symbol* NextSymbol() const {
    DCHECK(!IsComplete());
    DCHECK_LT(mark_, right().size());
    return right()[mark_];
  }

  // We successfully parsed NextSymbol() between {pos} and {new_pos}.
  // If NextSymbol() was a non-terminal, then {child} is a pointer to a
  // completed item for this parse.
  // We create a new item, which moves the mark one forward.
  Item Advance(size_t new_pos, const Item* child = nullptr) const {
    if (child) {
      DCHECK(child->IsComplete());
      DCHECK_EQ(pos(), child->start());
      DCHECK_EQ(new_pos, child->pos());
      DCHECK_EQ(NextSymbol(), child->left());
    }
    Item result(rule_, mark_ + 1, start_, new_pos);
    result.prev_ = this;
    result.child_ = child;
    return result;
  }

  // Collect the items representing the AST children of this completed item.
  std::vector<const Item*> Children() const;
  // The matched input separated according to the next branching AST level.
  std::string SplitByChildren(const LexerResult& tokens) const;
  // Check if {other} results in the same AST as this Item.
  void CheckAmbiguity(const Item& other, const LexerResult& tokens) const;

  MatchedInput GetMatchedInput(const LexerResult& tokens) const {
    const MatchedInput& start = tokens.token_contents[start_];
    const MatchedInput& end = start_ == pos_ ? tokens.token_contents[start_]
                                             : tokens.token_contents[pos_ - 1];
    CHECK_EQ(start.pos.source, end.pos.source);
    SourcePosition combined{start.pos.source, start.pos.start, end.pos.end};

    return {start.begin, end.end, combined};
  }

  // We exclude {prev_} and {child_} from equality and hash computations,
  // because they are just globally unique data associated with an item.
  bool operator==(const Item& other) const {
    return rule_ == other.rule_ && mark_ == other.mark_ &&
           start_ == other.start_ && pos_ == other.pos_;
  }

  friend size_t hash_value(const Item& i) {
    return base::hash_combine(i.rule_, i.mark_, i.start_, i.pos_);
  }

  const Rule* rule() const { return rule_; }
  Symbol* left() const { return rule_->left(); }
  const std::vector<Symbol*>& right() const { return rule_->right(); }
  size_t pos() const { return pos_; }
  size_t start() const { return start_; }

 private:
  const Rule* rule_;
  size_t mark_;
  size_t start_;
  size_t pos_;

  const Item* prev_ = nullptr;
  const Item* child_ = nullptr;
};

inline std::optional<ParseResult> Symbol::RunAction(const Item* item,
                                                    const LexerResult& tokens) {
  DCHECK(item->IsComplete());
  DCHECK_EQ(item->left(), this);
  return item->rule()->RunAction(item, tokens);
}

V8_EXPORT_PRIVATE const Item* RunEarleyAlgorithm(
    Symbol* start, const LexerResult& tokens,
    std::unordered_set<Item, base::hash<Item>>* processed);

inline std::optional<ParseResult> ParseTokens(Symbol* start,
                                              const LexerResult& tokens) {
  std::unordered_set<Item, base::hash<Item>> table;
  const Item* final_item = RunEarleyAlgorithm(start, tokens, &table);
  return start->RunAction(final_item, tokens);
}

// The lexical syntax is dynamically defined while building the grammar by
// adding patterns and keywords to the Lexer.
// The term keyword here can stand for any fixed character sequence, including
// operators and parentheses.
// Each pattern or keyword automatically gets a terminal symbol associated with
// it. These symbols form the result of the lexing.
// Patterns and keywords are matched using the longest match principle. If the
// longest matching pattern coincides with a keyword, the keyword symbol is
// chosen instead of the pattern.
// In addition, there is a single whitespace pattern which is consumed but does
// not become part of the token list.
class Lexer {
 public:
  // Functions to define patterns. They try to match starting from {pos}. If
  // successful, they return true and advance {pos}. Otherwise, {pos} stays
  // unchanged.
  using PatternFunction = bool (*)(InputPosition* pos);

  void SetWhitespace(PatternFunction whitespace) {
    match_whitespace_ = whitespace;
  }

  Symbol* Pattern(PatternFunction pattern) { return &patterns_[pattern]; }
  Symbol* Token(const std::string& keyword) { return &keywords_[keyword]; }
  V8_EXPORT_PRIVATE LexerResult RunLexer(const std::string& input);

 private:
  PatternFunction match_whitespace_ = [](InputPosition*) { return false; };
  std::map<PatternFunction, Symbol> patterns_;
  std::map<std::string, Symbol> keywords_;
  Symbol* MatchToken(InputPosition* pos, InputPosition end);
};

// A grammar can have a result, which is the results of the start symbol.
// Grammar is intended to be subclassed, with Symbol members forming the
// mutually recursive rules of the grammar.
class Grammar {
 public:
  using PatternFunction = Lexer::PatternFunction;

  explicit Grammar(Symbol* start) : start_(start) {}

  std::optional<ParseResult> Parse(const std::string& input) {
    LexerResult tokens = lexer().RunLexer(input);
    return ParseTokens(start_, tokens);
  }

 protected:
  Symbol* Token(const std::string& s) { return lexer_.Token(s); }
  Symbol* Pattern(PatternFunction pattern) { return lexer_.Pattern(pattern); }
  void SetWhitespace(PatternFunction ws) { lexer_.SetWhitespace(ws); }

  // NewSymbol() allocates a fresh symbol and stores it in the current grammar.
  // This is necessary to define helpers that create new symbols.
  Symbol* NewSymbol(std::initializer_list<Rule> rules = {}) {
    auto symbol = std::make_unique<Symbol>(rules);
    Symbol* result = symbol.get();
    generated_symbols_.push_back(std::move(symbol));
    return result;
  }

  // Helper functions to define lexer patterns. If they match, they return true
  // and advance {pos}. Otherwise, {pos} is unchanged.
  V8_EXPORT_PRIVATE static bool MatchChar(int (*char_class)(int),
                                          InputPosition* pos);
  V8_EXPORT_PRIVATE static bool MatchChar(bool (*char_class)(char),
                                          InputPosition* pos);
  V8_EXPORT_PRIVATE static bool MatchAnyChar(InputPosition* pos);
  V8_EXPORT_PRIVATE static bool MatchString(const char* s, InputPosition* pos);

  // The action MatchInput() produces the input matched by the rule as
  // result.
  static std::optional<ParseResult> YieldMatchedInput(
      ParseResultIterator* child_results) {
    return ParseResult{child_results->matched_input().ToString()};
  }

  // Create a new symbol to parse the given sequence of symbols.
  // At most one of the symbols can return a result.
  Symbol* Sequence(std::vector<Symbol*> symbols) {
    return NewSymbol({Rule(std::move(symbols))});
  }

  template <class T, T value>
  static std::optional<ParseResult> YieldIntegralConstant(
      ParseResultIterator* child_results) {
    return ParseResult{value};
  }

  template <class T>
  static std::optional<ParseResult> YieldDefaultValue(
      ParseResultIterator* child_results) {
    return ParseResult{T{}};
  }

  template <class From, class To>
  static std::optional<ParseResult> CastParseResult(
      ParseResultIterator* child_results) {
    To result = child_results->NextAs<From>();
    return ParseResult{std::move(result)};
  }

  // Try to parse {s} and return the result of type {Result} casted to {T}.
  // Otherwise, the result is a default-constructed {T}.
  template <class T, class Result = T>
  Symbol* TryOrDefault(Symbol* s) {
    return NewSymbol({Rule({s}, CastParseResult<Result, T>),
                      Rule({}, YieldDefaultValue<T>)});
  }

  template <class T>
  static std::optional<ParseResult> MakeSingletonVector(
      ParseResultIterator* child_results) {
    T x = child_results->NextAs<T>();
    std::vector<T> result;
    result.push_back(std::move(x));
    return ParseResult{std::move(result)};
  }

  template <class T>
  static std::optional<ParseResult> MakeExtendedVector(
      ParseResultIterator* child_results) {
    std::vector<T> l = child_results->NextAs<std::vector<T>>();
    T x = child_results->NextAs<T>();
    l.push_back(std::move(x));
    return ParseResult{std::move(l)};
  }

  // For example, NonemptyList(Token("A"), Token(",")) parses any of
  // A or A,A or A,A,A and so on.
  template <class T>
  Symbol* NonemptyList(Symbol* element, std::optional<Symbol*> separator = {}) {
    Symbol* list = NewSymbol();
    *list = {Rule({element}, MakeSingletonVector<T>),
             separator
                 ? Rule({list, *separator, element}, MakeExtendedVector<T>)
                 : Rule({list, element}, MakeExtendedVector<T>)};
    return list;
  }

  template <class T>
  Symbol* List(Symbol* element, std::optional<Symbol*> separator = {}) {
    return TryOrDefault<std::vector<T>>(NonemptyList<T>(element, separator));
  }

  template <class T>
  Symbol* Optional(Symbol* x) {
    return TryOrDefault<std::optional<T>, T>(x);
  }

  Symbol* CheckIf(Symbol* x) {
    return NewSymbol({Rule({x}, YieldIntegralConstant<bool, true>),
                      Rule({}, YieldIntegralConstant<bool, false>)});
  }

  Lexer& lexer() { return lexer_; }

 private:
  Lexer lexer_;
  std::vector<std::unique_ptr<Symbol>> generated_symbols_;
  Symbol* start_;
};

}  // namespace v8::internal::torque

#endif  // V8_TORQUE_EARLEY_PARSER_H_

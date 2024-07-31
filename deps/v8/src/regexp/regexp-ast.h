// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_AST_H_
#define V8_REGEXP_REGEXP_AST_H_

#include "src/base/strings.h"
#include "src/regexp/regexp-flags.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone-list.h"
#include "src/zone/zone.h"
#ifdef V8_INTL_SUPPORT
#include "unicode/uniset.h"
#endif  // V8_INTL_SUPPORT

namespace v8 {
namespace internal {

#define FOR_EACH_REG_EXP_TREE_TYPE(VISIT) \
  VISIT(Disjunction)                      \
  VISIT(Alternative)                      \
  VISIT(Assertion)                        \
  VISIT(ClassRanges)                      \
  VISIT(ClassSetOperand)                  \
  VISIT(ClassSetExpression)               \
  VISIT(Atom)                             \
  VISIT(Quantifier)                       \
  VISIT(Capture)                          \
  VISIT(Group)                            \
  VISIT(Lookaround)                       \
  VISIT(BackReference)                    \
  VISIT(Empty)                            \
  VISIT(Text)

#define FORWARD_DECLARE(Name) class RegExp##Name;
FOR_EACH_REG_EXP_TREE_TYPE(FORWARD_DECLARE)
#undef FORWARD_DECLARE

class RegExpCompiler;
class RegExpNode;
class RegExpTree;

class RegExpVisitor {
 public:
  virtual ~RegExpVisitor() = default;
#define MAKE_CASE(Name) \
  virtual void* Visit##Name(RegExp##Name*, void* data) = 0;
  FOR_EACH_REG_EXP_TREE_TYPE(MAKE_CASE)
#undef MAKE_CASE
};

// A simple closed interval.
class Interval {
 public:
  Interval() : from_(kNone), to_(kNone - 1) {}  // '- 1' for branchless size().
  Interval(int from, int to) : from_(from), to_(to) {}
  Interval Union(Interval that) {
    if (that.from_ == kNone) return *this;
    if (from_ == kNone) return that;
    return Interval(std::min(from_, that.from_), std::max(to_, that.to_));
  }

  static Interval Empty() { return Interval(); }

  bool Contains(int value) const { return (from_ <= value) && (value <= to_); }
  bool is_empty() const { return from_ == kNone; }
  int from() const { return from_; }
  int to() const { return to_; }
  int size() const { return to_ - from_ + 1; }

  static constexpr int kNone = -1;

 private:
  int from_;
  int to_;
};

// Named standard character sets.
enum class StandardCharacterSet : char {
  kWhitespace = 's',         // Like /\s/.
  kNotWhitespace = 'S',      // Like /\S/.
  kWord = 'w',               // Like /\w/.
  kNotWord = 'W',            // Like /\W/.
  kDigit = 'd',              // Like /\d/.
  kNotDigit = 'D',           // Like /\D/.
  kLineTerminator = 'n',     // The inverse of /./.
  kNotLineTerminator = '.',  // Like /./.
  kEverything = '*',         // Matches every character, like /./s.
};

// Represents code points (with values up to 0x10FFFF) in the range from from_
// to to_, both ends are inclusive.
class CharacterRange {
 public:
  CharacterRange() = default;
  // For compatibility with the CHECK_OK macro.
  CharacterRange(void* null) { DCHECK_NULL(null); }  // NOLINT

  static inline CharacterRange Singleton(base::uc32 value) {
    return CharacterRange(value, value);
  }
  static inline CharacterRange Range(base::uc32 from, base::uc32 to) {
    DCHECK(0 <= from && to <= kMaxCodePoint);
    DCHECK(static_cast<uint32_t>(from) <= static_cast<uint32_t>(to));
    return CharacterRange(from, to);
  }
  static inline CharacterRange Everything() {
    return CharacterRange(0, kMaxCodePoint);
  }

  static inline ZoneList<CharacterRange>* List(Zone* zone,
                                               CharacterRange range) {
    ZoneList<CharacterRange>* list =
        zone->New<ZoneList<CharacterRange>>(1, zone);
    list->Add(range, zone);
    return list;
  }

  // Add class escapes. Add case equivalent closure for \w and \W if necessary.
  V8_EXPORT_PRIVATE static void AddClassEscape(
      StandardCharacterSet standard_character_set,
      ZoneList<CharacterRange>* ranges, bool add_unicode_case_equivalents,
      Zone* zone);
  // Add case equivalents to ranges. Only used for /i, not for /ui or /vi, as
  // the semantics for unicode mode are slightly different.
  // See https://tc39.es/ecma262/#sec-runtime-semantics-canonicalize-ch Note 4.
  V8_EXPORT_PRIVATE static void AddCaseEquivalents(
      Isolate* isolate, Zone* zone, ZoneList<CharacterRange>* ranges,
      bool is_one_byte);
  // Add case equivalent code points to ranges. Only used for /ui and /vi, not
  // for /i, as the semantics for non-unicode mode are slightly different.
  // See https://tc39.es/ecma262/#sec-runtime-semantics-canonicalize-ch Note 4.
  static void AddUnicodeCaseEquivalents(ZoneList<CharacterRange>* ranges,
                                        Zone* zone);

  bool Contains(base::uc32 i) const { return from_ <= i && i <= to_; }
  base::uc32 from() const { return from_; }
  base::uc32 to() const { return to_; }
  bool IsEverything(base::uc32 max) const { return from_ == 0 && to_ >= max; }
  bool IsSingleton() const { return from_ == to_; }

  // Whether a range list is in canonical form: Ranges ordered by from value,
  // and ranges non-overlapping and non-adjacent.
  V8_EXPORT_PRIVATE static bool IsCanonical(
      const ZoneList<CharacterRange>* ranges);
  // Convert range list to canonical form. The characters covered by the ranges
  // will still be the same, but no character is in more than one range, and
  // adjacent ranges are merged. The resulting list may be shorter than the
  // original, but cannot be longer.
  static void Canonicalize(ZoneList<CharacterRange>* ranges);
  // Negate the contents of a character range in canonical form.
  static void Negate(const ZoneList<CharacterRange>* src,
                     ZoneList<CharacterRange>* dst, Zone* zone);
  // Intersect the contents of two character ranges in canonical form.
  static void Intersect(const ZoneList<CharacterRange>* lhs,
                        const ZoneList<CharacterRange>* rhs,
                        ZoneList<CharacterRange>* dst, Zone* zone);
  // Subtract the contents of |to_remove| from the contents of |src|.
  static void Subtract(const ZoneList<CharacterRange>* src,
                       const ZoneList<CharacterRange>* to_remove,
                       ZoneList<CharacterRange>* dst, Zone* zone);
  // Remove all ranges outside the one-byte range.
  static void ClampToOneByte(ZoneList<CharacterRange>* ranges);
  // Checks if two ranges (both need to be canonical) are equal.
  static bool Equals(const ZoneList<CharacterRange>* lhs,
                     const ZoneList<CharacterRange>* rhs);

 private:
  CharacterRange(base::uc32 from, base::uc32 to) : from_(from), to_(to) {}

  static constexpr int kMaxCodePoint = 0x10ffff;

  base::uc32 from_ = 0;
  base::uc32 to_ = 0;
};

inline bool operator==(const CharacterRange& lhs, const CharacterRange& rhs) {
  return lhs.from() == rhs.from() && lhs.to() == rhs.to();
}
inline bool operator!=(const CharacterRange& lhs, const CharacterRange& rhs) {
  return !operator==(lhs, rhs);
}

#define DECL_BOILERPLATE(Name)                                         \
  void* Accept(RegExpVisitor* visitor, void* data) override;           \
  RegExpNode* ToNode(RegExpCompiler* compiler, RegExpNode* on_success) \
      override;                                                        \
  RegExp##Name* As##Name() override;                                   \
  bool Is##Name() override

class RegExpTree : public ZoneObject {
 public:
  static const int kInfinity = kMaxInt;
  virtual ~RegExpTree() = default;
  virtual void* Accept(RegExpVisitor* visitor, void* data) = 0;
  virtual RegExpNode* ToNode(RegExpCompiler* compiler,
                             RegExpNode* on_success) = 0;
  virtual bool IsTextElement() { return false; }
  virtual bool IsAnchoredAtStart() { return false; }
  virtual bool IsAnchoredAtEnd() { return false; }
  virtual int min_match() = 0;
  virtual int max_match() = 0;
  // Returns the interval of registers used for captures within this
  // expression.
  virtual Interval CaptureRegisters() { return Interval::Empty(); }
  virtual void AppendToText(RegExpText* text, Zone* zone);
  V8_EXPORT_PRIVATE std::ostream& Print(std::ostream& os, Zone* zone);
#define MAKE_ASTYPE(Name)           \
  virtual RegExp##Name* As##Name(); \
  virtual bool Is##Name();
  FOR_EACH_REG_EXP_TREE_TYPE(MAKE_ASTYPE)
#undef MAKE_ASTYPE
};


class RegExpDisjunction final : public RegExpTree {
 public:
  explicit RegExpDisjunction(ZoneList<RegExpTree*>* alternatives);

  DECL_BOILERPLATE(Disjunction);

  Interval CaptureRegisters() override;
  bool IsAnchoredAtStart() override;
  bool IsAnchoredAtEnd() override;
  int min_match() override { return min_match_; }
  int max_match() override { return max_match_; }
  ZoneList<RegExpTree*>* alternatives() const { return alternatives_; }

 private:
  bool SortConsecutiveAtoms(RegExpCompiler* compiler);
  void RationalizeConsecutiveAtoms(RegExpCompiler* compiler);
  void FixSingleCharacterDisjunctions(RegExpCompiler* compiler);
  ZoneList<RegExpTree*>* alternatives_;
  int min_match_;
  int max_match_;
};


class RegExpAlternative final : public RegExpTree {
 public:
  explicit RegExpAlternative(ZoneList<RegExpTree*>* nodes);

  DECL_BOILERPLATE(Alternative);

  Interval CaptureRegisters() override;
  bool IsAnchoredAtStart() override;
  bool IsAnchoredAtEnd() override;
  int min_match() override { return min_match_; }
  int max_match() override { return max_match_; }
  ZoneList<RegExpTree*>* nodes() const { return nodes_; }

 private:
  ZoneList<RegExpTree*>* nodes_;
  int min_match_;
  int max_match_;
};


class RegExpAssertion final : public RegExpTree {
 public:
  enum class Type {
    START_OF_LINE = 0,
    START_OF_INPUT = 1,
    END_OF_LINE = 2,
    END_OF_INPUT = 3,
    BOUNDARY = 4,
    NON_BOUNDARY = 5,
    LAST_ASSERTION_TYPE = NON_BOUNDARY,
  };
  explicit RegExpAssertion(Type type) : assertion_type_(type) {}

  DECL_BOILERPLATE(Assertion);

  bool IsAnchoredAtStart() override;
  bool IsAnchoredAtEnd() override;
  int min_match() override { return 0; }
  int max_match() override { return 0; }
  Type assertion_type() const { return assertion_type_; }

 private:
  const Type assertion_type_;
};

class CharacterSet final {
 public:
  explicit CharacterSet(StandardCharacterSet standard_set_type)
      : standard_set_type_(standard_set_type) {}
  explicit CharacterSet(ZoneList<CharacterRange>* ranges) : ranges_(ranges) {}

  ZoneList<CharacterRange>* ranges(Zone* zone);
  StandardCharacterSet standard_set_type() const {
    return standard_set_type_.value();
  }
  void set_standard_set_type(StandardCharacterSet standard_set_type) {
    standard_set_type_ = standard_set_type;
  }
  bool is_standard() const { return standard_set_type_.has_value(); }
  V8_EXPORT_PRIVATE void Canonicalize();

 private:
  ZoneList<CharacterRange>* ranges_ = nullptr;
  base::Optional<StandardCharacterSet> standard_set_type_;
};

class RegExpClassRanges final : public RegExpTree {
 public:
  // NEGATED: The character class is negated and should match everything but
  //     the specified ranges.
  // CONTAINS_SPLIT_SURROGATE: The character class contains part of a split
  //     surrogate and should not be unicode-desugared (crbug.com/641091).
  // IS_CASE_FOLDED: If case folding is required (/i), it was already
  //     performed on individual ranges and should not be applied again.
  enum Flag {
    NEGATED = 1 << 0,
    CONTAINS_SPLIT_SURROGATE = 1 << 1,
    IS_CASE_FOLDED = 1 << 2,
  };
  using ClassRangesFlags = base::Flags<Flag>;

  RegExpClassRanges(Zone* zone, ZoneList<CharacterRange>* ranges,
                    ClassRangesFlags class_ranges_flags = ClassRangesFlags())
      : set_(ranges), class_ranges_flags_(class_ranges_flags) {
    // Convert the empty set of ranges to the negated Everything() range.
    if (ranges->is_empty()) {
      ranges->Add(CharacterRange::Everything(), zone);
      class_ranges_flags_ ^= NEGATED;
    }
  }
  explicit RegExpClassRanges(StandardCharacterSet standard_set_type)
      : set_(standard_set_type), class_ranges_flags_() {}

  DECL_BOILERPLATE(ClassRanges);

  bool IsTextElement() override { return true; }
  int min_match() override { return 1; }
  // The character class may match two code units for unicode regexps.
  // TODO(yangguo): we should split this class for usage in TextElement, and
  //                make max_match() dependent on the character class content.
  int max_match() override { return 2; }

  void AppendToText(RegExpText* text, Zone* zone) override;

  // TODO(lrn): Remove need for complex version if is_standard that
  // recognizes a mangled standard set and just do { return set_.is_special(); }
  bool is_standard(Zone* zone);
  // Returns a value representing the standard character set if is_standard()
  // returns true.
  StandardCharacterSet standard_type() const {
    return set_.standard_set_type();
  }

  CharacterSet character_set() const { return set_; }
  ZoneList<CharacterRange>* ranges(Zone* zone) { return set_.ranges(zone); }

  bool is_negated() const { return (class_ranges_flags_ & NEGATED) != 0; }
  bool contains_split_surrogate() const {
    return (class_ranges_flags_ & CONTAINS_SPLIT_SURROGATE) != 0;
  }
  bool is_case_folded() const {
    return (class_ranges_flags_ & IS_CASE_FOLDED) != 0;
  }

 private:
  CharacterSet set_;
  ClassRangesFlags class_ranges_flags_;
};

struct CharacterClassStringLess {
  bool operator()(base::Vector<const base::uc32> lhs,
                  base::Vector<const base::uc32> rhs) const {
    // Longer strings first so we generate matches for the largest string
    // possible.
    if (lhs.length() != rhs.length()) {
      return lhs.length() > rhs.length();
    }
    for (int i = 0; i < lhs.length(); i++) {
      if (lhs[i] != rhs[i]) {
        return lhs[i] < rhs[i];
      }
    }
    return false;
  }
};

// A type used for strings as part of character classes (only possible in
// unicode sets mode).
// We use a ZoneMap instead of an UnorderedZoneMap because we need to match
// the longest alternatives first. By using a ZoneMap with the custom comparator
// we can avoid sorting before assembling the code.
// Strings are likely short (the largest string in current unicode properties
// consists of 10 code points).
using CharacterClassStrings = ZoneMap<base::Vector<const base::uc32>,
                                      RegExpTree*, CharacterClassStringLess>;

// TODO(pthier): If we are sure we don't want to use icu::UnicodeSets
// (performance evaluation pending), this class can be merged with
// RegExpClassRanges.
class RegExpClassSetOperand final : public RegExpTree {
 public:
  RegExpClassSetOperand(ZoneList<CharacterRange>* ranges,
                        CharacterClassStrings* strings);

  DECL_BOILERPLATE(ClassSetOperand);

  bool IsTextElement() override { return true; }
  int min_match() override { return min_match_; }
  int max_match() override { return max_match_; }

  void Union(RegExpClassSetOperand* other, Zone* zone);
  void Intersect(RegExpClassSetOperand* other,
                 ZoneList<CharacterRange>* temp_ranges, Zone* zone);
  void Subtract(RegExpClassSetOperand* other,
                ZoneList<CharacterRange>* temp_ranges, Zone* zone);

  bool has_strings() const { return strings_ != nullptr && !strings_->empty(); }
  ZoneList<CharacterRange>* ranges() { return ranges_; }
  CharacterClassStrings* strings() {
    DCHECK_NOT_NULL(strings_);
    return strings_;
  }

 private:
  ZoneList<CharacterRange>* ranges_;
  CharacterClassStrings* strings_;
  int min_match_;
  int max_match_;
};

class RegExpClassSetExpression final : public RegExpTree {
 public:
  enum class OperationType { kUnion, kIntersection, kSubtraction };

  RegExpClassSetExpression(OperationType op, bool is_negated,
                           bool may_contain_strings,
                           ZoneList<RegExpTree*>* operands);

  DECL_BOILERPLATE(ClassSetExpression);

  // Create an empty class set expression (matches everything if |is_negated|,
  // nothing otherwise).
  static RegExpClassSetExpression* Empty(Zone* zone, bool is_negated);

  bool IsTextElement() override { return true; }
  int min_match() override { return 0; }
  int max_match() override { return max_match_; }

  OperationType operation() const { return operation_; }
  bool is_negated() const { return is_negated_; }
  bool may_contain_strings() const { return may_contain_strings_; }
  const ZoneList<RegExpTree*>* operands() const { return operands_; }
  ZoneList<RegExpTree*>* operands() { return operands_; }

 private:
  // Recursively evaluates the tree rooted at |root|, computing the valid
  // CharacterRanges and strings after applying all set operations.
  // The original tree will be modified by this method, so don't store pointers
  // to inner nodes of the tree somewhere else!
  // Modifying the tree in-place saves memory and speeds up multiple calls of
  // the method (e.g. when unrolling quantifiers).
  // |temp_ranges| is used for intermediate results, passed as parameter to
  // avoid allocating new lists all the time.
  static RegExpClassSetOperand* ComputeExpression(
      RegExpTree* root, ZoneList<CharacterRange>* temp_ranges, Zone* zone);

  const OperationType operation_;
  bool is_negated_;
  const bool may_contain_strings_;
  ZoneList<RegExpTree*>* operands_ = nullptr;
  int max_match_;
};

class RegExpAtom final : public RegExpTree {
 public:
  explicit RegExpAtom(base::Vector<const base::uc16> data) : data_(data) {}

  DECL_BOILERPLATE(Atom);

  bool IsTextElement() override { return true; }
  int min_match() override { return data_.length(); }
  int max_match() override { return data_.length(); }
  void AppendToText(RegExpText* text, Zone* zone) override;

  base::Vector<const base::uc16> data() const { return data_; }
  int length() const { return data_.length(); }

 private:
  base::Vector<const base::uc16> data_;
};

class TextElement final {
 public:
  enum TextType { ATOM, CLASS_RANGES };

  static TextElement Atom(RegExpAtom* atom);
  static TextElement ClassRanges(RegExpClassRanges* class_ranges);

  int cp_offset() const { return cp_offset_; }
  void set_cp_offset(int cp_offset) { cp_offset_ = cp_offset; }
  int length() const;

  TextType text_type() const { return text_type_; }

  RegExpTree* tree() const { return tree_; }

  RegExpAtom* atom() const {
    DCHECK(text_type() == ATOM);
    return reinterpret_cast<RegExpAtom*>(tree());
  }

  RegExpClassRanges* class_ranges() const {
    DCHECK(text_type() == CLASS_RANGES);
    return reinterpret_cast<RegExpClassRanges*>(tree());
  }

 private:
  TextElement(TextType text_type, RegExpTree* tree)
      : cp_offset_(-1), text_type_(text_type), tree_(tree) {}

  int cp_offset_;
  TextType text_type_;
  RegExpTree* tree_;
};

class RegExpText final : public RegExpTree {
 public:
  explicit RegExpText(Zone* zone) : elements_(2, zone) {}

  DECL_BOILERPLATE(Text);

  bool IsTextElement() override { return true; }
  int min_match() override { return length_; }
  int max_match() override { return length_; }
  void AppendToText(RegExpText* text, Zone* zone) override;
  void AddElement(TextElement elm, Zone* zone) {
    elements_.Add(elm, zone);
    length_ += elm.length();
  }
  ZoneList<TextElement>* elements() { return &elements_; }

 private:
  ZoneList<TextElement> elements_;
  int length_ = 0;
};


class RegExpQuantifier final : public RegExpTree {
 public:
  enum QuantifierType { GREEDY, NON_GREEDY, POSSESSIVE };
  RegExpQuantifier(int min, int max, QuantifierType type, RegExpTree* body)
      : body_(body),
        min_(min),
        max_(max),
        quantifier_type_(type) {
    if (min > 0 && body->min_match() > kInfinity / min) {
      min_match_ = kInfinity;
    } else {
      min_match_ = min * body->min_match();
    }
    if (max > 0 && body->max_match() > kInfinity / max) {
      max_match_ = kInfinity;
    } else {
      max_match_ = max * body->max_match();
    }
  }

  DECL_BOILERPLATE(Quantifier);

  static RegExpNode* ToNode(int min, int max, bool is_greedy, RegExpTree* body,
                            RegExpCompiler* compiler, RegExpNode* on_success,
                            bool not_at_start = false);
  Interval CaptureRegisters() override;
  int min_match() override { return min_match_; }
  int max_match() override { return max_match_; }
  int min() const { return min_; }
  int max() const { return max_; }
  QuantifierType quantifier_type() const { return quantifier_type_; }
  bool is_possessive() const { return quantifier_type_ == POSSESSIVE; }
  bool is_non_greedy() const { return quantifier_type_ == NON_GREEDY; }
  bool is_greedy() const { return quantifier_type_ == GREEDY; }
  RegExpTree* body() const { return body_; }

 private:
  RegExpTree* body_;
  int min_;
  int max_;
  int min_match_;
  int max_match_;
  QuantifierType quantifier_type_;
};


class RegExpCapture final : public RegExpTree {
 public:
  explicit RegExpCapture(int index)
      : body_(nullptr),
        index_(index),
        min_match_(0),
        max_match_(0),
        name_(nullptr) {}

  DECL_BOILERPLATE(Capture);

  static RegExpNode* ToNode(RegExpTree* body, int index,
                            RegExpCompiler* compiler, RegExpNode* on_success);
  bool IsAnchoredAtStart() override;
  bool IsAnchoredAtEnd() override;
  Interval CaptureRegisters() override;
  int min_match() override { return min_match_; }
  int max_match() override { return max_match_; }
  RegExpTree* body() { return body_; }
  void set_body(RegExpTree* body) {
    body_ = body;
    min_match_ = body->min_match();
    max_match_ = body->max_match();
  }
  int index() const { return index_; }
  const ZoneVector<base::uc16>* name() const { return name_; }
  void set_name(const ZoneVector<base::uc16>* name) { name_ = name; }
  static int StartRegister(int index) { return index * 2; }
  static int EndRegister(int index) { return index * 2 + 1; }

 private:
  RegExpTree* body_ = nullptr;
  int index_;
  int min_match_ = 0;
  int max_match_ = 0;
  const ZoneVector<base::uc16>* name_ = nullptr;
};

class RegExpGroup final : public RegExpTree {
 public:
  explicit RegExpGroup(RegExpTree* body, RegExpFlags flags)
      : body_(body),
        flags_(flags),
        min_match_(body->min_match()),
        max_match_(body->max_match()) {}

  DECL_BOILERPLATE(Group);

  bool IsAnchoredAtStart() override { return body_->IsAnchoredAtStart(); }
  bool IsAnchoredAtEnd() override { return body_->IsAnchoredAtEnd(); }
  int min_match() override { return min_match_; }
  int max_match() override { return max_match_; }
  Interval CaptureRegisters() override { return body_->CaptureRegisters(); }
  RegExpTree* body() const { return body_; }
  RegExpFlags flags() const { return flags_; }

 private:
  RegExpTree* body_;
  const RegExpFlags flags_;
  int min_match_;
  int max_match_;
};

class RegExpLookaround final : public RegExpTree {
 public:
  enum Type { LOOKAHEAD, LOOKBEHIND };

  RegExpLookaround(RegExpTree* body, bool is_positive, int capture_count,
                   int capture_from, Type type, int index)
      : body_(body),
        is_positive_(is_positive),
        capture_count_(capture_count),
        capture_from_(capture_from),
        type_(type),
        index_(index) {}

  DECL_BOILERPLATE(Lookaround);

  Interval CaptureRegisters() override;
  bool IsAnchoredAtStart() override;
  int min_match() override { return 0; }
  int max_match() override { return 0; }
  RegExpTree* body() const { return body_; }
  bool is_positive() const { return is_positive_; }
  int capture_count() const { return capture_count_; }
  int capture_from() const { return capture_from_; }
  Type type() const { return type_; }
  int index() const { return index_; }

  class Builder {
   public:
    Builder(bool is_positive, RegExpNode* on_success,
            int stack_pointer_register, int position_register,
            int capture_register_count = 0, int capture_register_start = 0);
    RegExpNode* on_match_success() const { return on_match_success_; }
    RegExpNode* ForMatch(RegExpNode* match);

   private:
    bool is_positive_;
    RegExpNode* on_match_success_;
    RegExpNode* on_success_;
    int stack_pointer_register_;
    int position_register_;
  };

 private:
  RegExpTree* body_;
  bool is_positive_;
  int capture_count_;
  int capture_from_;
  Type type_;
  int index_;
};


class RegExpBackReference final : public RegExpTree {
 public:
  explicit RegExpBackReference(Zone* zone) : captures_(1, zone) {}
  explicit RegExpBackReference(RegExpCapture* capture, Zone* zone)
      : captures_(1, zone) {
    captures_.Add(capture, zone);
  }

  DECL_BOILERPLATE(BackReference);

  int min_match() override { return 0; }
  // The back reference may be recursive, e.g. /(\2)(\1)/. To avoid infinite
  // recursion, we give up. Ignorance is bliss.
  int max_match() override { return kInfinity; }
  const ZoneList<RegExpCapture*>* captures() const { return &captures_; }
  void add_capture(RegExpCapture* capture, Zone* zone) {
    captures_.Add(capture, zone);
  }
  const ZoneVector<base::uc16>* name() const { return name_; }
  void set_name(const ZoneVector<base::uc16>* name) { name_ = name; }

 private:
  ZoneList<RegExpCapture*> captures_;
  const ZoneVector<base::uc16>* name_ = nullptr;
};


class RegExpEmpty final : public RegExpTree {
 public:
  DECL_BOILERPLATE(Empty);
  int min_match() override { return 0; }
  int max_match() override { return 0; }
};

}  // namespace internal
}  // namespace v8

#undef DECL_BOILERPLATE

#endif  // V8_REGEXP_REGEXP_AST_H_

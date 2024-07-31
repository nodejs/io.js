// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/objects/string.h"
#include "src/regexp/regexp-compiler.h"
#include "src/regexp/regexp.h"
#include "src/strings/unicode-inl.h"
#include "src/zone/zone-list-inl.h"

#ifdef V8_INTL_SUPPORT
#include "src/base/strings.h"
#include "src/regexp/special-case.h"
#include "unicode/locid.h"
#include "unicode/uniset.h"
#include "unicode/utypes.h"
#endif  // V8_INTL_SUPPORT

namespace v8 {
namespace internal {

using namespace regexp_compiler_constants;  // NOLINT(build/namespaces)

constexpr base::uc32 kMaxCodePoint = 0x10ffff;
constexpr int kMaxUtf16CodeUnit = 0xffff;
constexpr uint32_t kMaxUtf16CodeUnitU = 0xffff;
constexpr int32_t kMaxOneByteCharCode = unibrow::Latin1::kMaxChar;

// -------------------------------------------------------------------
// Tree to graph conversion

RegExpNode* RegExpAtom::ToNode(RegExpCompiler* compiler,
                               RegExpNode* on_success) {
  ZoneList<TextElement>* elms =
      compiler->zone()->New<ZoneList<TextElement>>(1, compiler->zone());
  elms->Add(TextElement::Atom(this), compiler->zone());
  return compiler->zone()->New<TextNode>(elms, compiler->read_backward(),
                                         on_success);
}

RegExpNode* RegExpText::ToNode(RegExpCompiler* compiler,
                               RegExpNode* on_success) {
  return compiler->zone()->New<TextNode>(elements(), compiler->read_backward(),
                                         on_success);
}

namespace {

bool CompareInverseRanges(ZoneList<CharacterRange>* ranges,
                          const int* special_class, int length) {
  length--;  // Remove final marker.

  DCHECK_EQ(kRangeEndMarker, special_class[length]);
  DCHECK_NE(0, ranges->length());
  DCHECK_NE(0, length);
  DCHECK_NE(0, special_class[0]);

  if (ranges->length() != (length >> 1) + 1) return false;

  CharacterRange range = ranges->at(0);
  if (range.from() != 0) return false;

  for (int i = 0; i < length; i += 2) {
    if (static_cast<base::uc32>(special_class[i]) != (range.to() + 1)) {
      return false;
    }
    range = ranges->at((i >> 1) + 1);
    if (static_cast<base::uc32>(special_class[i + 1]) != range.from()) {
      return false;
    }
  }

  return range.to() == kMaxCodePoint;
}

bool CompareRanges(ZoneList<CharacterRange>* ranges, const int* special_class,
                   int length) {
  length--;  // Remove final marker.

  DCHECK_EQ(kRangeEndMarker, special_class[length]);
  if (ranges->length() * 2 != length) return false;

  for (int i = 0; i < length; i += 2) {
    CharacterRange range = ranges->at(i >> 1);
    if (range.from() != static_cast<base::uc32>(special_class[i]) ||
        range.to() != static_cast<base::uc32>(special_class[i + 1] - 1)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool RegExpClassRanges::is_standard(Zone* zone) {
  // TODO(lrn): Remove need for this function, by not throwing away information
  // along the way.
  if (is_negated()) {
    return false;
  }
  if (set_.is_standard()) {
    return true;
  }
  if (CompareRanges(set_.ranges(zone), kSpaceRanges, kSpaceRangeCount)) {
    set_.set_standard_set_type(StandardCharacterSet::kWhitespace);
    return true;
  }
  if (CompareInverseRanges(set_.ranges(zone), kSpaceRanges, kSpaceRangeCount)) {
    set_.set_standard_set_type(StandardCharacterSet::kNotWhitespace);
    return true;
  }
  if (CompareInverseRanges(set_.ranges(zone), kLineTerminatorRanges,
                           kLineTerminatorRangeCount)) {
    set_.set_standard_set_type(StandardCharacterSet::kNotLineTerminator);
    return true;
  }
  if (CompareRanges(set_.ranges(zone), kLineTerminatorRanges,
                    kLineTerminatorRangeCount)) {
    set_.set_standard_set_type(StandardCharacterSet::kLineTerminator);
    return true;
  }
  if (CompareRanges(set_.ranges(zone), kWordRanges, kWordRangeCount)) {
    set_.set_standard_set_type(StandardCharacterSet::kWord);
    return true;
  }
  if (CompareInverseRanges(set_.ranges(zone), kWordRanges, kWordRangeCount)) {
    set_.set_standard_set_type(StandardCharacterSet::kNotWord);
    return true;
  }
  return false;
}

UnicodeRangeSplitter::UnicodeRangeSplitter(ZoneList<CharacterRange>* base) {
  // The unicode range splitter categorizes given character ranges into:
  // - Code points from the BMP representable by one code unit.
  // - Code points outside the BMP that need to be split into
  // surrogate pairs.
  // - Lone lead surrogates.
  // - Lone trail surrogates.
  // Lone surrogates are valid code points, even though no actual characters.
  // They require special matching to make sure we do not split surrogate pairs.

  for (int i = 0; i < base->length(); i++) AddRange(base->at(i));
}

void UnicodeRangeSplitter::AddRange(CharacterRange range) {
  static constexpr base::uc32 kBmp1Start = 0;
  static constexpr base::uc32 kBmp1End = kLeadSurrogateStart - 1;
  static constexpr base::uc32 kBmp2Start = kTrailSurrogateEnd + 1;
  static constexpr base::uc32 kBmp2End = kNonBmpStart - 1;

  // Ends are all inclusive.
  static_assert(kBmp1Start == 0);
  static_assert(kBmp1Start < kBmp1End);
  static_assert(kBmp1End + 1 == kLeadSurrogateStart);
  static_assert(kLeadSurrogateStart < kLeadSurrogateEnd);
  static_assert(kLeadSurrogateEnd + 1 == kTrailSurrogateStart);
  static_assert(kTrailSurrogateStart < kTrailSurrogateEnd);
  static_assert(kTrailSurrogateEnd + 1 == kBmp2Start);
  static_assert(kBmp2Start < kBmp2End);
  static_assert(kBmp2End + 1 == kNonBmpStart);
  static_assert(kNonBmpStart < kNonBmpEnd);

  static constexpr base::uc32 kStarts[] = {
      kBmp1Start, kLeadSurrogateStart, kTrailSurrogateStart,
      kBmp2Start, kNonBmpStart,
  };

  static constexpr base::uc32 kEnds[] = {
      kBmp1End, kLeadSurrogateEnd, kTrailSurrogateEnd, kBmp2End, kNonBmpEnd,
  };

  CharacterRangeVector* const kTargets[] = {
      &bmp_, &lead_surrogates_, &trail_surrogates_, &bmp_, &non_bmp_,
  };

  static constexpr int kCount = arraysize(kStarts);
  static_assert(kCount == arraysize(kEnds));
  static_assert(kCount == arraysize(kTargets));

  for (int i = 0; i < kCount; i++) {
    if (kStarts[i] > range.to()) break;
    const base::uc32 from = std::max(kStarts[i], range.from());
    const base::uc32 to = std::min(kEnds[i], range.to());
    if (from > to) continue;
    kTargets[i]->emplace_back(CharacterRange::Range(from, to));
  }
}

namespace {

// Translates between new and old V8-isms (SmallVector, ZoneList).
ZoneList<CharacterRange>* ToCanonicalZoneList(
    const UnicodeRangeSplitter::CharacterRangeVector* v, Zone* zone) {
  if (v->empty()) return nullptr;

  ZoneList<CharacterRange>* result =
      zone->New<ZoneList<CharacterRange>>(static_cast<int>(v->size()), zone);
  for (size_t i = 0; i < v->size(); i++) {
    result->Add(v->at(i), zone);
  }

  CharacterRange::Canonicalize(result);
  return result;
}

void AddBmpCharacters(RegExpCompiler* compiler, ChoiceNode* result,
                      RegExpNode* on_success, UnicodeRangeSplitter* splitter) {
  ZoneList<CharacterRange>* bmp =
      ToCanonicalZoneList(splitter->bmp(), compiler->zone());
  if (bmp == nullptr) return;
  result->AddAlternative(GuardedAlternative(TextNode::CreateForCharacterRanges(
      compiler->zone(), bmp, compiler->read_backward(), on_success)));
}

using UC16Range = uint32_t;  // {from, to} packed into one uint32_t.
constexpr UC16Range ToUC16Range(base::uc16 from, base::uc16 to) {
  return (static_cast<uint32_t>(from) << 16) | to;
}
constexpr base::uc16 ExtractFrom(UC16Range r) {
  return static_cast<base::uc16>(r >> 16);
}
constexpr base::uc16 ExtractTo(UC16Range r) {
  return static_cast<base::uc16>(r);
}

void AddNonBmpSurrogatePairs(RegExpCompiler* compiler, ChoiceNode* result,
                             RegExpNode* on_success,
                             UnicodeRangeSplitter* splitter) {
  DCHECK(!compiler->one_byte());
  Zone* const zone = compiler->zone();
  ZoneList<CharacterRange>* non_bmp =
      ToCanonicalZoneList(splitter->non_bmp(), zone);
  if (non_bmp == nullptr) return;

  // Translate each 32-bit code point range into the corresponding 16-bit code
  // unit representation consisting of the lead- and trail surrogate.
  //
  // The generated alternatives are grouped by the leading surrogate to avoid
  // emitting excessive code. For example, for
  //
  //  { \ud800[\udc00-\udc01]
  //  , \ud800[\udc05-\udc06]
  //  }
  //
  // there's no need to emit matching code for the leading surrogate \ud800
  // twice. We also create a dedicated grouping for full trailing ranges, i.e.
  // [dc00-dfff].
  ZoneUnorderedMap<UC16Range, ZoneList<CharacterRange>*> grouped_by_leading(
      zone);
  ZoneList<CharacterRange>* leading_with_full_trailing_range =
      zone->New<ZoneList<CharacterRange>>(1, zone);
  const auto AddRange = [&](base::uc16 from_l, base::uc16 to_l,
                            base::uc16 from_t, base::uc16 to_t) {
    const UC16Range leading_range = ToUC16Range(from_l, to_l);
    if (grouped_by_leading.count(leading_range) == 0) {
      if (from_t == kTrailSurrogateStart && to_t == kTrailSurrogateEnd) {
        leading_with_full_trailing_range->Add(
            CharacterRange::Range(from_l, to_l), zone);
        return;
      }
      grouped_by_leading[leading_range] =
          zone->New<ZoneList<CharacterRange>>(2, zone);
    }
    grouped_by_leading[leading_range]->Add(CharacterRange::Range(from_t, to_t),
                                           zone);
  };

  // First, create the grouped ranges.
  CharacterRange::Canonicalize(non_bmp);
  for (int i = 0; i < non_bmp->length(); i++) {
    // Match surrogate pair.
    // E.g. [\u10005-\u11005] becomes
    //      \ud800[\udc05-\udfff]|
    //      [\ud801-\ud803][\udc00-\udfff]|
    //      \ud804[\udc00-\udc05]
    base::uc32 from = non_bmp->at(i).from();
    base::uc32 to = non_bmp->at(i).to();
    base::uc16 from_l = unibrow::Utf16::LeadSurrogate(from);
    base::uc16 from_t = unibrow::Utf16::TrailSurrogate(from);
    base::uc16 to_l = unibrow::Utf16::LeadSurrogate(to);
    base::uc16 to_t = unibrow::Utf16::TrailSurrogate(to);

    if (from_l == to_l) {
      // The lead surrogate is the same.
      AddRange(from_l, to_l, from_t, to_t);
      continue;
    }

    if (from_t != kTrailSurrogateStart) {
      // Add [from_l][from_t-\udfff].
      AddRange(from_l, from_l, from_t, kTrailSurrogateEnd);
      from_l++;
    }
    if (to_t != kTrailSurrogateEnd) {
      // Add [to_l][\udc00-to_t].
      AddRange(to_l, to_l, kTrailSurrogateStart, to_t);
      to_l--;
    }
    if (from_l <= to_l) {
      // Add [from_l-to_l][\udc00-\udfff].
      AddRange(from_l, to_l, kTrailSurrogateStart, kTrailSurrogateEnd);
    }
  }

  // Create the actual TextNode now that ranges are fully grouped.
  if (!leading_with_full_trailing_range->is_empty()) {
    CharacterRange::Canonicalize(leading_with_full_trailing_range);
    result->AddAlternative(GuardedAlternative(TextNode::CreateForSurrogatePair(
        zone, leading_with_full_trailing_range,
        CharacterRange::Range(kTrailSurrogateStart, kTrailSurrogateEnd),
        compiler->read_backward(), on_success)));
  }
  for (const auto& it : grouped_by_leading) {
    CharacterRange leading_range =
        CharacterRange::Range(ExtractFrom(it.first), ExtractTo(it.first));
    ZoneList<CharacterRange>* trailing_ranges = it.second;
    CharacterRange::Canonicalize(trailing_ranges);
    result->AddAlternative(GuardedAlternative(TextNode::CreateForSurrogatePair(
        zone, leading_range, trailing_ranges, compiler->read_backward(),
        on_success)));
  }
}

RegExpNode* NegativeLookaroundAgainstReadDirectionAndMatch(
    RegExpCompiler* compiler, ZoneList<CharacterRange>* lookbehind,
    ZoneList<CharacterRange>* match, RegExpNode* on_success,
    bool read_backward) {
  Zone* zone = compiler->zone();
  RegExpNode* match_node = TextNode::CreateForCharacterRanges(
      zone, match, read_backward, on_success);
  int stack_register = compiler->UnicodeLookaroundStackRegister();
  int position_register = compiler->UnicodeLookaroundPositionRegister();
  RegExpLookaround::Builder lookaround(false, match_node, stack_register,
                                       position_register);
  RegExpNode* negative_match = TextNode::CreateForCharacterRanges(
      zone, lookbehind, !read_backward, lookaround.on_match_success());
  return lookaround.ForMatch(negative_match);
}

RegExpNode* MatchAndNegativeLookaroundInReadDirection(
    RegExpCompiler* compiler, ZoneList<CharacterRange>* match,
    ZoneList<CharacterRange>* lookahead, RegExpNode* on_success,
    bool read_backward) {
  Zone* zone = compiler->zone();
  int stack_register = compiler->UnicodeLookaroundStackRegister();
  int position_register = compiler->UnicodeLookaroundPositionRegister();
  RegExpLookaround::Builder lookaround(false, on_success, stack_register,
                                       position_register);
  RegExpNode* negative_match = TextNode::CreateForCharacterRanges(
      zone, lookahead, read_backward, lookaround.on_match_success());
  return TextNode::CreateForCharacterRanges(
      zone, match, read_backward, lookaround.ForMatch(negative_match));
}

void AddLoneLeadSurrogates(RegExpCompiler* compiler, ChoiceNode* result,
                           RegExpNode* on_success,
                           UnicodeRangeSplitter* splitter) {
  ZoneList<CharacterRange>* lead_surrogates =
      ToCanonicalZoneList(splitter->lead_surrogates(), compiler->zone());
  if (lead_surrogates == nullptr) return;
  Zone* zone = compiler->zone();
  // E.g. \ud801 becomes \ud801(?![\udc00-\udfff]).
  ZoneList<CharacterRange>* trail_surrogates = CharacterRange::List(
      zone, CharacterRange::Range(kTrailSurrogateStart, kTrailSurrogateEnd));

  RegExpNode* match;
  if (compiler->read_backward()) {
    // Reading backward. Assert that reading forward, there is no trail
    // surrogate, and then backward match the lead surrogate.
    match = NegativeLookaroundAgainstReadDirectionAndMatch(
        compiler, trail_surrogates, lead_surrogates, on_success, true);
  } else {
    // Reading forward. Forward match the lead surrogate and assert that
    // no trail surrogate follows.
    match = MatchAndNegativeLookaroundInReadDirection(
        compiler, lead_surrogates, trail_surrogates, on_success, false);
  }
  result->AddAlternative(GuardedAlternative(match));
}

void AddLoneTrailSurrogates(RegExpCompiler* compiler, ChoiceNode* result,
                            RegExpNode* on_success,
                            UnicodeRangeSplitter* splitter) {
  ZoneList<CharacterRange>* trail_surrogates =
      ToCanonicalZoneList(splitter->trail_surrogates(), compiler->zone());
  if (trail_surrogates == nullptr) return;
  Zone* zone = compiler->zone();
  // E.g. \udc01 becomes (?<![\ud800-\udbff])\udc01
  ZoneList<CharacterRange>* lead_surrogates = CharacterRange::List(
      zone, CharacterRange::Range(kLeadSurrogateStart, kLeadSurrogateEnd));

  RegExpNode* match;
  if (compiler->read_backward()) {
    // Reading backward. Backward match the trail surrogate and assert that no
    // lead surrogate precedes it.
    match = MatchAndNegativeLookaroundInReadDirection(
        compiler, trail_surrogates, lead_surrogates, on_success, true);
  } else {
    // Reading forward. Assert that reading backward, there is no lead
    // surrogate, and then forward match the trail surrogate.
    match = NegativeLookaroundAgainstReadDirectionAndMatch(
        compiler, lead_surrogates, trail_surrogates, on_success, false);
  }
  result->AddAlternative(GuardedAlternative(match));
}

RegExpNode* UnanchoredAdvance(RegExpCompiler* compiler,
                              RegExpNode* on_success) {
  // This implements ES2015 21.2.5.2.3, AdvanceStringIndex.
  DCHECK(!compiler->read_backward());
  Zone* zone = compiler->zone();
  // Advance any character. If the character happens to be a lead surrogate and
  // we advanced into the middle of a surrogate pair, it will work out, as
  // nothing will match from there. We will have to advance again, consuming
  // the associated trail surrogate.
  ZoneList<CharacterRange>* range =
      CharacterRange::List(zone, CharacterRange::Range(0, kMaxUtf16CodeUnit));
  return TextNode::CreateForCharacterRanges(zone, range, false, on_success);
}

}  // namespace

// static
void CharacterRange::AddUnicodeCaseEquivalents(ZoneList<CharacterRange>* ranges,
                                               Zone* zone) {
#ifdef V8_INTL_SUPPORT
  DCHECK(IsCanonical(ranges));

  // Micro-optimization to avoid passing large ranges to UnicodeSet::closeOver.
  // See also https://crbug.com/v8/6727.
  // TODO(jgruber): This only covers the special case of the {0,0x10FFFF} range,
  // which we use frequently internally. But large ranges can also easily be
  // created by the user. We might want to have a more general caching mechanism
  // for such ranges.
  if (ranges->length() == 1 && ranges->at(0).IsEverything(kNonBmpEnd)) return;

  // Use ICU to compute the case fold closure over the ranges.
  icu::UnicodeSet set;
  for (int i = 0; i < ranges->length(); i++) {
    set.add(ranges->at(i).from(), ranges->at(i).to());
  }
  // Clear the ranges list without freeing the backing store.
  ranges->Rewind(0);
  set.closeOver(USET_SIMPLE_CASE_INSENSITIVE);
  for (int i = 0; i < set.getRangeCount(); i++) {
    ranges->Add(Range(set.getRangeStart(i), set.getRangeEnd(i)), zone);
  }
  // No errors and everything we collected have been ranges.
  Canonicalize(ranges);
#endif  // V8_INTL_SUPPORT
}

RegExpNode* RegExpClassRanges::ToNode(RegExpCompiler* compiler,
                                      RegExpNode* on_success) {
  set_.Canonicalize();
  Zone* const zone = compiler->zone();
  ZoneList<CharacterRange>* ranges = this->ranges(zone);

  const bool needs_case_folding =
      NeedsUnicodeCaseEquivalents(compiler->flags()) && !is_case_folded();
  if (needs_case_folding) {
    CharacterRange::AddUnicodeCaseEquivalents(ranges, zone);
  }

  if (!IsEitherUnicode(compiler->flags()) || compiler->one_byte() ||
      contains_split_surrogate()) {
    return zone->New<TextNode>(this, compiler->read_backward(), on_success);
  }

  if (is_negated()) {
    // With /v, character classes are never negated.
    // https://tc39.es/ecma262/#sec-compileatom
    // Atom :: CharacterClass
    //   4. Assert: cc.[[Invert]] is false.
    // Instead the complement is created when evaluating the class set.
    // The only exception is the "nothing range" (negated everything), which is
    // internally created for an empty set.
    DCHECK_IMPLIES(
        IsUnicodeSets(compiler->flags()),
        ranges->length() == 1 && ranges->first().IsEverything(kMaxCodePoint));
    ZoneList<CharacterRange>* negated =
        zone->New<ZoneList<CharacterRange>>(2, zone);
    CharacterRange::Negate(ranges, negated, zone);
    ranges = negated;
  }

  if (ranges->length() == 0) {
    // The empty character class is used as a 'fail' node.
    RegExpClassRanges* fail = zone->New<RegExpClassRanges>(zone, ranges);
    return zone->New<TextNode>(fail, compiler->read_backward(), on_success);
  }

  if (set_.is_standard() &&
      standard_type() == StandardCharacterSet::kEverything) {
    return UnanchoredAdvance(compiler, on_success);
  }

  // Split ranges in order to handle surrogates correctly:
  // - Surrogate pairs: translate the 32-bit code point into two uc16 code
  //   units (irregexp operates only on code units).
  // - Lone surrogates: these require lookarounds to ensure we don't match in
  //   the middle of a surrogate pair.
  ChoiceNode* result = zone->New<ChoiceNode>(2, zone);
  UnicodeRangeSplitter splitter(ranges);
  AddBmpCharacters(compiler, result, on_success, &splitter);
  AddNonBmpSurrogatePairs(compiler, result, on_success, &splitter);
  AddLoneLeadSurrogates(compiler, result, on_success, &splitter);
  AddLoneTrailSurrogates(compiler, result, on_success, &splitter);

  static constexpr int kMaxRangesToInline = 32;  // Arbitrary.
  if (ranges->length() > kMaxRangesToInline) result->SetDoNotInline();

  return result;
}

RegExpNode* RegExpClassSetOperand::ToNode(RegExpCompiler* compiler,
                                          RegExpNode* on_success) {
  Zone* zone = compiler->zone();
  const int size = (has_strings() ? static_cast<int>(strings()->size()) : 0) +
                   (ranges()->is_empty() ? 0 : 1);
  if (size == 0) {
    // If neither ranges nor strings are present, the operand is equal to an
    // empty range (matching nothing).
    ZoneList<CharacterRange>* empty =
        zone->template New<ZoneList<CharacterRange>>(0, zone);
    return zone->template New<RegExpClassRanges>(zone, empty)
        ->ToNode(compiler, on_success);
  }
  ZoneList<RegExpTree*>* alternatives =
      zone->template New<ZoneList<RegExpTree*>>(size, zone);
  // Strings are sorted by length first (larger strings before shorter ones).
  // See the comment on CharacterClassStrings.
  // Empty strings (if present) are added after character ranges.
  RegExpTree* empty_string = nullptr;
  if (has_strings()) {
    for (auto string : *strings()) {
      if (string.second->IsEmpty()) {
        empty_string = string.second;
      } else {
        alternatives->Add(string.second, zone);
      }
    }
  }
  if (!ranges()->is_empty()) {
    // In unicode sets mode case folding has to be done at precise locations
    // (e.g. before building complements).
    // It is therefore the parsers responsibility to case fold (sub-) ranges
    // before creating ClassSetOperands.
    alternatives->Add(zone->template New<RegExpClassRanges>(
                          zone, ranges(), RegExpClassRanges::IS_CASE_FOLDED),
                      zone);
  }
  if (empty_string != nullptr) {
    alternatives->Add(empty_string, zone);
  }

  RegExpTree* node = nullptr;
  if (size == 1) {
    DCHECK_EQ(alternatives->length(), 1);
    node = alternatives->first();
  } else {
    node = zone->template New<RegExpDisjunction>(alternatives);
  }
  return node->ToNode(compiler, on_success);
}

RegExpNode* RegExpClassSetExpression::ToNode(RegExpCompiler* compiler,
                                             RegExpNode* on_success) {
  Zone* zone = compiler->zone();
  ZoneList<CharacterRange>* temp_ranges =
      zone->template New<ZoneList<CharacterRange>>(4, zone);
  RegExpClassSetOperand* root = ComputeExpression(this, temp_ranges, zone);
  return root->ToNode(compiler, on_success);
}

void RegExpClassSetOperand::Union(RegExpClassSetOperand* other, Zone* zone) {
  ranges()->AddAll(*other->ranges(), zone);
  if (other->has_strings()) {
    if (strings_ == nullptr) {
      strings_ = zone->template New<CharacterClassStrings>(zone);
    }
    strings()->insert(other->strings()->begin(), other->strings()->end());
  }
}

void RegExpClassSetOperand::Intersect(RegExpClassSetOperand* other,
                                      ZoneList<CharacterRange>* temp_ranges,
                                      Zone* zone) {
  CharacterRange::Intersect(ranges(), other->ranges(), temp_ranges, zone);
  std::swap(*ranges(), *temp_ranges);
  temp_ranges->Rewind(0);
  if (has_strings()) {
    if (!other->has_strings()) {
      strings()->clear();
    } else {
      for (auto iter = strings()->begin(); iter != strings()->end();) {
        if (other->strings()->find(iter->first) == other->strings()->end()) {
          iter = strings()->erase(iter);
        } else {
          iter++;
        }
      }
    }
  }
}

void RegExpClassSetOperand::Subtract(RegExpClassSetOperand* other,
                                     ZoneList<CharacterRange>* temp_ranges,
                                     Zone* zone) {
  CharacterRange::Subtract(ranges(), other->ranges(), temp_ranges, zone);
  std::swap(*ranges(), *temp_ranges);
  temp_ranges->Rewind(0);
  if (has_strings() && other->has_strings()) {
    for (auto iter = strings()->begin(); iter != strings()->end();) {
      if (other->strings()->find(iter->first) != other->strings()->end()) {
        iter = strings()->erase(iter);
      } else {
        iter++;
      }
    }
  }
}

// static
RegExpClassSetOperand* RegExpClassSetExpression::ComputeExpression(
    RegExpTree* root, ZoneList<CharacterRange>* temp_ranges, Zone* zone) {
  DCHECK(temp_ranges->is_empty());
  if (root->IsClassSetOperand()) {
    return root->AsClassSetOperand();
  }
  DCHECK(root->IsClassSetExpression());
  RegExpClassSetExpression* node = root->AsClassSetExpression();
  RegExpClassSetOperand* result =
      ComputeExpression(node->operands()->at(0), temp_ranges, zone);
  switch (node->operation()) {
    case OperationType::kUnion: {
      for (int i = 1; i < node->operands()->length(); i++) {
        RegExpClassSetOperand* op =
            ComputeExpression(node->operands()->at(i), temp_ranges, zone);
        result->Union(op, zone);
      }
      CharacterRange::Canonicalize(result->ranges());
      break;
    }
    case OperationType::kIntersection: {
      for (int i = 1; i < node->operands()->length(); i++) {
        RegExpClassSetOperand* op =
            ComputeExpression(node->operands()->at(i), temp_ranges, zone);
        result->Intersect(op, temp_ranges, zone);
      }
      break;
    }
    case OperationType::kSubtraction: {
      for (int i = 1; i < node->operands()->length(); i++) {
        RegExpClassSetOperand* op =
            ComputeExpression(node->operands()->at(i), temp_ranges, zone);
        result->Subtract(op, temp_ranges, zone);
      }
      break;
    }
  }
  if (node->is_negated()) {
    DCHECK(!result->has_strings());
    CharacterRange::Negate(result->ranges(), temp_ranges, zone);
    std::swap(*result->ranges(), *temp_ranges);
    temp_ranges->Rewind(0);
    node->is_negated_ = false;
  }
  // Store the result as single operand of the current node.
  node->operands()->Set(0, result);
  node->operands()->Rewind(1);

  return result;
}

namespace {

int CompareFirstChar(RegExpTree* const* a, RegExpTree* const* b) {
  RegExpAtom* atom1 = (*a)->AsAtom();
  RegExpAtom* atom2 = (*b)->AsAtom();
  base::uc16 character1 = atom1->data().at(0);
  base::uc16 character2 = atom2->data().at(0);
  if (character1 < character2) return -1;
  if (character1 > character2) return 1;
  return 0;
}

#ifdef V8_INTL_SUPPORT

int CompareCaseInsensitive(const icu::UnicodeString& a,
                           const icu::UnicodeString& b) {
  return a.caseCompare(b, U_FOLD_CASE_DEFAULT);
}

int CompareFirstCharCaseInsensitive(RegExpTree* const* a,
                                    RegExpTree* const* b) {
  RegExpAtom* atom1 = (*a)->AsAtom();
  RegExpAtom* atom2 = (*b)->AsAtom();
  return CompareCaseInsensitive(icu::UnicodeString{atom1->data().at(0)},
                                icu::UnicodeString{atom2->data().at(0)});
}

bool Equals(bool ignore_case, const icu::UnicodeString& a,
            const icu::UnicodeString& b) {
  if (a == b) return true;
  if (ignore_case) return CompareCaseInsensitive(a, b) == 0;
  return false;  // Case-sensitive equality already checked above.
}

bool CharAtEquals(bool ignore_case, int index, const RegExpAtom* a,
                  const RegExpAtom* b) {
  return Equals(ignore_case, a->data().at(index), b->data().at(index));
}

#else

unibrow::uchar Canonical(
    unibrow::Mapping<unibrow::Ecma262Canonicalize>* canonicalize,
    unibrow::uchar c) {
  unibrow::uchar chars[unibrow::Ecma262Canonicalize::kMaxWidth];
  int length = canonicalize->get(c, '\0', chars);
  DCHECK_LE(length, 1);
  unibrow::uchar canonical = c;
  if (length == 1) canonical = chars[0];
  return canonical;
}

int CompareCaseInsensitive(
    unibrow::Mapping<unibrow::Ecma262Canonicalize>* canonicalize,
    unibrow::uchar a, unibrow::uchar b) {
  if (a == b) return 0;
  if (a >= 'a' || b >= 'a') {
    a = Canonical(canonicalize, a);
    b = Canonical(canonicalize, b);
  }
  return static_cast<int>(a) - static_cast<int>(b);
}

int CompareFirstCharCaseInsensitive(
    unibrow::Mapping<unibrow::Ecma262Canonicalize>* canonicalize,
    RegExpTree* const* a, RegExpTree* const* b) {
  RegExpAtom* atom1 = (*a)->AsAtom();
  RegExpAtom* atom2 = (*b)->AsAtom();
  return CompareCaseInsensitive(canonicalize, atom1->data().at(0),
                                atom2->data().at(0));
}

bool Equals(bool ignore_case,
            unibrow::Mapping<unibrow::Ecma262Canonicalize>* canonicalize,
            unibrow::uchar a, unibrow::uchar b) {
  if (a == b) return true;
  if (ignore_case) {
    return CompareCaseInsensitive(canonicalize, a, b) == 0;
  }
  return false;  // Case-sensitive equality already checked above.
}

bool CharAtEquals(bool ignore_case,
                  unibrow::Mapping<unibrow::Ecma262Canonicalize>* canonicalize,
                  int index, const RegExpAtom* a, const RegExpAtom* b) {
  return Equals(ignore_case, canonicalize, a->data().at(index),
                b->data().at(index));
}

#endif  // V8_INTL_SUPPORT

}  // namespace

// We can stable sort runs of atoms, since the order does not matter if they
// start with different characters.
// Returns true if any consecutive atoms were found.
bool RegExpDisjunction::SortConsecutiveAtoms(RegExpCompiler* compiler) {
  ZoneList<RegExpTree*>* alternatives = this->alternatives();
  int length = alternatives->length();
  bool found_consecutive_atoms = false;
  for (int i = 0; i < length; i++) {
    while (i < length) {
      RegExpTree* alternative = alternatives->at(i);
      if (alternative->IsAtom()) break;
      i++;
    }
    // i is length or it is the index of an atom.
    if (i == length) break;
    int first_atom = i;
    i++;
    while (i < length) {
      RegExpTree* alternative = alternatives->at(i);
      if (!alternative->IsAtom()) break;
      i++;
    }
    // Sort atoms to get ones with common prefixes together.
    // This step is more tricky if we are in a case-independent regexp,
    // because it would change /is|I/ to /I|is/, and order matters when
    // the regexp parts don't match only disjoint starting points. To fix
    // this we have a version of CompareFirstChar that uses case-
    // independent character classes for comparison.
    DCHECK_LT(first_atom, alternatives->length());
    DCHECK_LE(i, alternatives->length());
    DCHECK_LE(first_atom, i);
    if (IsIgnoreCase(compiler->flags())) {
#ifdef V8_INTL_SUPPORT
      alternatives->StableSort(CompareFirstCharCaseInsensitive, first_atom,
                               i - first_atom);
#else
      unibrow::Mapping<unibrow::Ecma262Canonicalize>* canonicalize =
          compiler->isolate()->regexp_macro_assembler_canonicalize();
      auto compare_closure = [canonicalize](RegExpTree* const* a,
                                            RegExpTree* const* b) {
        return CompareFirstCharCaseInsensitive(canonicalize, a, b);
      };
      alternatives->StableSort(compare_closure, first_atom, i - first_atom);
#endif  // V8_INTL_SUPPORT
    } else {
      alternatives->StableSort(CompareFirstChar, first_atom, i - first_atom);
    }
    if (i - first_atom > 1) found_consecutive_atoms = true;
  }
  return found_consecutive_atoms;
}

// Optimizes ab|ac|az to a(?:b|c|d).
void RegExpDisjunction::RationalizeConsecutiveAtoms(RegExpCompiler* compiler) {
  Zone* zone = compiler->zone();
  ZoneList<RegExpTree*>* alternatives = this->alternatives();
  int length = alternatives->length();
  const bool ignore_case = IsIgnoreCase(compiler->flags());

  int write_posn = 0;
  int i = 0;
  while (i < length) {
    RegExpTree* alternative = alternatives->at(i);
    if (!alternative->IsAtom()) {
      alternatives->at(write_posn++) = alternatives->at(i);
      i++;
      continue;
    }
    RegExpAtom* const atom = alternative->AsAtom();
#ifdef V8_INTL_SUPPORT
    icu::UnicodeString common_prefix(atom->data().at(0));
#else
    unibrow::Mapping<unibrow::Ecma262Canonicalize>* const canonicalize =
        compiler->isolate()->regexp_macro_assembler_canonicalize();
    unibrow::uchar common_prefix = atom->data().at(0);
    if (ignore_case) {
      common_prefix = Canonical(canonicalize, common_prefix);
    }
#endif  // V8_INTL_SUPPORT
    int first_with_prefix = i;
    int prefix_length = atom->length();
    i++;
    while (i < length) {
      alternative = alternatives->at(i);
      if (!alternative->IsAtom()) break;
      RegExpAtom* const alt_atom = alternative->AsAtom();
#ifdef V8_INTL_SUPPORT
      icu::UnicodeString new_prefix(alt_atom->data().at(0));
      if (!Equals(ignore_case, new_prefix, common_prefix)) break;
#else
      unibrow::uchar new_prefix = alt_atom->data().at(0);
      if (!Equals(ignore_case, canonicalize, new_prefix, common_prefix)) break;
#endif  // V8_INTL_SUPPORT
      prefix_length = std::min(prefix_length, alt_atom->length());
      i++;
    }
    if (i > first_with_prefix + 2) {
      // Found worthwhile run of alternatives with common prefix of at least one
      // character.  The sorting function above did not sort on more than one
      // character for reasons of correctness, but there may still be a longer
      // common prefix if the terms were similar or presorted in the input.
      // Find out how long the common prefix is.
      int run_length = i - first_with_prefix;
      RegExpAtom* const alt_atom =
          alternatives->at(first_with_prefix)->AsAtom();
      for (int j = 1; j < run_length && prefix_length > 1; j++) {
        RegExpAtom* old_atom =
            alternatives->at(j + first_with_prefix)->AsAtom();
        for (int k = 1; k < prefix_length; k++) {
#ifdef V8_INTL_SUPPORT
          if (!CharAtEquals(ignore_case, k, alt_atom, old_atom)) {
#else
          if (!CharAtEquals(ignore_case, canonicalize, k, alt_atom, old_atom)) {
#endif  // V8_INTL_SUPPORT
            prefix_length = k;
            break;
          }
        }
      }
      RegExpAtom* prefix =
          zone->New<RegExpAtom>(alt_atom->data().SubVector(0, prefix_length));
      ZoneList<RegExpTree*>* pair = zone->New<ZoneList<RegExpTree*>>(2, zone);
      pair->Add(prefix, zone);
      ZoneList<RegExpTree*>* suffixes =
          zone->New<ZoneList<RegExpTree*>>(run_length, zone);
      for (int j = 0; j < run_length; j++) {
        RegExpAtom* old_atom =
            alternatives->at(j + first_with_prefix)->AsAtom();
        int len = old_atom->length();
        if (len == prefix_length) {
          suffixes->Add(zone->New<RegExpEmpty>(), zone);
        } else {
          RegExpTree* suffix = zone->New<RegExpAtom>(
              old_atom->data().SubVector(prefix_length, old_atom->length()));
          suffixes->Add(suffix, zone);
        }
      }
      pair->Add(zone->New<RegExpDisjunction>(suffixes), zone);
      alternatives->at(write_posn++) = zone->New<RegExpAlternative>(pair);
    } else {
      // Just copy any non-worthwhile alternatives.
      for (int j = first_with_prefix; j < i; j++) {
        alternatives->at(write_posn++) = alternatives->at(j);
      }
    }
  }
  alternatives->Rewind(write_posn);  // Trim end of array.
}

// Optimizes b|c|z to [bcz].
void RegExpDisjunction::FixSingleCharacterDisjunctions(
    RegExpCompiler* compiler) {
  Zone* zone = compiler->zone();
  ZoneList<RegExpTree*>* alternatives = this->alternatives();
  int length = alternatives->length();

  int write_posn = 0;
  int i = 0;
  while (i < length) {
    RegExpTree* alternative = alternatives->at(i);
    if (!alternative->IsAtom()) {
      alternatives->at(write_posn++) = alternatives->at(i);
      i++;
      continue;
    }
    RegExpAtom* const atom = alternative->AsAtom();
    if (atom->length() != 1) {
      alternatives->at(write_posn++) = alternatives->at(i);
      i++;
      continue;
    }
    const RegExpFlags flags = compiler->flags();
    DCHECK_IMPLIES(IsEitherUnicode(flags),
                   !unibrow::Utf16::IsLeadSurrogate(atom->data().at(0)));
    bool contains_trail_surrogate =
        unibrow::Utf16::IsTrailSurrogate(atom->data().at(0));
    int first_in_run = i;
    i++;
    // Find a run of single-character atom alternatives that have identical
    // flags (case independence and unicode-ness).
    while (i < length) {
      alternative = alternatives->at(i);
      if (!alternative->IsAtom()) break;
      RegExpAtom* const alt_atom = alternative->AsAtom();
      if (alt_atom->length() != 1) break;
      DCHECK_IMPLIES(IsEitherUnicode(flags),
                     !unibrow::Utf16::IsLeadSurrogate(alt_atom->data().at(0)));
      contains_trail_surrogate |=
          unibrow::Utf16::IsTrailSurrogate(alt_atom->data().at(0));
      i++;
    }
    if (i > first_in_run + 1) {
      // Found non-trivial run of single-character alternatives.
      int run_length = i - first_in_run;
      ZoneList<CharacterRange>* ranges =
          zone->New<ZoneList<CharacterRange>>(2, zone);
      for (int j = 0; j < run_length; j++) {
        RegExpAtom* old_atom = alternatives->at(j + first_in_run)->AsAtom();
        DCHECK_EQ(old_atom->length(), 1);
        ranges->Add(CharacterRange::Singleton(old_atom->data().at(0)), zone);
      }
      RegExpClassRanges::ClassRangesFlags class_ranges_flags;
      if (IsEitherUnicode(flags) && contains_trail_surrogate) {
        class_ranges_flags = RegExpClassRanges::CONTAINS_SPLIT_SURROGATE;
      }
      alternatives->at(write_posn++) =
          zone->New<RegExpClassRanges>(zone, ranges, class_ranges_flags);
    } else {
      // Just copy any trivial alternatives.
      for (int j = first_in_run; j < i; j++) {
        alternatives->at(write_posn++) = alternatives->at(j);
      }
    }
  }
  alternatives->Rewind(write_posn);  // Trim end of array.
}

RegExpNode* RegExpDisjunction::ToNode(RegExpCompiler* compiler,
                                      RegExpNode* on_success) {
  compiler->ToNodeMaybeCheckForStackOverflow();

  ZoneList<RegExpTree*>* alternatives = this->alternatives();

  if (alternatives->length() > 2) {
    bool found_consecutive_atoms = SortConsecutiveAtoms(compiler);
    if (found_consecutive_atoms) RationalizeConsecutiveAtoms(compiler);
    FixSingleCharacterDisjunctions(compiler);
    if (alternatives->length() == 1) {
      return alternatives->at(0)->ToNode(compiler, on_success);
    }
  }

  int length = alternatives->length();

  ChoiceNode* result =
      compiler->zone()->New<ChoiceNode>(length, compiler->zone());
  for (int i = 0; i < length; i++) {
    GuardedAlternative alternative(
        alternatives->at(i)->ToNode(compiler, on_success));
    result->AddAlternative(alternative);
  }
  return result;
}

RegExpNode* RegExpQuantifier::ToNode(RegExpCompiler* compiler,
                                     RegExpNode* on_success) {
  return ToNode(min(), max(), is_greedy(), body(), compiler, on_success);
}

namespace {
// Desugar \b to (?<=\w)(?=\W)|(?<=\W)(?=\w) and
//         \B to (?<=\w)(?=\w)|(?<=\W)(?=\W)
RegExpNode* BoundaryAssertionAsLookaround(RegExpCompiler* compiler,
                                          RegExpNode* on_success,
                                          RegExpAssertion::Type type) {
  CHECK(NeedsUnicodeCaseEquivalents(compiler->flags()));
  Zone* zone = compiler->zone();
  ZoneList<CharacterRange>* word_range =
      zone->New<ZoneList<CharacterRange>>(2, zone);
  CharacterRange::AddClassEscape(StandardCharacterSet::kWord, word_range, true,
                                 zone);
  int stack_register = compiler->UnicodeLookaroundStackRegister();
  int position_register = compiler->UnicodeLookaroundPositionRegister();
  ChoiceNode* result = zone->New<ChoiceNode>(2, zone);
  // Add two choices. The (non-)boundary could start with a word or
  // a non-word-character.
  for (int i = 0; i < 2; i++) {
    bool lookbehind_for_word = i == 0;
    bool lookahead_for_word =
        (type == RegExpAssertion::Type::BOUNDARY) ^ lookbehind_for_word;
    // Look to the left.
    RegExpLookaround::Builder lookbehind(lookbehind_for_word, on_success,
                                         stack_register, position_register);
    RegExpNode* backward = TextNode::CreateForCharacterRanges(
        zone, word_range, true, lookbehind.on_match_success());
    // Look to the right.
    RegExpLookaround::Builder lookahead(lookahead_for_word,
                                        lookbehind.ForMatch(backward),
                                        stack_register, position_register);
    RegExpNode* forward = TextNode::CreateForCharacterRanges(
        zone, word_range, false, lookahead.on_match_success());
    result->AddAlternative(GuardedAlternative(lookahead.ForMatch(forward)));
  }
  return result;
}
}  // anonymous namespace

RegExpNode* RegExpAssertion::ToNode(RegExpCompiler* compiler,
                                    RegExpNode* on_success) {
  NodeInfo info;
  Zone* zone = compiler->zone();

  switch (assertion_type()) {
    case Type::START_OF_LINE:
      return AssertionNode::AfterNewline(on_success);
    case Type::START_OF_INPUT:
      return AssertionNode::AtStart(on_success);
    case Type::BOUNDARY:
      return NeedsUnicodeCaseEquivalents(compiler->flags())
                 ? BoundaryAssertionAsLookaround(compiler, on_success,
                                                 Type::BOUNDARY)
                 : AssertionNode::AtBoundary(on_success);
    case Type::NON_BOUNDARY:
      return NeedsUnicodeCaseEquivalents(compiler->flags())
                 ? BoundaryAssertionAsLookaround(compiler, on_success,
                                                 Type::NON_BOUNDARY)
                 : AssertionNode::AtNonBoundary(on_success);
    case Type::END_OF_INPUT:
      return AssertionNode::AtEnd(on_success);
    case Type::END_OF_LINE: {
      // Compile $ in multiline regexps as an alternation with a positive
      // lookahead in one side and an end-of-input on the other side.
      // We need two registers for the lookahead.
      int stack_pointer_register = compiler->AllocateRegister();
      int position_register = compiler->AllocateRegister();
      // The ChoiceNode to distinguish between a newline and end-of-input.
      ChoiceNode* result = zone->New<ChoiceNode>(2, zone);
      // Create a newline atom.
      ZoneList<CharacterRange>* newline_ranges =
          zone->New<ZoneList<CharacterRange>>(3, zone);
      CharacterRange::AddClassEscape(StandardCharacterSet::kLineTerminator,
                                     newline_ranges, false, zone);
      RegExpClassRanges* newline_atom =
          zone->New<RegExpClassRanges>(StandardCharacterSet::kLineTerminator);
      TextNode* newline_matcher =
          zone->New<TextNode>(newline_atom, false,
                              ActionNode::PositiveSubmatchSuccess(
                                  stack_pointer_register, position_register,
                                  0,   // No captures inside.
                                  -1,  // Ignored if no captures.
                                  on_success));
      // Create an end-of-input matcher.
      RegExpNode* end_of_line = ActionNode::BeginPositiveSubmatch(
          stack_pointer_register, position_register, newline_matcher);
      // Add the two alternatives to the ChoiceNode.
      GuardedAlternative eol_alternative(end_of_line);
      result->AddAlternative(eol_alternative);
      GuardedAlternative end_alternative(AssertionNode::AtEnd(on_success));
      result->AddAlternative(end_alternative);
      return result;
    }
    default:
      UNREACHABLE();
  }
}

RegExpNode* RegExpBackReference::ToNode(RegExpCompiler* compiler,
                                        RegExpNode* on_success) {
  RegExpNode* backref_node = on_success;
  // Only one of the captures in the list can actually match. Since
  // back-references to unmatched captures are treated as empty, we can simply
  // create back-references to all possible captures.
  for (auto capture : *captures()) {
    backref_node = compiler->zone()->New<BackReferenceNode>(
        RegExpCapture::StartRegister(capture->index()),
        RegExpCapture::EndRegister(capture->index()), compiler->read_backward(),
        backref_node);
  }
  return backref_node;
}

RegExpNode* RegExpEmpty::ToNode(RegExpCompiler* compiler,
                                RegExpNode* on_success) {
  return on_success;
}

namespace {

class V8_NODISCARD ModifiersScope {
 public:
  ModifiersScope(RegExpCompiler* compiler, RegExpFlags flags)
      : compiler_(compiler), previous_flags_(compiler->flags()) {
    compiler->set_flags(flags);
  }
  ~ModifiersScope() { compiler_->set_flags(previous_flags_); }

 private:
  RegExpCompiler* compiler_;
  const RegExpFlags previous_flags_;
};

}  // namespace

RegExpNode* RegExpGroup::ToNode(RegExpCompiler* compiler,
                                RegExpNode* on_success) {
  // If no flags are modified, simply convert and return the body.
  if (flags() == compiler->flags()) {
    return body_->ToNode(compiler, on_success);
  }
  // Reset flags for successor node.
  const RegExpFlags old_flags = compiler->flags();
  on_success = ActionNode::ModifyFlags(old_flags, on_success);

  // Convert body using modifier.
  ModifiersScope modifiers_scope(compiler, flags());
  RegExpNode* body = body_->ToNode(compiler, on_success);

  // Wrap body into modifier node.
  RegExpNode* modified_body = ActionNode::ModifyFlags(flags(), body);
  return modified_body;
}

RegExpLookaround::Builder::Builder(bool is_positive, RegExpNode* on_success,
                                   int stack_pointer_register,
                                   int position_register,
                                   int capture_register_count,
                                   int capture_register_start)
    : is_positive_(is_positive),
      on_success_(on_success),
      stack_pointer_register_(stack_pointer_register),
      position_register_(position_register) {
  if (is_positive_) {
    on_match_success_ = ActionNode::PositiveSubmatchSuccess(
        stack_pointer_register, position_register, capture_register_count,
        capture_register_start, on_success_);
  } else {
    Zone* zone = on_success_->zone();
    on_match_success_ = zone->New<NegativeSubmatchSuccess>(
        stack_pointer_register, position_register, capture_register_count,
        capture_register_start, zone);
  }
}

RegExpNode* RegExpLookaround::Builder::ForMatch(RegExpNode* match) {
  if (is_positive_) {
    return ActionNode::BeginPositiveSubmatch(stack_pointer_register_,
                                             position_register_, match);
  } else {
    Zone* zone = on_success_->zone();
    // We use a ChoiceNode to represent the negative lookaround. The first
    // alternative is the negative match. On success, the end node backtracks.
    // On failure, the second alternative is tried and leads to success.
    // NegativeLookaheadChoiceNode is a special ChoiceNode that ignores the
    // first exit when calculating quick checks.
    ChoiceNode* choice_node = zone->New<NegativeLookaroundChoiceNode>(
        GuardedAlternative(match), GuardedAlternative(on_success_), zone);
    return ActionNode::BeginNegativeSubmatch(stack_pointer_register_,
                                             position_register_, choice_node);
  }
}

RegExpNode* RegExpLookaround::ToNode(RegExpCompiler* compiler,
                                     RegExpNode* on_success) {
  int stack_pointer_register = compiler->AllocateRegister();
  int position_register = compiler->AllocateRegister();

  const int registers_per_capture = 2;
  const int register_of_first_capture = 2;
  int register_count = capture_count_ * registers_per_capture;
  int register_start =
      register_of_first_capture + capture_from_ * registers_per_capture;

  RegExpNode* result;
  bool was_reading_backward = compiler->read_backward();
  compiler->set_read_backward(type() == LOOKBEHIND);
  Builder builder(is_positive(), on_success, stack_pointer_register,
                  position_register, register_count, register_start);
  RegExpNode* match = body_->ToNode(compiler, builder.on_match_success());
  result = builder.ForMatch(match);
  compiler->set_read_backward(was_reading_backward);
  return result;
}

RegExpNode* RegExpCapture::ToNode(RegExpCompiler* compiler,
                                  RegExpNode* on_success) {
  return ToNode(body(), index(), compiler, on_success);
}

RegExpNode* RegExpCapture::ToNode(RegExpTree* body, int index,
                                  RegExpCompiler* compiler,
                                  RegExpNode* on_success) {
  DCHECK_NOT_NULL(body);
  int start_reg = RegExpCapture::StartRegister(index);
  int end_reg = RegExpCapture::EndRegister(index);
  if (compiler->read_backward()) std::swap(start_reg, end_reg);
  RegExpNode* store_end = ActionNode::StorePosition(end_reg, true, on_success);
  RegExpNode* body_node = body->ToNode(compiler, store_end);
  return ActionNode::StorePosition(start_reg, true, body_node);
}

namespace {

class AssertionSequenceRewriter final {
 public:
  // TODO(jgruber): Consider moving this to a separate AST tree rewriter pass
  // instead of sprinkling rewrites into the AST->Node conversion process.
  static void MaybeRewrite(ZoneList<RegExpTree*>* terms, Zone* zone) {
    AssertionSequenceRewriter rewriter(terms, zone);

    static constexpr int kNoIndex = -1;
    int from = kNoIndex;

    for (int i = 0; i < terms->length(); i++) {
      RegExpTree* t = terms->at(i);
      if (from == kNoIndex && t->IsAssertion()) {
        from = i;  // Start a sequence.
      } else if (from != kNoIndex && !t->IsAssertion()) {
        // Terminate and process the sequence.
        if (i - from > 1) rewriter.Rewrite(from, i);
        from = kNoIndex;
      }
    }

    if (from != kNoIndex && terms->length() - from > 1) {
      rewriter.Rewrite(from, terms->length());
    }
  }

  // All assertions are zero width. A consecutive sequence of assertions is
  // order-independent. There's two ways we can optimize here:
  // 1. fold all identical assertions.
  // 2. if any assertion combinations are known to fail (e.g. \b\B), the entire
  //    sequence fails.
  void Rewrite(int from, int to) {
    DCHECK_GT(to, from + 1);

    // Bitfield of all seen assertions.
    uint32_t seen_assertions = 0;
    static_assert(static_cast<int>(RegExpAssertion::Type::LAST_ASSERTION_TYPE) <
                  kUInt32Size * kBitsPerByte);

    for (int i = from; i < to; i++) {
      RegExpAssertion* t = terms_->at(i)->AsAssertion();
      const uint32_t bit = 1 << static_cast<int>(t->assertion_type());

      if (seen_assertions & bit) {
        // Fold duplicates.
        terms_->Set(i, zone_->New<RegExpEmpty>());
      }

      seen_assertions |= bit;
    }

    // Collapse failures.
    const uint32_t always_fails_mask =
        1 << static_cast<int>(RegExpAssertion::Type::BOUNDARY) |
        1 << static_cast<int>(RegExpAssertion::Type::NON_BOUNDARY);
    if ((seen_assertions & always_fails_mask) == always_fails_mask) {
      ReplaceSequenceWithFailure(from, to);
    }
  }

  void ReplaceSequenceWithFailure(int from, int to) {
    // Replace the entire sequence with a single node that always fails.
    // TODO(jgruber): Consider adding an explicit Fail kind. Until then, the
    // negated '*' (everything) range serves the purpose.
    ZoneList<CharacterRange>* ranges =
        zone_->New<ZoneList<CharacterRange>>(0, zone_);
    RegExpClassRanges* cc = zone_->New<RegExpClassRanges>(zone_, ranges);
    terms_->Set(from, cc);

    // Zero out the rest.
    RegExpEmpty* empty = zone_->New<RegExpEmpty>();
    for (int i = from + 1; i < to; i++) terms_->Set(i, empty);
  }

 private:
  AssertionSequenceRewriter(ZoneList<RegExpTree*>* terms, Zone* zone)
      : zone_(zone), terms_(terms) {}

  Zone* zone_;
  ZoneList<RegExpTree*>* terms_;
};

}  // namespace

RegExpNode* RegExpAlternative::ToNode(RegExpCompiler* compiler,
                                      RegExpNode* on_success) {
  compiler->ToNodeMaybeCheckForStackOverflow();

  ZoneList<RegExpTree*>* children = nodes();

  AssertionSequenceRewriter::MaybeRewrite(children, compiler->zone());

  RegExpNode* current = on_success;
  if (compiler->read_backward()) {
    for (int i = 0; i < children->length(); i++) {
      current = children->at(i)->ToNode(compiler, current);
    }
  } else {
    for (int i = children->length() - 1; i >= 0; i--) {
      current = children->at(i)->ToNode(compiler, current);
    }
  }
  return current;
}

namespace {

void AddClass(const int* elmv, int elmc, ZoneList<CharacterRange>* ranges,
              Zone* zone) {
  elmc--;
  DCHECK_EQ(kRangeEndMarker, elmv[elmc]);
  for (int i = 0; i < elmc; i += 2) {
    DCHECK(elmv[i] < elmv[i + 1]);
    ranges->Add(CharacterRange::Range(elmv[i], elmv[i + 1] - 1), zone);
  }
}

void AddClassNegated(const int* elmv, int elmc,
                     ZoneList<CharacterRange>* ranges, Zone* zone) {
  elmc--;
  DCHECK_EQ(kRangeEndMarker, elmv[elmc]);
  DCHECK_NE(0x0000, elmv[0]);
  DCHECK_NE(kMaxCodePoint, elmv[elmc - 1]);
  base::uc16 last = 0x0000;
  for (int i = 0; i < elmc; i += 2) {
    DCHECK(last <= elmv[i] - 1);
    DCHECK(elmv[i] < elmv[i + 1]);
    ranges->Add(CharacterRange::Range(last, elmv[i] - 1), zone);
    last = elmv[i + 1];
  }
  ranges->Add(CharacterRange::Range(last, kMaxCodePoint), zone);
}

}  // namespace

void CharacterRange::AddClassEscape(StandardCharacterSet standard_character_set,
                                    ZoneList<CharacterRange>* ranges,
                                    bool add_unicode_case_equivalents,
                                    Zone* zone) {
  if (add_unicode_case_equivalents &&
      (standard_character_set == StandardCharacterSet::kWord ||
       standard_character_set == StandardCharacterSet::kNotWord)) {
    // See #sec-runtime-semantics-wordcharacters-abstract-operation
    // In case of unicode and ignore_case, we need to create the closure over
    // case equivalent characters before negating.
    ZoneList<CharacterRange>* new_ranges =
        zone->New<ZoneList<CharacterRange>>(2, zone);
    AddClass(kWordRanges, kWordRangeCount, new_ranges, zone);
    AddUnicodeCaseEquivalents(new_ranges, zone);
    if (standard_character_set == StandardCharacterSet::kNotWord) {
      ZoneList<CharacterRange>* negated =
          zone->New<ZoneList<CharacterRange>>(2, zone);
      CharacterRange::Negate(new_ranges, negated, zone);
      new_ranges = negated;
    }
    ranges->AddAll(*new_ranges, zone);
    return;
  }

  switch (standard_character_set) {
    case StandardCharacterSet::kWhitespace:
      AddClass(kSpaceRanges, kSpaceRangeCount, ranges, zone);
      break;
    case StandardCharacterSet::kNotWhitespace:
      AddClassNegated(kSpaceRanges, kSpaceRangeCount, ranges, zone);
      break;
    case StandardCharacterSet::kWord:
      AddClass(kWordRanges, kWordRangeCount, ranges, zone);
      break;
    case StandardCharacterSet::kNotWord:
      AddClassNegated(kWordRanges, kWordRangeCount, ranges, zone);
      break;
    case StandardCharacterSet::kDigit:
      AddClass(kDigitRanges, kDigitRangeCount, ranges, zone);
      break;
    case StandardCharacterSet::kNotDigit:
      AddClassNegated(kDigitRanges, kDigitRangeCount, ranges, zone);
      break;
    // This is the set of characters matched by the $ and ^ symbols
    // in multiline mode.
    case StandardCharacterSet::kLineTerminator:
      AddClass(kLineTerminatorRanges, kLineTerminatorRangeCount, ranges, zone);
      break;
    case StandardCharacterSet::kNotLineTerminator:
      AddClassNegated(kLineTerminatorRanges, kLineTerminatorRangeCount, ranges,
                      zone);
      break;
    // This is not a character range as defined by the spec but a
    // convenient shorthand for a character class that matches any
    // character.
    case StandardCharacterSet::kEverything:
      ranges->Add(CharacterRange::Everything(), zone);
      break;
  }
}

// static
void CharacterRange::AddCaseEquivalents(Isolate* isolate, Zone* zone,
                                        ZoneList<CharacterRange>* ranges,
                                        bool is_one_byte) {
  CharacterRange::Canonicalize(ranges);
  int range_count = ranges->length();
#ifdef V8_INTL_SUPPORT
  icu::UnicodeSet others;
  for (int i = 0; i < range_count; i++) {
    CharacterRange range = ranges->at(i);
    base::uc32 from = range.from();
    if (from > kMaxUtf16CodeUnit) continue;
    base::uc32 to = std::min({range.to(), kMaxUtf16CodeUnitU});
    // Nothing to be done for surrogates.
    if (from >= kLeadSurrogateStart && to <= kTrailSurrogateEnd) continue;
    if (is_one_byte && !RangeContainsLatin1Equivalents(range)) {
      if (from > kMaxOneByteCharCode) continue;
      if (to > kMaxOneByteCharCode) to = kMaxOneByteCharCode;
    }
    others.add(from, to);
  }

  // Compute the set of additional characters that should be added,
  // using UnicodeSet::closeOver. ECMA 262 defines slightly different
  // case-folding rules than Unicode, so some characters that are
  // added by closeOver do not match anything other than themselves in
  // JS. For example, 'ſ' (U+017F LATIN SMALL LETTER LONG S) is the
  // same case-insensitive character as 's' or 'S' according to
  // Unicode, but does not match any other character in JS. To handle
  // this case, we add such characters to the IgnoreSet and filter
  // them out. We filter twice: once before calling closeOver (to
  // prevent 'ſ' from adding 's'), and once after calling closeOver
  // (to prevent 's' from adding 'ſ'). See regexp/special-case.h for
  // more information.
  icu::UnicodeSet already_added(others);
  others.removeAll(RegExpCaseFolding::IgnoreSet());
  others.closeOver(USET_CASE_INSENSITIVE);
  others.removeAll(RegExpCaseFolding::IgnoreSet());
  others.removeAll(already_added);

  // Add others to the ranges
  for (int32_t i = 0; i < others.getRangeCount(); i++) {
    UChar32 from = others.getRangeStart(i);
    UChar32 to = others.getRangeEnd(i);
    if (from == to) {
      ranges->Add(CharacterRange::Singleton(from), zone);
    } else {
      ranges->Add(CharacterRange::Range(from, to), zone);
    }
  }
#else
  for (int i = 0; i < range_count; i++) {
    CharacterRange range = ranges->at(i);
    base::uc32 bottom = range.from();
    if (bottom > kMaxUtf16CodeUnit) continue;
    base::uc32 top = std::min({range.to(), kMaxUtf16CodeUnitU});
    // Nothing to be done for surrogates.
    if (bottom >= kLeadSurrogateStart && top <= kTrailSurrogateEnd) continue;
    if (is_one_byte && !RangeContainsLatin1Equivalents(range)) {
      if (bottom > kMaxOneByteCharCode) continue;
      if (top > kMaxOneByteCharCode) top = kMaxOneByteCharCode;
    }
    unibrow::uchar chars[unibrow::Ecma262UnCanonicalize::kMaxWidth];
    if (top == bottom) {
      // If this is a singleton we just expand the one character.
      int length = isolate->jsregexp_uncanonicalize()->get(bottom, '\0', chars);
      for (int i = 0; i < length; i++) {
        base::uc32 chr = chars[i];
        if (chr != bottom) {
          ranges->Add(CharacterRange::Singleton(chars[i]), zone);
        }
      }
    } else {
      // If this is a range we expand the characters block by block, expanding
      // contiguous subranges (blocks) one at a time.  The approach is as
      // follows.  For a given start character we look up the remainder of the
      // block that contains it (represented by the end point), for instance we
      // find 'z' if the character is 'c'.  A block is characterized by the
      // property that all characters uncanonicalize in the same way, except
      // that each entry in the result is incremented by the distance from the
      // first element.  So a-z is a block because 'a' uncanonicalizes to ['a',
      // 'A'] and the k'th letter uncanonicalizes to ['a' + k, 'A' + k].  Once
      // we've found the end point we look up its uncanonicalization and
      // produce a range for each element.  For instance for [c-f] we look up
      // ['z', 'Z'] and produce [c-f] and [C-F].  We then only add a range if
      // it is not already contained in the input, so [c-f] will be skipped but
      // [C-F] will be added.  If this range is not completely contained in a
      // block we do this for all the blocks covered by the range (handling
      // characters that is not in a block as a "singleton block").
      unibrow::uchar equivalents[unibrow::Ecma262UnCanonicalize::kMaxWidth];
      base::uc32 pos = bottom;
      while (pos <= top) {
        int length =
            isolate->jsregexp_canonrange()->get(pos, '\0', equivalents);
        base::uc32 block_end;
        if (length == 0) {
          block_end = pos;
        } else {
          DCHECK_EQ(1, length);
          block_end = equivalents[0];
        }
        int end = (block_end > top) ? top : block_end;
        length = isolate->jsregexp_uncanonicalize()->get(block_end, '\0',
                                                         equivalents);
        for (int i = 0; i < length; i++) {
          base::uc32 c = equivalents[i];
          base::uc32 range_from = c - (block_end - pos);
          base::uc32 range_to = c - (block_end - end);
          if (!(bottom <= range_from && range_to <= top)) {
            ranges->Add(CharacterRange::Range(range_from, range_to), zone);
          }
        }
        pos = end + 1;
      }
    }
  }
#endif  // V8_INTL_SUPPORT
}

bool CharacterRange::IsCanonical(const ZoneList<CharacterRange>* ranges) {
  DCHECK_NOT_NULL(ranges);
  int n = ranges->length();
  if (n <= 1) return true;
  base::uc32 max = ranges->at(0).to();
  for (int i = 1; i < n; i++) {
    CharacterRange next_range = ranges->at(i);
    if (next_range.from() <= max + 1) return false;
    max = next_range.to();
  }
  return true;
}

ZoneList<CharacterRange>* CharacterSet::ranges(Zone* zone) {
  if (ranges_ == nullptr) {
    ranges_ = zone->New<ZoneList<CharacterRange>>(2, zone);
    CharacterRange::AddClassEscape(standard_set_type_.value(), ranges_, false,
                                   zone);
  }
  return ranges_;
}

namespace {

// Move a number of elements in a zonelist to another position
// in the same list. Handles overlapping source and target areas.
void MoveRanges(ZoneList<CharacterRange>* list, int from, int to, int count) {
  // Ranges are potentially overlapping.
  if (from < to) {
    for (int i = count - 1; i >= 0; i--) {
      list->at(to + i) = list->at(from + i);
    }
  } else {
    for (int i = 0; i < count; i++) {
      list->at(to + i) = list->at(from + i);
    }
  }
}

int InsertRangeInCanonicalList(ZoneList<CharacterRange>* list, int count,
                               CharacterRange insert) {
  // Inserts a range into list[0..count[, which must be sorted
  // by from value and non-overlapping and non-adjacent, using at most
  // list[0..count] for the result. Returns the number of resulting
  // canonicalized ranges. Inserting a range may collapse existing ranges into
  // fewer ranges, so the return value can be anything in the range 1..count+1.
  base::uc32 from = insert.from();
  base::uc32 to = insert.to();
  int start_pos = 0;
  int end_pos = count;
  for (int i = count - 1; i >= 0; i--) {
    CharacterRange current = list->at(i);
    if (current.from() > to + 1) {
      end_pos = i;
    } else if (current.to() + 1 < from) {
      start_pos = i + 1;
      break;
    }
  }

  // Inserted range overlaps, or is adjacent to, ranges at positions
  // [start_pos..end_pos[. Ranges before start_pos or at or after end_pos are
  // not affected by the insertion.
  // If start_pos == end_pos, the range must be inserted before start_pos.
  // if start_pos < end_pos, the entire range from start_pos to end_pos
  // must be merged with the insert range.

  if (start_pos == end_pos) {
    // Insert between existing ranges at position start_pos.
    if (start_pos < count) {
      MoveRanges(list, start_pos, start_pos + 1, count - start_pos);
    }
    list->at(start_pos) = insert;
    return count + 1;
  }
  if (start_pos + 1 == end_pos) {
    // Replace single existing range at position start_pos.
    CharacterRange to_replace = list->at(start_pos);
    int new_from = std::min(to_replace.from(), from);
    int new_to = std::max(to_replace.to(), to);
    list->at(start_pos) = CharacterRange::Range(new_from, new_to);
    return count;
  }
  // Replace a number of existing ranges from start_pos to end_pos - 1.
  // Move the remaining ranges down.

  int new_from = std::min(list->at(start_pos).from(), from);
  int new_to = std::max(list->at(end_pos - 1).to(), to);
  if (end_pos < count) {
    MoveRanges(list, end_pos, start_pos + 1, count - end_pos);
  }
  list->at(start_pos) = CharacterRange::Range(new_from, new_to);
  return count - (end_pos - start_pos) + 1;
}

}  // namespace

void CharacterSet::Canonicalize() {
  // Special/default classes are always considered canonical. The result
  // of calling ranges() will be sorted.
  if (ranges_ == nullptr) return;
  CharacterRange::Canonicalize(ranges_);
}

// static
void CharacterRange::Canonicalize(ZoneList<CharacterRange>* character_ranges) {
  if (character_ranges->length() <= 1) return;
  // Check whether ranges are already canonical (increasing, non-overlapping,
  // non-adjacent).
  int n = character_ranges->length();
  base::uc32 max = character_ranges->at(0).to();
  int i = 1;
  while (i < n) {
    CharacterRange current = character_ranges->at(i);
    if (current.from() <= max + 1) {
      break;
    }
    max = current.to();
    i++;
  }
  // Canonical until the i'th range. If that's all of them, we are done.
  if (i == n) return;

  // The ranges at index i and forward are not canonicalized. Make them so by
  // doing the equivalent of insertion sort (inserting each into the previous
  // list, in order).
  // Notice that inserting a range can reduce the number of ranges in the
  // result due to combining of adjacent and overlapping ranges.
  int read = i;           // Range to insert.
  int num_canonical = i;  // Length of canonicalized part of list.
  do {
    num_canonical = InsertRangeInCanonicalList(character_ranges, num_canonical,
                                               character_ranges->at(read));
    read++;
  } while (read < n);
  character_ranges->Rewind(num_canonical);

  DCHECK(CharacterRange::IsCanonical(character_ranges));
}

// static
void CharacterRange::Negate(const ZoneList<CharacterRange>* ranges,
                            ZoneList<CharacterRange>* negated_ranges,
                            Zone* zone) {
  DCHECK(CharacterRange::IsCanonical(ranges));
  DCHECK_EQ(0, negated_ranges->length());
  int range_count = ranges->length();
  base::uc32 from = 0;
  int i = 0;
  if (range_count > 0 && ranges->at(0).from() == 0) {
    from = ranges->at(0).to() + 1;
    i = 1;
  }
  while (i < range_count) {
    CharacterRange range = ranges->at(i);
    negated_ranges->Add(CharacterRange::Range(from, range.from() - 1), zone);
    from = range.to() + 1;
    i++;
  }
  if (from < kMaxCodePoint) {
    negated_ranges->Add(CharacterRange::Range(from, kMaxCodePoint), zone);
  }
}

// static
void CharacterRange::Intersect(const ZoneList<CharacterRange>* lhs,
                               const ZoneList<CharacterRange>* rhs,
                               ZoneList<CharacterRange>* intersection,
                               Zone* zone) {
  DCHECK(CharacterRange::IsCanonical(lhs));
  DCHECK(CharacterRange::IsCanonical(rhs));
  DCHECK_EQ(0, intersection->length());
  int lhs_index = 0;
  int rhs_index = 0;
  while (lhs_index < lhs->length() && rhs_index < rhs->length()) {
    // Skip non-overlapping ranges.
    if (lhs->at(lhs_index).to() < rhs->at(rhs_index).from()) {
      lhs_index++;
      continue;
    }
    if (rhs->at(rhs_index).to() < lhs->at(lhs_index).from()) {
      rhs_index++;
      continue;
    }

    base::uc32 from =
        std::max(lhs->at(lhs_index).from(), rhs->at(rhs_index).from());
    base::uc32 to = std::min(lhs->at(lhs_index).to(), rhs->at(rhs_index).to());
    intersection->Add(CharacterRange::Range(from, to), zone);
    if (to == lhs->at(lhs_index).to()) {
      lhs_index++;
    } else {
      rhs_index++;
    }
  }

  DCHECK(IsCanonical(intersection));
}

namespace {

// Advance |index| and set |from| and |to| to the new range, if not out of
// bounds of |range|, otherwise |from| is set to a code point beyond the legal
// unicode character range.
void SafeAdvanceRange(const ZoneList<CharacterRange>* range, int* index,
                      base::uc32* from, base::uc32* to) {
  ++(*index);
  if (*index < range->length()) {
    *from = range->at(*index).from();
    *to = range->at(*index).to();
  } else {
    *from = kMaxCodePoint + 1;
  }
}

}  // namespace

// static
void CharacterRange::Subtract(const ZoneList<CharacterRange>* src,
                              const ZoneList<CharacterRange>* to_remove,
                              ZoneList<CharacterRange>* result, Zone* zone) {
  DCHECK(CharacterRange::IsCanonical(src));
  DCHECK(CharacterRange::IsCanonical(to_remove));
  DCHECK_EQ(0, result->length());

  if (src->is_empty()) return;

  int src_index = 0;
  int to_remove_index = 0;
  base::uc32 from = src->at(src_index).from();
  base::uc32 to = src->at(src_index).to();
  while (src_index < src->length() && to_remove_index < to_remove->length()) {
    CharacterRange remove_range = to_remove->at(to_remove_index);
    if (remove_range.to() < from) {
      // (a) Non-overlapping case, ignore current to_remove range.
      //            |-------|
      // |-------|
      to_remove_index++;
    } else if (to < remove_range.from()) {
      // (b) Non-overlapping case, add full current range to result.
      // |-------|
      //            |-------|
      result->Add(CharacterRange::Range(from, to), zone);
      SafeAdvanceRange(src, &src_index, &from, &to);
    } else if (from >= remove_range.from() && to <= remove_range.to()) {
      // (c) Current to_remove range fully covers current range.
      //   |---|
      // |-------|
      SafeAdvanceRange(src, &src_index, &from, &to);
    } else if (from < remove_range.from() && to > remove_range.to()) {
      // (d) Split current range.
      // |-------|
      //   |---|
      result->Add(CharacterRange::Range(from, remove_range.from() - 1), zone);
      from = remove_range.to() + 1;
      to_remove_index++;
    } else if (from < remove_range.from()) {
      // (e) End current range.
      // |-------|
      //    |-------|
      to = remove_range.from() - 1;
      result->Add(CharacterRange::Range(from, to), zone);
      SafeAdvanceRange(src, &src_index, &from, &to);
    } else if (to > remove_range.to()) {
      // (f) Modify start of current range.
      //    |-------|
      // |-------|
      from = remove_range.to() + 1;
      to_remove_index++;
    } else {
      UNREACHABLE();
    }
  }
  // The last range needs special treatment after |to_remove| is exhausted, as
  // |from| might have been modified by the last |to_remove| range and |to| was
  // not yet known (i.e. cases d and f).
  if (from <= to) {
    result->Add(CharacterRange::Range(from, to), zone);
  }
  src_index++;

  // Add remaining ranges after |to_remove| is exhausted.
  for (; src_index < src->length(); src_index++) {
    result->Add(src->at(src_index), zone);
  }

  DCHECK(IsCanonical(result));
}

// static
void CharacterRange::ClampToOneByte(ZoneList<CharacterRange>* ranges) {
  DCHECK(IsCanonical(ranges));

  // Drop all ranges that don't contain one-byte code units, and clamp the last
  // range s.t. it likewise only contains one-byte code units. Note this relies
  // on `ranges` being canonicalized, i.e. sorted and non-overlapping.

  static constexpr base::uc32 max_char = String::kMaxOneByteCharCodeU;
  int n = ranges->length();
  for (; n > 0; n--) {
    CharacterRange& r = ranges->at(n - 1);
    if (r.from() <= max_char) {
      r.to_ = std::min(r.to_, max_char);
      break;
    }
  }

  ranges->Rewind(n);
}

// static
bool CharacterRange::Equals(const ZoneList<CharacterRange>* lhs,
                            const ZoneList<CharacterRange>* rhs) {
  DCHECK(IsCanonical(lhs));
  DCHECK(IsCanonical(rhs));
  if (lhs->length() != rhs->length()) return false;

  for (int i = 0; i < lhs->length(); i++) {
    if (lhs->at(i) != rhs->at(i)) return false;
  }

  return true;
}

namespace {

// Scoped object to keep track of how much we unroll quantifier loops in the
// regexp graph generator.
class RegExpExpansionLimiter {
 public:
  static const int kMaxExpansionFactor = 6;
  RegExpExpansionLimiter(RegExpCompiler* compiler, int factor)
      : compiler_(compiler),
        saved_expansion_factor_(compiler->current_expansion_factor()),
        ok_to_expand_(saved_expansion_factor_ <= kMaxExpansionFactor) {
    DCHECK_LT(0, factor);
    if (ok_to_expand_) {
      if (factor > kMaxExpansionFactor) {
        // Avoid integer overflow of the current expansion factor.
        ok_to_expand_ = false;
        compiler->set_current_expansion_factor(kMaxExpansionFactor + 1);
      } else {
        int new_factor = saved_expansion_factor_ * factor;
        ok_to_expand_ = (new_factor <= kMaxExpansionFactor);
        compiler->set_current_expansion_factor(new_factor);
      }
    }
  }

  ~RegExpExpansionLimiter() {
    compiler_->set_current_expansion_factor(saved_expansion_factor_);
  }

  bool ok_to_expand() { return ok_to_expand_; }

 private:
  RegExpCompiler* compiler_;
  int saved_expansion_factor_;
  bool ok_to_expand_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(RegExpExpansionLimiter);
};

}  // namespace

RegExpNode* RegExpQuantifier::ToNode(int min, int max, bool is_greedy,
                                     RegExpTree* body, RegExpCompiler* compiler,
                                     RegExpNode* on_success,
                                     bool not_at_start) {
  // x{f, t} becomes this:
  //
  //             (r++)<-.
  //               |     `
  //               |     (x)
  //               v     ^
  //      (r=0)-->(?)---/ [if r < t]
  //               |
  //   [if r >= f] \----> ...
  //

  // 15.10.2.5 RepeatMatcher algorithm.
  // The parser has already eliminated the case where max is 0.  In the case
  // where max_match is zero the parser has removed the quantifier if min was
  // > 0 and removed the atom if min was 0.  See AddQuantifierToAtom.

  // If we know that we cannot match zero length then things are a little
  // simpler since we don't need to make the special zero length match check
  // from step 2.1.  If the min and max are small we can unroll a little in
  // this case.
  static const int kMaxUnrolledMinMatches = 3;  // Unroll (foo)+ and (foo){3,}
  static const int kMaxUnrolledMaxMatches = 3;  // Unroll (foo)? and (foo){x,3}
  if (max == 0) return on_success;  // This can happen due to recursion.
  bool body_can_be_empty = (body->min_match() == 0);
  int body_start_reg = RegExpCompiler::kNoRegister;
  Interval capture_registers = body->CaptureRegisters();
  bool needs_capture_clearing = !capture_registers.is_empty();
  Zone* zone = compiler->zone();

  if (body_can_be_empty) {
    body_start_reg = compiler->AllocateRegister();
  } else if (compiler->optimize() && !needs_capture_clearing) {
    // Only unroll if there are no captures and the body can't be
    // empty.
    {
      RegExpExpansionLimiter limiter(compiler, min + ((max != min) ? 1 : 0));
      if (min > 0 && min <= kMaxUnrolledMinMatches && limiter.ok_to_expand()) {
        int new_max = (max == kInfinity) ? max : max - min;
        // Recurse once to get the loop or optional matches after the fixed
        // ones.
        RegExpNode* answer =
            ToNode(0, new_max, is_greedy, body, compiler, on_success, true);
        // Unroll the forced matches from 0 to min.  This can cause chains of
        // TextNodes (which the parser does not generate).  These should be
        // combined if it turns out they hinder good code generation.
        for (int i = 0; i < min; i++) {
          answer = body->ToNode(compiler, answer);
        }
        return answer;
      }
    }
    if (max <= kMaxUnrolledMaxMatches && min == 0) {
      DCHECK_LT(0, max);  // Due to the 'if' above.
      RegExpExpansionLimiter limiter(compiler, max);
      if (limiter.ok_to_expand()) {
        // Unroll the optional matches up to max.
        RegExpNode* answer = on_success;
        for (int i = 0; i < max; i++) {
          ChoiceNode* alternation = zone->New<ChoiceNode>(2, zone);
          if (is_greedy) {
            alternation->AddAlternative(
                GuardedAlternative(body->ToNode(compiler, answer)));
            alternation->AddAlternative(GuardedAlternative(on_success));
          } else {
            alternation->AddAlternative(GuardedAlternative(on_success));
            alternation->AddAlternative(
                GuardedAlternative(body->ToNode(compiler, answer)));
          }
          answer = alternation;
          if (not_at_start && !compiler->read_backward()) {
            alternation->set_not_at_start();
          }
        }
        return answer;
      }
    }
  }
  bool has_min = min > 0;
  bool has_max = max < RegExpTree::kInfinity;
  bool needs_counter = has_min || has_max;
  int reg_ctr = needs_counter ? compiler->AllocateRegister()
                              : RegExpCompiler::kNoRegister;
  LoopChoiceNode* center = zone->New<LoopChoiceNode>(
      body->min_match() == 0, compiler->read_backward(), min, zone);
  if (not_at_start && !compiler->read_backward()) center->set_not_at_start();
  RegExpNode* loop_return =
      needs_counter ? static_cast<RegExpNode*>(
                          ActionNode::IncrementRegister(reg_ctr, center))
                    : static_cast<RegExpNode*>(center);
  if (body_can_be_empty) {
    // If the body can be empty we need to check if it was and then
    // backtrack.
    loop_return =
        ActionNode::EmptyMatchCheck(body_start_reg, reg_ctr, min, loop_return);
  }
  RegExpNode* body_node = body->ToNode(compiler, loop_return);
  if (body_can_be_empty) {
    // If the body can be empty we need to store the start position
    // so we can bail out if it was empty.
    body_node = ActionNode::StorePosition(body_start_reg, false, body_node);
  }
  if (needs_capture_clearing) {
    // Before entering the body of this loop we need to clear captures.
    body_node = ActionNode::ClearCaptures(capture_registers, body_node);
  }
  GuardedAlternative body_alt(body_node);
  if (has_max) {
    Guard* body_guard = zone->New<Guard>(reg_ctr, Guard::LT, max);
    body_alt.AddGuard(body_guard, zone);
  }
  GuardedAlternative rest_alt(on_success);
  if (has_min) {
    Guard* rest_guard = compiler->zone()->New<Guard>(reg_ctr, Guard::GEQ, min);
    rest_alt.AddGuard(rest_guard, zone);
  }
  if (is_greedy) {
    center->AddLoopAlternative(body_alt);
    center->AddContinueAlternative(rest_alt);
  } else {
    center->AddContinueAlternative(rest_alt);
    center->AddLoopAlternative(body_alt);
  }
  if (needs_counter) {
    return ActionNode::SetRegisterForLoop(reg_ctr, 0, center);
  } else {
    return center;
  }
}

}  // namespace internal
}  // namespace v8

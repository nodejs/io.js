// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include "src/base/small-vector.h"
#include "src/base/strings.h"
#include "src/common/message-template.h"
#include "src/execution/arguments-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/heap/heap-inl.h"  // For ToBoolean. TODO(jkummerow): Drop.
#include "src/logging/counters.h"
#include "src/numbers/conversions-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-regexp-inl.h"
#include "src/regexp/regexp-utils.h"
#include "src/regexp/regexp.h"
#include "src/strings/string-builder-inl.h"
#include "src/strings/string-search.h"

namespace v8 {
namespace internal {

namespace {

// Fairly arbitrary, but intended to fit:
//
// - captures
// - results
// - parsed replacement pattern parts
//
// for small, common cases.
constexpr int kStaticVectorSlots = 8;

// Returns -1 for failure.
uint32_t GetArgcForReplaceCallable(uint32_t num_captures,
                                   bool has_named_captures) {
  const uint32_t kAdditionalArgsWithoutNamedCaptures = 2;
  const uint32_t kAdditionalArgsWithNamedCaptures = 3;
  if (num_captures > Code::kMaxArguments) return -1;
  uint32_t argc = has_named_captures
                      ? num_captures + kAdditionalArgsWithNamedCaptures
                      : num_captures + kAdditionalArgsWithoutNamedCaptures;
  static_assert(Code::kMaxArguments < std::numeric_limits<uint32_t>::max() -
                                          kAdditionalArgsWithNamedCaptures);
  return (argc > Code::kMaxArguments) ? -1 : argc;
}

// Looks up the capture of the given name. Returns the (1-based) numbered
// capture index or -1 on failure.
// The lookup starts at index |index_in_out|. On success |index_in_out| is set
// to the index after the entry was found (i.e. the start index to continue the
// search in the presence of duplicate group names).
int LookupNamedCapture(const std::function<bool(Tagged<String>)>& name_matches,
                       Tagged<FixedArray> capture_name_map, int* index_in_out) {
  DCHECK_GE(*index_in_out, 0);
  // TODO(jgruber): Sort capture_name_map and do binary search via
  // internalized strings.

  int maybe_capture_index = -1;
  const int named_capture_count = capture_name_map->length() >> 1;
  DCHECK_LE(*index_in_out, named_capture_count);
  for (int j = *index_in_out; j < named_capture_count; j++) {
    // The format of {capture_name_map} is documented at
    // JSRegExp::kIrregexpCaptureNameMapIndex.
    const int name_ix = j * 2;
    const int index_ix = j * 2 + 1;

    Tagged<String> capture_name = Cast<String>(capture_name_map->get(name_ix));
    if (!name_matches(capture_name)) continue;

    maybe_capture_index = Smi::ToInt(capture_name_map->get(index_ix));
    *index_in_out = j + 1;
    break;
  }

  return maybe_capture_index;
}

}  // namespace

class CompiledReplacement {
 public:
  // Return whether the replacement is simple.
  bool Compile(Isolate* isolate, DirectHandle<JSRegExp> regexp,
               DirectHandle<RegExpData> regexp_data, Handle<String> replacement,
               int capture_count, int subject_length);

  // Use Apply only if Compile returned false.
  void Apply(ReplacementStringBuilder* builder, int match_from, int match_to,
             int32_t* match);

  // Number of distinct parts of the replacement pattern.
  int parts() { return static_cast<int>(parts_.size()); }

 private:
  enum PartType {
    SUBJECT_PREFIX = 1,
    SUBJECT_SUFFIX,
    SUBJECT_CAPTURE,
    REPLACEMENT_SUBSTRING,
    REPLACEMENT_STRING,
    EMPTY_REPLACEMENT,
    NUMBER_OF_PART_TYPES
  };

  struct ReplacementPart {
    static inline ReplacementPart SubjectMatch() {
      return ReplacementPart(SUBJECT_CAPTURE, 0);
    }
    static inline ReplacementPart SubjectCapture(int capture_index) {
      return ReplacementPart(SUBJECT_CAPTURE, capture_index);
    }
    static inline ReplacementPart SubjectPrefix() {
      return ReplacementPart(SUBJECT_PREFIX, 0);
    }
    static inline ReplacementPart SubjectSuffix(int subject_length) {
      return ReplacementPart(SUBJECT_SUFFIX, subject_length);
    }
    static inline ReplacementPart ReplacementString() {
      return ReplacementPart(REPLACEMENT_STRING, 0);
    }
    static inline ReplacementPart EmptyReplacement() {
      return ReplacementPart(EMPTY_REPLACEMENT, 0);
    }
    static inline ReplacementPart ReplacementSubString(int from, int to) {
      DCHECK_LE(0, from);
      DCHECK_GT(to, from);
      return ReplacementPart(-from, to);
    }

    // If tag <= 0 then it is the negation of a start index of a substring of
    // the replacement pattern, otherwise it's a value from PartType.
    ReplacementPart(int tag, int data) : tag(tag), data(data) {
      // Must be non-positive or a PartType value.
      DCHECK(tag < NUMBER_OF_PART_TYPES);
    }
    // Either a value of PartType or a non-positive number that is
    // the negation of an index into the replacement string.
    int tag;
    // The data value's interpretation depends on the value of tag:
    // tag == SUBJECT_PREFIX ||
    // tag == SUBJECT_SUFFIX:  data is unused.
    // tag == SUBJECT_CAPTURE: data is the number of the capture.
    // tag == REPLACEMENT_SUBSTRING ||
    // tag == REPLACEMENT_STRING:    data is index into array of substrings
    //                               of the replacement string.
    // tag == EMPTY_REPLACEMENT: data is unused.
    // tag <= 0: Temporary representation of the substring of the replacement
    //           string ranging over -tag .. data.
    //           Is replaced by REPLACEMENT_{SUB,}STRING when we create the
    //           substring objects.
    int data;
  };

  template <typename Char>
  bool ParseReplacementPattern(base::Vector<Char> characters,
                               Tagged<FixedArray> capture_name_map,
                               int capture_count, int subject_length) {
    // Equivalent to String::GetSubstitution, except that this method converts
    // the replacement string into an internal representation that avoids
    // repeated parsing when used repeatedly.
    int length = characters.length();
    int last = 0;
    for (int i = 0; i < length; i++) {
      Char c = characters[i];
      if (c == '$') {
        int next_index = i + 1;
        if (next_index == length) {  // No next character!
          break;
        }
        Char c2 = characters[next_index];
        switch (c2) {
          case '$':
            if (i > last) {
              // There is a substring before. Include the first "$".
              parts_.emplace_back(
                  ReplacementPart::ReplacementSubString(last, next_index));
              last = next_index + 1;  // Continue after the second "$".
            } else {
              // Let the next substring start with the second "$".
              last = next_index;
            }
            i = next_index;
            break;
          case '`':
            if (i > last) {
              parts_.emplace_back(
                  ReplacementPart::ReplacementSubString(last, i));
            }
            parts_.emplace_back(ReplacementPart::SubjectPrefix());
            i = next_index;
            last = i + 1;
            break;
          case '\'':
            if (i > last) {
              parts_.emplace_back(
                  ReplacementPart::ReplacementSubString(last, i));
            }
            parts_.emplace_back(ReplacementPart::SubjectSuffix(subject_length));
            i = next_index;
            last = i + 1;
            break;
          case '&':
            if (i > last) {
              parts_.emplace_back(
                  ReplacementPart::ReplacementSubString(last, i));
            }
            parts_.emplace_back(ReplacementPart::SubjectMatch());
            i = next_index;
            last = i + 1;
            break;
          case '0':
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9': {
            int capture_ref = c2 - '0';
            if (capture_ref > capture_count) {
              i = next_index;
              continue;
            }
            int second_digit_index = next_index + 1;
            if (second_digit_index < length) {
              // Peek ahead to see if we have two digits.
              Char c3 = characters[second_digit_index];
              if ('0' <= c3 && c3 <= '9') {  // Double digits.
                int double_digit_ref = capture_ref * 10 + c3 - '0';
                if (double_digit_ref <= capture_count) {
                  next_index = second_digit_index;
                  capture_ref = double_digit_ref;
                }
              }
            }
            if (capture_ref > 0) {
              if (i > last) {
                parts_.emplace_back(
                    ReplacementPart::ReplacementSubString(last, i));
              }
              DCHECK(capture_ref <= capture_count);
              parts_.emplace_back(ReplacementPart::SubjectCapture(capture_ref));
              last = next_index + 1;
            }
            i = next_index;
            break;
          }
          case '<': {
            if (capture_name_map.is_null()) {
              i = next_index;
              break;
            }

            // Scan until the next '>', and let the enclosed substring be the
            // groupName.

            const int name_start_index = next_index + 1;
            int closing_bracket_index = -1;
            for (int j = name_start_index; j < length; j++) {
              if (characters[j] == '>') {
                closing_bracket_index = j;
                break;
              }
            }

            // If no closing bracket is found, '$<' is treated as a string
            // literal.
            if (closing_bracket_index == -1) {
              i = next_index;
              break;
            }

            if (i > last) {
              parts_.emplace_back(
                  ReplacementPart::ReplacementSubString(last, i));
            }

            base::Vector<Char> requested_name =
                characters.SubVector(name_start_index, closing_bracket_index);

            // If capture is undefined or does not exist, replace the text
            // through the following '>' with the empty string.
            // Otherwise, replace the text through the following '>' with
            // ? ToString(capture).
            // For duplicated capture group names we don't know which of them
            // matches at this point in time, so we create a seperate
            // replacement for each possible match. When applying the
            // replacement unmatched groups will be skipped.

            int capture_index = 0;
            int capture_name_map_index = 0;
            while (capture_index != -1) {
              capture_index = LookupNamedCapture(
                  [=](Tagged<String> capture_name) {
                    return capture_name->IsEqualTo(requested_name);
                  },
                  capture_name_map, &capture_name_map_index);
              DCHECK(capture_index == -1 ||
                     (1 <= capture_index && capture_index <= capture_count));

              parts_.emplace_back(
                  capture_index == -1
                      ? ReplacementPart::EmptyReplacement()
                      : ReplacementPart::SubjectCapture(capture_index));
            }

            last = closing_bracket_index + 1;
            i = closing_bracket_index;
            break;
          }
          default:
            i = next_index;
            break;
        }
      }
    }
    if (length > last) {
      if (last == 0) {
        // Replacement is simple.  Do not use Apply to do the replacement.
        return true;
      } else {
        parts_.emplace_back(
            ReplacementPart::ReplacementSubString(last, length));
      }
    }
    return false;
  }

  base::SmallVector<ReplacementPart, kStaticVectorSlots> parts_;
  base::SmallVector<Handle<String>, kStaticVectorSlots> replacement_substrings_;
};

bool CompiledReplacement::Compile(Isolate* isolate,
                                  DirectHandle<JSRegExp> regexp,
                                  DirectHandle<RegExpData> regexp_data,
                                  Handle<String> replacement, int capture_count,
                                  int subject_length) {
  {
    DisallowGarbageCollection no_gc;
    String::FlatContent content = replacement->GetFlatContent(no_gc);
    DCHECK(content.IsFlat());

    Tagged<FixedArray> capture_name_map;
    if (capture_count > 0) {
      // capture_count > 0 implies IrRegExpData. Since capture_count is in
      // trusted space, this is not a SBXCHECK.
      DCHECK(Is<IrRegExpData>(*regexp_data));
      Tagged<IrRegExpData> re_data = Cast<IrRegExpData>(*regexp_data);

      Tagged<Object> maybe_capture_name_map = re_data->capture_name_map();
      if (IsFixedArray(maybe_capture_name_map)) {
        capture_name_map = Cast<FixedArray>(maybe_capture_name_map);
      }
    }

    bool simple;
    if (content.IsOneByte()) {
      simple =
          ParseReplacementPattern(content.ToOneByteVector(), capture_name_map,
                                  capture_count, subject_length);
    } else {
      DCHECK(content.IsTwoByte());
      simple = ParseReplacementPattern(content.ToUC16Vector(), capture_name_map,
                                       capture_count, subject_length);
    }
    if (simple) return true;
  }

  // Find substrings of replacement string and create them as String objects.
  int substring_index = 0;
  for (ReplacementPart& part : parts_) {
    int tag = part.tag;
    if (tag <= 0) {  // A replacement string slice.
      int from = -tag;
      int to = part.data;
      replacement_substrings_.emplace_back(
          isolate->factory()->NewSubString(replacement, from, to));
      part.tag = REPLACEMENT_SUBSTRING;
      part.data = substring_index;
      substring_index++;
    } else if (tag == REPLACEMENT_STRING) {
      replacement_substrings_.emplace_back(replacement);
      part.data = substring_index;
      substring_index++;
    }
  }
  return false;
}

void CompiledReplacement::Apply(ReplacementStringBuilder* builder,
                                int match_from, int match_to, int32_t* match) {
  DCHECK_LT(0, parts_.size());
  for (ReplacementPart& part : parts_) {
    switch (part.tag) {
      case SUBJECT_PREFIX:
        if (match_from > 0) builder->AddSubjectSlice(0, match_from);
        break;
      case SUBJECT_SUFFIX: {
        int subject_length = part.data;
        if (match_to < subject_length) {
          builder->AddSubjectSlice(match_to, subject_length);
        }
        break;
      }
      case SUBJECT_CAPTURE: {
        int capture = part.data;
        int from = match[capture * 2];
        int to = match[capture * 2 + 1];
        if (from >= 0 && to > from) {
          builder->AddSubjectSlice(from, to);
        }
        break;
      }
      case REPLACEMENT_SUBSTRING:
      case REPLACEMENT_STRING:
        builder->AddString(replacement_substrings_[part.data]);
        break;
      case EMPTY_REPLACEMENT:
        break;
      default:
        UNREACHABLE();
    }
  }
}

void FindOneByteStringIndices(base::Vector<const uint8_t> subject,
                              uint8_t pattern, std::vector<int>* indices,
                              unsigned int limit) {
  DCHECK_LT(0, limit);
  // Collect indices of pattern in subject using memchr.
  // Stop after finding at most limit values.
  const uint8_t* subject_start = subject.begin();
  const uint8_t* subject_end = subject_start + subject.length();
  const uint8_t* pos = subject_start;
  while (limit > 0) {
    pos = reinterpret_cast<const uint8_t*>(
        memchr(pos, pattern, subject_end - pos));
    if (pos == nullptr) return;
    indices->push_back(static_cast<int>(pos - subject_start));
    pos++;
    limit--;
  }
}

void FindTwoByteStringIndices(const base::Vector<const base::uc16> subject,
                              base::uc16 pattern, std::vector<int>* indices,
                              unsigned int limit) {
  DCHECK_LT(0, limit);
  const base::uc16* subject_start = subject.begin();
  const base::uc16* subject_end = subject_start + subject.length();
  for (const base::uc16* pos = subject_start; pos < subject_end && limit > 0;
       pos++) {
    if (*pos == pattern) {
      indices->push_back(static_cast<int>(pos - subject_start));
      limit--;
    }
  }
}

template <typename SubjectChar, typename PatternChar>
void FindStringIndices(Isolate* isolate,
                       base::Vector<const SubjectChar> subject,
                       base::Vector<const PatternChar> pattern,
                       std::vector<int>* indices, unsigned int limit) {
  DCHECK_LT(0, limit);
  // Collect indices of pattern in subject.
  // Stop after finding at most limit values.
  int pattern_length = pattern.length();
  int index = 0;
  StringSearch<PatternChar, SubjectChar> search(isolate, pattern);
  while (limit > 0) {
    index = search.Search(subject, index);
    if (index < 0) return;
    indices->push_back(index);
    index += pattern_length;
    limit--;
  }
}

void FindStringIndicesDispatch(Isolate* isolate, Tagged<String> subject,
                               Tagged<String> pattern,
                               std::vector<int>* indices, unsigned int limit) {
  {
    DisallowGarbageCollection no_gc;
    String::FlatContent subject_content = subject->GetFlatContent(no_gc);
    String::FlatContent pattern_content = pattern->GetFlatContent(no_gc);
    DCHECK(subject_content.IsFlat());
    DCHECK(pattern_content.IsFlat());
    if (subject_content.IsOneByte()) {
      base::Vector<const uint8_t> subject_vector =
          subject_content.ToOneByteVector();
      if (pattern_content.IsOneByte()) {
        base::Vector<const uint8_t> pattern_vector =
            pattern_content.ToOneByteVector();
        if (pattern_vector.length() == 1) {
          FindOneByteStringIndices(subject_vector, pattern_vector[0], indices,
                                   limit);
        } else {
          FindStringIndices(isolate, subject_vector, pattern_vector, indices,
                            limit);
        }
      } else {
        FindStringIndices(isolate, subject_vector,
                          pattern_content.ToUC16Vector(), indices, limit);
      }
    } else {
      base::Vector<const base::uc16> subject_vector =
          subject_content.ToUC16Vector();
      if (pattern_content.IsOneByte()) {
        base::Vector<const uint8_t> pattern_vector =
            pattern_content.ToOneByteVector();
        if (pattern_vector.length() == 1) {
          FindTwoByteStringIndices(subject_vector, pattern_vector[0], indices,
                                   limit);
        } else {
          FindStringIndices(isolate, subject_vector, pattern_vector, indices,
                            limit);
        }
      } else {
        base::Vector<const base::uc16> pattern_vector =
            pattern_content.ToUC16Vector();
        if (pattern_vector.length() == 1) {
          FindTwoByteStringIndices(subject_vector, pattern_vector[0], indices,
                                   limit);
        } else {
          FindStringIndices(isolate, subject_vector, pattern_vector, indices,
                            limit);
        }
      }
    }
  }
}

namespace {
std::vector<int>* GetRewoundRegexpIndicesList(Isolate* isolate) {
  std::vector<int>* list = isolate->regexp_indices();
  list->clear();
  return list;
}

void TruncateRegexpIndicesList(Isolate* isolate) {
  // Same size as smallest zone segment, preserving behavior from the
  // runtime zone.
  // TODO(jgruber): Consider removing the reusable regexp_indices list and
  // simply allocating a new list each time. It feels like we're needlessly
  // optimizing an edge case.
  static const int kMaxRegexpIndicesListCapacity = 8 * KB / kIntSize;
  std::vector<int>* indices = isolate->regexp_indices();
  if (indices->capacity() > kMaxRegexpIndicesListCapacity) {
    // Throw away backing storage.
    indices->clear();
    indices->shrink_to_fit();
  }
}
}  // namespace

template <typename ResultSeqString>
V8_WARN_UNUSED_RESULT static Tagged<Object>
StringReplaceGlobalAtomRegExpWithString(
    Isolate* isolate, DirectHandle<String> subject,
    DirectHandle<JSRegExp> pattern_regexp, DirectHandle<String> replacement,
    Handle<RegExpMatchInfo> last_match_info,
    DirectHandle<AtomRegExpData> regexp_data) {
  DCHECK(subject->IsFlat());
  DCHECK(replacement->IsFlat());

  std::vector<int>* indices = GetRewoundRegexpIndicesList(isolate);

  Tagged<String> pattern = regexp_data->pattern();
  int subject_len = subject->length();
  int pattern_len = pattern->length();
  int replacement_len = replacement->length();

  FindStringIndicesDispatch(isolate, *subject, pattern, indices, 0xFFFFFFFF);

  if (indices->empty()) return *subject;

  // Detect integer overflow.
  int64_t result_len_64 = (static_cast<int64_t>(replacement_len) -
                           static_cast<int64_t>(pattern_len)) *
                              static_cast<int64_t>(indices->size()) +
                          static_cast<int64_t>(subject_len);
  int result_len;
  if (result_len_64 > static_cast<int64_t>(String::kMaxLength)) {
    static_assert(String::kMaxLength < kMaxInt);
    result_len = kMaxInt;  // Provoke exception.
  } else {
    result_len = static_cast<int>(result_len_64);
  }
  if (result_len == 0) {
    return ReadOnlyRoots(isolate).empty_string();
  }

  int subject_pos = 0;
  int result_pos = 0;

  MaybeHandle<SeqString> maybe_res;
  if (ResultSeqString::kHasOneByteEncoding) {
    maybe_res = isolate->factory()->NewRawOneByteString(result_len);
  } else {
    maybe_res = isolate->factory()->NewRawTwoByteString(result_len);
  }
  Handle<SeqString> untyped_res;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, untyped_res, maybe_res);
  Handle<ResultSeqString> result = Cast<ResultSeqString>(untyped_res);

  DisallowGarbageCollection no_gc;
  for (int index : *indices) {
    // Copy non-matched subject content.
    if (subject_pos < index) {
      String::WriteToFlat(*subject, result->GetChars(no_gc) + result_pos,
                          subject_pos, index - subject_pos);
      result_pos += index - subject_pos;
    }

    // Replace match.
    if (replacement_len > 0) {
      String::WriteToFlat(*replacement, result->GetChars(no_gc) + result_pos, 0,
                          replacement_len);
      result_pos += replacement_len;
    }

    subject_pos = index + pattern_len;
  }
  // Add remaining subject content at the end.
  if (subject_pos < subject_len) {
    String::WriteToFlat(*subject, result->GetChars(no_gc) + result_pos,
                        subject_pos, subject_len - subject_pos);
  }

  int32_t match_indices[] = {indices->back(), indices->back() + pattern_len};
  RegExp::SetLastMatchInfo(isolate, last_match_info, subject, 0, match_indices);

  TruncateRegexpIndicesList(isolate);

  return *result;
}

V8_WARN_UNUSED_RESULT static Tagged<Object> StringReplaceGlobalRegExpWithString(
    Isolate* isolate, Handle<String> subject, DirectHandle<JSRegExp> regexp,
    DirectHandle<RegExpData> regexp_data, Handle<String> replacement,
    Handle<RegExpMatchInfo> last_match_info) {
  DCHECK(subject->IsFlat());
  DCHECK(replacement->IsFlat());

  int capture_count = regexp_data->capture_count();
  int subject_length = subject->length();

  // Ensure the RegExp is compiled so we can access the capture-name map.
  if (!RegExp::EnsureFullyCompiled(isolate, regexp_data, subject)) {
    return ReadOnlyRoots(isolate).exception();
  }

  CompiledReplacement compiled_replacement;
  const bool simple_replace = compiled_replacement.Compile(
      isolate, regexp, regexp_data, replacement, capture_count, subject_length);

  // Shortcut for simple non-regexp global replacements.
  if (regexp_data->type_tag() == RegExpData::Type::ATOM && simple_replace) {
    if (subject->IsOneByteRepresentation() &&
        replacement->IsOneByteRepresentation()) {
      return StringReplaceGlobalAtomRegExpWithString<SeqOneByteString>(
          isolate, subject, regexp, replacement, last_match_info,
          Cast<AtomRegExpData>(regexp_data));
    } else {
      return StringReplaceGlobalAtomRegExpWithString<SeqTwoByteString>(
          isolate, subject, regexp, replacement, last_match_info,
          Cast<AtomRegExpData>(regexp_data));
    }
  }

  RegExpGlobalCache global_cache(handle(*regexp_data, isolate), subject,
                                 isolate);
  if (global_cache.HasException()) return ReadOnlyRoots(isolate).exception();

  int32_t* current_match = global_cache.FetchNext();
  if (current_match == nullptr) {
    if (global_cache.HasException()) return ReadOnlyRoots(isolate).exception();
    return *subject;
  }

  // Guessing the number of parts that the final result string is built
  // from. Global regexps can match any number of times, so we guess
  // conservatively.
  int expected_parts = (compiled_replacement.parts() + 1) * 4 + 1;
  // TODO(v8:12843): improve the situation where the expected_parts exceeds
  // the maximum size of the backing store.
  ReplacementStringBuilder builder(isolate->heap(), subject, expected_parts);

  int prev = 0;

  do {
    int start = current_match[0];
    int end = current_match[1];

    if (prev < start) {
      builder.AddSubjectSlice(prev, start);
    }

    if (simple_replace) {
      builder.AddString(replacement);
    } else {
      compiled_replacement.Apply(&builder, start, end, current_match);
    }
    prev = end;

    current_match = global_cache.FetchNext();
  } while (current_match != nullptr);

  if (global_cache.HasException()) return ReadOnlyRoots(isolate).exception();

  if (prev < subject_length) {
    builder.AddSubjectSlice(prev, subject_length);
  }

  RegExp::SetLastMatchInfo(isolate, last_match_info, subject, capture_count,
                           global_cache.LastSuccessfulMatch());

  RETURN_RESULT_OR_FAILURE(isolate, builder.ToString());
}

template <typename ResultSeqString>
V8_WARN_UNUSED_RESULT static Tagged<Object>
StringReplaceGlobalRegExpWithEmptyString(
    Isolate* isolate, Handle<String> subject, DirectHandle<JSRegExp> regexp,
    DirectHandle<RegExpData> regexp_data,
    Handle<RegExpMatchInfo> last_match_info) {
  DCHECK(subject->IsFlat());

  // Shortcut for simple non-regexp global replacements.
  if (regexp_data->type_tag() == RegExpData::Type::ATOM) {
    DirectHandle<String> empty_string = isolate->factory()->empty_string();
    if (subject->IsOneByteRepresentation()) {
      return StringReplaceGlobalAtomRegExpWithString<SeqOneByteString>(
          isolate, subject, regexp, empty_string, last_match_info,
          Cast<AtomRegExpData>(regexp_data));
    } else {
      return StringReplaceGlobalAtomRegExpWithString<SeqTwoByteString>(
          isolate, subject, regexp, empty_string, last_match_info,
          Cast<AtomRegExpData>(regexp_data));
    }
  }

  RegExpGlobalCache global_cache(handle(*regexp_data, isolate), subject,
                                 isolate);
  if (global_cache.HasException()) return ReadOnlyRoots(isolate).exception();

  int32_t* current_match = global_cache.FetchNext();
  if (current_match == nullptr) {
    if (global_cache.HasException()) return ReadOnlyRoots(isolate).exception();
    return *subject;
  }

  int start = current_match[0];
  int end = current_match[1];
  int capture_count = regexp_data->capture_count();
  int subject_length = subject->length();

  int new_length = subject_length - (end - start);
  if (new_length == 0) return ReadOnlyRoots(isolate).empty_string();

  Handle<ResultSeqString> answer;
  if (ResultSeqString::kHasOneByteEncoding) {
    answer = Cast<ResultSeqString>(
        isolate->factory()->NewRawOneByteString(new_length).ToHandleChecked());
  } else {
    answer = Cast<ResultSeqString>(
        isolate->factory()->NewRawTwoByteString(new_length).ToHandleChecked());
  }

  int prev = 0;
  int position = 0;

  DisallowGarbageCollection no_gc;
  do {
    start = current_match[0];
    end = current_match[1];
    if (prev < start) {
      // Add substring subject[prev;start] to answer string.
      String::WriteToFlat(*subject, answer->GetChars(no_gc) + position, prev,
                          start - prev);
      position += start - prev;
    }
    prev = end;

    current_match = global_cache.FetchNext();
  } while (current_match != nullptr);

  if (global_cache.HasException()) return ReadOnlyRoots(isolate).exception();

  RegExp::SetLastMatchInfo(isolate, last_match_info, subject, capture_count,
                           global_cache.LastSuccessfulMatch());

  if (prev < subject_length) {
    // Add substring subject[prev;length] to answer string.
    String::WriteToFlat(*subject, answer->GetChars(no_gc) + position, prev,
                        subject_length - prev);
    position += subject_length - prev;
  }

  if (position == 0) return ReadOnlyRoots(isolate).empty_string();

  // Shorten string and fill
  int string_size = ResultSeqString::SizeFor(position);
  int allocated_string_size = ResultSeqString::SizeFor(new_length);
  int delta = allocated_string_size - string_size;

  answer->set_length(position);
  if (delta == 0) return *answer;

  Address end_of_string = answer->address() + string_size;
  Heap* heap = isolate->heap();

  // The trimming is performed on a newly allocated object, which is on a
  // freshly allocated page or on an already swept page. Hence, the sweeper
  // thread can not get confused with the filler creation. No synchronization
  // needed.
  // TODO(hpayer): We should shrink the large object page if the size
  // of the object changed significantly.
  if (!heap->IsLargeObject(*answer)) {
    heap->CreateFillerObjectAt(end_of_string, delta);
  }
  return *answer;
}

RUNTIME_FUNCTION(Runtime_StringSplit) {
  HandleScope handle_scope(isolate);
  DCHECK_EQ(3, args.length());
  Handle<String> subject = args.at<String>(0);
  Handle<String> pattern = args.at<String>(1);
  uint32_t limit = NumberToUint32(args[2]);
  CHECK_LT(0, limit);

  int subject_length = subject->length();
  int pattern_length = pattern->length();
  CHECK_LT(0, pattern_length);

  if (limit == 0xFFFFFFFFu) {
    Tagged<FixedArray> last_match_cache_unused;
    Handle<Object> cached_answer(
        RegExpResultsCache::Lookup(isolate->heap(), *subject, *pattern,
                                   &last_match_cache_unused,
                                   RegExpResultsCache::STRING_SPLIT_SUBSTRINGS),
        isolate);
    if (*cached_answer != Smi::zero()) {
      // The cache FixedArray is a COW-array and can therefore be reused.
      DirectHandle<JSArray> result = isolate->factory()->NewJSArrayWithElements(
          Cast<FixedArray>(cached_answer));
      return *result;
    }
  }

  // The limit can be very large (0xFFFFFFFFu), but since the pattern
  // isn't empty, we can never create more parts than ~half the length
  // of the subject.

  subject = String::Flatten(isolate, subject);
  pattern = String::Flatten(isolate, pattern);

  std::vector<int>* indices = GetRewoundRegexpIndicesList(isolate);

  FindStringIndicesDispatch(isolate, *subject, *pattern, indices, limit);

  if (static_cast<uint32_t>(indices->size()) < limit) {
    indices->push_back(subject_length);
  }

  // The list indices now contains the end of each part to create.

  // Create JSArray of substrings separated by separator.
  int part_count = static_cast<int>(indices->size());

  DirectHandle<JSArray> result = isolate->factory()->NewJSArray(
      PACKED_ELEMENTS, part_count, part_count,
      ArrayStorageAllocationMode::INITIALIZE_ARRAY_ELEMENTS_WITH_HOLE);

  DCHECK(result->HasObjectElements());

  DirectHandle<FixedArray> elements(Cast<FixedArray>(result->elements()),
                                    isolate);

  if (part_count == 1 && indices->at(0) == subject_length) {
    elements->set(0, *subject);
  } else {
    int part_start = 0;
    FOR_WITH_HANDLE_SCOPE(isolate, int, i = 0, i, i < part_count, i++, {
      int part_end = indices->at(i);
      DirectHandle<String> substring =
          isolate->factory()->NewProperSubString(subject, part_start, part_end);
      elements->set(i, *substring);
      part_start = part_end + pattern_length;
    });
  }

  if (limit == 0xFFFFFFFFu) {
    if (result->HasObjectElements()) {
      RegExpResultsCache::Enter(isolate, subject, pattern, elements,
                                isolate->factory()->empty_fixed_array(),
                                RegExpResultsCache::STRING_SPLIT_SUBSTRINGS);
    }
  }

  TruncateRegexpIndicesList(isolate);

  return *result;
}

namespace {

MaybeHandle<Object> RegExpExec(Isolate* isolate, DirectHandle<JSRegExp> regexp,
                               Handle<String> subject, int32_t index,
                               Handle<RegExpMatchInfo> last_match_info,
                               RegExp::ExecQuirks exec_quirks) {
  // Due to the way the JS calls are constructed this must be less than the
  // length of a string, i.e. it is always a Smi.  We check anyway for security.
  CHECK_LE(0, index);
  CHECK_GE(subject->length(), index);
  isolate->counters()->regexp_entry_runtime()->Increment();
  return RegExp::Exec(isolate, regexp, subject, index, last_match_info,
                      exec_quirks);
}

MaybeHandle<Object> ExperimentalOneshotExec(
    Isolate* isolate, DirectHandle<JSRegExp> regexp,
    DirectHandle<String> subject, int32_t index,
    Handle<RegExpMatchInfo> last_match_info, RegExp::ExecQuirks exec_quirks) {
  // Due to the way the JS calls are constructed this must be less than the
  // length of a string, i.e. it is always a Smi.  We check anyway for security.
  CHECK_LE(0, index);
  CHECK_GE(subject->length(), index);
  isolate->counters()->regexp_entry_runtime()->Increment();
  return RegExp::ExperimentalOneshotExec(isolate, regexp, subject, index,
                                         last_match_info, exec_quirks);
}

}  // namespace

RUNTIME_FUNCTION(Runtime_RegExpExec) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  DirectHandle<JSRegExp> regexp = args.at<JSRegExp>(0);
  Handle<String> subject = args.at<String>(1);
  int32_t index = 0;
  CHECK(Object::ToInt32(args[2], &index));
  Handle<RegExpMatchInfo> last_match_info = args.at<RegExpMatchInfo>(3);
  RETURN_RESULT_OR_FAILURE(
      isolate, RegExpExec(isolate, regexp, subject, index, last_match_info,
                          RegExp::ExecQuirks::kNone));
}

RUNTIME_FUNCTION(Runtime_RegExpExecTreatMatchAtEndAsFailure) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  DirectHandle<JSRegExp> regexp = args.at<JSRegExp>(0);
  Handle<String> subject = args.at<String>(1);
  int32_t index = 0;
  CHECK(Object::ToInt32(args[2], &index));
  Handle<RegExpMatchInfo> last_match_info = args.at<RegExpMatchInfo>(3);
  RETURN_RESULT_OR_FAILURE(
      isolate, RegExpExec(isolate, regexp, subject, index, last_match_info,
                          RegExp::ExecQuirks::kTreatMatchAtEndAsFailure));
}

RUNTIME_FUNCTION(Runtime_RegExpExperimentalOneshotExec) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  DirectHandle<JSRegExp> regexp = args.at<JSRegExp>(0);
  DirectHandle<String> subject = args.at<String>(1);
  int32_t index = 0;
  CHECK(Object::ToInt32(args[2], &index));
  Handle<RegExpMatchInfo> last_match_info = args.at<RegExpMatchInfo>(3);
  RETURN_RESULT_OR_FAILURE(
      isolate,
      ExperimentalOneshotExec(isolate, regexp, subject, index, last_match_info,
                              RegExp::ExecQuirks::kNone));
}

RUNTIME_FUNCTION(
    Runtime_RegExpExperimentalOneshotExecTreatMatchAtEndAsFailure) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  DirectHandle<JSRegExp> regexp = args.at<JSRegExp>(0);
  DirectHandle<String> subject = args.at<String>(1);
  int32_t index = 0;
  CHECK(Object::ToInt32(args[2], &index));
  Handle<RegExpMatchInfo> last_match_info = args.at<RegExpMatchInfo>(3);
  RETURN_RESULT_OR_FAILURE(
      isolate,
      ExperimentalOneshotExec(isolate, regexp, subject, index, last_match_info,
                              RegExp::ExecQuirks::kTreatMatchAtEndAsFailure));
}

RUNTIME_FUNCTION(Runtime_RegExpBuildIndices) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  DirectHandle<RegExpMatchInfo> match_info = args.at<RegExpMatchInfo>(1);
  Handle<Object> maybe_names = args.at(2);
#ifdef DEBUG
  DirectHandle<JSRegExp> regexp = args.at<JSRegExp>(0);
  DCHECK(regexp->flags() & JSRegExp::kHasIndices);
#endif

  return *JSRegExpResultIndices::BuildIndices(isolate, match_info, maybe_names);
}

namespace {

class MatchInfoBackedMatch : public String::Match {
 public:
  MatchInfoBackedMatch(Isolate* isolate, DirectHandle<JSRegExp> regexp,
                       DirectHandle<RegExpData> regexp_data,
                       Handle<String> subject,
                       Handle<RegExpMatchInfo> match_info)
      : isolate_(isolate), match_info_(match_info) {
    subject_ = String::Flatten(isolate, subject);

    if (RegExpData::TypeSupportsCaptures(regexp_data->type_tag())) {
      DCHECK(Is<IrRegExpData>(*regexp_data));
      Tagged<Object> o = Cast<IrRegExpData>(regexp_data)->capture_name_map();
      has_named_captures_ = IsFixedArray(o);
      if (has_named_captures_) {
        capture_name_map_ = handle(Cast<FixedArray>(o), isolate);
      }
    } else {
      has_named_captures_ = false;
    }
  }

  Handle<String> GetMatch() override {
    return RegExpUtils::GenericCaptureGetter(isolate_, match_info_, 0, nullptr);
  }

  Handle<String> GetPrefix() override {
    const int match_start = match_info_->capture(0);
    return isolate_->factory()->NewSubString(subject_, 0, match_start);
  }

  Handle<String> GetSuffix() override {
    const int match_end = match_info_->capture(1);
    return isolate_->factory()->NewSubString(subject_, match_end,
                                             subject_->length());
  }

  bool HasNamedCaptures() override { return has_named_captures_; }

  int CaptureCount() override {
    return match_info_->number_of_capture_registers() / 2;
  }

  MaybeHandle<String> GetCapture(int i, bool* capture_exists) override {
    Handle<Object> capture_obj = RegExpUtils::GenericCaptureGetter(
        isolate_, match_info_, i, capture_exists);
    return (*capture_exists) ? Object::ToString(isolate_, capture_obj)
                             : isolate_->factory()->empty_string();
  }

  MaybeHandle<String> GetNamedCapture(Handle<String> name,
                                      CaptureState* state) override {
    DCHECK(has_named_captures_);
    int capture_index = 0;
    int capture_name_map_index = 0;
    while (true) {
      capture_index = LookupNamedCapture(
          [=](Tagged<String> capture_name) {
            return capture_name->Equals(*name);
          },
          *capture_name_map_, &capture_name_map_index);
      if (capture_index == -1) {
        *state = UNMATCHED;
        return isolate_->factory()->empty_string();
      }
      if (RegExpUtils::IsMatchedCapture(*match_info_, capture_index)) {
        Handle<String> capture_value;
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate_, capture_value,
            Object::ToString(isolate_,
                             RegExpUtils::GenericCaptureGetter(
                                 isolate_, match_info_, capture_index)));
        *state = MATCHED;
        return capture_value;
      }
    }
  }

 private:
  Isolate* isolate_;
  Handle<String> subject_;
  Handle<RegExpMatchInfo> match_info_;

  bool has_named_captures_;
  Handle<FixedArray> capture_name_map_;
};

class VectorBackedMatch : public String::Match {
 public:
  VectorBackedMatch(Isolate* isolate, Handle<String> subject,
                    Handle<String> match, int match_position,
                    base::Vector<Handle<Object>> captures,
                    Handle<Object> groups_obj)
      : isolate_(isolate),
        match_(match),
        match_position_(match_position),
        captures_(captures) {
    subject_ = String::Flatten(isolate, subject);

    DCHECK(IsUndefined(*groups_obj, isolate) || IsJSReceiver(*groups_obj));
    has_named_captures_ = !IsUndefined(*groups_obj, isolate);
    if (has_named_captures_) groups_obj_ = Cast<JSReceiver>(groups_obj);
  }

  Handle<String> GetMatch() override { return match_; }

  Handle<String> GetPrefix() override {
    return isolate_->factory()->NewSubString(subject_, 0, match_position_);
  }

  Handle<String> GetSuffix() override {
    const int match_end_position = match_position_ + match_->length();
    return isolate_->factory()->NewSubString(subject_, match_end_position,
                                             subject_->length());
  }

  bool HasNamedCaptures() override { return has_named_captures_; }

  int CaptureCount() override { return captures_.length(); }

  MaybeHandle<String> GetCapture(int i, bool* capture_exists) override {
    Handle<Object> capture_obj = captures_[i];
    if (IsUndefined(*capture_obj, isolate_)) {
      *capture_exists = false;
      return isolate_->factory()->empty_string();
    }
    *capture_exists = true;
    return Object::ToString(isolate_, capture_obj);
  }

  MaybeHandle<String> GetNamedCapture(Handle<String> name,
                                      CaptureState* state) override {
    DCHECK(has_named_captures_);

    // Strings representing integer indices are not valid identifiers (and
    // therefore not valid capture names).
    {
      size_t unused;
      if (name->AsIntegerIndex(&unused)) {
        *state = UNMATCHED;
        return isolate_->factory()->empty_string();
      }
    }
    Handle<Object> capture_obj;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate_, capture_obj,
        Object::GetProperty(isolate_, groups_obj_, name));
    if (IsUndefined(*capture_obj, isolate_)) {
      *state = UNMATCHED;
      return isolate_->factory()->empty_string();
    } else {
      *state = MATCHED;
      return Object::ToString(isolate_, capture_obj);
    }
  }

 private:
  Isolate* isolate_;
  Handle<String> subject_;
  Handle<String> match_;
  const int match_position_;
  base::Vector<Handle<Object>> captures_;

  bool has_named_captures_;
  Handle<JSReceiver> groups_obj_;
};

// Create the groups object (see also the RegExp result creation in
// RegExpBuiltinsAssembler::ConstructNewResultFromMatchInfo).
// TODO(42203211): We cannot simply pass a std::function here, as the closure
// may contain direct handles and they cannot be stored off-stack.
template <typename FunctionType,
          typename = std::enable_if_t<std::is_function_v<Tagged<Object>(int)>>>
Handle<JSObject> ConstructNamedCaptureGroupsObject(
    Isolate* isolate, DirectHandle<FixedArray> capture_map,
    const FunctionType& f_get_capture) {
  Handle<JSObject> groups = isolate->factory()->NewJSObjectWithNullProto();

  const int named_capture_count = capture_map->length() >> 1;
  for (int i = 0; i < named_capture_count; i++) {
    const int name_ix = i * 2;
    const int index_ix = i * 2 + 1;

    Handle<String> capture_name(Cast<String>(capture_map->get(name_ix)),
                                isolate);
    const int capture_ix = Smi::ToInt(capture_map->get(index_ix));
    DCHECK_GE(capture_ix, 1);  // Explicit groups start at index 1.

    Handle<Object> capture_value(f_get_capture(capture_ix), isolate);
    DCHECK(IsUndefined(*capture_value, isolate) || IsString(*capture_value));

    LookupIterator it(isolate, groups, capture_name, groups,
                      LookupIterator::OWN_SKIP_INTERCEPTOR);
    if (it.IsFound()) {
      DCHECK(v8_flags.js_regexp_duplicate_named_groups);
      if (!IsUndefined(*capture_value, isolate)) {
        DCHECK(IsUndefined(*it.GetDataValue(), isolate));
        CHECK(Object::SetDataProperty(&it, capture_value).ToChecked());
      }
    } else {
      CHECK(Object::AddDataProperty(&it, capture_value, NONE,
                                    Just(ShouldThrow::kThrowOnError),
                                    StoreOrigin::kNamed)
                .IsJust());
    }
  }

  return groups;
}

// Only called from Runtime_RegExpExecMultiple so it doesn't need to maintain
// separate last match info.  See comment on that function.
template <bool has_capture>
static Tagged<Object> SearchRegExpMultiple(
    Isolate* isolate, Handle<String> subject, DirectHandle<JSRegExp> regexp,
    DirectHandle<RegExpData> regexp_data,
    Handle<RegExpMatchInfo> last_match_array) {
  DCHECK(RegExpUtils::IsUnmodifiedRegExp(isolate, regexp));
  DCHECK_NE(has_capture, regexp_data->capture_count() == 0);
  DCHECK_IMPLIES(has_capture, Is<IrRegExpData>(*regexp_data));
  DCHECK(subject->IsFlat());

  // Force tier up to native code for global replaces. The global replace is
  // implemented differently for native code and bytecode execution, where the
  // native code expects an array to store all the matches, and the bytecode
  // matches one at a time, so it's easier to tier-up to native code from the
  // start.
  if (v8_flags.regexp_tier_up &&
      regexp_data->type_tag() == RegExpData::Type::IRREGEXP) {
    Cast<IrRegExpData>(regexp_data)->MarkTierUpForNextExec();
    if (v8_flags.trace_regexp_tier_up) {
      PrintF("Forcing tier-up of JSRegExp object %p in SearchRegExpMultiple\n",
             reinterpret_cast<void*>(regexp->ptr()));
    }
  }

  int capture_count = regexp_data->capture_count();
  int subject_length = subject->length();

  static const int kMinLengthToCache = 0x1000;

  if (subject_length > kMinLengthToCache) {
    Tagged<FixedArray> last_match_cache;
    Tagged<Object> cached_answer = RegExpResultsCache::Lookup(
        isolate->heap(), *subject, regexp_data->wrapper(), &last_match_cache,
        RegExpResultsCache::REGEXP_MULTIPLE_INDICES);
    if (IsFixedArray(cached_answer)) {
      int capture_registers = JSRegExp::RegistersForCaptureCount(capture_count);
      std::unique_ptr<int32_t[]> last_match(new int32_t[capture_registers]);
      int32_t* raw_last_match = last_match.get();
      for (int i = 0; i < capture_registers; i++) {
        raw_last_match[i] = Smi::ToInt(last_match_cache->get(i));
      }
      DirectHandle<FixedArray> cached_fixed_array(
          Cast<FixedArray>(cached_answer), isolate);
      // The cache FixedArray is a COW-array and we need to return a copy.
      DirectHandle<FixedArray> copied_fixed_array =
          isolate->factory()->CopyFixedArrayWithMap(
              cached_fixed_array, isolate->factory()->fixed_array_map());
      RegExp::SetLastMatchInfo(isolate, last_match_array, subject,
                               capture_count, raw_last_match);
      return *copied_fixed_array;
    }
  }

  RegExpGlobalCache global_cache(handle(*regexp_data, isolate), subject,
                                 isolate);
  if (global_cache.HasException()) return ReadOnlyRoots(isolate).exception();

  FixedArrayBuilder builder = FixedArrayBuilder::Lazy(isolate);

  // Position to search from.
  int match_start = -1;
  int match_end = 0;
  bool first = true;

  // Two smis before and after the match, for very long strings.
  static const int kMaxBuilderEntriesPerRegExpMatch = 5;

  while (true) {
    int32_t* current_match = global_cache.FetchNext();
    if (current_match == nullptr) break;
    match_start = current_match[0];
    builder.EnsureCapacity(isolate, kMaxBuilderEntriesPerRegExpMatch);
    if (match_end < match_start) {
      ReplacementStringBuilder::AddSubjectSlice(&builder, match_end,
                                                match_start);
    }
    match_end = current_match[1];
    {
      // Avoid accumulating new handles inside loop.
      HandleScope temp_scope(isolate);
      DirectHandle<String> match;
      if (!first) {
        match = isolate->factory()->NewProperSubString(subject, match_start,
                                                       match_end);
      } else {
        match =
            isolate->factory()->NewSubString(subject, match_start, match_end);
        first = false;
      }

      if (has_capture) {
        // Arguments array to replace function is match, captures, index and
        // subject, i.e., 3 + capture count in total. If the RegExp contains
        // named captures, they are also passed as the last argument.

        // has_capture can only be true for IrRegExp.
        Tagged<IrRegExpData> re_data = Cast<IrRegExpData>(*regexp_data);
        Handle<Object> maybe_capture_map(re_data->capture_name_map(), isolate);
        const bool has_named_captures = IsFixedArray(*maybe_capture_map);

        const int argc =
            has_named_captures ? 4 + capture_count : 3 + capture_count;

        DirectHandle<FixedArray> elements =
            isolate->factory()->NewFixedArray(argc);
        int cursor = 0;

        elements->set(cursor++, *match);
        for (int i = 1; i <= capture_count; i++) {
          int start = current_match[i * 2];
          if (start >= 0) {
            int end = current_match[i * 2 + 1];
            DCHECK(start <= end);
            DirectHandle<String> substring =
                isolate->factory()->NewSubString(subject, start, end);
            elements->set(cursor++, *substring);
          } else {
            DCHECK_GT(0, current_match[i * 2 + 1]);
            elements->set(cursor++, ReadOnlyRoots(isolate).undefined_value());
          }
        }

        elements->set(cursor++, Smi::FromInt(match_start));
        elements->set(cursor++, *subject);

        if (has_named_captures) {
          Handle<FixedArray> capture_map = Cast<FixedArray>(maybe_capture_map);
          DirectHandle<JSObject> groups = ConstructNamedCaptureGroupsObject(
              isolate, capture_map, [=](int ix) { return elements->get(ix); });
          elements->set(cursor++, *groups);
        }

        DCHECK_EQ(cursor, argc);
        builder.Add(*isolate->factory()->NewJSArrayWithElements(elements));
      } else {
        builder.Add(*match);
      }
    }
  }

  if (global_cache.HasException()) return ReadOnlyRoots(isolate).exception();

  if (match_start >= 0) {
    // Finished matching, with at least one match.
    if (match_end < subject_length) {
      ReplacementStringBuilder::AddSubjectSlice(&builder, match_end,
                                                subject_length);
    }

    RegExp::SetLastMatchInfo(isolate, last_match_array, subject, capture_count,
                             global_cache.LastSuccessfulMatch());

    if (subject_length > kMinLengthToCache) {
      // Store the last successful match into the array for caching.
      int capture_registers = JSRegExp::RegistersForCaptureCount(capture_count);
      DirectHandle<FixedArray> last_match_cache =
          isolate->factory()->NewFixedArray(capture_registers);
      int32_t* last_match = global_cache.LastSuccessfulMatch();
      for (int i = 0; i < capture_registers; i++) {
        last_match_cache->set(i, Smi::FromInt(last_match[i]));
      }
      DirectHandle<FixedArray> result_fixed_array =
          FixedArray::RightTrimOrEmpty(
              isolate, indirect_handle(builder.array(), isolate),
              builder.length());
      // Cache the result and copy the FixedArray into a COW array.
      DirectHandle<FixedArray> copied_fixed_array =
          isolate->factory()->CopyFixedArrayWithMap(
              result_fixed_array, isolate->factory()->fixed_array_map());
      RegExpResultsCache::Enter(
          isolate, subject, handle(regexp->data(isolate)->wrapper(), isolate),
          copied_fixed_array, last_match_cache,
          RegExpResultsCache::REGEXP_MULTIPLE_INDICES);
    }
    return *builder.array();
  } else {
    return ReadOnlyRoots(isolate).null_value();  // No matches at all.
  }
}

// Legacy implementation of RegExp.prototype[Symbol.replace] which
// doesn't properly call the underlying exec method.
V8_WARN_UNUSED_RESULT MaybeHandle<String> RegExpReplace(
    Isolate* isolate, Handle<JSRegExp> regexp, Handle<String> string,
    Handle<String> replace) {
  // Functional fast-paths are dispatched directly by replace builtin.
  DCHECK(RegExpUtils::IsUnmodifiedRegExp(isolate, regexp));

  Factory* factory = isolate->factory();

  const int flags = regexp->flags();
  const bool global = (flags & JSRegExp::kGlobal) != 0;
  const bool sticky = (flags & JSRegExp::kSticky) != 0;

  replace = String::Flatten(isolate, replace);

  Handle<RegExpMatchInfo> last_match_info = isolate->regexp_last_match_info();
  DirectHandle<RegExpData> data = direct_handle(regexp->data(isolate), isolate);

  if (!global) {
    // Non-global regexp search, string replace.

    uint32_t last_index = 0;
    if (sticky) {
      Handle<Object> last_index_obj(regexp->last_index(), isolate);
      ASSIGN_RETURN_ON_EXCEPTION(isolate, last_index_obj,
                                 Object::ToLength(isolate, last_index_obj));
      last_index = PositiveNumberToUint32(*last_index_obj);
    }

    Handle<Object> match_indices_obj(ReadOnlyRoots(isolate).null_value(),
                                     isolate);

    // A lastIndex exceeding the string length always returns null (signalling
    // failure) in RegExpBuiltinExec, thus we can skip the call.
    if (last_index <= static_cast<uint32_t>(string->length())) {
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, match_indices_obj,
          RegExp::Exec(isolate, regexp, string, last_index, last_match_info));
    }

    if (IsNull(*match_indices_obj, isolate)) {
      if (sticky) regexp->set_last_index(Smi::zero(), SKIP_WRITE_BARRIER);
      return string;
    }

    auto match_indices = Cast<RegExpMatchInfo>(match_indices_obj);

    const int start_index = match_indices->capture(0);
    const int end_index = match_indices->capture(1);

    if (sticky) {
      regexp->set_last_index(Smi::FromInt(end_index), SKIP_WRITE_BARRIER);
    }

    IncrementalStringBuilder builder(isolate);
    builder.AppendString(factory->NewSubString(string, 0, start_index));

    if (replace->length() > 0) {
      MatchInfoBackedMatch m(isolate, regexp, data, string, match_indices);
      Handle<String> replacement;
      ASSIGN_RETURN_ON_EXCEPTION(isolate, replacement,
                                 String::GetSubstitution(isolate, &m, replace));
      builder.AppendString(replacement);
    }

    builder.AppendString(
        factory->NewSubString(string, end_index, string->length()));
    return indirect_handle(builder.Finish(), isolate);
  } else {
    // Global regexp search, string replace.
    DCHECK(global);
    RETURN_ON_EXCEPTION(isolate, RegExpUtils::SetLastIndex(isolate, regexp, 0));

    // Force tier up to native code for global replaces. The global replace is
    // implemented differently for native code and bytecode execution, where the
    // native code expects an array to store all the matches, and the bytecode
    // matches one at a time, so it's easier to tier-up to native code from the
    // start.
    if (v8_flags.regexp_tier_up &&
        data->type_tag() == RegExpData::Type::IRREGEXP) {
      Cast<IrRegExpData>(data)->MarkTierUpForNextExec();
      if (v8_flags.trace_regexp_tier_up) {
        PrintF("Forcing tier-up of JSRegExp object %p in RegExpReplace\n",
               reinterpret_cast<void*>(regexp->ptr()));
      }
    }

    if (replace->length() == 0) {
      if (string->IsOneByteRepresentation()) {
        Tagged<Object> result =
            StringReplaceGlobalRegExpWithEmptyString<SeqOneByteString>(
                isolate, string, regexp, data, last_match_info);
        return handle(Cast<String>(result), isolate);
      } else {
        Tagged<Object> result =
            StringReplaceGlobalRegExpWithEmptyString<SeqTwoByteString>(
                isolate, string, regexp, data, last_match_info);
        return handle(Cast<String>(result), isolate);
      }
    }

    Tagged<Object> result = StringReplaceGlobalRegExpWithString(
        isolate, string, regexp, data, replace, last_match_info);
    if (IsString(result)) {
      return handle(Cast<String>(result), isolate);
    } else {
      return MaybeHandle<String>();
    }
  }

  UNREACHABLE();
}

}  // namespace

// This is only called for StringReplaceGlobalRegExpWithFunction.
RUNTIME_FUNCTION(Runtime_RegExpExecMultiple) {
  HandleScope handles(isolate);
  DCHECK_EQ(3, args.length());

  DirectHandle<JSRegExp> regexp = args.at<JSRegExp>(0);
  Handle<String> subject = args.at<String>(1);
  Handle<RegExpMatchInfo> last_match_info = args.at<RegExpMatchInfo>(2);

  DCHECK(RegExpUtils::IsUnmodifiedRegExp(isolate, regexp));
  DirectHandle<RegExpData> regexp_data =
      direct_handle(regexp->data(isolate), isolate);

  subject = String::Flatten(isolate, subject);
  CHECK(regexp->flags() & JSRegExp::kGlobal);

  Tagged<Object> result;
  if (regexp_data->capture_count() == 0) {
    result = SearchRegExpMultiple<false>(isolate, subject, regexp, regexp_data,
                                         last_match_info);
  } else {
    result = SearchRegExpMultiple<true>(isolate, subject, regexp, regexp_data,
                                        last_match_info);
  }
  DCHECK(RegExpUtils::IsUnmodifiedRegExp(isolate, regexp));
  return result;
}

RUNTIME_FUNCTION(Runtime_StringReplaceNonGlobalRegExpWithFunction) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Handle<String> subject = args.at<String>(0);
  DirectHandle<JSRegExp> regexp = args.at<JSRegExp>(1);
  Handle<JSReceiver> replace_obj = args.at<JSReceiver>(2);

  DCHECK(RegExpUtils::IsUnmodifiedRegExp(isolate, regexp));
  DCHECK(replace_obj->map()->is_callable());

  Factory* factory = isolate->factory();
  Handle<RegExpMatchInfo> last_match_info = isolate->regexp_last_match_info();
  DirectHandle<RegExpData> data = direct_handle(regexp->data(isolate), isolate);

  const int flags = regexp->flags();
  DCHECK_EQ(flags & JSRegExp::kGlobal, 0);

  // TODO(jgruber): This should be an easy port to CSA with massive payback.

  const bool sticky = (flags & JSRegExp::kSticky) != 0;
  uint32_t last_index = 0;
  if (sticky) {
    Handle<Object> last_index_obj(regexp->last_index(), isolate);
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, last_index_obj, Object::ToLength(isolate, last_index_obj));
    last_index = PositiveNumberToUint32(*last_index_obj);
  }

  Handle<Object> match_indices_obj(ReadOnlyRoots(isolate).null_value(),
                                   isolate);

  // A lastIndex exceeding the string length always returns null (signalling
  // failure) in RegExpBuiltinExec, thus we can skip the call.
  if (last_index <= static_cast<uint32_t>(subject->length())) {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, match_indices_obj,
        RegExp::Exec(isolate, regexp, subject, last_index, last_match_info));
  }

  if (IsNull(*match_indices_obj, isolate)) {
    if (sticky) regexp->set_last_index(Smi::zero(), SKIP_WRITE_BARRIER);
    return *subject;
  }

  auto match_indices = Cast<RegExpMatchInfo>(match_indices_obj);

  const int index = match_indices->capture(0);
  const int end_of_match = match_indices->capture(1);

  if (sticky) {
    regexp->set_last_index(Smi::FromInt(end_of_match), SKIP_WRITE_BARRIER);
  }

  IncrementalStringBuilder builder(isolate);
  builder.AppendString(factory->NewSubString(subject, 0, index));

  // Compute the parameter list consisting of the match, captures, index,
  // and subject for the replace function invocation. If the RegExp contains
  // named captures, they are also passed as the last argument.

  // The number of captures plus one for the match.
  const int m = match_indices->number_of_capture_registers() / 2;

  bool has_named_captures = false;
  DirectHandle<FixedArray> capture_map;
  if (m > 1) {
    SBXCHECK(Is<IrRegExpData>(*data));

    Tagged<Object> maybe_capture_map =
        Cast<IrRegExpData>(data)->capture_name_map();
    if (IsFixedArray(maybe_capture_map)) {
      has_named_captures = true;
      capture_map = direct_handle(Cast<FixedArray>(maybe_capture_map), isolate);
    }
  }

  const uint32_t argc = GetArgcForReplaceCallable(m, has_named_captures);
  if (argc == static_cast<uint32_t>(-1)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kTooManyArguments));
  }
  base::ScopedVector<Handle<Object>> argv(argc);

  int cursor = 0;
  for (int j = 0; j < m; j++) {
    bool ok;
    Handle<String> capture =
        RegExpUtils::GenericCaptureGetter(isolate, match_indices, j, &ok);
    if (ok) {
      argv[cursor++] = capture;
    } else {
      argv[cursor++] = factory->undefined_value();
    }
  }

  argv[cursor++] = handle(Smi::FromInt(index), isolate);
  argv[cursor++] = subject;

  if (has_named_captures) {
    argv[cursor++] = ConstructNamedCaptureGroupsObject(
        isolate, capture_map, [&argv](int ix) { return *argv[ix]; });
  }

  DCHECK_EQ(cursor, argc);

  Handle<Object> replacement_obj;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, replacement_obj,
      Execution::Call(isolate, replace_obj, factory->undefined_value(), argc,
                      argv.begin()));

  Handle<String> replacement;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, replacement, Object::ToString(isolate, replacement_obj));

  builder.AppendString(replacement);
  builder.AppendString(
      factory->NewSubString(subject, end_of_match, subject->length()));

  RETURN_RESULT_OR_FAILURE(isolate, builder.Finish());
}

namespace {

V8_WARN_UNUSED_RESULT MaybeHandle<Object> ToUint32(Isolate* isolate,
                                                   Handle<Object> object,
                                                   uint32_t* out) {
  if (IsUndefined(*object, isolate)) {
    *out = kMaxUInt32;
    return object;
  }

  Handle<Object> number;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, number,
                             Object::ToNumber(isolate, object));
  *out = NumberToUint32(*number);
  return object;
}

Handle<JSArray> NewJSArrayWithElements(Isolate* isolate,
                                       Handle<FixedArray> elems,
                                       int num_elems) {
  return isolate->factory()->NewJSArrayWithElements(
      FixedArray::RightTrimOrEmpty(isolate, elems, num_elems));
}

}  // namespace

// Slow path for:
// ES#sec-regexp.prototype-@@replace
// RegExp.prototype [ @@split ] ( string, limit )
RUNTIME_FUNCTION(Runtime_RegExpSplit) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());

  Handle<JSReceiver> recv = args.at<JSReceiver>(0);
  Handle<String> string = args.at<String>(1);
  Handle<Object> limit_obj = args.at(2);

  Factory* factory = isolate->factory();

  Handle<JSFunction> regexp_fun = isolate->regexp_function();
  Handle<Object> ctor;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, ctor, Object::SpeciesConstructor(isolate, recv, regexp_fun));

  Handle<Object> flags_obj;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, flags_obj,
      JSObject::GetProperty(isolate, recv, factory->flags_string()));

  Handle<String> flags;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, flags,
                                     Object::ToString(isolate, flags_obj));

  Handle<String> u_str = factory->LookupSingleCharacterStringFromCode('u');
  const bool unicode = (String::IndexOf(isolate, flags, u_str, 0) >= 0);

  Handle<String> y_str = factory->LookupSingleCharacterStringFromCode('y');
  const bool sticky = (String::IndexOf(isolate, flags, y_str, 0) >= 0);

  Handle<String> new_flags = flags;
  if (!sticky) {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, new_flags,
                                       factory->NewConsString(flags, y_str));
  }

  Handle<JSReceiver> splitter;
  {
    const int argc = 2;

    base::ScopedVector<Handle<Object>> argv(argc);
    argv[0] = recv;
    argv[1] = new_flags;

    Handle<Object> splitter_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, splitter_obj,
        Execution::New(isolate, ctor, argc, argv.begin()));

    splitter = Cast<JSReceiver>(splitter_obj);
  }

  uint32_t limit;
  RETURN_FAILURE_ON_EXCEPTION(isolate, ToUint32(isolate, limit_obj, &limit));

  const uint32_t length = string->length();

  if (limit == 0) return *factory->NewJSArray(0);

  if (length == 0) {
    Handle<Object> result;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, result, RegExpUtils::RegExpExec(isolate, splitter, string,
                                                 factory->undefined_value()));

    if (!IsNull(*result, isolate)) return *factory->NewJSArray(0);

    DirectHandle<FixedArray> elems = factory->NewFixedArray(1);
    elems->set(0, *string);
    return *factory->NewJSArrayWithElements(elems);
  }

  static const int kInitialArraySize = 8;
  Handle<FixedArray> elems = factory->NewFixedArrayWithHoles(kInitialArraySize);
  uint32_t num_elems = 0;

  uint32_t string_index = 0;
  uint32_t prev_string_index = 0;
  while (string_index < length) {
    RETURN_FAILURE_ON_EXCEPTION(
        isolate, RegExpUtils::SetLastIndex(isolate, splitter, string_index));

    Handle<Object> result;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, result, RegExpUtils::RegExpExec(isolate, splitter, string,
                                                 factory->undefined_value()));

    if (IsNull(*result, isolate)) {
      string_index = static_cast<uint32_t>(
          RegExpUtils::AdvanceStringIndex(string, string_index, unicode));
      continue;
    }

    Handle<Object> last_index_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, last_index_obj, RegExpUtils::GetLastIndex(isolate, splitter));

    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, last_index_obj, Object::ToLength(isolate, last_index_obj));

    const uint32_t end =
        std::min(PositiveNumberToUint32(*last_index_obj), length);
    if (end == prev_string_index) {
      string_index = static_cast<uint32_t>(
          RegExpUtils::AdvanceStringIndex(string, string_index, unicode));
      continue;
    }

    {
      DirectHandle<String> substr =
          factory->NewSubString(string, prev_string_index, string_index);
      elems = FixedArray::SetAndGrow(isolate, elems, num_elems++, substr);
      if (num_elems == limit) {
        return *NewJSArrayWithElements(isolate, elems, num_elems);
      }
    }

    prev_string_index = end;

    Handle<Object> num_captures_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num_captures_obj,
        Object::GetProperty(isolate, result,
                            isolate->factory()->length_string()));

    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, num_captures_obj, Object::ToLength(isolate, num_captures_obj));
    const uint32_t num_captures = PositiveNumberToUint32(*num_captures_obj);

    for (uint32_t i = 1; i < num_captures; i++) {
      Handle<Object> capture;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, capture, Object::GetElement(isolate, result, i));
      elems = FixedArray::SetAndGrow(isolate, elems, num_elems++, capture);
      if (num_elems == limit) {
        return *NewJSArrayWithElements(isolate, elems, num_elems);
      }
    }

    string_index = prev_string_index;
  }

  {
    DirectHandle<String> substr =
        factory->NewSubString(string, prev_string_index, length);
    elems = FixedArray::SetAndGrow(isolate, elems, num_elems++, substr);
  }

  return *NewJSArrayWithElements(isolate, elems, num_elems);
}

// Slow path for:
// ES#sec-regexp.prototype-@@replace
// RegExp.prototype [ @@replace ] ( string, replaceValue )
RUNTIME_FUNCTION(Runtime_RegExpReplaceRT) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());

  Handle<JSReceiver> recv = args.at<JSReceiver>(0);
  Handle<String> string = args.at<String>(1);
  Handle<Object> replace_obj = args.at(2);

  Factory* factory = isolate->factory();

  string = String::Flatten(isolate, string);

  const bool functional_replace = IsCallable(*replace_obj);

  Handle<String> replace;
  if (!functional_replace) {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, replace,
                                       Object::ToString(isolate, replace_obj));
  }

  // Fast-path for unmodified JSRegExps (and non-functional replace).
  if (RegExpUtils::IsUnmodifiedRegExp(isolate, recv)) {
    // We should never get here with functional replace because unmodified
    // regexp and functional replace should be fully handled in CSA code.
    CHECK(!functional_replace);
    Handle<Object> result;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, result,
        RegExpReplace(isolate, Cast<JSRegExp>(recv), string, replace));
    DCHECK(RegExpUtils::IsUnmodifiedRegExp(isolate, recv));
    return *result;
  }

  const uint32_t length = string->length();

  Handle<Object> global_obj;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, global_obj,
      JSReceiver::GetProperty(isolate, recv, factory->global_string()));
  const bool global = Object::BooleanValue(*global_obj, isolate);

  bool unicode = false;
  if (global) {
    Handle<Object> unicode_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, unicode_obj,
        JSReceiver::GetProperty(isolate, recv, factory->unicode_string()));
    unicode = Object::BooleanValue(*unicode_obj, isolate);

    RETURN_FAILURE_ON_EXCEPTION(isolate,
                                RegExpUtils::SetLastIndex(isolate, recv, 0));
  }

  base::SmallVector<Handle<Object>, kStaticVectorSlots> results;

  while (true) {
    Handle<Object> result;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, result, RegExpUtils::RegExpExec(isolate, recv, string,
                                                 factory->undefined_value()));

    if (IsNull(*result, isolate)) break;

    results.emplace_back(result);
    if (!global) break;

    Handle<Object> match_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, match_obj,
                                       Object::GetElement(isolate, result, 0));

    Handle<String> match;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, match,
                                       Object::ToString(isolate, match_obj));

    if (match->length() == 0) {
      RETURN_FAILURE_ON_EXCEPTION(isolate, RegExpUtils::SetAdvancedStringIndex(
                                               isolate, recv, string, unicode));
    }
  }

  // TODO(jgruber): Look into ReplacementStringBuilder instead.
  IncrementalStringBuilder builder(isolate);
  uint32_t next_source_position = 0;

  for (const auto& result : results) {
    HandleScope handle_scope(isolate);
    Handle<Object> captures_length_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, captures_length_obj,
        Object::GetProperty(isolate, result, factory->length_string()));

    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, captures_length_obj,
        Object::ToLength(isolate, captures_length_obj));
    const uint32_t captures_length =
        PositiveNumberToUint32(*captures_length_obj);

    Handle<Object> match_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, match_obj,
                                       Object::GetElement(isolate, result, 0));

    Handle<String> match;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, match,
                                       Object::ToString(isolate, match_obj));

    const int match_length = match->length();

    Handle<Object> position_obj;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, position_obj,
        Object::GetProperty(isolate, result, factory->index_string()));

    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, position_obj, Object::ToInteger(isolate, position_obj));
    const uint32_t position =
        std::min(PositiveNumberToUint32(*position_obj), length);

    // Do not reserve capacity since captures_length is user-controlled.
    base::SmallVector<Handle<Object>, kStaticVectorSlots> captures;

    for (uint32_t n = 0; n < captures_length; n++) {
      Handle<Object> capture;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, capture, Object::GetElement(isolate, result, n));

      if (!IsUndefined(*capture, isolate)) {
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, capture,
                                           Object::ToString(isolate, capture));
      }
      captures.emplace_back(capture);
    }

    Handle<Object> groups_obj = isolate->factory()->undefined_value();
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, groups_obj,
        Object::GetProperty(isolate, result, factory->groups_string()));

    const bool has_named_captures = !IsUndefined(*groups_obj, isolate);

    Handle<String> replacement;
    if (functional_replace) {
      const uint32_t argc =
          GetArgcForReplaceCallable(captures_length, has_named_captures);
      if (argc == static_cast<uint32_t>(-1)) {
        THROW_NEW_ERROR_RETURN_FAILURE(
            isolate, NewRangeError(MessageTemplate::kTooManyArguments));
      }

      base::ScopedVector<Handle<Object>> argv(argc);

      int cursor = 0;
      for (uint32_t j = 0; j < captures_length; j++) {
        argv[cursor++] = captures[j];
      }

      argv[cursor++] = handle(Smi::FromInt(position), isolate);
      argv[cursor++] = string;
      if (has_named_captures) argv[cursor++] = groups_obj;

      DCHECK_EQ(cursor, argc);

      Handle<Object> replacement_obj;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, replacement_obj,
          Execution::Call(isolate, replace_obj, factory->undefined_value(),
                          argc, argv.begin()));

      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, replacement, Object::ToString(isolate, replacement_obj));
    } else {
      DCHECK(!functional_replace);
      if (!IsUndefined(*groups_obj, isolate)) {
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
            isolate, groups_obj, Object::ToObject(isolate, groups_obj));
      }
      VectorBackedMatch m(isolate, string, match, position,
                          base::VectorOf(captures), groups_obj);
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, replacement, String::GetSubstitution(isolate, &m, replace));
    }

    if (position >= next_source_position) {
      builder.AppendString(
          factory->NewSubString(string, next_source_position, position));
      builder.AppendString(replacement);

      next_source_position = position + match_length;
    }
  }

  if (next_source_position < length) {
    builder.AppendString(
        factory->NewSubString(string, next_source_position, length));
  }

  RETURN_RESULT_OR_FAILURE(isolate, builder.Finish());
}

RUNTIME_FUNCTION(Runtime_RegExpInitializeAndCompile) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  // TODO(pwong): To follow the spec more closely and simplify calling code,
  // this could handle the canonicalization of pattern and flags. See
  // https://tc39.github.io/ecma262/#sec-regexpinitialize
  Handle<JSRegExp> regexp = args.at<JSRegExp>(0);
  Handle<String> source = args.at<String>(1);
  Handle<String> flags = args.at<String>(2);

  RETURN_FAILURE_ON_EXCEPTION(isolate,
                              JSRegExp::Initialize(regexp, source, flags));

  return *regexp;
}

RUNTIME_FUNCTION(Runtime_RegExpStringFromFlags) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  auto regexp = Cast<JSRegExp>(args[0]);
  DirectHandle<String> flags =
      JSRegExp::StringFromFlags(isolate, regexp->flags());
  return *flags;
}

}  // namespace internal
}  // namespace v8

// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp-macro-assembler.h"

#include "src/codegen/assembler.h"
#include "src/codegen/label.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/pointer-authentication.h"
#include "src/execution/simulator.h"
#include "src/regexp/regexp-stack.h"
#include "src/regexp/special-case.h"
#include "src/strings/unicode-inl.h"

#ifdef V8_INTL_SUPPORT
#include "unicode/uchar.h"
#include "unicode/unistr.h"
#endif  // V8_INTL_SUPPORT

namespace v8 {
namespace internal {

RegExpMacroAssembler::RegExpMacroAssembler(Isolate* isolate, Zone* zone)
    : slow_safe_compiler_(false),
      backtrack_limit_(JSRegExp::kNoBacktrackLimit),
      global_mode_(NOT_GLOBAL),
      isolate_(isolate),
      zone_(zone) {}

bool RegExpMacroAssembler::has_backtrack_limit() const {
  return backtrack_limit_ != JSRegExp::kNoBacktrackLimit;
}

// static
int RegExpMacroAssembler::CaseInsensitiveCompareNonUnicode(Address byte_offset1,
                                                           Address byte_offset2,
                                                           size_t byte_length,
                                                           Isolate* isolate) {
#ifdef V8_INTL_SUPPORT
  // This function is not allowed to cause a garbage collection.
  // A GC might move the calling generated code and invalidate the
  // return address on the stack.
  DisallowGarbageCollection no_gc;
  DCHECK_EQ(0, byte_length % 2);
  size_t length = byte_length / 2;
  base::uc16* substring1 = reinterpret_cast<base::uc16*>(byte_offset1);
  base::uc16* substring2 = reinterpret_cast<base::uc16*>(byte_offset2);

  for (size_t i = 0; i < length; i++) {
    UChar32 c1 = RegExpCaseFolding::Canonicalize(substring1[i]);
    UChar32 c2 = RegExpCaseFolding::Canonicalize(substring2[i]);
    if (c1 != c2) {
      return 0;
    }
  }
  return 1;
#else
  return CaseInsensitiveCompareUnicode(byte_offset1, byte_offset2, byte_length,
                                       isolate);
#endif
}

// static
int RegExpMacroAssembler::CaseInsensitiveCompareUnicode(Address byte_offset1,
                                                        Address byte_offset2,
                                                        size_t byte_length,
                                                        Isolate* isolate) {
  // This function is not allowed to cause a garbage collection.
  // A GC might move the calling generated code and invalidate the
  // return address on the stack.
  DisallowGarbageCollection no_gc;
  DCHECK_EQ(0, byte_length % 2);

#ifdef V8_INTL_SUPPORT
  int32_t length = static_cast<int32_t>(byte_length >> 1);
  icu::UnicodeString uni_str_1(reinterpret_cast<const char16_t*>(byte_offset1),
                               length);
  return uni_str_1.caseCompare(reinterpret_cast<const char16_t*>(byte_offset2),
                               length, U_FOLD_CASE_DEFAULT) == 0;
#else
  base::uc16* substring1 = reinterpret_cast<base::uc16*>(byte_offset1);
  base::uc16* substring2 = reinterpret_cast<base::uc16*>(byte_offset2);
  size_t length = byte_length >> 1;
  DCHECK_NOT_NULL(isolate);
  unibrow::Mapping<unibrow::Ecma262Canonicalize>* canonicalize =
      isolate->regexp_macro_assembler_canonicalize();
  for (size_t i = 0; i < length; i++) {
    unibrow::uchar c1 = substring1[i];
    unibrow::uchar c2 = substring2[i];
    if (c1 != c2) {
      unibrow::uchar s1[1] = {c1};
      canonicalize->get(c1, '\0', s1);
      if (s1[0] != c2) {
        unibrow::uchar s2[1] = {c2};
        canonicalize->get(c2, '\0', s2);
        if (s1[0] != s2[0]) {
          return 0;
        }
      }
    }
  }
  return 1;
#endif  // V8_INTL_SUPPORT
}

namespace {

uint32_t Hash(const ZoneList<CharacterRange>* ranges) {
  size_t seed = 0;
  for (int i = 0; i < ranges->length(); i++) {
    const CharacterRange& r = ranges->at(i);
    seed = base::hash_combine(seed, r.from(), r.to());
  }
  return static_cast<uint32_t>(seed);
}

constexpr base::uc32 MaskEndOfRangeMarker(base::uc32 c) {
  // CharacterRanges may use 0x10ffff as the end-of-range marker irrespective
  // of whether the regexp IsUnicode or not; translate the marker value here.
  DCHECK_IMPLIES(c > kMaxUInt16, c == String::kMaxCodePoint);
  return c & 0xffff;
}

int RangeArrayLengthFor(const ZoneList<CharacterRange>* ranges) {
  const int ranges_length = ranges->length();
  return MaskEndOfRangeMarker(ranges->at(ranges_length - 1).to()) == kMaxUInt16
             ? ranges_length * 2 - 1
             : ranges_length * 2;
}

bool Equals(const ZoneList<CharacterRange>* lhs,
            const DirectHandle<FixedUInt16Array>& rhs) {
  const int rhs_length = rhs->length();
  if (rhs_length != RangeArrayLengthFor(lhs)) return false;
  for (int i = 0; i < lhs->length(); i++) {
    const CharacterRange& r = lhs->at(i);
    if (rhs->get(i * 2 + 0) != r.from()) return false;
    if (i * 2 + 1 == rhs_length) break;
    if (rhs->get(i * 2 + 1) != r.to() + 1) return false;
  }
  return true;
}

Handle<FixedUInt16Array> MakeRangeArray(
    Isolate* isolate, const ZoneList<CharacterRange>* ranges) {
  const int ranges_length = ranges->length();
  const int range_array_length = RangeArrayLengthFor(ranges);
  Handle<FixedUInt16Array> range_array =
      FixedUInt16Array::New(isolate, range_array_length);
  for (int i = 0; i < ranges_length; i++) {
    const CharacterRange& r = ranges->at(i);
    DCHECK_LE(r.from(), kMaxUInt16);
    range_array->set(i * 2 + 0, r.from());
    const base::uc32 to = MaskEndOfRangeMarker(r.to());
    if (i == ranges_length - 1 && to == kMaxUInt16) {
      DCHECK_EQ(range_array_length, ranges_length * 2 - 1);
      break;  // Avoid overflow by leaving the last range open-ended.
    }
    DCHECK_LT(to, kMaxUInt16);
    range_array->set(i * 2 + 1, to + 1);  // Exclusive.
  }
  return range_array;
}

}  // namespace

Handle<ByteArray> NativeRegExpMacroAssembler::GetOrAddRangeArray(
    const ZoneList<CharacterRange>* ranges) {
  const uint32_t hash = Hash(ranges);

  if (range_array_cache_.count(hash) != 0) {
    Handle<FixedUInt16Array> range_array = range_array_cache_[hash];
    if (Equals(ranges, range_array)) return range_array;
  }

  Handle<FixedUInt16Array> range_array = MakeRangeArray(isolate(), ranges);
  range_array_cache_[hash] = range_array;
  return range_array;
}

// static
uint32_t RegExpMacroAssembler::IsCharacterInRangeArray(uint32_t current_char,
                                                       Address raw_byte_array) {
  // Use uint32_t to avoid complexity around bool return types (which may be
  // optimized to use only the least significant byte).
  static constexpr uint32_t kTrue = 1;
  static constexpr uint32_t kFalse = 0;

  Tagged<FixedUInt16Array> ranges =
      Cast<FixedUInt16Array>(Tagged<Object>(raw_byte_array));
  DCHECK_GE(ranges->length(), 1);

  // Shortcut for fully out of range chars.
  if (current_char < ranges->get(0)) return kFalse;
  if (current_char >= ranges->get(ranges->length() - 1)) {
    // The last range may be open-ended.
    return (ranges->length() % 2) == 0 ? kFalse : kTrue;
  }

  // Binary search for the matching range. `ranges` is encoded as
  // [from0, to0, from1, to1, ..., fromN, toN], or
  // [from0, to0, from1, to1, ..., fromN] (open-ended last interval).

  int mid, lower = 0, upper = ranges->length();
  do {
    mid = lower + (upper - lower) / 2;
    const base::uc16 elem = ranges->get(mid);
    if (current_char < elem) {
      upper = mid;
    } else if (current_char > elem) {
      lower = mid + 1;
    } else {
      DCHECK_EQ(current_char, elem);
      break;
    }
  } while (lower < upper);

  const bool current_char_ge_last_elem = current_char >= ranges->get(mid);
  const int current_range_start_index =
      current_char_ge_last_elem ? mid : mid - 1;

  // Ranges start at even indices and end at odd indices.
  return (current_range_start_index % 2) == 0 ? kTrue : kFalse;
}

void RegExpMacroAssembler::CheckNotInSurrogatePair(int cp_offset,
                                                   Label* on_failure) {
  Label ok;
  // Check that current character is not a trail surrogate.
  LoadCurrentCharacter(cp_offset, &ok);
  CheckCharacterNotInRange(kTrailSurrogateStart, kTrailSurrogateEnd, &ok);
  // Check that previous character is not a lead surrogate.
  LoadCurrentCharacter(cp_offset - 1, &ok);
  CheckCharacterInRange(kLeadSurrogateStart, kLeadSurrogateEnd, on_failure);
  Bind(&ok);
}

void RegExpMacroAssembler::CheckPosition(int cp_offset,
                                         Label* on_outside_input) {
  LoadCurrentCharacter(cp_offset, on_outside_input, true);
}

void RegExpMacroAssembler::LoadCurrentCharacter(int cp_offset,
                                                Label* on_end_of_input,
                                                bool check_bounds,
                                                int characters,
                                                int eats_at_least) {
  // By default, eats_at_least = characters.
  if (eats_at_least == kUseCharactersValue) {
    eats_at_least = characters;
  }

  LoadCurrentCharacterImpl(cp_offset, on_end_of_input, check_bounds, characters,
                           eats_at_least);
}

void NativeRegExpMacroAssembler::LoadCurrentCharacterImpl(
    int cp_offset, Label* on_end_of_input, bool check_bounds, int characters,
    int eats_at_least) {
  // It's possible to preload a small number of characters when each success
  // path requires a large number of characters, but not the reverse.
  DCHECK_GE(eats_at_least, characters);

  DCHECK(base::IsInRange(cp_offset, kMinCPOffset, kMaxCPOffset));
  if (check_bounds) {
    if (cp_offset >= 0) {
      CheckPosition(cp_offset + eats_at_least - 1, on_end_of_input);
    } else {
      CheckPosition(cp_offset, on_end_of_input);
    }
  }
  LoadCurrentCharacterUnchecked(cp_offset, characters);
}

bool NativeRegExpMacroAssembler::CanReadUnaligned() const {
  return v8_flags.enable_regexp_unaligned_accesses && !slow_safe();
}

#ifndef COMPILING_IRREGEXP_FOR_EXTERNAL_EMBEDDER

// This method may only be called after an interrupt.
// static
int NativeRegExpMacroAssembler::CheckStackGuardState(
    Isolate* isolate, int start_index, RegExp::CallOrigin call_origin,
    Address* return_address, Tagged<InstructionStream> re_code,
    Address* subject, const uint8_t** input_start, const uint8_t** input_end,
    uintptr_t gap) {
  DisallowGarbageCollection no_gc;
  Address old_pc = PointerAuthentication::AuthenticatePC(return_address, 0);
  DCHECK_LE(re_code->instruction_start(), old_pc);
  DCHECK_LE(old_pc, re_code->code(kAcquireLoad)->instruction_end());

  StackLimitCheck check(isolate);
  bool js_has_overflowed = check.JsHasOverflowed(gap);

  if (call_origin == RegExp::CallOrigin::kFromJs) {
    // Direct calls from JavaScript can be interrupted in two ways:
    // 1. A real stack overflow, in which case we let the caller throw the
    //    exception.
    // 2. The stack guard was used to interrupt execution for another purpose,
    //    forcing the call through the runtime system.

    // Bug(v8:9540) Investigate why this method is called from JS although no
    // stackoverflow or interrupt is pending on ARM64. We return 0 in this case
    // to continue execution normally.
    if (js_has_overflowed) {
      return EXCEPTION;
    } else if (check.InterruptRequested()) {
      return RETRY;
    } else {
      return 0;
    }
  }
  DCHECK(call_origin == RegExp::CallOrigin::kFromRuntime);

  // Prepare for possible GC.
  HandleScope handles(isolate);
  DirectHandle<InstructionStream> code_handle(re_code, isolate);
  DirectHandle<String> subject_handle(Cast<String>(Tagged<Object>(*subject)),
                                      isolate);
  bool is_one_byte = String::IsOneByteRepresentationUnderneath(*subject_handle);
  int return_value = 0;

  {
    DisableGCMole no_gc_mole;
    if (js_has_overflowed) {
      AllowGarbageCollection yes_gc;
      isolate->StackOverflow();
      return_value = EXCEPTION;
    } else if (check.InterruptRequested()) {
      AllowGarbageCollection yes_gc;
      Tagged<Object> result = isolate->stack_guard()->HandleInterrupts();
      if (IsException(result, isolate)) return_value = EXCEPTION;
    }

    // We are not using operator == here because it does a slow DCHECK
    // CheckObjectComparisonAllowed() which might crash when trying to access
    // the page header of the stale pointer.
    if (!code_handle->SafeEquals(re_code)) {  // Return address no longer valid
      // Overwrite the return address on the stack.
      intptr_t delta = code_handle->address() - re_code.address();
      Address new_pc = old_pc + delta;
      // TODO(v8:10026): avoid replacing a signed pointer.
      PointerAuthentication::ReplacePC(return_address, new_pc, 0);
    }
  }

  // If we continue, we need to update the subject string addresses.
  if (return_value == 0) {
    // String encoding might have changed.
    if (String::IsOneByteRepresentationUnderneath(*subject_handle) !=
        is_one_byte) {
      // If we changed between an LATIN1 and an UC16 string, the specialized
      // code cannot be used, and we need to restart regexp matching from
      // scratch (including, potentially, compiling a new version of the code).
      return_value = RETRY;
    } else {
      *subject = subject_handle->ptr();
      intptr_t byte_length = *input_end - *input_start;
      *input_start = subject_handle->AddressOfCharacterAt(start_index, no_gc);
      *input_end = *input_start + byte_length;
    }
  }
  return return_value;
}

// Returns a {Result} sentinel, or the number of successful matches.
int NativeRegExpMacroAssembler::Match(DirectHandle<IrRegExpData> regexp_data,
                                      DirectHandle<String> subject,
                                      int* offsets_vector,
                                      int offsets_vector_length,
                                      int previous_index, Isolate* isolate) {
  DCHECK(subject->IsFlat());
  DCHECK_LE(0, previous_index);
  DCHECK_LE(previous_index, subject->length());

  // No allocations before calling the regexp, but we can't use
  // DisallowGarbageCollection, since regexps might be preempted, and another
  // thread might do allocation anyway.

  Tagged<String> subject_ptr = *subject;
  // Character offsets into string.
  int start_offset = previous_index;
  int char_length = subject_ptr->length() - start_offset;
  int slice_offset = 0;

  // The string has been flattened, so if it is a cons string it contains the
  // full string in the first part.
  if (StringShape(subject_ptr).IsCons()) {
    DCHECK_EQ(0, Cast<ConsString>(subject_ptr)->second()->length());
    subject_ptr = Cast<ConsString>(subject_ptr)->first();
  } else if (StringShape(subject_ptr).IsSliced()) {
    Tagged<SlicedString> slice = Cast<SlicedString>(subject_ptr);
    subject_ptr = slice->parent();
    slice_offset = slice->offset();
  }
  if (StringShape(subject_ptr).IsThin()) {
    subject_ptr = Cast<ThinString>(subject_ptr)->actual();
  }
  // Ensure that an underlying string has the same representation.
  bool is_one_byte = subject_ptr->IsOneByteRepresentation();
  DCHECK(IsExternalString(subject_ptr) || IsSeqString(subject_ptr));
  // String is now either Sequential or External
  int char_size_shift = is_one_byte ? 0 : 1;

  DisallowGarbageCollection no_gc;
  const uint8_t* input_start =
      subject_ptr->AddressOfCharacterAt(start_offset + slice_offset, no_gc);
  int byte_length = char_length << char_size_shift;
  const uint8_t* input_end = input_start + byte_length;
  return Execute(*subject, start_offset, input_start, input_end, offsets_vector,
                 offsets_vector_length, isolate, *regexp_data);
}

// static
int NativeRegExpMacroAssembler::ExecuteForTesting(
    Tagged<String> input, int start_offset, const uint8_t* input_start,
    const uint8_t* input_end, int* output, int output_size, Isolate* isolate,
    Tagged<JSRegExp> regexp) {
  Tagged<RegExpData> data = regexp->data(isolate);
  SBXCHECK(Is<IrRegExpData>(data));
  return Execute(input, start_offset, input_start, input_end, output,
                 output_size, isolate, Cast<IrRegExpData>(data));
}

// Returns a {Result} sentinel, or the number of successful matches.
int NativeRegExpMacroAssembler::Execute(
    Tagged<String>
        input,  // This needs to be the unpacked (sliced, cons) string.
    int start_offset, const uint8_t* input_start, const uint8_t* input_end,
    int* output, int output_size, Isolate* isolate,
    Tagged<IrRegExpData> regexp_data) {
  RegExpStackScope stack_scope(isolate);

  bool is_one_byte = String::IsOneByteRepresentationUnderneath(input);
  Tagged<Code> code = regexp_data->code(isolate, is_one_byte);
  RegExp::CallOrigin call_origin = RegExp::CallOrigin::kFromRuntime;

  using RegexpMatcherSig =
      // NOLINTNEXTLINE(readability/casting)
      int(Address input_string, int start_offset, const uint8_t* input_start,
          const uint8_t* input_end, int* output, int output_size,
          int call_origin, Isolate* isolate, Address regexp_data);

  auto fn = GeneratedCode<RegexpMatcherSig>::FromCode(isolate, code);
  int result =
      fn.Call(input.ptr(), start_offset, input_start, input_end, output,
              output_size, call_origin, isolate, regexp_data.ptr());
  DCHECK_GE(result, SMALLEST_REGEXP_RESULT);

  if (result == EXCEPTION && !isolate->has_exception()) {
    // We detected a stack overflow (on the backtrack stack) in RegExp code,
    // but haven't created the exception yet. Additionally, we allow heap
    // allocation because even though it invalidates {input_start} and
    // {input_end}, we are about to return anyway.
    AllowGarbageCollection allow_allocation;
    isolate->StackOverflow();
  }
  return result;
}

#endif  // !COMPILING_IRREGEXP_FOR_EXTERNAL_EMBEDDER

// clang-format off
const uint8_t NativeRegExpMacroAssembler::word_character_map[] = {
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,

    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,  // '0' - '7'
    0xFFu, 0xFFu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,  // '8' - '9'

    0x00u, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,  // 'A' - 'G'
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,  // 'H' - 'O'
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,  // 'P' - 'W'
    0xFFu, 0xFFu, 0xFFu, 0x00u, 0x00u, 0x00u, 0x00u, 0xFFu,  // 'X' - 'Z', '_'

    0x00u, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,  // 'a' - 'g'
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,  // 'h' - 'o'
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,  // 'p' - 'w'
    0xFFu, 0xFFu, 0xFFu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,  // 'x' - 'z'
    // Latin-1 range
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,

    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,

    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,

    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
};
// clang-format on

// static
Address NativeRegExpMacroAssembler::GrowStack(Isolate* isolate) {
  DisallowGarbageCollection no_gc;

  RegExpStack* regexp_stack = isolate->regexp_stack();
  const size_t old_size = regexp_stack->memory_size();

#ifdef DEBUG
  const Address old_stack_top = regexp_stack->memory_top();
  const Address old_stack_pointer = regexp_stack->stack_pointer();
  CHECK_LE(old_stack_pointer, old_stack_top);
  CHECK_LE(static_cast<size_t>(old_stack_top - old_stack_pointer), old_size);
#endif  // DEBUG

  Address new_stack_base = regexp_stack->EnsureCapacity(old_size * 2);
  if (new_stack_base == kNullAddress) return kNullAddress;

  return regexp_stack->stack_pointer();
}

}  // namespace internal
}  // namespace v8

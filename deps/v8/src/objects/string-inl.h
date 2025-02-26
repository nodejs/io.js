// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_STRING_INL_H_
#define V8_OBJECTS_STRING_INL_H_

#include <optional>
#include <type_traits>

#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/execution/isolate-utils.h"
#include "src/handles/handles-inl.h"
#include "src/heap/factory.h"
#include "src/heap/heap-layout-inl.h"
#include "src/numbers/hash-seed-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/name-inl.h"
#include "src/objects/objects-body-descriptors.h"
#include "src/objects/smi-inl.h"
#include "src/objects/string-table-inl.h"
#include "src/objects/string.h"
#include "src/sandbox/external-pointer-inl.h"
#include "src/sandbox/external-pointer.h"
#include "src/sandbox/isolate.h"
#include "src/strings/string-hasher-inl.h"
#include "src/strings/unicode-inl.h"
#include "src/torque/runtime-macro-shims.h"
#include "src/torque/runtime-support.h"
#include "src/utils/utils.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

class V8_NODISCARD SharedStringAccessGuardIfNeeded {
 public:
  // Creates no SpinningMutexGuard for the string access since it was
  // called from the main thread.
  explicit SharedStringAccessGuardIfNeeded(Isolate* isolate) {}

  // Creates a SpinningMutexGuard for the string access if it was called
  // from a background thread.
  explicit SharedStringAccessGuardIfNeeded(LocalIsolate* local_isolate) {
    if (IsNeeded(local_isolate)) {
      mutex_guard.emplace(local_isolate->internalized_string_access());
    }
  }

  // Slow version which gets the isolate from the String.
  explicit SharedStringAccessGuardIfNeeded(Tagged<String> str) {
    Isolate* isolate = GetIsolateIfNeeded(str);
    if (isolate != nullptr) {
      mutex_guard.emplace(isolate->internalized_string_access());
    }
  }

  SharedStringAccessGuardIfNeeded(Tagged<String> str,
                                  LocalIsolate* local_isolate) {
    if (IsNeeded(str, local_isolate)) {
      mutex_guard.emplace(local_isolate->internalized_string_access());
    }
  }

  static SharedStringAccessGuardIfNeeded NotNeeded() {
    return SharedStringAccessGuardIfNeeded();
  }

  static bool IsNeeded(Tagged<String> str, LocalIsolate* local_isolate) {
    return IsNeeded(local_isolate) && IsNeeded(str, false);
  }

  static bool IsNeeded(Tagged<String> str, bool check_local_heap = true) {
    if (check_local_heap) {
      LocalHeap* local_heap = LocalHeap::Current();
      if (!local_heap || local_heap->is_main_thread()) {
        // Don't acquire the lock for the main thread.
        return false;
      }
    }

    if (ReadOnlyHeap::Contains(str)) {
      // Don't acquire lock for strings in ReadOnlySpace.
      return false;
    }

    return true;
  }

  static bool IsNeeded(LocalIsolate* local_isolate) {
    // TODO(leszeks): Remove the nullptr check for local_isolate.
    return local_isolate && !local_isolate->heap()->is_main_thread();
  }

 private:
  // Default constructor and move constructor required for the NotNeeded()
  // static constructor.
  constexpr SharedStringAccessGuardIfNeeded() = default;
  constexpr SharedStringAccessGuardIfNeeded(SharedStringAccessGuardIfNeeded&&)
      V8_NOEXCEPT {
    DCHECK(!mutex_guard.has_value());
  }

  // Returns the Isolate from the String if we need it for the lock.
  static Isolate* GetIsolateIfNeeded(Tagged<String> str) {
    if (!IsNeeded(str)) return nullptr;

    Isolate* isolate;
    if (!GetIsolateFromHeapObject(str, &isolate)) {
      // If we can't get the isolate from the String, it must be read-only.
      DCHECK(ReadOnlyHeap::Contains(str));
      return nullptr;
    }
    return isolate;
  }

  std::optional<base::SpinningMutexGuard> mutex_guard;
};

uint32_t String::length() const { return length_; }

uint32_t String::length(AcquireLoadTag) const {
  return base::AsAtomic32::Acquire_Load(&length_);
}

void String::set_length(uint32_t value) { length_ = value; }

void String::set_length(uint32_t value, ReleaseStoreTag) {
  base::AsAtomic32::Release_Store(&length_, value);
}

static_assert(kTaggedCanConvertToRawObjects);

StringShape::StringShape(const Tagged<String> str)
    : type_(str->map(kAcquireLoad)->instance_type()) {
  set_valid();
  DCHECK_EQ(type_ & kIsNotStringMask, kStringTag);
}

StringShape::StringShape(const Tagged<String> str, PtrComprCageBase cage_base)
    : type_(str->map(kAcquireLoad)->instance_type()) {
  set_valid();
  DCHECK_EQ(type_ & kIsNotStringMask, kStringTag);
}

StringShape::StringShape(Tagged<Map> map) : type_(map->instance_type()) {
  set_valid();
  DCHECK_EQ(type_ & kIsNotStringMask, kStringTag);
}

StringShape::StringShape(InstanceType t) : type_(static_cast<uint32_t>(t)) {
  set_valid();
  DCHECK_EQ(type_ & kIsNotStringMask, kStringTag);
}

bool StringShape::IsInternalized() const {
  DCHECK(valid());
  static_assert(kNotInternalizedTag != 0);
  return (type_ & (kIsNotStringMask | kIsNotInternalizedMask)) ==
         (kStringTag | kInternalizedTag);
}

bool StringShape::IsCons() const {
  return (type_ & kStringRepresentationMask) == kConsStringTag;
}

bool StringShape::IsThin() const {
  return (type_ & kStringRepresentationMask) == kThinStringTag;
}

bool StringShape::IsSliced() const {
  return (type_ & kStringRepresentationMask) == kSlicedStringTag;
}

bool StringShape::IsIndirect() const {
  return (type_ & kIsIndirectStringMask) == kIsIndirectStringTag;
}

bool StringShape::IsDirect() const { return !IsIndirect(); }

bool StringShape::IsExternal() const {
  return (type_ & kStringRepresentationMask) == kExternalStringTag;
}

bool StringShape::IsSequential() const {
  return (type_ & kStringRepresentationMask) == kSeqStringTag;
}

bool StringShape::IsUncachedExternal() const {
  return (type_ & kUncachedExternalStringMask) == kUncachedExternalStringTag;
}

bool StringShape::IsShared() const {
  // TODO(v8:12007): Set is_shared to true on internalized string when
  // v8_flags.shared_string_table is removed.
  return (type_ & kSharedStringMask) == kSharedStringTag ||
         (v8_flags.shared_string_table && IsInternalized());
}

StringRepresentationTag StringShape::representation_tag() const {
  uint32_t tag = (type_ & kStringRepresentationMask);
  return static_cast<StringRepresentationTag>(tag);
}

uint32_t StringShape::encoding_tag() const {
  return type_ & kStringEncodingMask;
}

uint32_t StringShape::representation_and_encoding_tag() const {
  return (type_ & (kStringRepresentationAndEncodingMask));
}

uint32_t StringShape::representation_encoding_and_shared_tag() const {
  return (type_ & (kStringRepresentationEncodingAndSharedMask));
}

static_assert((kStringRepresentationAndEncodingMask) ==
              Internals::kStringRepresentationAndEncodingMask);

static_assert(static_cast<uint32_t>(kStringEncodingMask) ==
              Internals::kStringEncodingMask);

bool StringShape::IsSequentialOneByte() const {
  return representation_and_encoding_tag() == kSeqOneByteStringTag;
}

bool StringShape::IsSequentialTwoByte() const {
  return representation_and_encoding_tag() == kSeqTwoByteStringTag;
}

bool StringShape::IsExternalOneByte() const {
  return representation_and_encoding_tag() == kExternalOneByteStringTag;
}

static_assert(kExternalOneByteStringTag ==
              Internals::kExternalOneByteRepresentationTag);

static_assert(v8::String::ONE_BYTE_ENCODING == kOneByteStringTag);

bool StringShape::IsExternalTwoByte() const {
  return representation_and_encoding_tag() == kExternalTwoByteStringTag;
}

static_assert(kExternalTwoByteStringTag ==
              Internals::kExternalTwoByteRepresentationTag);

static_assert(v8::String::TWO_BYTE_ENCODING == kTwoByteStringTag);

template <typename TDispatcher, typename... TArgs>
inline auto String::DispatchToSpecificTypeWithoutCast(
    InstanceType instance_type, TArgs&&... args) {
  switch (StringShape(instance_type).representation_and_encoding_tag()) {
    case kSeqStringTag | kOneByteStringTag:
      return TDispatcher::HandleSeqOneByteString(std::forward<TArgs>(args)...);
    case kSeqStringTag | kTwoByteStringTag:
      return TDispatcher::HandleSeqTwoByteString(std::forward<TArgs>(args)...);
    case kConsStringTag | kOneByteStringTag:
    case kConsStringTag | kTwoByteStringTag:
      return TDispatcher::HandleConsString(std::forward<TArgs>(args)...);
    case kExternalStringTag | kOneByteStringTag:
      return TDispatcher::HandleExternalOneByteString(
          std::forward<TArgs>(args)...);
    case kExternalStringTag | kTwoByteStringTag:
      return TDispatcher::HandleExternalTwoByteString(
          std::forward<TArgs>(args)...);
    case kSlicedStringTag | kOneByteStringTag:
    case kSlicedStringTag | kTwoByteStringTag:
      return TDispatcher::HandleSlicedString(std::forward<TArgs>(args)...);
    case kThinStringTag | kOneByteStringTag:
    case kThinStringTag | kTwoByteStringTag:
      return TDispatcher::HandleThinString(std::forward<TArgs>(args)...);
    default:
      return TDispatcher::HandleInvalidString(std::forward<TArgs>(args)...);
  }
}

// All concrete subclasses of String (leaves of the inheritance tree).
#define STRING_CLASS_TYPES(V) \
  V(SeqOneByteString)         \
  V(SeqTwoByteString)         \
  V(ConsString)               \
  V(ExternalOneByteString)    \
  V(ExternalTwoByteString)    \
  V(SlicedString)             \
  V(ThinString)

template <typename TDispatcher>
V8_INLINE auto String::DispatchToSpecificType(TDispatcher&& dispatcher) const
    -> std::common_type_t<decltype(dispatcher(Tagged<SeqOneByteString>{})),
                          decltype(dispatcher(Tagged<SeqTwoByteString>{})),
                          decltype(dispatcher(Tagged<ExternalOneByteString>{})),
                          decltype(dispatcher(Tagged<ExternalTwoByteString>{})),
                          decltype(dispatcher(Tagged<ThinString>{})),
                          decltype(dispatcher(Tagged<ConsString>{})),
                          decltype(dispatcher(Tagged<SlicedString>{}))> {
  // The following code inlines the dispatcher calls with V8_INLINE_STATEMENT.
  // This is so that this behaves, as far as the caller is concerned, like an
  // inlined type switch.

#if V8_STATIC_ROOTS_BOOL
  Tagged<Map> map = this->map(kAcquireLoad);
  if (InstanceTypeChecker::IsSeqString(map)) {
    if (InstanceTypeChecker::IsOneByteString(map)) {
      V8_INLINE_STATEMENT return dispatcher(
          UncheckedCast<SeqOneByteString>(this));
    } else {
      V8_INLINE_STATEMENT return dispatcher(
          UncheckedCast<SeqTwoByteString>(this));
    }
  } else if (InstanceTypeChecker::IsExternalString(map)) {
    if (InstanceTypeChecker::IsOneByteString(map)) {
      V8_INLINE_STATEMENT return dispatcher(
          UncheckedCast<ExternalOneByteString>(this));
    } else {
      V8_INLINE_STATEMENT return dispatcher(
          UncheckedCast<ExternalTwoByteString>(this));
    }
  } else if (InstanceTypeChecker::IsThinString(map)) {
    V8_INLINE_STATEMENT return dispatcher(UncheckedCast<ThinString>(this));
  } else if (InstanceTypeChecker::IsConsString(map)) {
    V8_INLINE_STATEMENT return dispatcher(UncheckedCast<ConsString>(this));
  } else if (InstanceTypeChecker::IsSlicedString(map)) {
    V8_INLINE_STATEMENT return dispatcher(UncheckedCast<SlicedString>(this));
  }
  UNREACHABLE();

#else
  switch (StringShape(Tagged(this)).representation_and_encoding_tag()) {
    case kSeqStringTag | kOneByteStringTag:
      V8_INLINE_STATEMENT return dispatcher(
          UncheckedCast<SeqOneByteString>(this));
    case kSeqStringTag | kTwoByteStringTag:
      V8_INLINE_STATEMENT return dispatcher(
          UncheckedCast<SeqTwoByteString>(this));
    case kConsStringTag | kOneByteStringTag:
    case kConsStringTag | kTwoByteStringTag:
      V8_INLINE_STATEMENT return dispatcher(UncheckedCast<ConsString>(this));
    case kExternalStringTag | kOneByteStringTag:
      V8_INLINE_STATEMENT return dispatcher(
          UncheckedCast<ExternalOneByteString>(this));
    case kExternalStringTag | kTwoByteStringTag:
      V8_INLINE_STATEMENT return dispatcher(
          UncheckedCast<ExternalTwoByteString>(this));
    case kSlicedStringTag | kOneByteStringTag:
    case kSlicedStringTag | kTwoByteStringTag:
      V8_INLINE_STATEMENT return dispatcher(UncheckedCast<SlicedString>(this));
    case kThinStringTag | kOneByteStringTag:
    case kThinStringTag | kTwoByteStringTag:
      V8_INLINE_STATEMENT return dispatcher(UncheckedCast<ThinString>(this));
    default:
      UNREACHABLE();
  }
  UNREACHABLE();
#endif
}

bool String::IsOneByteRepresentation() const {
  return InstanceTypeChecker::IsOneByteString(map());
}

bool String::IsTwoByteRepresentation() const {
  return InstanceTypeChecker::IsTwoByteString(map());
}

base::uc32 FlatStringReader::Get(uint32_t index) const {
  if (is_one_byte_) {
    return Get<uint8_t>(index);
  } else {
    return Get<base::uc16>(index);
  }
}

template <typename Char>
Char FlatStringReader::Get(uint32_t index) const {
  DCHECK_EQ(is_one_byte_, sizeof(Char) == 1);
  DCHECK_LT(index, length_);
  if (sizeof(Char) == 1) {
    return static_cast<Char>(static_cast<const uint8_t*>(start_)[index]);
  } else {
    return static_cast<Char>(static_cast<const base::uc16*>(start_)[index]);
  }
}

template <typename Char>
class SequentialStringKey final : public StringTableKey {
 public:
  SequentialStringKey(base::Vector<const Char> chars, uint64_t seed,
                      bool convert = false)
      : SequentialStringKey(StringHasher::HashSequentialString<Char>(
                                chars.begin(), chars.length(), seed),
                            chars, convert) {}

  SequentialStringKey(int raw_hash_field, base::Vector<const Char> chars,
                      bool convert = false)
      : StringTableKey(raw_hash_field, chars.length()),
        chars_(chars),
        convert_(convert) {}

  template <typename IsolateT>
  bool IsMatch(IsolateT* isolate, Tagged<String> s) {
    return s->IsEqualTo<String::EqualityType::kNoLengthCheck>(chars_, isolate);
  }

  template <typename IsolateT>
  void PrepareForInsertion(IsolateT* isolate) {
    if (sizeof(Char) == 1) {
      internalized_string_ = isolate->factory()->NewOneByteInternalizedString(
          base::Vector<const uint8_t>::cast(chars_), raw_hash_field());
    } else if (convert_) {
      internalized_string_ =
          isolate->factory()->NewOneByteInternalizedStringFromTwoByte(
              base::Vector<const uint16_t>::cast(chars_), raw_hash_field());
    } else {
      internalized_string_ = isolate->factory()->NewTwoByteInternalizedString(
          base::Vector<const uint16_t>::cast(chars_), raw_hash_field());
    }
  }

  DirectHandle<String> GetHandleForInsertion(Isolate* isolate) {
    DCHECK(!internalized_string_.is_null());
    return internalized_string_;
  }

 private:
  base::Vector<const Char> chars_;
  bool convert_;
  DirectHandle<String> internalized_string_;
};

using OneByteStringKey = SequentialStringKey<uint8_t>;
using TwoByteStringKey = SequentialStringKey<uint16_t>;

template <typename SeqString>
class SeqSubStringKey final : public StringTableKey {
 public:
  using Char = typename SeqString::Char;
// VS 2017 on official builds gives this spurious warning:
// warning C4789: buffer 'key' of size 16 bytes will be overrun; 4 bytes will
// be written starting at offset 16
// https://bugs.chromium.org/p/v8/issues/detail?id=6068
#if defined(V8_CC_MSVC)
#pragma warning(push)
#pragma warning(disable : 4789)
#endif
  SeqSubStringKey(Isolate* isolate, DirectHandle<SeqString> string, int from,
                  int len, bool convert = false)
      : StringTableKey(0, len),
        string_(string),
        from_(from),
        convert_(convert) {
    // We have to set the hash later.
    DisallowGarbageCollection no_gc;
    uint32_t raw_hash_field = StringHasher::HashSequentialString(
        string->GetChars(no_gc) + from, len, HashSeed(isolate));
    set_raw_hash_field(raw_hash_field);

    DCHECK_LE(0, length());
    DCHECK_LE(from_ + length(), string_->length());
    DCHECK_EQ(IsSeqOneByteString(*string_), sizeof(Char) == 1);
    DCHECK_EQ(IsSeqTwoByteString(*string_), sizeof(Char) == 2);
  }
#if defined(V8_CC_MSVC)
#pragma warning(pop)
#endif

  bool IsMatch(Isolate* isolate, Tagged<String> string) {
    DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(string));
    DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(*string_));
    DisallowGarbageCollection no_gc;
    return string->IsEqualTo<String::EqualityType::kNoLengthCheck>(
        base::Vector<const Char>(string_->GetChars(no_gc) + from_, length()),
        isolate);
  }

  void PrepareForInsertion(Isolate* isolate) {
    if (sizeof(Char) == 1 || (sizeof(Char) == 2 && convert_)) {
      DirectHandle<SeqOneByteString> result =
          isolate->factory()->AllocateRawOneByteInternalizedString(
              length(), raw_hash_field());
      DisallowGarbageCollection no_gc;
      CopyChars(result->GetChars(no_gc), string_->GetChars(no_gc) + from_,
                length());
      internalized_string_ = result;
    } else {
      DirectHandle<SeqTwoByteString> result =
          isolate->factory()->AllocateRawTwoByteInternalizedString(
              length(), raw_hash_field());
      DisallowGarbageCollection no_gc;
      CopyChars(result->GetChars(no_gc), string_->GetChars(no_gc) + from_,
                length());
      internalized_string_ = result;
    }
  }

  DirectHandle<String> GetHandleForInsertion(Isolate* isolate) {
    DCHECK(!internalized_string_.is_null());
    return internalized_string_;
  }

 private:
  DirectHandle<typename CharTraits<Char>::String> string_;
  int from_;
  bool convert_;
  DirectHandle<String> internalized_string_;
};

using SeqOneByteSubStringKey = SeqSubStringKey<SeqOneByteString>;
using SeqTwoByteSubStringKey = SeqSubStringKey<SeqTwoByteString>;

bool String::Equals(Tagged<String> other) const {
  if (other == this) return true;
  if (IsInternalizedString(this) && IsInternalizedString(other)) {
    return false;
  }
  return SlowEquals(other);
}

// static
bool String::Equals(Isolate* isolate, DirectHandle<String> one,
                    DirectHandle<String> two) {
  if (one.is_identical_to(two)) return true;
  if (IsInternalizedString(*one) && IsInternalizedString(*two)) {
    return false;
  }
  return SlowEquals(isolate, one, two);
}

template <String::EqualityType kEqType, typename Char>
bool String::IsEqualTo(base::Vector<const Char> str, Isolate* isolate) const {
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(this));
  return IsEqualToImpl<kEqType>(str,
                                SharedStringAccessGuardIfNeeded::NotNeeded());
}

template <String::EqualityType kEqType, typename Char>
bool String::IsEqualTo(base::Vector<const Char> str) const {
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(this));
  return IsEqualToImpl<kEqType>(str,
                                SharedStringAccessGuardIfNeeded::NotNeeded());
}

template <String::EqualityType kEqType, typename Char>
bool String::IsEqualTo(base::Vector<const Char> str,
                       LocalIsolate* isolate) const {
  SharedStringAccessGuardIfNeeded access_guard(isolate);
  return IsEqualToImpl<kEqType>(str, access_guard);
}

template <String::EqualityType kEqType, typename Char>
bool String::IsEqualToImpl(
    base::Vector<const Char> str,
    const SharedStringAccessGuardIfNeeded& access_guard) const {
  size_t len = str.size();
  switch (kEqType) {
    case EqualityType::kWholeString:
      if (static_cast<size_t>(length()) != len) return false;
      break;
    case EqualityType::kPrefix:
      if (static_cast<size_t>(length()) < len) return false;
      break;
    case EqualityType::kNoLengthCheck:
      DCHECK_EQ(length(), len);
      break;
  }

  DisallowGarbageCollection no_gc;

  int slice_offset = 0;
  Tagged<String> string = this;
  const Char* data = str.data();
  while (true) {
    int32_t type = string->map()->instance_type();
    switch (type & kStringRepresentationAndEncodingMask) {
      case kSeqOneByteStringTag:
        return CompareCharsEqual(
            Cast<SeqOneByteString>(string)->GetChars(no_gc, access_guard) +
                slice_offset,
            data, len);
      case kSeqTwoByteStringTag:
        return CompareCharsEqual(
            Cast<SeqTwoByteString>(string)->GetChars(no_gc, access_guard) +
                slice_offset,
            data, len);
      case kExternalOneByteStringTag:
        return CompareCharsEqual(
            Cast<ExternalOneByteString>(string)->GetChars() + slice_offset,
            data, len);
      case kExternalTwoByteStringTag:
        return CompareCharsEqual(
            Cast<ExternalTwoByteString>(string)->GetChars() + slice_offset,
            data, len);

      case kSlicedStringTag | kOneByteStringTag:
      case kSlicedStringTag | kTwoByteStringTag: {
        Tagged<SlicedString> slicedString = Cast<SlicedString>(string);
        slice_offset += slicedString->offset();
        string = slicedString->parent();
        continue;
      }

      case kConsStringTag | kOneByteStringTag:
      case kConsStringTag | kTwoByteStringTag: {
        // The ConsString path is more complex and rare, so call out to an
        // out-of-line handler.
        // Slices cannot refer to ConsStrings, so there cannot be a non-zero
        // slice offset here.
        DCHECK_EQ(slice_offset, 0);
        return IsConsStringEqualToImpl<Char>(Cast<ConsString>(string), str,
                                             access_guard);
      }

      case kThinStringTag | kOneByteStringTag:
      case kThinStringTag | kTwoByteStringTag:
        string = Cast<ThinString>(string)->actual();
        continue;

      default:
        UNREACHABLE();
    }
  }
}

// static
template <typename Char>
bool String::IsConsStringEqualToImpl(
    Tagged<ConsString> string, base::Vector<const Char> str,
    const SharedStringAccessGuardIfNeeded& access_guard) {
  // Already checked the len in IsEqualToImpl. Check GE rather than EQ in case
  // this is a prefix check.
  DCHECK_GE(string->length(), str.size());

  ConsStringIterator iter(Cast<ConsString>(string));
  base::Vector<const Char> remaining_str = str;
  int offset;
  for (Tagged<String> segment = iter.Next(&offset); !segment.is_null();
       segment = iter.Next(&offset)) {
    // We create the iterator without an offset, so we should never have a
    // per-segment offset.
    DCHECK_EQ(offset, 0);
    // Compare the individual segment against the appropriate subvector of the
    // remaining string.
    size_t len = std::min<size_t>(segment->length(), remaining_str.size());
    base::Vector<const Char> sub_str = remaining_str.SubVector(0, len);
    if (!segment->IsEqualToImpl<EqualityType::kNoLengthCheck>(sub_str,
                                                              access_guard)) {
      return false;
    }
    remaining_str += len;
    if (remaining_str.empty()) break;
  }
  DCHECK_EQ(remaining_str.data(), str.end());
  DCHECK_EQ(remaining_str.size(), 0);
  return true;
}

bool String::IsOneByteEqualTo(base::Vector<const char> str) {
  return IsEqualTo(str);
}

template <typename Char>
const Char* String::GetDirectStringChars(
    const DisallowGarbageCollection& no_gc) const {
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(this));
  DCHECK(StringShape(this).IsDirect());
  return StringShape(this).IsExternal()
             ? Cast<typename CharTraits<Char>::ExternalString>(this).GetChars()
             : Cast<typename CharTraits<Char>::String>(this).GetChars(no_gc);
}

template <typename Char>
const Char* String::GetDirectStringChars(
    const DisallowGarbageCollection& no_gc,
    const SharedStringAccessGuardIfNeeded& access_guard) const {
  DCHECK(StringShape(this).IsDirect());
  return StringShape(this).IsExternal()
             ? Cast<typename CharTraits<Char>::ExternalString>(this)->GetChars()
             : Cast<typename CharTraits<Char>::String>(this)->GetChars(
                   no_gc, access_guard);
}

// Note this function is reimplemented by StringSlowFlatten in string.tq.
// Keep them in sync.
// Note: This is an inline method template and exporting it for windows
// component builds works only without the EXPORT_TEMPLATE_DECLARE macro.
//
// static
template <template <typename> typename HandleType>
  requires(std::is_convertible_v<HandleType<String>, DirectHandle<String>>)
V8_EXPORT_PRIVATE HandleType<String> String::SlowFlatten(
    Isolate* isolate, HandleType<ConsString> cons, AllocationType allocation) {
  DCHECK(!cons->IsFlat());
  DCHECK_NE(cons->second()->length(), 0);  // Equivalent to !IsFlat.
  DCHECK(!HeapLayout::InAnySharedSpace(*cons));

  bool is_one_byte_representation;
  uint32_t length;

  {
    DisallowGarbageCollection no_gc;
    Tagged<ConsString> raw_cons = *cons;

    // TurboFan can create cons strings with empty first parts. Make sure the
    // cons shape is canonicalized by the end of this function (either here, if
    // returning early, or below). Note this case is very rare in practice.
    if (V8_UNLIKELY(raw_cons->first()->length() == 0)) {
      Tagged<String> second = raw_cons->second();
      if (StringShape{second}.IsSequential()) {
        raw_cons->set_first(second);
        raw_cons->set_second(ReadOnlyRoots(isolate).empty_string());
        DCHECK(raw_cons->IsFlat());
        return HandleType<String>(second, isolate);
      }
      // Note that the remaining subtree may still be non-flat and we thus
      // need to continue below.
    }

    if (V8_LIKELY(allocation != AllocationType::kSharedOld)) {
      if (!HeapLayout::InYoungGeneration(raw_cons)) {
        allocation = AllocationType::kOld;
      }
    }
    length = raw_cons->length();
    is_one_byte_representation = cons->IsOneByteRepresentation();
  }

  DCHECK_EQ(length, cons->length());
  DCHECK_EQ(is_one_byte_representation, cons->IsOneByteRepresentation());
  DCHECK(AllowGarbageCollection::IsAllowed());

  HandleType<SeqString> result;
  if (is_one_byte_representation) {
    HandleType<SeqOneByteString> flat =
        isolate->factory()
            ->NewRawOneByteString(length, allocation)
            .ToHandleChecked();
    // When the ConsString had a forwarding index, it is possible that it was
    // transitioned to a ThinString (and eventually shortcutted to
    // InternalizedString) during GC.
    if constexpr (v8_flags.always_use_string_forwarding_table.value()) {
      if (!IsConsString(*cons)) {
        DCHECK(IsInternalizedString(*cons) || IsThinString(*cons));
        return String::Flatten(isolate, cons, allocation);
      }
    }
    DisallowGarbageCollection no_gc;
    Tagged<ConsString> raw_cons = *cons;
    WriteToFlat2(flat->GetChars(no_gc), raw_cons, 0, length,
                 SharedStringAccessGuardIfNeeded::NotNeeded(), no_gc);
    raw_cons->set_first(*flat);
    raw_cons->set_second(ReadOnlyRoots(isolate).empty_string());
    result = flat;
  } else {
    HandleType<SeqTwoByteString> flat =
        isolate->factory()
            ->NewRawTwoByteString(length, allocation)
            .ToHandleChecked();
    // When the ConsString had a forwarding index, it is possible that it was
    // transitioned to a ThinString (and eventually shortcutted to
    // InternalizedString) during GC.
    if constexpr (v8_flags.always_use_string_forwarding_table.value()) {
      if (!IsConsString(*cons)) {
        DCHECK(IsInternalizedString(*cons) || IsThinString(*cons));
        return String::Flatten(isolate, cons, allocation);
      }
    }
    DisallowGarbageCollection no_gc;
    Tagged<ConsString> raw_cons = *cons;
    WriteToFlat2(flat->GetChars(no_gc), raw_cons, 0, length,
                 SharedStringAccessGuardIfNeeded::NotNeeded(), no_gc);
    raw_cons->set_first(*flat);
    raw_cons->set_second(ReadOnlyRoots(isolate).empty_string());
    result = flat;
  }
  DCHECK(result->IsFlat());
  DCHECK(cons->IsFlat());
  return result;
}

// Note that RegExpExecInternal currently relies on this to in-place flatten
// the input `string`.
// static
template <typename T, template <typename> typename HandleType>
  requires(std::is_convertible_v<HandleType<T>, DirectHandle<String>>)
HandleType<String> String::Flatten(Isolate* isolate, HandleType<T> string,
                                   AllocationType allocation) {
  DisallowGarbageCollection no_gc;  // Unhandlified code.
  Tagged<String> s = *string;
  StringShape shape(s);

  // Shortcut already-flat strings.
  if (V8_LIKELY(shape.IsDirect())) return string;

  if (shape.IsCons()) {
    DCHECK(!HeapLayout::InAnySharedSpace(s));
    Tagged<ConsString> cons = Cast<ConsString>(s);
    if (!cons->IsFlat()) {
      AllowGarbageCollection yes_gc;
      DCHECK_EQ(*string, s);
      HandleType<String> result =
          SlowFlatten(isolate, Cast<ConsString>(string), allocation);
      DCHECK(result->IsFlat());
      DCHECK(string->IsFlat());  // In-place flattened.
      return result;
    }
    s = cons->first();
    shape = StringShape(s);
  }

  if (shape.IsThin()) {
    s = Cast<ThinString>(s)->actual();
    DCHECK(!IsConsString(s));
  }

  DCHECK(s->IsFlat());
  DCHECK(string->IsFlat());  // In-place flattened.
  return HandleType<String>(s, isolate);
}

// static
template <typename T, template <typename> typename HandleType>
  requires(std::is_convertible_v<HandleType<T>, DirectHandle<String>>)
HandleType<String> String::Flatten(LocalIsolate* isolate, HandleType<T> string,
                                   AllocationType allocation) {
  // We should never pass non-flat strings to String::Flatten when off-thread.
  DCHECK(string->IsFlat());
  return string;
}

// static
std::optional<String::FlatContent> String::TryGetFlatContentFromDirectString(
    const DisallowGarbageCollection& no_gc, Tagged<String> string,
    uint32_t offset, uint32_t length,
    const SharedStringAccessGuardIfNeeded& access_guard) {
  DCHECK_LE(offset + length, string->length());
  switch (StringShape{string}.representation_and_encoding_tag()) {
    case kSeqOneByteStringTag:
      return FlatContent(
          Cast<SeqOneByteString>(string)->GetChars(no_gc, access_guard) +
              offset,
          length, no_gc);
    case kSeqTwoByteStringTag:
      return FlatContent(
          Cast<SeqTwoByteString>(string)->GetChars(no_gc, access_guard) +
              offset,
          length, no_gc);
    case kExternalOneByteStringTag:
      return FlatContent(
          Cast<ExternalOneByteString>(string)->GetChars() + offset, length,
          no_gc);
    case kExternalTwoByteStringTag:
      return FlatContent(
          Cast<ExternalTwoByteString>(string)->GetChars() + offset, length,
          no_gc);
    default:
      return {};
  }
  UNREACHABLE();
}

String::FlatContent String::GetFlatContent(
    const DisallowGarbageCollection& no_gc) {
  return GetFlatContent(no_gc, SharedStringAccessGuardIfNeeded::NotNeeded());
}

String::FlatContent::FlatContent(const uint8_t* start, uint32_t length,
                                 const DisallowGarbageCollection& no_gc)
    : onebyte_start(start), length_(length), state_(ONE_BYTE), no_gc_(no_gc) {
#ifdef ENABLE_SLOW_DCHECKS
  checksum_ = ComputeChecksum();
#endif
}

String::FlatContent::FlatContent(const base::uc16* start, uint32_t length,
                                 const DisallowGarbageCollection& no_gc)
    : twobyte_start(start), length_(length), state_(TWO_BYTE), no_gc_(no_gc) {
#ifdef ENABLE_SLOW_DCHECKS
  checksum_ = ComputeChecksum();
#endif
}

String::FlatContent::~FlatContent() {
  // When ENABLE_SLOW_DCHECKS, check the string contents did not change during
  // the lifetime of the FlatContent. To avoid extra memory use, only the hash
  // is checked instead of snapshotting the full character data.
  //
  // If you crashed here, it means something changed the character data of this
  // FlatContent during its lifetime (e.g. GC relocated the string). This is
  // almost always a bug. If you are certain it is not a bug, you can disable
  // the checksum verification in the caller by calling
  // UnsafeDisableChecksumVerification().
  SLOW_DCHECK(checksum_ == kChecksumVerificationDisabled ||
              checksum_ == ComputeChecksum());
}

#ifdef ENABLE_SLOW_DCHECKS
uint32_t String::FlatContent::ComputeChecksum() const {
  constexpr uint64_t hashseed = 1;
  uint32_t hash;
  if (state_ == ONE_BYTE) {
    hash = StringHasher::HashSequentialString(onebyte_start, length_, hashseed);
  } else {
    DCHECK_EQ(TWO_BYTE, state_);
    hash = StringHasher::HashSequentialString(twobyte_start, length_, hashseed);
  }
  DCHECK_NE(kChecksumVerificationDisabled, hash);
  return hash;
}
#endif

String::FlatContent String::GetFlatContent(
    const DisallowGarbageCollection& no_gc,
    const SharedStringAccessGuardIfNeeded& access_guard) {
  std::optional<FlatContent> flat_content =
      TryGetFlatContentFromDirectString(no_gc, this, 0, length(), access_guard);
  if (flat_content.has_value()) return flat_content.value();
  return SlowGetFlatContent(no_gc, access_guard);
}

template <typename T, template <typename> typename HandleType>
  requires(std::is_convertible_v<HandleType<T>, DirectHandle<String>>)
HandleType<String> String::Share(Isolate* isolate, HandleType<T> string) {
  DCHECK(v8_flags.shared_string_table);
  MaybeDirectHandle<Map> new_map;
  switch (
      isolate->factory()->ComputeSharingStrategyForString(string, &new_map)) {
    case StringTransitionStrategy::kCopy:
      return SlowShare(isolate, string);
    case StringTransitionStrategy::kInPlace:
      // A relaxed write is sufficient here, because at this point the string
      // has not yet escaped the current thread.
      DCHECK(HeapLayout::InAnySharedSpace(*string));
      string->set_map_no_write_barrier(isolate, *new_map.ToHandleChecked());
      return string;
    case StringTransitionStrategy::kAlreadyTransitioned:
      return string;
  }
}

uint16_t String::Get(uint32_t index) const {
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(this));
  return GetImpl(index, SharedStringAccessGuardIfNeeded::NotNeeded());
}

uint16_t String::Get(uint32_t index, Isolate* isolate) const {
  SharedStringAccessGuardIfNeeded scope(isolate);
  return GetImpl(index, scope);
}

uint16_t String::Get(uint32_t index, LocalIsolate* local_isolate) const {
  SharedStringAccessGuardIfNeeded scope(local_isolate);
  return GetImpl(index, scope);
}

uint16_t String::Get(
    uint32_t index, const SharedStringAccessGuardIfNeeded& access_guard) const {
  return GetImpl(index, access_guard);
}

uint16_t String::GetImpl(
    uint32_t index, const SharedStringAccessGuardIfNeeded& access_guard) const {
  DCHECK(index >= 0 && index < length());

  return DispatchToSpecificType(
      [&](auto str) { return str->Get(index, access_guard); });
}

void String::Set(uint32_t index, uint16_t value) {
  DCHECK(index >= 0 && index < length());
  DCHECK(StringShape(this).IsSequential());

  return IsOneByteRepresentation()
             ? Cast<SeqOneByteString>(this)->SeqOneByteStringSet(index, value)
             : Cast<SeqTwoByteString>(this)->SeqTwoByteStringSet(index, value);
}

bool String::IsFlat() const {
  if (!StringShape(this).IsCons()) return true;
  return Cast<ConsString>(this)->IsFlat();
}

bool String::IsShared() const {
  const bool result = StringShape(this).IsShared();
  DCHECK_IMPLIES(result, HeapLayout::InAnySharedSpace(this));
  return result;
}

Tagged<String> String::GetUnderlying() const {
  // Giving direct access to underlying string only makes sense if the
  // wrapping string is already flattened.
  DCHECK(IsFlat());
  DCHECK(StringShape(this).IsIndirect());
  static_assert(offsetof(ConsString, first_) ==
                offsetof(SlicedString, parent_));
  static_assert(offsetof(ConsString, first_) == offsetof(ThinString, actual_));

  return static_cast<const SlicedString*>(this)->parent();
}

template <class Visitor>
Tagged<ConsString> String::VisitFlat(Visitor* visitor, Tagged<String> string,
                                     const int offset) {
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(string));
  return VisitFlat(visitor, string, offset,
                   SharedStringAccessGuardIfNeeded::NotNeeded());
}

template <class Visitor>
Tagged<ConsString> String::VisitFlat(
    Visitor* visitor, Tagged<String> string, const int offset,
    const SharedStringAccessGuardIfNeeded& access_guard) {
  DisallowGarbageCollection no_gc;
  int slice_offset = offset;
  const uint32_t length = string->length();
  DCHECK_LE(offset, length);
  while (true) {
    int32_t tag = StringShape(string).representation_and_encoding_tag();
    switch (tag) {
      case kSeqOneByteStringTag:
        visitor->VisitOneByteString(
            Cast<SeqOneByteString>(string)->GetChars(no_gc, access_guard) +
                slice_offset,
            length - offset);
        return Tagged<ConsString>();

      case kSeqTwoByteStringTag:
        visitor->VisitTwoByteString(
            Cast<SeqTwoByteString>(string)->GetChars(no_gc, access_guard) +
                slice_offset,
            length - offset);
        return Tagged<ConsString>();

      case kExternalOneByteStringTag:
        visitor->VisitOneByteString(
            Cast<ExternalOneByteString>(string)->GetChars() + slice_offset,
            length - offset);
        return Tagged<ConsString>();

      case kExternalTwoByteStringTag:
        visitor->VisitTwoByteString(
            Cast<ExternalTwoByteString>(string)->GetChars() + slice_offset,
            length - offset);
        return Tagged<ConsString>();

      case kSlicedStringTag | kOneByteStringTag:
      case kSlicedStringTag | kTwoByteStringTag: {
        Tagged<SlicedString> slicedString = Cast<SlicedString>(string);
        slice_offset += slicedString->offset();
        string = slicedString->parent();
        continue;
      }

      case kConsStringTag | kOneByteStringTag:
      case kConsStringTag | kTwoByteStringTag:
        return Cast<ConsString>(string);

      case kThinStringTag | kOneByteStringTag:
      case kThinStringTag | kTwoByteStringTag:
        string = Cast<ThinString>(string)->actual();
        continue;

      default:
        UNREACHABLE();
    }
  }
}

// static
size_t String::Utf8Length(Isolate* isolate, DirectHandle<String> string) {
  string = Flatten(isolate, string);

  DisallowGarbageCollection no_gc;
  FlatContent content = string->GetFlatContent(no_gc);
  DCHECK(content.IsFlat());
  size_t utf8_length = 0;
  if (content.IsOneByte()) {
    for (uint8_t c : content.ToOneByteVector()) {
      utf8_length += unibrow::Utf8::LengthOneByte(c);
    }
  } else {
    uint16_t last_character = unibrow::Utf16::kNoPreviousCharacter;
    for (uint16_t c : content.ToUC16Vector()) {
      utf8_length += unibrow::Utf8::Length(c, last_character);
      last_character = c;
    }
  }
  return utf8_length;
}

bool String::IsWellFormedUnicode(Isolate* isolate,
                                 DirectHandle<String> string) {
  // One-byte strings are definitionally well formed and cannot have unpaired
  // surrogates.
  if (string->IsOneByteRepresentation()) return true;

  // TODO(v8:13557): The two-byte case can be optimized by extending the
  // InstanceType. See
  // https://docs.google.com/document/d/15f-1c_Ysw3lvjy_Gx0SmmD9qeO8UuXuAbWIpWCnTDO8/
  string = Flatten(isolate, string);
  if (string->IsOneByteRepresentation()) return true;
  DisallowGarbageCollection no_gc;
  String::FlatContent flat = string->GetFlatContent(no_gc);
  DCHECK(flat.IsFlat());
  const uint16_t* data = flat.ToUC16Vector().begin();
  return !unibrow::Utf16::HasUnpairedSurrogate(data, string->length());
}

template <>
inline base::Vector<const uint8_t> String::GetCharVector(
    const DisallowGarbageCollection& no_gc) {
  String::FlatContent flat = GetFlatContent(no_gc);
  DCHECK(flat.IsOneByte());
  return flat.ToOneByteVector();
}

template <>
inline base::Vector<const base::uc16> String::GetCharVector(
    const DisallowGarbageCollection& no_gc) {
  String::FlatContent flat = GetFlatContent(no_gc);
  DCHECK(flat.IsTwoByte());
  return flat.ToUC16Vector();
}

uint8_t SeqOneByteString::Get(uint32_t index) const {
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(this));
  return Get(index, SharedStringAccessGuardIfNeeded::NotNeeded());
}

uint8_t SeqOneByteString::Get(
    uint32_t index, const SharedStringAccessGuardIfNeeded& access_guard) const {
  USE(access_guard);
  DCHECK(index >= 0 && index < length());
  return chars()[index];
}

void SeqOneByteString::SeqOneByteStringSet(uint32_t index, uint16_t value) {
  DisallowGarbageCollection no_gc;
  DCHECK_GE(index, 0);
  DCHECK_LT(index, length());
  DCHECK_LE(value, kMaxOneByteCharCode);
  chars()[index] = value;
}

void SeqOneByteString::SeqOneByteStringSetChars(uint32_t index,
                                                const uint8_t* string,
                                                uint32_t string_length) {
  DisallowGarbageCollection no_gc;
  DCHECK_LT(index + string_length, length());
  void* address = static_cast<void*>(&chars()[index]);
  memcpy(address, string, string_length);
}

Address SeqOneByteString::GetCharsAddress() const {
  return reinterpret_cast<Address>(&chars()[0]);
}

uint8_t* SeqOneByteString::GetChars(const DisallowGarbageCollection& no_gc) {
  USE(no_gc);
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(this));
  return chars();
}

uint8_t* SeqOneByteString::GetChars(
    const DisallowGarbageCollection& no_gc,
    const SharedStringAccessGuardIfNeeded& access_guard) {
  USE(no_gc);
  USE(access_guard);
  return chars();
}

Address SeqTwoByteString::GetCharsAddress() const {
  return reinterpret_cast<Address>(&chars()[0]);
}

base::uc16* SeqTwoByteString::GetChars(const DisallowGarbageCollection& no_gc) {
  USE(no_gc);
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(this));
  return chars();
}

base::uc16* SeqTwoByteString::GetChars(
    const DisallowGarbageCollection& no_gc,
    const SharedStringAccessGuardIfNeeded& access_guard) {
  USE(no_gc);
  USE(access_guard);
  return chars();
}

uint16_t SeqTwoByteString::Get(
    uint32_t index, const SharedStringAccessGuardIfNeeded& access_guard) const {
  USE(access_guard);
  DCHECK(index >= 0 && index < length());
  return chars()[index];
}

void SeqTwoByteString::SeqTwoByteStringSet(uint32_t index, uint16_t value) {
  DisallowGarbageCollection no_gc;
  DCHECK(index >= 0 && index < length());
  chars()[index] = value;
}

// static
V8_INLINE constexpr int32_t SeqOneByteString::DataSizeFor(int32_t length) {
  return sizeof(SeqOneByteString) + length * sizeof(Char);
}

// static
V8_INLINE constexpr int32_t SeqTwoByteString::DataSizeFor(int32_t length) {
  return sizeof(SeqTwoByteString) + length * sizeof(Char);
}

// static
V8_INLINE constexpr int32_t SeqOneByteString::SizeFor(int32_t length) {
  return OBJECT_POINTER_ALIGN(SeqOneByteString::DataSizeFor(length));
}

// static
V8_INLINE constexpr int32_t SeqTwoByteString::SizeFor(int32_t length) {
  return OBJECT_POINTER_ALIGN(SeqTwoByteString::DataSizeFor(length));
}

// Due to ThinString rewriting, concurrent visitors need to read the length with
// acquire semantics.
inline int SeqOneByteString::AllocatedSize() const {
  return SizeFor(length(kAcquireLoad));
}
inline int SeqTwoByteString::AllocatedSize() const {
  return SizeFor(length(kAcquireLoad));
}

// static
bool SeqOneByteString::IsCompatibleMap(Tagged<Map> map, ReadOnlyRoots roots) {
  return map == roots.seq_one_byte_string_map() ||
         map == roots.shared_seq_one_byte_string_map();
}

// static
bool SeqTwoByteString::IsCompatibleMap(Tagged<Map> map, ReadOnlyRoots roots) {
  return map == roots.seq_two_byte_string_map() ||
         map == roots.shared_seq_two_byte_string_map();
}

inline Tagged<String> SlicedString::parent() const { return parent_.load(); }

void SlicedString::set_parent(Tagged<String> parent, WriteBarrierMode mode) {
  DCHECK(IsSeqString(parent) || IsExternalString(parent));
  parent_.store(this, parent, mode);
}

inline int32_t SlicedString::offset() const { return offset_.load().value(); }

void SlicedString::set_offset(int32_t value) {
  offset_.store(this, Smi::FromInt(value), SKIP_WRITE_BARRIER);
}

inline Tagged<String> ConsString::first() const { return first_.load(); }
inline void ConsString::set_first(Tagged<String> value, WriteBarrierMode mode) {
  first_.store(this, value, mode);
}

inline Tagged<String> ConsString::second() const { return second_.load(); }
inline void ConsString::set_second(Tagged<String> value,
                                   WriteBarrierMode mode) {
  second_.store(this, value, mode);
}

Tagged<Object> ConsString::unchecked_first() const { return first_.load(); }

Tagged<Object> ConsString::unchecked_second() const {
  return second_.Relaxed_Load();
}

bool ConsString::IsFlat() const { return second()->length() == 0; }

inline Tagged<String> ThinString::actual() const { return actual_.load(); }
inline void ThinString::set_actual(Tagged<String> value,
                                   WriteBarrierMode mode) {
  actual_.store(this, value, mode);
}

Tagged<HeapObject> ThinString::unchecked_actual() const {
  return actual_.load();
}

bool ExternalString::is_uncached() const {
  InstanceType type = map()->instance_type();
  return (type & kUncachedExternalStringMask) == kUncachedExternalStringTag;
}

void ExternalString::InitExternalPointerFields(Isolate* isolate) {
  resource_.Init(address(), isolate, kNullAddress);
  if (is_uncached()) return;
  resource_data_.Init(address(), isolate, kNullAddress);
}

void ExternalString::VisitExternalPointers(ObjectVisitor* visitor) {
  visitor->VisitExternalPointer(this, ExternalPointerSlot(&resource_));
  if (is_uncached()) return;
  visitor->VisitExternalPointer(this, ExternalPointerSlot(&resource_data_));
}

Address ExternalString::resource_as_address() const {
  IsolateForSandbox isolate = GetIsolateForSandbox(this);
  return resource_.load(isolate);
}

void ExternalString::set_address_as_resource(Isolate* isolate, Address value) {
  resource_.store(isolate, value);
  if (IsExternalOneByteString(this)) {
    Cast<ExternalOneByteString>(this)->update_data_cache(isolate);
  } else {
    Cast<ExternalTwoByteString>(this)->update_data_cache(isolate);
  }
}

uint32_t ExternalString::GetResourceRefForDeserialization() {
  return static_cast<uint32_t>(resource_.load_encoded());
}

void ExternalString::SetResourceRefForSerialization(uint32_t ref) {
  resource_.store_encoded(static_cast<ExternalPointer_t>(ref));
  if (is_uncached()) return;
  resource_data_.store_encoded(kNullExternalPointer);
}

void ExternalString::DisposeResource(Isolate* isolate) {
  Address value = resource_.load(isolate);
  v8::String::ExternalStringResourceBase* resource =
      reinterpret_cast<v8::String::ExternalStringResourceBase*>(value);

  // Dispose of the C++ object if it has not already been disposed.
  if (resource != nullptr) {
    if (!IsShared() && !HeapLayout::InWritableSharedSpace(this)) {
      resource->Unaccount(reinterpret_cast<v8::Isolate*>(isolate));
    }
    resource->Dispose();
    resource_.store(isolate, kNullAddress);
  }
}

const ExternalOneByteString::Resource* ExternalOneByteString::resource() const {
  return reinterpret_cast<const Resource*>(resource_as_address());
}

ExternalOneByteString::Resource* ExternalOneByteString::mutable_resource() {
  return reinterpret_cast<Resource*>(resource_as_address());
}

void ExternalOneByteString::update_data_cache(Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  if (is_uncached()) {
    if (resource()->IsCacheable()) mutable_resource()->UpdateDataCache();
  } else {
    resource_data_.store(isolate,
                         reinterpret_cast<Address>(resource()->data()));
  }
}

void ExternalOneByteString::SetResource(
    Isolate* isolate, const ExternalOneByteString::Resource* resource) {
  set_resource(isolate, resource);
  size_t new_payload = resource == nullptr ? 0 : resource->length();
  if (new_payload > 0) {
    isolate->heap()->UpdateExternalString(this, 0, new_payload);
  }
}

void ExternalOneByteString::set_resource(
    Isolate* isolate, const ExternalOneByteString::Resource* resource) {
  resource_.store(isolate, reinterpret_cast<Address>(resource));
  if (resource != nullptr) update_data_cache(isolate);
}

const uint8_t* ExternalOneByteString::GetChars() const {
  DisallowGarbageCollection no_gc;
  auto res = resource();
  if (is_uncached()) {
    if (res->IsCacheable()) {
      // TODO(solanes): Teach TurboFan/CSA to not bailout to the runtime to
      // avoid this call.
      return reinterpret_cast<const uint8_t*>(res->cached_data());
    }
#if DEBUG
    // Check that this method is called only from the main thread if we have an
    // uncached string with an uncacheable resource.
    {
      Isolate* isolate;
      DCHECK_IMPLIES(GetIsolateFromHeapObject(this, &isolate),
                     ThreadId::Current() == isolate->thread_id());
    }
#endif
  }

  return reinterpret_cast<const uint8_t*>(res->data());
}

uint8_t ExternalOneByteString::Get(
    uint32_t index, const SharedStringAccessGuardIfNeeded& access_guard) const {
  USE(access_guard);
  DCHECK(index >= 0 && index < length());
  return GetChars()[index];
}

const ExternalTwoByteString::Resource* ExternalTwoByteString::resource() const {
  return reinterpret_cast<const Resource*>(resource_as_address());
}

ExternalTwoByteString::Resource* ExternalTwoByteString::mutable_resource() {
  return reinterpret_cast<Resource*>(resource_as_address());
}

void ExternalTwoByteString::update_data_cache(Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  if (is_uncached()) {
    if (resource()->IsCacheable()) mutable_resource()->UpdateDataCache();
  } else {
    resource_data_.store(isolate,
                         reinterpret_cast<Address>(resource()->data()));
  }
}

void ExternalTwoByteString::SetResource(
    Isolate* isolate, const ExternalTwoByteString::Resource* resource) {
  set_resource(isolate, resource);
  size_t new_payload = resource == nullptr ? 0 : resource->length() * 2;
  if (new_payload > 0) {
    isolate->heap()->UpdateExternalString(this, 0, new_payload);
  }
}

void ExternalTwoByteString::set_resource(
    Isolate* isolate, const ExternalTwoByteString::Resource* resource) {
  resource_.store(isolate, reinterpret_cast<Address>(resource));
  if (resource != nullptr) update_data_cache(isolate);
}

const uint16_t* ExternalTwoByteString::GetChars() const {
  DisallowGarbageCollection no_gc;
  auto res = resource();
  if (is_uncached()) {
    if (res->IsCacheable()) {
      // TODO(solanes): Teach TurboFan/CSA to not bailout to the runtime to
      // avoid this call.
      return res->cached_data();
    }
#if DEBUG
    // Check that this method is called only from the main thread if we have an
    // uncached string with an uncacheable resource.
    {
      Isolate* isolate;
      DCHECK_IMPLIES(GetIsolateFromHeapObject(this, &isolate),
                     ThreadId::Current() == isolate->thread_id());
    }
#endif
  }

  return res->data();
}

uint16_t ExternalTwoByteString::Get(
    uint32_t index, const SharedStringAccessGuardIfNeeded& access_guard) const {
  USE(access_guard);
  DCHECK(index >= 0 && index < length());
  return GetChars()[index];
}

const uint16_t* ExternalTwoByteString::ExternalTwoByteStringGetData(
    uint32_t start) {
  return GetChars() + start;
}

int ConsStringIterator::OffsetForDepth(int depth) { return depth & kDepthMask; }

void ConsStringIterator::PushLeft(Tagged<ConsString> string) {
  frames_[depth_++ & kDepthMask] = string;
}

void ConsStringIterator::PushRight(Tagged<ConsString> string) {
  // Inplace update.
  frames_[(depth_ - 1) & kDepthMask] = string;
}

void ConsStringIterator::AdjustMaximumDepth() {
  if (depth_ > maximum_depth_) maximum_depth_ = depth_;
}

void ConsStringIterator::Pop() {
  DCHECK_GT(depth_, 0);
  DCHECK(depth_ <= maximum_depth_);
  depth_--;
}

class StringCharacterStream {
 public:
  inline explicit StringCharacterStream(Tagged<String> string, int offset = 0);
  StringCharacterStream(const StringCharacterStream&) = delete;
  StringCharacterStream& operator=(const StringCharacterStream&) = delete;
  inline uint16_t GetNext();
  inline bool HasMore();
  inline void Reset(Tagged<String> string, int offset = 0);
  inline void VisitOneByteString(const uint8_t* chars, int length);
  inline void VisitTwoByteString(const uint16_t* chars, int length);

 private:
  ConsStringIterator iter_;
  bool is_one_byte_;
  union {
    const uint8_t* buffer8_;
    const uint16_t* buffer16_;
  };
  const uint8_t* end_;
  SharedStringAccessGuardIfNeeded access_guard_;
};

uint16_t StringCharacterStream::GetNext() {
  DCHECK(buffer8_ != nullptr && end_ != nullptr);
  // Advance cursor if needed.
  if (buffer8_ == end_) HasMore();
  DCHECK(buffer8_ < end_);
  return is_one_byte_ ? *buffer8_++ : *buffer16_++;
}

// TODO(solanes, v8:7790, chromium:1166095): Assess if we need to use
// Isolate/LocalIsolate and pipe them through, instead of using the slow
// version of the SharedStringAccessGuardIfNeeded.
StringCharacterStream::StringCharacterStream(Tagged<String> string, int offset)
    : is_one_byte_(false), access_guard_(string) {
  Reset(string, offset);
}

void StringCharacterStream::Reset(Tagged<String> string, int offset) {
  buffer8_ = nullptr;
  end_ = nullptr;

  Tagged<ConsString> cons_string =
      String::VisitFlat(this, string, offset, access_guard_);
  iter_.Reset(cons_string, offset);
  if (!cons_string.is_null()) {
    string = iter_.Next(&offset);
    if (!string.is_null())
      String::VisitFlat(this, string, offset, access_guard_);
  }
}

bool StringCharacterStream::HasMore() {
  if (buffer8_ != end_) return true;
  int offset;
  Tagged<String> string = iter_.Next(&offset);
  DCHECK_EQ(offset, 0);
  if (string.is_null()) return false;
  String::VisitFlat(this, string, 0, access_guard_);
  DCHECK(buffer8_ != end_);
  return true;
}

void StringCharacterStream::VisitOneByteString(const uint8_t* chars,
                                               int length) {
  is_one_byte_ = true;
  buffer8_ = chars;
  end_ = chars + length;
}

void StringCharacterStream::VisitTwoByteString(const uint16_t* chars,
                                               int length) {
  is_one_byte_ = false;
  buffer16_ = chars;
  end_ = reinterpret_cast<const uint8_t*>(chars + length);
}

bool String::AsArrayIndex(uint32_t* index) {
  DisallowGarbageCollection no_gc;
  uint32_t field = raw_hash_field();
  if (ContainsCachedArrayIndex(field)) {
    *index = ArrayIndexValueBits::decode(field);
    return true;
  }
  if (IsHashFieldComputed(field) && !IsIntegerIndex(field)) {
    return false;
  }
  return SlowAsArrayIndex(index);
}

bool String::AsIntegerIndex(size_t* index) {
  uint32_t field = raw_hash_field();
  if (ContainsCachedArrayIndex(field)) {
    *index = ArrayIndexValueBits::decode(field);
    return true;
  }
  if (IsHashFieldComputed(field) && !IsIntegerIndex(field)) {
    return false;
  }
  return SlowAsIntegerIndex(index);
}

SubStringRange::SubStringRange(Tagged<String> string,
                               const DisallowGarbageCollection& no_gc,
                               int first, int length)
    : string_(string),
      first_(first),
      length_(length == -1 ? string->length() : length),
      no_gc_(no_gc) {}

class SubStringRange::iterator final {
 public:
  using iterator_category = std::forward_iterator_tag;
  using difference_type = int;
  using value_type = base::uc16;
  using pointer = base::uc16*;
  using reference = base::uc16&;

  iterator(const iterator& other) = default;

  base::uc16 operator*() { return content_.Get(offset_); }
  bool operator==(const iterator& other) const {
    return content_.UsesSameString(other.content_) && offset_ == other.offset_;
  }
  bool operator!=(const iterator& other) const {
    return !content_.UsesSameString(other.content_) || offset_ != other.offset_;
  }
  iterator& operator++() {
    ++offset_;
    return *this;
  }
  iterator operator++(int);

 private:
  friend class String;
  friend class SubStringRange;
  iterator(Tagged<String> from, int offset,
           const DisallowGarbageCollection& no_gc)
      : content_(from->GetFlatContent(no_gc)), offset_(offset) {}
  String::FlatContent content_;
  int offset_;
};

SubStringRange::iterator SubStringRange::begin() {
  return SubStringRange::iterator(string_, first_, no_gc_);
}

SubStringRange::iterator SubStringRange::end() {
  return SubStringRange::iterator(string_, first_ + length_, no_gc_);
}

void SeqOneByteString::clear_padding_destructively(uint32_t length) {
  // Ensure we are not killing the map word, which is already set at this point
  static_assert(SizeFor(0) >= kObjectAlignment + kTaggedSize);
  memset(reinterpret_cast<void*>(reinterpret_cast<char*>(this) +
                                 SizeFor(length) - kObjectAlignment),
         0, kObjectAlignment);
}

void SeqTwoByteString::clear_padding_destructively(uint32_t length) {
  // Ensure we are not killing the map word, which is already set at this point
  static_assert(SizeFor(0) >= kObjectAlignment + kTaggedSize);
  memset(reinterpret_cast<void*>(reinterpret_cast<char*>(this) +
                                 SizeFor(length) - kObjectAlignment),
         0, kObjectAlignment);
}

// static
bool String::IsInPlaceInternalizable(Tagged<String> string) {
  return IsInPlaceInternalizable(string->map()->instance_type());
}

// static
bool String::IsInPlaceInternalizable(InstanceType instance_type) {
  switch (instance_type) {
    case SEQ_TWO_BYTE_STRING_TYPE:
    case SEQ_ONE_BYTE_STRING_TYPE:
    case SHARED_SEQ_TWO_BYTE_STRING_TYPE:
    case SHARED_SEQ_ONE_BYTE_STRING_TYPE:
    case EXTERNAL_TWO_BYTE_STRING_TYPE:
    case EXTERNAL_ONE_BYTE_STRING_TYPE:
    case SHARED_EXTERNAL_TWO_BYTE_STRING_TYPE:
    case SHARED_EXTERNAL_ONE_BYTE_STRING_TYPE:
      return true;
    default:
      return false;
  }
}

// static
bool String::IsInPlaceInternalizableExcludingExternal(
    InstanceType instance_type) {
  return IsInPlaceInternalizable(instance_type) &&
         !InstanceTypeChecker::IsExternalString(instance_type);
}

class SeqOneByteString::BodyDescriptor final : public DataOnlyBodyDescriptor {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<SeqOneByteString>(raw_object)->AllocatedSize();
  }
};

class SeqTwoByteString::BodyDescriptor final : public DataOnlyBodyDescriptor {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<SeqTwoByteString>(raw_object)->AllocatedSize();
  }
};

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_STRING_INL_H_

// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/json/json-stringifier.h"

#include <string_view>

#include "hwy/highway.h"
#include "src/base/strings.h"
#include "src/common/assert-scope.h"
#include "src/common/message-template.h"
#include "src/execution/protectors-inl.h"
#include "src/numbers/conversions.h"
#include "src/objects/elements-kind.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-raw-json-inl.h"
#include "src/objects/lookup.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball-inl.h"
#include "src/objects/ordered-hash-table.h"
#include "src/objects/smi.h"
#include "src/objects/tagged.h"
#include "src/strings/string-builder-inl.h"

namespace v8 {
namespace internal {

static constexpr char kJsonStringifierZoneName[] = "json-stringifier-zone";

class JsonStringifier {
 public:
  explicit JsonStringifier(Isolate* isolate);

  ~JsonStringifier() {
    if (one_byte_ptr_ != one_byte_array_) delete[] one_byte_ptr_;
    if (two_byte_ptr_) delete[] two_byte_ptr_;
    DeleteArray(gap_);
  }

  V8_WARN_UNUSED_RESULT MaybeDirectHandle<Object> Stringify(
      Handle<JSAny> object, Handle<JSAny> replacer, Handle<Object> gap);

 private:
  enum Result { UNCHANGED, SUCCESS, EXCEPTION, NEED_STACK };

  bool InitializeReplacer(Handle<JSAny> replacer);
  bool InitializeGap(Handle<Object> gap);

  V8_WARN_UNUSED_RESULT MaybeHandle<JSAny> ApplyToJsonFunction(
      Handle<JSAny> object, DirectHandle<Object> key);
  V8_WARN_UNUSED_RESULT MaybeHandle<JSAny> ApplyReplacerFunction(
      Handle<JSAny> value, DirectHandle<Object> key,
      DirectHandle<Object> initial_holder);

  // Entry point to serialize the object.
  V8_INLINE Result SerializeObject(Handle<JSAny> obj) {
    return Serialize_<false>(obj, false, factory()->empty_string());
  }

  // Serialize an array element.
  // The index may serve as argument for the toJSON function.
  V8_INLINE Result SerializeElement(Isolate* isolate, Handle<JSAny> object,
                                    int i) {
    return Serialize_<false>(object, false,
                             Handle<Object>(Smi::FromInt(i), isolate));
  }

  // Serialize an object property.
  // The key may or may not be serialized depending on the property.
  // The key may also serve as argument for the toJSON function.
  V8_INLINE Result SerializeProperty(Handle<JSAny> object, bool deferred_comma,
                                     Handle<String> deferred_key) {
    DCHECK(!deferred_key.is_null());
    return Serialize_<true>(object, deferred_comma, deferred_key);
  }

  template <typename SrcChar, typename DestChar>
  V8_INLINE void Append(SrcChar c) {
    DCHECK_EQ(encoding_ == String::ONE_BYTE_ENCODING, sizeof(DestChar) == 1);
    if (sizeof(DestChar) == 1) {
      DCHECK_EQ(String::ONE_BYTE_ENCODING, encoding_);
      one_byte_ptr_[current_index_++] = c;
    } else {
      DCHECK_EQ(String::TWO_BYTE_ENCODING, encoding_);
      // Make sure to use unsigned extension even when SrcChar == char.
      two_byte_ptr_[current_index_++] =
          static_cast<std::make_unsigned_t<SrcChar>>(c);
    }
    if V8_UNLIKELY (current_index_ == part_length_) Extend();
  }

  V8_INLINE void AppendCharacter(uint8_t c) {
    if (encoding_ == String::ONE_BYTE_ENCODING) {
      Append<uint8_t, uint8_t>(c);
    } else {
      Append<uint8_t, base::uc16>(c);
    }
  }

  template <int N>
  V8_INLINE void AppendCStringLiteral(const char (&literal)[N]) {
    // Note that the literal contains the zero char.
    const int length = N - 1;
    static_assert(length > 0);
    if (length == 1) return AppendCharacter(literal[0]);
    if (encoding_ == String::ONE_BYTE_ENCODING && CurrentPartCanFit(N)) {
      const uint8_t* chars = reinterpret_cast<const uint8_t*>(literal);
      CopyChars<uint8_t, uint8_t>(one_byte_ptr_ + current_index_, chars,
                                  length);
      current_index_ += length;
      if (current_index_ == part_length_) Extend();
      DCHECK(HasValidCurrentIndex());
      return;
    }
    return AppendCString(literal);
  }

  template <typename SrcChar>
  V8_INLINE void AppendCString(const SrcChar* s) {
    if (encoding_ == String::ONE_BYTE_ENCODING) {
      while (*s != '\0') Append<SrcChar, uint8_t>(*s++);
    } else {
      while (*s != '\0') Append<SrcChar, base::uc16>(*s++);
    }
  }

  template <typename SrcChar>
  V8_INLINE void AppendString(std::basic_string_view<SrcChar> s) {
    if (encoding_ == String::ONE_BYTE_ENCODING) {
      for (SrcChar c : s) Append<SrcChar, uint8_t>(c);
    } else {
      for (SrcChar c : s) Append<SrcChar, base::uc16>(c);
    }
  }

  V8_INLINE bool CurrentPartCanFit(size_t length) {
    return part_length_ - current_index_ > length;
  }

  // We make a rough estimate to find out if the current string can be
  // serialized without allocating a new string part. The worst case length of
  // an escaped character is 6. Shifting the remaining string length right by 3
  // is a more pessimistic estimate, but faster to calculate.
  V8_INLINE bool EscapedLengthIfCurrentPartFits(size_t length) {
    if (length > kMaxPartLength) return false;
    static_assert(kMaxPartLength <= (String::kMaxLength >> 3));
    // This shift will not overflow because length is already less than the
    // maximum part length.
    return CurrentPartCanFit(length << 3);
  }

  void AppendStringByCopy(Tagged<String> string, size_t length,
                          const DisallowGarbageCollection& no_gc) {
    DCHECK_EQ(length, string->length());
    DCHECK(encoding_ == String::TWO_BYTE_ENCODING ||
           (string->IsFlat() && string->IsOneByteRepresentation()));
    DCHECK(CurrentPartCanFit(length + 1));
    String::FlatContent flat = string->GetFlatContent(no_gc);
    if (encoding_ == String::ONE_BYTE_ENCODING) {
      if (flat.IsOneByte()) {
        CopyChars<uint8_t, uint8_t>(one_byte_ptr_ + current_index_,
                                    flat.ToOneByteVector().begin(), length);
      } else {
        ChangeEncoding();
        CopyChars<uint16_t, uint16_t>(two_byte_ptr_ + current_index_,
                                      flat.ToUC16Vector().begin(), length);
      }
    } else {
      if (flat.IsOneByte()) {
        CopyChars<uint8_t, uint16_t>(two_byte_ptr_ + current_index_,
                                     flat.ToOneByteVector().begin(), length);
      } else {
        CopyChars<uint16_t, uint16_t>(two_byte_ptr_ + current_index_,
                                      flat.ToUC16Vector().begin(), length);
      }
    }
    current_index_ += length;
    DCHECK(current_index_ <= part_length_);
  }

  V8_NOINLINE void AppendString(Handle<String> string_handle) {
    {
      DisallowGarbageCollection no_gc;
      Tagged<String> string = *string_handle;
      const bool representation_ok =
          encoding_ == String::TWO_BYTE_ENCODING ||
          (string->IsFlat() && string->IsOneByteRepresentation());
      if (representation_ok) {
        size_t length = string->length();
        while (!CurrentPartCanFit(length + 1)) {
          Extend();
          if (V8_UNLIKELY(overflowed_)) return;
        }
        AppendStringByCopy(string, length, no_gc);
        return;
      }
    }
    SerializeString<true>(string_handle);
  }

  template <typename SrcChar>
  void AppendSubstringByCopy(const SrcChar* src, size_t count) {
    DCHECK(CurrentPartCanFit(count + 1));
    if (encoding_ == String::ONE_BYTE_ENCODING) {
      if (sizeof(SrcChar) == 1) {
        CopyChars<SrcChar, uint8_t>(one_byte_ptr_ + current_index_, src, count);
      } else {
        ChangeEncoding();
        CopyChars<SrcChar, base::uc16>(two_byte_ptr_ + current_index_, src,
                                       count);
      }
    } else {
      CopyChars<SrcChar, base::uc16>(two_byte_ptr_ + current_index_, src,
                                     count);
    }
    current_index_ += count;
    DCHECK_LE(current_index_, part_length_);
  }

  template <typename SrcChar>
  V8_NOINLINE void AppendSubstring(const SrcChar* src, size_t from, size_t to) {
    if (from == to) return;
    DCHECK_LT(from, to);
    size_t count = to - from;
    while (!CurrentPartCanFit(count + 1)) {
      Extend();
      if (V8_UNLIKELY(overflowed_)) return;
    }
    AppendSubstringByCopy(src + from, count);
  }

  bool HasValidCurrentIndex() const { return current_index_ < part_length_; }

  template <bool deferred_string_key>
  Result Serialize_(Handle<JSAny> object, bool comma, Handle<Object> key);

  V8_INLINE void SerializeDeferredKey(bool deferred_comma,
                                      Handle<Object> deferred_key);

  Result SerializeSmi(Tagged<Smi> object);

  Result SerializeDouble(double number);
  V8_INLINE Result SerializeHeapNumber(DirectHandle<HeapNumber> object) {
    return SerializeDouble(object->value());
  }

  Result SerializeJSPrimitiveWrapper(Handle<JSPrimitiveWrapper> object,
                                     Handle<Object> key);

  V8_INLINE Result SerializeJSArray(Handle<JSArray> object, Handle<Object> key);
  V8_INLINE Result SerializeJSObject(Handle<JSObject> object,
                                     Handle<Object> key);

  Result SerializeJSProxy(Handle<JSProxy> object, Handle<Object> key);
  Result SerializeJSReceiverSlow(DirectHandle<JSReceiver> object);
  template <ElementsKind kind>
  V8_INLINE Result SerializeFixedArrayWithInterruptCheck(
      DirectHandle<JSArray> array, uint32_t length, uint32_t* slow_path_index);
  template <ElementsKind kind>
  V8_INLINE Result SerializeFixedArrayWithPossibleTransitions(
      DirectHandle<JSArray> array, uint32_t length, uint32_t* slow_path_index);
  template <ElementsKind kind, typename T>
  V8_INLINE Result SerializeFixedArrayElement(Tagged<T> elements, uint32_t i,
                                              Tagged<JSArray> array,
                                              bool can_treat_hole_as_undefined);
  Result SerializeArrayLikeSlow(DirectHandle<JSReceiver> object, uint32_t start,
                                uint32_t length);

  // Returns whether any escape sequences were used.
  template <bool raw_json>
  bool SerializeString(Handle<String> object);

  template <typename DestChar>
  class NoExtendBuilder {
   public:
    NoExtendBuilder(DestChar* start, size_t* current_index)
        : current_index_(current_index), start_(start), cursor_(start) {}
    ~NoExtendBuilder() { *current_index_ += cursor_ - start_; }

    V8_INLINE void Append(DestChar c) { *(cursor_++) = c; }
    V8_INLINE void AppendCString(const char* s) {
      const uint8_t* u = reinterpret_cast<const uint8_t*>(s);
      while (*u != '\0') Append(*(u++));
    }
    V8_INLINE void AppendString(std::string_view str) {
      AppendChars(
          base::Vector<const uint8_t>(
              reinterpret_cast<const uint8_t*>(str.data()), str.length()),
          str.length());
    }

    // Appends all of the chars from the provided span, but only increases the
    // cursor by `length`. This allows oversizing the span to the nearest
    // convenient multiple, allowing CopyChars to run slightly faster.
    V8_INLINE void AppendChars(base::Vector<const uint8_t> chars,
                               size_t length) {
      DCHECK_GE(chars.size(), length);
      CopyChars(cursor_, chars.begin(), chars.size());
      cursor_ += length;
    }

    template <typename SrcChar>
    V8_INLINE void AppendSubstring(const SrcChar* src, size_t from, size_t to) {
      if (from == to) return;
      DCHECK_LT(from, to);
      int count = static_cast<int>(to - from);
      CopyChars(cursor_, src + from, count);
      cursor_ += count;
    }

   private:
    size_t* current_index_;
    DestChar* start_;
    DestChar* cursor_;
  };

  // A cache of recently seen property keys which were simple. Simple means:
  //
  // - Internalized, sequential, one-byte string
  // - Contains no characters which need escaping
  //
  // This can be helpful because it's common for JSON to have lists of similar
  // objects. Since property keys are internalized, we will see identical key
  // pointers again and again, and we can use a fast path to copy those keys to
  // the output. However, strings can be externalized any time JS runs, so the
  // caller is responsible for checking whether a string is still the expected
  // type. This cache is cleared on GC, since the GC could move those strings.
  // Using Handles for the cache has been tried, but is too expensive to set up
  // when JSON.stringify is called for tiny inputs.
  class SimplePropertyKeyCache {
   public:
    explicit SimplePropertyKeyCache(Isolate* isolate) : isolate_(isolate) {
      Clear();
      isolate->main_thread_local_heap()->AddGCEpilogueCallback(
          UpdatePointersCallback, this);
    }

    ~SimplePropertyKeyCache() {
      isolate_->main_thread_local_heap()->RemoveGCEpilogueCallback(
          UpdatePointersCallback, this);
    }

    void TryInsert(Tagged<String> string) {
      ReadOnlyRoots roots(isolate_);
      if (string->map() == roots.internalized_one_byte_string_map()) {
        keys_[GetIndex(string)] = MaybeCompress(string);
      }
    }

    bool Contains(Tagged<String> string) {
      return keys_[GetIndex(string)] == MaybeCompress(string);
    }

   private:
    size_t GetIndex(Tagged<String> string) {
      // Short strings are 16 bytes long in pointer-compression builds, so the
      // lower four bits of the pointer may not provide much entropy.
      return (string.ptr() >> 4) & kIndexMask;
    }

    Tagged_t MaybeCompress(Tagged<String> string) {
      return COMPRESS_POINTERS_BOOL
                 ? V8HeapCompressionScheme::CompressObject(string.ptr())
                 : static_cast<Tagged_t>(string.ptr());
    }

    void Clear() { MemsetTagged(keys_, Smi::zero(), kSize); }

    static void UpdatePointersCallback(void* cache) {
      reinterpret_cast<SimplePropertyKeyCache*>(cache)->Clear();
    }

    static constexpr size_t kSizeBits = 6;
    static constexpr size_t kSize = 1 << kSizeBits;
    static constexpr size_t kIndexMask = kSize - 1;

    Isolate* isolate_;
    Tagged_t keys_[kSize];
  };

  // Returns whether any escape sequences were used.
  template <typename SrcChar, typename DestChar, bool raw_json>
  V8_INLINE static bool SerializeStringUnchecked_(
      base::Vector<const SrcChar> src, NoExtendBuilder<DestChar>* dest);

  // Returns whether any escape sequences were used.
  template <typename SrcChar, typename DestChar, bool raw_json>
  V8_INLINE bool SerializeString_(Tagged<String> string,
                                  const DisallowGarbageCollection& no_gc);

  // Tries to do fast-path serialization for a property key, and returns whether
  // it was successful.
  template <typename DestChar>
  bool TrySerializeSimplePropertyKey(Tagged<String> string,
                                     const DisallowGarbageCollection& no_gc);

  V8_INLINE void NewLine();
  V8_NOINLINE void NewLineOutline();
  V8_INLINE void Indent() { indent_++; }
  V8_INLINE void Unindent() { indent_--; }
  V8_INLINE void Separator(bool first);

  DirectHandle<JSReceiver> CurrentHolder(DirectHandle<Object> value,
                                         DirectHandle<Object> inital_holder);

  Result StackPush(Handle<Object> object, Handle<Object> key);
  void StackPop();

  // Uses the current stack_ to provide a detailed error message of
  // the objects involved in the circular structure.
  Handle<String> ConstructCircularStructureErrorMessage(
      DirectHandle<Object> last_key, size_t start_index);
  // The prefix and postfix count do NOT include the starting and
  // closing lines of the error message.
  static const int kCircularErrorMessagePrefixCount = 2;
  static const int kCircularErrorMessagePostfixCount = 1;

  static const size_t kInitialPartLength = 2048;
  static const size_t kMaxPartLength = 16 * 1024;
  static const size_t kPartLengthGrowthFactor = 2;

  Factory* factory() { return isolate_->factory(); }

  V8_NOINLINE void Extend();
  V8_NOINLINE void ChangeEncoding();

  Isolate* isolate_;
  String::Encoding encoding_;
  Handle<FixedArray> property_list_;
  Handle<JSReceiver> replacer_function_;
  uint8_t* one_byte_ptr_;
  base::uc16* gap_;
  base::uc16* two_byte_ptr_;
  void* part_ptr_;
  int indent_;
  size_t part_length_;
  size_t current_index_;
  int stack_nesting_level_;
  bool overflowed_;
  bool need_stack_;

  using KeyObject = std::pair<Handle<Object>, Handle<Object>>;
  std::vector<KeyObject> stack_;

  SimplePropertyKeyCache key_cache_;
  uint8_t one_byte_array_[kInitialPartLength];
};

namespace {

constexpr int kJsonEscapeTableEntrySize = 8;

// Translation table to escape Latin1 characters.
// Table entries start at a multiple of 8 and are null-terminated.
constexpr const char* const JsonEscapeTable =
    "\\u0000\0 \\u0001\0 \\u0002\0 \\u0003\0 "
    "\\u0004\0 \\u0005\0 \\u0006\0 \\u0007\0 "
    "\\b\0     \\t\0     \\n\0     \\u000b\0 "
    "\\f\0     \\r\0     \\u000e\0 \\u000f\0 "
    "\\u0010\0 \\u0011\0 \\u0012\0 \\u0013\0 "
    "\\u0014\0 \\u0015\0 \\u0016\0 \\u0017\0 "
    "\\u0018\0 \\u0019\0 \\u001a\0 \\u001b\0 "
    "\\u001c\0 \\u001d\0 \\u001e\0 \\u001f\0 "
    " \0      !\0      \\\"\0     #\0      "
    "$\0      %\0      &\0      '\0      "
    "(\0      )\0      *\0      +\0      "
    ",\0      -\0      .\0      /\0      "
    "0\0      1\0      2\0      3\0      "
    "4\0      5\0      6\0      7\0      "
    "8\0      9\0      :\0      ;\0      "
    "<\0      =\0      >\0      ?\0      "
    "@\0      A\0      B\0      C\0      "
    "D\0      E\0      F\0      G\0      "
    "H\0      I\0      J\0      K\0      "
    "L\0      M\0      N\0      O\0      "
    "P\0      Q\0      R\0      S\0      "
    "T\0      U\0      V\0      W\0      "
    "X\0      Y\0      Z\0      [\0      "
    "\\\\\0     ]\0      ^\0      _\0      ";

constexpr bool JsonDoNotEscapeFlagTable[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

template <typename Char>
constexpr bool DoNotEscape(Char c);

template <>
constexpr bool DoNotEscape(uint8_t c) {
  // https://tc39.github.io/ecma262/#table-json-single-character-escapes
  return JsonDoNotEscapeFlagTable[c];
}

template <>
constexpr bool DoNotEscape(uint16_t c) {
  // https://tc39.github.io/ecma262/#table-json-single-character-escapes
  return (c >= 0x20 && c <= 0x21) ||
         (c >= 0x23 && c != 0x5C && (c < 0xD800 || c > 0xDFFF));
}

bool CanFastSerializeJSArray(Isolate* isolate, Tagged<JSArray> object) {
  // If the no elements protector is intact, Array.prototype and
  // Object.prototype are guaranteed to not have elements in any native context.
  if (!Protectors::IsNoElementsIntact(isolate)) return false;
  Tagged<Map> map = object->map(isolate);
  Tagged<NativeContext> native_context = map->map(isolate)->native_context();
  Tagged<HeapObject> proto = map->prototype();
  return native_context->get(Context::INITIAL_ARRAY_PROTOTYPE_INDEX) == proto;
}

V8_INLINE bool CanFastSerializeJSObject(Tagged<JSObject> raw_object,
                                        Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  if (IsCustomElementsReceiverMap(raw_object->map())) return false;
  if (!raw_object->HasFastProperties()) return false;
  auto roots = ReadOnlyRoots(isolate);
  auto elements = raw_object->elements();
  return elements == roots.empty_fixed_array() ||
         elements == roots.empty_slow_element_dictionary();
}

}  // namespace

JsonStringifier::JsonStringifier(Isolate* isolate)
    : isolate_(isolate),
      encoding_(String::ONE_BYTE_ENCODING),
      gap_(nullptr),
      two_byte_ptr_(nullptr),
      indent_(0),
      part_length_(kInitialPartLength),
      current_index_(0),
      stack_nesting_level_(0),
      overflowed_(false),
      need_stack_(false),
      stack_(),
      key_cache_(isolate) {
  one_byte_ptr_ = one_byte_array_;
  part_ptr_ = one_byte_ptr_;
}

MaybeDirectHandle<Object> JsonStringifier::Stringify(Handle<JSAny> object,
                                                     Handle<JSAny> replacer,
                                                     Handle<Object> gap) {
  if (!InitializeReplacer(replacer)) {
    CHECK(isolate_->has_exception());
    return MaybeDirectHandle<Object>();
  }
  if (!IsUndefined(*gap, isolate_) && !InitializeGap(gap)) {
    CHECK(isolate_->has_exception());
    return MaybeDirectHandle<Object>();
  }
  Result result = SerializeObject(object);
  if (result == NEED_STACK) {
    indent_ = 0;
    current_index_ = 0;
    result = SerializeObject(object);
  }
  if (result == UNCHANGED) return factory()->undefined_value();
  if (result == SUCCESS) {
    if (overflowed_ || current_index_ > String::kMaxLength) {
      THROW_NEW_ERROR(isolate_, NewInvalidStringLengthError());
    }
    if (encoding_ == String::ONE_BYTE_ENCODING) {
      return isolate_->factory()
          ->NewStringFromOneByte(base::OneByteVector(
              reinterpret_cast<char*>(one_byte_ptr_), current_index_))
          .ToHandleChecked();
    } else {
      return isolate_->factory()->NewStringFromTwoByte(
          base::Vector<const base::uc16>(two_byte_ptr_, current_index_));
    }
  }
  DCHECK(result == EXCEPTION);
  CHECK(isolate_->has_exception());
  return MaybeDirectHandle<Object>();
}

bool JsonStringifier::InitializeReplacer(Handle<JSAny> replacer) {
  DCHECK(property_list_.is_null());
  DCHECK(replacer_function_.is_null());
  Maybe<bool> is_array = Object::IsArray(replacer);
  if (is_array.IsNothing()) return false;
  if (is_array.FromJust()) {
    HandleScope handle_scope(isolate_);
    Handle<OrderedHashSet> set = factory()->NewOrderedHashSet();
    DirectHandle<Object> length_obj;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, length_obj,
        Object::GetLengthFromArrayLike(isolate_, Cast<JSReceiver>(replacer)),
        false);
    uint32_t length;
    if (!Object::ToUint32(*length_obj, &length)) length = kMaxUInt32;
    for (uint32_t i = 0; i < length; i++) {
      Handle<Object> element;
      Handle<String> key;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate_, element, Object::GetElement(isolate_, replacer, i), false);
      if (IsNumber(*element) || IsString(*element)) {
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate_, key, Object::ToString(isolate_, element), false);
      } else if (IsJSPrimitiveWrapper(*element)) {
        DirectHandle<Object> value(Cast<JSPrimitiveWrapper>(element)->value(),
                                   isolate_);
        if (IsNumber(*value) || IsString(*value)) {
          ASSIGN_RETURN_ON_EXCEPTION_VALUE(
              isolate_, key, Object::ToString(isolate_, element), false);
        }
      }
      if (key.is_null()) continue;
      // Object keys are internalized, so do it here.
      key = factory()->InternalizeString(key);
      MaybeHandle<OrderedHashSet> set_candidate =
          OrderedHashSet::Add(isolate_, set, key);
      if (!set_candidate.ToHandle(&set)) {
        CHECK(isolate_->has_exception());
        return false;
      }
    }
    property_list_ = OrderedHashSet::ConvertToKeysArray(
        isolate_, set, GetKeysConversion::kKeepNumbers);
    property_list_ = handle_scope.CloseAndEscape(property_list_);
  } else if (IsCallable(*replacer)) {
    replacer_function_ = Cast<JSReceiver>(replacer);
  }
  return true;
}

bool JsonStringifier::InitializeGap(Handle<Object> gap) {
  DCHECK_NULL(gap_);
  HandleScope scope(isolate_);
  if (IsJSPrimitiveWrapper(*gap)) {
    DirectHandle<Object> value(Cast<JSPrimitiveWrapper>(gap)->value(),
                               isolate_);
    if (IsString(*value)) {
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate_, gap,
                                       Object::ToString(isolate_, gap), false);
    } else if (IsNumber(*value)) {
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate_, gap,
                                       Object::ToNumber(isolate_, gap), false);
    }
  }

  if (IsString(*gap)) {
    auto gap_string = Cast<String>(gap);
    if (gap_string->length() > 0) {
      uint32_t gap_length = std::min(gap_string->length(), 10u);
      gap_ = NewArray<base::uc16>(gap_length + 1);
      String::WriteToFlat(*gap_string, gap_, 0, gap_length);
      for (uint32_t i = 0; i < gap_length; i++) {
        if (gap_[i] > String::kMaxOneByteCharCode) {
          ChangeEncoding();
          break;
        }
      }
      gap_[gap_length] = '\0';
    }
  } else if (IsNumber(*gap)) {
    double value = std::min(Object::NumberValue(*gap), 10.0);
    if (value > 0) {
      uint32_t gap_length = DoubleToUint32(value);
      gap_ = NewArray<base::uc16>(gap_length + 1);
      for (uint32_t i = 0; i < gap_length; i++) gap_[i] = ' ';
      gap_[gap_length] = '\0';
    }
  }
  return true;
}

MaybeHandle<JSAny> JsonStringifier::ApplyToJsonFunction(
    Handle<JSAny> object, DirectHandle<Object> key) {
  HandleScope scope(isolate_);

  // Retrieve toJSON function. The LookupIterator automatically handles
  // the ToObject() equivalent ("GetRoot") if {object} is a BigInt.
  DirectHandle<Object> fun;
  LookupIterator it(isolate_, object, factory()->toJSON_string(),
                    LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  ASSIGN_RETURN_ON_EXCEPTION(isolate_, fun, Object::GetProperty(&it));
  if (!IsCallable(*fun)) return object;

  // Call toJSON function.
  if (IsSmi(*key)) key = factory()->NumberToString(key);
  DirectHandle<Object> args[] = {key};
  ASSIGN_RETURN_ON_EXCEPTION(isolate_, object,
                             Cast<JSAny>(Execution::Call(
                                 isolate_, fun, object, base::VectorOf(args))));
  return scope.CloseAndEscape(object);
}

MaybeHandle<JSAny> JsonStringifier::ApplyReplacerFunction(
    Handle<JSAny> value, DirectHandle<Object> key,
    DirectHandle<Object> initial_holder) {
  HandleScope scope(isolate_);
  if (IsSmi(*key)) key = factory()->NumberToString(key);
  DirectHandle<Object> args[] = {key, value};
  DirectHandle<JSReceiver> holder = CurrentHolder(value, initial_holder);
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate_, value,
      Cast<JSAny>(Execution::Call(isolate_, replacer_function_, holder,
                                  base::VectorOf(args))));
  return scope.CloseAndEscape(value);
}

DirectHandle<JSReceiver> JsonStringifier::CurrentHolder(
    DirectHandle<Object> value, DirectHandle<Object> initial_holder) {
  if (stack_.empty()) {
    DirectHandle<JSObject> holder =
        factory()->NewJSObject(isolate_->object_function());
    JSObject::AddProperty(isolate_, holder, factory()->empty_string(),
                          initial_holder, NONE);
    return holder;
  } else {
    return DirectHandle<JSReceiver>(Cast<JSReceiver>(*stack_.back().second),
                                    isolate_);
  }
}

JsonStringifier::Result JsonStringifier::StackPush(Handle<Object> object,
                                                   Handle<Object> key) {
  if (!need_stack_) {
    ++stack_nesting_level_;
    if V8_UNLIKELY (stack_nesting_level_ > 10) {
      need_stack_ = true;
      return NEED_STACK;
    }
    return SUCCESS;
  }
  StackLimitCheck check(isolate_);
  if (check.HasOverflowed()) {
    isolate_->StackOverflow();
    return EXCEPTION;
  }

  {
    DisallowGarbageCollection no_gc;
    Tagged<Object> raw_obj = *object;
    size_t size = stack_.size();
    for (size_t i = 0; i < size; ++i) {
      if (*stack_[i].second == raw_obj) {
        AllowGarbageCollection allow_to_return_error;
        Handle<String> circle_description =
            ConstructCircularStructureErrorMessage(key, i);
        DirectHandle<Object> error = factory()->NewTypeError(
            MessageTemplate::kCircularStructure, circle_description);
        isolate_->Throw(*error);
        return EXCEPTION;
      }
    }
  }
  stack_.emplace_back(key, object);
  return SUCCESS;
}

void JsonStringifier::StackPop() {
  if V8_LIKELY (!need_stack_) {
    --stack_nesting_level_;
    return;
  }
  stack_.pop_back();
}

class CircularStructureMessageBuilder {
 public:
  explicit CircularStructureMessageBuilder(Isolate* isolate)
      : builder_(isolate) {}

  void AppendStartLine(DirectHandle<Object> start_object) {
    builder_.AppendCString(kStartPrefix);
    builder_.AppendCStringLiteral("starting at object with constructor ");
    AppendConstructorName(start_object);
  }

  void AppendNormalLine(DirectHandle<Object> key, DirectHandle<Object> object) {
    builder_.AppendCString(kLinePrefix);
    AppendKey(key);
    builder_.AppendCStringLiteral(" -> object with constructor ");
    AppendConstructorName(object);
  }

  void AppendClosingLine(DirectHandle<Object> closing_key) {
    builder_.AppendCString(kEndPrefix);
    AppendKey(closing_key);
    builder_.AppendCStringLiteral(" closes the circle");
  }

  void AppendEllipsis() {
    builder_.AppendCString(kLinePrefix);
    builder_.AppendCStringLiteral("...");
  }

  MaybeDirectHandle<String> Finish() { return builder_.Finish(); }

 private:
  void AppendConstructorName(DirectHandle<Object> object) {
    builder_.AppendCharacter('\'');
    DirectHandle<String> constructor_name = JSReceiver::GetConstructorName(
        builder_.isolate(), Cast<JSReceiver>(object));
    builder_.AppendString(constructor_name);
    builder_.AppendCharacter('\'');
  }

  // A key can either be a string, the empty string or a Smi.
  void AppendKey(DirectHandle<Object> key) {
    if (IsSmi(*key)) {
      builder_.AppendCStringLiteral("index ");
      AppendSmi(Cast<Smi>(*key));
      return;
    }

    CHECK(IsString(*key));
    DirectHandle<String> key_as_string = Cast<String>(key);
    if (key_as_string->length() == 0) {
      builder_.AppendCStringLiteral("<anonymous>");
    } else {
      builder_.AppendCStringLiteral("property '");
      builder_.AppendString(key_as_string);
      builder_.AppendCharacter('\'');
    }
  }

  void AppendSmi(Tagged<Smi> smi) {
    static_assert(Smi::kMaxValue <= 2147483647);
    static_assert(Smi::kMinValue >= -2147483648);
    // sizeof(string) includes \0.
    static const int kBufferSize = sizeof("-2147483648") - 1;
    char chars[kBufferSize];
    base::Vector<char> buffer(chars, kBufferSize);
    builder_.AppendString(IntToStringView(smi.value(), buffer));
  }

  IncrementalStringBuilder builder_;
  static constexpr const char* kStartPrefix = "\n    --> ";
  static constexpr const char* kEndPrefix = "\n    --- ";
  static constexpr const char* kLinePrefix = "\n    |     ";
};

Handle<String> JsonStringifier::ConstructCircularStructureErrorMessage(
    DirectHandle<Object> last_key, size_t start_index) {
  DCHECK(start_index < stack_.size());
  CircularStructureMessageBuilder builder(isolate_);

  // We track the index to be printed next for better readability.
  size_t index = start_index;
  const size_t stack_size = stack_.size();

  builder.AppendStartLine(stack_[index++].second);

  // Append a maximum of kCircularErrorMessagePrefixCount normal lines.
  const size_t prefix_end =
      std::min(stack_size, index + kCircularErrorMessagePrefixCount);
  for (; index < prefix_end; ++index) {
    builder.AppendNormalLine(stack_[index].first, stack_[index].second);
  }

  // If the circle consists of too many objects, we skip them and just
  // print an ellipsis.
  if (stack_size > index + kCircularErrorMessagePostfixCount) {
    builder.AppendEllipsis();
  }

  // Since we calculate the postfix lines from the back of the stack,
  // we have to ensure that lines are not printed twice.
  index = std::max(index, stack_size - kCircularErrorMessagePostfixCount);
  for (; index < stack_size; ++index) {
    builder.AppendNormalLine(stack_[index].first, stack_[index].second);
  }

  builder.AppendClosingLine(last_key);

  Handle<String> result;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate_, result,
                                   indirect_handle(builder.Finish(), isolate_),
                                   factory()->empty_string());
  return result;
}

bool MayHaveInterestingProperties(Isolate* isolate, Tagged<JSReceiver> object) {
  for (PrototypeIterator iter(isolate, object, kStartAtReceiver);
       !iter.IsAtEnd(); iter.Advance()) {
    if (iter.GetCurrent()->map()->may_have_interesting_properties()) {
      return true;
    }
  }
  return false;
}

template <bool deferred_string_key>
JsonStringifier::Result JsonStringifier::Serialize_(Handle<JSAny> object,
                                                    bool comma,
                                                    Handle<Object> key) {
  StackLimitCheck interrupt_check(isolate_);
  if (interrupt_check.InterruptRequested() &&
      IsException(isolate_->stack_guard()->HandleInterrupts(), isolate_)) {
    return EXCEPTION;
  }

  DirectHandle<JSAny> initial_value = object;
  PtrComprCageBase cage_base(isolate_);
  if (!IsSmi(*object)) {
    InstanceType instance_type =
        Cast<HeapObject>(*object)->map(cage_base)->instance_type();
    if ((InstanceTypeChecker::IsJSReceiver(instance_type) &&
         MayHaveInterestingProperties(isolate_, Cast<JSReceiver>(*object))) ||
        InstanceTypeChecker::IsBigInt(instance_type)) {
      if (!need_stack_ && stack_nesting_level_ > 0) {
        need_stack_ = true;
        return NEED_STACK;
      }
      need_stack_ = true;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate_, object, ApplyToJsonFunction(object, key), EXCEPTION);
    }
  }
  if (!replacer_function_.is_null()) {
    need_stack_ = true;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, object, ApplyReplacerFunction(object, key, initial_value),
        EXCEPTION);
  }

  if (IsSmi(*object)) {
    if (deferred_string_key) SerializeDeferredKey(comma, key);
    return SerializeSmi(Cast<Smi>(*object));
  }

  InstanceType instance_type =
      Cast<HeapObject>(*object)->map(cage_base)->instance_type();
  switch (instance_type) {
    case HEAP_NUMBER_TYPE:
      if (deferred_string_key) SerializeDeferredKey(comma, key);
      return SerializeHeapNumber(Cast<HeapNumber>(object));
    case BIGINT_TYPE:
      isolate_->Throw(
          *factory()->NewTypeError(MessageTemplate::kBigIntSerializeJSON));
      return EXCEPTION;
    case ODDBALL_TYPE:
      switch (Cast<Oddball>(*object)->kind()) {
        case Oddball::kFalse:
          if (deferred_string_key) SerializeDeferredKey(comma, key);
          AppendCStringLiteral("false");
          return SUCCESS;
        case Oddball::kTrue:
          if (deferred_string_key) SerializeDeferredKey(comma, key);
          AppendCStringLiteral("true");
          return SUCCESS;
        case Oddball::kNull:
          if (deferred_string_key) SerializeDeferredKey(comma, key);
          AppendCStringLiteral("null");
          return SUCCESS;
        default:
          return UNCHANGED;
      }
    case JS_ARRAY_TYPE:
      if (deferred_string_key) SerializeDeferredKey(comma, key);
      return SerializeJSArray(Cast<JSArray>(object), key);
    case JS_PRIMITIVE_WRAPPER_TYPE:
      if (!need_stack_) {
        need_stack_ = true;
        return NEED_STACK;
      }
      if (deferred_string_key) SerializeDeferredKey(comma, key);
      return SerializeJSPrimitiveWrapper(Cast<JSPrimitiveWrapper>(object), key);
    case SYMBOL_TYPE:
      return UNCHANGED;
    case JS_RAW_JSON_TYPE:
      if (deferred_string_key) SerializeDeferredKey(comma, key);
      {
        DirectHandle<JSRawJson> raw_json_obj = Cast<JSRawJson>(object);
        Handle<String> raw_json;
        if (raw_json_obj->HasInitialLayout(isolate_)) {
          // Fast path: the object returned by JSON.rawJSON has its initial map
          // intact.
          raw_json = Cast<String>(handle(
              raw_json_obj->InObjectPropertyAt(JSRawJson::kRawJsonInitialIndex),
              isolate_));
        } else {
          // Slow path: perform a property get for "rawJSON". Because raw JSON
          // objects are created frozen, it is still guaranteed that there will
          // be a property named "rawJSON" that is a String. Their initial maps
          // only change due to VM-internal operations like being optimized for
          // being used as a prototype.
          raw_json = Cast<String>(
              JSObject::GetProperty(isolate_, raw_json_obj,
                                    isolate_->factory()->raw_json_string())
                  .ToHandleChecked());
        }
        AppendString(raw_json);
      }
      return SUCCESS;
    case HOLE_TYPE:
      UNREACHABLE();
#if V8_ENABLE_WEBASSEMBLY
    case WASM_STRUCT_TYPE:
    case WASM_ARRAY_TYPE:
      return UNCHANGED;
#endif
    default:
      if (InstanceTypeChecker::IsString(instance_type)) {
        if (deferred_string_key) SerializeDeferredKey(comma, key);
        SerializeString<false>(Cast<String>(object));
        return SUCCESS;
      } else {
        // Make sure that we have a JSReceiver before we cast it to one.
        // If we ever leak an internal object that is not a JSReceiver it could
        // end up here and lead to a type confusion.
        CHECK(IsJSReceiver(*object));
        if (IsCallable(Cast<HeapObject>(*object), cage_base)) return UNCHANGED;
        // Go to slow path for global proxy and objects requiring access checks.
        if (deferred_string_key) SerializeDeferredKey(comma, key);
        if (InstanceTypeChecker::IsJSProxy(instance_type)) {
          return SerializeJSProxy(Cast<JSProxy>(object), key);
        }
        // WASM_{STRUCT,ARRAY}_TYPE are handled in `case:` blocks above.
        DCHECK(IsJSObject(*object));
        return SerializeJSObject(Cast<JSObject>(object), key);
      }
  }

  UNREACHABLE();
}

JsonStringifier::Result JsonStringifier::SerializeJSPrimitiveWrapper(
    Handle<JSPrimitiveWrapper> object, Handle<Object> key) {
  Tagged<Object> raw = object->value();
  if (IsString(raw)) {
    Handle<Object> value;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, value, Object::ToString(isolate_, object), EXCEPTION);
    SerializeString<false>(Cast<String>(value));
  } else if (IsNumber(raw)) {
    DirectHandle<Object> value;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, value, Object::ToNumber(isolate_, object), EXCEPTION);
    if (IsSmi(*value)) return SerializeSmi(Cast<Smi>(*value));
    SerializeHeapNumber(Cast<HeapNumber>(value));
  } else if (IsBigInt(raw)) {
    isolate_->Throw(
        *factory()->NewTypeError(MessageTemplate::kBigIntSerializeJSON));
    return EXCEPTION;
  } else if (IsBoolean(raw)) {
    if (IsTrue(raw, isolate_)) {
      AppendCStringLiteral("true");
    } else {
      AppendCStringLiteral("false");
    }
  } else {
    // ES6 24.3.2.1 step 10.c, serialize as an ordinary JSObject.
    return SerializeJSObject(object, key);
  }
  return SUCCESS;
}

JsonStringifier::Result JsonStringifier::SerializeSmi(Tagged<Smi> object) {
  static_assert(Smi::kMaxValue <= 2147483647);
  static_assert(Smi::kMinValue >= -2147483648);
  // sizeof(string) includes \0.
  static const int kBufferSize = sizeof("-2147483648") - 1;
  char chars[kBufferSize];
  base::Vector<char> buffer(chars, kBufferSize);
  AppendString(IntToStringView(object.value(), buffer));
  return SUCCESS;
}

JsonStringifier::Result JsonStringifier::SerializeDouble(double number) {
  if (std::isinf(number) || std::isnan(number)) {
    AppendCStringLiteral("null");
    return SUCCESS;
  }
  static const int kBufferSize = 100;
  char chars[kBufferSize];
  base::Vector<char> buffer(chars, kBufferSize);
  std::string_view str = DoubleToStringView(number, buffer);
  AppendString(str);
  return SUCCESS;
}

JsonStringifier::Result JsonStringifier::SerializeJSArray(
    Handle<JSArray> object, Handle<Object> key) {
  uint32_t length = 0;
  CHECK(Object::ToArrayLength(object->length(), &length));
  DCHECK(!IsAccessCheckNeeded(*object));
  if (length == 0) {
    AppendCStringLiteral("[]");
    return SUCCESS;
  }

  Result stack_push = StackPush(object, key);
  if (stack_push != SUCCESS) return stack_push;

  AppendCharacter('[');
  Indent();
  uint32_t slow_path_index = 0;
  Result result = UNCHANGED;
  if (replacer_function_.is_null()) {
#define CASE_WITH_INTERRUPT(kind)                                           \
  case kind:                                                                \
    result = SerializeFixedArrayWithInterruptCheck<kind>(object, length,    \
                                                         &slow_path_index); \
    break;
#define CASE_WITH_TRANSITION(kind)                             \
  case kind:                                                   \
    result = SerializeFixedArrayWithPossibleTransitions<kind>( \
        object, length, &slow_path_index);                     \
    break;

    switch (object->GetElementsKind()) {
      CASE_WITH_INTERRUPT(PACKED_SMI_ELEMENTS)
      CASE_WITH_INTERRUPT(HOLEY_SMI_ELEMENTS)
      CASE_WITH_TRANSITION(PACKED_ELEMENTS)
      CASE_WITH_TRANSITION(HOLEY_ELEMENTS)
      CASE_WITH_INTERRUPT(PACKED_DOUBLE_ELEMENTS)
      CASE_WITH_INTERRUPT(HOLEY_DOUBLE_ELEMENTS)
      default:
        break;
    }

#undef CASE_WITH_TRANSITION
#undef CASE_WITH_INTERRUPT
  }
  if (result == UNCHANGED) {
    // Slow path for non-fast elements and fall-back in edge cases.
    result = SerializeArrayLikeSlow(object, slow_path_index, length);
  }
  if (result != SUCCESS) return result;
  Unindent();
  NewLine();
  AppendCharacter(']');
  StackPop();
  return SUCCESS;
}

template <ElementsKind kind>
JsonStringifier::Result JsonStringifier::SerializeFixedArrayWithInterruptCheck(
    DirectHandle<JSArray> array, uint32_t length, uint32_t* slow_path_index) {
  static_assert(IsSmiElementsKind(kind) || IsDoubleElementsKind(kind));
  using ArrayT = std::conditional_t<IsDoubleElementsKind(kind),
                                    FixedDoubleArray, FixedArray>;

  StackLimitCheck interrupt_check(isolate_);
  constexpr uint32_t kInterruptLength = 4000;
  uint32_t limit = std::min(length, kInterruptLength);
  constexpr uint32_t kMaxAllowedFastPackedLength =
      std::numeric_limits<uint32_t>::max() - kInterruptLength;
  static_assert(FixedArray::kMaxLength < kMaxAllowedFastPackedLength);

  constexpr bool is_holey = IsHoleyElementsKind(kind);
  bool bailout_on_hole =
      is_holey ? !CanFastSerializeJSArray(isolate_, *array) : true;

  uint32_t i = 0;
  while (true) {
    for (; i < limit; i++) {
      Result result = SerializeFixedArrayElement<kind>(
          Cast<ArrayT>(array->elements()), i, *array, bailout_on_hole);
      if constexpr (is_holey) {
        if (result != SUCCESS) {
          *slow_path_index = i;
          return result;
        }
      } else {
        USE(result);
        DCHECK_EQ(result, SUCCESS);
      }
    }
    if (i >= length) return SUCCESS;
    DCHECK_LT(limit, kMaxAllowedFastPackedLength);
    limit = std::min(length, limit + kInterruptLength);
    if (interrupt_check.InterruptRequested() &&
        IsException(isolate_->stack_guard()->HandleInterrupts(), isolate_)) {
      return EXCEPTION;
    }
  }
  return SUCCESS;
}

template <ElementsKind kind>
JsonStringifier::Result
JsonStringifier::SerializeFixedArrayWithPossibleTransitions(
    DirectHandle<JSArray> array, uint32_t length, uint32_t* slow_path_index) {
  static_assert(IsObjectElementsKind(kind));

  HandleScope handle_scope(isolate_);
  DirectHandle<Object> old_length(array->length(), isolate_);
  constexpr bool is_holey = IsHoleyElementsKind(kind);
  bool should_check_treat_hole_as_undefined = true;
  for (uint32_t i = 0; i < length; i++) {
    if (array->length() != *old_length || kind != array->GetElementsKind()) {
      // Array was modified during SerializeElement.
      *slow_path_index = i;
      return UNCHANGED;
    }
    Tagged<Object> current_element =
        Cast<FixedArray>(array->elements())->get(i);
    if (is_holey && IsTheHole(current_element)) {
      if (should_check_treat_hole_as_undefined) {
        if (!CanFastSerializeJSArray(isolate_, *array)) {
          *slow_path_index = i;
          return UNCHANGED;
        }
        should_check_treat_hole_as_undefined = false;
      }
      Separator(i == 0);
      AppendCStringLiteral("null");
    } else {
      Separator(i == 0);
      Result result = SerializeElement(
          isolate_, handle(Cast<JSAny>(current_element), isolate_), i);
      if (result == UNCHANGED) {
        AppendCStringLiteral("null");
      } else if (result != SUCCESS) {
        return result;
      }
      if constexpr (is_holey) {
        should_check_treat_hole_as_undefined = true;
      }
    }
  }
  return SUCCESS;
}

template <ElementsKind kind, typename T>
JsonStringifier::Result JsonStringifier::SerializeFixedArrayElement(
    Tagged<T> elements, uint32_t i, Tagged<JSArray> array,
    bool bailout_on_hole) {
  if constexpr (IsHoleyElementsKind(kind)) {
    if (elements->is_the_hole(isolate_, i)) {
      if (bailout_on_hole) return UNCHANGED;
      Separator(i == 0);
      AppendCStringLiteral("null");
      return SUCCESS;
    }
  }
  DCHECK(!elements->is_the_hole(isolate_, i));
  Separator(i == 0);
  if constexpr (IsSmiElementsKind(kind)) {
    SerializeSmi(Cast<Smi>(elements->get(i)));
  } else if constexpr (IsDoubleElementsKind(kind)) {
    SerializeDouble(elements->get_scalar(i));
  } else {
    UNREACHABLE();
  }
  return SUCCESS;
}

JsonStringifier::Result JsonStringifier::SerializeArrayLikeSlow(
    DirectHandle<JSReceiver> object, uint32_t start, uint32_t length) {
  if (!need_stack_) {
    need_stack_ = true;
    return NEED_STACK;
  }
  // We need to write out at least two characters per array element.
  static const int kMaxSerializableArrayLength = String::kMaxLength / 2;
  if (length > kMaxSerializableArrayLength) {
    isolate_->Throw(*isolate_->factory()->NewInvalidStringLengthError());
    return EXCEPTION;
  }
  HandleScope handle_scope(isolate_);
  for (uint32_t i = start; i < length; i++) {
    Separator(i == 0);
    Handle<Object> element;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, element, JSReceiver::GetElement(isolate_, object, i),
        EXCEPTION);
    Result result = SerializeElement(isolate_, Cast<JSAny>(element), i);
    if (result == SUCCESS) continue;
    if (result == UNCHANGED) {
      // Detect overflow sooner for large sparse arrays.
      if (overflowed_) {
        isolate_->Throw(*isolate_->factory()->NewInvalidStringLengthError());
        return EXCEPTION;
      }
      AppendCStringLiteral("null");
    } else {
      return result;
    }
  }
  return SUCCESS;
}

JsonStringifier::Result JsonStringifier::SerializeJSObject(
    Handle<JSObject> object, Handle<Object> key) {
  PtrComprCageBase cage_base(isolate_);
  HandleScope handle_scope(isolate_);

  if (!property_list_.is_null() ||
      !CanFastSerializeJSObject(*object, isolate_)) {
    if (!need_stack_) {
      need_stack_ = true;
      return NEED_STACK;
    }
    Result stack_push = StackPush(object, key);
    if (stack_push != SUCCESS) return stack_push;
    Result result = SerializeJSReceiverSlow(object);
    if (result != SUCCESS) return result;
    StackPop();
    return SUCCESS;
  }

  DCHECK(!IsJSGlobalProxy(*object));
  DCHECK(!object->HasIndexedInterceptor());
  DCHECK(!object->HasNamedInterceptor());

  DirectHandle<Map> map(object->map(cage_base), isolate_);
  if (map->NumberOfOwnDescriptors() == 0) {
    AppendCStringLiteral("{}");
    return SUCCESS;
  }

  Result stack_push = StackPush(object, key);
  if (stack_push != SUCCESS) return stack_push;
  AppendCharacter('{');
  Indent();
  bool comma = false;
  for (InternalIndex i : map->IterateOwnDescriptors()) {
    Handle<String> key_name;
    PropertyDetails details = PropertyDetails::Empty();
    {
      DisallowGarbageCollection no_gc;
      Tagged<DescriptorArray> descriptors =
          map->instance_descriptors(cage_base);
      Tagged<Name> name = descriptors->GetKey(i);
      // TODO(rossberg): Should this throw?
      if (!IsString(name, cage_base)) continue;
      key_name = handle(Cast<String>(name), isolate_);
      details = descriptors->GetDetails(i);
    }
    if (details.IsDontEnum()) continue;
    Handle<JSAny> property;
    if (details.location() == PropertyLocation::kField &&
        *map == object->map(cage_base)) {
      DCHECK_EQ(PropertyKind::kData, details.kind());
      FieldIndex field_index = FieldIndex::ForDetails(*map, details);
      if (replacer_function_.is_null()) {
        // If there's no replacer function, read the raw property to avoid
        // reboxing doubles in mutable boxes.
        property = handle(object->RawFastPropertyAt(field_index), isolate_);
      } else {
        // Rebox the value if there is a replacer function since it could change
        // the value in the box.
        property = JSObject::FastPropertyAt(
            isolate_, object, details.representation(), field_index);
      }
    } else {
      if (!need_stack_) {
        need_stack_ = true;
        return NEED_STACK;
      }
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate_, property,
          Cast<JSAny>(Object::GetPropertyOrElement(isolate_, object, key_name)),
          EXCEPTION);
    }
    Result result = SerializeProperty(property, comma, key_name);
    if (!comma && result == SUCCESS) comma = true;
    if (result == EXCEPTION || result == NEED_STACK) return result;
  }
  Unindent();
  if (comma) NewLine();
  AppendCharacter('}');
  StackPop();
  return SUCCESS;
}

JsonStringifier::Result JsonStringifier::SerializeJSReceiverSlow(
    DirectHandle<JSReceiver> object) {
  DirectHandle<FixedArray> contents = property_list_;
  if (contents.is_null()) {
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, contents,
        KeyAccumulator::GetKeys(isolate_, object, KeyCollectionMode::kOwnOnly,
                                ENUMERABLE_STRINGS,
                                GetKeysConversion::kConvertToString),
        EXCEPTION);
  }
  AppendCharacter('{');
  Indent();
  bool comma = false;
  for (int i = 0; i < contents->length(); i++) {
    Handle<String> key(Cast<String>(contents->get(i)), isolate_);
    Handle<Object> property;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, property, Object::GetPropertyOrElement(isolate_, object, key),
        EXCEPTION);
    Result result = SerializeProperty(Cast<JSAny>(property), comma, key);
    if (!comma && result == SUCCESS) comma = true;
    if (result == EXCEPTION || result == NEED_STACK) return result;
  }
  Unindent();
  if (comma) NewLine();
  AppendCharacter('}');
  return SUCCESS;
}

JsonStringifier::Result JsonStringifier::SerializeJSProxy(
    Handle<JSProxy> object, Handle<Object> key) {
  HandleScope scope(isolate_);
  Result stack_push = StackPush(object, key);
  if (stack_push != SUCCESS) return stack_push;
  Maybe<bool> is_array = Object::IsArray(object);
  if (is_array.IsNothing()) return EXCEPTION;
  if (is_array.FromJust()) {
    DirectHandle<Object> length_object;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate_, length_object,
        Object::GetLengthFromArrayLike(isolate_, Cast<JSReceiver>(object)),
        EXCEPTION);
    uint32_t length;
    if (!Object::ToUint32(*length_object, &length)) {
      // Technically, we need to be able to handle lengths outside the
      // uint32_t range. However, we would run into string size overflow
      // if we tried to stringify such an array.
      isolate_->Throw(*isolate_->factory()->NewInvalidStringLengthError());
      return EXCEPTION;
    }
    AppendCharacter('[');
    Indent();
    Result result = SerializeArrayLikeSlow(object, 0, length);
    if (result != SUCCESS) return result;
    Unindent();
    if (length > 0) NewLine();
    AppendCharacter(']');
  } else {
    Result result = SerializeJSReceiverSlow(object);
    if (result != SUCCESS) return result;
  }
  StackPop();
  return SUCCESS;
}

template <typename SrcChar, typename DestChar, bool raw_json>
bool JsonStringifier::SerializeStringUnchecked_(
    base::Vector<const SrcChar> src, NoExtendBuilder<DestChar>* dest) {
  // Assert that base::uc16 character is not truncated down to 8 bit.
  // The <base::uc16, char> version of this method must not be called.
  DCHECK(sizeof(DestChar) >= sizeof(SrcChar));
  bool required_escaping = false;
  int prev_escaped_offset = -1;
  for (int i = 0; i < src.length(); i++) {
    SrcChar c = src[i];
    if (raw_json || DoNotEscape(c)) {
      continue;
    } else if (sizeof(SrcChar) != 1 &&
               base::IsInRange(c, static_cast<SrcChar>(0xD800),
                               static_cast<SrcChar>(0xDFFF))) {
      // The current character is a surrogate.
      required_escaping = true;
      dest->AppendSubstring(src.data(), prev_escaped_offset + 1, i);

      char double_to_radix_chars[kDoubleToRadixMaxChars];
      base::Vector<char> double_to_radix_buffer =
          base::ArrayVector(double_to_radix_chars);
      if (c <= 0xDBFF) {
        // The current character is a leading surrogate.
        if (i + 1 < src.length()) {
          // There is a next character.
          SrcChar next = src[i + 1];
          if (base::IsInRange(next, static_cast<SrcChar>(0xDC00),
                              static_cast<SrcChar>(0xDFFF))) {
            // The next character is a trailing surrogate, meaning this is a
            // surrogate pair.
            dest->Append(c);
            dest->Append(next);
            i++;
          } else {
            // The next character is not a trailing surrogate. Thus, the
            // current character is a lone leading surrogate.
            dest->AppendCString("\\u");
            std::string_view hex =
                DoubleToRadixStringView(c, 16, double_to_radix_buffer);
            dest->AppendString(hex);
          }
        } else {
          // There is no next character. Thus, the current character is a lone
          // leading surrogate.
          dest->AppendCString("\\u");
          std::string_view hex =
              DoubleToRadixStringView(c, 16, double_to_radix_buffer);
          dest->AppendString(hex);
        }
      } else {
        // The current character is a lone trailing surrogate. (If it had been
        // preceded by a leading surrogate, we would've ended up in the other
        // branch earlier on, and the current character would've been handled
        // as part of the surrogate pair already.)
        dest->AppendCString("\\u");
        std::string_view hex =
            DoubleToRadixStringView(c, 16, double_to_radix_buffer);
        dest->AppendString(hex);
      }
      prev_escaped_offset = i;
    } else {
      required_escaping = true;
      dest->AppendSubstring(src.data(), prev_escaped_offset + 1, i);
      DCHECK_LT(c, 0x60);
      dest->AppendCString(&JsonEscapeTable[c * kJsonEscapeTableEntrySize]);
      prev_escaped_offset = i;
    }
  }
  dest->AppendSubstring(src.data(), prev_escaped_offset + 1, src.size());
  return required_escaping;
}

template <typename SrcChar, typename DestChar, bool raw_json>
bool JsonStringifier::SerializeString_(Tagged<String> string,
                                       const DisallowGarbageCollection& no_gc) {
  bool required_escaping = false;
  if (!raw_json) Append<uint8_t, DestChar>('"');
  // We might be able to fit the whole escaped string in the current string
  // part, or we might need to allocate.
  base::Vector<const SrcChar> vector = string->GetCharVector<SrcChar>(no_gc);
  if V8_LIKELY (EscapedLengthIfCurrentPartFits(vector.size())) {
    NoExtendBuilder<DestChar> no_extend(
        reinterpret_cast<DestChar*>(part_ptr_) + current_index_,
        &current_index_);
    required_escaping = SerializeStringUnchecked_<SrcChar, DestChar, raw_json>(
        vector, &no_extend);
  } else {
    DCHECK(encoding_ == String::TWO_BYTE_ENCODING ||
           (string->IsFlat() && string->IsOneByteRepresentation()));
    int prev_escaped_offset = -1;
    for (int i = 0; i < vector.length(); i++) {
      SrcChar c = vector.at(i);
      if (raw_json || DoNotEscape(c)) {
        continue;
      } else if (sizeof(SrcChar) != 1 &&
                 base::IsInRange(c, static_cast<SrcChar>(0xD800),
                                 static_cast<SrcChar>(0xDFFF))) {
        // The current character is a surrogate.
        required_escaping = true;
        AppendSubstring(vector.data(), prev_escaped_offset + 1, i);

        char double_to_radix_chars[kDoubleToRadixMaxChars];
        base::Vector<char> double_to_radix_buffer =
            base::ArrayVector(double_to_radix_chars);
        if (c <= 0xDBFF) {
          // The current character is a leading surrogate.
          if (i + 1 < vector.length()) {
            // There is a next character.
            SrcChar next = vector.at(i + 1);
            if (base::IsInRange(next, static_cast<SrcChar>(0xDC00),
                                static_cast<SrcChar>(0xDFFF))) {
              // The next character is a trailing surrogate, meaning this is a
              // surrogate pair.
              Append<SrcChar, DestChar>(c);
              Append<SrcChar, DestChar>(next);
              i++;
            } else {
              // The next character is not a trailing surrogate. Thus, the
              // current character is a lone leading surrogate.
              AppendCStringLiteral("\\u");
              std::string_view hex =
                  DoubleToRadixStringView(c, 16, double_to_radix_buffer);
              AppendString(hex);
            }
          } else {
            // There is no next character. Thus, the current character is a
            // lone leading surrogate.
            AppendCStringLiteral("\\u");
            std::string_view hex =
                DoubleToRadixStringView(c, 16, double_to_radix_buffer);
            AppendString(hex);
          }
        } else {
          // The current character is a lone trailing surrogate. (If it had
          // been preceded by a leading surrogate, we would've ended up in the
          // other branch earlier on, and the current character would've been
          // handled as part of the surrogate pair already.)
          AppendCStringLiteral("\\u");
          std::string_view hex =
              DoubleToRadixStringView(c, 16, double_to_radix_buffer);
          AppendString(hex);
        }
        prev_escaped_offset = i;
      } else {
        required_escaping = true;
        AppendSubstring(vector.data(), prev_escaped_offset + 1, i);
        DCHECK_LT(c, 0x60);
        AppendCString(&JsonEscapeTable[c * kJsonEscapeTableEntrySize]);
        prev_escaped_offset = i;
      }
    }
    AppendSubstring(vector.data(), prev_escaped_offset + 1, vector.size());
  }
  if (!raw_json) Append<uint8_t, DestChar>('"');
  return required_escaping;
}

template <typename DestChar>
bool JsonStringifier::TrySerializeSimplePropertyKey(
    Tagged<String> key, const DisallowGarbageCollection& no_gc) {
  ReadOnlyRoots roots(isolate_);
  if (key->map() != roots.internalized_one_byte_string_map()) {
    return false;
  }
  if (!key_cache_.Contains(key)) {
    return false;
  }
  int length = key->length();
  int copy_length = length;
  if constexpr (sizeof(DestChar) == 1) {
    // CopyChars has fast paths for small integer lengths, and is generally a
    // little faster if we round the length up to the nearest 4. This is still
    // within the bounds of the object on the heap, because object alignment is
    // never less than 4 for any build configuration.
    constexpr int kRounding = 4;
    static_assert(kRounding <= kObjectAlignment);
    copy_length = RoundUp(length, kRounding);
  }
  // Add three for the quote marks and colon, to determine how much output space
  // is needed. We might actually require a little less output space than this,
  // depending on how much rounding happened above, but it's more important to
  // compute the requirement quickly than to be precise.
  int required_length = copy_length + 3;
  if (!CurrentPartCanFit(required_length)) {
    return false;
  }
  NoExtendBuilder<DestChar> no_extend(
      reinterpret_cast<DestChar*>(part_ptr_) + current_index_, &current_index_);
  no_extend.Append('"');
  base::Vector<const uint8_t> chars(
      Cast<SeqOneByteString>(key)->GetChars(no_gc), copy_length);
  DCHECK_LE(reinterpret_cast<Address>(chars.end()),
            key.address() + key->Size());
#if DEBUG
  for (int i = 0; i < length; ++i) {
    DCHECK(DoNotEscape(chars[i]));
  }
#endif  // DEBUG
  no_extend.AppendChars(chars, length);
  no_extend.Append('"');
  no_extend.Append(':');
  return true;
}

void JsonStringifier::NewLine() {
  if (gap_ == nullptr) return;
  NewLineOutline();
}

void JsonStringifier::NewLineOutline() {
  AppendCharacter('\n');
  for (int i = 0; i < indent_; i++) AppendCString(gap_);
}

void JsonStringifier::Separator(bool first) {
  if (!first) AppendCharacter(',');
  NewLine();
}

void JsonStringifier::SerializeDeferredKey(bool deferred_comma,
                                           Handle<Object> deferred_key) {
  Separator(!deferred_comma);
  Handle<String> string_key = Cast<String>(deferred_key);
  bool wrote_simple = false;
  {
    DisallowGarbageCollection no_gc;
    wrote_simple =
        encoding_ == String::ONE_BYTE_ENCODING
            ? TrySerializeSimplePropertyKey<uint8_t>(*string_key, no_gc)
            : TrySerializeSimplePropertyKey<base::uc16>(*string_key, no_gc);
  }

  if (!wrote_simple) {
    bool required_escaping = SerializeString<false>(string_key);
    if (!required_escaping) {
      key_cache_.TryInsert(*string_key);
    }
    AppendCharacter(':');
  }

  if (gap_ != nullptr) AppendCharacter(' ');
}

template <bool raw_json>
bool JsonStringifier::SerializeString(Handle<String> object) {
  object = String::Flatten(isolate_, object);
  DisallowGarbageCollection no_gc;
  auto string = *object;
  if (encoding_ == String::ONE_BYTE_ENCODING) {
    if (string->IsOneByteRepresentation()) {
      return SerializeString_<uint8_t, uint8_t, raw_json>(string, no_gc);
    } else {
      ChangeEncoding();
    }
  }
  DCHECK_EQ(encoding_, String::TWO_BYTE_ENCODING);
  if (string->IsOneByteRepresentation()) {
    return SerializeString_<uint8_t, base::uc16, raw_json>(string, no_gc);
  } else {
    return SerializeString_<base::uc16, base::uc16, raw_json>(string, no_gc);
  }
}

void JsonStringifier::Extend() {
  if (part_length_ >= String::kMaxLength) {
    // Set the flag and carry on. Delay throwing the exception till the end.
    current_index_ = 0;
    overflowed_ = true;
    return;
  }
  part_length_ *= kPartLengthGrowthFactor;
  if (encoding_ == String::ONE_BYTE_ENCODING) {
    uint8_t* tmp_ptr = new uint8_t[part_length_];
    memcpy(tmp_ptr, one_byte_ptr_, current_index_);
    if (one_byte_ptr_ != one_byte_array_) delete[] one_byte_ptr_;
    one_byte_ptr_ = tmp_ptr;
    part_ptr_ = one_byte_ptr_;
  } else {
    base::uc16* tmp_ptr = new base::uc16[part_length_];
    for (uint32_t i = 0; i < current_index_; i++) {
      tmp_ptr[i] = two_byte_ptr_[i];
    }
    delete[] two_byte_ptr_;
    two_byte_ptr_ = tmp_ptr;
    part_ptr_ = two_byte_ptr_;
  }
}

void JsonStringifier::ChangeEncoding() {
  encoding_ = String::TWO_BYTE_ENCODING;
  two_byte_ptr_ = new base::uc16[part_length_];
  for (uint32_t i = 0; i < current_index_; i++) {
    two_byte_ptr_[i] = one_byte_ptr_[i];
  }
  part_ptr_ = two_byte_ptr_;
  if (one_byte_ptr_ != one_byte_array_) delete[] one_byte_ptr_;
  one_byte_ptr_ = nullptr;
}

template <typename Char>
class OutBuffer {
 public:
  explicit OutBuffer(AccountingAllocator* allocator) : allocator_(allocator) {
    cur_ = stack_buffer_;
    segment_end_ = cur_ + kStackBufferSize;
  }
  template <typename SrcChar>
    requires(sizeof(Char) >= sizeof(SrcChar))
  V8_INLINE void AppendCharacter(SrcChar c) {
    DCHECK_GT(SegmentFreeChars(), 0);
    *cur_++ = c;
    if (cur_ == segment_end_) {
      Extend();
    }
  }
  template <typename SrcChar>
    requires(sizeof(Char) >= sizeof(SrcChar))
  void Append(const SrcChar* chars, int length) {
    DCHECK_GT(SegmentFreeChars(), 0);
    if (length <= SegmentFreeChars()) {
      CopyChars(cur_, chars, length);
      cur_ += length;
    } else {
      int cur_length = SegmentFreeChars();
      CopyChars(cur_, chars, cur_length);
      chars += cur_length;
      int remaining_length = length - cur_length;
      while (remaining_length > 0) {
        Extend();
        int copy_length = std::min(remaining_length, kSegmentLength);
        CopyChars(cur_, chars, copy_length);
        chars += copy_length;
        cur_ += copy_length;
        remaining_length -= copy_length;
      }
    }
    if (SegmentFreeChars() <= 0) Extend();
  }
  int length() const {
    if (ZoneUsed()) {
      return kStackBufferSize + (segments_->length() - 1) * kSegmentLength +
             CurSegmentLength();
    } else {
      return StackBufferLength();
    }
  }
  template <typename Dst>
  void CopyTo(Dst* dst) {
    if (ZoneUsed()) {
      // Copy stack segment.
      CopyChars(dst, stack_buffer_, kStackBufferSize);
      dst += kStackBufferSize;
      // Copy full segments.
      for (int i = 0; i < segments_->length() - 1; i++) {
        CopyChars(dst, segments_.value()[i].begin(), kSegmentLength);
        dst += kSegmentLength;
      }
      // Copy last (partially filled) segment.
      base::Vector<Char> segment = segments_->last();
      CopyChars(dst, segment.begin(), CurSegmentLength());
    } else {
      // Copy (partially filled) stack segment.
      CopyChars(dst, stack_buffer_, StackBufferLength());
    }
  }

 private:
  static constexpr int kSegmentLength = 2048;
  static constexpr int kStackBufferSize = 256;

  void Extend() {
    if (!ZoneUsed()) {
      zone_.emplace(allocator_, kJsonStringifierZoneName);
      segments_.emplace(1, &zone_.value());
    }
    segments_->Add(zone_->AllocateVector<Char>(kSegmentLength), &zone_.value());
    cur_ = segments_->last().begin();
    segment_end_ = segments_->last().end();
  }
  V8_INLINE int SegmentFreeChars() const {
    return static_cast<int>(segment_end_ - cur_);
  }
  V8_INLINE int StackBufferLength() const {
    DCHECK(!ZoneUsed());
    return static_cast<int>(cur_ - stack_buffer_);
  }
  V8_INLINE int CurSegmentLength() const {
    DCHECK(ZoneUsed());
    return static_cast<int>(cur_ - segments_->last().begin());
  }
  V8_INLINE bool ZoneUsed() const { return zone_.has_value(); }
  AccountingAllocator* allocator_;
  Char stack_buffer_[kStackBufferSize];
  Char* cur_;
  Char* segment_end_;
  std::optional<Zone> zone_;
  std::optional<ZoneList<base::Vector<Char>>> segments_;
};

struct ContinuationRecord {
  enum Type {
    kObject,
    kArray,
    kResumeFromOther,  // Resume after encoding change or array serialization.
                       // Either object, array or string.
                       // |index| is ignored for this type.
    kObjectKey         // For encoding changes triggered by object keys.
                       // This is required (instead of simply resuming with the
                       // object/index that triggered the change), to avoid
                       // re-serializing '{' if the first property key triggered
                       // the change.
                       // |index| is used to indicate comma requirement
                       // (0 = no comma, otherwise add comma).
  };
  Type type;
  Tagged<JSAny> object;
  uint32_t index;
};

enum FastJsonStringifierResult {
  SUCCESS,
  UNCHANGED,
  UNDEFINED,
  CHANGE_ENCODING,
  SLOW_PATH,
  EXCEPTION
};

template <typename Char>
class FastJsonStringifier {
 public:
  explicit FastJsonStringifier(Isolate* isolate);

  int ResultLength() const { return buffer_.length(); }
  template <typename DstChar>
  void CopyResultTo(DstChar* out_buffer) {
    buffer_.CopyTo(out_buffer);
  }
  V8_INLINE FastJsonStringifierResult
  SerializeObject(Tagged<JSAny> object, const DisallowGarbageCollection& no_gc);

  template <typename OldChar>
    requires(sizeof(OldChar) < sizeof(Char))
  FastJsonStringifierResult ResumeFrom(
      FastJsonStringifier<OldChar>& old_stringifier,
      const DisallowGarbageCollection& no_gc);

 private:
  static constexpr bool is_one_byte = sizeof(Char) == sizeof(uint8_t);

  V8_INLINE void Separator(bool comma) {
    if (comma) AppendCharacter(',');
  }
  V8_INLINE void SerializeSmi(Tagged<Smi> object);
  void SerializeDouble(double number);
  V8_INLINE FastJsonStringifierResult SerializeObjectKey(
      Tagged<String> key, bool comma, const DisallowGarbageCollection& no_gc);
  template <typename StringT>
  V8_INLINE FastJsonStringifierResult SerializeObjectKey(
      Tagged<String> key, bool comma, const DisallowGarbageCollection& no_gc);
  template <typename StringT, bool deferred_key>
  V8_INLINE FastJsonStringifierResult
  SerializeString(Tagged<HeapObject> str, bool comma, Tagged<String> key,
                  const DisallowGarbageCollection& no_gc);

  template <bool deferred_key>
  V8_INLINE FastJsonStringifierResult TrySerializeSimpleObject(
      Tagged<JSAny> object, bool comma = false, Tagged<String> key = {});
  FastJsonStringifierResult SerializeObject(Tagged<JSAny> object,
                                            uint32_t obj_cont_idx,
                                            uint32_t array_cont_idx);
  template <bool deferred_key>
  FastJsonStringifierResult SerializeJSPrimitiveWrapper(
      Tagged<JSPrimitiveWrapper> obj, bool comma, Tagged<String> key,
      const DisallowGarbageCollection& no_gc);
  V8_INLINE FastJsonStringifierResult
  SerializeJSObject(Tagged<JSObject> obj, uint32_t start_idx,
                    const DisallowGarbageCollection& no_gc);
  FastJsonStringifierResult SerializeJSArray(Tagged<JSArray> array,
                                             uint32_t start_idx);
  template <ElementsKind kind>
  V8_INLINE FastJsonStringifierResult SerializeFixedArrayWithInterruptCheck(
      Tagged<JSArray> array, uint32_t length, uint32_t start_index,
      uint32_t* bailout_idx = nullptr);
  template <ElementsKind kind>
  V8_INLINE FastJsonStringifierResult SerializeFixedArray(Tagged<JSArray> array,
                                                          uint32_t length,
                                                          uint32_t start_index);
  template <ElementsKind kind, typename T>
  V8_INLINE FastJsonStringifierResult SerializeFixedArrayElement(
      Tagged<T> elements, uint32_t i, Tagged<JSArray> array);

  FastJsonStringifierResult HandleInterruptAndCheckCycle();
  bool CheckCycle();

  template <typename SrcChar>
  V8_INLINE void AppendCharacter(SrcChar c) {
    buffer_.AppendCharacter(c);
  }

  template <int N>
  V8_INLINE void AppendCStringLiteral(const char (&literal)[N]) {
    // Note that the literal contains the zero char.
    constexpr int length = N - 1;
    static_assert(length > 0);
    if constexpr (length == 1) return AppendCharacter(literal[0]);
    const uint8_t* chars = reinterpret_cast<const uint8_t*>(literal);
    buffer_.Append(chars, length);
  }

  V8_INLINE void AppendCString(const char* chars, size_t len) {
    buffer_.Append(reinterpret_cast<const unsigned char*>(chars),
                   static_cast<int>(len));
  }
  V8_INLINE void AppendCString(const char* chars) {
    AppendCString(chars, strlen(chars));
  }
  V8_INLINE void AppendString(std::string_view str) {
    AppendCString(str.data(), str.length());
  }

  V8_INLINE FastJsonStringifierResult
  AppendStringChecked(Tagged<String> string);

  template <typename SrcChar>
    requires(sizeof(SrcChar) == sizeof(uint8_t))
  V8_INLINE FastJsonStringifierResult
  AppendStringChecked(const SrcChar* chars, uint32_t length,
                      const DisallowGarbageCollection& no_gc);

  template <typename SrcChar>
    requires(sizeof(SrcChar) == sizeof(uint8_t))
  V8_INLINE FastJsonStringifierResult AppendStringCheckedScalar(
      const SrcChar* chars, uint32_t length, uint32_t start,
      uint32_t uncopied_src_index, const DisallowGarbageCollection& no_gc);

  template <typename SrcChar>
    requires(sizeof(SrcChar) == sizeof(uint8_t))
  V8_INLINE FastJsonStringifierResult
  AppendStringCheckedSIMD(const SrcChar* chars, uint32_t length,
                          const DisallowGarbageCollection& no_gc);

  template <typename SrcChar>
    requires(sizeof(SrcChar) == sizeof(base::uc16))
  V8_INLINE FastJsonStringifierResult
  AppendStringChecked(const SrcChar* chars, uint32_t length,
                      const DisallowGarbageCollection& no_gc);

  static constexpr uint32_t kGlobalInterruptBudget = 200000;
  static constexpr uint32_t kArrayInterruptLength = 4000;

  Isolate* isolate_;
  OutBuffer<Char> buffer_;
  std::vector<ContinuationRecord> stack_;

  template <typename>
  friend class FastJsonStringifier;
};

namespace {

V8_INLINE bool CanFastSerializeJSArrayFastPath(Tagged<JSArray> object,
                                               Isolate* isolate) {
  // Check if the prototype is the initial array prototype without interesting
  // properties (toJSON).
  Tagged<Map> map = object->map();
  if (map->may_have_interesting_properties()) return false;
  Tagged<NativeContext> native_context = map->map()->native_context();
  Tagged<HeapObject> proto = map->prototype();
  if (native_context->get(Context::INITIAL_ARRAY_PROTOTYPE_INDEX) != proto) {
    return false;
  }
  Tagged<Map> proto_map = proto->map();
  if (proto_map->may_have_interesting_properties()) {
    return false;
  }
  // Additionally check if the Array prototype is the initial object prototype
  // without interesting properties (toJSON).
  proto = proto_map->prototype();
  if (native_context->get(Context::INITIAL_OBJECT_PROTOTYPE_INDEX) != proto) {
    return false;
  }
  return !proto->map()->may_have_interesting_properties();
}

V8_INLINE bool CanFastSerializeJSObjectFastPath(Tagged<JSObject> object,
                                                Tagged<Map> map,
                                                Isolate* isolate) {
  if (IsCustomElementsReceiverMap(map)) return false;
  if (!object->HasFastProperties()) return false;
  auto roots = ReadOnlyRoots(isolate);
  auto elements = object->elements();
  if (elements != roots.empty_fixed_array() &&
      elements != roots.empty_slow_element_dictionary()) {
    return false;
  }
  if (map->may_have_interesting_properties()) return false;
  Tagged<NativeContext> native_context = map->map()->native_context();
  Tagged<HeapObject> proto = map->prototype();
  if (native_context->get(Context::INITIAL_OBJECT_PROTOTYPE_INDEX) != proto) {
    return false;
  }
  return !proto->map()->may_have_interesting_properties();
}

}  // namespace

template <typename Char>
FastJsonStringifier<Char>::FastJsonStringifier(Isolate* isolate)
    : isolate_(isolate), buffer_(isolate->allocator()) {}

template <typename Char>
void FastJsonStringifier<Char>::SerializeSmi(Tagged<Smi> object) {
  static_assert(Smi::kMaxValue <= 2147483647);
  static_assert(Smi::kMinValue >= -2147483648);
  // sizeof(string) includes \0.
  static constexpr int kBufferSize = sizeof("-2147483648") - 1;
  char chars[kBufferSize];
  base::Vector<char> buffer(chars, kBufferSize);
  AppendString(IntToStringView(object.value(), buffer));
}

template <typename Char>
void FastJsonStringifier<Char>::SerializeDouble(double number) {
  if (std::isinf(number) || std::isnan(number)) {
    AppendCStringLiteral("null");
    return;
  }
  static constexpr int kBufferSize = 100;
  char chars[kBufferSize];
  base::Vector<char> buffer(chars, kBufferSize);
  std::string_view str = DoubleToStringView(number, buffer);
  AppendString(str);
}

template <typename Char>
FastJsonStringifierResult FastJsonStringifier<Char>::SerializeObjectKey(
    Tagged<String> key, bool comma, const DisallowGarbageCollection& no_gc) {
#if V8_STATIC_ROOTS_BOOL
  // This is slightly faster than the switch over instance types.
  ReadOnlyRoots roots(isolate_);
  Tagged<Map> map = key->map();
  if (map == roots.internalized_one_byte_string_map()) {
    return SerializeObjectKey<SeqOneByteString>(key, comma, no_gc);
  } else if (map == roots.external_internalized_one_byte_string_map() ||
             map ==
                 roots.uncached_external_internalized_one_byte_string_map()) {
    return SerializeObjectKey<ExternalOneByteString>(key, comma, no_gc);
  } else {
    if constexpr (is_one_byte) {
      if (InstanceTypeChecker::IsTwoByteString(map)) {
        return CHANGE_ENCODING;
      }
    } else {
      if (map == roots.internalized_two_byte_string_map()) {
        return SerializeObjectKey<SeqTwoByteString>(key, comma, no_gc);
      } else if (
          map == roots.external_internalized_two_byte_string_map() ||
          map == roots.uncached_external_internalized_two_byte_string_map()) {
        return SerializeObjectKey<ExternalTwoByteString>(key, comma, no_gc);
      }
    }
  }
#else
  InstanceType instance_type = key->map()->instance_type();
  switch (instance_type) {
    case INTERNALIZED_ONE_BYTE_STRING_TYPE:
      return SerializeObjectKey<SeqOneByteString>(key, comma, no_gc);
    case EXTERNAL_INTERNALIZED_ONE_BYTE_STRING_TYPE:
    case UNCACHED_EXTERNAL_INTERNALIZED_ONE_BYTE_STRING_TYPE:
      return SerializeObjectKey<ExternalOneByteString>(key, comma, no_gc);
    case INTERNALIZED_TWO_BYTE_STRING_TYPE:
      return SerializeObjectKey<SeqTwoByteString>(key, comma, no_gc);
    case EXTERNAL_INTERNALIZED_TWO_BYTE_STRING_TYPE:
    case UNCACHED_EXTERNAL_INTERNALIZED_TWO_BYTE_STRING_TYPE:
      return SerializeObjectKey<ExternalTwoByteString>(key, comma, no_gc);
    default:
      UNREACHABLE();
  }
#endif
  UNREACHABLE();
}

template <typename Char>
template <typename StringT>
FastJsonStringifierResult FastJsonStringifier<Char>::SerializeObjectKey(
    Tagged<String> obj, bool comma, const DisallowGarbageCollection& no_gc) {
  using StringChar = StringT::Char;
  if constexpr (is_one_byte && sizeof(StringChar) == 2) {
    return CHANGE_ENCODING;
  } else {
    Tagged<StringT> string = Cast<StringT>(obj);
    const StringChar* chars;
    if constexpr (requires { string->GetChars(no_gc); }) {
      chars = string->GetChars(no_gc);
    } else {
      chars = string->GetChars();
    }
    const uint32_t length = string->length();
    Separator(comma);
    AppendCharacter('"');
    AppendStringChecked(chars, length, no_gc);
    AppendCharacter('"');
    AppendCharacter(':');
    return SUCCESS;
  }
}

template <typename Char>
template <typename StringT, bool deferred_key>
FastJsonStringifierResult FastJsonStringifier<Char>::SerializeString(
    Tagged<HeapObject> obj, bool comma, Tagged<String> key,
    const DisallowGarbageCollection& no_gc) {
  using StringChar = StringT::Char;
  if constexpr (is_one_byte && sizeof(StringChar) == 2) {
    return CHANGE_ENCODING;
  } else {
    Tagged<StringT> string = Cast<StringT>(obj);
    const StringChar* chars;
    if constexpr (requires { string->GetChars(no_gc); }) {
      chars = string->GetChars(no_gc);
    } else {
      chars = string->GetChars();
    }
    const uint32_t length = string->length();
    if constexpr (deferred_key) {
      FastJsonStringifierResult result = SerializeObjectKey(key, comma, no_gc);
      if (V8_UNLIKELY(result != SUCCESS)) return result;
    }
    AppendCharacter('"');
    AppendStringChecked(chars, length, no_gc);
    AppendCharacter('"');
    return SUCCESS;
  }
}

template <typename Char>
template <bool deferred_key>
FastJsonStringifierResult FastJsonStringifier<Char>::TrySerializeSimpleObject(
    Tagged<JSAny> object, bool comma, Tagged<String> key) {
  DisallowGarbageCollection no_gc;
  // GCMole doesn't handle kNoGC interrupts correctly.
  DisableGCMole no_gc_mole;

  if (IsSmi(object)) {
    if constexpr (deferred_key) {
      FastJsonStringifierResult result = SerializeObjectKey(key, comma, no_gc);
      if (V8_UNLIKELY(result != SUCCESS)) return result;
    }
    SerializeSmi(Cast<Smi>(object));
    return SUCCESS;
  }
  Tagged<HeapObject> obj = Cast<HeapObject>(object);
  Tagged<Map> map = obj->map();
  InstanceType instance_type = map->instance_type();
  switch (instance_type) {
    case INTERNALIZED_ONE_BYTE_STRING_TYPE:
    case SEQ_ONE_BYTE_STRING_TYPE:
      return SerializeString<SeqOneByteString, deferred_key>(obj, comma, key,
                                                             no_gc);
    case EXTERNAL_INTERNALIZED_ONE_BYTE_STRING_TYPE:
    case UNCACHED_EXTERNAL_INTERNALIZED_ONE_BYTE_STRING_TYPE:
    case EXTERNAL_ONE_BYTE_STRING_TYPE:
    case UNCACHED_EXTERNAL_ONE_BYTE_STRING_TYPE:
      return SerializeString<ExternalOneByteString, deferred_key>(obj, comma,
                                                                  key, no_gc);
    case THIN_ONE_BYTE_STRING_TYPE: {
      Tagged<String> actual = Cast<ThinString>(obj)->actual();
      if (IsExternalString(actual)) {
        return SerializeString<ExternalOneByteString, deferred_key>(
            actual, comma, key, no_gc);
      } else {
        return SerializeString<SeqOneByteString, deferred_key>(actual, comma,
                                                               key, no_gc);
      }
    }
    case INTERNALIZED_TWO_BYTE_STRING_TYPE:
    case SEQ_TWO_BYTE_STRING_TYPE:
      return SerializeString<SeqTwoByteString, deferred_key>(obj, comma, key,
                                                             no_gc);
    case EXTERNAL_INTERNALIZED_TWO_BYTE_STRING_TYPE:
    case UNCACHED_EXTERNAL_INTERNALIZED_TWO_BYTE_STRING_TYPE:
    case EXTERNAL_TWO_BYTE_STRING_TYPE:
    case UNCACHED_EXTERNAL_TWO_BYTE_STRING_TYPE:
      return SerializeString<ExternalTwoByteString, deferred_key>(obj, comma,
                                                                  key, no_gc);
    case THIN_TWO_BYTE_STRING_TYPE: {
      Tagged<String> actual = Cast<ThinString>(obj)->actual();
      if (IsExternalString(actual)) {
        return SerializeString<ExternalTwoByteString, deferred_key>(
            actual, comma, key, no_gc);
      } else {
        return SerializeString<SeqTwoByteString, deferred_key>(actual, comma,
                                                               key, no_gc);
      }
    }
    case HEAP_NUMBER_TYPE:
      if constexpr (deferred_key) {
        FastJsonStringifierResult result =
            SerializeObjectKey(key, comma, no_gc);
        if (V8_UNLIKELY(result != SUCCESS)) return result;
      }
      SerializeDouble(Cast<HeapNumber>(obj)->value());
      return SUCCESS;
    case ODDBALL_TYPE:
      switch (Cast<Oddball>(obj)->kind()) {
        case Oddball::kFalse:
          if constexpr (deferred_key) {
            FastJsonStringifierResult result =
                SerializeObjectKey(key, comma, no_gc);
            if (V8_UNLIKELY(result != SUCCESS)) return result;
          }
          AppendCStringLiteral("false");
          return SUCCESS;
        case Oddball::kTrue:
          if constexpr (deferred_key) {
            FastJsonStringifierResult result =
                SerializeObjectKey(key, comma, no_gc);
            if (V8_UNLIKELY(result != SUCCESS)) return result;
          }
          AppendCStringLiteral("true");
          return SUCCESS;
        case Oddball::kNull:
          if constexpr (deferred_key) {
            FastJsonStringifierResult result =
                SerializeObjectKey(key, comma, no_gc);
            if (V8_UNLIKELY(result != SUCCESS)) return result;
          }
          AppendCStringLiteral("null");
          return SUCCESS;
        default:
          return UNDEFINED;
      }
    case SYMBOL_TYPE:
      return UNDEFINED;
    case JS_PRIMITIVE_WRAPPER_TYPE:
      return SerializeJSPrimitiveWrapper<deferred_key>(
          Cast<JSPrimitiveWrapper>(obj), comma, key, no_gc);
    case JS_OBJECT_TYPE:
    case JS_ARRAY_TYPE:
      return UNCHANGED;
    default:
      return SLOW_PATH;
  }

  UNREACHABLE();
}

namespace {

Builtin GetBuiltin(Isolate* isolate, Tagged<JSObject> obj,
                   DirectHandle<Name> name) {
  HandleScope handle_scope(isolate);

  DirectHandle<JSObject> obj_handle = direct_handle(obj, isolate);
  LookupIterator it(isolate, obj_handle, name, obj_handle);
  if (!it.IsFound()) {
    return Builtin::kNoBuiltinId;
  }
  if (it.state() != LookupIterator::DATA) {
    return Builtin::kNoBuiltinId;
  }
  DirectHandle<Object> fun = Object::GetProperty(&it).ToHandleChecked();
  if (!IsJSFunction(*fun)) {
    return Builtin::kNoBuiltinId;
  }
  return Cast<JSFunction>(*fun)->code(isolate)->builtin_id();
}

}  // namespace

template <typename Char>
template <bool deferred_key>
FastJsonStringifierResult
FastJsonStringifier<Char>::SerializeJSPrimitiveWrapper(
    Tagged<JSPrimitiveWrapper> obj, bool comma, Tagged<String> key,
    const DisallowGarbageCollection& no_gc) {
  // TODO(pthier): Consider extending IsStringWrapperToPrimitive to also cover
  // toString() and all JSPrimitiveWrappers to avoid some of the checks here.
  if (V8_UNLIKELY(MayHaveInterestingProperties(isolate_, obj))) {
    return SLOW_PATH;
  }

  Tagged<JSAny> raw = obj->value();
  if (IsSymbol(raw)) {
    // Symbol object wrapper is treated as a regular object.
    Tagged<Map> map = obj->map();
    // Check that object has no own properties.
    if (V8_UNLIKELY(!obj->HasFastProperties())) return SLOW_PATH;
    if (V8_UNLIKELY(map->NumberOfOwnDescriptors() != 0)) return SLOW_PATH;
    // Check that object has no elements.
    auto roots = ReadOnlyRoots(isolate_);
    auto elements = obj->elements();
    if (V8_UNLIKELY(elements != roots.empty_fixed_array() &&
                    elements != roots.empty_slow_element_dictionary())) {
      return SLOW_PATH;
    }

    if constexpr (deferred_key) {
      FastJsonStringifierResult result = SerializeObjectKey(key, comma, no_gc);
      if (V8_UNLIKELY(result != SUCCESS)) return result;
    }
    AppendCStringLiteral("{}");
    return SUCCESS;
  } else if (IsString(raw)) {
    if (V8_UNLIKELY(!Protectors::IsStringWrapperToPrimitiveIntact(isolate_))) {
      return SLOW_PATH;
    }
    // We only fetch data properties in GetBuiltin, but GCMole doesn't know
    // that and assumes GCs can happen.
    DisableGCMole no_gc_mole;
    // Check that toString() on the prototype chain is the expected builtin.
    if (V8_UNLIKELY(
            GetBuiltin(isolate_, obj, isolate_->factory()->toString_string()) !=
            Builtin::kStringPrototypeToString)) {
      return SLOW_PATH;
    }

    Tagged<String> string = Cast<String>(raw);
    while (true) {
      FastJsonStringifierResult result =
          string->DispatchToSpecificType(base::overloaded{
              [&](Tagged<SeqOneByteString> str) {
                return SerializeString<SeqOneByteString, deferred_key>(
                    str, comma, key, no_gc);
              },
              [&](Tagged<SeqTwoByteString> str) {
                return SerializeString<SeqTwoByteString, deferred_key>(
                    str, comma, key, no_gc);
              },
              [&](Tagged<ExternalOneByteString> str) {
                return SerializeString<ExternalOneByteString, deferred_key>(
                    str, comma, key, no_gc);
              },
              [&](Tagged<ExternalTwoByteString> str) {
                return SerializeString<ExternalTwoByteString, deferred_key>(
                    str, comma, key, no_gc);
              },
              [&](Tagged<ThinString> str) {
                string = str->actual();
                return UNCHANGED;
              },
              [&](Tagged<ConsString> str) { return SLOW_PATH; },
              [&](Tagged<SlicedString> str) { return SLOW_PATH; }});
      if (V8_LIKELY(result != UNCHANGED)) return result;
    }
    UNREACHABLE();
  } else if (IsNumber(raw)) {
    // We only fetch data properties in GetBuiltin, but GCMole doesn't know
    // that and assumes GCs can happen.
    DisableGCMole no_gc_mole;
    // Check that valueOf() on the prototype chain is the expected builtin.
    if (V8_UNLIKELY(
            GetBuiltin(isolate_, obj, isolate_->factory()->valueOf_string()) !=
            Builtin::kNumberPrototypeValueOf)) {
      return SLOW_PATH;
    }
    if constexpr (deferred_key) {
      FastJsonStringifierResult result = SerializeObjectKey(key, comma, no_gc);
      if (V8_UNLIKELY(result != SUCCESS)) return result;
    }
    if (IsSmi(raw)) {
      SerializeSmi(Cast<Smi>(raw));
      return SUCCESS;
    }
    DCHECK(IsHeapNumber(raw));
    SerializeDouble(Cast<HeapNumber>(raw)->value());
    return SUCCESS;
  } else if (IsBoolean(raw)) {
    if constexpr (deferred_key) {
      FastJsonStringifierResult result = SerializeObjectKey(key, comma, no_gc);
      if (V8_UNLIKELY(result != SUCCESS)) return result;
    }
    if (IsTrue(raw, isolate_)) {
      AppendCStringLiteral("true");
    } else {
      AppendCStringLiteral("false");
    }
    return SUCCESS;
  } else if (IsBigInt(raw)) {
    return SLOW_PATH;
  }

  UNREACHABLE();
}

template <typename Char>
FastJsonStringifierResult FastJsonStringifier<Char>::SerializeJSObject(
    Tagged<JSObject> obj, uint32_t start_idx,
    const DisallowGarbageCollection& no_gc) {
  Tagged<Map> map = obj->map();
  if (V8_UNLIKELY(!CanFastSerializeJSObjectFastPath(obj, map, isolate_))) {
    return SLOW_PATH;
  }
  if (map->NumberOfOwnDescriptors() == 0) {
    AppendCStringLiteral("{}");
    return SUCCESS;
  }
  size_t length = map->NumberOfOwnDescriptors();
  if (start_idx < length) {
    bool comma = true;
    if (start_idx == 0) {
      AppendCharacter('{');
      comma = false;
    }
    Tagged<DescriptorArray> descriptors = map->instance_descriptors();
    InternalIndex::Range range{start_idx, length};
    for (InternalIndex i : range) {
      PropertyDetails details = PropertyDetails::Empty();
      Tagged<Name> name = descriptors->GetKey(i);
      if (V8_UNLIKELY(IsSymbol(name))) {
        continue;
      }
      DCHECK(IsInternalizedString(name));
      details = descriptors->GetDetails(i);
      if (V8_UNLIKELY(details.IsDontEnum())) {
        continue;
      }
      if (V8_UNLIKELY(details.location() != PropertyLocation::kField)) {
        return SLOW_PATH;
      }
      DCHECK_EQ(PropertyKind::kData, details.kind());
      FieldIndex field_index = FieldIndex::ForDetails(map, details);
      Tagged<JSAny> property = obj->RawFastPropertyAt(field_index);
      Tagged<String> key_name = Cast<String>(name);
      FastJsonStringifierResult result =
          TrySerializeSimpleObject<true>(property, comma, key_name);
      switch (result) {
        case SUCCESS:
          comma = true;
          break;
        case UNDEFINED:
          break;
        case UNCHANGED:
          stack_.emplace_back(ContinuationRecord::kObject, obj,
                              i.as_uint32() + 1);
          // property can be an object or array. We don't need to distinguish
          // as index is 0 anyways.
          stack_.emplace_back(ContinuationRecord::kObject, property, 0);
          result = SerializeObjectKey(key_name, comma, no_gc);
          if constexpr (is_one_byte) {
            if (V8_UNLIKELY(result != SUCCESS)) {
              DCHECK_EQ(result, CHANGE_ENCODING);
              stack_.emplace_back(ContinuationRecord::kObjectKey, key_name,
                                  comma);
              return result;
            }
          }
          return result;
        case CHANGE_ENCODING:
          DCHECK(is_one_byte);
          stack_.emplace_back(ContinuationRecord::kObject, obj,
                              i.as_uint32() + 1);
          stack_.emplace_back(ContinuationRecord::kResumeFromOther, property,
                              0);
          result = SerializeObjectKey(key_name, comma, no_gc);
          if (V8_UNLIKELY(result != SUCCESS)) {
            stack_.emplace_back(ContinuationRecord::kObjectKey, key_name,
                                comma);
            return result;
          }
          DCHECK(IsString(property));
          return result;
        case SLOW_PATH:
        case EXCEPTION:
          return result;
      }
    }
  }
  AppendCharacter('}');
  return SUCCESS;
}

template <typename Char>
FastJsonStringifierResult FastJsonStringifier<Char>::SerializeJSArray(
    Tagged<JSArray> array, uint32_t start_idx) {
  if (V8_UNLIKELY(!CanFastSerializeJSArrayFastPath(array, isolate_))) {
    return SLOW_PATH;
  }
  uint32_t length = 0;
  CHECK(Object::ToArrayLength(array->length(), &length));
  if (length == 0) {
    AppendCStringLiteral("[]");
    return SUCCESS;
  }
  FastJsonStringifierResult result = SUCCESS;
  if (start_idx < length) {
    if (start_idx == 0) {
      AppendCharacter('[');
    }
    switch (array->GetElementsKind()) {
#define CASE(kind)                                                        \
  case kind:                                                              \
    if constexpr (IsHoleyElementsKind(kind)) {                            \
      if (V8_UNLIKELY(!Protectors::IsNoElementsIntact(isolate_))) {       \
        return SLOW_PATH;                                                 \
      }                                                                   \
    }                                                                     \
    if (V8_UNLIKELY(length > kArrayInterruptLength)) {                    \
      result = SerializeFixedArrayWithInterruptCheck<kind>(array, length, \
                                                           start_idx);    \
    } else {                                                              \
      result = SerializeFixedArray<kind>(array, length, start_idx);       \
    }                                                                     \
    break;
      CASE(PACKED_SMI_ELEMENTS)
      CASE(PACKED_ELEMENTS)
      CASE(PACKED_DOUBLE_ELEMENTS)
      CASE(HOLEY_SMI_ELEMENTS)
      CASE(HOLEY_ELEMENTS)
      CASE(HOLEY_DOUBLE_ELEMENTS)
#undef CASE
      default:
        return SLOW_PATH;
    }
  }
  if (result == SUCCESS) {
    AppendCharacter(']');
  }
  return result;
}

template <typename Char>
template <ElementsKind kind>
FastJsonStringifierResult
FastJsonStringifier<Char>::SerializeFixedArrayWithInterruptCheck(
    Tagged<JSArray> array, uint32_t length, uint32_t start_index,
    uint32_t* bailout_idx) {
  using ArrayT = std::conditional_t<IsDoubleElementsKind(kind),
                                    FixedDoubleArray, FixedArray>;

  StackLimitCheck interrupt_check(isolate_);
  uint32_t limit = std::min(length, kArrayInterruptLength);
  constexpr uint32_t kMaxAllowedFastPackedLength =
      std::numeric_limits<uint32_t>::max() - kArrayInterruptLength;
  static_assert(FixedArray::kMaxLength < kMaxAllowedFastPackedLength);

  // GCMole doesn't handle kNoGC interrupts correctly.
  DisableGCMole no_gc_mole;

  uint32_t i = start_index;
  while (true) {
    for (; i < limit; i++) {
      FastJsonStringifierResult result = SerializeFixedArrayElement<kind>(
          Cast<ArrayT>(array->elements()), i, array);
      if (result != SUCCESS) return result;
    }
    if (i >= length) return SUCCESS;
    DCHECK_LT(limit, kMaxAllowedFastPackedLength);
    limit = std::min(length, limit + kArrayInterruptLength);
    {
      // A TerminationException can actually trigger a GC when allocating the
      // message object. We don't touch any of the heap objects after we
      // encountered an exception, so this is fine.
      AllowGarbageCollection allow_gc;
      if (interrupt_check.InterruptRequested() &&
          IsException(isolate_->stack_guard()->HandleInterrupts(
                          StackGuard::InterruptLevel::kNoGC),
                      isolate_)) {
        return EXCEPTION;
      }
    }
  }
  return SUCCESS;
}

template <typename Char>
template <ElementsKind kind>
FastJsonStringifierResult FastJsonStringifier<Char>::SerializeFixedArray(
    Tagged<JSArray> array, uint32_t limit, uint32_t start_index) {
  using ArrayT = std::conditional_t<IsDoubleElementsKind(kind),
                                    FixedDoubleArray, FixedArray>;

  for (uint32_t i = start_index; i < limit; i++) {
    FastJsonStringifierResult result = SerializeFixedArrayElement<kind>(
        Cast<ArrayT>(array->elements()), i, array);
    if (result != SUCCESS) return result;
  }
  return SUCCESS;
}

template <typename Char>
template <ElementsKind kind, typename T>
FastJsonStringifierResult FastJsonStringifier<Char>::SerializeFixedArrayElement(
    Tagged<T> elements, uint32_t i, Tagged<JSArray> array) {
  if constexpr (IsHoleyElementsKind(kind)) {
    if (elements->is_the_hole(isolate_, i)) {
      Separator(i > 0);
      AppendCStringLiteral("null");
      return SUCCESS;
    }
  }
  DCHECK(!elements->is_the_hole(isolate_, i));
  Separator(i > 0);
  if constexpr (IsSmiElementsKind(kind)) {
    SerializeSmi(Cast<Smi>(elements->get(i)));
  } else if constexpr (IsDoubleElementsKind(kind)) {
    SerializeDouble(elements->get_scalar(i));
  } else {
    Tagged<JSAny> obj = Cast<JSAny>(elements->get(i));
    FastJsonStringifierResult result = TrySerializeSimpleObject<false>(obj);
    switch (result) {
      case UNDEFINED:
        AppendCStringLiteral("null");
        return SUCCESS;
      case CHANGE_ENCODING:
        DCHECK(IsString(obj));
        stack_.emplace_back(ContinuationRecord::kArray, array, i + 1);
        stack_.emplace_back(ContinuationRecord::kResumeFromOther, obj, 0);
        return result;
      case UNCHANGED:
        stack_.emplace_back(ContinuationRecord::kArray, array, i + 1);
        stack_.emplace_back(ContinuationRecord::kResumeFromOther, obj, 0);
        return result;
      default:
        return result;
    }
  }
  return SUCCESS;
}

template <typename Char>
template <typename OldChar>
  requires(sizeof(OldChar) < sizeof(Char))
FastJsonStringifierResult FastJsonStringifier<Char>::ResumeFrom(
    FastJsonStringifier<OldChar>& old_stringifier,
    const DisallowGarbageCollection& no_gc) {
  DCHECK_EQ(ResultLength(), 0);
  DCHECK(stack_.empty());
  DCHECK(!old_stringifier.stack_.empty());

  stack_ = old_stringifier.stack_;
  ContinuationRecord cont = stack_.back();
  stack_.pop_back();
  DCHECK(cont.type == ContinuationRecord::kResumeFromOther ||
         cont.type == ContinuationRecord::kObjectKey);
  Tagged<JSAny> object = cont.object;
  FastJsonStringifierResult result;
  DCHECK(IsString(object));
  if (cont.type == ContinuationRecord::kObjectKey) {
    // Serializing an object key caused an encoding change.
    result = SerializeObjectKey(Cast<String>(object), cont.index, no_gc);
    DCHECK_EQ(result, SUCCESS);
    // Resuming due to encoding change of an object key guarantees that there
    // are at least two other objects on the stack (the value for that key, and
    // the continuation record for the object the key is a member of).
    DCHECK_GE(stack_.size(), 2);
    cont = stack_.back();
    stack_.pop_back();
    // Check that we have the object's continuation record.
    DCHECK_EQ(stack_.back().type, ContinuationRecord::kObject);
  }
  if (cont.type == ContinuationRecord::kResumeFromOther) {
    // Multiple scenarios lead here:
    // 1) The object to serialize was a string on the top-level, which triggered
    //    an encoding change.
    // 2) A string value in an array or object triggered an encoding change.
    // 3) Fall-through from the object key case above.
    object = cont.object;
    result = TrySerializeSimpleObject<false>(object);
    DCHECK_EQ(result, SUCCESS);

    if (stack_.empty()) return result;

    cont = stack_.back();
    DCHECK(cont.type == ContinuationRecord::kObject ||
           cont.type == ContinuationRecord::kArray);
    stack_.pop_back();
    uint32_t obj_cont_idx =
        cont.type == ContinuationRecord::kObject ? cont.index : 0;
    uint32_t array_cont_idx =
        cont.type == ContinuationRecord::kArray ? cont.index : 0;
    return SerializeObject(cont.object, obj_cont_idx, array_cont_idx);
  } else {
    // Object can be JSArray or JSObject. Right now this is only possible after
    // encoding change of an object's key, in which case we push a continuation
    // of kObject even for JSArrays (as index is 0 anyways).
    DCHECK_EQ(cont.type, ContinuationRecord::kObject);
    DCHECK_EQ(cont.index, 0);
    return SerializeObject(cont.object, 0, 0);
  }
}

template <typename Char>
FastJsonStringifierResult FastJsonStringifier<Char>::SerializeObject(
    Tagged<JSAny> object, const DisallowGarbageCollection& no_gc) {
  FastJsonStringifierResult result = TrySerializeSimpleObject<false>(object);
  if constexpr (is_one_byte) {
    if (V8_UNLIKELY(result == CHANGE_ENCODING)) {
      DCHECK(IsString(object));
      stack_.emplace_back(ContinuationRecord::kResumeFromOther, object, 0);
      return result;
    }
  } else {
    DCHECK_NE(result, CHANGE_ENCODING);
  }
  if (result != UNCHANGED) {
    return result;
  }
  return SerializeObject(object, 0, 0);
}

template <typename Char>
FastJsonStringifierResult FastJsonStringifier<Char>::SerializeObject(
    Tagged<JSAny> object, uint32_t obj_cont_idx, uint32_t array_cont_idx) {
  DisallowGarbageCollection no_gc;
  // GCMole doesn't handle kNoGC interrupts correctly.
  DisableGCMole no_gc_mole;

  uint32_t interrupt_budget = kGlobalInterruptBudget;

  FastJsonStringifierResult result;
  while (true) {
    if (--interrupt_budget == 0) {
      result = HandleInterruptAndCheckCycle();
      if (result != SUCCESS) return result;
      interrupt_budget = kGlobalInterruptBudget;
    }
    Tagged<HeapObject> obj = Cast<HeapObject>(object);
    Tagged<Map> map = obj->map();
    InstanceType instance_type = map->instance_type();
    switch (instance_type) {
      case JS_OBJECT_TYPE: {
        result = SerializeJSObject(Cast<JSObject>(obj), obj_cont_idx, no_gc);
        break;
      }
      case JS_ARRAY_TYPE: {
        result = SerializeJSArray(Cast<JSArray>(obj), array_cont_idx);
        break;
      }
      default:
        return SLOW_PATH;
    }
    static_assert(SUCCESS == 0);
    static_assert(UNCHANGED == 1);
    if (V8_UNLIKELY(result > UNCHANGED)) return result;
    if (stack_.empty()) return SUCCESS;
    ContinuationRecord cont = stack_.back();
    object = cont.object;
    obj_cont_idx = cont.type == ContinuationRecord::kObject ? cont.index : 0;
    array_cont_idx = cont.type == ContinuationRecord::kArray ? cont.index : 0;
    stack_.pop_back();
  }
}

template <typename Char>
FastJsonStringifierResult
FastJsonStringifier<Char>::HandleInterruptAndCheckCycle() {
  StackLimitCheck interrupt_check(isolate_);
  {
    // A TerminationException can actually trigger a GC when allocating the
    // message object. We don't touch any of the heap objects after we
    // encountered an exception, so this is fine.
    AllowGarbageCollection allow_gc;
    if (interrupt_check.InterruptRequested() &&
        IsException(isolate_->stack_guard()->HandleInterrupts(
                        StackGuard::InterruptLevel::kNoGC),
                    isolate_)) {
      return EXCEPTION;
    }
  }

  if (CheckCycle()) {
    // TODO(pthier): Construct exception message on fast-path and avoid falling
    // back to slow path just to handle the exception.
    return SLOW_PATH;
  }

  return SUCCESS;
}

template <typename Char>
bool FastJsonStringifier<Char>::CheckCycle() {
  std::unordered_set<Address> set;
  for (uint32_t i = 0; i < stack_.size(); i++) {
    ContinuationRecord rec = stack_[i];
    if (rec.type == ContinuationRecord::kObjectKey) continue;
    Tagged<JSAny> obj = rec.object;
    if (set.find(obj.ptr()) != set.end()) {
      return true;
    }
    set.insert(obj.ptr());
  }
  return false;
}

template <typename Char>
FastJsonStringifierResult FastJsonStringifier<Char>::AppendStringChecked(
    Tagged<String> string) {
  DisallowGarbageCollection no_gc;
  DCHECK_EQ(string->map()->instance_type(), INTERNALIZED_ONE_BYTE_STRING_TYPE);
  return AppendStringChecked(Cast<SeqOneByteString>(string)->GetChars(no_gc),
                             string->length(), no_gc);
}

template <typename Char>
template <typename SrcChar>
  requires(sizeof(SrcChar) == sizeof(uint8_t))
FastJsonStringifierResult FastJsonStringifier<Char>::AppendStringChecked(
    const SrcChar* chars, uint32_t length,
    const DisallowGarbageCollection& no_gc) {
  constexpr int kUseSimdLengthThreshold = 32;
  if (length >= kUseSimdLengthThreshold) {
    return AppendStringCheckedSIMD(chars, length, no_gc);
  }
  return AppendStringCheckedScalar(chars, length, 0, 0, no_gc);
}

template <typename Char>
template <typename SrcChar>
  requires(sizeof(SrcChar) == sizeof(uint8_t))
FastJsonStringifierResult FastJsonStringifier<Char>::AppendStringCheckedScalar(
    const SrcChar* chars, uint32_t length, uint32_t start,
    uint32_t uncopied_src_index, const DisallowGarbageCollection& no_gc) {
  for (uint32_t i = start; i < length; i++) {
    SrcChar c = chars[i];
    if (V8_LIKELY(DoNotEscape(c))) continue;
    buffer_.Append(chars + uncopied_src_index, i - uncopied_src_index);
    AppendCString(&JsonEscapeTable[c * kJsonEscapeTableEntrySize]);
    uncopied_src_index = i + 1;
  }
  if (uncopied_src_index < length) {
    buffer_.Append(chars + uncopied_src_index, length - uncopied_src_index);
  }
  return SUCCESS;
}

template <typename Char>
template <typename SrcChar>
  requires(sizeof(SrcChar) == sizeof(uint8_t))
FastJsonStringifierResult FastJsonStringifier<Char>::AppendStringCheckedSIMD(
    const SrcChar* chars, uint32_t length,
    const DisallowGarbageCollection& no_gc) {
  namespace hw = hwy::HWY_NAMESPACE;

  uint32_t uncopied_src_index = 0;  // Index of first char not copied yet.
  const SrcChar* block = chars;
  const SrcChar* end = chars + length;
  hw::FixedTag<SrcChar, 16> tag;
  static int stride = hw::Lanes(tag);

  const auto mask_0x20 = hw::Set(tag, 0x20);
  const auto mask_0x22 = hw::Set(tag, 0x22);
  const auto mask_0x5c = hw::Set(tag, 0x5c);

  for (; block + (stride - 1) < end; block += stride) {
    const auto input = hw::LoadU(tag, block);
    const auto has_lower_than_0x20 = input < mask_0x20;
    const auto has_0x22 = input == mask_0x22;
    const auto has_0x5c = input == mask_0x5c;
    const auto result = hw::Or(hw::Or(has_lower_than_0x20, has_0x22), has_0x5c);

    // No character that needs escaping found in block.
    if (V8_LIKELY(hw::AllFalse(tag, result))) continue;

    size_t index = hw::FindKnownFirstTrue(tag, result);
    Char found_char = block[index];
    const int char_index =
        static_cast<int>(block - chars) + static_cast<int>(index);
    const int copy_length = char_index - uncopied_src_index;
    buffer_.Append(chars + uncopied_src_index, copy_length);
    AppendCString(&JsonEscapeTable[found_char * kJsonEscapeTableEntrySize]);
    uncopied_src_index = char_index + 1;
    // Advance to character after the one that was found to need escaping.
    // Subtract stride as it will be added again at the beginning of the loop.
    block += static_cast<int>(index + 1 - stride);
  }

  // Handle remaining characters.
  const uint32_t start_index = static_cast<uint32_t>(block - chars);
  return AppendStringCheckedScalar(chars, length, start_index,
                                   uncopied_src_index, no_gc);
}

template <typename Char>
template <typename SrcChar>
  requires(sizeof(SrcChar) == sizeof(base::uc16))
FastJsonStringifierResult FastJsonStringifier<Char>::AppendStringChecked(
    const SrcChar* chars, uint32_t length,
    const DisallowGarbageCollection& no_gc) {
  uint32_t uncopied_src_index = 0;  // Index of first char not copied yet.
  // TODO(pthier): Add SIMD version.
  for (uint32_t i = 0; i < length; i++) {
    SrcChar c = chars[i];
    if (V8_LIKELY(DoNotEscape(c))) continue;
    if (sizeof(SrcChar) != 1 && base::IsInRange(c, static_cast<SrcChar>(0xD800),
                                                static_cast<SrcChar>(0xDFFF))) {
      // The current character is a surrogate.
      buffer_.Append(chars + uncopied_src_index, i - uncopied_src_index);
      char double_to_radix_chars[kDoubleToRadixMaxChars];
      base::Vector<char> double_to_radix_buffer =
          base::ArrayVector(double_to_radix_chars);
      if (c <= 0xDBFF) {
        // The current character is a leading surrogate.
        if (i + 1 < length) {
          // There is a next character.
          SrcChar next = chars[i + 1];
          if (base::IsInRange(next, static_cast<SrcChar>(0xDC00),
                              static_cast<SrcChar>(0xDFFF))) {
            // The next character is a trailing surrogate, meaning this is a
            // surrogate pair.
            AppendCharacter(c);
            AppendCharacter(next);
            i++;
          } else {
            // The next character is not a trailing surrogate. Thus, the
            // current character is a lone leading surrogate.
            AppendCStringLiteral("\\u");
            std::string_view hex =
                DoubleToRadixStringView(c, 16, double_to_radix_buffer);
            AppendString(hex);
          }
        } else {
          // There is no next character. Thus, the current character is a lone
          // leading surrogate.
          AppendCStringLiteral("\\u");
          std::string_view hex =
              DoubleToRadixStringView(c, 16, double_to_radix_buffer);
          AppendString(hex);
        }
      } else {
        // The current character is a lone trailing surrogate. (If it had been
        // preceded by a leading surrogate, we would've ended up in the other
        // branch earlier on, and the current character would've been handled
        // as part of the surrogate pair already.)
        AppendCStringLiteral("\\u");
        std::string_view hex =
            DoubleToRadixStringView(c, 16, double_to_radix_buffer);
        AppendString(hex);
      }
      uncopied_src_index = i + 1;
    } else {
      buffer_.Append(chars + uncopied_src_index, i - uncopied_src_index);
      DCHECK_LT(c, 0x60);
      AppendCString(&JsonEscapeTable[c * kJsonEscapeTableEntrySize]);
      uncopied_src_index = i + 1;
    }
  }
  if (uncopied_src_index < length) {
    buffer_.Append(chars + uncopied_src_index, length - uncopied_src_index);
  }
  return SUCCESS;
}

namespace {

bool CanUseFastStringifier(DirectHandle<JSAny> replacer,
                           DirectHandle<Object> gap) {
  // TODO(pthier): Support gap on fast-path.
  return v8_flags.json_stringify_fast_path && IsUndefined(*replacer) &&
         IsUndefined(*gap);
}

MaybeDirectHandle<Object> FastJsonStringify(Isolate* isolate,
                                            Handle<JSAny> object) {
  DisallowGarbageCollection no_gc;

  FastJsonStringifier<uint8_t> one_byte_stringifier(isolate);
  std::optional<FastJsonStringifier<base::uc16>> two_byte_stringifier;
  FastJsonStringifierResult result =
      one_byte_stringifier.SerializeObject(*object, no_gc);
  bool result_is_one_byte = true;

  if (result == CHANGE_ENCODING) {
    two_byte_stringifier.emplace(isolate);
    result = two_byte_stringifier->ResumeFrom(one_byte_stringifier, no_gc);
    DCHECK_NE(result, CHANGE_ENCODING);
    result_is_one_byte = false;
  }

  if (V8_LIKELY(result == SUCCESS)) {
    if (result_is_one_byte) {
      const int length = one_byte_stringifier.ResultLength();
      DirectHandle<SeqOneByteString> ret;
      {
        AllowGarbageCollection allow_gc;
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, ret, isolate->factory()->NewRawOneByteString(length));
      }
      one_byte_stringifier.CopyResultTo(ret->GetChars(no_gc));
      return ret;
    } else {
      DCHECK(two_byte_stringifier.has_value());
      const int one_byte_length = one_byte_stringifier.ResultLength();
      const int two_byte_length = two_byte_stringifier->ResultLength();
      const int total_length = one_byte_length + two_byte_length;
      DirectHandle<SeqTwoByteString> ret;
      {
        AllowGarbageCollection allow_gc;
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, ret,
            isolate->factory()->NewRawTwoByteString(total_length));
      }
      base::uc16* chars = ret->GetChars(no_gc);
      if (one_byte_length > 0) {
        one_byte_stringifier.CopyResultTo(chars);
      }
      DCHECK_GT(two_byte_length, 0);
      two_byte_stringifier->CopyResultTo(chars + one_byte_length);
      return ret;
    }
  } else if (result == UNDEFINED) {
    return isolate->factory()->undefined_value();
  } else if (result == SLOW_PATH) {
    // TODO(pthier): Resume instead of restarting.
    AllowGarbageCollection allow_gc;
    JsonStringifier stringifier(isolate);
    Handle<JSAny> undefined = isolate->factory()->undefined_value();
    return stringifier.Stringify(object, undefined, undefined);
  }
  DCHECK(result == EXCEPTION);
  CHECK(isolate->has_exception());
  return MaybeDirectHandle<Object>();
}

}  // namespace

MaybeDirectHandle<Object> JsonStringify(Isolate* isolate, Handle<JSAny> object,
                                        Handle<JSAny> replacer,
                                        Handle<Object> gap) {
  if (CanUseFastStringifier(replacer, gap)) {
    return FastJsonStringify(isolate, object);
  } else {
    JsonStringifier stringifier(isolate);
    return stringifier.Stringify(object, replacer, gap);
  }
}

}  // namespace internal
}  // namespace v8

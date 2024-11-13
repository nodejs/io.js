// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_STRING_H_
#define V8_OBJECTS_STRING_H_

#include <memory>
#include <optional>

#include "src/base/bits.h"
#include "src/base/export-template.h"
#include "src/base/small-vector.h"
#include "src/base/strings.h"
#include "src/common/globals.h"
#include "src/heap/heap.h"
#include "src/objects/instance-type.h"
#include "src/objects/map.h"
#include "src/objects/name.h"
#include "src/objects/smi.h"
#include "src/objects/tagged.h"
#include "src/sandbox/external-pointer.h"
#include "src/strings/unicode-decoder.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

namespace maglev {
class CheckedInternalizedString;
class BuiltinStringFromCharCode;
}  // namespace maglev

namespace wasm {
namespace baseline {
class LiftoffCompiler;
}  // namespace baseline
}  // namespace wasm

class SharedStringAccessGuardIfNeeded;

enum InstanceType : uint16_t;

enum AllowNullsFlag { ALLOW_NULLS, DISALLOW_NULLS };
enum RobustnessFlag { ROBUST_STRING_TRAVERSAL, FAST_STRING_TRAVERSAL };

// The characteristics of a string are stored in its map.  Retrieving these
// few bits of information is moderately expensive, involving two memory
// loads where the second is dependent on the first.  To improve efficiency
// the shape of the string is given its own class so that it can be retrieved
// once and used for several string operations.  A StringShape is small enough
// to be passed by value and is immutable, but be aware that flattening a
// string can potentially alter its shape.  Also be aware that a GC caused by
// something else can alter the shape of a string due to ConsString
// shortcutting.  Keeping these restrictions in mind has proven to be error-
// prone and so we no longer put StringShapes in variables unless there is a
// concrete performance benefit at that particular point in the code.
class StringShape {
 public:
  V8_INLINE explicit StringShape(const Tagged<String> s);
  V8_INLINE explicit StringShape(const Tagged<String> s,
                                 PtrComprCageBase cage_base);
  V8_INLINE explicit StringShape(Tagged<Map> s);
  V8_INLINE explicit StringShape(InstanceType t);
  V8_INLINE bool IsSequential() const;
  V8_INLINE bool IsExternal() const;
  V8_INLINE bool IsCons() const;
  V8_INLINE bool IsSliced() const;
  V8_INLINE bool IsThin() const;
  V8_INLINE bool IsDirect() const;
  V8_INLINE bool IsIndirect() const;
  V8_INLINE bool IsUncachedExternal() const;
  V8_INLINE bool IsExternalOneByte() const;
  V8_INLINE bool IsExternalTwoByte() const;
  V8_INLINE bool IsSequentialOneByte() const;
  V8_INLINE bool IsSequentialTwoByte() const;
  V8_INLINE bool IsInternalized() const;
  V8_INLINE bool IsShared() const;
  V8_INLINE StringRepresentationTag representation_tag() const;
  V8_INLINE uint32_t encoding_tag() const;
  V8_INLINE uint32_t representation_and_encoding_tag() const;
  V8_INLINE uint32_t representation_encoding_and_shared_tag() const;
#ifdef DEBUG
  inline uint32_t type() const { return type_; }
  inline void invalidate() { valid_ = false; }
  inline bool valid() const { return valid_; }
#else
  inline void invalidate() {}
#endif

  // Run different behavior for each concrete string class type, as defined by
  // the dispatcher.
  template <typename TDispatcher, typename TResult, typename... TArgs>
  inline TResult DispatchToSpecificTypeWithoutCast(TArgs&&... args);
  template <typename TDispatcher, typename TResult, typename... TArgs>
  inline TResult DispatchToSpecificType(Tagged<String> str, TArgs&&... args);

 private:
  uint32_t type_;
#ifdef DEBUG
  inline void set_valid() { valid_ = true; }
  bool valid_;
#else
  inline void set_valid() {}
#endif
};

// The String abstract class captures JavaScript string values:
//
// Ecma-262:
//  4.3.16 String Value
//    A string value is a member of the type String and is a finite
//    ordered sequence of zero or more 16-bit unsigned integer values.
//
// All string values have a length field.
V8_OBJECT class String : public Name {
 public:
  enum Encoding { ONE_BYTE_ENCODING, TWO_BYTE_ENCODING };

  // Representation of the flat content of a String.
  // A non-flat string doesn't have flat content.
  // A flat string has content that's encoded as a sequence of either
  // one-byte chars or two-byte UC16.
  // Returned by String::GetFlatContent().
  // Not safe to use from concurrent background threads.
  // TODO(solanes): Move FlatContent into FlatStringReader, and make it private.
  // This would de-duplicate code, as well as taking advantage of the fact that
  // FlatStringReader is relocatable.
  V8_OBJECT_INNER_CLASS class FlatContent {
   public:
    inline ~FlatContent();

    // Returns true if the string is flat and this structure contains content.
    bool IsFlat() const { return state_ != NON_FLAT; }
    // Returns true if the structure contains one-byte content.
    bool IsOneByte() const { return state_ == ONE_BYTE; }
    // Returns true if the structure contains two-byte content.
    bool IsTwoByte() const { return state_ == TWO_BYTE; }

    // Return the one byte content of the string. Only use if IsOneByte()
    // returns true.
    base::Vector<const uint8_t> ToOneByteVector() const {
      DCHECK_EQ(ONE_BYTE, state_);
      return base::Vector<const uint8_t>(onebyte_start, length_);
    }
    // Return the two-byte content of the string. Only use if IsTwoByte()
    // returns true.
    base::Vector<const base::uc16> ToUC16Vector() const {
      DCHECK_EQ(TWO_BYTE, state_);
      return base::Vector<const base::uc16>(twobyte_start, length_);
    }

    base::uc16 Get(int i) const {
      DCHECK(i < length_);
      DCHECK(state_ != NON_FLAT);
      if (state_ == ONE_BYTE) return onebyte_start[i];
      return twobyte_start[i];
    }

    bool UsesSameString(const FlatContent& other) const {
      return onebyte_start == other.onebyte_start;
    }

    // It is almost always a bug if the contents of a FlatContent changes during
    // its lifetime, which can happen due to GC or bugs in concurrent string
    // access. Rarely, callers need the ability to GC and have ensured safety in
    // other ways, such as in IrregexpInterpreter. Those callers can disable the
    // checksum verification with this call.
    void UnsafeDisableChecksumVerification() {
#ifdef ENABLE_SLOW_DCHECKS
      checksum_ = kChecksumVerificationDisabled;
#endif
    }

    int length() const { return length_; }

   private:
    enum State { NON_FLAT, ONE_BYTE, TWO_BYTE };

    // Constructors only used by String::GetFlatContent().
    inline FlatContent(const uint8_t* start, int length,
                       const DisallowGarbageCollection& no_gc);
    inline FlatContent(const base::uc16* start, int length,
                       const DisallowGarbageCollection& no_gc);
    explicit FlatContent(const DisallowGarbageCollection& no_gc)
        : onebyte_start(nullptr), length_(0), state_(NON_FLAT), no_gc_(no_gc) {}

    union {
      const uint8_t* onebyte_start;
      const base::uc16* twobyte_start;
    };
    int length_;
    State state_;
    const DisallowGarbageCollection& no_gc_;

    static constexpr uint32_t kChecksumVerificationDisabled = 0;

#ifdef ENABLE_SLOW_DCHECKS
    inline uint32_t ComputeChecksum() const;

    uint32_t checksum_;
#endif

    friend class String;
    friend class IterableSubString;
  } V8_OBJECT_INNER_CLASS_END;

  template <typename IsolateT>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  void MakeThin(IsolateT* isolate, Tagged<String> canonical);

  template <typename Char>
  V8_INLINE base::Vector<const Char> GetCharVector(
      const DisallowGarbageCollection& no_gc);

  // Get chars from sequential or external strings. May only be called when a
  // SharedStringAccessGuard is not needed (i.e. on the main thread or on
  // read-only strings).
  template <typename Char>
  inline const Char* GetDirectStringChars(
      const DisallowGarbageCollection& no_gc) const;

  // Get chars from sequential or external strings.
  template <typename Char>
  inline const Char* GetDirectStringChars(
      const DisallowGarbageCollection& no_gc,
      const SharedStringAccessGuardIfNeeded& access_guard) const;

  // Returns the address of the character at an offset into this string.
  // Requires: this->IsFlat()
  const uint8_t* AddressOfCharacterAt(int start_index,
                                      const DisallowGarbageCollection& no_gc);

  inline int32_t length() const;
  inline int32_t length(AcquireLoadTag) const;

  inline void set_length(int32_t hash);
  inline void set_length(int32_t hash, ReleaseStoreTag);

  // Returns whether this string has only one-byte chars, i.e. all of them can
  // be one-byte encoded.  This might be the case even if the string is
  // two-byte.  Such strings may appear when the embedder prefers
  // two-byte external representations even for one-byte data.
  inline bool IsOneByteRepresentation() const;
  inline bool IsTwoByteRepresentation() const;

  // Cons and slices have an encoding flag that may not represent the actual
  // encoding of the underlying string.  This is taken into account here.
  // This function is static because that helps it get inlined.
  // Requires: string.IsFlat()
  static inline bool IsOneByteRepresentationUnderneath(Tagged<String> string);

  // Get and set individual two byte chars in the string.
  inline void Set(int index, uint16_t value);
  // Get individual two byte char in the string.  Repeated calls
  // to this method are not efficient unless the string is flat.
  // If it is called from a background thread, the LocalIsolate version should
  // be used.
  V8_INLINE uint16_t Get(int index) const;
  V8_INLINE uint16_t Get(int index, Isolate* isolate) const;
  V8_INLINE uint16_t Get(int index, LocalIsolate* local_isolate) const;
  // Method to pass down the access_guard. Useful for recursive calls such as
  // ThinStrings where we go String::Get into ThinString::Get into String::Get
  // again for the internalized string.
  V8_INLINE uint16_t
  Get(int index, const SharedStringAccessGuardIfNeeded& access_guard) const;

  // ES6 section 7.1.3.1 ToNumber Applied to the String Type
  static Handle<Number> ToNumber(Isolate* isolate, Handle<String> subject);

  // Flattens the string.  Checks first inline to see if it is
  // necessary.  Does nothing if the string is not a cons string.
  // Flattening allocates a sequential string with the same data as
  // the given string and mutates the cons string to a degenerate
  // form, where the first component is the new sequential string and
  // the second component is the empty string.  If allocation fails,
  // this function returns a failure.  If flattening succeeds, this
  // function returns the sequential string that is now the first
  // component of the cons string.
  //
  // Degenerate cons strings are handled specially by the garbage
  // collector (see IsShortcutCandidate).

  static V8_INLINE Handle<String> Flatten(
      Isolate* isolate, Handle<String> string,
      AllocationType allocation = AllocationType::kYoung);
  static V8_INLINE Handle<String> Flatten(
      LocalIsolate* isolate, Handle<String> string,
      AllocationType allocation = AllocationType::kYoung);

  // Tries to return the content of a flat string as a structure holding either
  // a flat vector of char or of base::uc16.
  // If the string isn't flat, and therefore doesn't have flat content, the
  // returned structure will report so, and can't provide a vector of either
  // kind.
  // When using a SharedStringAccessGuard, the guard's must outlive the
  // returned FlatContent.
  V8_EXPORT_PRIVATE V8_INLINE FlatContent
  GetFlatContent(const DisallowGarbageCollection& no_gc);
  V8_EXPORT_PRIVATE V8_INLINE FlatContent
  GetFlatContent(const DisallowGarbageCollection& no_gc,
                 const SharedStringAccessGuardIfNeeded&);

  // Returns the parent of a sliced string or first part of a flat cons string.
  // Requires: StringShape(this).IsIndirect() && this->IsFlat()
  inline Tagged<String> GetUnderlying() const;

  // Shares the string. Checks inline if the string is already shared or can be
  // shared by transitioning its map in-place. If neither is possible, flattens
  // and copies into a new shared sequential string.
  static inline Handle<String> Share(Isolate* isolate, Handle<String> string);

  // String relational comparison, implemented according to ES6 section 7.2.11
  // Abstract Relational Comparison (step 5): The comparison of Strings uses a
  // simple lexicographic ordering on sequences of code unit values. There is no
  // attempt to use the more complex, semantically oriented definitions of
  // character or string equality and collating order defined in the Unicode
  // specification. Therefore String values that are canonically equal according
  // to the Unicode standard could test as unequal. In effect this algorithm
  // assumes that both Strings are already in normalized form. Also, note that
  // for strings containing supplementary characters, lexicographic ordering on
  // sequences of UTF-16 code unit values differs from that on sequences of code
  // point values.
  V8_WARN_UNUSED_RESULT static ComparisonResult Compare(Isolate* isolate,
                                                        Handle<String> x,
                                                        Handle<String> y);

  // Perform ES6 21.1.3.8, including checking arguments.
  static Tagged<Object> IndexOf(Isolate* isolate, Handle<Object> receiver,
                                Handle<Object> search, Handle<Object> position);
  // Perform string match of pattern on subject, starting at start index.
  // Caller must ensure that 0 <= start_index <= sub->length(), as this does not
  // check any arguments.
  static int IndexOf(Isolate* isolate, Handle<String> receiver,
                     Handle<String> search, int start_index);

  static Tagged<Object> LastIndexOf(Isolate* isolate, Handle<Object> receiver,
                                    Handle<Object> search,
                                    Handle<Object> position);

  // Encapsulates logic related to a match and its capture groups as required
  // by GetSubstitution.
  class Match {
   public:
    virtual Handle<String> GetMatch() = 0;
    virtual Handle<String> GetPrefix() = 0;
    virtual Handle<String> GetSuffix() = 0;

    // A named capture can be unmatched (either not specified in the pattern,
    // or specified but unmatched in the current string), or matched.
    enum CaptureState { UNMATCHED, MATCHED };

    virtual int CaptureCount() = 0;
    virtual bool HasNamedCaptures() = 0;
    virtual MaybeHandle<String> GetCapture(int i, bool* capture_exists) = 0;
    virtual MaybeHandle<String> GetNamedCapture(Handle<String> name,
                                                CaptureState* state) = 0;

    virtual ~Match() = default;
  };

  // ES#sec-getsubstitution
  // GetSubstitution(matched, str, position, captures, replacement)
  // Expand the $-expressions in the string and return a new string with
  // the result.
  // A {start_index} can be passed to specify where to start scanning the
  // replacement string.
  V8_WARN_UNUSED_RESULT static MaybeHandle<String> GetSubstitution(
      Isolate* isolate, Match* match, Handle<String> replacement,
      int start_index = 0);

  // String equality operations.
  inline bool Equals(Tagged<String> other) const;
  inline static bool Equals(Isolate* isolate, Handle<String> one,
                            Handle<String> two);

  enum class EqualityType { kWholeString, kPrefix, kNoLengthCheck };

  // Check if this string matches the given vector of characters, either as a
  // whole string or just a prefix.
  //
  // The Isolate is passed as "evidence" that this call is on the main thread,
  // and to distiguish from the LocalIsolate overload.
  template <EqualityType kEqType = EqualityType::kWholeString, typename Char>
  inline bool IsEqualTo(base::Vector<const Char> str, Isolate* isolate) const;

  // Check if this string matches the given vector of characters, either as a
  // whole string or just a prefix.
  //
  // This is main-thread only, like the Isolate* overload, but additionally
  // computes the PtrComprCageBase for IsEqualToImpl.
  template <EqualityType kEqType = EqualityType::kWholeString, typename Char>
  inline bool IsEqualTo(base::Vector<const Char> str) const;

  // Check if this string matches the given vector of characters, either as a
  // whole string or just a prefix.
  //
  // The LocalIsolate is passed to provide access to the string access lock,
  // which is taken when reading the string's contents on a background thread.
  template <EqualityType kEqType = EqualityType::kWholeString, typename Char>
  inline bool IsEqualTo(base::Vector<const Char> str,
                        LocalIsolate* isolate) const;

  V8_EXPORT_PRIVATE bool HasOneBytePrefix(base::Vector<const char> str);
  V8_EXPORT_PRIVATE inline bool IsOneByteEqualTo(base::Vector<const char> str);

  // Returns true if the |str| is a valid ECMAScript identifier.
  static bool IsIdentifier(Isolate* isolate, Handle<String> str);

  // Return a UTF8 representation of the string.  The string is null
  // terminated but may optionally contain nulls.  Length is returned
  // in length_output if length_output is not a null pointer  The string
  // should be nearly flat, otherwise the performance of this method may
  // be very slow (quadratic in the length).  Setting robustness_flag to
  // ROBUST_STRING_TRAVERSAL invokes behaviour that is robust  This means it
  // handles unexpected data without causing assert failures and it does not
  // do any heap allocations.  This is useful when printing stack traces.
  std::unique_ptr<char[]> ToCString(AllowNullsFlag allow_nulls,
                                    RobustnessFlag robustness_flag, int offset,
                                    int length, int* length_output = nullptr);
  V8_EXPORT_PRIVATE std::unique_ptr<char[]> ToCString(
      AllowNullsFlag allow_nulls = DISALLOW_NULLS,
      RobustnessFlag robustness_flag = FAST_STRING_TRAVERSAL,
      int* length_output = nullptr);

  // Externalization.
  template <typename T>
  bool MarkForExternalizationDuringGC(Isolate* isolate, T* resource);
  template <typename T>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  void MakeExternalDuringGC(Isolate* isolate, T* resource);
  V8_EXPORT_PRIVATE bool MakeExternal(
      v8::String::ExternalStringResource* resource);
  V8_EXPORT_PRIVATE bool MakeExternal(
      v8::String::ExternalOneByteStringResource* resource);
  bool SupportsExternalization(v8::String::Encoding);

  // Conversion.
  // "array index": an index allowed by the ES spec for JSArrays.
  inline bool AsArrayIndex(uint32_t* index);

  // This is used for calculating array indices but differs from an
  // Array Index in the regard that this does not support the full
  // array index range. This only supports positive numbers less than
  // or equal to INT_MAX.
  //
  // String::AsArrayIndex might be a better fit if you're looking to
  // calculate the array index.
  //
  // if val < 0 or val > INT_MAX, returns -1
  // if 0 <= val <= INT_MAX, returns val
  static int32_t ToArrayIndex(Address addr);

  // "integer index": the string is the decimal representation of an
  // integer in the range of a size_t. Useful for TypedArray accesses.
  inline bool AsIntegerIndex(size_t* index);

  // Trimming.
  enum TrimMode { kTrim, kTrimStart, kTrimEnd };

  V8_EXPORT_PRIVATE void PrintOn(FILE* out);
  V8_EXPORT_PRIVATE void PrintOn(std::ostream& out);

  // For use during stack traces.  Performs rudimentary sanity check.
  bool LooksValid();

  // Printing utility functions.
  // - PrintUC16 prints the raw string contents to the given stream.
  //   Non-printable characters are formatted as hex, but otherwise the string
  //   is printed as-is.
  // - StringShortPrint and StringPrint have extra formatting: they add a
  //   prefix and suffix depending on the string kind, may add other information
  //   such as the string heap object address, may truncate long strings, etc.
  const char* PrefixForDebugPrint() const;
  const char* SuffixForDebugPrint() const;
  void StringShortPrint(StringStream* accumulator);
  void PrintUC16(std::ostream& os, int start = 0, int end = -1);
  void PrintUC16(StringStream* accumulator, int start, int end);

  // Dispatched behavior.
#if defined(DEBUG) || defined(OBJECT_PRINT)
  char* ToAsciiArray();
#endif
  DECL_PRINTER(String)
  DECL_VERIFIER(String)

  inline bool IsFlat() const;
  inline bool IsShared() const;

  // Max char codes.
  static const int32_t kMaxOneByteCharCode = unibrow::Latin1::kMaxChar;
  static const uint32_t kMaxOneByteCharCodeU = unibrow::Latin1::kMaxChar;
  static const int kMaxUtf16CodeUnit = 0xffff;
  static const uint32_t kMaxUtf16CodeUnitU = kMaxUtf16CodeUnit;
  static const base::uc32 kMaxCodePoint = 0x10ffff;

  // Maximal string length.
  // The max length is different on 32 and 64 bit platforms. Max length for
  // 32-bit platforms is ~268.4M chars. On 64-bit platforms, max length is
  // ~536.8M chars.
  // See include/v8.h for the definition.
  static const int kMaxLength = v8::String::kMaxLength;

  // Max length for computing hash. For strings longer than this limit the
  // string length is used as the hash value.
  static const int kMaxHashCalcLength = 16383;

  // Limit for truncation in short printing.
  static const int kMaxShortPrintLength = 1024;

  // Helper function for flattening strings.
  template <typename sinkchar>
  EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
  static void WriteToFlat(Tagged<String> source, sinkchar* sink, int from,
                          int to);
  template <typename sinkchar>
  static void WriteToFlat(Tagged<String> source, sinkchar* sink, int from,
                          int to, const SharedStringAccessGuardIfNeeded&);

  // Returns true if this string has no unpaired surrogates and false otherwise.
  static inline bool IsWellFormedUnicode(Isolate* isolate,
                                         Handle<String> string);

  static inline bool IsAscii(const char* chars, int length) {
    return IsAscii(reinterpret_cast<const uint8_t*>(chars), length);
  }

  static inline bool IsAscii(const uint8_t* chars, int length) {
    return NonAsciiStart(chars, length) >= length;
  }

  static inline int NonOneByteStart(const base::uc16* chars, int length) {
    DCHECK(IsAligned(reinterpret_cast<Address>(chars), sizeof(base::uc16)));
    const uint16_t* start = chars;
    const uint16_t* limit = chars + length;

    if (static_cast<size_t>(length) >= kUIntptrSize) {
      // Check unaligned chars.
      while (!IsAligned(reinterpret_cast<Address>(chars), kUIntptrSize)) {
        if (*chars > unibrow::Latin1::kMaxChar) {
          return static_cast<int>(chars - start);
        }
        ++chars;
      }

      // Check aligned words.
      static_assert(unibrow::Latin1::kMaxChar == 0xFF);
#ifdef V8_TARGET_LITTLE_ENDIAN
      const uintptr_t non_one_byte_mask = kUintptrAllBitsSet / 0xFFFF * 0xFF00;
#else
      const uintptr_t non_one_byte_mask = kUintptrAllBitsSet / 0xFFFF * 0x00FF;
#endif
      while (chars + sizeof(uintptr_t) <= limit) {
        if (*reinterpret_cast<const uintptr_t*>(chars) & non_one_byte_mask) {
          break;
        }
        chars += (sizeof(uintptr_t) / sizeof(base::uc16));
      }
    }

    // Check remaining unaligned chars, or find non-one-byte char in word.
    while (chars < limit) {
      if (*chars > unibrow::Latin1::kMaxChar) {
        return static_cast<int>(chars - start);
      }
      ++chars;
    }

    return static_cast<int>(chars - start);
  }

  static inline bool IsOneByte(const base::uc16* chars, int length) {
    return NonOneByteStart(chars, length) >= length;
  }

  // May only be called when a SharedStringAccessGuard is not needed (i.e. on
  // the main thread or on read-only strings).
  template <class Visitor>
  static inline Tagged<ConsString> VisitFlat(Visitor* visitor,
                                             Tagged<String> string,
                                             int offset = 0);

  template <class Visitor>
  static inline Tagged<ConsString> VisitFlat(
      Visitor* visitor, Tagged<String> string, int offset,
      const SharedStringAccessGuardIfNeeded& access_guard);

  static int constexpr kInlineLineEndsSize = 32;
  using LineEndsVector = base::SmallVector<int32_t, kInlineLineEndsSize>;

  template <typename IsolateT>
  static LineEndsVector CalculateLineEndsVector(IsolateT* isolate,
                                                Handle<String> string,
                                                bool include_ending_line);

  template <typename IsolateT>
  static Handle<FixedArray> CalculateLineEnds(IsolateT* isolate,
                                              Handle<String> string,
                                              bool include_ending_line);

  // Returns true if string can be internalized without copying. In such cases
  // the string is inserted into the string table and its map is changed to an
  // internalized equivalent.
  static inline bool IsInPlaceInternalizable(Tagged<String> string);
  static inline bool IsInPlaceInternalizable(InstanceType instance_type);

  static inline bool IsInPlaceInternalizableExcludingExternal(
      InstanceType instance_type);

 private:
  friend class Name;
  friend class CodeStubAssembler;
  friend class StringTableInsertionKey;
  friend class SharedStringTableInsertionKey;
  friend class InternalizedStringKey;

  friend struct OffsetsForDebug;
  friend class Accessors;
  friend class StringBuiltinsAssembler;
  friend class maglev::MaglevAssembler;
  friend class compiler::AccessBuilder;
  friend class wasm::baseline::LiftoffCompiler;
  friend class TorqueGeneratedStringAsserts;

  // Implementation of the Get() public methods. Do not use directly.
  V8_INLINE uint16_t
  GetImpl(int index, const SharedStringAccessGuardIfNeeded& access_guard) const;

  // Implementation of the IsEqualTo() public methods. Do not use directly.
  template <EqualityType kEqType, typename Char>
  V8_INLINE bool IsEqualToImpl(
      base::Vector<const Char> str,
      const SharedStringAccessGuardIfNeeded& access_guard) const;

  // Out-of-line IsEqualToImpl for ConsString.
  template <typename Char>
  V8_NOINLINE static bool IsConsStringEqualToImpl(
      Tagged<ConsString> string, base::Vector<const Char> str,
      const SharedStringAccessGuardIfNeeded& access_guard);

  V8_EXPORT_PRIVATE static Handle<String> SlowFlatten(
      Isolate* isolate, Handle<ConsString> cons, AllocationType allocation);

  V8_EXPORT_PRIVATE V8_INLINE static std::optional<FlatContent>
  TryGetFlatContentFromDirectString(const DisallowGarbageCollection& no_gc,
                                    Tagged<String> string, int offset,
                                    int length,
                                    const SharedStringAccessGuardIfNeeded&);
  V8_EXPORT_PRIVATE FlatContent
  SlowGetFlatContent(const DisallowGarbageCollection& no_gc,
                     const SharedStringAccessGuardIfNeeded&);

  V8_EXPORT_PRIVATE static Handle<String> SlowShare(Isolate* isolate,
                                                    Handle<String> source);

  // Slow case of String::Equals.  This implementation works on any strings
  // but it is most efficient on strings that are almost flat.
  V8_EXPORT_PRIVATE bool SlowEquals(Tagged<String> other) const;
  V8_EXPORT_PRIVATE bool SlowEquals(
      Tagged<String> other, const SharedStringAccessGuardIfNeeded&) const;

  V8_EXPORT_PRIVATE static bool SlowEquals(Isolate* isolate, Handle<String> one,
                                           Handle<String> two);

  // Slow case of AsArrayIndex.
  V8_EXPORT_PRIVATE bool SlowAsArrayIndex(uint32_t* index);
  V8_EXPORT_PRIVATE bool SlowAsIntegerIndex(size_t* index);

  // Compute and set the hash code.
  // The value returned is always a computed hash, even if the value stored is
  // a forwarding index.
  V8_EXPORT_PRIVATE uint32_t ComputeAndSetRawHash();
  V8_EXPORT_PRIVATE uint32_t
  ComputeAndSetRawHash(const SharedStringAccessGuardIfNeeded&);

  int32_t length_;
} V8_OBJECT_END;

template <>
struct ObjectTraits<String> {
  static constexpr int kHeaderSize = sizeof(String);

  // There are several defining limits imposed by our current implementation:
  // - any string's length must fit into a Smi.
  static_assert(String::kMaxLength <= kSmiMaxValue,
                "String length must fit into a Smi");
  // - adding two string lengths must still fit into a 32-bit int without
  //   overflow
  static_assert(String::kMaxLength * 2 <= kMaxInt,
                "String::kMaxLength * 2 must fit into an int32");
  // - any heap object's size in bytes must be able to fit into a Smi, because
  //   its space on the heap might be filled with a Filler; for strings this
  //   means SeqTwoByteString::kMaxSize must be able to fit into a Smi.
  static_assert(String::kMaxLength * 2 + kHeaderSize <= kSmiMaxValue,
                "String object size in bytes must fit into a Smi");
  // - any heap object's size in bytes must be able to fit into an int, because
  //   that's what our object handling code uses almost everywhere.
  static_assert(String::kMaxLength * 2 + kHeaderSize <= kMaxInt,
                "String object size in bytes must fit into an int");
};

// clang-format off
extern template EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE) void
    String::WriteToFlat(Tagged<String> source, uint8_t* sink, int from, int to);
extern template EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE) void
    String::WriteToFlat(Tagged<String> source, uint16_t* sink, int from,
                        int to);
extern template EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE) void
    String::WriteToFlat(Tagged<String> source, uint8_t* sink, int from, int to,
                        const SharedStringAccessGuardIfNeeded&);
extern template EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE) void
    String::WriteToFlat(Tagged<String> source, uint16_t* sink, int from, int to,
                        const SharedStringAccessGuardIfNeeded&);
// clang-format on

class SubStringRange {
 public:
  inline SubStringRange(Tagged<String> string,
                        const DisallowGarbageCollection& no_gc, int first = 0,
                        int length = -1);
  class iterator;
  inline iterator begin();
  inline iterator end();

 private:
  Tagged<String> string_;
  int first_;
  int length_;
  const DisallowGarbageCollection& no_gc_;
};

// The SeqString abstract class captures sequential string values.
class SeqString : public String {
 public:
  // Truncate the string in-place if possible and return the result.
  // In case of new_length == 0, the empty string is returned without
  // truncating the original string.
  V8_WARN_UNUSED_RESULT static Handle<String> Truncate(Isolate* isolate,
                                                       Handle<SeqString> string,
                                                       int new_length);

  struct DataAndPaddingSizes {
    const int data_size;
    const int padding_size;
    bool operator==(const DataAndPaddingSizes& other) const {
      return data_size == other.data_size && padding_size == other.padding_size;
    }
  };
  DataAndPaddingSizes GetDataAndPaddingSizes() const;

  // Zero out only the padding bytes of this string.
  void ClearPadding();

  EXPORT_DECL_VERIFIER(SeqString)
};

V8_OBJECT class InternalizedString : public String {
  // TODO(neis): Possibly move some stuff from String here.
} V8_OBJECT_END;

// The OneByteString class captures sequential one-byte string objects.
// Each character in the OneByteString is an one-byte character.
V8_OBJECT class SeqOneByteString : public SeqString {
 public:
  static const bool kHasOneByteEncoding = true;
  using Char = uint8_t;

  V8_INLINE static constexpr int32_t DataSizeFor(int32_t length);
  V8_INLINE static constexpr int32_t SizeFor(int32_t length);

  // Dispatched behavior. The non SharedStringAccessGuardIfNeeded method is also
  // defined for convenience and it will check that the access guard is not
  // needed.
  inline uint8_t Get(int index) const;
  inline uint8_t Get(int index,
                     const SharedStringAccessGuardIfNeeded& access_guard) const;
  inline void SeqOneByteStringSet(int index, uint16_t value);
  inline void SeqOneByteStringSetChars(int index, const uint8_t* string,
                                       int length);

  // Get the address of the characters in this string.
  inline Address GetCharsAddress() const;

  // Get a pointer to the characters of the string. May only be called when a
  // SharedStringAccessGuard is not needed (i.e. on the main thread or on
  // read-only strings).
  inline uint8_t* GetChars(const DisallowGarbageCollection& no_gc);

  // Get a pointer to the characters of the string.
  inline uint8_t* GetChars(const DisallowGarbageCollection& no_gc,
                           const SharedStringAccessGuardIfNeeded& access_guard);

  DataAndPaddingSizes GetDataAndPaddingSizes() const;

  // Initializes padding bytes. Potentially zeros tail of the payload too!
  inline void clear_padding_destructively(int length);

  // Maximal memory usage for a single sequential one-byte string.
  static const int kMaxCharsSize = kMaxLength;

  inline int AllocatedSize() const;

  // A SeqOneByteString have different maps depending on whether it is shared.
  static inline bool IsCompatibleMap(Tagged<Map> map, ReadOnlyRoots roots);

  class BodyDescriptor;

 private:
  friend struct OffsetsForDebug;
  friend class CodeStubAssembler;
  friend class ToDirectStringAssembler;
  friend class IntlBuiltinsAssembler;
  friend class StringBuiltinsAssembler;
  friend class StringFromCharCodeAssembler;
  friend class maglev::MaglevAssembler;
  friend class compiler::AccessBuilder;
  friend class TorqueGeneratedSeqOneByteStringAsserts;

  FLEXIBLE_ARRAY_MEMBER(Char, chars);
} V8_OBJECT_END;

template <>
struct ObjectTraits<SeqOneByteString> {
  using BodyDescriptor = SeqOneByteString::BodyDescriptor;

  static constexpr int kHeaderSize = sizeof(SeqOneByteString);
  static constexpr int kMaxSize =
      OBJECT_POINTER_ALIGN(SeqOneByteString::kMaxCharsSize + kHeaderSize);

  static_assert(static_cast<int>((kMaxSize - kHeaderSize) /
                                 sizeof(SeqOneByteString::Char)) >=
                String::kMaxLength);
};

// The TwoByteString class captures sequential unicode string objects.
// Each character in the TwoByteString is a two-byte uint16_t.
V8_OBJECT class SeqTwoByteString : public SeqString {
 public:
  static const bool kHasOneByteEncoding = false;
  using Char = uint16_t;

  V8_INLINE static constexpr int32_t DataSizeFor(int32_t length);
  V8_INLINE static constexpr int32_t SizeFor(int32_t length);

  // Dispatched behavior.
  inline uint16_t Get(
      int index, const SharedStringAccessGuardIfNeeded& access_guard) const;
  inline void SeqTwoByteStringSet(int index, uint16_t value);

  // Get the address of the characters in this string.
  inline Address GetCharsAddress() const;

  // Get a pointer to the characters of the string. May only be called when a
  // SharedStringAccessGuard is not needed (i.e. on the main thread or on
  // read-only strings).
  inline base::uc16* GetChars(const DisallowGarbageCollection& no_gc);

  // Get a pointer to the characters of the string.
  inline base::uc16* GetChars(
      const DisallowGarbageCollection& no_gc,
      const SharedStringAccessGuardIfNeeded& access_guard);

  DataAndPaddingSizes GetDataAndPaddingSizes() const;

  // Initializes padding bytes. Potentially zeros tail of the payload too!
  inline void clear_padding_destructively(int length);

  // Maximal memory usage for a single sequential two-byte string.
  static const int kMaxCharsSize = kMaxLength * sizeof(Char);

  inline int AllocatedSize() const;

  // A SeqTwoByteString have different maps depending on whether it is shared.
  static inline bool IsCompatibleMap(Tagged<Map> map, ReadOnlyRoots roots);

  class BodyDescriptor;

 private:
  friend struct OffsetsForDebug;
  friend class CodeStubAssembler;
  friend class ToDirectStringAssembler;
  friend class IntlBuiltinsAssembler;
  friend class StringBuiltinsAssembler;
  friend class StringFromCharCodeAssembler;
  friend class maglev::MaglevAssembler;
  friend class maglev::BuiltinStringFromCharCode;
  friend class compiler::AccessBuilder;
  friend class TorqueGeneratedSeqTwoByteStringAsserts;

  FLEXIBLE_ARRAY_MEMBER(Char, chars);
} V8_OBJECT_END;

template <>
struct ObjectTraits<SeqTwoByteString> {
  using BodyDescriptor = SeqTwoByteString::BodyDescriptor;

  static constexpr int kHeaderSize = sizeof(SeqTwoByteString);
  static constexpr int kMaxSize =
      OBJECT_POINTER_ALIGN(SeqTwoByteString::kMaxCharsSize + kHeaderSize);

  static_assert(static_cast<int>((kMaxSize - kHeaderSize) /
                                 sizeof(SeqTwoByteString::Char)) >=
                String::kMaxLength);
};

// The ConsString class describes string values built by using the
// addition operator on strings.  A ConsString is a pair where the
// first and second components are pointers to other string values.
// One or both components of a ConsString can be pointers to other
// ConsStrings, creating a binary tree of ConsStrings where the leaves
// are non-ConsString string values.  The string value represented by
// a ConsString can be obtained by concatenating the leaf string
// values in a left-to-right depth-first traversal of the tree.
V8_OBJECT class ConsString : public String {
 public:
  inline Tagged<String> first() const;
  inline void set_first(Tagged<String> value,
                        WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  inline Tagged<String> second() const;
  inline void set_second(Tagged<String> value,
                         WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // Doesn't check that the result is a string, even in debug mode.  This is
  // useful during GC where the mark bits confuse the checks.
  inline Tagged<Object> unchecked_first() const;

  // Doesn't check that the result is a string, even in debug mode.  This is
  // useful during GC where the mark bits confuse the checks.
  inline Tagged<Object> unchecked_second() const;

  V8_INLINE bool IsFlat() const;

  // Dispatched behavior.
  V8_EXPORT_PRIVATE uint16_t
  Get(int index, const SharedStringAccessGuardIfNeeded& access_guard) const;

  // Minimum length for a cons string.
  static const int kMinLength = 13;

  DECL_VERIFIER(ConsString)

 private:
  friend struct ObjectTraits<ConsString>;
  friend struct OffsetsForDebug;
  friend class V8HeapExplorer;
  friend class CodeStubAssembler;
  friend class ToDirectStringAssembler;
  friend class StringBuiltinsAssembler;
  friend class maglev::MaglevAssembler;
  friend class compiler::AccessBuilder;
  friend class TorqueGeneratedConsStringAsserts;

  friend Tagged<String> String::GetUnderlying() const;

  TaggedMember<String> first_;
  TaggedMember<String> second_;
} V8_OBJECT_END;

template <>
struct ObjectTraits<ConsString> {
  using BodyDescriptor =
      FixedBodyDescriptor<offsetof(ConsString, first_), sizeof(ConsString),
                          sizeof(ConsString)>;
};

// The ThinString class describes string objects that are just references
// to another string object. They are used for in-place internalization when
// the original string cannot actually be internalized in-place: in these
// cases, the original string is converted to a ThinString pointing at its
// internalized version (which is allocated as a new object).
// In terms of memory layout and most algorithms operating on strings,
// ThinStrings can be thought of as "one-part cons strings".
V8_OBJECT class ThinString : public String {
 public:
  inline Tagged<String> actual() const;
  inline void set_actual(Tagged<String> value,
                         WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  inline Tagged<HeapObject> unchecked_actual() const;

  V8_EXPORT_PRIVATE uint16_t
  Get(int index, const SharedStringAccessGuardIfNeeded& access_guard) const;

  DECL_VERIFIER(ThinString)

 private:
  friend struct ObjectTraits<ThinString>;
  friend struct OffsetsForDebug;
  friend class V8HeapExplorer;
  friend class CodeStubAssembler;
  friend class ToDirectStringAssembler;
  friend class StringBuiltinsAssembler;
  friend class maglev::MaglevAssembler;
  friend class maglev::CheckedInternalizedString;
  friend class compiler::AccessBuilder;
  friend class FullStringForwardingTableCleaner;
  friend class TorqueGeneratedThinStringAsserts;

  friend Tagged<String> String::GetUnderlying() const;

  TaggedMember<String> actual_;
} V8_OBJECT_END;

template <>
struct ObjectTraits<ThinString> {
  using BodyDescriptor =
      FixedBodyDescriptor<offsetof(ThinString, actual_), sizeof(ThinString),
                          sizeof(ThinString)>;
};

// The Sliced String class describes strings that are substrings of another
// sequential string.  The motivation is to save time and memory when creating
// a substring.  A Sliced String is described as a pointer to the parent,
// the offset from the start of the parent string and the length.  Using
// a Sliced String therefore requires unpacking of the parent string and
// adding the offset to the start address.  A substring of a Sliced String
// are not nested since the double indirection is simplified when creating
// such a substring.
// Currently missing features are:
//  - truncating sliced string to enable otherwise unneeded parent to be GC'ed.
V8_OBJECT class SlicedString : public String {
 public:
  inline Tagged<String> parent() const;
  inline void set_parent(Tagged<String> parent,
                         WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  inline int32_t offset() const;
  inline void set_offset(int32_t offset);

  // Dispatched behavior.
  V8_EXPORT_PRIVATE uint16_t
  Get(int index, const SharedStringAccessGuardIfNeeded& access_guard) const;

  // Minimum length for a sliced string.
  static const int kMinLength = 13;

  DECL_VERIFIER(SlicedString)
 private:
  friend struct ObjectTraits<SlicedString>;
  friend struct OffsetsForDebug;
  friend class V8HeapExplorer;
  friend class CodeStubAssembler;
  friend class SandboxTesting;
  friend class ToDirectStringAssembler;
  friend class maglev::MaglevAssembler;
  friend class compiler::AccessBuilder;
  friend class TorqueGeneratedSlicedStringAsserts;

  friend Tagged<String> String::GetUnderlying() const;

  TaggedMember<String> parent_;
  TaggedMember<Smi> offset_;
} V8_OBJECT_END;

template <>
struct ObjectTraits<SlicedString> {
  using BodyDescriptor =
      FixedBodyDescriptor<offsetof(SlicedString, parent_), sizeof(SlicedString),
                          sizeof(SlicedString)>;
};

// TODO(leszeks): Build this out into a full V8 class.
V8_OBJECT class UncachedExternalString : public String {
 protected:
  ExternalPointerMember<kExternalStringResourceTag> resource_;
} V8_OBJECT_END;

// The ExternalString class describes string values that are backed by
// a string resource that lies outside the V8 heap.  ExternalStrings
// consist of the length field common to all strings, a pointer to the
// external resource.  It is important to ensure (externally) that the
// resource is not deallocated while the ExternalString is live in the
// V8 heap.
//
// The API expects that all ExternalStrings are created through the
// API.  Therefore, ExternalStrings should not be used internally.
V8_OBJECT class ExternalString : public UncachedExternalString {
 public:
  class BodyDescriptor;

  DECL_VERIFIER(ExternalString)

  inline void InitExternalPointerFields(Isolate* isolate);
  inline void VisitExternalPointers(ObjectVisitor* visitor);

  // Return whether the external string data pointer is not cached.
  inline bool is_uncached() const;
  // Size in bytes of the external payload.
  int ExternalPayloadSize() const;

  // Used in the serializer/deserializer.
  inline Address resource_as_address() const;
  inline void set_address_as_resource(Isolate* isolate, Address address);
  inline uint32_t GetResourceRefForDeserialization();
  inline void SetResourceRefForSerialization(uint32_t ref);

  // Disposes string's resource object if it has not already been disposed.
  inline void DisposeResource(Isolate* isolate);

  void InitExternalPointerFieldsDuringExternalization(Tagged<Map> new_map,
                                                      Isolate* isolate);

 private:
  friend ObjectTraits<ExternalString>;
  friend struct OffsetsForDebug;
  friend class CodeStubAssembler;
  friend class compiler::AccessBuilder;
  friend class TorqueGeneratedExternalStringAsserts;

 protected:
  ExternalPointerMember<kExternalStringResourceDataTag> resource_data_;
} V8_OBJECT_END;

template <>
struct ObjectTraits<ExternalString> {
  using BodyDescriptor = ExternalString::BodyDescriptor;

  static_assert(offsetof(ExternalString, resource_) ==
                Internals::kStringResourceOffset);
};

// The ExternalOneByteString class is an external string backed by an
// one-byte string.
V8_OBJECT class ExternalOneByteString : public ExternalString {
 public:
  static const bool kHasOneByteEncoding = true;

  using Resource = v8::String::ExternalOneByteStringResource;

  // The underlying resource.
  inline const Resource* resource() const;

  // It is assumed that the previous resource is null. If it is not null, then
  // it is the responsability of the caller the handle the previous resource.
  inline void SetResource(Isolate* isolate, const Resource* buffer);

  // Used only during serialization.
  inline void set_resource(Isolate* isolate, const Resource* buffer);

  // Update the pointer cache to the external character array.
  // The cached pointer is always valid, as the external character array does =
  // not move during lifetime.  Deserialization is the only exception, after
  // which the pointer cache has to be refreshed.
  inline void update_data_cache(Isolate* isolate);

  inline const uint8_t* GetChars() const;

  // Dispatched behavior.
  inline uint8_t Get(int index,
                     const SharedStringAccessGuardIfNeeded& access_guard) const;

 private:
  // The underlying resource as a non-const pointer.
  inline Resource* mutable_resource();
} V8_OBJECT_END;

static_assert(sizeof(ExternalOneByteString) == sizeof(ExternalString));

// The ExternalTwoByteString class is an external string backed by a UTF-16
// encoded string.
V8_OBJECT class ExternalTwoByteString : public ExternalString {
 public:
  static const bool kHasOneByteEncoding = false;

  using Resource = v8::String::ExternalStringResource;

  // The underlying string resource.
  inline const Resource* resource() const;

  // It is assumed that the previous resource is null. If it is not null, then
  // it is the responsability of the caller the handle the previous resource.
  inline void SetResource(Isolate* isolate, const Resource* buffer);

  // Used only during serialization.
  inline void set_resource(Isolate* isolate, const Resource* buffer);

  // Update the pointer cache to the external character array.
  // The cached pointer is always valid, as the external character array does =
  // not move during lifetime.  Deserialization is the only exception, after
  // which the pointer cache has to be refreshed.
  inline void update_data_cache(Isolate* isolate);

  inline const uint16_t* GetChars() const;

  // Dispatched behavior.
  inline uint16_t Get(
      int index, const SharedStringAccessGuardIfNeeded& access_guard) const;

  // For regexp code.
  inline const uint16_t* ExternalTwoByteStringGetData(unsigned start);

 private:
  // The underlying resource as a non-const pointer.
  inline Resource* mutable_resource();
} V8_OBJECT_END;

static_assert(sizeof(ExternalTwoByteString) == sizeof(ExternalString));

// A flat string reader provides random access to the contents of a
// string independent of the character width of the string. The handle
// must be valid as long as the reader is being used.
// Not safe to use from concurrent background threads.
class V8_EXPORT_PRIVATE FlatStringReader : public Relocatable {
 public:
  FlatStringReader(Isolate* isolate, Handle<String> str);
  void PostGarbageCollection() override;
  inline base::uc32 Get(int index) const;
  template <typename Char>
  inline Char Get(int index) const;
  int length() const { return length_; }

 private:
  Handle<String> str_;
  bool is_one_byte_;
  int const length_;
  const void* start_;
};

// This maintains an off-stack representation of the stack frames required
// to traverse a ConsString, allowing an entirely iterative and restartable
// traversal of the entire string
class ConsStringIterator {
 public:
  inline ConsStringIterator() = default;
  inline explicit ConsStringIterator(Tagged<ConsString> cons_string,
                                     int offset = 0) {
    Reset(cons_string, offset);
  }
  ConsStringIterator(const ConsStringIterator&) = delete;
  ConsStringIterator& operator=(const ConsStringIterator&) = delete;
  inline void Reset(Tagged<ConsString> cons_string, int offset = 0) {
    depth_ = 0;
    // Next will always return nullptr.
    if (cons_string.is_null()) return;
    Initialize(cons_string, offset);
  }
  // Returns nullptr when complete. The offset_out parameter will be set to the
  // offset within the returned segment that the user should start looking at,
  // to match the offset passed into the constructor or Reset -- this will only
  // be non-zero immediately after construction or Reset, and only if those had
  // a non-zero offset.
  inline Tagged<String> Next(int* offset_out) {
    *offset_out = 0;
    if (depth_ == 0) return Tagged<String>();
    return Continue(offset_out);
  }

 private:
  static const int kStackSize = 32;
  // Use a mask instead of doing modulo operations for stack wrapping.
  static const int kDepthMask = kStackSize - 1;
  static_assert(base::bits::IsPowerOfTwo(kStackSize),
                "kStackSize must be power of two");
  static inline int OffsetForDepth(int depth);

  inline void PushLeft(Tagged<ConsString> string);
  inline void PushRight(Tagged<ConsString> string);
  inline void AdjustMaximumDepth();
  inline void Pop();
  inline bool StackBlown() { return maximum_depth_ - depth_ == kStackSize; }
  V8_EXPORT_PRIVATE void Initialize(Tagged<ConsString> cons_string, int offset);
  V8_EXPORT_PRIVATE Tagged<String> Continue(int* offset_out);
  Tagged<String> NextLeaf(bool* blew_stack);
  Tagged<String> Search(int* offset_out);

  // Stack must always contain only frames for which right traversal
  // has not yet been performed.
  Tagged<ConsString> frames_[kStackSize];
  Tagged<ConsString> root_;
  int depth_;
  int maximum_depth_;
  int consumed_;
};

class StringCharacterStream;

template <typename Char>
struct CharTraits;

template <>
struct CharTraits<uint8_t> {
  using String = SeqOneByteString;
  using ExternalString = ExternalOneByteString;
};

template <>
struct CharTraits<uint16_t> {
  using String = SeqTwoByteString;
  using ExternalString = ExternalTwoByteString;
};

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_STRING_H_

// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_REGEXP_H_
#define V8_OBJECTS_JS_REGEXP_H_

#include <optional>

#include "include/v8-regexp.h"
#include "src/objects/contexts.h"
#include "src/objects/js-array.h"
#include "src/regexp/regexp-flags.h"
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

class RegExpData;

#include "torque-generated/src/objects/js-regexp-tq.inc"

class RegExpData;

// Regular expressions
class JSRegExp : public TorqueGeneratedJSRegExp<JSRegExp, JSObject> {
 public:
  DEFINE_TORQUE_GENERATED_JS_REG_EXP_FLAGS()

  V8_EXPORT_PRIVATE static MaybeHandle<JSRegExp> New(
      Isolate* isolate, Handle<String> source, Flags flags,
      uint32_t backtrack_limit = kNoBacktrackLimit);

  static MaybeHandle<JSRegExp> Initialize(
      Handle<JSRegExp> regexp, Handle<String> source, Flags flags,
      uint32_t backtrack_limit = kNoBacktrackLimit);
  static MaybeHandle<JSRegExp> Initialize(Handle<JSRegExp> regexp,
                                          Handle<String> source,
                                          Handle<String> flags_string);

  DECL_ACCESSORS(last_index, Tagged<Object>)

  // Instance fields accessors.
  inline Tagged<String> source() const;
  inline Flags flags() const;

  DECL_TRUSTED_POINTER_ACCESSORS(data, RegExpData)

  static constexpr Flag AsJSRegExpFlag(RegExpFlag f) {
    return static_cast<Flag>(f);
  }
  static constexpr Flags AsJSRegExpFlags(RegExpFlags f) {
    return Flags{static_cast<int>(f)};
  }
  static constexpr RegExpFlags AsRegExpFlags(Flags f) {
    return RegExpFlags{static_cast<int>(f)};
  }

  static std::optional<RegExpFlag> FlagFromChar(char c) {
    std::optional<RegExpFlag> f = TryRegExpFlagFromChar(c);
    if (!f.has_value()) return f;
    if (f.value() == RegExpFlag::kLinear &&
        !v8_flags.enable_experimental_regexp_engine) {
      return {};
    }
    return f;
  }

  static_assert(static_cast<int>(kNone) == v8::RegExp::kNone);
#define V(_, Camel, ...)                                             \
  static_assert(static_cast<int>(k##Camel) == v8::RegExp::k##Camel); \
  static_assert(static_cast<int>(k##Camel) ==                        \
                static_cast<int>(RegExpFlag::k##Camel));
  REGEXP_FLAG_LIST(V)
#undef V
  static_assert(kFlagCount == v8::RegExp::kFlagCount);
  static_assert(kFlagCount == kRegExpFlagCount);

  static std::optional<Flags> FlagsFromString(Isolate* isolate,
                                              Handle<String> flags);

  V8_EXPORT_PRIVATE static Handle<String> StringFromFlags(Isolate* isolate,
                                                          Flags flags);

  inline Tagged<String> EscapedPattern();

  // Each capture (including the match itself) needs two registers.
  static constexpr int RegistersForCaptureCount(int count) {
    return (count + 1) * 2;
  }
  static constexpr int CaptureCountForRegisters(int register_count) {
    DCHECK_EQ(register_count % 2, 0);
    DCHECK_GE(register_count, 2);
    return (register_count - 2) / 2;
  }

  // Dispatched behavior.
  DECL_PRINTER(JSRegExp)
  DECL_VERIFIER(JSRegExp)

  /* This is already an in-object field. */
  // TODO(v8:8944): improve handling of in-object fields
  static constexpr int kLastIndexOffset = kHeaderSize;

  // The initial value of the last_index field on a new JSRegExp instance.
  static constexpr int kInitialLastIndexValue = 0;

  // In-object fields.
  static constexpr int kLastIndexFieldIndex = 0;
  static constexpr int kInObjectFieldCount = 1;

  // The actual object size including in-object fields.
  static constexpr int kSize = kHeaderSize + kInObjectFieldCount * kTaggedSize;
  static constexpr int Size() { return kSize; }

  // Descriptor array index to important methods in the prototype.
  static constexpr int kExecFunctionDescriptorIndex = 1;
  static constexpr int kSymbolMatchFunctionDescriptorIndex = 15;
  static constexpr int kSymbolMatchAllFunctionDescriptorIndex = 16;
  static constexpr int kSymbolReplaceFunctionDescriptorIndex = 17;
  static constexpr int kSymbolSearchFunctionDescriptorIndex = 18;
  static constexpr int kSymbolSplitFunctionDescriptorIndex = 19;

  // The uninitialized value for a regexp code object.
  static constexpr int kUninitializedValue = -1;

  // If the backtrack limit is set to this marker value, no limit is applied.
  static constexpr uint32_t kNoBacktrackLimit = 0;

  // The heuristic value for the length of the subject string for which we
  // tier-up to the compiler immediately, instead of using the interpreter.
  static constexpr int kTierUpForSubjectLengthValue = 1000;

  // Maximum number of captures allowed.
  static constexpr int kMaxCaptures = 1 << 16;

  class BodyDescriptor;

 private:
  using FlagsBuffer = base::EmbeddedVector<char, kFlagCount + 1>;
  inline static const char* FlagsToString(Flags flags, FlagsBuffer* out_buffer);

  friend class RegExpData;

  TQ_OBJECT_CONSTRUCTORS(JSRegExp)
};

DEFINE_OPERATORS_FOR_FLAGS(JSRegExp::Flags)

class RegExpDataWrapper;

class RegExpData : public ExposedTrustedObject {
 public:
  enum class Type : uint8_t {
    ATOM,          // A simple string match.
    IRREGEXP,      // Compiled with Irregexp (code or bytecode).
    EXPERIMENTAL,  // Compiled to use the experimental linear time engine.
  };

  inline Type type_tag() const;
  inline void set_type_tag(Type);

  DECL_ACCESSORS(source, Tagged<String>)

  inline JSRegExp::Flags flags() const;
  inline void set_flags(JSRegExp::Flags flags);

  DECL_ACCESSORS(wrapper, Tagged<RegExpDataWrapper>)

  inline int capture_count() const;

  static constexpr bool TypeSupportsCaptures(Type t) {
    return t == Type::IRREGEXP || t == Type::EXPERIMENTAL;
  }

  V8_EXPORT_PRIVATE bool HasCompiledCode() const;

  DECL_PRINTER(RegExpData)
  DECL_VERIFIER(RegExpData)

#define FIELD_LIST(V)            \
  V(kTypeTagOffset, kTaggedSize) \
  V(kSourceOffset, kTaggedSize)  \
  V(kFlagsOffset, kTaggedSize)   \
  V(kWrapperOffset, kTaggedSize) \
  V(kHeaderSize, 0)              \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(ExposedTrustedObject::kHeaderSize, FIELD_LIST)

#undef FIELD_LIST

  class BodyDescriptor;

  OBJECT_CONSTRUCTORS(RegExpData, ExposedTrustedObject);
};

class RegExpDataWrapper : public Struct {
 public:
  DECL_TRUSTED_POINTER_ACCESSORS(data, RegExpData)

  DECL_PRINTER(RegExpDataWrapper)
  DECL_VERIFIER(RegExpDataWrapper)

#define FIELD_LIST(V)                 \
  V(kDataOffset, kTrustedPointerSize) \
  V(kHeaderSize, 0)                   \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(Struct::kHeaderSize, FIELD_LIST)
#undef FIELD_LIST

  class BodyDescriptor;

  OBJECT_CONSTRUCTORS(RegExpDataWrapper, Struct);
};

class AtomRegExpData : public RegExpData {
 public:
  DECL_ACCESSORS(pattern, Tagged<String>)

  DECL_PRINTER(AtomRegExpData)
  DECL_VERIFIER(AtomRegExpData)

#define FIELD_LIST(V)            \
  V(kPatternOffset, kTaggedSize) \
  V(kHeaderSize, 0)              \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(RegExpData::kHeaderSize, FIELD_LIST)

#undef FIELD_LIST

  class BodyDescriptor;

  OBJECT_CONSTRUCTORS(AtomRegExpData, RegExpData);
};

class IrRegExpData : public RegExpData {
 public:
  DECL_CODE_POINTER_ACCESSORS(latin1_code)
  DECL_CODE_POINTER_ACCESSORS(uc16_code)
  inline bool has_code(bool is_one_byte) const;
  inline void set_code(bool is_one_byte, Tagged<Code> code);
  inline Tagged<Code> code(IsolateForSandbox isolate, bool is_one_byte) const;
  DECL_PROTECTED_POINTER_ACCESSORS(latin1_bytecode, TrustedByteArray)
  DECL_PROTECTED_POINTER_ACCESSORS(uc16_bytecode, TrustedByteArray)
  inline bool has_bytecode(bool is_one_byte) const;
  inline void clear_bytecode(bool is_one_byte);
  inline void set_bytecode(bool is_one_byte, Tagged<TrustedByteArray> bytecode);
  inline Tagged<TrustedByteArray> bytecode(bool is_one_byte) const;
  DECL_ACCESSORS(capture_name_map, Tagged<Object>)
  inline void set_capture_name_map(Handle<FixedArray> capture_name_map);
  DECL_INT_ACCESSORS(max_register_count)
  // Number of captures (without the match itself).
  DECL_INT_ACCESSORS(capture_count)
  DECL_INT_ACCESSORS(ticks_until_tier_up)
  DECL_INT_ACCESSORS(backtrack_limit)

  bool CanTierUp();
  bool MarkedForTierUp();
  void ResetLastTierUpTick();
  void TierUpTick();
  void MarkTierUpForNextExec();
  bool ShouldProduceBytecode();

  void DiscardCompiledCodeForSerialization();

  // Sets the bytecode as well as initializing trampoline slots to the
  // RegExpExperimentalTrampoline.
  void SetBytecodeForExperimental(Isolate* isolate,
                                  Tagged<TrustedByteArray> bytecode);

  DECL_PRINTER(IrRegExpData)
  DECL_VERIFIER(IrRegExpData)

#define FIELD_LIST(V)                             \
  V(kLatin1BytecodeOffset, kProtectedPointerSize) \
  V(kUc16BytecodeOffset, kProtectedPointerSize)   \
  V(kLatin1CodeOffset, kCodePointerSize)          \
  V(kUc16CodeOffset, kCodePointerSize)            \
  V(kCaptureNameMapOffset, kTaggedSize)           \
  V(kMaxRegisterCountOffset, kTaggedSize)         \
  V(kCaptureCountOffset, kTaggedSize)             \
  V(kTicksUntilTierUpOffset, kTaggedSize)         \
  V(kBacktrackLimitOffset, kTaggedSize)           \
  V(kHeaderSize, 0)                               \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(RegExpData::kHeaderSize, FIELD_LIST)

#undef FIELD_LIST

  class BodyDescriptor;

  OBJECT_CONSTRUCTORS(IrRegExpData, RegExpData);
};

// JSRegExpResult is just a JSArray with a specific initial map.
// This initial map adds in-object properties for "index" and "input"
// properties, as assigned by RegExp.prototype.exec, which allows
// faster creation of RegExp exec results.
// This class just holds constants used when creating the result.
// After creation the result must be treated as a JSArray in all regards.
class JSRegExpResult
    : public TorqueGeneratedJSRegExpResult<JSRegExpResult, JSArray> {
 public:
  // TODO(joshualitt): We would like to add printers and verifiers to
  // JSRegExpResult, and maybe JSRegExpResultIndices, but both have the same
  // instance type as JSArray.

  // Indices of in-object properties.
  static constexpr int kIndexIndex = 0;
  static constexpr int kInputIndex = 1;
  static constexpr int kGroupsIndex = 2;

  // Private internal only fields.
  static constexpr int kNamesIndex = 3;
  static constexpr int kRegExpInputIndex = 4;
  static constexpr int kRegExpLastIndex = 5;
  static constexpr int kInObjectPropertyCount = 6;

  static constexpr int kMapIndexInContext = Context::REGEXP_RESULT_MAP_INDEX;

  TQ_OBJECT_CONSTRUCTORS(JSRegExpResult)
};

class JSRegExpResultWithIndices
    : public TorqueGeneratedJSRegExpResultWithIndices<JSRegExpResultWithIndices,
                                                      JSRegExpResult> {
 public:
  static_assert(
      JSRegExpResult::kInObjectPropertyCount == 6,
      "JSRegExpResultWithIndices must be a subclass of JSRegExpResult");
  static constexpr int kIndicesIndex = 6;
  static constexpr int kInObjectPropertyCount = 7;

  TQ_OBJECT_CONSTRUCTORS(JSRegExpResultWithIndices)
};

// JSRegExpResultIndices is just a JSArray with a specific initial map.
// This initial map adds in-object properties for "group"
// properties, as assigned by RegExp.prototype.exec, which allows
// faster creation of RegExp exec results.
// This class just holds constants used when creating the result.
// After creation the result must be treated as a JSArray in all regards.
class JSRegExpResultIndices
    : public TorqueGeneratedJSRegExpResultIndices<JSRegExpResultIndices,
                                                  JSArray> {
 public:
  static Handle<JSRegExpResultIndices> BuildIndices(
      Isolate* isolate, DirectHandle<RegExpMatchInfo> match_info,
      Handle<Object> maybe_names);

  // Indices of in-object properties.
  static constexpr int kGroupsIndex = 0;
  static constexpr int kInObjectPropertyCount = 1;

  // Descriptor index of groups.
  static constexpr int kGroupsDescriptorIndex = 1;

  TQ_OBJECT_CONSTRUCTORS(JSRegExpResultIndices)
};

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_REGEXP_H_

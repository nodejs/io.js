// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_REGEXP_H_
#define V8_OBJECTS_JS_REGEXP_H_

#include "include/v8-regexp.h"
#include "src/objects/contexts.h"
#include "src/objects/js-array.h"
#include "src/regexp/regexp-flags.h"
#include "torque-generated/bit-fields.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-regexp-tq.inc"

// Regular expressions
// The regular expression holds a single reference to a FixedArray in
// the kDataOffset field.
// The FixedArray contains the following data:
// - tag : type of regexp implementation (not compiled yet, atom or irregexp)
// - reference to the original source string
// - reference to the original flag string
// If it is an atom regexp
// - a reference to a literal string to search for
// If it is an irregexp regexp:
// - a reference to code for Latin1 inputs (bytecode or compiled), or a smi
// used for tracking the last usage (used for regexp code flushing).
// - a reference to code for UC16 inputs (bytecode or compiled), or a smi
// used for tracking the last usage (used for regexp code flushing).
// - max number of registers used by irregexp implementations.
// - number of capture registers (output values) of the regexp.
class JSRegExp : public TorqueGeneratedJSRegExp<JSRegExp, JSObject> {
 public:
  enum Type {
    NOT_COMPILED,  // Initial value. No data array has been set yet.
    ATOM,          // A simple string match.
    IRREGEXP,      // Compiled with Irregexp (code or bytecode).
    EXPERIMENTAL,  // Compiled to use the experimental linear time engine.
  };
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

  // Data array field accessors.

  inline Type type_tag() const;
  inline Tagged<String> atom_pattern() const;
  // This could be a Smi kUninitializedValue or InstructionStream.
  V8_EXPORT_PRIVATE Tagged<Object> code(IsolateForSandbox isolate,
                                        bool is_latin1) const;
  V8_EXPORT_PRIVATE void set_code(bool is_unicode, Handle<Code> code);
  // This could be a Smi kUninitializedValue or ByteArray.
  V8_EXPORT_PRIVATE Tagged<Object> bytecode(bool is_latin1) const;
  // Sets the bytecode as well as initializing trampoline slots to the
  // RegExpInterpreterTrampoline.
  void set_bytecode_and_trampoline(Isolate* isolate,
                                   Handle<ByteArray> bytecode);
  inline int max_register_count() const;
  // Number of captures (without the match itself).
  inline int capture_count() const;
  inline Tagged<Object> capture_name_map();
  inline void set_capture_name_map(Handle<FixedArray> capture_name_map);
  uint32_t backtrack_limit() const;

  static constexpr Flag AsJSRegExpFlag(RegExpFlag f) {
    return static_cast<Flag>(f);
  }
  static constexpr Flags AsJSRegExpFlags(RegExpFlags f) {
    return Flags{static_cast<int>(f)};
  }
  static constexpr RegExpFlags AsRegExpFlags(Flags f) {
    return RegExpFlags{static_cast<int>(f)};
  }

  static base::Optional<RegExpFlag> FlagFromChar(char c) {
    base::Optional<RegExpFlag> f = TryRegExpFlagFromChar(c);
    if (!f.has_value()) return f;
    if (f.value() == RegExpFlag::kLinear &&
        !v8_flags.enable_experimental_regexp_engine) {
      return {};
    }
    if (f.value() == RegExpFlag::kUnicodeSets &&
        !v8_flags.harmony_regexp_unicode_sets) {
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

  static base::Optional<Flags> FlagsFromString(Isolate* isolate,
                                               Handle<String> flags);

  V8_EXPORT_PRIVATE static Handle<String> StringFromFlags(Isolate* isolate,
                                                          Flags flags);

  inline Tagged<String> EscapedPattern();

  bool CanTierUp();
  bool MarkedForTierUp();
  void ResetLastTierUpTick();
  void TierUpTick();
  void MarkTierUpForNextExec();

  bool ShouldProduceBytecode();
  inline bool HasCompiledCode() const;
  inline void DiscardCompiledCodeForSerialization();

  static constexpr bool TypeSupportsCaptures(Type t) {
    return t == IRREGEXP || t == EXPERIMENTAL;
  }

  // Each capture (including the match itself) needs two registers.
  static constexpr int RegistersForCaptureCount(int count) {
    return (count + 1) * 2;
  }
  static constexpr int CaptureCountForRegisters(int register_count) {
    DCHECK_EQ(register_count % 2, 0);
    DCHECK_GE(register_count, 2);
    return (register_count - 2) / 2;
  }

  static constexpr int code_index(bool is_latin1) {
    return is_latin1 ? kIrregexpLatin1CodeIndex : kIrregexpUC16CodeIndex;
  }

  static constexpr int bytecode_index(bool is_latin1) {
    return is_latin1 ? kIrregexpLatin1BytecodeIndex
                     : kIrregexpUC16BytecodeIndex;
  }

  // Dispatched behavior.
  DECL_PRINTER(JSRegExp)
  DECL_VERIFIER(JSRegExp)

  /* This is already an in-object field. */
  // TODO(v8:8944): improve handling of in-object fields
  static constexpr int kLastIndexOffset = kHeaderSize;

  // The initial value of the last_index field on a new JSRegExp instance.
  static constexpr int kInitialLastIndexValue = 0;

  // Indices in the data array.
  static constexpr int kTagIndex = 0;
  static constexpr int kSourceIndex = kTagIndex + 1;
  static constexpr int kFlagsIndex = kSourceIndex + 1;
  static constexpr int kFirstTypeSpecificIndex = kFlagsIndex + 1;
  static constexpr int kMinDataArrayLength = kFirstTypeSpecificIndex;

  // The data fields are used in different ways depending on the
  // value of the tag.
  // Atom regexps (literal strings).
  static constexpr int kAtomPatternIndex = kFirstTypeSpecificIndex;
  static constexpr int kAtomDataSize = kAtomPatternIndex + 1;

  // A InstructionStream object or a Smi marker value equal to
  // kUninitializedValue.
  static constexpr int kIrregexpLatin1CodeIndex = kFirstTypeSpecificIndex;
  static constexpr int kIrregexpUC16CodeIndex = kIrregexpLatin1CodeIndex + 1;
  // A ByteArray object or a Smi marker value equal to kUninitializedValue.
  static constexpr int kIrregexpLatin1BytecodeIndex =
      kIrregexpUC16CodeIndex + 1;
  static constexpr int kIrregexpUC16BytecodeIndex =
      kIrregexpLatin1BytecodeIndex + 1;
  // Maximal number of registers used by either Latin1 or UC16.
  // Only used to check that there is enough stack space
  static constexpr int kIrregexpMaxRegisterCountIndex =
      kIrregexpUC16BytecodeIndex + 1;
  // Number of captures in the compiled regexp.
  static constexpr int kIrregexpCaptureCountIndex =
      kIrregexpMaxRegisterCountIndex + 1;
  // Maps names of named capture groups (at indices 2i) to their corresponding
  // (1-based) capture group indices (at indices 2i + 1).
  static constexpr int kIrregexpCaptureNameMapIndex =
      kIrregexpCaptureCountIndex + 1;
  // Tier-up ticks are set to the value of the tier-up ticks flag. The value is
  // decremented on each execution of the bytecode, so that the tier-up
  // happens once the ticks reach zero.
  // This value is ignored if the regexp-tier-up flag isn't turned on.
  static constexpr int kIrregexpTicksUntilTierUpIndex =
      kIrregexpCaptureNameMapIndex + 1;
  // A smi containing either the backtracking limit or kNoBacktrackLimit.
  // TODO(jgruber): If needed, this limit could be packed into other fields
  // above to save space.
  static constexpr int kIrregexpBacktrackLimit =
      kIrregexpTicksUntilTierUpIndex + 1;
  static constexpr int kIrregexpDataSize = kIrregexpBacktrackLimit + 1;

  // TODO(mbid,v8:10765): At the moment the EXPERIMENTAL data array conforms
  // to the format of an IRREGEXP data array, with most fields set to some
  // default/uninitialized value. This is because EXPERIMENTAL and IRREGEXP
  // regexps take the same code path in `RegExpExecInternal`, which reads off
  // various fields from the data array. `RegExpExecInternal` should probably
  // distinguish between EXPERIMENTAL and IRREGEXP, and then we can get rid of
  // all the IRREGEXP only fields.
  static constexpr int kExperimentalDataSize = kIrregexpDataSize;

  // In-object fields.
  static constexpr int kLastIndexFieldIndex = 0;
  static constexpr int kInObjectFieldCount = 1;

  // The actual object size including in-object fields.
  static constexpr int Size() {
    return kHeaderSize + kInObjectFieldCount * kTaggedSize;
  }

  // Descriptor array index to important methods in the prototype.
  static constexpr int kExecFunctionDescriptorIndex = 1;
  static constexpr int kSymbolMatchFunctionDescriptorIndex = 14;
  static constexpr int kSymbolMatchAllFunctionDescriptorIndex = 15;
  static constexpr int kSymbolReplaceFunctionDescriptorIndex = 16;
  static constexpr int kSymbolSearchFunctionDescriptorIndex = 17;
  static constexpr int kSymbolSplitFunctionDescriptorIndex = 18;

  // The uninitialized value for a regexp code object.
  static constexpr int kUninitializedValue = -1;

  // If the backtrack limit is set to this marker value, no limit is applied.
  static constexpr uint32_t kNoBacktrackLimit = 0;

  // The heuristic value for the length of the subject string for which we
  // tier-up to the compiler immediately, instead of using the interpreter.
  static constexpr int kTierUpForSubjectLengthValue = 1000;

  // Maximum number of captures allowed.
  static constexpr int kMaxCaptures = 1 << 16;

 private:
  using FlagsBuffer = base::EmbeddedVector<char, kFlagCount + 1>;
  inline static const char* FlagsToString(Flags flags, FlagsBuffer* out_buffer);

  inline Tagged<Object> DataAt(int index) const;
  inline void SetDataAt(int index, Tagged<Object> value);

  TQ_OBJECT_CONSTRUCTORS(JSRegExp)
};

DEFINE_OPERATORS_FOR_FLAGS(JSRegExp::Flags)

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
      Isolate* isolate, Handle<RegExpMatchInfo> match_info,
      Handle<Object> maybe_names);

  // Indices of in-object properties.
  static constexpr int kGroupsIndex = 0;
  static constexpr int kInObjectPropertyCount = 1;

  // Descriptor index of groups.
  static constexpr int kGroupsDescriptorIndex = 1;

  TQ_OBJECT_CONSTRUCTORS(JSRegExpResultIndices)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_REGEXP_H_

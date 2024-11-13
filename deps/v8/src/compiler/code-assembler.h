// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_CODE_ASSEMBLER_H_
#define V8_COMPILER_CODE_ASSEMBLER_H_

#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <type_traits>

// Clients of this interface shouldn't depend on lots of compiler internals.
// Do not include anything from src/compiler here!
#include "include/cppgc/source-location.h"
#include "src/base/macros.h"
#include "src/builtins/builtins.h"
#include "src/codegen/atomic-memory-order.h"
#include "src/codegen/callable.h"
#include "src/codegen/handler-table.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/source-position.h"
#include "src/codegen/tnode.h"
#include "src/heap/heap.h"
#include "src/objects/object-type.h"
#include "src/objects/objects.h"
#include "src/runtime/runtime.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

// Forward declarations.
class AsmWasmData;
class AsyncGeneratorRequest;
struct AssemblerOptions;
class BigInt;
class CallInterfaceDescriptor;
class Callable;
class Factory;
class InterpreterData;
class Isolate;
class JSAsyncFunctionObject;
class JSAsyncGeneratorObject;
class JSCollator;
class JSCollection;
class JSDateTimeFormat;
class JSDisplayNames;
class JSDurationFormat;
class JSListFormat;
class JSLocale;
class JSNumberFormat;
class JSPluralRules;
class JSRegExpStringIterator;
class JSRelativeTimeFormat;
class JSSegmentIterator;
class JSSegmenter;
class JSSegments;
class JSV8BreakIterator;
class JSWeakCollection;
class JSFinalizationRegistry;
class JSWeakMap;
class JSWeakRef;
class JSWeakSet;
class OSROptimizedCodeCache;
class ProfileDataFromFile;
class PromiseCapability;
class PromiseFulfillReactionJobTask;
class PromiseReaction;
class PromiseReactionJobTask;
class PromiseRejectReactionJobTask;
class Zone;
#define MAKE_FORWARD_DECLARATION(Name) class Name;
TORQUE_DEFINED_CLASS_LIST(MAKE_FORWARD_DECLARATION)
#undef MAKE_FORWARD_DECLARATION

template <typename T>
class Signature;

enum class CheckBounds { kAlways, kDebugOnly };
inline bool NeedsBoundsCheck(CheckBounds check_bounds) {
  switch (check_bounds) {
    case CheckBounds::kAlways:
      return true;
    case CheckBounds::kDebugOnly:
      return DEBUG_BOOL;
  }
}

enum class StoreToObjectWriteBarrier { kNone, kMap, kFull };

class AccessCheckNeeded;
class BigIntBase;
class BigIntWrapper;
class ClassBoilerplate;
class BooleanWrapper;
class CompilationCacheTable;
class Constructor;
class Filler;
class FunctionTemplateRareData;
class HeapNumber;
class InternalizedString;
class JSArgumentsObject;
class JSArrayBufferView;
class JSContextExtensionObject;
class JSError;
class JSSloppyArgumentsObject;
class MapCache;
class NativeContext;
class NumberWrapper;
class ScriptWrapper;
class SloppyArgumentsElements;
class StringWrapper;
class SymbolWrapper;
class Undetectable;
class UniqueName;
class WasmCapiFunctionData;
class WasmTagObject;
class WasmExceptionPackage;
class WasmExceptionTag;
class WasmExportedFunctionData;
class WasmGlobalObject;
class WasmJSFunctionData;
class WasmMemoryObject;
class WasmModuleObject;
class WasmTableObject;

template <class T>
struct ObjectTypeOf {};

#define OBJECT_TYPE_CASE(Name)                           \
  template <>                                            \
  struct ObjectTypeOf<Name> {                            \
    static const ObjectType value = ObjectType::k##Name; \
  };
#define OBJECT_TYPE_STRUCT_CASE(NAME, Name, name)        \
  template <>                                            \
  struct ObjectTypeOf<Name> {                            \
    static const ObjectType value = ObjectType::k##Name; \
  };
#define OBJECT_TYPE_TEMPLATE_CASE(Name)                  \
  template <class... Args>                               \
  struct ObjectTypeOf<Name<Args...>> {                   \
    static const ObjectType value = ObjectType::k##Name; \
  };
#define OBJECT_TYPE_ODDBALL_CASE(Name)                    \
  template <>                                             \
  struct ObjectTypeOf<Name> {                             \
    static const ObjectType value = ObjectType::kOddball; \
  };
OBJECT_TYPE_CASE(Object)
OBJECT_TYPE_CASE(Smi)
OBJECT_TYPE_CASE(TaggedIndex)
OBJECT_TYPE_CASE(HeapObject)
OBJECT_TYPE_CASE(HeapObjectReference)
OBJECT_TYPE_LIST(OBJECT_TYPE_CASE)
HEAP_OBJECT_ORDINARY_TYPE_LIST(OBJECT_TYPE_CASE)
HEAP_OBJECT_TRUSTED_TYPE_LIST(OBJECT_TYPE_CASE)
STRUCT_LIST(OBJECT_TYPE_STRUCT_CASE)
HEAP_OBJECT_TEMPLATE_TYPE_LIST(OBJECT_TYPE_TEMPLATE_CASE)
OBJECT_TYPE_ODDBALL_CASE(Null)
OBJECT_TYPE_ODDBALL_CASE(Undefined)
OBJECT_TYPE_ODDBALL_CASE(True)
OBJECT_TYPE_ODDBALL_CASE(False)
#undef OBJECT_TYPE_CASE
#undef OBJECT_TYPE_STRUCT_CASE
#undef OBJECT_TYPE_TEMPLATE_CASE

#if defined(V8_HOST_ARCH_32_BIT)
#define BINT_IS_SMI
using BInt = Smi;
using AtomicInt64 = PairT<IntPtrT, IntPtrT>;
using AtomicUint64 = PairT<UintPtrT, UintPtrT>;
#elif defined(V8_HOST_ARCH_64_BIT)
#define BINT_IS_INTPTR
using BInt = IntPtrT;
using AtomicInt64 = IntPtrT;
using AtomicUint64 = UintPtrT;
#else
#error Unknown architecture.
#endif

namespace compiler {

class CallDescriptor;
class CodeAssemblerLabel;
class CodeAssemblerVariable;
template <class T>
class TypedCodeAssemblerVariable;
class CodeAssemblerState;
class JSGraph;
class Node;
class RawMachineAssembler;
class RawMachineLabel;
class SourcePositionTable;

using CodeAssemblerVariableList = ZoneVector<CodeAssemblerVariable*>;

using CodeAssemblerCallback = std::function<void()>;

template <class... Types>
class CodeAssemblerParameterizedLabel;

// This macro alias allows to use PairT<T1, T2> as a macro argument.
#define PAIR_TYPE(T1, T2) PairT<T1, T2>

#define CODE_ASSEMBLER_COMPARE_BINARY_OP_LIST(V)          \
  V(Float32Equal, BoolT, Float32T, Float32T)              \
  V(Float32LessThan, BoolT, Float32T, Float32T)           \
  V(Float32LessThanOrEqual, BoolT, Float32T, Float32T)    \
  V(Float32GreaterThan, BoolT, Float32T, Float32T)        \
  V(Float32GreaterThanOrEqual, BoolT, Float32T, Float32T) \
  V(Float64Equal, BoolT, Float64T, Float64T)              \
  V(Float64NotEqual, BoolT, Float64T, Float64T)           \
  V(Float64LessThan, BoolT, Float64T, Float64T)           \
  V(Float64LessThanOrEqual, BoolT, Float64T, Float64T)    \
  V(Float64GreaterThan, BoolT, Float64T, Float64T)        \
  V(Float64GreaterThanOrEqual, BoolT, Float64T, Float64T) \
  /* Use Word32Equal if you need Int32Equal */            \
  V(Int32GreaterThan, BoolT, Word32T, Word32T)            \
  V(Int32GreaterThanOrEqual, BoolT, Word32T, Word32T)     \
  V(Int32LessThan, BoolT, Word32T, Word32T)               \
  V(Int32LessThanOrEqual, BoolT, Word32T, Word32T)        \
  /* Use WordEqual if you need IntPtrEqual */             \
  V(IntPtrLessThan, BoolT, WordT, WordT)                  \
  V(IntPtrLessThanOrEqual, BoolT, WordT, WordT)           \
  V(IntPtrGreaterThan, BoolT, WordT, WordT)               \
  V(IntPtrGreaterThanOrEqual, BoolT, WordT, WordT)        \
  /* Use Word32Equal if you need Uint32Equal */           \
  V(Uint32LessThan, BoolT, Word32T, Word32T)              \
  V(Uint32LessThanOrEqual, BoolT, Word32T, Word32T)       \
  V(Uint32GreaterThan, BoolT, Word32T, Word32T)           \
  V(Uint32GreaterThanOrEqual, BoolT, Word32T, Word32T)    \
  /* Use Word64Equal if you need Uint64Equal */           \
  V(Uint64LessThan, BoolT, Word64T, Word64T)              \
  V(Uint64LessThanOrEqual, BoolT, Word64T, Word64T)       \
  V(Uint64GreaterThan, BoolT, Word64T, Word64T)           \
  V(Uint64GreaterThanOrEqual, BoolT, Word64T, Word64T)    \
  /* Use WordEqual if you need UintPtrEqual */            \
  V(UintPtrLessThan, BoolT, WordT, WordT)                 \
  V(UintPtrLessThanOrEqual, BoolT, WordT, WordT)          \
  V(UintPtrGreaterThan, BoolT, WordT, WordT)              \
  V(UintPtrGreaterThanOrEqual, BoolT, WordT, WordT)

#define CODE_ASSEMBLER_BINARY_OP_LIST(V)                                \
  CODE_ASSEMBLER_COMPARE_BINARY_OP_LIST(V)                              \
  V(Float32Sub, Float32T, Float32T, Float32T)                           \
  V(Float32Add, Float32T, Float32T, Float32T)                           \
  V(Float32Mul, Float32T, Float32T, Float32T)                           \
  V(Float64Add, Float64T, Float64T, Float64T)                           \
  V(Float64Sub, Float64T, Float64T, Float64T)                           \
  V(Float64Mul, Float64T, Float64T, Float64T)                           \
  V(Float64Div, Float64T, Float64T, Float64T)                           \
  V(Float64Mod, Float64T, Float64T, Float64T)                           \
  V(Float64Atan2, Float64T, Float64T, Float64T)                         \
  V(Float64Pow, Float64T, Float64T, Float64T)                           \
  V(Float64Max, Float64T, Float64T, Float64T)                           \
  V(Float64Min, Float64T, Float64T, Float64T)                           \
  V(Float64InsertLowWord32, Float64T, Float64T, Word32T)                \
  V(Float64InsertHighWord32, Float64T, Float64T, Word32T)               \
  V(I8x16Eq, I8x16T, I8x16T, I8x16T)                                    \
  V(IntPtrAdd, WordT, WordT, WordT)                                     \
  V(IntPtrSub, WordT, WordT, WordT)                                     \
  V(IntPtrMul, WordT, WordT, WordT)                                     \
  V(IntPtrMulHigh, IntPtrT, IntPtrT, IntPtrT)                           \
  V(UintPtrMulHigh, UintPtrT, UintPtrT, UintPtrT)                       \
  V(IntPtrDiv, IntPtrT, IntPtrT, IntPtrT)                               \
  V(IntPtrMod, IntPtrT, IntPtrT, IntPtrT)                               \
  V(IntPtrAddWithOverflow, PAIR_TYPE(IntPtrT, BoolT), IntPtrT, IntPtrT) \
  V(IntPtrSubWithOverflow, PAIR_TYPE(IntPtrT, BoolT), IntPtrT, IntPtrT) \
  V(IntPtrMulWithOverflow, PAIR_TYPE(IntPtrT, BoolT), IntPtrT, IntPtrT) \
  V(Int32Add, Word32T, Word32T, Word32T)                                \
  V(Int32AddWithOverflow, PAIR_TYPE(Int32T, BoolT), Int32T, Int32T)     \
  V(Int32Sub, Word32T, Word32T, Word32T)                                \
  V(Int32SubWithOverflow, PAIR_TYPE(Int32T, BoolT), Int32T, Int32T)     \
  V(Int32Mul, Word32T, Word32T, Word32T)                                \
  V(Int32MulWithOverflow, PAIR_TYPE(Int32T, BoolT), Int32T, Int32T)     \
  V(Int32Div, Int32T, Int32T, Int32T)                                   \
  V(Uint32Div, Uint32T, Uint32T, Uint32T)                               \
  V(Int32Mod, Int32T, Int32T, Int32T)                                   \
  V(Uint32Mod, Uint32T, Uint32T, Uint32T)                               \
  V(Int64Add, Word64T, Word64T, Word64T)                                \
  V(Int64Sub, Word64T, Word64T, Word64T)                                \
  V(Int64SubWithOverflow, PAIR_TYPE(Int64T, BoolT), Int64T, Int64T)     \
  V(Int64Mul, Word64T, Word64T, Word64T)                                \
  V(Int64MulHigh, Int64T, Int64T, Int64T)                               \
  V(Uint64MulHigh, Uint64T, Uint64T, Uint64T)                           \
  V(Int64Div, Int64T, Int64T, Int64T)                                   \
  V(Int64Mod, Int64T, Int64T, Int64T)                                   \
  V(WordOr, WordT, WordT, WordT)                                        \
  V(WordAnd, WordT, WordT, WordT)                                       \
  V(WordXor, WordT, WordT, WordT)                                       \
  V(WordRor, WordT, WordT, IntegralT)                                   \
  V(WordShl, WordT, WordT, IntegralT)                                   \
  V(WordShr, WordT, WordT, IntegralT)                                   \
  V(WordSar, WordT, WordT, IntegralT)                                   \
  V(WordSarShiftOutZeros, WordT, WordT, IntegralT)                      \
  V(Word32Or, Word32T, Word32T, Word32T)                                \
  V(Word32And, Word32T, Word32T, Word32T)                               \
  V(Word32Xor, Word32T, Word32T, Word32T)                               \
  V(Word32Ror, Word32T, Word32T, Word32T)                               \
  V(Word32Shl, Word32T, Word32T, Word32T)                               \
  V(Word32Shr, Word32T, Word32T, Word32T)                               \
  V(Word32Sar, Word32T, Word32T, Word32T)                               \
  V(Word32SarShiftOutZeros, Word32T, Word32T, Word32T)                  \
  V(Word64And, Word64T, Word64T, Word64T)                               \
  V(Word64Or, Word64T, Word64T, Word64T)                                \
  V(Word64Xor, Word64T, Word64T, Word64T)                               \
  V(Word64Shl, Word64T, Word64T, Word64T)                               \
  V(Word64Shr, Word64T, Word64T, Word64T)                               \
  V(Word64Sar, Word64T, Word64T, Word64T)

TNode<Float64T> Float64Add(TNode<Float64T> a, TNode<Float64T> b);

#define CODE_ASSEMBLER_UNARY_OP_LIST(V)                        \
  V(Float32Abs, Float32T, Float32T)                            \
  V(Float64Abs, Float64T, Float64T)                            \
  V(Float64Acos, Float64T, Float64T)                           \
  V(Float64Acosh, Float64T, Float64T)                          \
  V(Float64Asin, Float64T, Float64T)                           \
  V(Float64Asinh, Float64T, Float64T)                          \
  V(Float64Atan, Float64T, Float64T)                           \
  V(Float64Atanh, Float64T, Float64T)                          \
  V(Float64Cos, Float64T, Float64T)                            \
  V(Float64Cosh, Float64T, Float64T)                           \
  V(Float64Exp, Float64T, Float64T)                            \
  V(Float64Expm1, Float64T, Float64T)                          \
  V(Float64Log, Float64T, Float64T)                            \
  V(Float64Log1p, Float64T, Float64T)                          \
  V(Float64Log2, Float64T, Float64T)                           \
  V(Float64Log10, Float64T, Float64T)                          \
  V(Float64Cbrt, Float64T, Float64T)                           \
  V(Float64Neg, Float64T, Float64T)                            \
  V(Float64Sin, Float64T, Float64T)                            \
  V(Float64Sinh, Float64T, Float64T)                           \
  V(Float64Sqrt, Float64T, Float64T)                           \
  V(Float64Tan, Float64T, Float64T)                            \
  V(Float64Tanh, Float64T, Float64T)                           \
  V(Float64ExtractLowWord32, Uint32T, Float64T)                \
  V(Float64ExtractHighWord32, Uint32T, Float64T)               \
  V(BitcastTaggedToWord, IntPtrT, Object)                      \
  V(BitcastTaggedToWordForTagAndSmiBits, IntPtrT, AnyTaggedT)  \
  V(BitcastMaybeObjectToWord, IntPtrT, MaybeObject)            \
  V(BitcastWordToTagged, Object, WordT)                        \
  V(BitcastWordToTaggedSigned, Smi, WordT)                     \
  V(TruncateFloat64ToFloat32, Float32T, Float64T)              \
  V(TruncateFloat64ToWord32, Uint32T, Float64T)                \
  V(TruncateInt64ToInt32, Int32T, Int64T)                      \
  V(ChangeFloat32ToFloat64, Float64T, Float32T)                \
  V(ChangeFloat64ToUint32, Uint32T, Float64T)                  \
  V(ChangeFloat64ToUint64, Uint64T, Float64T)                  \
  V(ChangeInt32ToFloat64, Float64T, Int32T)                    \
  V(ChangeInt32ToInt64, Int64T, Int32T)                        \
  V(ChangeUint32ToFloat64, Float64T, Word32T)                  \
  V(ChangeUint32ToUint64, Uint64T, Word32T)                    \
  V(BitcastInt32ToFloat32, Float32T, Word32T)                  \
  V(BitcastFloat32ToInt32, Uint32T, Float32T)                  \
  V(BitcastFloat64ToInt64, Int64T, Float64T)                   \
  V(BitcastInt64ToFloat64, Float64T, Int64T)                   \
  V(RoundFloat64ToInt32, Int32T, Float64T)                     \
  V(RoundInt32ToFloat32, Float32T, Int32T)                     \
  V(Float64SilenceNaN, Float64T, Float64T)                     \
  V(Float64RoundDown, Float64T, Float64T)                      \
  V(Float64RoundUp, Float64T, Float64T)                        \
  V(Float64RoundTiesEven, Float64T, Float64T)                  \
  V(Float64RoundTruncate, Float64T, Float64T)                  \
  V(Word32Clz, Int32T, Word32T)                                \
  V(Word64Clz, Int64T, Word64T)                                \
  V(Word32Ctz, Int32T, Word32T)                                \
  V(Word64Ctz, Int64T, Word64T)                                \
  V(Word32Popcnt, Int32T, Word32T)                             \
  V(Word64Popcnt, Int64T, Word64T)                             \
  V(Word32BitwiseNot, Word32T, Word32T)                        \
  V(WordNot, WordT, WordT)                                     \
  V(Word64Not, Word64T, Word64T)                               \
  V(I8x16BitMask, Int32T, I8x16T)                              \
  V(I8x16Splat, I8x16T, Int32T)                                \
  V(Int32AbsWithOverflow, PAIR_TYPE(Int32T, BoolT), Int32T)    \
  V(Int64AbsWithOverflow, PAIR_TYPE(Int64T, BoolT), Int64T)    \
  V(IntPtrAbsWithOverflow, PAIR_TYPE(IntPtrT, BoolT), IntPtrT) \
  V(Word32BinaryNot, BoolT, Word32T)                           \
  V(StackPointerGreaterThan, BoolT, WordT)

// A "public" interface used by components outside of compiler directory to
// create code objects with TurboFan's backend. This class is mostly a thin
// shim around the RawMachineAssembler, and its primary job is to ensure that
// the innards of the RawMachineAssembler and other compiler implementation
// details don't leak outside of the the compiler directory..
//
// V8 components that need to generate low-level code using this interface
// should include this header--and this header only--from the compiler
// directory (this is actually enforced). Since all interesting data
// structures are forward declared, it's not possible for clients to peek
// inside the compiler internals.
//
// In addition to providing isolation between TurboFan and code generation
// clients, CodeAssembler also provides an abstraction for creating variables
// and enhanced Label functionality to merge variable values along paths where
// they have differing values, including loops.
//
// The CodeAssembler itself is stateless (and instances are expected to be
// temporary-scoped and short-lived); all its state is encapsulated into
// a CodeAssemblerState instance.
class V8_EXPORT_PRIVATE CodeAssembler {
 public:
  explicit CodeAssembler(CodeAssemblerState* state) : state_(state) {}
  ~CodeAssembler();

  CodeAssembler(const CodeAssembler&) = delete;
  CodeAssembler& operator=(const CodeAssembler&) = delete;

  static Handle<Code> GenerateCode(CodeAssemblerState* state,
                                   const AssemblerOptions& options,
                                   const ProfileDataFromFile* profile_data);
  bool Is64() const;
  bool Is32() const;
  bool IsFloat64RoundUpSupported() const;
  bool IsFloat64RoundDownSupported() const;
  bool IsFloat64RoundTiesEvenSupported() const;
  bool IsFloat64RoundTruncateSupported() const;
  bool IsInt32AbsWithOverflowSupported() const;
  bool IsInt64AbsWithOverflowSupported() const;
  bool IsIntPtrAbsWithOverflowSupported() const;
  bool IsWord32PopcntSupported() const;
  bool IsWord64PopcntSupported() const;
  bool IsWord32CtzSupported() const;
  bool IsWord64CtzSupported() const;

  // Shortened aliases for use in CodeAssembler subclasses.
  using Label = CodeAssemblerLabel;
  template <class T>
  using TVariable = TypedCodeAssemblerVariable<T>;
  using VariableList = CodeAssemblerVariableList;

  // ===========================================================================
  // Base Assembler
  // ===========================================================================

  template <class PreviousType, bool FromTyped>
  class CheckedNode {
   public:
#ifdef DEBUG
    CheckedNode(Node* node, CodeAssembler* code_assembler, const char* location)
        : node_(node), code_assembler_(code_assembler), location_(location) {}
#else
    CheckedNode(compiler::Node* node, CodeAssembler*, const char*)
        : node_(node) {}
#endif

    template <class A>
    operator TNode<A>() {
      static_assert(!std::is_same<A, Tagged<MaybeObject>>::value,
                    "Can't cast to Tagged<MaybeObject>, use explicit "
                    "conversion functions. ");

      static_assert(types_have_common_values<A, PreviousType>::value,
                    "Incompatible types: this cast can never succeed.");
      static_assert(std::is_convertible<TNode<A>, TNode<MaybeObject>>::value ||
                        std::is_convertible<TNode<A>, TNode<Object>>::value,
                    "Coercion to untagged values cannot be "
                    "checked.");
      static_assert(
          !FromTyped ||
              !std::is_convertible<TNode<PreviousType>, TNode<A>>::value,
          "Unnecessary CAST: types are convertible.");
#ifdef DEBUG
      if (v8_flags.debug_code) {
        TNode<ExternalReference> function = code_assembler_->ExternalConstant(
            ExternalReference::check_object_type());
        code_assembler_->CallCFunction(
            function, MachineType::AnyTagged(),
            std::make_pair(MachineType::AnyTagged(), node_),
            std::make_pair(MachineType::TaggedSigned(),
                           code_assembler_->SmiConstant(
                               static_cast<int>(ObjectTypeOf<A>::value))),
            std::make_pair(MachineType::AnyTagged(),
                           code_assembler_->StringConstant(location_)));
      }
#endif
      return TNode<A>::UncheckedCast(node_);
    }

    Node* node() const { return node_; }

   private:
    Node* node_;
#ifdef DEBUG
    CodeAssembler* code_assembler_;
    const char* location_;
#endif
  };

  template <class T>
  TNode<T> UncheckedCast(Node* value) {
    return TNode<T>::UncheckedCast(value);
  }
  template <class T, class U>
  TNode<T> UncheckedCast(TNode<U> value) {
    static_assert(types_have_common_values<T, U>::value,
                  "Incompatible types: this cast can never succeed.");
    return TNode<T>::UncheckedCast(value);
  }

  // ReinterpretCast<T>(v) has the power to cast even when the type of v is
  // unrelated to T. Use with care.
  template <class T>
  TNode<T> ReinterpretCast(Node* value) {
    return TNode<T>::UncheckedCast(value);
  }

  CheckedNode<Object, false> Cast(Node* value, const char* location = "") {
    return {value, this, location};
  }

  template <class T>
  CheckedNode<T, true> Cast(TNode<T> value, const char* location = "") {
    return {value, this, location};
  }

#ifdef DEBUG
#define STRINGIFY(x) #x
#define TO_STRING_LITERAL(x) STRINGIFY(x)
#define CAST(x) \
  Cast(x, "CAST(" #x ") at " __FILE__ ":" TO_STRING_LITERAL(__LINE__))
#define TORQUE_CAST(x) \
  ca_.Cast(x, "CAST(" #x ") at " __FILE__ ":" TO_STRING_LITERAL(__LINE__))
#else
#define CAST(x) Cast(x)
#define TORQUE_CAST(x) ca_.Cast(x)
#endif

  // Constants.
  TNode<Int32T> UniqueInt32Constant(int32_t value);
  TNode<Int32T> Int32Constant(int32_t value);
  TNode<Int64T> UniqueInt64Constant(int64_t value);
  TNode<Int64T> Int64Constant(int64_t value);
  TNode<Uint64T> Uint64Constant(uint64_t value) {
    return Unsigned(Int64Constant(base::bit_cast<int64_t>(value)));
  }
  TNode<IntPtrT> IntPtrConstant(intptr_t value);
  TNode<IntPtrT> UniqueIntPtrConstant(intptr_t value);
  TNode<Uint32T> UniqueUint32Constant(int32_t value) {
    return Unsigned(UniqueInt32Constant(base::bit_cast<int32_t>(value)));
  }
  TNode<Uint32T> Uint32Constant(uint32_t value) {
    return Unsigned(Int32Constant(base::bit_cast<int32_t>(value)));
  }
  TNode<Uint32T> Uint64HighWordConstant(uint64_t value) {
    return Uint32Constant(value >> 32);
  }
  TNode<Uint32T> Uint64HighWordConstantNoLowWord(uint64_t value) {
    DCHECK_EQ(0, value & ~uint32_t{0});
    return Uint64HighWordConstant(value);
  }
  TNode<Uint32T> Uint64LowWordConstant(uint64_t value) {
    return Uint32Constant(static_cast<uint32_t>(value));
  }
  TNode<UintPtrT> UintPtrConstant(uintptr_t value) {
    return Unsigned(IntPtrConstant(base::bit_cast<intptr_t>(value)));
  }
  TNode<TaggedIndex> TaggedIndexConstant(intptr_t value);
  TNode<RawPtrT> PointerConstant(void* value) {
    return ReinterpretCast<RawPtrT>(
        IntPtrConstant(reinterpret_cast<intptr_t>(value)));
  }
  TNode<Number> NumberConstant(double value);
  TNode<Smi> SmiConstant(Tagged<Smi> value);
  TNode<Smi> SmiConstant(int value);
  template <typename E,
            typename = typename std::enable_if<std::is_enum<E>::value>::type>
  TNode<Smi> SmiConstant(E value) {
    static_assert(sizeof(E) <= sizeof(int));
    return SmiConstant(static_cast<int>(value));
  }
  TNode<HeapObject> UntypedHeapConstantNoHole(Handle<HeapObject> object);
  TNode<HeapObject> UntypedHeapConstantMaybeHole(Handle<HeapObject> object);
  TNode<HeapObject> UntypedHeapConstantHole(Handle<HeapObject> object);
  template <class Type>
  TNode<Type> HeapConstantNoHole(Handle<Type> object) {
    return UncheckedCast<Type>(UntypedHeapConstantNoHole(object));
  }
  template <class Type>
  TNode<Type> HeapConstantMaybeHole(Handle<Type> object) {
    return UncheckedCast<Type>(UntypedHeapConstantMaybeHole(object));
  }
  template <class Type>
  TNode<Type> HeapConstantHole(Handle<Type> object) {
    return UncheckedCast<Type>(UntypedHeapConstantHole(object));
  }
  TNode<String> StringConstant(const char* str);
  TNode<Boolean> BooleanConstant(bool value);
  TNode<ExternalReference> ExternalConstant(ExternalReference address);
  TNode<ExternalReference> IsolateField(IsolateFieldId id);
  TNode<Float32T> Float32Constant(double value);
  TNode<Float64T> Float64Constant(double value);
  TNode<BoolT> Int32TrueConstant() {
    return ReinterpretCast<BoolT>(Int32Constant(1));
  }
  TNode<BoolT> Int32FalseConstant() {
    return ReinterpretCast<BoolT>(Int32Constant(0));
  }
  TNode<BoolT> BoolConstant(bool value) {
    return value ? Int32TrueConstant() : Int32FalseConstant();
  }
  TNode<ExternalPointerHandleT> ExternalPointerHandleNullConstant() {
    return ReinterpretCast<ExternalPointerHandleT>(Uint32Constant(0));
  }

  bool IsMapOffsetConstant(Node* node);

  bool TryToInt32Constant(TNode<IntegralT> node, int32_t* out_value);
  bool TryToInt64Constant(TNode<IntegralT> node, int64_t* out_value);
  bool TryToIntPtrConstant(TNode<IntegralT> node, intptr_t* out_value);
  bool TryToIntPtrConstant(TNode<Smi> tnode, intptr_t* out_value);
  bool TryToSmiConstant(TNode<IntegralT> node, Tagged<Smi>* out_value);
  bool TryToSmiConstant(TNode<Smi> node, Tagged<Smi>* out_value);

  bool IsUndefinedConstant(TNode<Object> node);
  bool IsNullConstant(TNode<Object> node);

  TNode<Int32T> Signed(TNode<Word32T> x) { return UncheckedCast<Int32T>(x); }
  TNode<Int64T> Signed(TNode<Word64T> x) { return UncheckedCast<Int64T>(x); }
  TNode<IntPtrT> Signed(TNode<WordT> x) { return UncheckedCast<IntPtrT>(x); }
  TNode<Uint32T> Unsigned(TNode<Word32T> x) {
    return UncheckedCast<Uint32T>(x);
  }
  TNode<Uint64T> Unsigned(TNode<Word64T> x) {
    return UncheckedCast<Uint64T>(x);
  }
  TNode<UintPtrT> Unsigned(TNode<WordT> x) {
    return UncheckedCast<UintPtrT>(x);
  }

  static constexpr int kTargetParameterIndex = kJSCallClosureParameterIndex;
  static_assert(kTargetParameterIndex == -1);

  template <class T>
  TNode<T> Parameter(int value,
                     const SourceLocation& loc = SourceLocation::Current()) {
    static_assert(
        std::is_convertible<TNode<T>, TNode<Object>>::value,
        "Parameter is only for tagged types. Use UncheckedParameter instead.");
    std::stringstream message;
    message << "Parameter " << value;
    if (loc.FileName()) {
      message << " at " << loc.FileName() << ":" << loc.Line();
    }
    size_t buf_size = message.str().size() + 1;
    char* message_dup = zone()->AllocateArray<char>(buf_size);
    snprintf(message_dup, buf_size, "%s", message.str().c_str());

    return Cast(UntypedParameter(value), message_dup);
  }

  template <class T>
  TNode<T> UncheckedParameter(int value) {
    return UncheckedCast<T>(UntypedParameter(value));
  }

  Node* UntypedParameter(int value);

  TNode<Context> GetJSContextParameter();
  void Return(TNode<Object> value);
  void Return(TNode<Object> value1, TNode<Object> value2);
  void Return(TNode<Object> value1, TNode<Object> value2, TNode<Object> value3);
  void Return(TNode<Int32T> value);
  void Return(TNode<Uint32T> value);
  void Return(TNode<WordT> value);
  void Return(TNode<Float32T> value);
  void Return(TNode<Float64T> value);
  void Return(TNode<WordT> value1, TNode<WordT> value2);
  void Return(TNode<Word32T> value1, TNode<Word32T> value2);
  void Return(TNode<WordT> value1, TNode<Object> value2);
  void PopAndReturn(Node* pop, Node* value);
  void PopAndReturn(Node* pop, Node* value1, Node* value2, Node* value3,
                    Node* value4);

  void ReturnIf(TNode<BoolT> condition, TNode<Object> value);

  void AbortCSADcheck(Node* message);
  void DebugBreak();
  void Unreachable();

  // Hack for supporting SourceLocation alongside template packs.
  struct MessageWithSourceLocation {
    const char* message;
    const SourceLocation& loc;

    // Allow implicit construction, necessary for the hack.
    // NOLINTNEXTLINE
    MessageWithSourceLocation(
        const char* message,
        const SourceLocation& loc = SourceLocation::Current())
        : message(message), loc(loc) {}
  };
  template <class... Args>
  void Comment(MessageWithSourceLocation message, Args&&... args) {
    if (!v8_flags.code_comments) return;
    std::ostringstream s;
    USE(s << message.message, (s << std::forward<Args>(args))...);
    if (message.loc.FileName()) {
      s << " - " << message.loc.ToString();
    }
    EmitComment(std::move(s).str());
  }

  void StaticAssert(TNode<BoolT> value,
                    const char* source = "unknown position");

  // The following methods refer to source positions in CSA or Torque code
  // compiled during mksnapshot, not JS compiled at runtime.
  void SetSourcePosition(const char* file, int line);
  void PushSourcePosition();
  void PopSourcePosition();
  class V8_NODISCARD SourcePositionScope {
   public:
    explicit SourcePositionScope(CodeAssembler* ca) : ca_(ca) {
      ca->PushSourcePosition();
    }
    ~SourcePositionScope() { ca_->PopSourcePosition(); }

   private:
    CodeAssembler* ca_;
  };
  const std::vector<FileAndLine>& GetMacroSourcePositionStack() const;

  void Bind(Label* label);
#if DEBUG
  void Bind(Label* label, AssemblerDebugInfo debug_info);
#endif  // DEBUG
  void Goto(Label* label);
  void GotoIf(TNode<IntegralT> condition, Label* true_label);
  void GotoIfNot(TNode<IntegralT> condition, Label* false_label);
  void Branch(TNode<IntegralT> condition, Label* true_label,
              Label* false_label);

  template <class T>
  TNode<T> Uninitialized() {
    return {};
  }

  template <class... T>
  void Bind(CodeAssemblerParameterizedLabel<T...>* label, TNode<T>*... phis) {
    Bind(label->plain_label());
    label->CreatePhis(phis...);
  }
  template <class... T, class... Args>
  void Branch(TNode<BoolT> condition,
              CodeAssemblerParameterizedLabel<T...>* if_true,
              CodeAssemblerParameterizedLabel<T...>* if_false, Args... args) {
    if_true->AddInputs(args...);
    if_false->AddInputs(args...);
    Branch(condition, if_true->plain_label(), if_false->plain_label());
  }
  template <class... T, class... U>
  void Branch(TNode<BoolT> condition,
              CodeAssemblerParameterizedLabel<T...>* if_true,
              std::vector<Node*> args_true,
              CodeAssemblerParameterizedLabel<U...>* if_false,
              std::vector<Node*> args_false) {
    if_true->AddInputsVector(std::move(args_true));
    if_false->AddInputsVector(std::move(args_false));
    Branch(condition, if_true->plain_label(), if_false->plain_label());
  }

  template <class... T, class... Args>
  void Goto(CodeAssemblerParameterizedLabel<T...>* label, Args... args) {
    label->AddInputs(args...);
    Goto(label->plain_label());
  }

  void Branch(TNode<BoolT> condition, const std::function<void()>& true_body,
              const std::function<void()>& false_body);
  void Branch(TNode<BoolT> condition, Label* true_label,
              const std::function<void()>& false_body);
  void Branch(TNode<BoolT> condition, const std::function<void()>& true_body,
              Label* false_label);

  void Switch(Node* index, Label* default_label, const int32_t* case_values,
              Label** case_labels, size_t case_count);

  // Access to the frame pointer.
  TNode<RawPtrT> LoadFramePointer();
  TNode<RawPtrT> LoadParentFramePointer();
  TNode<RawPtrT> StackSlotPtr(int size, int alignment);

#if V8_ENABLE_WEBASSEMBLY
  // Access to the stack pointer.
  TNode<RawPtrT> LoadStackPointer();
  void SetStackPointer(TNode<RawPtrT> ptr);
#endif  // V8_ENABLE_WEBASSEMBLY

  TNode<RawPtrT> LoadPointerFromRootRegister(TNode<IntPtrT> offset);
  TNode<Uint8T> LoadUint8FromRootRegister(TNode<IntPtrT> offset);

  // Load raw memory location.
  Node* Load(MachineType type, Node* base);
  template <class Type>
  TNode<Type> Load(MachineType type, TNode<RawPtr<Type>> base) {
    DCHECK(
        IsSubtype(type.representation(), MachineRepresentationOf<Type>::value));
    return UncheckedCast<Type>(Load(type, static_cast<Node*>(base)));
  }
  Node* Load(MachineType type, Node* base, Node* offset);
  template <class Type>
  TNode<Type> Load(Node* base) {
    return UncheckedCast<Type>(Load(MachineTypeOf<Type>::value, base));
  }
  template <class Type>
  TNode<Type> Load(Node* base, TNode<WordT> offset) {
    return UncheckedCast<Type>(Load(MachineTypeOf<Type>::value, base, offset));
  }
  template <class Type>
  TNode<Type> AtomicLoad(AtomicMemoryOrder order, TNode<RawPtrT> base,
                         TNode<WordT> offset) {
    return UncheckedCast<Type>(
        AtomicLoad(MachineTypeOf<Type>::value, order, base, offset));
  }
  template <class Type>
  TNode<Type> AtomicLoad64(AtomicMemoryOrder order, TNode<RawPtrT> base,
                           TNode<WordT> offset);
  // Load uncompressed tagged value from (most likely off JS heap) memory
  // location.
  TNode<Object> LoadFullTagged(Node* base);
  TNode<Object> LoadFullTagged(Node* base, TNode<IntPtrT> offset);

  Node* LoadFromObject(MachineType type, TNode<Object> object,
                       TNode<IntPtrT> offset);
  Node* LoadProtectedPointerFromObject(TNode<Object> object,
                                       TNode<IntPtrT> offset);

#ifdef V8_MAP_PACKING
  Node* PackMapWord(Node* value);
#endif

  // Load a value from the root array.
  // If map packing is enabled, LoadRoot for a root map returns the unpacked map
  // word (i.e., the map). Use LoadRootMapWord to obtain the packed map word
  // instead.
  TNode<Object> LoadRoot(RootIndex root_index);
  TNode<AnyTaggedT> LoadRootMapWord(RootIndex root_index);

  template <typename Type>
  TNode<Type> UnalignedLoad(TNode<RawPtrT> base, TNode<IntPtrT> offset) {
    MachineType mt = MachineTypeOf<Type>::value;
    return UncheckedCast<Type>(UnalignedLoad(mt, base, offset));
  }

  // Store value to raw memory location.
  void Store(Node* base, Node* value);
  void Store(Node* base, Node* offset, Node* value);
  void StoreEphemeronKey(Node* base, Node* offset, Node* value);
  void StoreNoWriteBarrier(MachineRepresentation rep, Node* base, Node* value);
  void StoreNoWriteBarrier(MachineRepresentation rep, Node* base, Node* offset,
                           Node* value);
  void UnsafeStoreNoWriteBarrier(MachineRepresentation rep, Node* base,
                                 Node* value);
  void UnsafeStoreNoWriteBarrier(MachineRepresentation rep, Node* base,
                                 Node* offset, Node* value);

  // Stores uncompressed tagged value to (most likely off JS heap) memory
  // location without write barrier.
  void StoreFullTaggedNoWriteBarrier(TNode<RawPtrT> base,
                                     TNode<Object> tagged_value);
  void StoreFullTaggedNoWriteBarrier(TNode<RawPtrT> base, TNode<IntPtrT> offset,
                                     TNode<Object> tagged_value);

  // Optimized memory operations that map to Turbofan simplified nodes.
  TNode<HeapObject> OptimizedAllocate(TNode<IntPtrT> size,
                                      AllocationType allocation);
  void StoreToObject(MachineRepresentation rep, TNode<Object> object,
                     TNode<IntPtrT> offset, Node* value,
                     StoreToObjectWriteBarrier write_barrier);
  void OptimizedStoreField(MachineRepresentation rep, TNode<HeapObject> object,
                           int offset, Node* value);
  void OptimizedStoreIndirectPointerField(TNode<HeapObject> object, int offset,
                                          IndirectPointerTag tag, Node* value);
  void OptimizedStoreIndirectPointerFieldNoWriteBarrier(
      TNode<HeapObject> object, int offset, IndirectPointerTag tag,
      Node* value);
  void OptimizedStoreFieldAssertNoWriteBarrier(MachineRepresentation rep,
                                               TNode<HeapObject> object,
                                               int offset, Node* value);
  void OptimizedStoreFieldUnsafeNoWriteBarrier(MachineRepresentation rep,
                                               TNode<HeapObject> object,
                                               int offset, Node* value);
  void OptimizedStoreMap(TNode<HeapObject> object, TNode<Map>);
  void AtomicStore(MachineRepresentation rep, AtomicMemoryOrder order,
                   TNode<RawPtrT> base, TNode<WordT> offset,
                   TNode<Word32T> value);
  // {value_high} is used for 64-bit stores on 32-bit platforms, must be
  // nullptr in other cases.
  void AtomicStore64(AtomicMemoryOrder order, TNode<RawPtrT> base,
                     TNode<WordT> offset, TNode<UintPtrT> value,
                     TNode<UintPtrT> value_high);

  TNode<Word32T> AtomicAdd(MachineType type, TNode<RawPtrT> base,
                           TNode<UintPtrT> offset, TNode<Word32T> value);
  template <class Type>
  TNode<Type> AtomicAdd64(TNode<RawPtrT> base, TNode<UintPtrT> offset,
                          TNode<UintPtrT> value, TNode<UintPtrT> value_high);

  TNode<Word32T> AtomicSub(MachineType type, TNode<RawPtrT> base,
                           TNode<UintPtrT> offset, TNode<Word32T> value);
  template <class Type>
  TNode<Type> AtomicSub64(TNode<RawPtrT> base, TNode<UintPtrT> offset,
                          TNode<UintPtrT> value, TNode<UintPtrT> value_high);

  TNode<Word32T> AtomicAnd(MachineType type, TNode<RawPtrT> base,
                           TNode<UintPtrT> offset, TNode<Word32T> value);
  template <class Type>
  TNode<Type> AtomicAnd64(TNode<RawPtrT> base, TNode<UintPtrT> offset,
                          TNode<UintPtrT> value, TNode<UintPtrT> value_high);

  TNode<Word32T> AtomicOr(MachineType type, TNode<RawPtrT> base,
                          TNode<UintPtrT> offset, TNode<Word32T> value);
  template <class Type>
  TNode<Type> AtomicOr64(TNode<RawPtrT> base, TNode<UintPtrT> offset,
                         TNode<UintPtrT> value, TNode<UintPtrT> value_high);

  TNode<Word32T> AtomicXor(MachineType type, TNode<RawPtrT> base,
                           TNode<UintPtrT> offset, TNode<Word32T> value);
  template <class Type>
  TNode<Type> AtomicXor64(TNode<RawPtrT> base, TNode<UintPtrT> offset,
                          TNode<UintPtrT> value, TNode<UintPtrT> value_high);

  // Exchange value at raw memory location
  TNode<Word32T> AtomicExchange(MachineType type, TNode<RawPtrT> base,
                                TNode<UintPtrT> offset, TNode<Word32T> value);
  template <class Type>
  TNode<Type> AtomicExchange64(TNode<RawPtrT> base, TNode<UintPtrT> offset,
                               TNode<UintPtrT> value,
                               TNode<UintPtrT> value_high);

  // Compare and Exchange value at raw memory location
  TNode<Word32T> AtomicCompareExchange(MachineType type, TNode<RawPtrT> base,
                                       TNode<WordT> offset,
                                       TNode<Word32T> old_value,
                                       TNode<Word32T> new_value);

  template <class Type>
  TNode<Type> AtomicCompareExchange64(TNode<RawPtrT> base, TNode<WordT> offset,
                                      TNode<UintPtrT> old_value,
                                      TNode<UintPtrT> new_value,
                                      TNode<UintPtrT> old_value_high,
                                      TNode<UintPtrT> new_value_high);

  void MemoryBarrier(AtomicMemoryOrder order);

  // Store a value to the root array.
  void StoreRoot(RootIndex root_index, TNode<Object> value);

// Basic arithmetic operations.
#define DECLARE_CODE_ASSEMBLER_BINARY_OP(name, ResType, Arg1Type, Arg2Type) \
  TNode<ResType> name(TNode<Arg1Type> a, TNode<Arg2Type> b);
  CODE_ASSEMBLER_BINARY_OP_LIST(DECLARE_CODE_ASSEMBLER_BINARY_OP)
#undef DECLARE_CODE_ASSEMBLER_BINARY_OP

  // Pairwise operations for 32bit.
  TNode<PairT<Word32T, Word32T>> Int32PairAdd(TNode<Word32T> lhs_lo_word,
                                              TNode<Word32T> lhs_hi_word,
                                              TNode<Word32T> rhs_lo_word,
                                              TNode<Word32T> rhs_hi_word);
  TNode<PairT<Word32T, Word32T>> Int32PairSub(TNode<Word32T> lhs_lo_word,
                                              TNode<Word32T> lhs_hi_word,
                                              TNode<Word32T> rhs_lo_word,
                                              TNode<Word32T> rhs_hi_word);

  TNode<UintPtrT> WordShr(TNode<UintPtrT> left, TNode<IntegralT> right) {
    return Unsigned(WordShr(static_cast<TNode<WordT>>(left), right));
  }
  TNode<IntPtrT> WordSar(TNode<IntPtrT> left, TNode<IntegralT> right) {
    return Signed(WordSar(static_cast<TNode<WordT>>(left), right));
  }
  TNode<IntPtrT> WordShl(TNode<IntPtrT> left, TNode<IntegralT> right) {
    return Signed(WordShl(static_cast<TNode<WordT>>(left), right));
  }
  TNode<UintPtrT> WordShl(TNode<UintPtrT> left, TNode<IntegralT> right) {
    return Unsigned(WordShl(static_cast<TNode<WordT>>(left), right));
  }

  TNode<Int32T> Word32Shl(TNode<Int32T> left, TNode<Int32T> right) {
    return Signed(Word32Shl(static_cast<TNode<Word32T>>(left), right));
  }
  TNode<Uint32T> Word32Shl(TNode<Uint32T> left, TNode<Uint32T> right) {
    return Unsigned(Word32Shl(static_cast<TNode<Word32T>>(left), right));
  }
  TNode<Uint32T> Word32Shr(TNode<Uint32T> left, TNode<Uint32T> right) {
    return Unsigned(Word32Shr(static_cast<TNode<Word32T>>(left), right));
  }
  TNode<Int32T> Word32Sar(TNode<Int32T> left, TNode<Int32T> right) {
    return Signed(Word32Sar(static_cast<TNode<Word32T>>(left), right));
  }

  TNode<Int64T> Word64Shl(TNode<Int64T> left, TNode<Int64T> right) {
    return Signed(Word64Shl(static_cast<TNode<Word64T>>(left), right));
  }
  TNode<Uint64T> Word64Shl(TNode<Uint64T> left, TNode<Uint64T> right) {
    return Unsigned(Word64Shl(static_cast<TNode<Word64T>>(left), right));
  }
  TNode<Uint64T> Word64Shr(TNode<Uint64T> left, TNode<Uint64T> right) {
    return Unsigned(Word64Shr(static_cast<TNode<Word64T>>(left), right));
  }
  TNode<Int64T> Word64Sar(TNode<Int64T> left, TNode<Int64T> right) {
    return Signed(Word64Sar(static_cast<TNode<Word64T>>(left), right));
  }

  TNode<Int64T> Word64And(TNode<Int64T> left, TNode<Int64T> right) {
    return Signed(Word64And(static_cast<TNode<Word64T>>(left), right));
  }
  TNode<Uint64T> Word64And(TNode<Uint64T> left, TNode<Uint64T> right) {
    return Unsigned(Word64And(static_cast<TNode<Word64T>>(left), right));
  }

  TNode<Int64T> Word64Xor(TNode<Int64T> left, TNode<Int64T> right) {
    return Signed(Word64Xor(static_cast<TNode<Word64T>>(left), right));
  }
  TNode<Uint64T> Word64Xor(TNode<Uint64T> left, TNode<Uint64T> right) {
    return Unsigned(Word64Xor(static_cast<TNode<Word64T>>(left), right));
  }

  TNode<Int64T> Word64Not(TNode<Int64T> value) {
    return Signed(Word64Not(static_cast<TNode<Word64T>>(value)));
  }
  TNode<Uint64T> Word64Not(TNode<Uint64T> value) {
    return Unsigned(Word64Not(static_cast<TNode<Word64T>>(value)));
  }

  TNode<IntPtrT> WordAnd(TNode<IntPtrT> left, TNode<IntPtrT> right) {
    return Signed(WordAnd(static_cast<TNode<WordT>>(left),
                          static_cast<TNode<WordT>>(right)));
  }
  TNode<UintPtrT> WordAnd(TNode<UintPtrT> left, TNode<UintPtrT> right) {
    return Unsigned(WordAnd(static_cast<TNode<WordT>>(left),
                            static_cast<TNode<WordT>>(right)));
  }

  TNode<Int32T> Word32And(TNode<Int32T> left, TNode<Int32T> right) {
    return Signed(Word32And(static_cast<TNode<Word32T>>(left),
                            static_cast<TNode<Word32T>>(right)));
  }
  TNode<Uint32T> Word32And(TNode<Uint32T> left, TNode<Uint32T> right) {
    return Unsigned(Word32And(static_cast<TNode<Word32T>>(left),
                              static_cast<TNode<Word32T>>(right)));
  }

  TNode<IntPtrT> WordOr(TNode<IntPtrT> left, TNode<IntPtrT> right) {
    return Signed(WordOr(static_cast<TNode<WordT>>(left),
                         static_cast<TNode<WordT>>(right)));
  }

  TNode<Int32T> Word32Or(TNode<Int32T> left, TNode<Int32T> right) {
    return Signed(Word32Or(static_cast<TNode<Word32T>>(left),
                           static_cast<TNode<Word32T>>(right)));
  }
  TNode<Uint32T> Word32Or(TNode<Uint32T> left, TNode<Uint32T> right) {
    return Unsigned(Word32Or(static_cast<TNode<Word32T>>(left),
                             static_cast<TNode<Word32T>>(right)));
  }

  TNode<BoolT> IntPtrEqual(TNode<WordT> left, TNode<WordT> right);
  TNode<BoolT> WordEqual(TNode<WordT> left, TNode<WordT> right);
  TNode<BoolT> WordNotEqual(TNode<WordT> left, TNode<WordT> right);
  TNode<BoolT> Word32Equal(TNode<Word32T> left, TNode<Word32T> right);
  TNode<BoolT> Word32NotEqual(TNode<Word32T> left, TNode<Word32T> right);
  TNode<BoolT> Word64Equal(TNode<Word64T> left, TNode<Word64T> right);
  TNode<BoolT> Word64NotEqual(TNode<Word64T> left, TNode<Word64T> right);

  TNode<IntPtrT> WordNot(TNode<IntPtrT> a) {
    return Signed(WordNot(static_cast<TNode<WordT>>(a)));
  }
  TNode<Int32T> Word32BitwiseNot(TNode<Int32T> a) {
    return Signed(Word32BitwiseNot(static_cast<TNode<Word32T>>(a)));
  }
  TNode<BoolT> Word32Or(TNode<BoolT> left, TNode<BoolT> right) {
    return UncheckedCast<BoolT>(Word32Or(static_cast<TNode<Word32T>>(left),
                                         static_cast<TNode<Word32T>>(right)));
  }
  TNode<BoolT> Word32And(TNode<BoolT> left, TNode<BoolT> right) {
    return UncheckedCast<BoolT>(Word32And(static_cast<TNode<Word32T>>(left),
                                          static_cast<TNode<Word32T>>(right)));
  }

  TNode<Int32T> Int32Add(TNode<Int32T> left, TNode<Int32T> right) {
    return Signed(Int32Add(static_cast<TNode<Word32T>>(left),
                           static_cast<TNode<Word32T>>(right)));
  }

  TNode<Uint32T> Uint32Add(TNode<Uint32T> left, TNode<Uint32T> right) {
    return Unsigned(Int32Add(static_cast<TNode<Word32T>>(left),
                             static_cast<TNode<Word32T>>(right)));
  }

  TNode<Uint32T> Uint32Sub(TNode<Uint32T> left, TNode<Uint32T> right) {
    return Unsigned(Int32Sub(static_cast<TNode<Word32T>>(left),
                             static_cast<TNode<Word32T>>(right)));
  }

  TNode<Int32T> Int32Sub(TNode<Int32T> left, TNode<Int32T> right) {
    return Signed(Int32Sub(static_cast<TNode<Word32T>>(left),
                           static_cast<TNode<Word32T>>(right)));
  }

  TNode<Int32T> Int32Mul(TNode<Int32T> left, TNode<Int32T> right) {
    return Signed(Int32Mul(static_cast<TNode<Word32T>>(left),
                           static_cast<TNode<Word32T>>(right)));
  }

  TNode<Uint32T> Uint32Mul(TNode<Uint32T> left, TNode<Uint32T> right) {
    return Unsigned(Int32Mul(static_cast<TNode<Word32T>>(left),
                             static_cast<TNode<Word32T>>(right)));
  }

  TNode<Int64T> Int64Add(TNode<Int64T> left, TNode<Int64T> right) {
    return Signed(Int64Add(static_cast<TNode<Word64T>>(left), right));
  }

  TNode<Uint64T> Uint64Add(TNode<Uint64T> left, TNode<Uint64T> right) {
    return Unsigned(Int64Add(static_cast<TNode<Word64T>>(left), right));
  }

  TNode<Int64T> Int64Sub(TNode<Int64T> left, TNode<Int64T> right) {
    return Signed(Int64Sub(static_cast<TNode<Word64T>>(left), right));
  }

  TNode<Uint64T> Uint64Sub(TNode<Uint64T> left, TNode<Uint64T> right) {
    return Unsigned(Int64Sub(static_cast<TNode<Word64T>>(left), right));
  }

  TNode<Int64T> Int64Mul(TNode<Int64T> left, TNode<Int64T> right) {
    return Signed(Int64Mul(static_cast<TNode<Word64T>>(left), right));
  }

  TNode<Uint64T> Uint64Mul(TNode<Uint64T> left, TNode<Uint64T> right) {
    return Unsigned(Int64Mul(static_cast<TNode<Word64T>>(left), right));
  }

  TNode<IntPtrT> IntPtrAdd(TNode<IntPtrT> left, TNode<IntPtrT> right) {
    return Signed(IntPtrAdd(static_cast<TNode<WordT>>(left),
                            static_cast<TNode<WordT>>(right)));
  }
  TNode<IntPtrT> IntPtrSub(TNode<IntPtrT> left, TNode<IntPtrT> right) {
    return Signed(IntPtrSub(static_cast<TNode<WordT>>(left),
                            static_cast<TNode<WordT>>(right)));
  }
  TNode<IntPtrT> IntPtrMul(TNode<IntPtrT> left, TNode<IntPtrT> right) {
    return Signed(IntPtrMul(static_cast<TNode<WordT>>(left),
                            static_cast<TNode<WordT>>(right)));
  }
  TNode<UintPtrT> UintPtrAdd(TNode<UintPtrT> left, TNode<UintPtrT> right) {
    return Unsigned(IntPtrAdd(static_cast<TNode<WordT>>(left),
                              static_cast<TNode<WordT>>(right)));
  }
  TNode<UintPtrT> UintPtrSub(TNode<UintPtrT> left, TNode<UintPtrT> right) {
    return Unsigned(IntPtrSub(static_cast<TNode<WordT>>(left),
                              static_cast<TNode<WordT>>(right)));
  }
  TNode<RawPtrT> RawPtrAdd(TNode<RawPtrT> left, TNode<IntPtrT> right) {
    return ReinterpretCast<RawPtrT>(IntPtrAdd(left, right));
  }
  TNode<RawPtrT> RawPtrSub(TNode<RawPtrT> left, TNode<IntPtrT> right) {
    return ReinterpretCast<RawPtrT>(IntPtrSub(left, right));
  }
  TNode<IntPtrT> RawPtrSub(TNode<RawPtrT> left, TNode<RawPtrT> right) {
    return Signed(IntPtrSub(static_cast<TNode<WordT>>(left),
                            static_cast<TNode<WordT>>(right)));
  }

  TNode<WordT> WordShl(TNode<WordT> value, int shift);
  TNode<WordT> WordShr(TNode<WordT> value, int shift);
  TNode<WordT> WordSar(TNode<WordT> value, int shift);
  TNode<IntPtrT> WordShr(TNode<IntPtrT> value, int shift) {
    return UncheckedCast<IntPtrT>(WordShr(TNode<WordT>(value), shift));
  }
  TNode<IntPtrT> WordSar(TNode<IntPtrT> value, int shift) {
    return UncheckedCast<IntPtrT>(WordSar(TNode<WordT>(value), shift));
  }
  TNode<Word32T> Word32Shr(TNode<Word32T> value, int shift);
  TNode<Word32T> Word32Sar(TNode<Word32T> value, int shift);

  // Convenience overloads.
  TNode<Int32T> Int32Sub(TNode<Int32T> left, int right) {
    return Int32Sub(left, Int32Constant(right));
  }
  TNode<Word32T> Word32And(TNode<Word32T> left, int right) {
    return Word32And(left, Int32Constant(right));
  }
  TNode<Int32T> Word32Shl(TNode<Int32T> left, int right) {
    return Word32Shl(left, Int32Constant(right));
  }
  TNode<BoolT> Word32Equal(TNode<Word32T> left, int right) {
    return Word32Equal(left, Int32Constant(right));
  }

// Unary
#define DECLARE_CODE_ASSEMBLER_UNARY_OP(name, ResType, ArgType) \
  TNode<ResType> name(TNode<ArgType> a);
  CODE_ASSEMBLER_UNARY_OP_LIST(DECLARE_CODE_ASSEMBLER_UNARY_OP)
#undef DECLARE_CODE_ASSEMBLER_UNARY_OP

  template <class Dummy = void>
  TNode<IntPtrT> BitcastTaggedToWord(TNode<Smi> node) {
    static_assert(sizeof(Dummy) < 0,
                  "Should use BitcastTaggedToWordForTagAndSmiBits instead.");
  }

  // Changes a double to an inptr_t for pointer arithmetic outside of Smi range.
  // Assumes that the double can be exactly represented as an int.
  TNode<IntPtrT> ChangeFloat64ToIntPtr(TNode<Float64T> value);
  TNode<UintPtrT> ChangeFloat64ToUintPtr(TNode<Float64T> value);
  // Same in the opposite direction.
  TNode<Float64T> ChangeUintPtrToFloat64(TNode<UintPtrT> value);

  // Changes an intptr_t to a double, e.g. for storing an element index
  // outside Smi range in a HeapNumber. Lossless on 32-bit,
  // rounds on 64-bit (which doesn't affect valid element indices).
  TNode<Float64T> RoundIntPtrToFloat64(Node* value);
  // No-op on 32-bit, otherwise zero extend.
  TNode<UintPtrT> ChangeUint32ToWord(TNode<Word32T> value);
  // No-op on 32-bit, otherwise sign extend.
  TNode<IntPtrT> ChangeInt32ToIntPtr(TNode<Word32T> value);

  // Truncates a float to a 32-bit integer. If the float is outside of 32-bit
  // range, make sure that overflow detection is easy. In particular, return
  // int_min instead of int_max on arm platforms by using parameter
  // kSetOverflowToMin.
  TNode<Int32T> TruncateFloat32ToInt32(TNode<Float32T> value);
  TNode<Int64T> TruncateFloat64ToInt64(TNode<Float64T> value);

  // Projections
  template <int index, class T1, class T2>
  TNode<typename std::tuple_element<index, std::tuple<T1, T2>>::type>
  Projection(TNode<PairT<T1, T2>> value) {
    return UncheckedCast<
        typename std::tuple_element<index, std::tuple<T1, T2>>::type>(
        Projection(index, value));
  }

  // Calls
  template <class T = Object, class... TArgs>
  TNode<T> CallRuntime(Runtime::FunctionId function, TNode<Object> context,
                       TArgs... args) {
    return UncheckedCast<T>(CallRuntimeImpl(
        function, context, {implicit_cast<TNode<Object>>(args)...}));
  }

  template <class... TArgs>
  void TailCallRuntime(Runtime::FunctionId function, TNode<Object> context,
                       TArgs... args) {
    int argc = static_cast<int>(sizeof...(args));
    TNode<Int32T> arity = Int32Constant(argc);
    return TailCallRuntimeImpl(function, arity, context,
                               {implicit_cast<TNode<Object>>(args)...});
  }

  template <class... TArgs>
  void TailCallRuntime(Runtime::FunctionId function, TNode<Int32T> arity,
                       TNode<Object> context, TArgs... args) {
    return TailCallRuntimeImpl(function, arity, context,
                               {implicit_cast<TNode<Object>>(args)...});
  }

  //
  // If context passed to CallBuiltin is nullptr, it won't be passed to the
  // builtin.
  //

  template <typename T = Object, class... TArgs>
  TNode<T> CallBuiltin(Builtin id, TNode<Object> context, TArgs... args) {
    Callable callable = Builtins::CallableFor(isolate(), id);
    TNode<Code> target = HeapConstantNoHole(callable.code());
    return CallStub<T>(callable.descriptor(), target, context, args...);
  }

  template <class... TArgs>
  void CallBuiltinVoid(Builtin id, TNode<Object> context, TArgs... args) {
    Callable callable = Builtins::CallableFor(isolate(), id);
    TNode<Code> target = HeapConstantNoHole(callable.code());
    CallStubR(StubCallMode::kCallCodeObject, callable.descriptor(), target,
              context, args...);
  }

  template <class... TArgs>
  void TailCallBuiltin(Builtin id, TNode<Object> context, TArgs... args) {
    Callable callable = Builtins::CallableFor(isolate(), id);
    TNode<Code> target = HeapConstantNoHole(callable.code());
    TailCallStub(callable.descriptor(), target, context, args...);
  }

  //
  // If context passed to CallStub is nullptr, it won't be passed to the stub.
  //

  template <class T = Object, class... TArgs>
  TNode<T> CallStub(const CallInterfaceDescriptor& descriptor,
                    TNode<Code> target, TNode<Object> context, TArgs... args) {
    return UncheckedCast<T>(CallStubR(StubCallMode::kCallCodeObject, descriptor,
                                      target, context, args...));
  }

  template <class T = Object, class... TArgs>
  TNode<T> CallBuiltinPointer(const CallInterfaceDescriptor& descriptor,
                              TNode<BuiltinPtr> target, TNode<Object> context,
                              TArgs... args) {
    return UncheckedCast<T>(CallStubR(StubCallMode::kCallBuiltinPointer,
                                      descriptor, target, context, args...));
  }

  template <class... TArgs>
  void TailCallStub(const CallInterfaceDescriptor& descriptor,
                    TNode<Code> target, TNode<Object> context, TArgs... args) {
    TailCallStubImpl(descriptor, target, context, {args...});
  }

  template <class... TArgs>
  void TailCallBytecodeDispatch(const CallInterfaceDescriptor& descriptor,
                                TNode<RawPtrT> target, TArgs... args);

  template <class... TArgs>
  void TailCallBuiltinThenBytecodeDispatch(Builtin builtin, Node* context,
                                           TArgs... args) {
    Callable callable = Builtins::CallableFor(isolate(), builtin);
    TNode<Code> target = HeapConstantNoHole(callable.code());
    TailCallStubThenBytecodeDispatchImpl(callable.descriptor(), target, context,
                                         {args...});
  }

  // Tailcalls to the given code object with JSCall linkage. The JS arguments
  // (including receiver) are supposed to be already on the stack.
  // This is a building block for implementing trampoline stubs that are
  // installed instead of code objects with JSCall linkage.
  // Note that no arguments adaption is going on here - all the JavaScript
  // arguments are left on the stack unmodified. Therefore, this tail call can
  // only be used after arguments adaptation has been performed already.
  void TailCallJSCode(TNode<Code> code, TNode<Context> context,
                      TNode<JSFunction> function, TNode<Object> new_target,
                      TNode<Int32T> arg_count);

  template <class... TArgs>
  TNode<Object> CallJS(Builtin builtin, TNode<Context> context,
                       TNode<Object> function,
                       std::optional<TNode<Object>> new_target,
                       TNode<Object> receiver, TArgs... args) {
    Callable callable = Builtins::CallableFor(isolate(), builtin);
    // CallTrampolineDescriptor doesn't have |new_target| parameter.
    DCHECK_IMPLIES(callable.descriptor() == CallTrampolineDescriptor{},
                   !new_target.has_value());
    int argc = JSParameterCount(static_cast<int>(sizeof...(args)));
    TNode<Int32T> arity = Int32Constant(argc);
    TNode<Code> target = HeapConstantNoHole(callable.code());
    return CAST(CallJSStubImpl(callable.descriptor(), target, context, function,
                               new_target, arity, {receiver, args...}));
  }

  template <class... TArgs>
  TNode<Object> ConstructJSWithTarget(Builtin builtin, TNode<Context> context,
                                      TNode<Object> function,
                                      TNode<Object> new_target, TArgs... args) {
    Callable callable = Builtins::CallableFor(isolate(), builtin);
    // Only descriptors with |new_target| parameter are allowed here.
    DCHECK_EQ(callable.descriptor(), JSTrampolineDescriptor{});
    int argc = JSParameterCount(static_cast<int>(sizeof...(args)));
    TNode<Int32T> arity = Int32Constant(argc);
    TNode<Object> receiver = LoadRoot(RootIndex::kUndefinedValue);
    TNode<Code> target = HeapConstantNoHole(callable.code());
    return CAST(CallJSStubImpl(callable.descriptor(), target, context, function,
                               new_target, arity, {receiver, args...}));
  }

  template <class... TArgs>
  TNode<Object> ConstructJS(Builtin builtin, TNode<Context> context,
                            TNode<Object> target, TArgs... args) {
    return CallOrConstructJSWithTarget(builtin, context, target, target,
                                       args...);
  }

  Node* CallCFunctionN(Signature<MachineType>* signature, int input_count,
                       Node* const* inputs);

  // Type representing C function argument with type info.
  using CFunctionArg = std::pair<MachineType, Node*>;

  // Call to a C function.
  template <class... CArgs>
  Node* CallCFunction(Node* function, std::optional<MachineType> return_type,
                      CArgs... cargs) {
    static_assert(
        std::conjunction_v<std::is_convertible<CArgs, CFunctionArg>...>,
        "invalid argument types");
    return CallCFunction(function, return_type, {cargs...});
  }

  // Call to a C function without a function discriptor on AIX.
  template <class... CArgs>
  Node* CallCFunctionWithoutFunctionDescriptor(Node* function,
                                               MachineType return_type,
                                               CArgs... cargs) {
    static_assert(
        std::conjunction_v<std::is_convertible<CArgs, CFunctionArg>...>,
        "invalid argument types");
    return CallCFunctionWithoutFunctionDescriptor(function, return_type,
                                                  {cargs...});
  }

  // Call to a C function, while saving/restoring caller registers.
  template <class... CArgs>
  Node* CallCFunctionWithCallerSavedRegisters(Node* function,
                                              MachineType return_type,
                                              SaveFPRegsMode mode,
                                              CArgs... cargs) {
    static_assert(
        std::conjunction_v<std::is_convertible<CArgs, CFunctionArg>...>,
        "invalid argument types");
    return CallCFunctionWithCallerSavedRegisters(function, return_type, mode,
                                                 {cargs...});
  }

  // Helpers which delegate to RawMachineAssembler.
  Factory* factory() const;
  Isolate* isolate() const;
  Zone* zone() const;

  CodeAssemblerState* state() { return state_; }

  void BreakOnNode(int node_id);

  bool UnalignedLoadSupported(MachineRepresentation rep) const;
  bool UnalignedStoreSupported(MachineRepresentation rep) const;

  bool IsExceptionHandlerActive() const;

 protected:
  void RegisterCallGenerationCallbacks(
      const CodeAssemblerCallback& call_prologue,
      const CodeAssemblerCallback& call_epilogue);
  void UnregisterCallGenerationCallbacks();

  bool Word32ShiftIsSafe() const;

  bool IsJSFunctionCall() const;

 private:
  void HandleException(Node* result);

  Node* CallCFunction(Node* function, std::optional<MachineType> return_type,
                      std::initializer_list<CFunctionArg> args);

  Node* CallCFunctionWithoutFunctionDescriptor(
      Node* function, MachineType return_type,
      std::initializer_list<CFunctionArg> args);

  Node* CallCFunctionWithCallerSavedRegisters(
      Node* function, MachineType return_type, SaveFPRegsMode mode,
      std::initializer_list<CFunctionArg> args);

  Node* CallRuntimeImpl(Runtime::FunctionId function, TNode<Object> context,
                        std::initializer_list<TNode<Object>> args);

  void TailCallRuntimeImpl(Runtime::FunctionId function, TNode<Int32T> arity,
                           TNode<Object> context,
                           std::initializer_list<TNode<Object>> args);

  void TailCallStubImpl(const CallInterfaceDescriptor& descriptor,
                        TNode<Code> target, TNode<Object> context,
                        std::initializer_list<Node*> args);

  void TailCallStubThenBytecodeDispatchImpl(
      const CallInterfaceDescriptor& descriptor, Node* target, Node* context,
      std::initializer_list<Node*> args);

  template <class... TArgs>
  Node* CallStubR(StubCallMode call_mode,
                  const CallInterfaceDescriptor& descriptor,
                  TNode<Object> target, TNode<Object> context, TArgs... args) {
    return CallStubRImpl(call_mode, descriptor, target, context, {args...});
  }

  Node* CallStubRImpl(StubCallMode call_mode,
                      const CallInterfaceDescriptor& descriptor,
                      TNode<Object> target, TNode<Object> context,
                      std::initializer_list<Node*> args);

  Node* CallJSStubImpl(const CallInterfaceDescriptor& descriptor,
                       TNode<Object> target, TNode<Object> context,
                       TNode<Object> function,
                       std::optional<TNode<Object>> new_target,
                       TNode<Int32T> arity, std::initializer_list<Node*> args);

  Node* CallStubN(StubCallMode call_mode,
                  const CallInterfaceDescriptor& descriptor, int input_count,
                  Node* const* inputs);

  Node* AtomicLoad(MachineType type, AtomicMemoryOrder order,
                   TNode<RawPtrT> base, TNode<WordT> offset);

  Node* UnalignedLoad(MachineType type, TNode<RawPtrT> base,
                      TNode<WordT> offset);

  void EmitComment(std::string msg);

  // These two don't have definitions and are here only for catching use cases
  // where the cast is not necessary.
  TNode<Int32T> Signed(TNode<Int32T> x);
  TNode<Uint32T> Unsigned(TNode<Uint32T> x);

  Node* Projection(int index, Node* value);

  RawMachineAssembler* raw_assembler() const;
  JSGraph* jsgraph() const;

  // Calls respective callback registered in the state.
  void CallPrologue();
  void CallEpilogue();

  CodeAssemblerState* state_;
};

// TODO(solanes, v8:6949): this class should be merged into
// TypedCodeAssemblerVariable. It's required to be separate for
// CodeAssemblerVariableLists.
class V8_EXPORT_PRIVATE CodeAssemblerVariable {
 public:
  CodeAssemblerVariable(const CodeAssemblerVariable&) = delete;
  CodeAssemblerVariable& operator=(const CodeAssemblerVariable&) = delete;

  Node* value() const;
  MachineRepresentation rep() const;
  bool IsBound() const;

 protected:
  explicit CodeAssemblerVariable(CodeAssembler* assembler,
                                 MachineRepresentation rep);
  CodeAssemblerVariable(CodeAssembler* assembler, MachineRepresentation rep,
                        Node* initial_value);
#if DEBUG
  CodeAssemblerVariable(CodeAssembler* assembler, AssemblerDebugInfo debug_info,
                        MachineRepresentation rep);
  CodeAssemblerVariable(CodeAssembler* assembler, AssemblerDebugInfo debug_info,
                        MachineRepresentation rep, Node* initial_value);
#endif  // DEBUG

  ~CodeAssemblerVariable();
  void Bind(Node* value);

 private:
  class Impl;
  friend class CodeAssemblerLabel;
  friend class CodeAssemblerState;
  friend std::ostream& operator<<(std::ostream&, const Impl&);
  friend std::ostream& operator<<(std::ostream&, const CodeAssemblerVariable&);
  struct ImplComparator {
    bool operator()(const CodeAssemblerVariable::Impl* a,
                    const CodeAssemblerVariable::Impl* b) const;
  };
  Impl* impl_;
  CodeAssemblerState* state_;
};

std::ostream& operator<<(std::ostream&, const CodeAssemblerVariable&);
std::ostream& operator<<(std::ostream&, const CodeAssemblerVariable::Impl&);

template <class T>
class TypedCodeAssemblerVariable : public CodeAssemblerVariable {
 public:
  TypedCodeAssemblerVariable(TNode<T> initial_value, CodeAssembler* assembler)
      : CodeAssemblerVariable(assembler, PhiMachineRepresentationOf<T>,
                              initial_value) {}
  explicit TypedCodeAssemblerVariable(CodeAssembler* assembler)
      : CodeAssemblerVariable(assembler, PhiMachineRepresentationOf<T>) {}
#if DEBUG
  TypedCodeAssemblerVariable(AssemblerDebugInfo debug_info,
                             CodeAssembler* assembler)
      : CodeAssemblerVariable(assembler, debug_info,
                              PhiMachineRepresentationOf<T>) {}
  TypedCodeAssemblerVariable(AssemblerDebugInfo debug_info,
                             TNode<T> initial_value, CodeAssembler* assembler)
      : CodeAssemblerVariable(assembler, debug_info,
                              PhiMachineRepresentationOf<T>, initial_value) {}
#endif  // DEBUG

  TNode<T> value() const {
    return TNode<T>::UncheckedCast(CodeAssemblerVariable::value());
  }

  void operator=(TNode<T> value) { Bind(value); }
  void operator=(const TypedCodeAssemblerVariable<T>& variable) {
    Bind(variable.value());
  }

 private:
  using CodeAssemblerVariable::Bind;
};

class V8_EXPORT_PRIVATE CodeAssemblerLabel {
 public:
  enum Type { kDeferred, kNonDeferred };

  explicit CodeAssemblerLabel(
      CodeAssembler* assembler,
      CodeAssemblerLabel::Type type = CodeAssemblerLabel::kNonDeferred)
      : CodeAssemblerLabel(assembler, 0, nullptr, type) {}
  CodeAssemblerLabel(
      CodeAssembler* assembler,
      const CodeAssemblerVariableList& merged_variables,
      CodeAssemblerLabel::Type type = CodeAssemblerLabel::kNonDeferred)
      : CodeAssemblerLabel(assembler, merged_variables.size(),
                           &(merged_variables[0]), type) {}
  CodeAssemblerLabel(
      CodeAssembler* assembler, size_t count,
      CodeAssemblerVariable* const* vars,
      CodeAssemblerLabel::Type type = CodeAssemblerLabel::kNonDeferred);
  CodeAssemblerLabel(
      CodeAssembler* assembler,
      std::initializer_list<CodeAssemblerVariable*> vars,
      CodeAssemblerLabel::Type type = CodeAssemblerLabel::kNonDeferred)
      : CodeAssemblerLabel(assembler, vars.size(), vars.begin(), type) {}
  CodeAssemblerLabel(
      CodeAssembler* assembler, CodeAssemblerVariable* merged_variable,
      CodeAssemblerLabel::Type type = CodeAssemblerLabel::kNonDeferred)
      : CodeAssemblerLabel(assembler, 1, &merged_variable, type) {}
  ~CodeAssemblerLabel();

  // Cannot be copied because the destructor explicitly call the destructor of
  // the underlying {RawMachineLabel}, hence only one pointer can point to it.
  CodeAssemblerLabel(const CodeAssemblerLabel&) = delete;
  CodeAssemblerLabel& operator=(const CodeAssemblerLabel&) = delete;

  inline bool is_bound() const { return bound_; }
  inline bool is_used() const { return merge_count_ != 0; }

 private:
  friend class CodeAssembler;

  void Bind();
#if DEBUG
  void Bind(AssemblerDebugInfo debug_info);
#endif  // DEBUG
  void UpdateVariablesAfterBind();
  void MergeVariables();

  bool bound_;
  size_t merge_count_;
  CodeAssemblerState* state_;
  RawMachineLabel* label_;
  // Map of variables that need to be merged to their phi nodes (or placeholders
  // for those phis).
  std::map<CodeAssemblerVariable::Impl*, Node*,
           CodeAssemblerVariable::ImplComparator>
      variable_phis_;
  // Map of variables to the list of value nodes that have been added from each
  // merge path in their order of merging.
  std::map<CodeAssemblerVariable::Impl*, std::vector<Node*>,
           CodeAssemblerVariable::ImplComparator>
      variable_merges_;
};

class CodeAssemblerParameterizedLabelBase {
 public:
  bool is_used() const { return plain_label_.is_used(); }
  explicit CodeAssemblerParameterizedLabelBase(CodeAssembler* assembler,
                                               size_t arity,
                                               CodeAssemblerLabel::Type type)
      : state_(assembler->state()),
        phi_inputs_(arity),
        plain_label_(assembler, type) {}

 protected:
  CodeAssemblerLabel* plain_label() { return &plain_label_; }
  void AddInputs(std::vector<Node*> inputs);
  Node* CreatePhi(MachineRepresentation rep, const std::vector<Node*>& inputs);
  const std::vector<Node*>& CreatePhis(
      std::vector<MachineRepresentation> representations);

 private:
  CodeAssemblerState* state_;
  std::vector<std::vector<Node*>> phi_inputs_;
  std::vector<Node*> phi_nodes_;
  CodeAssemblerLabel plain_label_;
};

template <class... Types>
class CodeAssemblerParameterizedLabel
    : public CodeAssemblerParameterizedLabelBase {
 public:
  static constexpr size_t kArity = sizeof...(Types);
  explicit CodeAssemblerParameterizedLabel(CodeAssembler* assembler,
                                           CodeAssemblerLabel::Type type)
      : CodeAssemblerParameterizedLabelBase(assembler, kArity, type) {}

 private:
  friend class CodeAssembler;

  void AddInputsVector(std::vector<Node*> inputs) {
    CodeAssemblerParameterizedLabelBase::AddInputs(std::move(inputs));
  }
  void AddInputs(TNode<Types>... inputs) {
    CodeAssemblerParameterizedLabelBase::AddInputs(
        std::vector<Node*>{inputs...});
  }
  void CreatePhis(TNode<Types>*... results) {
    const std::vector<Node*>& phi_nodes =
        CodeAssemblerParameterizedLabelBase::CreatePhis(
            {PhiMachineRepresentationOf<Types>...});
    auto it = phi_nodes.begin();
    USE(it);
    (AssignPhi(results, *(it++)), ...);
  }
  template <class T>
  static void AssignPhi(TNode<T>* result, Node* phi) {
    if (phi != nullptr) *result = TNode<T>::UncheckedCast(phi);
  }
};

using CodeAssemblerExceptionHandlerLabel =
    CodeAssemblerParameterizedLabel<Object>;

class V8_EXPORT_PRIVATE CodeAssemblerState {
 public:
  // Create with CallStub linkage.
  // |result_size| specifies the number of results returned by the stub.
  // TODO(rmcilroy): move result_size to the CallInterfaceDescriptor.
  CodeAssemblerState(Isolate* isolate, Zone* zone,
                     const CallInterfaceDescriptor& descriptor, CodeKind kind,
                     const char* name, Builtin builtin = Builtin::kNoBuiltinId);

  // Create with JSCall linkage.
  CodeAssemblerState(Isolate* isolate, Zone* zone, int parameter_count,
                     CodeKind kind, const char* name,
                     Builtin builtin = Builtin::kNoBuiltinId);

  ~CodeAssemblerState();

  CodeAssemblerState(const CodeAssemblerState&) = delete;
  CodeAssemblerState& operator=(const CodeAssemblerState&) = delete;

  const char* name() const { return name_; }
  int parameter_count() const;

#if DEBUG
  void PrintCurrentBlock(std::ostream& os);
#endif  // DEBUG
  bool InsideBlock();
  void SetInitialDebugInformation(const char* msg, const char* file, int line);

 private:
  friend class CodeAssembler;
  friend class CodeAssemblerLabel;
  friend class CodeAssemblerVariable;
  friend class CodeAssemblerTester;
  friend class CodeAssemblerParameterizedLabelBase;
  friend class ScopedExceptionHandler;

  CodeAssemblerState(Isolate* isolate, Zone* zone,
                     CallDescriptor* call_descriptor, CodeKind kind,
                     const char* name, Builtin builtin);

  void PushExceptionHandler(CodeAssemblerExceptionHandlerLabel* label);
  void PopExceptionHandler();

  std::unique_ptr<RawMachineAssembler> raw_assembler_;
  CodeKind kind_;
  const char* name_;
  Builtin builtin_;
  bool code_generated_;
  ZoneSet<CodeAssemblerVariable::Impl*, CodeAssemblerVariable::ImplComparator>
      variables_;
  CodeAssemblerCallback call_prologue_;
  CodeAssemblerCallback call_epilogue_;
  std::vector<CodeAssemblerExceptionHandlerLabel*> exception_handler_labels_;
  using VariableId = uint32_t;
  VariableId next_variable_id_ = 0;
  JSGraph* jsgraph_;

  // Only used by CodeStubAssembler builtins.
  std::vector<FileAndLine> macro_call_stack_;

  VariableId NextVariableId() { return next_variable_id_++; }
};

class V8_EXPORT_PRIVATE V8_NODISCARD ScopedExceptionHandler {
 public:
  ScopedExceptionHandler(CodeAssembler* assembler,
                         CodeAssemblerExceptionHandlerLabel* label);

  // Use this constructor for compatability/ports of old CSA code only. New code
  // should use the CodeAssemblerExceptionHandlerLabel version.
  ScopedExceptionHandler(CodeAssembler* assembler, CodeAssemblerLabel* label,
                         TypedCodeAssemblerVariable<Object>* exception);

  ~ScopedExceptionHandler();

 private:
  bool has_handler_;
  CodeAssembler* assembler_;
  CodeAssemblerLabel* compatibility_label_;
  std::unique_ptr<CodeAssemblerExceptionHandlerLabel> label_;
  TypedCodeAssemblerVariable<Object>* exception_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_CODE_ASSEMBLER_H_

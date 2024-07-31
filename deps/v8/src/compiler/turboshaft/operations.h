// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_OPERATIONS_H_
#define V8_COMPILER_TURBOSHAFT_OPERATIONS_H_

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/optional.h"
#include "src/base/platform/mutex.h"
#include "src/base/small-vector.h"
#include "src/base/template-utils.h"
#include "src/base/vector.h"
#include "src/codegen/external-reference.h"
#include "src/common/globals.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/fast-api-calls.h"
#include "src/compiler/globals.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/turboshaft/deopt-data.h"
#include "src/compiler/turboshaft/fast-hash.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/compiler/turboshaft/types.h"
#include "src/compiler/turboshaft/utils.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/flags/flags.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-module.h"
#endif

namespace v8::internal {
class HeapObject;
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           AbortReason reason);
}  // namespace v8::internal
namespace v8::internal::compiler {
class CallDescriptor;
class DeoptimizeParameters;
class FrameStateInfo;
class Node;
enum class TrapId : int32_t;
}  // namespace v8::internal::compiler
namespace v8::internal::compiler::turboshaft {
class Block;
struct FrameStateData;
class Graph;
struct FrameStateOp;

// This belongs to `VariableReducer` in `variable-reducer.h`. It is defined here
// because of cyclic header dependencies.
struct VariableData {
  MaybeRegisterRepresentation rep;
  bool loop_invariant;
  IntrusiveSetIndex active_loop_variables_index = {};
};
using Variable = SnapshotTable<OpIndex, VariableData>::Key;

// DEFINING NEW OPERATIONS
// =======================
// For each operation `Foo`, we define:
// - An entry V(Foo) in one of the TURBOSHAFT*OPERATION list (eg,
//   TURBOSHAFT_OPERATION_LIST_BLOCK_TERMINATOR,
//   TURBOSHAFT_SIMPLIFIED_OPERATION_LIST etc), which defines
//   `Opcode::kFoo` and whether the operation is a block terminator.
// - A `struct FooOp`, which derives from either `OperationT<FooOp>` or
//   `FixedArityOperationT<k, FooOp>` if the op always has excactly `k` inputs.
// Furthermore, the struct has to contain:
// - A bunch of options directly as public fields.
// - A getter `options()` returning a tuple of all these options. This is used
//   for default printing and hashing. Alternatively, `void
//   PrintOptions(std::ostream& os) const` and `size_t hash_value() const` can
//   also be defined manually.
// - Getters for named inputs.
// - A constructor that first takes all the inputs and then all the options. For
//   a variable arity operation where the constructor doesn't take the inputs as
//   a single base::Vector<OpIndex> argument, it's also necessary to overwrite
//   the static `New` function, see `CallOp` for an example.
// - An `Explode` method that unpacks an operation and invokes the passed
//   callback. If the operation inherits from FixedArityOperationT, the base
//   class already provides the required implementation.
// - `OpEffects` as either a static constexpr member `effects` or a
//   non-static method `Effects()` if the effects depend on the particular
//   operation and not just the opcode.
// - outputs_rep/inputs_rep methods, which should return a vector describing the
//   representation of the outputs and inputs of this operations.
// After defining the struct here, you'll also need to integrate it in
// Turboshaft:
// - If Foo is not in not lowered before reaching the instruction selector, add
//   a overload of ProcessOperation for FooOp in recreate-schedule.cc, and
//   handle Opcode::kFoo in the Turboshaft VisitNode of instruction-selector.cc.

#ifdef V8_INTL_SUPPORT
#define TURBOSHAFT_INTL_OPERATION_LIST(V) V(StringToCaseIntl)
#else
#define TURBOSHAFT_INTL_OPERATION_LIST(V)
#endif  // V8_INTL_SUPPORT

#ifdef V8_ENABLE_WEBASSEMBLY
// These operations should be lowered to Machine operations during
// WasmLoweringPhase.
#define TURBOSHAFT_WASM_OPERATION_LIST(V) \
  V(GlobalGet)                            \
  V(GlobalSet)                            \
  V(Null)                                 \
  V(IsNull)                               \
  V(AssertNotNull)                        \
  V(RttCanon)                             \
  V(WasmTypeCheck)                        \
  V(WasmTypeCast)                         \
  V(AnyConvertExtern)                     \
  V(ExternConvertAny)                     \
  V(WasmTypeAnnotation)                   \
  V(StructGet)                            \
  V(StructSet)                            \
  V(ArrayGet)                             \
  V(ArraySet)                             \
  V(ArrayLength)                          \
  V(WasmAllocateArray)                    \
  V(WasmAllocateStruct)                   \
  V(WasmRefFunc)                          \
  V(StringAsWtf16)                        \
  V(StringPrepareForGetCodeUnit)

#if V8_ENABLE_WASM_SIMD256_REVEC
#define TURBOSHAFT_SIMD256_OPERATION_LIST(V) \
  V(Simd256Extract128Lane)                   \
  V(Simd256LoadTransform)                    \
  V(Simd256Unary)                            \
  V(Simd256Binop)                            \
  V(Simd256Shift)                            \
  V(Simd256Ternary)
#else
#define TURBOSHAFT_SIMD256_OPERATION_LIST(V)
#endif

#define TURBOSHAFT_SIMD_OPERATION_LIST(V) \
  V(Simd128Constant)                      \
  V(Simd128Binop)                         \
  V(Simd128Unary)                         \
  V(Simd128Shift)                         \
  V(Simd128Test)                          \
  V(Simd128Splat)                         \
  V(Simd128Ternary)                       \
  V(Simd128ExtractLane)                   \
  V(Simd128ReplaceLane)                   \
  V(Simd128LaneMemory)                    \
  V(Simd128LoadTransform)                 \
  V(Simd128Shuffle)                       \
  TURBOSHAFT_SIMD256_OPERATION_LIST(V)

#else
#define TURBOSHAFT_WASM_OPERATION_LIST(V)
#define TURBOSHAFT_SIMD_OPERATION_LIST(V)
#endif

#define TURBOSHAFT_OPERATION_LIST_BLOCK_TERMINATOR(V) \
  V(CheckException)                                   \
  V(Goto)                                             \
  V(TailCall)                                         \
  V(Unreachable)                                      \
  V(Return)                                           \
  V(Branch)                                           \
  V(Switch)                                           \
  V(Deoptimize)

#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
#define TURBOSHAFT_CPED_OPERATION_LIST(V) \
  V(GetContinuationPreservedEmbedderData) \
  V(SetContinuationPreservedEmbedderData)
#else
#define TURBOSHAFT_CPED_OPERATION_LIST(V)
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA

// These operations should be lowered to Machine operations during
// MachineLoweringPhase.
#define TURBOSHAFT_SIMPLIFIED_OPERATION_LIST(V) \
  TURBOSHAFT_INTL_OPERATION_LIST(V)             \
  TURBOSHAFT_CPED_OPERATION_LIST(V)             \
  V(ArgumentsLength)                            \
  V(BigIntBinop)                                \
  V(BigIntComparison)                           \
  V(BigIntUnary)                                \
  V(CheckedClosure)                             \
  V(WordBinopDeoptOnOverflow)                   \
  V(CheckEqualsInternalizedString)              \
  V(CheckMaps)                                  \
  V(CompareMaps)                                \
  V(FloatIs)                                    \
  V(ObjectIs)                                   \
  V(ObjectIsNumericValue)                       \
  V(Float64SameValue)                           \
  V(SameValue)                                  \
  V(ChangeOrDeopt)                              \
  V(Convert)                                    \
  V(ConvertJSPrimitiveToObject)                 \
  V(ConvertJSPrimitiveToUntagged)               \
  V(ConvertJSPrimitiveToUntaggedOrDeopt)        \
  V(ConvertUntaggedToJSPrimitive)               \
  V(ConvertUntaggedToJSPrimitiveOrDeopt)        \
  V(TruncateJSPrimitiveToUntagged)              \
  V(TruncateJSPrimitiveToUntaggedOrDeopt)       \
  V(DoubleArrayMinMax)                          \
  V(EnsureWritableFastElements)                 \
  V(FastApiCall)                                \
  V(FindOrderedHashEntry)                       \
  V(LoadDataViewElement)                        \
  V(LoadFieldByIndex)                           \
  V(LoadMessage)                                \
  V(LoadStackArgument)                          \
  V(LoadTypedElement)                           \
  V(StoreDataViewElement)                       \
  V(StoreMessage)                               \
  V(StoreTypedElement)                          \
  V(MaybeGrowFastElements)                      \
  V(NewArgumentsElements)                       \
  V(NewArray)                                   \
  V(RuntimeAbort)                               \
  V(StaticAssert)                               \
  V(StringAt)                                   \
  V(StringComparison)                           \
  V(StringConcat)                               \
  V(StringFromCodePointAt)                      \
  V(StringIndexOf)                              \
  V(StringLength)                               \
  V(StringSubstring)                            \
  V(NewConsString)                              \
  V(TransitionAndStoreArrayElement)             \
  V(TransitionElementsKind)                     \
  V(DebugPrint)                                 \
  V(CheckTurboshaftTypeOf)

// These Operations are the lowest level handled by Turboshaft, and are
// supported by the InstructionSelector.
#define TURBOSHAFT_MACHINE_OPERATION_LIST(V) \
  V(WordBinop)                               \
  V(FloatBinop)                              \
  V(Word32PairBinop)                         \
  V(OverflowCheckedBinop)                    \
  V(WordUnary)                               \
  V(FloatUnary)                              \
  V(Shift)                                   \
  V(Comparison)                              \
  V(Change)                                  \
  V(TryChange)                               \
  V(BitcastWord32PairToFloat64)              \
  V(TaggedBitcast)                           \
  V(Select)                                  \
  V(PendingLoopPhi)                          \
  V(Constant)                                \
  V(LoadRootRegister)                        \
  V(Load)                                    \
  V(Store)                                   \
  V(Retain)                                  \
  V(Parameter)                               \
  V(OsrValue)                                \
  V(StackPointerGreaterThan)                 \
  V(StackSlot)                               \
  V(FrameConstant)                           \
  V(DeoptimizeIf)                            \
  IF_WASM(V, TrapIf)                         \
  IF_WASM(V, LoadStackPointer)               \
  IF_WASM(V, SetStackPointer)                \
  V(Phi)                                     \
  V(FrameState)                              \
  V(Call)                                    \
  V(CatchBlockBegin)                         \
  V(DidntThrow)                              \
  V(Tuple)                                   \
  V(Projection)                              \
  V(DebugBreak)                              \
  V(AssumeMap)                               \
  V(AtomicRMW)                               \
  V(AtomicWord32Pair)                        \
  V(MemoryBarrier)                           \
  V(Comment)

// These are operations used in the frontend and are mostly tied to JS
// semantics.
#define TURBOSHAFT_JS_OPERATION_LIST(V) \
  V(SpeculativeNumberBinop)             \
  V(GenericBinop)                       \
  V(GenericUnop)                        \
  V(ToNumberOrNumeric)

// These are operations that are not Machine operations and need to be lowered
// before Instruction Selection, but they are not lowered during the
// MachineLoweringPhase.
#define TURBOSHAFT_OTHER_OPERATION_LIST(V) \
  V(Allocate)                              \
  V(DecodeExternalPointer)                 \
  V(StackCheck)

#define TURBOSHAFT_OPERATION_LIST_NOT_BLOCK_TERMINATOR(V) \
  TURBOSHAFT_WASM_OPERATION_LIST(V)                       \
  TURBOSHAFT_SIMD_OPERATION_LIST(V)                       \
  TURBOSHAFT_MACHINE_OPERATION_LIST(V)                    \
  TURBOSHAFT_SIMPLIFIED_OPERATION_LIST(V)                 \
  TURBOSHAFT_JS_OPERATION_LIST(V)                         \
  TURBOSHAFT_OTHER_OPERATION_LIST(V)

#define TURBOSHAFT_OPERATION_LIST(V)            \
  TURBOSHAFT_OPERATION_LIST_BLOCK_TERMINATOR(V) \
  TURBOSHAFT_OPERATION_LIST_NOT_BLOCK_TERMINATOR(V)

enum class Opcode : uint8_t {
#define ENUM_CONSTANT(Name) k##Name,
  TURBOSHAFT_OPERATION_LIST(ENUM_CONSTANT)
#undef ENUM_CONSTANT
};

const char* OpcodeName(Opcode opcode);
constexpr std::underlying_type_t<Opcode> OpcodeIndex(Opcode x) {
  return static_cast<std::underlying_type_t<Opcode>>(x);
}

template <class Op>
struct operation_to_opcode_map {};

#define FORWARD_DECLARE(Name) struct Name##Op;
TURBOSHAFT_OPERATION_LIST(FORWARD_DECLARE)
#undef FORWARD_DECLARE

#define OPERATION_OPCODE_MAP_CASE(Name)    \
  template <>                              \
  struct operation_to_opcode_map<Name##Op> \
      : std::integral_constant<Opcode, Opcode::k##Name> {};
TURBOSHAFT_OPERATION_LIST(OPERATION_OPCODE_MAP_CASE)
#undef OPERATION_OPCODE_MAP_CASE

template <typename Op, uint64_t Mask, uint64_t Value>
struct OpMaskT {
  using operation = Op;
  static constexpr uint64_t mask = Mask;
  static constexpr uint64_t value = Value;
};

#define COUNT_OPCODES(Name) +1
constexpr uint16_t kNumberOfBlockTerminatorOpcodes =
    0 TURBOSHAFT_OPERATION_LIST_BLOCK_TERMINATOR(COUNT_OPCODES);
#undef COUNT_OPCODES

#define COUNT_OPCODES(Name) +1
constexpr uint16_t kNumberOfOpcodes =
    0 TURBOSHAFT_OPERATION_LIST(COUNT_OPCODES);
#undef COUNT_OPCODES

inline constexpr bool IsBlockTerminator(Opcode opcode) {
  return OpcodeIndex(opcode) < kNumberOfBlockTerminatorOpcodes;
}

// This list repeats the operations that may throw and need to be followed by
// `DidntThrow`.
#define TURBOSHAFT_THROWING_OPERATIONS_LIST(V) V(Call)

// Operations that need to be followed by `DidntThrowOp`.
inline constexpr bool MayThrow(Opcode opcode) {
#define CASE(Name) case Opcode::k##Name:
  switch (opcode) {
    TURBOSHAFT_THROWING_OPERATIONS_LIST(CASE)
    return true;
    default:
      return false;
  }
#undef CASE
}

template <typename T>
inline base::Vector<T> InitVectorOf(
    ZoneVector<T>& storage,
    std::initializer_list<RegisterRepresentation> values) {
  storage.resize(values.size());
  size_t i = 0;
  for (auto&& value : values) {
    storage[i++] = value;
  }
  return base::VectorOf(storage);
}

class InputsRepFactory {
 public:
  constexpr static base::Vector<const MaybeRegisterRepresentation> SingleRep(
      RegisterRepresentation rep) {
    return base::VectorOf(ToMaybeRepPointer(rep), 1);
  }

  constexpr static base::Vector<const MaybeRegisterRepresentation> PairOf(
      RegisterRepresentation rep) {
    return base::VectorOf(ToMaybeRepPointer(rep), 2);
  }

 protected:
  constexpr static const MaybeRegisterRepresentation* ToMaybeRepPointer(
      RegisterRepresentation rep) {
    size_t index = static_cast<size_t>(rep.value()) * 2;
    DCHECK_LT(index, arraysize(rep_map));
    return &rep_map[index];
  }

 private:
  constexpr static MaybeRegisterRepresentation rep_map[] = {
      MaybeRegisterRepresentation::Word32(),
      MaybeRegisterRepresentation::Word32(),
      MaybeRegisterRepresentation::Word64(),
      MaybeRegisterRepresentation::Word64(),
      MaybeRegisterRepresentation::Float32(),
      MaybeRegisterRepresentation::Float32(),
      MaybeRegisterRepresentation::Float64(),
      MaybeRegisterRepresentation::Float64(),
      MaybeRegisterRepresentation::Tagged(),
      MaybeRegisterRepresentation::Tagged(),
      MaybeRegisterRepresentation::Compressed(),
      MaybeRegisterRepresentation::Compressed(),
      MaybeRegisterRepresentation::Simd128(),
      MaybeRegisterRepresentation::Simd128(),
  };
};

struct EffectDimensions {
  // Produced by loads, consumed by operations that should not move before loads
  // because they change memory.
  bool load_heap_memory : 1;
  bool load_off_heap_memory : 1;

  // Produced by stores, consumed by operations that should not move before
  // stores because they load or store memory.
  bool store_heap_memory : 1;
  bool store_off_heap_memory : 1;

  // Operations that perform raw heap access (like initialization) consume
  // `before_raw_heap_access` and produce `after_raw_heap_access`.
  // Operations that need the heap to be in a consistent state produce
  // `before_raw_heap_access` and consume `after_raw_heap_access`.
  bool before_raw_heap_access : 1;
  // Produced by operations that access raw/untagged pointers into the
  // heap or keep such a pointer alive, consumed by operations that can GC to
  // ensure they don't move before the raw access.
  bool after_raw_heap_access : 1;

  // Produced by any operation that can affect whether subsequent operations are
  // executed, for example by branching, deopting, throwing or aborting.
  // Consumed by all operations that should not be hoisted before a check
  // because they rely on it. For example, loads usually rely on the shape of
  // the heap object or the index being in bounds.
  bool control_flow : 1;
  // We need to ensure that the padding bits have a specified value, as they are
  // observable in bitwise operations.
  uint8_t unused_padding : 1;

  using Bits = uint8_t;
  constexpr EffectDimensions()
      : load_heap_memory(false),
        load_off_heap_memory(false),
        store_heap_memory(false),
        store_off_heap_memory(false),
        before_raw_heap_access(false),
        after_raw_heap_access(false),
        control_flow(false),
        unused_padding(0) {}
  Bits bits() const { return base::bit_cast<Bits>(*this); }
  static EffectDimensions FromBits(Bits bits) {
    return base::bit_cast<EffectDimensions>(bits);
  }
  bool operator==(EffectDimensions other) const {
    return bits() == other.bits();
  }
  bool operator!=(EffectDimensions other) const {
    return bits() != other.bits();
  }
};
static_assert(sizeof(EffectDimensions) == sizeof(EffectDimensions::Bits));

// Possible reorderings are restricted using two bit vectors: `produces` and
// `consumes`. Two operations cannot be reordered if the first operation
// produces an effect dimension that the second operation consumes. This is not
// necessarily symmetric. For example, it is possible to reorder
//     Load(x)
//     CheckMaps(y)
// to become
//     CheckMaps(x)
//     Load(y)
// because the load cannot affect the map check. But the other direction could
// be unsound, if the load depends on the map check having been executed. The
// former reordering is useful to push a load across a check into a branch if
// it is only needed there. The effect system expresses this by having the map
// check produce `EffectDimensions::control_flow` and the load consuming
// `EffectDimensions::control_flow`. If the producing operation comes before the
// consuming operation, then this order has to be preserved. But if the
// consuming operation comes first, then we are free to reorder them. Operations
// that produce and consume the same effect dimension always have a fixed order
// among themselves. For example, stores produce and consume the store
// dimensions. It is possible for operations to be reorderable unless certain
// other operations appear in-between. This way, the IR can be generous with
// reorderings as long as all operations are high-level, but become more
// restrictive as soon as low-level operations appear. For example, allocations
// can be freely reordered. Tagged bitcasts can be reordered with other tagged
// bitcasts. But a tagged bitcast cannot be reordered with allocations, as this
// would mean that an untagged pointer can be alive while a GC is happening. The
// way this works is that allocations produce the `before_raw_heap_access`
// dimension and consume the `after_raw_heap_access` dimension to stay either
// before or after a raw heap access. This means that there are no ordering
// constraints between allocations themselves. Bitcasts should not
// be moved accross an allocation. We treat them as raw heap access by letting
// them consume `before_raw_heap_access` and produce `after_raw_heap_access`.
// This way, allocations cannot be moved across bitcasts. Similarily,
// initializing stores and uninitialized allocations are classified as raw heap
// access, to prevent any operation that relies on a consistent heap state to be
// scheduled in the middle of an inline allocation. As long as we didn't lower
// to raw heap accesses yet, pure allocating operations or operations reading
// immutable memory can float freely. As soon as there are raw heap accesses,
// they become more restricted in their movement. Note that calls are not the
// most side-effectful operations, as they do not leave the heap in an
// inconsistent state, so they do not need to be marked as raw heap access.
struct OpEffects {
  EffectDimensions produces;
  EffectDimensions consumes;

  // Operations that cannot be merged because they produce identity.  That is,
  // every repetition can produce a different result, but the order in which
  // they are executed does not matter. All we care about is that they are
  // different. Producing a random number or allocating an object with
  // observable pointer equality are examples. Producing identity doesn't
  // restrict reordering in straight-line code, but we must prevent using GVN or
  // moving identity-producing operations in- or out of loops.
  bool can_create_identity : 1;
  // If the operation can allocate and therefore can trigger GC.
  bool can_allocate : 1;
  // Instructions that have no uses but are `required_when_unused` should not be
  // removed.
  bool required_when_unused : 1;
  // We need to ensure that the padding bits have a specified value, as they are
  // observable in bitwise operations.  This is split into two fields so that
  // also MSVC creates the correct object layout.
  uint8_t unused_padding_1 : 5;
  uint8_t unused_padding_2;

  constexpr OpEffects()
      : can_create_identity(false),
        can_allocate(false),
        required_when_unused(false),
        unused_padding_1(0),
        unused_padding_2(0) {}

  using Bits = uint32_t;
  Bits bits() const { return base::bit_cast<Bits>(*this); }
  static OpEffects FromBits(Bits bits) {
    return base::bit_cast<OpEffects>(bits);
  }

  bool operator==(OpEffects other) const { return bits() == other.bits(); }
  bool operator!=(OpEffects other) const { return bits() != other.bits(); }
  OpEffects operator|(OpEffects other) const {
    return FromBits(bits() | other.bits());
  }
  OpEffects operator&(OpEffects other) const {
    return FromBits(bits() & other.bits());
  }
  bool IsSubsetOf(OpEffects other) const {
    return (bits() & ~other.bits()) == 0;
  }

  constexpr OpEffects AssumesConsistentHeap() const {
    OpEffects result = *this;
    // Do not move the operation into a region with raw heap access.
    result.produces.before_raw_heap_access = true;
    result.consumes.after_raw_heap_access = true;
    return result;
  }
  // Like `CanAllocate()`, but allocated values must be immutable and not have
  // identity (for example `HeapNumber`).
  // Note that if we first allocate something as mutable and later make it
  // immutable, we have to allocate it with identity.
  constexpr OpEffects CanAllocateWithoutIdentity() const {
    OpEffects result = AssumesConsistentHeap();
    result.can_allocate = true;
    return result;
  }
  // Allocations change the GC state and can trigger GC, as well as produce a
  // fresh identity.
  constexpr OpEffects CanAllocate() const {
    return CanAllocateWithoutIdentity().CanCreateIdentity();
  }
  // The operation can leave the heap in an incosistent state or have untagged
  // pointers into the heap as input or output.
  constexpr OpEffects CanDoRawHeapAccess() const {
    OpEffects result = *this;
    // Do not move any operation that relies on a consistent heap state accross.
    result.produces.after_raw_heap_access = true;
    result.consumes.before_raw_heap_access = true;
    return result;
  }
  // Reading mutable heap memory. Reading immutable memory doesn't count.
  constexpr OpEffects CanReadHeapMemory() const {
    OpEffects result = *this;
    result.produces.load_heap_memory = true;
    // Do not reorder before stores.
    result.consumes.store_heap_memory = true;
    return result;
  }
  // Reading mutable off-heap memory or other input. Reading immutable memory
  // doesn't count.
  constexpr OpEffects CanReadOffHeapMemory() const {
    OpEffects result = *this;
    result.produces.load_off_heap_memory = true;
    // Do not reorder before stores.
    result.consumes.store_off_heap_memory = true;
    return result;
  }
  // Writing any off-memory or other output.
  constexpr OpEffects CanWriteOffHeapMemory() const {
    OpEffects result = *this;
    result.required_when_unused = true;
    result.produces.store_off_heap_memory = true;
    // Do not reorder before stores.
    result.consumes.store_off_heap_memory = true;
    // Do not reorder before loads.
    result.consumes.load_off_heap_memory = true;
    // Do not move before deopting or aborting operations.
    result.consumes.control_flow = true;
    return result;
  }
  // Writing heap memory that existed before the operation started. Initializing
  // newly allocated memory doesn't count.
  constexpr OpEffects CanWriteHeapMemory() const {
    OpEffects result = *this;
    result.required_when_unused = true;
    result.produces.store_heap_memory = true;
    // Do not reorder before stores.
    result.consumes.store_heap_memory = true;
    // Do not reorder before loads.
    result.consumes.load_heap_memory = true;
    // Do not move before deopting or aborting operations.
    result.consumes.control_flow = true;
    return result;
  }
  // Writing any memory or other output, on- or off-heap.
  constexpr OpEffects CanWriteMemory() const {
    return CanWriteHeapMemory().CanWriteOffHeapMemory();
  }
  // Reading any memory or other input, on- or off-heap.
  constexpr OpEffects CanReadMemory() const {
    return CanReadHeapMemory().CanReadOffHeapMemory();
  }
  // The operation might read immutable data from the heap, so it can be freely
  // reordered with operations that keep the heap in a consistent state. But we
  // must prevent the operation from observing an incompletely initialized
  // object.
  constexpr OpEffects CanReadImmutableMemory() const {
    OpEffects result = AssumesConsistentHeap();
    return result;
  }
  // Partial operations that are only safe to execute after we performed certain
  // checks, for example loads may only be safe after a corresponding bound or
  // map checks.
  constexpr OpEffects CanDependOnChecks() const {
    OpEffects result = *this;
    result.consumes.control_flow = true;
    return result;
  }
  // The operation can affect control flow (like branch, deopt, throw or crash).
  constexpr OpEffects CanChangeControlFlow() const {
    OpEffects result = *this;
    result.required_when_unused = true;
    // Signal that this changes control flow. Prevents stores or operations
    // relying on checks from flowing before this operation.
    result.produces.control_flow = true;
    // Stores must not flow past something that affects control flow.
    result.consumes.store_heap_memory = true;
    result.consumes.store_off_heap_memory = true;
    return result;
  }
  // Execution of the current function may end with this operation, for example
  // because of return, deopt, exception throw or abort/trap.
  constexpr OpEffects CanLeaveCurrentFunction() const {
    // All memory becomes observable.
    return CanChangeControlFlow().CanReadMemory().RequiredWhenUnused();
  }
  // The operation can deopt.
  constexpr OpEffects CanDeopt() const {
    return CanLeaveCurrentFunction()
        // We might depend on previous checks to avoid deopting.
        .CanDependOnChecks();
  }
  // Producing identity doesn't prevent reorderings, but it prevents GVN from
  // de-duplicating identical operations.
  constexpr OpEffects CanCreateIdentity() const {
    OpEffects result = *this;
    result.can_create_identity = true;
    return result;
  }
  // The set of all possible effects.
  constexpr OpEffects CanCallAnything() const {
    return CanReadMemory()
        .CanWriteMemory()
        .CanAllocate()
        .CanChangeControlFlow()
        .CanDependOnChecks()
        .RequiredWhenUnused();
  }
  constexpr OpEffects RequiredWhenUnused() const {
    OpEffects result = *this;
    result.required_when_unused = true;
    return result;
  }

  // Operations that can be removed if their result is not used. Unused
  // allocations can be removed.
  constexpr bool is_required_when_unused() const {
    return required_when_unused;
  }
  // Operations that can be moved before a preceding branch or check.
  bool hoistable_before_a_branch() const {
    // Since this excludes `CanDependOnChecks()`, most loads actually cannot be
    // hoisted.
    return IsSubsetOf(OpEffects().CanReadMemory());
  }
  // Operations that can be eliminated via value numbering, which means that if
  // there are two identical operations where one dominates the other, then the
  // second can be replaced with the first one. This is safe for deopting or
  // throwing operations, because the absence of read effects guarantees
  // deterministic behavior.
  bool repetition_is_eliminatable() const {
    return IsSubsetOf(OpEffects()
                          .CanDependOnChecks()
                          .CanChangeControlFlow()
                          .CanAllocateWithoutIdentity());
  }
  bool can_read_mutable_memory() const {
    return produces.load_heap_memory | produces.load_off_heap_memory;
  }
  bool requires_consistent_heap() const {
    return produces.before_raw_heap_access | consumes.after_raw_heap_access;
  }
  bool can_write() const {
    return produces.store_heap_memory | produces.store_off_heap_memory;
  }
  bool can_be_constant_folded() const {
    // Operations that CanDependOnChecks can still be constant-folded. If they
    // did indeed depend on a check, then their result will only be used after
    // said check has been executed anyways.
    return IsSubsetOf(OpEffects().CanDependOnChecks());
  }
};
static_assert(sizeof(OpEffects) == sizeof(OpEffects::Bits));

inline bool CannotSwapOperations(OpEffects first, OpEffects second) {
  return first.produces.bits() & (second.consumes.bits());
}

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           OpEffects op_effects);

// SaturatedUint8 is a wrapper around a uint8_t, which can be incremented and
// decremented with the `Incr` and `Decr` methods. These methods prevent over-
// and underflow, and saturate once the uint8_t reaches the maximum (255):
// future increment and decrement will not change the value then.
// We purposefuly do not expose the uint8_t directly, so that users go through
// Incr/Decr/SetToZero/SetToOne to manipulate it, so that the saturation and
// lack of over/underflow is always respected.
class SaturatedUint8 {
 public:
  SaturatedUint8() = default;

  void Incr() {
    if (V8_LIKELY(val != kMax)) {
      val++;
    }
  }
  void Decr() {
    if (V8_LIKELY(val != 0 && val != kMax)) {
      val--;
    }
  }

  void SetToZero() { val = 0; }
  void SetToOne() { val = 1; }

  bool IsZero() const { return val == 0; }
  bool IsOne() const { return val == 1; }
  bool IsSaturated() const { return val == kMax; }
  uint8_t Get() const { return val; }

  SaturatedUint8& operator+=(const SaturatedUint8& other) {
    uint32_t sum = val;
    sum += other.val;
    val = static_cast<uint8_t>(std::min<uint32_t>(sum, kMax));
    return *this;
  }

  static SaturatedUint8 FromSize(size_t value) {
    uint8_t val = static_cast<uint8_t>(std::min<size_t>(value, kMax));
    return SaturatedUint8{val};
  }

 private:
  explicit SaturatedUint8(uint8_t val) : val(val) {}
  uint8_t val = 0;
  static constexpr uint8_t kMax = std::numeric_limits<uint8_t>::max();
};

// underlying_operation<> is used to extract the operation type from OpMaskT
// classes used in Operation::Is<> and Operation::TryCast<>.
template <typename T>
struct underlying_operation {
  using type = T;
};
template <typename T, uint64_t M, uint64_t V>
struct underlying_operation<OpMaskT<T, M, V>> {
  using type = T;
};
template <typename T>
using underlying_operation_t = typename underlying_operation<T>::type;

// Baseclass for all Turboshaft operations.
// The `alignas(OpIndex)` is necessary because it is followed by an array of
// `OpIndex` inputs.
struct alignas(OpIndex) Operation {
  struct IdentityMapper {
    OpIndex Map(OpIndex index) { return index; }
    OptionalOpIndex Map(OptionalOpIndex index) { return index; }
    template <size_t N>
    base::SmallVector<OpIndex, N> Map(base::Vector<const OpIndex> indices) {
      return base::SmallVector<OpIndex, N>{indices};
    }
  };

  const Opcode opcode;

  // The number of uses of this operation in the current graph.
  // Instead of overflowing, we saturate the value if it reaches the maximum. In
  // this case, the true number of uses is unknown.
  // We use such a small type to save memory and because nodes with a high
  // number of uses are rare. Additionally, we usually only care if the number
  // of uses is 0, 1 or bigger than 1.
  SaturatedUint8 saturated_use_count;

  const uint16_t input_count;

  // The inputs are stored adjacent in memory, right behind the `Operation`
  // object.
  base::Vector<const OpIndex> inputs() const;
  V8_INLINE OpIndex input(size_t i) const { return inputs()[i]; }

  static size_t StorageSlotCount(Opcode opcode, size_t input_count);
  size_t StorageSlotCount() const {
    return StorageSlotCount(opcode, input_count);
  }

  base::Vector<const RegisterRepresentation> outputs_rep() const;
  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const;

  template <class Op>
  bool Is() const {
    if constexpr (std::is_base_of_v<Operation, Op>) {
      return opcode == Op::opcode;
    } else {
      // Otherwise this must be OpMaskT.
      return IsOpmask<Op>();
    }
  }
  template <class Op>
  underlying_operation_t<Op>& Cast() {
    DCHECK(Is<Op>());
    return *static_cast<underlying_operation_t<Op>*>(this);
  }
  template <class Op>
  const underlying_operation_t<Op>& Cast() const {
    DCHECK(Is<Op>());
    return *static_cast<const underlying_operation_t<Op>*>(this);
  }
  template <class Op>
  const underlying_operation_t<Op>* TryCast() const {
    if (!Is<Op>()) return nullptr;
    return static_cast<const underlying_operation_t<Op>*>(this);
  }
  template <class Op>
  underlying_operation_t<Op>* TryCast() {
    if (!Is<Op>()) return nullptr;
    return static_cast<underlying_operation_t<Op>*>(this);
  }
  OpEffects Effects() const;
  bool IsBlockTerminator() const {
    return turboshaft::IsBlockTerminator(opcode);
  }
  bool IsRequiredWhenUnused() const {
    DCHECK_IMPLIES(IsBlockTerminator(), Effects().is_required_when_unused());
    return Effects().is_required_when_unused();
  }

  std::string ToString() const;
  void PrintInputs(std::ostream& os, const std::string& op_index_prefix) const;
  void PrintOptions(std::ostream& os) const;

  // Returns true if {this} is the only operation using {value}.
  bool IsOnlyUserOf(const Operation& value, const Graph& graph) const;

 protected:
  // Operation objects store their inputs behind the object. Therefore, they can
  // only be constructed as part of a Graph.
  explicit Operation(Opcode opcode, size_t input_count)
      : opcode(opcode), input_count(input_count) {
    DCHECK_LE(input_count,
              std::numeric_limits<decltype(this->input_count)>::max());
  }

  template <class OpmaskT>
  // A Turboshaft operation can be as small as 4 Bytes while Opmasks can span up
  // to 8 Bytes. Any mask larger than the operation it is compared with will
  // always have a mismatch in the initialized memory. Still, there can be some
  // uninitialized memory being compared as part of the 8 Byte comparison that
  // this function performs.
  V8_CLANG_NO_SANITIZE("memory") bool IsOpmask() const {
    static_assert(std::is_same_v<
                  underlying_operation_t<OpmaskT>,
                  typename OpMaskT<typename OpmaskT::operation, OpmaskT::mask,
                                   OpmaskT::value>::operation>);
    // We check with the given mask.
    uint64_t b;
    memcpy(&b, this, sizeof(uint64_t));
    b &= OpmaskT::mask;
    return b == OpmaskT::value;
  }

  Operation(const Operation&) = delete;
  Operation& operator=(const Operation&) = delete;
};

struct OperationPrintStyle {
  const Operation& op;
  const char* op_index_prefix = "#";
};

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           OperationPrintStyle op);
inline std::ostream& operator<<(std::ostream& os, const Operation& op) {
  return os << OperationPrintStyle{op};
}
V8_EXPORT_PRIVATE void Print(const Operation& op);

V8_EXPORT_PRIVATE Zone* get_zone(Graph* graph);

OperationStorageSlot* AllocateOpStorage(Graph* graph, size_t slot_count);
V8_EXPORT_PRIVATE const Operation& Get(const Graph& graph, OpIndex index);

// Determine if an operation declares `effects`, which means that its
// effects are static and don't depend on inputs or options.
template <class Op, class = void>
struct HasStaticEffects : std::bool_constant<false> {};
template <class Op>
struct HasStaticEffects<Op, std::void_t<decltype(Op::effects)>>
    : std::bool_constant<true> {};

// This template knows the complete type of the operation and is plugged into
// the inheritance hierarchy. It removes boilerplate from the concrete
// `Operation` subclasses, defining everything that can be expressed
// generically. It overshadows many methods from `Operation` with ones that
// exploit additional static information.
template <class Derived>
struct OperationT : Operation {
  // Enable concise base-constructor call in derived struct.
  using Base = OperationT;

  static const Opcode opcode;

  static constexpr OpEffects Effects() { return Derived::effects; }
  static constexpr bool IsBlockTerminator() {
    return turboshaft::IsBlockTerminator(opcode);
  }
  bool IsRequiredWhenUnused() const {
    return IsBlockTerminator() ||
           derived_this().Effects().is_required_when_unused();
  }

  static constexpr base::Optional<OpEffects> EffectsIfStatic() {
    if constexpr (HasStaticEffects<Derived>::value) {
      return Derived::Effects();
    }
    return base::nullopt;
  }

  Derived& derived_this() { return *static_cast<Derived*>(this); }
  const Derived& derived_this() const {
    return *static_cast<const Derived*>(this);
  }

  // Shadow Operation::inputs to exploit static knowledge about object size.
  base::Vector<OpIndex> inputs() {
    return {reinterpret_cast<OpIndex*>(reinterpret_cast<char*>(this) +
                                       sizeof(Derived)),
            derived_this().input_count};
  }
  base::Vector<const OpIndex> inputs() const {
    return {reinterpret_cast<const OpIndex*>(
                reinterpret_cast<const char*>(this) + sizeof(Derived)),
            derived_this().input_count};
  }

  V8_INLINE OpIndex& input(size_t i) { return derived_this().inputs()[i]; }
  V8_INLINE OpIndex input(size_t i) const { return derived_this().inputs()[i]; }

  static size_t StorageSlotCount(size_t input_count) {
    // The operation size in bytes is:
    //   `sizeof(Derived) + input_count*sizeof(OpIndex)`.
    // This is an optimized computation of:
    //   round_up(size_in_bytes / sizeof(StorageSlot))
    constexpr size_t r = sizeof(OperationStorageSlot) / sizeof(OpIndex);
    static_assert(sizeof(OperationStorageSlot) % sizeof(OpIndex) == 0);
    static_assert(sizeof(Derived) % sizeof(OpIndex) == 0);
    size_t result = std::max<size_t>(
        2, (r - 1 + sizeof(Derived) / sizeof(OpIndex) + input_count) / r);
    DCHECK_EQ(result, Operation::StorageSlotCount(opcode, input_count));
    return result;
  }
  size_t StorageSlotCount() const { return StorageSlotCount(input_count); }

  template <class... Args>
  static Derived& New(Graph* graph, size_t input_count, Args... args) {
    OperationStorageSlot* ptr =
        AllocateOpStorage(graph, StorageSlotCount(input_count));
    Derived* result = new (ptr) Derived(std::move(args)...);
#ifdef DEBUG
    result->Validate(*graph);
    ZoneVector<MaybeRegisterRepresentation> storage(get_zone(graph));
    base::Vector<const MaybeRegisterRepresentation> expected =
        result->inputs_rep(storage);
    // TODO(mliedtke): DCHECK that expected and inputs are of the same size
    // and adapt inputs_rep() to always emit a representation for all inputs.
    size_t end = std::min<size_t>(expected.size(), result->input_count);
    for (size_t i = 0; i < end; ++i) {
      if (expected[i] == MaybeRegisterRepresentation::None()) continue;
      DCHECK(ValidOpInputRep(*graph, result->inputs()[i],
                             RegisterRepresentation(expected[i])));
    }
#endif
    // If this DCHECK fails, then the number of inputs specified in the
    // operation constructor and in the static New function disagree.
    DCHECK_EQ(input_count, result->Operation::input_count);
    return *result;
  }

  template <class... Args>
  static Derived& New(Graph* graph, base::Vector<const OpIndex> inputs,
                      Args... args) {
    return New(graph, inputs.size(), inputs, args...);
  }

  explicit OperationT(size_t input_count) : Operation(opcode, input_count) {
    static_assert((std::is_base_of<OperationT, Derived>::value));
#if !V8_CC_MSVC
    static_assert(std::is_trivially_copyable<Derived>::value);
#endif  // !V8_CC_MSVC
    static_assert(std::is_trivially_destructible<Derived>::value);
  }
  explicit OperationT(base::Vector<const OpIndex> inputs)
      : OperationT(inputs.size()) {
    this->inputs().OverwriteWith(inputs);
  }

  bool EqualsForGVN(const Base& other) const {
    // By default, GVN only removed identical Operations. However, some
    // Operations (like DeoptimizeIf) can be GVNed when a dominating
    // similar-but-not-identical one exists. In that case, the Operation should
    // redefine EqualsForGVN, so that GVN knows which inputs or options of the
    // Operation to ignore (you should also probably redefine hash_value,
    // otherwise GVN won't even try to call EqualsForGVN).
    return derived_this() == other.derived_this();
  }
  bool operator==(const Base& other) const {
    return derived_this().inputs() == other.derived_this().inputs() &&
           derived_this().options() == other.derived_this().options();
  }
  size_t hash_value() const {
    return fast_hash_combine(opcode, derived_this().inputs(),
                             derived_this().options());
  }

  void PrintInputs(std::ostream& os, const std::string& op_index_prefix) const {
    os << "(";
    bool first = true;
    for (OpIndex input : inputs()) {
      if (!first) os << ", ";
      first = false;
      os << op_index_prefix << input.id();
    }
    os << ")";
  }

  void PrintOptions(std::ostream& os) const {
    const auto& options = derived_this().options();
    constexpr size_t options_count =
        std::tuple_size<std::remove_reference_t<decltype(options)>>::value;
    if (options_count == 0) {
      return;
    }
    PrintOptionsHelper(os, options, std::make_index_sequence<options_count>());
  }

  // Check graph invariants for this operation. Will be invoked in debug mode
  // immediately upon construction.
  // Concrete Operator classes are expected to re-define it.
  void Validate(const Graph& graph) const = delete;

 private:
  template <class... T, size_t... I>
  static void PrintOptionsHelper(std::ostream& os,
                                 const std::tuple<T...>& options,
                                 std::index_sequence<I...>) {
    os << "[";
    bool first = true;
    USE(first);
    ((first ? (first = false, os << std::get<I>(options))
            : os << ", " << std::get<I>(options)),
     ...);
    os << "]";
  }

  // All Operations have to define the outputs_rep function, to which
  // Operation::outputs_rep() will forward, based on their opcode. If you forget
  // to define it, then Operation::outputs_rep() would forward to itself,
  // resulting in an infinite loop. To avoid this, we define here in OperationT
  // a private version outputs_rep (with no implementation): if an operation
  // forgets to define outputs_rep, then Operation::outputs_rep() tries to call
  // this private version, which fails at compile time.
  base::Vector<const RegisterRepresentation> outputs_rep() const;

  // Returns a vector of the input representations.
  // The passed in {storage} can be used to store the underlying data.
  // The returned vector might be smaller than the input_count in which case the
  // additional inputs are assumed to have no register representation.
  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const;
};

template <size_t InputCount, class Derived>
struct FixedArityOperationT : OperationT<Derived> {
  // Enable concise base access in derived struct.
  using Base = FixedArityOperationT;

  // Shadow Operation::input_count to exploit static knowledge.
  static constexpr uint16_t input_count = InputCount;

  template <class... Args>
  explicit FixedArityOperationT(Args... args)
      : OperationT<Derived>(InputCount) {
    static_assert(sizeof...(Args) == InputCount, "wrong number of inputs");
    size_t i = 0;
    OpIndex* inputs = this->inputs().begin();
    ((inputs[i++] = args), ...);
  }

  // Redefine the input initialization to tell C++ about the static input size.
  template <class... Args>
  static Derived& New(Graph* graph, Args... args) {
    Derived& result =
        OperationT<Derived>::New(graph, InputCount, std::move(args)...);
    return result;
  }

  template <typename Fn, typename Mapper, size_t... InputI, size_t... OptionI>
  V8_INLINE auto ExplodeImpl(Fn fn, Mapper& mapper,
                             std::index_sequence<InputI...>,
                             std::index_sequence<OptionI...>) const {
    auto options = this->derived_this().options();
    USE(options);
    return fn(mapper.Map(this->input(InputI))...,
              std::get<OptionI>(options)...);
  }

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    return ExplodeImpl(
        fn, mapper, std::make_index_sequence<input_count>(),
        std::make_index_sequence<
            std::tuple_size_v<decltype(this->derived_this().options())>>());
  }
};

#define SUPPORTED_OPERATIONS_LIST(V)               \
  V(float32_round_down, Float32RoundDown)          \
  V(float64_round_down, Float64RoundDown)          \
  V(float32_round_up, Float32RoundUp)              \
  V(float64_round_up, Float64RoundUp)              \
  V(float32_round_to_zero, Float32RoundTruncate)   \
  V(float64_round_to_zero, Float64RoundTruncate)   \
  V(float32_round_ties_even, Float32RoundTiesEven) \
  V(float64_round_ties_even, Float64RoundTiesEven) \
  V(float64_round_ties_away, Float64RoundTiesAway) \
  V(int32_div_is_safe, Int32DivIsSafe)             \
  V(uint32_div_is_safe, Uint32DivIsSafe)           \
  V(word32_shift_is_safe, Word32ShiftIsSafe)       \
  V(word32_ctz, Word32Ctz)                         \
  V(word64_ctz, Word64Ctz)                         \
  V(word64_ctz_lowerable, Word64CtzLowerable)      \
  V(word32_popcnt, Word32Popcnt)                   \
  V(word64_popcnt, Word64Popcnt)                   \
  V(word32_reverse_bits, Word32ReverseBits)        \
  V(word64_reverse_bits, Word64ReverseBits)        \
  V(float32_select, Float32Select)                 \
  V(float64_select, Float64Select)                 \
  V(int32_abs_with_overflow, Int32AbsWithOverflow) \
  V(int64_abs_with_overflow, Int64AbsWithOverflow) \
  V(word32_rol, Word32Rol)                         \
  V(word64_rol, Word64Rol)                         \
  V(word64_rol_lowerable, Word64RolLowerable)      \
  V(sat_conversion_is_safe, SatConversionIsSafe)   \
  V(word32_select, Word32Select)                   \
  V(word64_select, Word64Select)

class V8_EXPORT_PRIVATE SupportedOperations {
#define DECLARE_FIELD(name, machine_name) bool name##_;
#define DECLARE_GETTER(name, machine_name)     \
  static bool name() {                         \
    if constexpr (DEBUG_BOOL) {                \
      base::MutexGuard lock(mutex_.Pointer()); \
      DCHECK(initialized_);                    \
    }                                          \
    return instance_.name##_;                  \
  }

 public:
  static void Initialize();
  static bool IsUnalignedLoadSupported(MemoryRepresentation repr);
  static bool IsUnalignedStoreSupported(MemoryRepresentation repr);
  SUPPORTED_OPERATIONS_LIST(DECLARE_GETTER)

 private:
  SUPPORTED_OPERATIONS_LIST(DECLARE_FIELD)

  static bool initialized_;
  static base::LazyMutex mutex_;
  static SupportedOperations instance_;

#undef DECLARE_FIELD
#undef DECLARE_GETTER
};

template <RegisterRepresentation::Enum... reps>
base::Vector<const RegisterRepresentation> RepVector() {
  static constexpr std::array<RegisterRepresentation, sizeof...(reps)>
      rep_array{RegisterRepresentation{reps}...};
  return base::VectorOf(rep_array);
}

template <MaybeRegisterRepresentation::Enum... reps>
base::Vector<const MaybeRegisterRepresentation> MaybeRepVector() {
  static constexpr std::array<MaybeRegisterRepresentation, sizeof...(reps)>
      rep_array{MaybeRegisterRepresentation{reps}...};
  return base::VectorOf(rep_array);
}

V8_EXPORT_PRIVATE bool ValidOpInputRep(
    const Graph& graph, OpIndex input,
    std::initializer_list<RegisterRepresentation> expected_rep,
    base::Optional<size_t> projection_index = {});
V8_EXPORT_PRIVATE bool ValidOpInputRep(
    const Graph& graph, OpIndex input, RegisterRepresentation expected_rep,
    base::Optional<size_t> projection_index = {});

struct GenericBinopOp : FixedArityOperationT<4, GenericBinopOp> {
#define GENERIC_BINOP_LIST(V) \
  V(Add)                      \
  V(Multiply)                 \
  V(Subtract)                 \
  V(Divide)                   \
  V(Modulus)                  \
  V(Exponentiate)             \
  V(BitwiseAnd)               \
  V(BitwiseOr)                \
  V(BitwiseXor)               \
  V(ShiftLeft)                \
  V(ShiftRight)               \
  V(ShiftRightLogical)        \
  V(Equal)                    \
  V(StrictEqual)              \
  V(LessThan)                 \
  V(LessThanOrEqual)          \
  V(GreaterThan)              \
  V(GreaterThanOrEqual)
  enum class Kind : uint8_t {
#define DEFINE_KIND(Name) k##Name,
    GENERIC_BINOP_LIST(DEFINE_KIND)
#undef DEFINE_KIND
  };
  Kind kind;

  static constexpr OpEffects effects = OpEffects().CanCallAnything();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged()>();
  }

  V<Object> left() const { return input(0); }
  V<Object> right() const { return input(1); }
  OpIndex frame_state() const { return input(2); }
  V<Context> context() const { return input(3); }

  GenericBinopOp(V<Object> left, V<Object> right, OpIndex frame_state,
                 V<Context> context, Kind kind)
      : Base(left, right, frame_state, context), kind(kind) {}

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           GenericBinopOp::Kind kind);

struct GenericUnopOp : FixedArityOperationT<3, GenericUnopOp> {
#define GENERIC_UNOP_LIST(V) \
  V(BitwiseNot)              \
  V(Negate)                  \
  V(Increment)               \
  V(Decrement)
  enum class Kind : uint8_t {
#define DEFINE_KIND(Name) k##Name,
    GENERIC_UNOP_LIST(DEFINE_KIND)
#undef DEFINE_KIND
  };
  Kind kind;

  static constexpr OpEffects effects = OpEffects().CanCallAnything();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  V<Object> input() const { return Base::input(0); }
  OpIndex frame_state() const { return Base::input(1); }
  V<Context> context() const { return Base::input(2); }

  GenericUnopOp(V<Object> input, OpIndex frame_state, V<Context> context,
                Kind kind)
      : Base(input, frame_state, context), kind(kind) {}

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           GenericUnopOp::Kind kind);

struct ToNumberOrNumericOp : FixedArityOperationT<3, ToNumberOrNumericOp> {
  Object::Conversion kind;

  static constexpr OpEffects effects = OpEffects().CanCallAnything();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }
  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  V<Object> input() const { return Base::input(0); }
  OpIndex frame_state() const { return Base::input(1); }
  V<Context> context() const { return Base::input(2); }

  ToNumberOrNumericOp(V<Object> input, OpIndex frame_state, V<Context> context,
                      Object::Conversion kind)
      : Base(input, frame_state, context), kind(kind) {}

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{kind}; }
};

struct WordBinopOp : FixedArityOperationT<2, WordBinopOp> {
  enum class Kind : uint8_t {
    kAdd,
    kMul,
    kSignedMulOverflownBits,
    kUnsignedMulOverflownBits,
    kBitwiseAnd,
    kBitwiseOr,
    kBitwiseXor,
    kSub,
    kSignedDiv,
    kUnsignedDiv,
    kSignedMod,
    kUnsignedMod,
  };
  Kind kind;
  WordRepresentation rep;

  // We must avoid division by 0.
  static constexpr OpEffects effects = OpEffects().CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(static_cast<const RegisterRepresentation*>(&rep), 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::PairOf(rep);
  }

  OpIndex left() const { return input(0); }
  OpIndex right() const { return input(1); }

  static bool IsCommutative(Kind kind) {
    switch (kind) {
      case Kind::kAdd:
      case Kind::kMul:
      case Kind::kSignedMulOverflownBits:
      case Kind::kUnsignedMulOverflownBits:
      case Kind::kBitwiseAnd:
      case Kind::kBitwiseOr:
      case Kind::kBitwiseXor:
        return true;
      case Kind::kSub:
      case Kind::kSignedDiv:
      case Kind::kUnsignedDiv:
      case Kind::kSignedMod:
      case Kind::kUnsignedMod:
        return false;
    }
  }

  static bool IsAssociative(Kind kind) {
    switch (kind) {
      case Kind::kAdd:
      case Kind::kMul:
      case Kind::kBitwiseAnd:
      case Kind::kBitwiseOr:
      case Kind::kBitwiseXor:
        return true;
      case Kind::kSignedMulOverflownBits:
      case Kind::kUnsignedMulOverflownBits:
      case Kind::kSub:
      case Kind::kSignedDiv:
      case Kind::kUnsignedDiv:
      case Kind::kSignedMod:
      case Kind::kUnsignedMod:
        return false;
    }
  }
  // The Word32 and Word64 versions of the operator compute the same result when
  // truncated to 32 bit.
  static bool AllowsWord64ToWord32Truncation(Kind kind) {
    switch (kind) {
      case Kind::kAdd:
      case Kind::kMul:
      case Kind::kBitwiseAnd:
      case Kind::kBitwiseOr:
      case Kind::kBitwiseXor:
      case Kind::kSub:
        return true;
      case Kind::kSignedMulOverflownBits:
      case Kind::kUnsignedMulOverflownBits:
      case Kind::kSignedDiv:
      case Kind::kUnsignedDiv:
      case Kind::kSignedMod:
      case Kind::kUnsignedMod:
        return false;
    }
  }

  WordBinopOp(OpIndex left, OpIndex right, Kind kind, WordRepresentation rep)
      : Base(left, right), kind(kind), rep(rep) {}

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{kind, rep}; }
  void PrintOptions(std::ostream& os) const;
};

struct FloatBinopOp : FixedArityOperationT<2, FloatBinopOp> {
  enum class Kind : uint8_t {
    kAdd,
    kMul,
    kMin,
    kMax,
    kSub,
    kDiv,
    kMod,
    kPower,
    kAtan2,
  };
  Kind kind;
  FloatRepresentation rep;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(static_cast<const RegisterRepresentation*>(&rep), 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::PairOf(rep);
  }

  OpIndex left() const { return input(0); }
  OpIndex right() const { return input(1); }

  static bool IsCommutative(Kind kind) {
    switch (kind) {
      case Kind::kAdd:
      case Kind::kMul:
      case Kind::kMin:
      case Kind::kMax:
        return true;
      case Kind::kSub:
      case Kind::kDiv:
      case Kind::kMod:
      case Kind::kPower:
      case Kind::kAtan2:
        return false;
    }
  }

  FloatBinopOp(OpIndex left, OpIndex right, Kind kind, FloatRepresentation rep)
      : Base(left, right), kind(kind), rep(rep) {}

  void Validate(const Graph& graph) const {
    DCHECK_IMPLIES(kind == any_of(Kind::kPower, Kind::kAtan2, Kind::kMod),
                   rep == FloatRepresentation::Float64());
  }
  auto options() const { return std::tuple{kind, rep}; }
  void PrintOptions(std::ostream& os) const;
};

struct Word32PairBinopOp : FixedArityOperationT<4, Word32PairBinopOp> {
  enum class Kind : uint8_t {
    kAdd,
    kSub,
    kMul,
    kShiftLeft,
    kShiftRightArithmetic,
    kShiftRightLogical,
  };
  Kind kind;

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32(),
                     RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      const ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Word32(),
                          MaybeRegisterRepresentation::Word32(),
                          MaybeRegisterRepresentation::Word32(),
                          MaybeRegisterRepresentation::Word32()>();
  }

  OpIndex left_low() const { return input(0); }
  OpIndex left_high() const { return input(1); }
  OpIndex right_low() const { return input(2); }
  OpIndex right_high() const { return input(3); }

  Word32PairBinopOp(OpIndex left_low, OpIndex left_high, OpIndex right_low,
                    OpIndex right_high, Kind kind)
      : Base(left_low, left_high, right_low, right_high), kind(kind) {}

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{kind}; }
  void PrintOptions(std::ostream& os) const;
};

struct WordBinopDeoptOnOverflowOp
    : FixedArityOperationT<3, WordBinopDeoptOnOverflowOp> {
  enum class Kind : uint8_t {
    kSignedAdd,
    kSignedMul,
    kSignedSub,
    kSignedDiv,
    kSignedMod,
    kUnsignedDiv,
    kUnsignedMod
  };
  Kind kind;
  WordRepresentation rep;
  FeedbackSource feedback;
  CheckForMinusZeroMode mode;

  static constexpr OpEffects effects = OpEffects().CanDeopt();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(static_cast<const RegisterRepresentation*>(&rep), 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::PairOf(rep);
  }

  OpIndex left() const { return input(0); }
  OpIndex right() const { return input(1); }
  OpIndex frame_state() const { return input(2); }

  WordBinopDeoptOnOverflowOp(OpIndex left, OpIndex right, OpIndex frame_state,
                             Kind kind, WordRepresentation rep,
                             FeedbackSource feedback,
                             CheckForMinusZeroMode mode)
      : Base(left, right, frame_state),
        kind(kind),
        rep(rep),
        feedback(feedback),
        mode(mode) {}

  void Validate(const Graph& graph) const {
    DCHECK_IMPLIES(kind == Kind::kUnsignedDiv || kind == Kind::kUnsignedMod,
                   rep == WordRepresentation::Word32());
  }
  auto options() const { return std::tuple{kind, rep, feedback, mode}; }
  void PrintOptions(std::ostream& os) const;
};

struct OverflowCheckedBinopOp
    : FixedArityOperationT<2, OverflowCheckedBinopOp> {
  static constexpr int kValueIndex = 0;
  static constexpr int kOverflowIndex = 1;

  enum class Kind : uint8_t {
    kSignedAdd,
    kSignedMul,
    kSignedSub,
  };
  Kind kind;
  WordRepresentation rep;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    switch (rep.value()) {
      case WordRepresentation::Word32():
        return RepVector<RegisterRepresentation::Word32(),
                         RegisterRepresentation::Word32()>();
      case WordRepresentation::Word64():
        return RepVector<RegisterRepresentation::Word64(),
                         RegisterRepresentation::Word32()>();
    }
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::PairOf(rep);
  }

  OpIndex left() const { return input(0); }
  OpIndex right() const { return input(1); }

  static bool IsCommutative(Kind kind) {
    switch (kind) {
      case Kind::kSignedAdd:
      case Kind::kSignedMul:
        return true;
      case Kind::kSignedSub:
        return false;
    }
  }

  OverflowCheckedBinopOp(OpIndex left, OpIndex right, Kind kind,
                         WordRepresentation rep)
      : Base(left, right), kind(kind), rep(rep) {}

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{kind, rep}; }
  void PrintOptions(std::ostream& os) const;
};

struct WordUnaryOp : FixedArityOperationT<1, WordUnaryOp> {
  enum class Kind : uint8_t {
    kReverseBytes,
    kCountLeadingZeros,
    kCountTrailingZeros,
    kPopCount,
    kSignExtend8,
    kSignExtend16,
  };
  Kind kind;
  WordRepresentation rep;
  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(static_cast<const RegisterRepresentation*>(&rep), 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::SingleRep(rep);
  }

  OpIndex input() const { return Base::input(0); }

  V8_EXPORT_PRIVATE static bool IsSupported(Kind kind, WordRepresentation rep);

  explicit WordUnaryOp(OpIndex input, Kind kind, WordRepresentation rep)
      : Base(input), kind(kind), rep(rep) {}

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{kind, rep}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           WordUnaryOp::Kind kind);

struct FloatUnaryOp : FixedArityOperationT<1, FloatUnaryOp> {
  enum class Kind : uint8_t {
    kAbs,
    kNegate,
    kSilenceNaN,
    kRoundDown,      // round towards -infinity
    kRoundUp,        // round towards +infinity
    kRoundToZero,    // round towards 0
    kRoundTiesEven,  // break ties by rounding towards the next even number
    kLog,
    kLog2,
    kLog10,
    kLog1p,
    kSqrt,
    kCbrt,
    kExp,
    kExpm1,
    kSin,
    kCos,
    kSinh,
    kCosh,
    kAcos,
    kAsin,
    kAsinh,
    kAcosh,
    kTan,
    kTanh,
    kAtan,
    kAtanh,
  };

  Kind kind;
  FloatRepresentation rep;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(static_cast<const RegisterRepresentation*>(&rep), 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::SingleRep(rep);
  }

  OpIndex input() const { return Base::input(0); }

  V8_EXPORT_PRIVATE static bool IsSupported(Kind kind, FloatRepresentation rep);

  explicit FloatUnaryOp(OpIndex input, Kind kind, FloatRepresentation rep)
      : Base(input), kind(kind), rep(rep) {}

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{kind, rep}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           FloatUnaryOp::Kind kind);

struct ShiftOp : FixedArityOperationT<2, ShiftOp> {
  enum class Kind : uint8_t {
    kShiftRightArithmeticShiftOutZeros,
    kShiftRightArithmetic,
    kShiftRightLogical,
    kShiftLeft,
    kRotateRight,
    kRotateLeft
  };
  Kind kind;
  WordRepresentation rep;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(static_cast<const RegisterRepresentation*>(&rep), 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InitVectorOf(storage,
                        {static_cast<const RegisterRepresentation&>(rep),
                         RegisterRepresentation::Word32()});
  }

  OpIndex left() const { return input(0); }
  OpIndex right() const { return input(1); }

  static bool IsRightShift(Kind kind) {
    switch (kind) {
      case Kind::kShiftRightArithmeticShiftOutZeros:
      case Kind::kShiftRightArithmetic:
      case Kind::kShiftRightLogical:
        return true;
      case Kind::kShiftLeft:
      case Kind::kRotateRight:
      case Kind::kRotateLeft:
        return false;
    }
  }
  // The Word32 and Word64 versions of the operator compute the same result when
  // truncated to 32 bit.
  static bool AllowsWord64ToWord32Truncation(Kind kind) {
    switch (kind) {
      case Kind::kShiftLeft:
        return true;
      case Kind::kShiftRightArithmeticShiftOutZeros:
      case Kind::kShiftRightArithmetic:
      case Kind::kShiftRightLogical:
      case Kind::kRotateRight:
      case Kind::kRotateLeft:
        return false;
    }
  }

  ShiftOp(OpIndex left, OpIndex right, Kind kind, WordRepresentation rep)
      : Base(left, right), kind(kind), rep(rep) {}

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{kind, rep}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           ShiftOp::Kind kind);

struct ComparisonOp : FixedArityOperationT<2, ComparisonOp> {
  enum class Kind : uint8_t {
    kEqual,
    kSignedLessThan,
    kSignedLessThanOrEqual,
    kUnsignedLessThan,
    kUnsignedLessThanOrEqual
  };
  Kind kind;
  RegisterRepresentation rep;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::PairOf(rep);
  }

  static bool IsCommutative(Kind kind) { return kind == Kind::kEqual; }

  OpIndex left() const { return input(0); }
  OpIndex right() const { return input(1); }

  ComparisonOp(OpIndex left, OpIndex right, Kind kind,
               RegisterRepresentation rep)
      : Base(left, right), kind(kind), rep(rep) {}

  void Validate(const Graph& graph) const {
    if (kind == Kind::kEqual) {
      DCHECK(rep == any_of(RegisterRepresentation::Word32(),
                           RegisterRepresentation::Word64(),
                           RegisterRepresentation::Float32(),
                           RegisterRepresentation::Float64(),
                           RegisterRepresentation::Tagged()));

      RegisterRepresentation input_rep = rep;
#ifdef V8_COMPRESS_POINTERS
      // In the presence of pointer compression, we only compare the lower
      // 32bit.
      if (input_rep == RegisterRepresentation::Tagged()) {
        input_rep = RegisterRepresentation::Compressed();
      }
#endif  // V8_COMPRESS_POINTERS
      DCHECK(ValidOpInputRep(graph, left(), input_rep));
      DCHECK(ValidOpInputRep(graph, right(), input_rep));
      USE(input_rep);
    } else {
      DCHECK_EQ(rep, any_of(RegisterRepresentation::Word32(),
                            RegisterRepresentation::Word64(),
                            RegisterRepresentation::Float32(),
                            RegisterRepresentation::Float64()));
      DCHECK_IMPLIES(
          rep == any_of(RegisterRepresentation::Float32(),
                        RegisterRepresentation::Float64()),
          kind == any_of(Kind::kSignedLessThan, Kind::kSignedLessThanOrEqual));
    }
  }
  auto options() const { return std::tuple{kind, rep}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           ComparisonOp::Kind kind);
DEFINE_MULTI_SWITCH_INTEGRAL(ComparisonOp::Kind, 8)

struct ChangeOp : FixedArityOperationT<1, ChangeOp> {
  enum class Kind : uint8_t {
    // convert between different floating-point types. Note that the
    // Float64->Float32 conversion is truncating.
    kFloatConversion,
    // overflow guaranteed to result in the minimal integer
    kSignedFloatTruncateOverflowToMin,
    kUnsignedFloatTruncateOverflowToMin,
    // JS semantics float64 to word32 truncation
    // https://tc39.es/ecma262/#sec-touint32
    kJSFloatTruncate,
    // convert (un)signed integer to floating-point value
    kSignedToFloat,
    kUnsignedToFloat,
    // extract half of a float64 value
    kExtractHighHalf,
    kExtractLowHalf,
    // increase bit-width for unsigned integer values
    kZeroExtend,
    // increase bid-width for signed integer values
    kSignExtend,
    // truncate word64 to word32
    kTruncate,
    // preserve bits, change meaning
    kBitcast
  };
  // Violated assumptions result in undefined behavior.
  enum class Assumption : uint8_t {
    kNoAssumption,
    // Used for conversions from floating-point to integer, assumes that the
    // value doesn't exceed the integer range.
    kNoOverflow,
    // Assume that the original value can be recovered by a corresponding
    // reverse transformation.
    kReversible,
  };
  Kind kind;
  // Reversible means undefined behavior if value cannot be represented
  // precisely.
  Assumption assumption;
  RegisterRepresentation from;
  RegisterRepresentation to;

  // Returns true if change<kind>(change<reverse_kind>(a)) == a for all a.
  // This assumes that change<reverse_kind> uses the inverted {from} and {to}
  // representations, i.e. the input to the inner change op has the same
  // representation as the result of the outer change op.
  static bool IsReversible(Kind kind, Assumption assumption,
                           RegisterRepresentation from,
                           RegisterRepresentation to, Kind reverse_kind,
                           bool signalling_nan_possible) {
    switch (kind) {
      case Kind::kFloatConversion:
        return from == RegisterRepresentation::Float32() &&
               to == RegisterRepresentation::Float64() &&
               reverse_kind == Kind::kFloatConversion &&
               !signalling_nan_possible;
      case Kind::kSignedFloatTruncateOverflowToMin:
        return assumption == Assumption::kReversible &&
               reverse_kind == Kind::kSignedToFloat;
      case Kind::kUnsignedFloatTruncateOverflowToMin:
        return assumption == Assumption::kReversible &&
               reverse_kind == Kind::kUnsignedToFloat;
      case Kind::kJSFloatTruncate:
        return false;
      case Kind::kSignedToFloat:
        if (from == RegisterRepresentation::Word32() &&
            to == RegisterRepresentation::Float64()) {
          return reverse_kind == any_of(Kind::kSignedFloatTruncateOverflowToMin,
                                        Kind::kJSFloatTruncate);
        } else {
          return assumption == Assumption::kReversible &&
                 reverse_kind ==
                     any_of(Kind::kSignedFloatTruncateOverflowToMin);
        }
      case Kind::kUnsignedToFloat:
        if (from == RegisterRepresentation::Word32() &&
            to == RegisterRepresentation::Float64()) {
          return reverse_kind ==
                 any_of(Kind::kUnsignedFloatTruncateOverflowToMin,
                        Kind::kJSFloatTruncate);
        } else {
          return assumption == Assumption::kReversible &&
                 reverse_kind == Kind::kUnsignedFloatTruncateOverflowToMin;
        }
      case Kind::kExtractHighHalf:
      case Kind::kExtractLowHalf:
        return false;
      case Kind::kZeroExtend:
      case Kind::kSignExtend:
        DCHECK_EQ(from, RegisterRepresentation::Word32());
        DCHECK_EQ(to, RegisterRepresentation::Word64());
        return reverse_kind == Kind::kTruncate;
      case Kind::kTruncate:
        DCHECK_EQ(from, RegisterRepresentation::Word64());
        DCHECK_EQ(to, RegisterRepresentation::Word32());
        return reverse_kind == Kind::kBitcast;
      case Kind::kBitcast:
        return reverse_kind == Kind::kBitcast;
    }
  }

  bool IsReversibleBy(Kind reverse_kind, bool signalling_nan_possible) const {
    return IsReversible(kind, assumption, from, to, reverse_kind,
                        signalling_nan_possible);
  }

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(&to, 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::SingleRep(from);
  }

  OpIndex input() const { return Base::input(0); }

  ChangeOp(OpIndex input, Kind kind, Assumption assumption,
           RegisterRepresentation from, RegisterRepresentation to)
      : Base(input), kind(kind), assumption(assumption), from(from), to(to) {}

  void Validate(const Graph& graph) const {
    // Bitcasts from and to Tagged should use a TaggedBitcast instead (which has
    // different effects, since it's unsafe to reorder such bitcasts accross
    // GCs).
    DCHECK_IMPLIES(kind == Kind::kBitcast,
                   from != RegisterRepresentation::Tagged() &&
                       to != RegisterRepresentation::Tagged());
  }
  auto options() const { return std::tuple{kind, assumption, from, to}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           ChangeOp::Kind kind);
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           ChangeOp::Assumption assumption);
DEFINE_MULTI_SWITCH_INTEGRAL(ChangeOp::Kind, 16)
DEFINE_MULTI_SWITCH_INTEGRAL(ChangeOp::Assumption, 4)

struct ChangeOrDeoptOp : FixedArityOperationT<2, ChangeOrDeoptOp> {
  enum class Kind : uint8_t {
    kUint32ToInt32,
    kInt64ToInt32,
    kUint64ToInt32,
    kUint64ToInt64,
    kFloat64ToInt32,
    kFloat64ToInt64,
    kFloat64NotHole,
  };
  Kind kind;
  CheckForMinusZeroMode minus_zero_mode;
  FeedbackSource feedback;

  static constexpr OpEffects effects = OpEffects().CanDeopt();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    switch (kind) {
      case Kind::kUint32ToInt32:
      case Kind::kInt64ToInt32:
      case Kind::kUint64ToInt32:
      case Kind::kFloat64ToInt32:
        return RepVector<RegisterRepresentation::Word32()>();
      case Kind::kUint64ToInt64:
      case Kind::kFloat64ToInt64:
        return RepVector<RegisterRepresentation::Word64()>();
      case Kind::kFloat64NotHole:
        return RepVector<RegisterRepresentation::Float64()>();
    }
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    switch (kind) {
      case Kind::kUint32ToInt32:
        return MaybeRepVector<MaybeRegisterRepresentation::Word32()>();
      case Kind::kInt64ToInt32:
      case Kind::kUint64ToInt32:
      case Kind::kUint64ToInt64:
        return MaybeRepVector<MaybeRegisterRepresentation::Word64()>();
      case Kind::kFloat64ToInt32:
      case Kind::kFloat64ToInt64:
      case Kind::kFloat64NotHole:
        return MaybeRepVector<MaybeRegisterRepresentation::Float64()>();
    }
  }

  OpIndex input() const { return Base::input(0); }
  OpIndex frame_state() const { return Base::input(1); }

  ChangeOrDeoptOp(OpIndex input, OpIndex frame_state, Kind kind,
                  CheckForMinusZeroMode minus_zero_mode,
                  const FeedbackSource& feedback)
      : Base(input, frame_state),
        kind(kind),
        minus_zero_mode(minus_zero_mode),
        feedback(feedback) {}

  void Validate(const Graph& graph) const {
    DCHECK(Get(graph, frame_state()).Is<FrameStateOp>());
  }

  auto options() const { return std::tuple{kind, minus_zero_mode, feedback}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           ChangeOrDeoptOp::Kind kind);

// Perform a conversion and return a pair of the result and a bit if it was
// successful.
struct TryChangeOp : FixedArityOperationT<1, TryChangeOp> {
  static constexpr uint32_t kSuccessValue = 1;
  static constexpr uint32_t kFailureValue = 0;
  enum class Kind : uint8_t {
    // The result of the truncation is undefined if the result is out of range.
    kSignedFloatTruncateOverflowUndefined,
    kUnsignedFloatTruncateOverflowUndefined,
  };
  Kind kind;
  FloatRepresentation from;
  WordRepresentation to;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    switch (to.value()) {
      case WordRepresentation::Word32():
        return RepVector<RegisterRepresentation::Word32(),
                         RegisterRepresentation::Word32()>();
      case WordRepresentation::Word64():
        return RepVector<RegisterRepresentation::Word64(),
                         RegisterRepresentation::Word32()>();
    }
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::SingleRep(from);
  }

  OpIndex input() const { return Base::input(0); }

  TryChangeOp(OpIndex input, Kind kind, FloatRepresentation from,
              WordRepresentation to)
      : Base(input), kind(kind), from(from), to(to) {}

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{kind, from, to}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           TryChangeOp::Kind kind);

struct BitcastWord32PairToFloat64Op
    : FixedArityOperationT<2, BitcastWord32PairToFloat64Op> {
  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Float64()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Word32(),
                          MaybeRegisterRepresentation::Word32()>();
  }

  OpIndex high_word32() const { return input(0); }
  OpIndex low_word32() const { return input(1); }

  BitcastWord32PairToFloat64Op(OpIndex high_word32, OpIndex low_word32)
      : Base(high_word32, low_word32) {}

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{}; }
};

struct TaggedBitcastOp : FixedArityOperationT<1, TaggedBitcastOp> {
  enum class Kind : uint8_t {
    kSmi,  // This is a bitcast from a Word to a Smi or from a Smi to a Word
    kHeapObject,     // This is a bitcast from or to a Heap Object
    kTagAndSmiBits,  // This is a bitcast where only access to the tag and the
                     // smi bits (if it's a smi) are valid
    kAny
  };
  Kind kind;
  RegisterRepresentation from;
  RegisterRepresentation to;

  OpEffects Effects() const {
    switch (kind) {
      case Kind::kSmi:
      case Kind::kTagAndSmiBits:
        return OpEffects();
      case Kind::kHeapObject:
      case Kind::kAny:
        // Due to moving GC, converting from or to pointers doesn't commute with
        // GC.
        return OpEffects().CanDoRawHeapAccess();
    }
  }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(&to, 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::SingleRep(from);
  }

  OpIndex input() const { return Base::input(0); }

  TaggedBitcastOp(OpIndex input, RegisterRepresentation from,
                  RegisterRepresentation to, Kind kind)
      : Base(input), kind(kind), from(from), to(to) {}

  void Validate(const Graph& graph) const {
    if (kind == Kind::kSmi) {
      DCHECK((from.IsWord() && to.IsTaggedOrCompressed()) ||
             (from.IsTaggedOrCompressed() && to.IsWord()));
      DCHECK_IMPLIES(from == RegisterRepresentation::Word64() ||
                         to == RegisterRepresentation::Word64(),
                     Is64());
    } else {
      // TODO(nicohartmann@): Without implicit trucation, the first case might
      // not be correct anymore.
      DCHECK((from.IsWord() && to == RegisterRepresentation::Tagged()) ||
             (from == RegisterRepresentation::Tagged() &&
              to == RegisterRepresentation::WordPtr()) ||
             (from == RegisterRepresentation::Compressed() &&
              to == RegisterRepresentation::Word32()));
    }
  }
  auto options() const { return std::tuple{from, to, kind}; }
};
std::ostream& operator<<(std::ostream& os, TaggedBitcastOp::Kind assumption);

struct SelectOp : FixedArityOperationT<3, SelectOp> {
  enum class Implementation : uint8_t { kBranch, kCMove };

  RegisterRepresentation rep;
  BranchHint hint;
  Implementation implem;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(&rep, 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InitVectorOf(storage, {RegisterRepresentation::Word32(), rep, rep});
  }

  SelectOp(OpIndex cond, OpIndex vtrue, OpIndex vfalse,
           RegisterRepresentation rep, BranchHint hint, Implementation implem)
      : Base(cond, vtrue, vfalse), rep(rep), hint(hint), implem(implem) {}

  void Validate(const Graph& graph) const {
    DCHECK_IMPLIES(implem == Implementation::kCMove,
                   (rep == RegisterRepresentation::Word32() &&
                    SupportedOperations::word32_select()) ||
                       (rep == RegisterRepresentation::Word64() &&
                        SupportedOperations::word64_select()) ||
                       (rep == RegisterRepresentation::Float32() &&
                        SupportedOperations::float32_select()) ||
                       (rep == RegisterRepresentation::Float64() &&
                        SupportedOperations::float64_select()));
  }

  OpIndex cond() const { return input(0); }
  OpIndex vtrue() const { return input(1); }
  OpIndex vfalse() const { return input(2); }

  auto options() const { return std::tuple{rep, hint, implem}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           SelectOp::Implementation kind);

struct PhiOp : OperationT<PhiOp> {
  RegisterRepresentation rep;

  // Phis have to remain at the beginning of the current block. As effects
  // cannot express this completely, we just mark them as having no effects but
  // treat them specially when scheduling operations.
  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(&rep, 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    storage.resize(input_count);
    for (size_t i = 0; i < input_count; ++i) {
      storage[i] = rep;
    }
    return base::VectorOf(storage);
  }

  static constexpr size_t kLoopPhiBackEdgeIndex = 1;

  explicit PhiOp(base::Vector<const OpIndex> inputs, RegisterRepresentation rep)
      : Base(inputs), rep(rep) {}

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    auto mapped_inputs = mapper.template Map<64>(inputs());
    return fn(base::VectorOf(mapped_inputs), rep);
  }

  void Validate(const Graph& graph) const { DCHECK_GT(input_count, 0); }
  auto options() const { return std::tuple{rep}; }
};

// Used as a placeholder for a loop-phi while building the graph, replaced with
// a normal `PhiOp` before graph building is over, so it should never appear in
// a complete graph.
struct PendingLoopPhiOp : FixedArityOperationT<1, PendingLoopPhiOp> {
  RegisterRepresentation rep;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(&rep, 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::SingleRep(rep);
  }

  OpIndex first() const { return input(0); }
  PendingLoopPhiOp(OpIndex first, RegisterRepresentation rep)
      : Base(first), rep(rep) {}

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{rep}; }
};

struct ConstantOp : FixedArityOperationT<0, ConstantOp> {
  enum class Kind : uint8_t {
    kWord32,
    kWord64,
    kFloat32,
    kFloat64,
    kSmi,
    kNumber,  // TODO(tebbi): See if we can avoid number constants.
    kTaggedIndex,
    kExternal,
    kHeapObject,
    kCompressedHeapObject,
    kRelocatableWasmCall,
    kRelocatableWasmStubCall
  };

  Kind kind;
  RegisterRepresentation rep = Representation(kind);
  union Storage {
    uint64_t integral;
    float float32;
    double float64;
    ExternalReference external;
    Handle<HeapObject> handle;

    Storage(uint64_t integral = 0) : integral(integral) {}
    Storage(i::Tagged<Smi> smi) : integral(smi.ptr()) {}
    Storage(double constant) : float64(constant) {}
    Storage(float constant) : float32(constant) {}
    Storage(ExternalReference constant) : external(constant) {}
    Storage(Handle<HeapObject> constant) : handle(constant) {}

    inline bool operator==(const ConstantOp::Storage&) const {
      // It is tricky to implement this properly. We currently need to define
      // this for the matchers, but this should never be called.
      UNREACHABLE();
    }
  } storage;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(&rep, 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  static RegisterRepresentation Representation(Kind kind) {
    switch (kind) {
      case Kind::kWord32:
        return RegisterRepresentation::Word32();
      case Kind::kWord64:
        return RegisterRepresentation::Word64();
      case Kind::kFloat32:
        return RegisterRepresentation::Float32();
      case Kind::kFloat64:
        return RegisterRepresentation::Float64();
      case Kind::kExternal:
      case Kind::kTaggedIndex:
      case Kind::kRelocatableWasmCall:
      case Kind::kRelocatableWasmStubCall:
        return RegisterRepresentation::WordPtr();
      case Kind::kSmi:
      case Kind::kHeapObject:
      case Kind::kNumber:
        return RegisterRepresentation::Tagged();
      case Kind::kCompressedHeapObject:
        return RegisterRepresentation::Compressed();
    }
  }

  ConstantOp(Kind kind, Storage storage)
      : Base(), kind(kind), storage(storage) {}

  void Validate(const Graph& graph) const {
    DCHECK_IMPLIES(
        kind == Kind::kWord32,
        storage.integral <= WordRepresentation::Word32().MaxUnsignedValue());
  }

  uint64_t integral() const {
    DCHECK(IsIntegral());
    return storage.integral;
  }

  int64_t signed_integral() const {
    DCHECK(IsIntegral());
    switch (kind) {
      case Kind::kWord32:
        return static_cast<int32_t>(storage.integral);
      case Kind::kWord64:
        return static_cast<int64_t>(storage.integral);
      default:
        UNREACHABLE();
    }
  }

  uint32_t word32() const {
    DCHECK(kind == Kind::kWord32 || kind == Kind::kWord64);
    return static_cast<uint32_t>(storage.integral);
  }

  uint64_t word64() const {
    DCHECK_EQ(kind, Kind::kWord64);
    return static_cast<uint64_t>(storage.integral);
  }

  i::Tagged<Smi> smi() const {
    DCHECK_EQ(kind, Kind::kSmi);
    return i::Tagged<Smi>(storage.integral);
  }

  double number() const {
    DCHECK_EQ(kind, Kind::kNumber);
    return storage.float64;
  }

  float float32() const {
    DCHECK_EQ(kind, Kind::kFloat32);
    return storage.float32;
  }

  double float64() const {
    DCHECK_EQ(kind, Kind::kFloat64);
    return storage.float64;
  }

  int32_t tagged_index() const {
    DCHECK_EQ(kind, Kind::kTaggedIndex);
    return static_cast<int32_t>(static_cast<uint32_t>(storage.integral));
  }

  ExternalReference external_reference() const {
    DCHECK_EQ(kind, Kind::kExternal);
    return storage.external;
  }

  Handle<i::HeapObject> handle() const {
    DCHECK(kind == Kind::kHeapObject || kind == Kind::kCompressedHeapObject);
    return storage.handle;
  }

  bool IsWord(uint64_t value) const {
    switch (kind) {
      case Kind::kWord32:
        return static_cast<uint32_t>(value) == word32();
      case Kind::kWord64:
        return value == word64();
      default:
        UNREACHABLE();
    }
  }

  bool IsIntegral() const {
    return kind == Kind::kWord32 || kind == Kind::kWord64 ||
           kind == Kind::kRelocatableWasmCall ||
           kind == Kind::kRelocatableWasmStubCall;
  }

  auto options() const { return std::tuple{kind, storage}; }

  void PrintOptions(std::ostream& os) const;
  size_t hash_value() const {
    switch (kind) {
      case Kind::kWord32:
      case Kind::kWord64:
      case Kind::kSmi:
      case Kind::kTaggedIndex:
      case Kind::kRelocatableWasmCall:
      case Kind::kRelocatableWasmStubCall:
        return fast_hash_combine(opcode, kind, storage.integral);
      case Kind::kFloat32:
        return fast_hash_combine(opcode, kind, storage.float32);
      case Kind::kFloat64:
      case Kind::kNumber:
        return fast_hash_combine(opcode, kind, storage.float64);
      case Kind::kExternal:
        return fast_hash_combine(opcode, kind, storage.external.address());
      case Kind::kHeapObject:
      case Kind::kCompressedHeapObject:
        return fast_hash_combine(opcode, kind, storage.handle.address());
    }
  }
  bool operator==(const ConstantOp& other) const {
    if (kind != other.kind) return false;
    switch (kind) {
      case Kind::kWord32:
      case Kind::kWord64:
      case Kind::kSmi:
      case Kind::kTaggedIndex:
      case Kind::kRelocatableWasmCall:
      case Kind::kRelocatableWasmStubCall:
        return storage.integral == other.storage.integral;
      case Kind::kFloat32:
        // Using a bit_cast to uint32_t in order to return false when comparing
        // +0 and -0.
        // Note: for JavaScript, it would be fine to return true when both
        // values are NaNs, but for Wasm we must not merge NaNs that way.
        // Since we canonicalize NaNs for JS anyway, we don't need to treat
        // them specially here.
        return base::bit_cast<uint32_t>(storage.float32) ==
               base::bit_cast<uint32_t>(other.storage.float32);
      case Kind::kFloat64:
      case Kind::kNumber:
        // Using a bit_cast to uint64_t in order to return false when comparing
        // +0 and -0.
        // Note: for JavaScript, it would be fine to return true when both
        // values are NaNs, but for Wasm we must not merge NaNs that way.
        // Since we canonicalize NaNs for JS anyway, we don't need to treat
        // them specially here.
        return base::bit_cast<uint64_t>(storage.float64) ==
               base::bit_cast<uint64_t>(other.storage.float64);
      case Kind::kExternal:
        return storage.external.address() == other.storage.external.address();
      case Kind::kHeapObject:
      case Kind::kCompressedHeapObject:
        return storage.handle.address() == other.storage.handle.address();
    }
  }
};

// Load `loaded_rep` from: base + offset + index * 2^element_size_log2.
// For Kind::tagged_base: subtract kHeapObjectTag,
//                        `base` has to be the object start.
// For (u)int8/16, the value will be sign- or zero-extended to Word32.
// When result_rep is RegisterRepresentation::Compressed(), then the load does
// not decompress the value.
struct LoadOp : OperationT<LoadOp> {
  struct Kind {
    // The `base` input is a tagged pointer to a HeapObject.
    bool tagged_base : 1;
    // The effective address might be unaligned. This is only set to true if
    // the platform does not support unaligned loads for the given
    // MemoryRepresentation natively.
    bool maybe_unaligned : 1;
    // There is a Wasm trap handler for out-of-bounds accesses.
    bool with_trap_handler : 1;
    // The wasm trap handler is used for null accesses. Note that this requires
    // with_trap_handler as well.
    bool trap_on_null : 1;
    // If {load_eliminable} is true, then:
    //   - Stores/Loads at this address cannot overlap. Concretely, it means
    //     that something like this cannot happen:
    //
    //         const u32s = Uint32Array.of(3, 8);
    //         const u8s = new Uint8Array(u32s.buffer);
    //         u32s[0] = 0xffffffff;
    //         u8s[1] = 0; // Overlaps with previous store!
    //
    //   - Stores/Loads at this address have a canonical base. Concretely, it
    //     means that something like this cannot happen:
    //
    //         let buffer = new ArrayBuffer(10000);
    //         let ta1 = new Int32Array(buffer, 0/*offset*/);
    //         let ta2 = new Int32Array(buffer, 100*4/*offset*/);
    //         ta2[0] = 0xff;
    //         ta1[100] = 42; // Same destination as the previous store!
    //
    //   - No other thread can modify the underlying value. E.g. in the case of
    //     loading the wasm stack limit, other threads can modify the loaded
    //     value, so we always have to reload it.
    //
    // This is mainly used for load elimination: when stores/loads don't have
    // the {load_eliminable} bit set to true, more things need to be
    // invalidated.
    // In the main JS pipeline, only ArrayBuffers (= TypedArray/DataView)
    // loads/stores have this {load_eliminable} set to false,
    // and all other loads have it to true.
    bool load_eliminable : 1;
    // The loaded value may not change.
    bool is_immutable : 1;
    // The load should be atomic.
    bool is_atomic : 1;

    static constexpr Kind Aligned(BaseTaggedness base_is_tagged) {
      switch (base_is_tagged) {
        case BaseTaggedness::kTaggedBase:
          return TaggedBase();
        case BaseTaggedness::kUntaggedBase:
          return RawAligned();
      }
    }

    // TODO(dmercadier): use designed initializers once we move to C++20.
    static constexpr Kind TaggedBase() {
      return {true, false, false, false, true, false, false};
    }
    static constexpr Kind RawAligned() {
      return {false, false, false, false, true, false, false};
    }
    static constexpr Kind RawUnaligned() {
      return {false, true, false, false, true, false, false};
    }
    static constexpr Kind Protected() {
      return {false, false, true, false, true, false, false};
    }
    static constexpr Kind TrapOnNull() {
      return {true, false, true, true, true, false, false};
    }
    static constexpr Kind MaybeUnaligned(MemoryRepresentation rep) {
      return rep == MemoryRepresentation::Int8() ||
                     rep == MemoryRepresentation::Uint8() ||
                     SupportedOperations::IsUnalignedLoadSupported(rep)
                 ? LoadOp::Kind::RawAligned()
                 : LoadOp::Kind::RawUnaligned();
    }

    constexpr Kind NotLoadEliminable() {
      Kind kind = *this;
      kind.load_eliminable = false;
      return kind;
    }

    constexpr Kind Immutable() const {
      Kind kind(*this);
      kind.is_immutable = true;
      return kind;
    }

    constexpr Kind Atomic() const {
      Kind kind(*this);
      kind.is_atomic = true;
      return kind;
    }

    bool operator==(const Kind& other) const {
      return tagged_base == other.tagged_base &&
             maybe_unaligned == other.maybe_unaligned &&
             with_trap_handler == other.with_trap_handler &&
             load_eliminable == other.load_eliminable &&
             is_immutable == other.is_immutable &&
             is_atomic == other.is_atomic && trap_on_null == other.trap_on_null;
    }
  };
  Kind kind;
  MemoryRepresentation loaded_rep;
  RegisterRepresentation result_rep;
  uint8_t element_size_log2;  // multiply index with 2^element_size_log2
  int32_t offset;             // add offset to scaled index

  OpEffects Effects() const {
    // Loads might depend on checks for pointer validity, object layout, bounds
    // checks, etc.
    // TODO(tebbi): Distinguish between on-heap and off-heap loads.
    OpEffects effects = OpEffects().CanReadMemory().CanDependOnChecks();
    if (kind.with_trap_handler) effects = effects.CanLeaveCurrentFunction();
    if (kind.is_atomic) {
      // Atomic load should not be reordered with other loads.
      effects = effects.CanWriteMemory();
    }
    return effects;
  }
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(&result_rep, 1);
  }

  MachineType machine_type() const;

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    base::Vector<const MaybeRegisterRepresentation> result =
        kind.tagged_base
            ? MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                             MaybeRegisterRepresentation::WordPtr()>()
            : MaybeRepVector<MaybeRegisterRepresentation::WordPtr(),
                             MaybeRegisterRepresentation::WordPtr()>();
    return index().valid() ? result : base::VectorOf(result.data(), 1);
  }

  OpIndex base() const { return input(0); }
  OptionalOpIndex index() const {
    return input_count == 2 ? input(1) : OpIndex::Invalid();
  }

  static constexpr bool OffsetIsValid(int32_t offset, bool tagged_base) {
    if (tagged_base) {
      // When a Load has the tagged_base Kind, it means that {offset} will
      // eventually need a "-kHeapObjectTag". If the {offset} is
      // min_int, then subtracting kHeapObjectTag will underflow.
      return offset >= std::numeric_limits<int32_t>::min() + kHeapObjectTag;
    }
    return true;
  }

  LoadOp(OpIndex base, OptionalOpIndex index, Kind kind,
         MemoryRepresentation loaded_rep, RegisterRepresentation result_rep,
         int32_t offset, uint8_t element_size_log2)
      : Base(1 + index.valid()),
        kind(kind),
        loaded_rep(loaded_rep),
        result_rep(result_rep),
        element_size_log2(element_size_log2),
        offset(offset) {
    input(0) = base;
    if (index.valid()) {
      input(1) = index.value();
    }
  }

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    return fn(mapper.Map(base()), mapper.Map(index()), kind, loaded_rep,
              result_rep, offset, element_size_log2);
  }

  void Validate(const Graph& graph) const {
    DCHECK(loaded_rep.ToRegisterRepresentation() == result_rep ||
           (loaded_rep.IsTagged() &&
            result_rep == RegisterRepresentation::Compressed()) ||
           kind.is_atomic);
    DCHECK_IMPLIES(element_size_log2 > 0, index().valid());
    DCHECK_IMPLIES(kind.maybe_unaligned,
                   !SupportedOperations::IsUnalignedLoadSupported(loaded_rep));
    DCHECK(OffsetIsValid(offset, kind.tagged_base));
  }
  static LoadOp& New(Graph* graph, OpIndex base, OptionalOpIndex index,
                     Kind kind, MemoryRepresentation loaded_rep,
                     RegisterRepresentation result_rep, int32_t offset,
                     uint8_t element_size_log2) {
    return Base::New(graph, 1 + index.valid(), base, index, kind, loaded_rep,
                     result_rep, offset, element_size_log2);
  }
  void PrintInputs(std::ostream& os, const std::string& op_index_prefix) const;
  void PrintOptions(std::ostream& os) const;
  auto options() const {
    return std::tuple{kind, loaded_rep, result_rep, offset, element_size_log2};
  }
};

V8_INLINE size_t hash_value(LoadOp::Kind kind) {
  return base::hash_value(
      static_cast<int>(kind.tagged_base) | (kind.maybe_unaligned << 1) |
      (kind.load_eliminable << 2) | (kind.is_immutable << 3) |
      (kind.with_trap_handler << 4) | (kind.is_atomic << 5));
}

struct AtomicRMWOp : OperationT<AtomicRMWOp> {
  enum class BinOp : uint8_t {
    kAdd,
    kSub,
    kAnd,
    kOr,
    kXor,
    kExchange,
    kCompareExchange
  };
  BinOp bin_op;
  RegisterRepresentation result_rep;
  MemoryRepresentation input_rep;
  MemoryAccessKind memory_access_kind;
  OpEffects Effects() const {
    OpEffects effects =
        OpEffects().CanWriteMemory().CanDependOnChecks().CanReadMemory();
    if (memory_access_kind == MemoryAccessKind::kProtected) {
      effects = effects.CanLeaveCurrentFunction();
    }
    return effects;
  }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(&result_rep, 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    if (bin_op == BinOp::kCompareExchange) {
      return InitVectorOf(storage, {RegisterRepresentation::WordPtr(),
                                    RegisterRepresentation::WordPtr(),
                                    input_rep.ToRegisterRepresentation(),
                                    input_rep.ToRegisterRepresentation()});
    }
    return InitVectorOf(storage, {RegisterRepresentation::WordPtr(),
                                  RegisterRepresentation::WordPtr(),
                                  input_rep.ToRegisterRepresentation()});
  }

  V<WordPtr> base() const { return input(0); }
  V<WordPtr> index() const { return input(1); }
  OpIndex value() const { return input(2); }
  OptionalOpIndex expected() const {
    return (input_count == 4) ? input(3) : OpIndex::Invalid();
  }

  void Validate(const Graph& graph) const {
    DCHECK_EQ(bin_op == BinOp::kCompareExchange, expected().valid());
  }

  AtomicRMWOp(OpIndex base, OpIndex index, OpIndex value,
              OptionalOpIndex expected, BinOp bin_op,
              RegisterRepresentation result_rep, MemoryRepresentation input_rep,
              MemoryAccessKind kind)
      : Base(3 + expected.valid()),
        bin_op(bin_op),
        result_rep(result_rep),
        input_rep(input_rep),
        memory_access_kind(kind) {
    input(0) = base;
    input(1) = index;
    input(2) = value;
    if (expected.valid()) {
      input(3) = expected.value();
    }
  }

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    return fn(mapper.Map(base()), mapper.Map(index()), mapper.Map(value()),
              mapper.Map(expected()), bin_op, result_rep, input_rep,
              memory_access_kind);
  }

  static AtomicRMWOp& New(Graph* graph, OpIndex base, OpIndex index,
                          OpIndex value, OptionalOpIndex expected, BinOp bin_op,
                          RegisterRepresentation result_rep,
                          MemoryRepresentation input_rep,
                          MemoryAccessKind kind) {
    return Base::New(graph, 3 + expected.valid(), base, index, value, expected,
                     bin_op, result_rep, input_rep, kind);
  }

  void PrintInputs(std::ostream& os, const std::string& op_index_prefix) const;

  void PrintOptions(std::ostream& os) const;

  auto options() const {
    return std::tuple{bin_op, result_rep, input_rep, memory_access_kind};
  }
};
DEFINE_MULTI_SWITCH_INTEGRAL(AtomicRMWOp::BinOp, 8)

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           AtomicRMWOp::BinOp kind);

struct AtomicWord32PairOp : OperationT<AtomicWord32PairOp> {
  enum class Kind : uint8_t {
    kAdd,
    kSub,
    kAnd,
    kOr,
    kXor,
    kExchange,
    kCompareExchange,
    kLoad,
    kStore
  };

  Kind kind;
  int32_t offset;

  static Kind KindFromBinOp(AtomicRMWOp::BinOp bin_op) {
    switch (bin_op) {
      case AtomicRMWOp::BinOp::kAdd:
        return Kind::kAdd;
      case AtomicRMWOp::BinOp::kSub:
        return Kind::kSub;
      case AtomicRMWOp::BinOp::kAnd:
        return Kind::kAnd;
      case AtomicRMWOp::BinOp::kOr:
        return Kind::kOr;
      case AtomicRMWOp::BinOp::kXor:
        return Kind::kXor;
      case AtomicRMWOp::BinOp::kExchange:
        return Kind::kExchange;
      case AtomicRMWOp::BinOp::kCompareExchange:
        return Kind::kCompareExchange;
    }
  }

  OpEffects Effects() const {
    OpEffects effects = OpEffects().CanDependOnChecks();
    if (kind == Kind::kStore) {
      return effects.CanWriteMemory();
    }
    // Atomic loads are marked as "can write memory" as they should not be
    // reordered with other loads. Secondly, they may not be removed even if
    // unused as they might make writes of other threads visible.
    return effects.CanReadMemory().CanWriteMemory();
  }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    if (kind == Kind::kStore) return {};
    return RepVector<RegisterRepresentation::Word32(),
                     RegisterRepresentation::Word32()>();
  }
  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    storage.resize(input_count);

    const bool has_index = HasIndex();
    storage[0] = RegisterRepresentation::WordPtr();  // base
    if (has_index) {
      storage[1] = RegisterRepresentation::WordPtr();  // index
    }
    if (kind != Kind::kLoad) {
      storage[1 + has_index] = RegisterRepresentation::Word32();  // value_low
      storage[2 + has_index] = RegisterRepresentation::Word32();  // value_high
      if (kind == Kind::kCompareExchange) {
        storage[3 + has_index] =
            RegisterRepresentation::Word32();  // expected_low
        storage[4 + has_index] =
            RegisterRepresentation::Word32();  // expected_high
      }
    }
    return base::VectorOf(storage);
  }

  V<WordPtr> base() const { return input(0); }
  OptionalV<WordPtr> index() const {
    return HasIndex() ? input(1) : OpIndex::Invalid();
  }
  OptionalV<Word32> value_low() const {
    return kind != Kind::kLoad ? input(1 + HasIndex()) : OpIndex::Invalid();
  }
  OptionalV<Word32> value_high() const {
    return kind != Kind::kLoad ? input(2 + HasIndex()) : OpIndex::Invalid();
  }
  OptionalV<Word32> expected_low() const {
    return kind == Kind::kCompareExchange ? input(3 + HasIndex())
                                          : OpIndex::Invalid();
  }
  OptionalV<Word32> expected_high() const {
    return kind == Kind::kCompareExchange ? input(4 + HasIndex())
                                          : OpIndex::Invalid();
  }

  void Validate(const Graph& graph) const {}

  AtomicWord32PairOp(V<WordPtr> base, OptionalV<WordPtr> index,
                     OptionalV<Word32> value_low, OptionalV<Word32> value_high,
                     OptionalV<Word32> expected_low,
                     OptionalV<Word32> expected_high, Kind kind, int32_t offset)
      : Base(InputCount(kind, index.has_value())), kind(kind), offset(offset) {
    DCHECK_EQ(value_low.valid(), value_high.valid());
    DCHECK_EQ(expected_low.valid(), expected_high.valid());
    DCHECK_EQ(kind == Kind::kCompareExchange, expected_low.valid());
    DCHECK_EQ(kind != Kind::kLoad, value_low.valid());

    const bool has_index = index.has_value();
    DCHECK_EQ(has_index, HasIndex());

    input(0) = base;
    if (has_index) input(1) = index.value();
    if (kind != Kind::kLoad) {
      input(1 + has_index) = value_low.value();
      input(2 + has_index) = value_high.value();
      if (kind == Kind::kCompareExchange) {
        input(3 + has_index) = expected_low.value();
        input(4 + has_index) = expected_high.value();
      }
    }
  }

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    return fn(mapper.Map(base()), mapper.Map(index()), mapper.Map(value_low()),
              mapper.Map(value_high()), mapper.Map(expected_low()),
              mapper.Map(expected_high()), kind, offset);
  }

  static constexpr size_t InputCount(Kind kind, bool has_index) {
    switch (kind) {
      case Kind::kLoad:
        return 1 + has_index;  // base, index?
      case Kind::kAdd:
      case Kind::kSub:
      case Kind::kAnd:
      case Kind::kOr:
      case Kind::kXor:
      case Kind::kExchange:
      case Kind::kStore:
        return 3 + has_index;  // base, index?, value_low, value_high
      case Kind::kCompareExchange:
        return 5 + has_index;  // base, index?, value_low, value_high,
                               // expected_low, expected_high
    }
  }
  bool HasIndex() const { return input_count == InputCount(kind, true); }

  static AtomicWord32PairOp& New(Graph* graph, V<WordPtr> base,
                                 OptionalV<WordPtr> index,
                                 OptionalV<Word32> value_low,
                                 OptionalV<Word32> value_high,
                                 OptionalV<Word32> expected_low,
                                 OptionalV<Word32> expected_high, Kind kind,
                                 int32_t offset) {
    return Base::New(graph, InputCount(kind, index.has_value()), base, index,
                     value_low, value_high, expected_low, expected_high, kind,
                     offset);
  }

  void PrintInputs(std::ostream& os, const std::string& op_index_prefix) const;

  void PrintOptions(std::ostream& os) const;

  auto options() const { return std::tuple{kind, offset}; }
};

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           AtomicWord32PairOp::Kind kind);

struct MemoryBarrierOp : FixedArityOperationT<0, MemoryBarrierOp> {
  AtomicMemoryOrder memory_order;

  static constexpr OpEffects effects =
      OpEffects().CanReadHeapMemory().CanWriteMemory();

  explicit MemoryBarrierOp(AtomicMemoryOrder memory_order)
      : Base(), memory_order(memory_order) {}

  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{memory_order}; }
  void PrintOptions(std::ostream& os) const;
};

// Store `value` to: base + offset + index * 2^element_size_log2.
// For Kind::tagged_base: subtract kHeapObjectTag,
//                        `base` has to be the object start.
struct StoreOp : OperationT<StoreOp> {
  using Kind = LoadOp::Kind;
  Kind kind;
  MemoryRepresentation stored_rep;
  WriteBarrierKind write_barrier;
  uint8_t element_size_log2;  // multiply index with 2^element_size_log2
  int32_t offset;             // add offset to scaled index
  bool maybe_initializing_or_transitioning;
  uint16_t
      shifted_indirect_pointer_tag;  // for indirect pointer stores, the
                                     // IndirectPointerTag of the store shifted
                                     // to the right by kIndirectPointerTagShift
                                     // (so it fits into 16 bits).
  // TODO(saelo): now that we have a pointer tag in these low-level operations,
  // we could also consider passing the external pointer tag (for external
  // pointers) through to the macro assembler (where we have routines to work
  // with external pointers) instead of handling those earlier in the compiler.
  // We might lose the ability to hardcode the table address though.

  OpEffects Effects() const {
    // Stores might depend on checks for pointer validity, object layout, bounds
    // checks, etc.
    // TODO(tebbi): Distinghish between on-heap and off-heap stores.
    OpEffects effects = OpEffects().CanWriteMemory().CanDependOnChecks();
    if (kind.with_trap_handler) effects = effects.CanLeaveCurrentFunction();
    if (maybe_initializing_or_transitioning) {
      effects = effects.CanDoRawHeapAccess();
    }
    if (kind.is_atomic) {
      // Atomic stores should not be eliminated away, even if the situation
      // seems to allow e.g. store-store elimination. Elimination is avoided by
      // setting the `CanReadMemory` effect.
      effects = effects.CanReadMemory();
    }
    return effects;
  }
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    RegisterRepresentation base = kind.tagged_base
                                      ? RegisterRepresentation::Tagged()
                                      : RegisterRepresentation::WordPtr();
    if (index() == OpIndex::Invalid()) {
      return InitVectorOf(
          storage, {base, stored_rep.ToRegisterRepresentationForStore()});
    }
    return InitVectorOf(storage,
                        {base, stored_rep.ToRegisterRepresentationForStore(),
                         RegisterRepresentation::WordPtr()});
  }

  OpIndex base() const { return input(0); }
  OpIndex value() const { return input(1); }
  OptionalOpIndex index() const {
    return input_count == 3 ? input(2) : OpIndex::Invalid();
  }

  IndirectPointerTag indirect_pointer_tag() const {
    uint64_t shifted = shifted_indirect_pointer_tag;
    return static_cast<IndirectPointerTag>(shifted << kIndirectPointerTagShift);
  }

  StoreOp(
      OpIndex base, OptionalOpIndex index, OpIndex value, Kind kind,
      MemoryRepresentation stored_rep, WriteBarrierKind write_barrier,
      int32_t offset, uint8_t element_size_log2,
      bool maybe_initializing_or_transitioning,
      IndirectPointerTag maybe_indirect_pointer_tag = kIndirectPointerNullTag)
      : Base(2 + index.valid()),
        kind(kind),
        stored_rep(stored_rep),
        write_barrier(write_barrier),
        element_size_log2(element_size_log2),
        offset(offset),
        maybe_initializing_or_transitioning(
            maybe_initializing_or_transitioning),
        shifted_indirect_pointer_tag(maybe_indirect_pointer_tag >>
                                     kIndirectPointerTagShift) {
    DCHECK_EQ(indirect_pointer_tag(), maybe_indirect_pointer_tag);
    input(0) = base;
    input(1) = value;
    if (index.valid()) {
      input(2) = index.value();
    }
  }

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    return fn(mapper.Map(base()), mapper.Map(index()), mapper.Map(value()),
              kind, stored_rep, write_barrier, offset, element_size_log2,
              maybe_initializing_or_transitioning, indirect_pointer_tag());
  }

  void Validate(const Graph& graph) const {
    DCHECK_IMPLIES(element_size_log2 > 0, index().valid());
    DCHECK_IMPLIES(kind.maybe_unaligned,
                   !SupportedOperations::IsUnalignedLoadSupported(stored_rep));
    DCHECK(LoadOp::OffsetIsValid(offset, kind.tagged_base));
  }
  static StoreOp& New(
      Graph* graph, OpIndex base, OptionalOpIndex index, OpIndex value,
      Kind kind, MemoryRepresentation stored_rep,
      WriteBarrierKind write_barrier, int32_t offset, uint8_t element_size_log2,
      bool maybe_initializing_or_transitioning,
      IndirectPointerTag maybe_indirect_pointer_tag = kIndirectPointerNullTag) {
    return Base::New(graph, 2 + index.valid(), base, index, value, kind,
                     stored_rep, write_barrier, offset, element_size_log2,
                     maybe_initializing_or_transitioning,
                     maybe_indirect_pointer_tag);
  }

  void PrintInputs(std::ostream& os, const std::string& op_index_prefix) const;
  void PrintOptions(std::ostream& os) const;
  auto options() const {
    return std::tuple{
        kind,   stored_rep,        write_barrier,
        offset, element_size_log2, maybe_initializing_or_transitioning};
  }
};

struct AllocateOp : FixedArityOperationT<1, AllocateOp> {
  AllocationType type;

  static constexpr OpEffects effects =
      OpEffects()
          .CanAllocate()
          // The resulting object is unitialized, which leaves the heap in an
          // inconsistent state.
          .CanDoRawHeapAccess()
          // Do not move allocations before checks, to avoid OOM or invalid
          // size.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::WordPtr()>();
  }

  OpIndex size() const { return input(0); }

  AllocateOp(OpIndex size, AllocationType type) : Base(size), type(type) {}

  void Validate(const Graph& graph) const {}
  void PrintOptions(std::ostream& os) const;

  auto options() const { return std::tuple{type}; }

  // template <typename H>
  // friend H AbslHashValue(H h, const AllocateOp& op) {
  //   return H::combine(std::move(h), op.size(), op.type);
  // }
};

struct DecodeExternalPointerOp
    : FixedArityOperationT<1, DecodeExternalPointerOp> {
  ExternalPointerTag tag;

  // Accessing external pointers is only safe if the garbage collected pointer
  // keeping the external pointer alive is retained for the length of the
  // operation. For this, it is essential that we use a `Retain` operation
  // placed after the last access to the external data.
  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::WordPtr()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Word32()>();
  }

  OpIndex handle() const { return input(0); }

  DecodeExternalPointerOp(OpIndex handle, ExternalPointerTag tag)
      : Base(handle), tag(tag) {}

  void Validate(const Graph& graph) const {
    DCHECK_NE(tag, kExternalPointerNullTag);
  }
  void PrintOptions(std::ostream& os) const;
  auto options() const { return std::tuple{tag}; }
};

struct StackCheckOp : FixedArityOperationT<0, StackCheckOp> {
  enum class CheckOrigin : bool { kFromJS, kFromWasm };

  enum class CheckKind : bool { kFunctionHeaderCheck, kLoopCheck };

  CheckOrigin check_origin;
  CheckKind check_kind;

  static constexpr OpEffects effects = OpEffects().CanCallAnything();

  explicit StackCheckOp(CheckOrigin origin, CheckKind kind)
      : Base(), check_origin(origin), check_kind(kind) {}

  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{check_origin, check_kind}; }
};

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           StackCheckOp::CheckOrigin origin);
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           StackCheckOp::CheckKind kind);

// Retain a HeapObject to prevent it from being garbage collected too early.
struct RetainOp : FixedArityOperationT<1, RetainOp> {
  OpIndex retained() const { return input(0); }

  // Retain doesn't actually write, it just keeps a value alive. However, since
  // this must not be reordered with reading operations, we mark it as writing.
  static constexpr OpEffects effects = OpEffects().CanWriteMemory();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  explicit RetainOp(OpIndex retained) : Base(retained) {}

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{}; }
};

// We compare the stack pointer register with the given limit and a
// codegen-dependant adjustment.
struct StackPointerGreaterThanOp
    : FixedArityOperationT<1, StackPointerGreaterThanOp> {
  StackCheckKind kind;

  // Since the frame size of optimized functions is constant, this behaves like
  // a pure operation.
  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::WordPtr()>();
  }

  OpIndex stack_limit() const { return input(0); }

  StackPointerGreaterThanOp(OpIndex stack_limit, StackCheckKind kind)
      : Base(stack_limit), kind(kind) {}

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{kind}; }
};

// Allocate a piece of memory in the current stack frame. Every operation
// in the IR is a separate stack slot, but repeated execution in a loop
// produces the same stack slot.
struct StackSlotOp : FixedArityOperationT<0, StackSlotOp> {
  int size;
  int alignment;
  bool is_tagged;

  // We can freely reorder stack slot operations, but must not de-duplicate
  // them.
  static constexpr OpEffects effects = OpEffects().CanCreateIdentity();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::WordPtr()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  StackSlotOp(int size, int alignment, bool is_tagged = false)
      : size(size), alignment(alignment), is_tagged(is_tagged) {}
  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{size, alignment, is_tagged}; }
};

// Values that are constant for the current stack frame/invocation.
// Therefore, they behaves like a constant, even though they are different for
// every invocation.
struct FrameConstantOp : FixedArityOperationT<0, FrameConstantOp> {
  enum class Kind : uint8_t {
    kStackCheckOffset,
    kFramePointer,
    kParentFramePointer
  };
  Kind kind;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    switch (kind) {
      case Kind::kStackCheckOffset:
        return RepVector<RegisterRepresentation::Tagged()>();
      case Kind::kFramePointer:
      case Kind::kParentFramePointer:
        return RepVector<RegisterRepresentation::WordPtr()>();
    }
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  explicit FrameConstantOp(Kind kind) : Base(), kind(kind) {}
  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           FrameConstantOp::Kind kind);

struct FrameStateOp : OperationT<FrameStateOp> {
  bool inlined;
  const FrameStateData* data;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  OpIndex parent_frame_state() const {
    DCHECK(inlined);
    return input(0);
  }
  base::Vector<const OpIndex> state_values() const {
    base::Vector<const OpIndex> result = inputs();
    if (inlined) result += 1;
    return result;
  }
  uint16_t state_values_count() const {
    DCHECK_EQ(input_count - inlined, state_values().size());
    return input_count - inlined;
  }
  const OpIndex state_value(size_t idx) const { return state_values()[idx]; }

  RegisterRepresentation state_value_rep(size_t idx) const {
    return RegisterRepresentation::FromMachineRepresentation(
        data->machine_types[idx].representation());
  }

  FrameStateOp(base::Vector<const OpIndex> inputs, bool inlined,
               const FrameStateData* data)
      : Base(inputs), inlined(inlined), data(data) {}

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    auto mapped_inputs = mapper.template Map<32>(inputs());
    return fn(base::VectorOf(mapped_inputs), inlined, data);
  }

  V8_EXPORT_PRIVATE void Validate(const Graph& graph) const;
  V8_EXPORT_PRIVATE void PrintOptions(std::ostream& os) const;
  auto options() const { return std::tuple{inlined, data}; }
};

struct DeoptimizeOp : FixedArityOperationT<1, DeoptimizeOp> {
  const DeoptimizeParameters* parameters;

  static constexpr OpEffects effects = OpEffects().CanDeopt();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  OpIndex frame_state() const { return input(0); }

  DeoptimizeOp(OpIndex frame_state, const DeoptimizeParameters* parameters)
      : Base(frame_state), parameters(parameters) {}
  void Validate(const Graph& graph) const {
    DCHECK(Get(graph, frame_state()).Is<FrameStateOp>());
  }
  auto options() const { return std::tuple{parameters}; }
};

struct DeoptimizeIfOp : FixedArityOperationT<2, DeoptimizeIfOp> {
  bool negated;
  const DeoptimizeParameters* parameters;

  static constexpr OpEffects effects = OpEffects().CanDeopt();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Word32()>();
  }

  OpIndex condition() const { return input(0); }
  OpIndex frame_state() const { return input(1); }

  DeoptimizeIfOp(OpIndex condition, OpIndex frame_state, bool negated,
                 const DeoptimizeParameters* parameters)
      : Base(condition, frame_state),
        negated(negated),
        parameters(parameters) {}

  bool EqualsForGVN(const DeoptimizeIfOp& other) const {
    // As far as GVN is concerned, the `frame_state` and `parameters` don't
    // matter: 2 DeoptimizeIf can be GVNed if they have the same `condition` and
    // same `negated`, regardless of their `frame_state` and `parameters`.
    return condition() == other.condition() && negated == other.negated;
  }
  size_t hash_value() const {
    // To enable GVNing as described above in `EqualsForGVN`, `hash_value` has
    // to ignore the `frame_state` and the `parameters`.
    return fast_hash_combine(Opcode::kDeoptimizeIf, condition(), negated);
  }
  void Validate(const Graph& graph) const {
    DCHECK(Get(graph, frame_state()).Is<FrameStateOp>());
  }
  auto options() const { return std::tuple{negated, parameters}; }
};

#if V8_ENABLE_WEBASSEMBLY
struct TrapIfOp : OperationT<TrapIfOp> {
  bool negated;
  const TrapId trap_id;

  static constexpr OpEffects effects =
      OpEffects()
          // Traps must not float above a protective check.
          .CanDependOnChecks()
          // Subsequent code can rely on the trap not having happened.
          .CanLeaveCurrentFunction();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Word32()>();
  }

  OpIndex condition() const { return input(0); }
  OptionalOpIndex frame_state() const {
    return input_count > 1 ? input(1) : OpIndex::Invalid();
  }

  TrapIfOp(OpIndex condition, OptionalOpIndex frame_state, bool negated,
           const TrapId trap_id)
      : Base(1 + frame_state.valid()), negated(negated), trap_id(trap_id) {
    input(0) = condition;
    if (frame_state.valid()) {
      input(1) = frame_state.value();
    }
  }

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    return fn(mapper.Map(condition()), mapper.Map(frame_state()), negated,
              trap_id);
  }

  static TrapIfOp& New(Graph* graph, OpIndex condition,
                       OptionalOpIndex frame_state, bool negated,
                       const TrapId trap_id) {
    return Base::New(graph, 1 + frame_state.valid(), condition, frame_state,
                     negated, trap_id);
  }

  void Validate(const Graph& graph) const {
    if (frame_state().valid()) {
      DCHECK(Get(graph, frame_state().value()).Is<FrameStateOp>());
    }
  }
  auto options() const { return std::tuple{negated, trap_id}; }
};
#endif  // V8_ENABLE_WEBASSEMBLY

struct StaticAssertOp : FixedArityOperationT<1, StaticAssertOp> {
  const char* source;
  static constexpr OpEffects effects =
      OpEffects().CanDependOnChecks().RequiredWhenUnused();

  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Word32()>();
  }

  OpIndex condition() const { return Base::input(0); }

  StaticAssertOp(OpIndex condition, const char* source)
      : Base(condition), source(source) {}

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{source}; }
};

struct ParameterOp : FixedArityOperationT<0, ParameterOp> {
  int32_t parameter_index;
  RegisterRepresentation rep;
  const char* debug_name;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return {&rep, 1};
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};  // On the callee side a parameter doesn't have an input.
  }

  explicit ParameterOp(int32_t parameter_index, RegisterRepresentation rep,
                       const char* debug_name = "")
      : Base(),
        parameter_index(parameter_index),
        rep(rep),
        debug_name(debug_name) {}
  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{parameter_index, rep, debug_name}; }
  void PrintOptions(std::ostream& os) const;
};

struct OsrValueOp : FixedArityOperationT<0, OsrValueOp> {
  int32_t index;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  explicit OsrValueOp(int32_t index) : Base(), index(index) {}
  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{index}; }
};

struct TSCallDescriptor : public NON_EXPORTED_BASE(ZoneObject) {
  const CallDescriptor* descriptor;
  base::Vector<const RegisterRepresentation> in_reps;
  base::Vector<const RegisterRepresentation> out_reps;
  CanThrow can_throw;

  TSCallDescriptor(const CallDescriptor* descriptor,
                   base::Vector<const RegisterRepresentation> in_reps,
                   base::Vector<const RegisterRepresentation> out_reps,
                   CanThrow can_throw)
      : descriptor(descriptor),
        in_reps(in_reps),
        out_reps(out_reps),
        can_throw(can_throw) {}

  static const TSCallDescriptor* Create(const CallDescriptor* descriptor,
                                        CanThrow can_throw, Zone* graph_zone) {
    base::Vector<RegisterRepresentation> in_reps =
        graph_zone->AllocateVector<RegisterRepresentation>(
            descriptor->ParameterCount());
    for (size_t i = 0; i < descriptor->ParameterCount(); ++i) {
      in_reps[i] = RegisterRepresentation::FromMachineRepresentation(
          descriptor->GetParameterType(i).representation());
    }
    base::Vector<RegisterRepresentation> out_reps =
        graph_zone->AllocateVector<RegisterRepresentation>(
            descriptor->ReturnCount());
    for (size_t i = 0; i < descriptor->ReturnCount(); ++i) {
      out_reps[i] = RegisterRepresentation::FromMachineRepresentation(
          descriptor->GetReturnType(i).representation());
    }
    return graph_zone->New<TSCallDescriptor>(descriptor, in_reps, out_reps,
                                             can_throw);
  }
};

// If {target} is a HeapObject representing a builtin, return that builtin's ID.
base::Optional<Builtin> TryGetBuiltinId(const ConstantOp* target,
                                        JSHeapBroker* broker);

struct CallOp : OperationT<CallOp> {
  const TSCallDescriptor* descriptor;
  OpEffects callee_effects;

  OpEffects Effects() const { return callee_effects; }

  // The outputs are produced by the `DidntThrow` operation.
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }
  base::Vector<const RegisterRepresentation> results_rep() const {
    return descriptor->out_reps;
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    storage.resize(input_count);
    size_t i = 0;
    if (descriptor->descriptor->IsCodeObjectCall() ||
        descriptor->descriptor->IsJSFunctionCall() ||
        descriptor->descriptor->IsBuiltinPointerCall()) {
      storage[i++] = MaybeRegisterRepresentation::Tagged();
    } else {
      storage[i++] = MaybeRegisterRepresentation::WordPtr();
    }
    if (HasFrameState()) {
      storage[i++] = MaybeRegisterRepresentation::None();
    }
    for (auto rep : descriptor->in_reps) {
      // In JavaScript, parameters are optional.
      if (i >= input_count) break;
      storage[i++] = rep;
    }
    storage.resize(i);
    return base::VectorOf(storage);
  }

  bool HasFrameState() const {
    return descriptor->descriptor->NeedsFrameState();
  }

  OpIndex callee() const { return input(0); }
  OptionalOpIndex frame_state() const {
    return HasFrameState() ? input(1) : OpIndex::Invalid();
  }
  base::Vector<const OpIndex> arguments() const {
    return inputs().SubVector(1 + HasFrameState(), input_count);
  }
  // Returns true if this call is a JS (but not wasm) stack check.
  V8_EXPORT_PRIVATE bool IsStackCheck(const Graph& graph, JSHeapBroker* broker,
                                      StackCheckKind kind) const;

  CallOp(OpIndex callee, OptionalOpIndex frame_state,
         base::Vector<const OpIndex> arguments,
         const TSCallDescriptor* descriptor, OpEffects effects)
      : Base(1 + frame_state.valid() + arguments.size()),
        descriptor(descriptor),
        callee_effects(effects) {
    base::Vector<OpIndex> inputs = this->inputs();
    inputs[0] = callee;
    if (frame_state.valid()) {
      inputs[1] = frame_state.value();
    }
    inputs.SubVector(1 + frame_state.valid(), inputs.size())
        .OverwriteWith(arguments);
  }

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    OpIndex mapped_callee = mapper.Map(callee());
    OptionalOpIndex mapped_frame_state = mapper.Map(frame_state());
    auto mapped_arguments = mapper.template Map<16>(arguments());
    return fn(mapped_callee, mapped_frame_state,
              base::VectorOf(mapped_arguments), descriptor, Effects());
  }

  void Validate(const Graph& graph) const {
    if (frame_state().valid()) {
      DCHECK(Get(graph, frame_state().value()).Is<FrameStateOp>());
    }
  }

  static CallOp& New(Graph* graph, OpIndex callee, OptionalOpIndex frame_state,
                     base::Vector<const OpIndex> arguments,
                     const TSCallDescriptor* descriptor, OpEffects effects) {
    return Base::New(graph, 1 + frame_state.valid() + arguments.size(), callee,
                     frame_state, arguments, descriptor, effects);
  }
  // TODO(mliedtke): Should the hash function be overwritten, so that calls (and
  // potentially tail calls) can participate in GVN? Right now this is prevented
  // by every call descriptor being a different pointer.
  auto options() const { return std::tuple{descriptor}; }
  void PrintOptions(std::ostream& os) const;
};

// Catch an exception from the first operation of the `successor` block and
// continue execution in `catch_block` in this case.
struct CheckExceptionOp : FixedArityOperationT<1, CheckExceptionOp> {
  Block* didnt_throw_block;
  Block* catch_block;

  static constexpr OpEffects effects = OpEffects().CanCallAnything();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  OpIndex throwing_operation() const { return input(0); }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  CheckExceptionOp(OpIndex throwing_operation, Block* successor,
                   Block* catch_block)
      : Base(throwing_operation),
        didnt_throw_block(successor),
        catch_block(catch_block) {}

  V8_EXPORT_PRIVATE void Validate(const Graph& graph) const;

  auto options() const { return std::tuple{didnt_throw_block, catch_block}; }
};

// This is a pseudo-operation that marks the beginning of a catch block. It
// returns the caught exception.
struct CatchBlockBeginOp : FixedArityOperationT<0, CatchBlockBeginOp> {
  static constexpr OpEffects effects = OpEffects().CanCallAnything();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  CatchBlockBeginOp() : Base() {}
  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{}; }
};

// Throwing operations always appear together with `DidntThrowOp`, which
// produces the value in case that no exception was thrown. If the callsite is
// non-catching, then `DidntThrowOp` follows right after the throwing operation:
//
//   100: Call(...)
//   101: DidntThrow(100)
//   102: Foo(101)
//
// If the callsite can catch, then the
// pattern is as follows:
//
//   100: Call(...)
//   101: CheckException(B10, B11)
//
//   B10:
//   102: DidntThrow(100)
//   103: Foo(102)
//
//   B11:
//   200: CatchBlockBegin()
//   201: ...
//
// This complexity is mostly hidden from graph creation, with
// `DidntThrowOp` and `CheckExceptionOp` being inserted automatically.
// The correct way to produce `CheckExceptionOp` is to create an
// `Assembler::CatchScope`, which will cause all throwing operations
// to add a `CheckExceptionOp` automatically while the scope is active.
// Since `CopyingPhase` does this automatically, lowering throwing
// operations into an arbitrary subgraph works automatically.
struct DidntThrowOp : FixedArityOperationT<1, DidntThrowOp> {
  static constexpr OpEffects effects = OpEffects().RequiredWhenUnused();

  // If there is a `CheckException` operation with a catch block for
  // `throwing_operation`.
  bool has_catch_block;
  // This is a pointer to a vector instead of a vector to save a bit of memory,
  // using optimal 16 bytes instead of 24.
  const base::Vector<const RegisterRepresentation>* results_rep;

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return *results_rep;
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::None()>();
  }

  OpIndex throwing_operation() const { return input(0); }

  explicit DidntThrowOp(
      OpIndex throwing_operation, bool has_catch_block,
      const base::Vector<const RegisterRepresentation>* results_rep)
      : Base(throwing_operation),
        has_catch_block(has_catch_block),
        results_rep(results_rep) {}
  V8_EXPORT_PRIVATE void Validate(const Graph& graph) const;
  auto options() const { return std::tuple{}; }
};

struct TailCallOp : OperationT<TailCallOp> {
  const TSCallDescriptor* descriptor;

  static constexpr OpEffects effects = OpEffects().CanLeaveCurrentFunction();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    // While TailCalls do return some values, those values are returned to the
    // caller rather than to the current function (and a TailCallOp thus never
    // has any uses), so we set the outputs_rep to empty. If you need to know
    // what a TailCallOp returns, you can find out in `descriptor->outputs_rep`.
    return {};
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    storage.resize(input_count);
    size_t i = 0;
    storage[i++] = MaybeRegisterRepresentation::Tagged();  // True for wasm?
    for (auto rep : descriptor->in_reps) {
      storage[i++] = rep;
    }
    storage.resize(i);
    return base::VectorOf(storage);
  }

  OpIndex callee() const { return input(0); }
  base::Vector<const OpIndex> arguments() const {
    return inputs().SubVector(1, input_count);
  }

  TailCallOp(OpIndex callee, base::Vector<const OpIndex> arguments,
             const TSCallDescriptor* descriptor)
      : Base(1 + arguments.size()), descriptor(descriptor) {
    base::Vector<OpIndex> inputs = this->inputs();
    inputs[0] = callee;
    inputs.SubVector(1, inputs.size()).OverwriteWith(arguments);
  }

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    OpIndex mapped_callee = mapper.Map(callee());
    auto mapped_arguments = mapper.template Map<16>(arguments());
    return fn(mapped_callee, base::VectorOf(mapped_arguments), descriptor);
  }

  void Validate(const Graph& graph) const {}
  static TailCallOp& New(Graph* graph, OpIndex callee,
                         base::Vector<const OpIndex> arguments,
                         const TSCallDescriptor* descriptor) {
    return Base::New(graph, 1 + arguments.size(), callee, arguments,
                     descriptor);
  }
  auto options() const { return std::tuple{descriptor}; }
  void PrintOptions(std::ostream& os) const;
};

// Control-flow should never reach here.
struct UnreachableOp : FixedArityOperationT<0, UnreachableOp> {
  static constexpr OpEffects effects =
      OpEffects().CanDependOnChecks().CanLeaveCurrentFunction();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  UnreachableOp() : Base() {}
  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{}; }
};

struct ReturnOp : OperationT<ReturnOp> {
  static constexpr OpEffects effects = OpEffects().CanLeaveCurrentFunction();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    // TODO(mliedtke): Ideally, a return op would expect to get the correct
    // types for all its return values, not just the pop count.
    return MaybeRepVector<MaybeRegisterRepresentation::Word32()>();
  }

  // Number of additional stack slots to be removed.
  V<Word32> pop_count() const { return input(0); }

  base::Vector<const OpIndex> return_values() const {
    return inputs().SubVector(1, input_count);
  }

  ReturnOp(V<Word32> pop_count, base::Vector<const OpIndex> return_values)
      : Base(1 + return_values.size()) {
    base::Vector<OpIndex> inputs = this->inputs();
    inputs[0] = pop_count;
    inputs.SubVector(1, inputs.size()).OverwriteWith(return_values);
  }

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    OpIndex mapped_pop_count = mapper.Map(pop_count());
    auto mapped_return_values = mapper.template Map<4>(return_values());
    return fn(mapped_pop_count, base::VectorOf(mapped_return_values));
  }

  void Validate(const Graph& graph) const {
  }
  static ReturnOp& New(Graph* graph, V<Word32> pop_count,
                       base::Vector<const OpIndex> return_values) {
    return Base::New(graph, 1 + return_values.size(), pop_count, return_values);
  }
  auto options() const { return std::tuple{}; }
};

struct GotoOp : FixedArityOperationT<0, GotoOp> {
  bool is_backedge;
  Block* destination;

  static constexpr OpEffects effects = OpEffects().CanChangeControlFlow();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  explicit GotoOp(Block* destination, bool is_backedge)
      : Base(), is_backedge(is_backedge), destination(destination) {}
  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{destination, is_backedge}; }
};

struct BranchOp : FixedArityOperationT<1, BranchOp> {
  BranchHint hint;
  Block* if_true;
  Block* if_false;

  static constexpr OpEffects effects = OpEffects().CanChangeControlFlow();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Word32()>();
  }

  OpIndex condition() const { return input(0); }

  BranchOp(OpIndex condition, Block* if_true, Block* if_false, BranchHint hint)
      : Base(condition), hint(hint), if_true(if_true), if_false(if_false) {}

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{if_true, if_false, hint}; }
};

struct SwitchOp : FixedArityOperationT<1, SwitchOp> {
  struct Case {
    BranchHint hint;
    int32_t value;
    Block* destination;

    Case(int32_t value, Block* destination, BranchHint hint)
        : hint(hint), value(value), destination(destination) {}

    bool operator==(const Case& other) const {
      return value == other.value && destination == other.destination &&
             hint == other.hint;
    }
  };
  BranchHint default_hint;
  base::Vector<Case> cases;
  Block* default_case;

  static constexpr OpEffects effects = OpEffects().CanChangeControlFlow();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Word32()>();
  }

  OpIndex input() const { return Base::input(0); }

  SwitchOp(OpIndex input, base::Vector<Case> cases, Block* default_case,
           BranchHint default_hint)
      : Base(input),
        default_hint(default_hint),
        cases(cases),
        default_case(default_case) {}

  void Validate(const Graph& graph) const {
  }
  void PrintOptions(std::ostream& os) const;
  auto options() const { return std::tuple{cases, default_case, default_hint}; }
};

template <>
struct fast_hash<SwitchOp::Case> {
  size_t operator()(SwitchOp::Case v) {
    return fast_hash_combine(v.value, v.destination);
  }
};

inline base::SmallVector<Block*, 4> SuccessorBlocks(const Operation& op) {
  switch (op.opcode) {
    case Opcode::kCheckException: {
      auto& casted = op.Cast<CheckExceptionOp>();
      return {casted.didnt_throw_block, casted.catch_block};
    }
    case Opcode::kGoto: {
      auto& casted = op.Cast<GotoOp>();
      return {casted.destination};
    }
    case Opcode::kBranch: {
      auto& casted = op.Cast<BranchOp>();
      return {casted.if_true, casted.if_false};
    }
    case Opcode::kReturn:
    case Opcode::kTailCall:
    case Opcode::kDeoptimize:
    case Opcode::kUnreachable:
      return base::SmallVector<Block*, 4>{};
    case Opcode::kSwitch: {
      auto& casted = op.Cast<SwitchOp>();
      base::SmallVector<Block*, 4> result;
      for (const SwitchOp::Case& c : casted.cases) {
        result.push_back(c.destination);
      }
      result.push_back(casted.default_case);
      return result;
    }
#define NON_TERMINATOR_CASE(op) case Opcode::k##op:
      TURBOSHAFT_OPERATION_LIST_NOT_BLOCK_TERMINATOR(NON_TERMINATOR_CASE)
      UNREACHABLE();
#undef NON_TERMINATOR_CASE
  }
}

V8_EXPORT_PRIVATE base::SmallVector<Block*, 4> SuccessorBlocks(
    const Block& block, const Graph& graph);

// Tuples are only used to lower operations with multiple outputs.
// `TupleOp` should be folded away by subsequent `ProjectionOp`s.
struct TupleOp : OperationT<TupleOp> {
  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  explicit TupleOp(base::Vector<const OpIndex> inputs) : Base(inputs) {}

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    auto mapped_inputs = mapper.template Map<4>(inputs());
    return fn(base::VectorOf(mapped_inputs));
  }

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{}; }
};

// For operations that produce multiple results, we use `ProjectionOp` to
// distinguish them.
struct ProjectionOp : FixedArityOperationT<1, ProjectionOp> {
  uint16_t index;
  RegisterRepresentation rep;

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(&rep, 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  OpIndex input() const { return Base::input(0); }

  ProjectionOp(OpIndex input, uint16_t index, RegisterRepresentation rep)
      : Base(input), index(index), rep(rep) {}

  void Validate(const Graph& graph) const {
    DCHECK(ValidOpInputRep(graph, input(), rep, index));
  }
  auto options() const { return std::tuple{index, rep}; }
};

struct CheckTurboshaftTypeOfOp
    : FixedArityOperationT<1, CheckTurboshaftTypeOfOp> {
  RegisterRepresentation rep;
  Type type;
  bool successful;

  static constexpr OpEffects effects = OpEffects()
                                           .CanDependOnChecks()
                                           .CanReadImmutableMemory()
                                           .RequiredWhenUnused();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::SingleRep(rep);
  }

  OpIndex input() const { return Base::input(0); }

  CheckTurboshaftTypeOfOp(OpIndex input, RegisterRepresentation rep, Type type,
                          bool successful)
      : Base(input), rep(rep), type(std::move(type)), successful(successful) {}

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{rep, type, successful}; }
};

struct ObjectIsOp : FixedArityOperationT<1, ObjectIsOp> {
  enum class Kind : uint8_t {
    kArrayBufferView,
    kBigInt,
    kBigInt64,
    kCallable,
    kConstructor,
    kDetectableCallable,
    kInternalizedString,
    kNonCallable,
    kNumber,
    kReceiver,
    kReceiverOrNullOrUndefined,
    kSmi,
    kString,
    kStringOrStringWrapper,
    kSymbol,
    kUndetectable,
  };
  enum class InputAssumptions : uint8_t {
    kNone,
    kHeapObject,
    kBigInt,
  };
  Kind kind;
  InputAssumptions input_assumptions;

  // All type checks performed by this operator are regarding immutable
  // properties. Therefore, it can be considered pure. Input assumptions,
  // howerever, can rely on being scheduled after checks.
  static constexpr OpEffects effects =
      OpEffects().CanDependOnChecks().CanReadImmutableMemory();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex input() const { return Base::input(0); }

  ObjectIsOp(OpIndex input, Kind kind, InputAssumptions input_assumptions)
      : Base(input), kind(kind), input_assumptions(input_assumptions) {}
  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{kind, input_assumptions}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           ObjectIsOp::Kind kind);
V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os, ObjectIsOp::InputAssumptions input_assumptions);

enum class NumericKind : uint8_t {
  kFloat64Hole,
  kFinite,
  kInteger,
  kSafeInteger,
  kSmi,
  kMinusZero,
  kNaN,
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os, NumericKind kind);

struct FloatIsOp : FixedArityOperationT<1, FloatIsOp> {
  NumericKind kind;
  FloatRepresentation input_rep;

  FloatIsOp(OpIndex input, NumericKind kind, FloatRepresentation input_rep)
      : Base(input), kind(kind), input_rep(input_rep) {}
  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::SingleRep(input_rep);
  }

  OpIndex input() const { return Base::input(0); }

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{kind, input_rep}; }
};

struct ObjectIsNumericValueOp
    : FixedArityOperationT<1, ObjectIsNumericValueOp> {
  NumericKind kind;
  FloatRepresentation input_rep;

  ObjectIsNumericValueOp(OpIndex input, NumericKind kind,
                         FloatRepresentation input_rep)
      : Base(input), kind(kind), input_rep(input_rep) {}

  // Heap numbers are immutable, so reading from them is pure. We might rely on
  // checks to assume that the input is a heap number.
  static constexpr OpEffects effects =
      OpEffects().CanDependOnChecks().CanReadImmutableMemory();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex input() const { return Base::input(0); }

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{kind, input_rep}; }
};

struct ConvertOp : FixedArityOperationT<1, ConvertOp> {
  enum class Kind : uint8_t {
    kObject,
    kBoolean,
    kNumber,
    kNumberOrOddball,
    kPlainPrimitive,
    kString,
    kSmi,
  };
  Kind from;
  Kind to;

  // All properties/values we read are immutable.
  static constexpr OpEffects effects =
      OpEffects()
          // We only allocate identityless primitives here.
          .CanAllocateWithoutIdentity()
          // We might use preceding checks to ensure the input has the right
          // type.
          .CanDependOnChecks()
          .CanReadImmutableMemory();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex input() const { return Base::input(0); }

  ConvertOp(OpIndex input, Kind from, Kind to)
      : Base(input), from(from), to(to) {}

  void Validate(const Graph& graph) const {
  }
  auto options() const { return std::tuple{from, to}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           ConvertOp::Kind kind);

struct ConvertUntaggedToJSPrimitiveOp
    : FixedArityOperationT<1, ConvertUntaggedToJSPrimitiveOp> {
  enum class JSPrimitiveKind : uint8_t {
    kBigInt,
    kBoolean,
    kHeapNumber,
    kHeapNumberOrUndefined,
    kNumber,
    kSmi,
    kString,
  };
  enum class InputInterpretation : uint8_t {
    kSigned,
    kUnsigned,
    kCharCode,
    kCodePoint,
  };
  JSPrimitiveKind kind;
  RegisterRepresentation input_rep;
  InputInterpretation input_interpretation;
  CheckForMinusZeroMode minus_zero_mode;

  // The input is untagged and the results are identityless primitives.
  static constexpr OpEffects effects = OpEffects().CanAllocateWithoutIdentity();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::SingleRep(input_rep);
  }

  OpIndex input() const { return Base::input(0); }

  ConvertUntaggedToJSPrimitiveOp(OpIndex input, JSPrimitiveKind kind,
                                 RegisterRepresentation input_rep,
                                 InputInterpretation input_interpretation,
                                 CheckForMinusZeroMode minus_zero_mode)
      : Base(input),
        kind(kind),
        input_rep(input_rep),
        input_interpretation(input_interpretation),
        minus_zero_mode(minus_zero_mode) {}

  void Validate(const Graph& graph) const {
    switch (kind) {
      case JSPrimitiveKind::kBigInt:
        DCHECK_EQ(input_rep, RegisterRepresentation::Word64());
        DCHECK_EQ(minus_zero_mode,
                  CheckForMinusZeroMode::kDontCheckForMinusZero);
        break;
      case JSPrimitiveKind::kBoolean:
        DCHECK_EQ(input_rep, RegisterRepresentation::Word32());
        DCHECK_EQ(minus_zero_mode,
                  CheckForMinusZeroMode::kDontCheckForMinusZero);
        break;
      case JSPrimitiveKind::kNumber:
      case JSPrimitiveKind::kHeapNumber:
        DCHECK_IMPLIES(
            minus_zero_mode == CheckForMinusZeroMode::kCheckForMinusZero,
            input_rep == RegisterRepresentation::Float64());
        break;
      case JSPrimitiveKind::kHeapNumberOrUndefined:
        DCHECK_IMPLIES(
            minus_zero_mode == CheckForMinusZeroMode::kDontCheckForMinusZero,
            input_rep == RegisterRepresentation::Float64());
        break;
      case JSPrimitiveKind::kSmi:
        DCHECK_EQ(input_rep, WordRepresentation::Word32());
        DCHECK_EQ(minus_zero_mode,
                  CheckForMinusZeroMode::kDontCheckForMinusZero);
        break;
      case JSPrimitiveKind::kString:
        DCHECK_EQ(input_rep, WordRepresentation::Word32());
        DCHECK_EQ(input_interpretation,
                  any_of(InputInterpretation::kCharCode,
                         InputInterpretation::kCodePoint));
        break;
    }
  }

  auto options() const {
    return std::tuple{kind, input_rep, input_interpretation, minus_zero_mode};
  }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os, ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind kind);

struct ConvertUntaggedToJSPrimitiveOrDeoptOp
    : FixedArityOperationT<2, ConvertUntaggedToJSPrimitiveOrDeoptOp> {
  enum class JSPrimitiveKind : uint8_t {
    kSmi,
  };
  enum class InputInterpretation : uint8_t {
    kSigned,
    kUnsigned,
  };
  JSPrimitiveKind kind;
  RegisterRepresentation input_rep;
  InputInterpretation input_interpretation;
  FeedbackSource feedback;

  // We currently only convert Word representations to Smi or deopt. We need to
  // change the effects if we add more kinds.
  static constexpr OpEffects effects = OpEffects().CanDeopt();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::SingleRep(input_rep);
  }

  OpIndex input() const { return Base::input(0); }
  OpIndex frame_state() const { return Base::input(1); }

  ConvertUntaggedToJSPrimitiveOrDeoptOp(
      OpIndex input, OpIndex frame_state, JSPrimitiveKind kind,
      RegisterRepresentation input_rep,
      InputInterpretation input_interpretation, const FeedbackSource& feedback)
      : Base(input, frame_state),
        kind(kind),
        input_rep(input_rep),
        input_interpretation(input_interpretation),
        feedback(feedback) {}

  void Validate(const Graph& graph) const {
    DCHECK(Get(graph, frame_state()).Is<FrameStateOp>());
  }

  auto options() const {
    return std::tuple{kind, input_rep, input_interpretation, feedback};
  }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os,
    ConvertUntaggedToJSPrimitiveOrDeoptOp::JSPrimitiveKind kind);
V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os, ConvertUntaggedToJSPrimitiveOrDeoptOp::InputInterpretation
                          input_interpretation);

struct ConvertJSPrimitiveToUntaggedOp
    : FixedArityOperationT<1, ConvertJSPrimitiveToUntaggedOp> {
  enum class UntaggedKind : uint8_t {
    kInt32,
    kInt64,
    kUint32,
    kBit,
    kFloat64,
  };
  enum class InputAssumptions : uint8_t {
    kBoolean,
    kSmi,
    kNumberOrOddball,
    kPlainPrimitive,
  };
  UntaggedKind kind;
  InputAssumptions input_assumptions;

  // This operation can read memory, but only immutable aspects, so it counts as
  // pure.
  static constexpr OpEffects effects =
      OpEffects()
          // We might rely on preceding checks to avoid deopts.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    switch (kind) {
      case UntaggedKind::kInt32:
      case UntaggedKind::kUint32:
      case UntaggedKind::kBit:
        return RepVector<RegisterRepresentation::Word32()>();
      case UntaggedKind::kInt64:
        return RepVector<RegisterRepresentation::Word64()>();
      case UntaggedKind::kFloat64:
        return RepVector<RegisterRepresentation::Float64()>();
    }
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex input() const { return Base::input(0); }

  ConvertJSPrimitiveToUntaggedOp(OpIndex input, UntaggedKind kind,
                                 InputAssumptions input_assumptions)
      : Base(input), kind(kind), input_assumptions(input_assumptions) {}
  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{kind, input_assumptions}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os, ConvertJSPrimitiveToUntaggedOp::UntaggedKind kind);
V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os,
    ConvertJSPrimitiveToUntaggedOp::InputAssumptions input_assumptions);

struct ConvertJSPrimitiveToUntaggedOrDeoptOp
    : FixedArityOperationT<2, ConvertJSPrimitiveToUntaggedOrDeoptOp> {
  enum class UntaggedKind : uint8_t {
    kInt32,
    kInt64,
    kFloat64,
    kArrayIndex,
  };
  enum class JSPrimitiveKind : uint8_t {
    kNumber,
    kNumberOrBoolean,
    kNumberOrOddball,
    kNumberOrString,
    kSmi,
  };
  JSPrimitiveKind from_kind;
  UntaggedKind to_kind;
  CheckForMinusZeroMode minus_zero_mode;
  FeedbackSource feedback;

  // This operation can read memory, but only immutable aspects, so it counts as
  // pure.
  static constexpr OpEffects effects = OpEffects().CanDeopt();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    switch (to_kind) {
      case UntaggedKind::kInt32:
        return RepVector<RegisterRepresentation::Word32()>();
      case UntaggedKind::kInt64:
        return RepVector<RegisterRepresentation::Word64()>();
      case UntaggedKind::kFloat64:
        return RepVector<RegisterRepresentation::Float64()>();
      case UntaggedKind::kArrayIndex:
        return Is64() ? RepVector<RegisterRepresentation::Word64()>()
                      : RepVector<RegisterRepresentation::Word32()>();
    }
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex input() const { return Base::input(0); }
  OpIndex frame_state() const { return Base::input(1); }

  ConvertJSPrimitiveToUntaggedOrDeoptOp(OpIndex input, OpIndex frame_state,
                                        JSPrimitiveKind from_kind,
                                        UntaggedKind to_kind,
                                        CheckForMinusZeroMode minus_zero_mode,
                                        const FeedbackSource& feedback)
      : Base(input, frame_state),
        from_kind(from_kind),
        to_kind(to_kind),
        minus_zero_mode(minus_zero_mode),
        feedback(feedback) {}
  void Validate(const Graph& graph) const {
    DCHECK(Get(graph, frame_state()).Is<FrameStateOp>());
  }

  auto options() const {
    return std::tuple{from_kind, to_kind, minus_zero_mode, feedback};
  }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os,
    ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind kind);
V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os, ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind kind);

struct TruncateJSPrimitiveToUntaggedOp
    : FixedArityOperationT<1, TruncateJSPrimitiveToUntaggedOp> {
  enum class UntaggedKind : uint8_t {
    kInt32,
    kInt64,
    kBit,
  };
  enum class InputAssumptions : uint8_t {
    kBigInt,
    kNumberOrOddball,
    kHeapObject,
    kObject,
  };
  UntaggedKind kind;
  InputAssumptions input_assumptions;

  // This operation can read memory, but only immutable aspects, so it counts as
  // pure.
  static constexpr OpEffects effects =
      OpEffects()
          // We might rely on preceding checks to ensure the input type.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    switch (kind) {
      case UntaggedKind::kInt32:
      case UntaggedKind::kBit:
        return RepVector<RegisterRepresentation::Word32()>();
      case UntaggedKind::kInt64:
        return RepVector<RegisterRepresentation::Word64()>();
    }
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex input() const { return Base::input(0); }

  TruncateJSPrimitiveToUntaggedOp(OpIndex input, UntaggedKind kind,
                                  InputAssumptions input_assumptions)
      : Base(input), kind(kind), input_assumptions(input_assumptions) {}
  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{kind, input_assumptions}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os, TruncateJSPrimitiveToUntaggedOp::UntaggedKind kind);
V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os,
    TruncateJSPrimitiveToUntaggedOp::InputAssumptions input_assumptions);

struct TruncateJSPrimitiveToUntaggedOrDeoptOp
    : FixedArityOperationT<2, TruncateJSPrimitiveToUntaggedOrDeoptOp> {
  enum class UntaggedKind : uint8_t {
    kInt32,
  };
  using InputRequirement =
      ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind;
  UntaggedKind kind;
  InputRequirement input_requirement;
  FeedbackSource feedback;

  static constexpr OpEffects effects = OpEffects().CanDeopt();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    switch (kind) {
      case UntaggedKind::kInt32:
        return RepVector<RegisterRepresentation::Word32()>();
    }
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex input() const { return Base::input(0); }
  OpIndex frame_state() const { return Base::input(1); }

  TruncateJSPrimitiveToUntaggedOrDeoptOp(OpIndex input, OpIndex frame_state,
                                         UntaggedKind kind,
                                         InputRequirement input_requirement,
                                         const FeedbackSource& feedback)
      : Base(input, frame_state),
        kind(kind),
        input_requirement(input_requirement),
        feedback(feedback) {}
  void Validate(const Graph& graph) const {
    DCHECK(Get(graph, frame_state()).Is<FrameStateOp>());
  }

  auto options() const { return std::tuple{kind, input_requirement, feedback}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os,
    TruncateJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind kind);

struct ConvertJSPrimitiveToObjectOp : OperationT<ConvertJSPrimitiveToObjectOp> {
  ConvertReceiverMode mode;

  static constexpr OpEffects effects = OpEffects().CanCallAnything();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged()>();
  }

  V<Object> value() const { return Base::input(0); }
  V<Object> native_context() const { return Base::input(1); }
  OptionalV<Object> global_proxy() const {
    return input_count > 2 ? Base::input(2) : OpIndex::Invalid();
  }

  ConvertJSPrimitiveToObjectOp(V<Object> value, V<Object> native_context,
                               OptionalV<Object> global_proxy,
                               ConvertReceiverMode mode)
      : Base(2 + global_proxy.valid()), mode(mode) {
    input(0) = value;
    input(1) = native_context;
    if (global_proxy.valid()) {
      input(2) = global_proxy.value();
    }
  }

  static ConvertJSPrimitiveToObjectOp& New(Graph* graph, V<Object> value,
                                           V<Object> native_context,
                                           OptionalV<Object> global_proxy,
                                           ConvertReceiverMode mode) {
    return Base::New(graph, 2 + global_proxy.valid(), value, native_context,
                     global_proxy, mode);
  }

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    return fn(mapper.Map(value()), mapper.Map(native_context()),
              mapper.Map(global_proxy()), mode);
  }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{mode}; }
};

struct NewConsStringOp : FixedArityOperationT<3, NewConsStringOp> {
  static constexpr OpEffects effects =
      OpEffects()
          // Strings are conceptually immutable and don't have identity.
          .CanAllocateWithoutIdentity()
          // We might rely on preceding checks to ensure the input is a string
          // and on their combined length being between ConsString::kMinLength
          // and ConsString::kMaxLength.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Word32(),
                          MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex length() const { return Base::input(0); }
  OpIndex first() const { return Base::input(1); }
  OpIndex second() const { return Base::input(2); }

  NewConsStringOp(OpIndex length, OpIndex first, OpIndex second)
      : Base(length, first, second) {}
  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{}; }
};

struct NewArrayOp : FixedArityOperationT<1, NewArrayOp> {
  enum class Kind : uint8_t {
    kDouble,
    kObject,
  };
  Kind kind;
  AllocationType allocation_type;

  static constexpr OpEffects effects =
      OpEffects()
          // Allocate the result, which has identity.
          .CanAllocate()
          // We might have checks to ensure the array length is valid and not
          // too big.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::WordPtr()>();
  }

  OpIndex length() const { return Base::input(0); }

  NewArrayOp(OpIndex length, Kind kind, AllocationType allocation_type)
      : Base(length), kind(kind), allocation_type(allocation_type) {}
  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{kind, allocation_type}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           NewArrayOp::Kind kind);

struct DoubleArrayMinMaxOp : FixedArityOperationT<1, DoubleArrayMinMaxOp> {
  enum class Kind : uint8_t {
    kMin,
    kMax,
  };
  Kind kind;

  static constexpr OpEffects effects =
      OpEffects()
          // Read the array contents.
          .CanReadHeapMemory()
          // Allocate the HeapNumber result.
          .CanAllocateWithoutIdentity()
          // We might depend on checks to ensure the input is an array.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex array() const { return Base::input(0); }

  DoubleArrayMinMaxOp(OpIndex array, Kind kind) : Base(array), kind(kind) {}
  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           DoubleArrayMinMaxOp::Kind kind);

// TODO(nicohartmann@): We should consider getting rid of the LoadFieldByIndex
// operation.
struct LoadFieldByIndexOp : FixedArityOperationT<2, LoadFieldByIndexOp> {
  static constexpr OpEffects effects =
      OpEffects()
          // Read the possibly mutable property.
          .CanReadHeapMemory()
          // We may allocate heap number for the result.
          .CanAllocateWithoutIdentity()
          // We assume the input is an object.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Word32()>();
  }

  OpIndex object() const { return Base::input(0); }
  // Index encoding (see `src/objects/field-index-inl.h`):
  // For efficiency, the LoadByFieldIndex instruction takes an index that is
  // optimized for quick access. If the property is inline, the index is
  // positive. If it's out-of-line, the encoded index is -raw_index - 1 to
  // disambiguate the zero out-of-line index from the zero inobject case.
  // The index itself is shifted up by one bit, the lower-most bit
  // signifying if the field is a mutable double box (1) or not (0).
  OpIndex index() const { return Base::input(1); }

  LoadFieldByIndexOp(OpIndex object, OpIndex index) : Base(object, index) {}
  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{}; }
};

struct DebugBreakOp : FixedArityOperationT<0, DebugBreakOp> {
  // Prevent any reordering.
  static constexpr OpEffects effects = OpEffects().CanDeopt();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  DebugBreakOp() : Base() {}
  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{}; }
};

struct DebugPrintOp : FixedArityOperationT<1, DebugPrintOp> {
  RegisterRepresentation rep;

  // We just need to ensure that the debug print stays in the same block and
  // observes the right memory state. It doesn't actually change control flow,
  // but pretending so ensures the we do not remove the debug print even though
  // it is unused. We assume that the debug print doesn't affect memory so that
  // the scheduling of loads is not affected.
  static constexpr OpEffects effects =
      OpEffects().CanChangeControlFlow().CanDependOnChecks().CanReadMemory();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InputsRepFactory::SingleRep(rep);
  }

  OpIndex input() const { return Base::input(0); }

  DebugPrintOp(OpIndex input, RegisterRepresentation rep)
      : Base(input), rep(rep) {}
  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{rep}; }
};

struct BigIntBinopOp : FixedArityOperationT<3, BigIntBinopOp> {
  enum class Kind : uint8_t {
    kAdd,
    kSub,
    kMul,
    kDiv,
    kMod,
    kBitwiseAnd,
    kBitwiseOr,
    kBitwiseXor,
    kShiftLeft,
    kShiftRightArithmetic,
  };
  Kind kind;

  // These operations can deopt (abort), allocate and read immutable data.
  static constexpr OpEffects effects =
      OpEffects()
          // Allocate the resulting BigInt, which does not have identity.
          .CanAllocateWithoutIdentity()
          .CanDeopt();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex left() const { return Base::input(0); }
  OpIndex right() const { return Base::input(1); }
  OpIndex frame_state() const { return Base::input(2); }

  BigIntBinopOp(OpIndex left, OpIndex right, OpIndex frame_state, Kind kind)
      : Base(left, right, frame_state), kind(kind) {}
  void Validate(const Graph& graph) const {
    DCHECK(Get(graph, frame_state()).Is<FrameStateOp>());
  }

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           BigIntBinopOp::Kind kind);

struct BigIntComparisonOp : FixedArityOperationT<2, BigIntComparisonOp> {
  enum class Kind : uint8_t {
    kEqual,
    kLessThan,
    kLessThanOrEqual,
  };
  Kind kind;

  static constexpr OpEffects effects =
      OpEffects()
          // We rely on the inputs having BigInt type.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged()>();
  }

  static bool IsCommutative(Kind kind) { return kind == Kind::kEqual; }

  OpIndex left() const { return Base::input(0); }
  OpIndex right() const { return Base::input(1); }

  BigIntComparisonOp(OpIndex left, OpIndex right, Kind kind)
      : Base(left, right), kind(kind) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           BigIntComparisonOp::Kind kind);

struct BigIntUnaryOp : FixedArityOperationT<1, BigIntUnaryOp> {
  enum class Kind : uint8_t {
    kNegate,
  };
  Kind kind;

  static constexpr OpEffects effects =
      OpEffects()
          // BigInt content is immutable, the allocated result does not have
          // identity.
          .CanAllocateWithoutIdentity()
          // We rely on the input being a BigInt.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex input() const { return Base::input(0); }

  BigIntUnaryOp(OpIndex input, Kind kind) : Base(input), kind(kind) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           BigIntUnaryOp::Kind kind);

struct LoadRootRegisterOp : FixedArityOperationT<0, LoadRootRegisterOp> {
  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::WordPtr()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  LoadRootRegisterOp() : Base() {}
  void Validate(const Graph& graph) const {}
  std::tuple<> options() const { return {}; }
};

struct StringAtOp : FixedArityOperationT<2, StringAtOp> {
  enum class Kind : uint8_t {
    kCharCode,
    kCodePoint,
  };
  Kind kind;

  static constexpr OpEffects effects =
      // String content is immutable, so this operation is pure.
      OpEffects()
          // We rely on the input being a string.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::WordPtr()>();
  }

  OpIndex string() const { return Base::input(0); }
  OpIndex position() const { return Base::input(1); }

  StringAtOp(OpIndex string, OpIndex position, Kind kind)
      : Base(string, position), kind(kind) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           StringAtOp::Kind kind);

#ifdef V8_INTL_SUPPORT
struct StringToCaseIntlOp : FixedArityOperationT<1, StringToCaseIntlOp> {
  enum class Kind : uint8_t {
    kLower,
    kUpper,
  };
  Kind kind;

  static constexpr OpEffects effects =
      OpEffects()
          // String content is immutable, the allocated result does not have
          // identity.
          .CanAllocateWithoutIdentity()
          // We rely on the input being a string.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex string() const { return Base::input(0); }

  StringToCaseIntlOp(OpIndex string, Kind kind) : Base(string), kind(kind) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           StringToCaseIntlOp::Kind kind);
#endif  // V8_INTL_SUPPORT

struct StringLengthOp : FixedArityOperationT<1, StringLengthOp> {
  static constexpr OpEffects effects =
      // String content is immutable, so this operation is pure.
      OpEffects()
          // We rely on the input being a string.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex string() const { return Base::input(0); }

  explicit StringLengthOp(OpIndex string) : Base(string) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{}; }
};

struct StringIndexOfOp : FixedArityOperationT<3, StringIndexOfOp> {
  static constexpr OpEffects effects =
      OpEffects()
          // String content is immutable, the allocated result does not have
          // identity.
          .CanAllocateWithoutIdentity()
          // We rely on the inputs being strings.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged()>();
  }

  // Search the string `search` within the string `string` starting at
  // `position`.
  OpIndex string() const { return Base::input(0); }
  OpIndex search() const { return Base::input(1); }
  OpIndex position() const { return Base::input(2); }

  StringIndexOfOp(OpIndex string, OpIndex search, OpIndex position)
      : Base(string, search, position) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{}; }
};

struct StringFromCodePointAtOp
    : FixedArityOperationT<2, StringFromCodePointAtOp> {
  static constexpr OpEffects effects =
      OpEffects()
          // String content is immutable, the allocated result does not have
          // identity.
          .CanAllocateWithoutIdentity()
          // We rely on the input being in a certain char range.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::WordPtr()>();
  }

  OpIndex string() const { return Base::input(0); }
  OpIndex index() const { return Base::input(1); }

  StringFromCodePointAtOp(OpIndex string, OpIndex index)
      : Base(string, index) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{}; }
};

struct StringSubstringOp : FixedArityOperationT<3, StringSubstringOp> {
  static constexpr OpEffects effects =
      OpEffects()
          // String content is immutable, the allocated result does not have
          // identity.
          .CanAllocateWithoutIdentity()
          // We rely on the input being a string.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Word32(),
                          MaybeRegisterRepresentation::Word32()>();
  }

  OpIndex string() const { return Base::input(0); }
  OpIndex start() const { return Base::input(1); }
  OpIndex end() const { return Base::input(2); }

  StringSubstringOp(OpIndex string, OpIndex start, OpIndex end)
      : Base(string, start, end) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{}; }
};

struct StringConcatOp : FixedArityOperationT<2, StringConcatOp> {
  static constexpr OpEffects effects =
      OpEffects()
          // String content is immutable, the allocated result does not have
          // identity.
          .CanAllocateWithoutIdentity()
          // We rely on the inputs being strings.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex left() const { return Base::input(0); }
  OpIndex right() const { return Base::input(1); }

  StringConcatOp(OpIndex left, OpIndex right) : Base(left, right) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{}; }
};

struct StringComparisonOp : FixedArityOperationT<2, StringComparisonOp> {
  enum class Kind : uint8_t {
    kEqual,
    kLessThan,
    kLessThanOrEqual,
  };
  Kind kind;

  static constexpr OpEffects effects =
      // String content is immutable, so the operation is pure.
      OpEffects()
          // We rely on the input being strings.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged()>();
  }

  static bool IsCommutative(Kind kind) { return kind == Kind::kEqual; }

  OpIndex left() const { return Base::input(0); }
  OpIndex right() const { return Base::input(1); }

  StringComparisonOp(OpIndex left, OpIndex right, Kind kind)
      : Base(left, right), kind(kind) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           StringComparisonOp::Kind kind);

struct ArgumentsLengthOp : FixedArityOperationT<0, ArgumentsLengthOp> {
  enum class Kind : uint8_t {
    kArguments,
    kRest,
  };
  Kind kind;
  int formal_parameter_count =
      0;  // This field is unused for kind == kArguments.

  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  explicit ArgumentsLengthOp(Kind kind, int formal_parameter_count)
      : Base(), kind(kind), formal_parameter_count(formal_parameter_count) {
    DCHECK_IMPLIES(kind == Kind::kArguments, formal_parameter_count == 0);
  }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{kind, formal_parameter_count}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           ArgumentsLengthOp::Kind kind);

struct NewArgumentsElementsOp
    : FixedArityOperationT<1, NewArgumentsElementsOp> {
  CreateArgumentsType type;
  int formal_parameter_count;

  static constexpr OpEffects effects =
      OpEffects()
          // Allocate the fixed array, which has identity.
          .CanAllocate()
          // Do not move the allocation before checks/branches.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex arguments_count() const { return Base::input(0); }

  NewArgumentsElementsOp(OpIndex arguments_count, CreateArgumentsType type,
                         int formal_parameter_count)
      : Base(arguments_count),
        type(type),
        formal_parameter_count(formal_parameter_count) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{type, formal_parameter_count}; }
};

inline constexpr RegisterRepresentation RegisterRepresentationForArrayType(
    ExternalArrayType array_type) {
  switch (array_type) {
    case kExternalInt8Array:
    case kExternalUint8Array:
    case kExternalUint8ClampedArray:
    case kExternalInt16Array:
    case kExternalUint16Array:
    case kExternalInt32Array:
    case kExternalUint32Array:
      return RegisterRepresentation::Word32();
    case kExternalFloat32Array:
      return RegisterRepresentation::Float32();
    case kExternalFloat64Array:
      return RegisterRepresentation::Float64();
    case kExternalBigInt64Array:
    case kExternalBigUint64Array:
      return RegisterRepresentation::Word64();
    case kExternalFloat16Array:
      UNIMPLEMENTED();
  }
}

inline base::Vector<const RegisterRepresentation> VectorForRep(
    RegisterRepresentation rep) {
  static constexpr std::array<RegisterRepresentation, 6> table{
      RegisterRepresentation::Word32(),  RegisterRepresentation::Word64(),
      RegisterRepresentation::Float32(), RegisterRepresentation::Float64(),
      RegisterRepresentation::Tagged(),  RegisterRepresentation::Compressed()};
  return base::VectorOf(&table[static_cast<size_t>(rep.value())], 1);
}

struct LoadTypedElementOp : FixedArityOperationT<4, LoadTypedElementOp> {
  ExternalArrayType array_type;

  static constexpr OpEffects effects =
      OpEffects()
          // We read mutable memory.
          .CanReadMemory()
          // We rely on the input type and a valid index.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return VectorForRep(RegisterRepresentationForArrayType(array_type));
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::WordPtr(),
                          MaybeRegisterRepresentation::WordPtr()>();
  }

  OpIndex buffer() const { return Base::input(0); }
  OpIndex base() const { return Base::input(1); }
  OpIndex external() const { return Base::input(2); }
  OpIndex index() const { return Base::input(3); }

  LoadTypedElementOp(OpIndex buffer, OpIndex base, OpIndex external,
                     OpIndex index, ExternalArrayType array_type)
      : Base(buffer, base, external, index), array_type(array_type) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{array_type}; }
};

struct LoadDataViewElementOp : FixedArityOperationT<4, LoadDataViewElementOp> {
  ExternalArrayType element_type;

  static constexpr OpEffects effects = OpEffects()
                                           // We read mutable memory.
                                           .CanReadMemory()
                                           // We rely on the input type.
                                           .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return VectorForRep(RegisterRepresentationForArrayType(element_type));
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::WordPtr(),
                          MaybeRegisterRepresentation::Word32()>();
  }

  OpIndex object() const { return Base::input(0); }
  OpIndex storage() const { return Base::input(1); }
  OpIndex index() const { return Base::input(2); }
  OpIndex is_little_endian() const { return Base::input(3); }

  LoadDataViewElementOp(OpIndex object, OpIndex storage, OpIndex index,
                        OpIndex is_little_endian,
                        ExternalArrayType element_type)
      : Base(object, storage, index, is_little_endian),
        element_type(element_type) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{element_type}; }
};

struct LoadStackArgumentOp : FixedArityOperationT<2, LoadStackArgumentOp> {
  // Stack arguments are immutable, so reading them is pure.
  static constexpr OpEffects effects =
      OpEffects()
          // We rely on the input being in bounds.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::WordPtr(),
                          MaybeRegisterRepresentation::WordPtr()>();
  }

  OpIndex base() const { return Base::input(0); }
  OpIndex index() const { return Base::input(1); }

  LoadStackArgumentOp(OpIndex base, OpIndex index) : Base(base, index) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{}; }
};

struct StoreTypedElementOp : FixedArityOperationT<5, StoreTypedElementOp> {
  ExternalArrayType array_type;

  static constexpr OpEffects effects =
      OpEffects()
          // We are reading the backing store pointer and writing into it.
          .CanReadMemory()
          .CanWriteMemory()
          // We rely on the input type and a valid index.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InitVectorOf(
        storage,
        {RegisterRepresentation::Tagged(), RegisterRepresentation::Tagged(),
         RegisterRepresentation::WordPtr(), RegisterRepresentation::WordPtr(),
         RegisterRepresentationForArrayType(array_type)});
  }

  OpIndex buffer() const { return Base::input(0); }
  OpIndex base() const { return Base::input(1); }
  OpIndex external() const { return Base::input(2); }
  OpIndex index() const { return Base::input(3); }
  OpIndex value() const { return Base::input(4); }

  StoreTypedElementOp(OpIndex buffer, OpIndex base, OpIndex external,
                      OpIndex index, OpIndex value,
                      ExternalArrayType array_type)
      : Base(buffer, base, external, index, value), array_type(array_type) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{array_type}; }
};

struct StoreDataViewElementOp
    : FixedArityOperationT<5, StoreDataViewElementOp> {
  ExternalArrayType element_type;

  static constexpr OpEffects effects =
      OpEffects()
          // We are reading the backing store pointer and writing into it.
          .CanReadMemory()
          .CanWriteMemory()
          // We rely on the input type and a valid index.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InitVectorOf(
        storage,
        {RegisterRepresentation::Tagged(), RegisterRepresentation::Tagged(),
         RegisterRepresentation::WordPtr(),
         RegisterRepresentationForArrayType(element_type),
         RegisterRepresentation::Word32()});
  }

  OpIndex object() const { return Base::input(0); }
  OpIndex storage() const { return Base::input(1); }
  OpIndex index() const { return Base::input(2); }
  OpIndex value() const { return Base::input(3); }
  OpIndex is_little_endian() const { return Base::input(4); }

  StoreDataViewElementOp(OpIndex object, OpIndex storage, OpIndex index,
                         OpIndex value, OpIndex is_little_endian,
                         ExternalArrayType element_type)
      : Base(object, storage, index, value, is_little_endian),
        element_type(element_type) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{element_type}; }
};

struct TransitionAndStoreArrayElementOp
    : FixedArityOperationT<3, TransitionAndStoreArrayElementOp> {
  enum class Kind : uint8_t {
    kElement,
    kNumberElement,
    kOddballElement,
    kNonNumberElement,
    kSignedSmallElement,
  };
  Kind kind;
  MaybeHandle<Map> fast_map;
  MaybeHandle<Map> double_map;

  static constexpr OpEffects effects =
      OpEffects()
          // We are reading and writing mutable memory.
          .CanReadHeapMemory()
          .CanWriteHeapMemory()
          // We rely on the input type and a valid index.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InitVectorOf(
        storage, {RegisterRepresentation::Tagged(),
                  RegisterRepresentation::WordPtr(), value_representation()});
  }

  OpIndex array() const { return Base::input(0); }
  OpIndex index() const { return Base::input(1); }
  OpIndex value() const { return Base::input(2); }

  TransitionAndStoreArrayElementOp(OpIndex array, OpIndex index, OpIndex value,
                                   Kind kind, MaybeHandle<Map> fast_map,
                                   MaybeHandle<Map> double_map)
      : Base(array, index, value),
        kind(kind),
        fast_map(fast_map),
        double_map(double_map) {}

  void Validate(const Graph& graph) const {}

  RegisterRepresentation value_representation() const {
    switch (kind) {
      case Kind::kElement:
      case Kind::kNonNumberElement:
      case Kind::kOddballElement:
        return RegisterRepresentation::Tagged();
      case Kind::kNumberElement:
        return RegisterRepresentation::Float64();
      case Kind::kSignedSmallElement:
        return RegisterRepresentation::Word32();
    }
  }

  size_t hash_value() const {
    return fast_hash_combine(kind, fast_map.address(), double_map.address());
  }

  bool operator==(const TransitionAndStoreArrayElementOp& other) const {
    return kind == other.kind && fast_map.equals(other.fast_map) &&
           double_map.equals(other.double_map);
  }

  auto options() const { return std::tuple{kind, fast_map, double_map}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os, TransitionAndStoreArrayElementOp::Kind kind);

struct CompareMapsOp : FixedArityOperationT<1, CompareMapsOp> {
  ZoneRefSet<Map> maps;

  static constexpr OpEffects effects = OpEffects().CanReadHeapMemory();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex heap_object() const { return Base::input(0); }

  CompareMapsOp(OpIndex heap_object, ZoneRefSet<Map> maps)
      : Base(heap_object), maps(std::move(maps)) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{maps}; }
};

struct CheckMapsOp : FixedArityOperationT<2, CheckMapsOp> {
  CheckMapsFlags flags;
  ZoneRefSet<Map> maps;
  FeedbackSource feedback;

  // TODO(tebbi): Map checks without map transitions have less effects.
  static constexpr OpEffects effects = OpEffects()
                                           .CanDependOnChecks()
                                           .CanDeopt()
                                           .CanReadHeapMemory()
                                           .CanWriteHeapMemory();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex heap_object() const { return Base::input(0); }
  OpIndex frame_state() const { return Base::input(1); }

  CheckMapsOp(OpIndex heap_object, OpIndex frame_state, ZoneRefSet<Map> maps,
              CheckMapsFlags flags, const FeedbackSource& feedback)
      : Base(heap_object, frame_state),
        flags(flags),
        maps(std::move(maps)),
        feedback(feedback) {}

  void Validate(const Graph& graph) const {
    DCHECK(Get(graph, frame_state()).Is<FrameStateOp>());
  }

  auto options() const { return std::tuple{maps, flags, feedback}; }
};

// AssumeMaps are inserted after CheckMaps have been lowered, in order to keep
// map information around and easily accessible for subsequent optimization
// passes (Load Elimination for instance can then use those AssumeMap to
// determine that some objects don't alias because they have different maps).
struct AssumeMapOp : FixedArityOperationT<1, AssumeMapOp> {
  ZoneRefSet<Map> maps;
  // AssumeMap should not be scheduled before the preceding CheckMaps
  static constexpr OpEffects effects = OpEffects()
                                           .CanDependOnChecks()
                                           .CanReadHeapMemory()
                                           .CanChangeControlFlow();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex heap_object() const { return Base::input(0); }

  AssumeMapOp(OpIndex heap_object, ZoneRefSet<Map> maps)
      : Base(heap_object), maps(std::move(maps)) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{maps}; }
};

struct CheckedClosureOp : FixedArityOperationT<2, CheckedClosureOp> {
  Handle<FeedbackCell> feedback_cell;

  // We only check immutable aspects of the incoming value.
  static constexpr OpEffects effects = OpEffects().CanDeopt();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex input() const { return Base::input(0); }
  OpIndex frame_state() const { return Base::input(1); }

  CheckedClosureOp(OpIndex input, OpIndex frame_state,
                   Handle<FeedbackCell> feedback_cell)
      : Base(input, frame_state), feedback_cell(feedback_cell) {}

  void Validate(const Graph& graph) const {
    DCHECK(Get(graph, frame_state()).Is<FrameStateOp>());
  }

  bool operator==(const CheckedClosureOp& other) const {
    return feedback_cell.address() == other.feedback_cell.address();
  }
  size_t hash_value() const {
    return base::hash_value(feedback_cell.address());
  }

  auto options() const { return std::tuple{feedback_cell}; }
};

struct CheckEqualsInternalizedStringOp
    : FixedArityOperationT<3, CheckEqualsInternalizedStringOp> {
  static constexpr OpEffects effects = OpEffects().CanDeopt();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex expected() const { return Base::input(0); }
  OpIndex value() const { return Base::input(1); }
  OpIndex frame_state() const { return Base::input(2); }

  CheckEqualsInternalizedStringOp(OpIndex expected, OpIndex value,
                                  OpIndex frame_state)
      : Base(expected, value, frame_state) {}

  void Validate(const Graph& graph) const {
    DCHECK(Get(graph, frame_state()).Is<FrameStateOp>());
  }

  auto options() const { return std::tuple{}; }
};

struct LoadMessageOp : FixedArityOperationT<1, LoadMessageOp> {
  static constexpr OpEffects effects =
      OpEffects()
          // We are reading the message from the isolate.
          .CanReadOffHeapMemory();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::WordPtr()>();
  }

  OpIndex offset() const { return Base::input(0); }

  explicit LoadMessageOp(OpIndex offset) : Base(offset) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{}; }
};

struct StoreMessageOp : FixedArityOperationT<2, StoreMessageOp> {
  static constexpr OpEffects effects =
      OpEffects()
          // We are writing the message in the isolate.
          .CanWriteOffHeapMemory();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::WordPtr(),
                          MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex offset() const { return Base::input(0); }
  OpIndex object() const { return Base::input(1); }

  explicit StoreMessageOp(OpIndex offset, OpIndex object)
      : Base(offset, object) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{}; }
};

struct SameValueOp : FixedArityOperationT<2, SameValueOp> {
  enum class Mode : uint8_t {
    kSameValue,
    kSameValueNumbersOnly,
  };
  Mode mode;

  static constexpr OpEffects effects =
      OpEffects()
          // We might depend on the inputs being numbers.
          .CanDependOnChecks();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex left() const { return Base::input(0); }
  OpIndex right() const { return Base::input(1); }

  SameValueOp(OpIndex left, OpIndex right, Mode mode)
      : Base(left, right), mode(mode) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{mode}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           SameValueOp::Mode mode);

struct Float64SameValueOp : FixedArityOperationT<2, Float64SameValueOp> {
  static constexpr OpEffects effects = OpEffects();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Float64(),
                          MaybeRegisterRepresentation::Float64()>();
  }

  OpIndex left() const { return Base::input(0); }
  OpIndex right() const { return Base::input(1); }

  Float64SameValueOp(OpIndex left, OpIndex right) : Base(left, right) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{}; }
};

struct FastApiCallParameters : public NON_EXPORTED_BASE(ZoneObject) {
  const FastApiCallFunctionVector c_functions;
  fast_api_call::OverloadsResolutionResult resolution_result;

  const CFunctionInfo* c_signature() const { return c_functions[0].signature; }

  FastApiCallParameters(
      const FastApiCallFunctionVector& c_functions,
      const fast_api_call::OverloadsResolutionResult& resolution_result)
      : c_functions(c_functions), resolution_result(resolution_result) {
    DCHECK_LT(0, c_functions.size());
  }

  static const FastApiCallParameters* Create(
      const FastApiCallFunctionVector& c_functions,
      const fast_api_call::OverloadsResolutionResult& resolution_result,
      Zone* graph_zone) {
    return graph_zone->New<FastApiCallParameters>(std::move(c_functions),
                                                  resolution_result);
  }
};

struct FastApiCallOp : OperationT<FastApiCallOp> {
  static constexpr uint32_t kSuccessValue = 1;
  static constexpr uint32_t kFailureValue = 0;
  const FastApiCallParameters* parameters;

  static constexpr OpEffects effects = OpEffects().CanCallAnything();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32(),
                     RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    DCHECK_EQ(inputs().size(), 1 + parameters->c_signature()->ArgumentCount());
    storage.resize(inputs().size());
    storage[0] = MaybeRegisterRepresentation::Tagged();
    for (unsigned i = 0; i < parameters->c_signature()->ArgumentCount(); ++i) {
      storage[i + 1] = argument_representation(i);
    }
    return base::VectorOf(storage);
  }

  MaybeRegisterRepresentation argument_representation(
      unsigned argument_index) const {
    const CTypeInfo& arg_type =
        parameters->c_signature()->ArgumentInfo(argument_index);
    uint8_t flags = static_cast<uint8_t>(arg_type.GetFlags());
    switch (arg_type.GetSequenceType()) {
      case CTypeInfo::SequenceType::kScalar:
        if (flags & (static_cast<uint8_t>(CTypeInfo::Flags::kEnforceRangeBit) |
                     static_cast<uint8_t>(CTypeInfo::Flags::kClampBit))) {
          return MaybeRegisterRepresentation::Float64();
        }
        switch (arg_type.GetType()) {
          case CTypeInfo::Type::kVoid:
            UNREACHABLE();
          case CTypeInfo::Type::kBool:
          case CTypeInfo::Type::kUint8:
          case CTypeInfo::Type::kInt32:
          case CTypeInfo::Type::kUint32:
            return MaybeRegisterRepresentation::Word32();
          case CTypeInfo::Type::kInt64:
          case CTypeInfo::Type::kUint64:
            return MaybeRegisterRepresentation::Word64();
          case CTypeInfo::Type::kV8Value:
          case CTypeInfo::Type::kApiObject:
          case CTypeInfo::Type::kPointer:
          case CTypeInfo::Type::kSeqOneByteString:
            return MaybeRegisterRepresentation::Tagged();
          case CTypeInfo::Type::kFloat32:
          case CTypeInfo::Type::kFloat64:
            return MaybeRegisterRepresentation::Float64();
          case CTypeInfo::Type::kAny:
            // As the register representation is unknown, just treat it as None
            // to prevent any validation.
            return MaybeRegisterRepresentation::None();
        }
      case CTypeInfo::SequenceType::kIsSequence:
      case CTypeInfo::SequenceType::kIsTypedArray:
        return MaybeRegisterRepresentation::Tagged();
      case CTypeInfo::SequenceType::kIsArrayBuffer:
        UNREACHABLE();
    }
  }

  OpIndex data_argument() const { return input(0); }
  base::Vector<const OpIndex> arguments() const {
    return inputs().SubVector(1, inputs().size());
  }

  FastApiCallOp(OpIndex data_argument, base::Vector<const OpIndex> arguments,
                const FastApiCallParameters* parameters)
      : Base(1 + arguments.size()), parameters(parameters) {
    base::Vector<OpIndex> inputs = this->inputs();
    inputs[0] = data_argument;
    inputs.SubVector(1, 1 + arguments.size()).OverwriteWith(arguments);
  }

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    OpIndex mapped_data_argument = mapper.Map(data_argument());
    auto mapped_arguments = mapper.template Map<8>(arguments());
    return fn(mapped_data_argument, base::VectorOf(mapped_arguments),
              parameters);
  }

  void Validate(const Graph& graph) const {
  }

  static FastApiCallOp& New(Graph* graph, OpIndex data_argument,
                            base::Vector<const OpIndex> arguments,
                            const FastApiCallParameters* parameters) {
    return Base::New(graph, 1 /*data_argument*/ + arguments.size(),
                     data_argument, arguments, parameters);
  }

  auto options() const { return std::tuple{parameters}; }
};

struct RuntimeAbortOp : FixedArityOperationT<0, RuntimeAbortOp> {
  AbortReason reason;

  static constexpr OpEffects effects = OpEffects().CanCallAnything();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  explicit RuntimeAbortOp(AbortReason reason) : reason(reason) {}

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{reason}; }
};

struct EnsureWritableFastElementsOp
    : FixedArityOperationT<2, EnsureWritableFastElementsOp> {
  // TODO(tebbi): Can we have more precise effects here?
  static constexpr OpEffects effects = OpEffects().CanCallAnything();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex object() const { return Base::input(0); }
  OpIndex elements() const { return Base::input(1); }

  EnsureWritableFastElementsOp(OpIndex object, OpIndex elements)
      : Base(object, elements) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{}; }
};

struct MaybeGrowFastElementsOp
    : FixedArityOperationT<5, MaybeGrowFastElementsOp> {
  GrowFastElementsMode mode;
  FeedbackSource feedback;

  // TODO(tebbi): Can we have more precise effects here?
  static constexpr OpEffects effects = OpEffects().CanCallAnything();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Word32(),
                          MaybeRegisterRepresentation::Word32()>();
  }

  OpIndex object() const { return Base::input(0); }
  OpIndex elements() const { return Base::input(1); }
  OpIndex index() const { return Base::input(2); }
  OpIndex elements_length() const { return Base::input(3); }
  OpIndex frame_state() const { return Base::input(4); }

  MaybeGrowFastElementsOp(OpIndex object, OpIndex elements, OpIndex index,
                          OpIndex elements_length, OpIndex frame_state,
                          GrowFastElementsMode mode,
                          const FeedbackSource& feedback)
      : Base(object, elements, index, elements_length, frame_state),
        mode(mode),
        feedback(feedback) {}

  void Validate(const Graph& graph) const {
    DCHECK(Get(graph, frame_state()).Is<FrameStateOp>());
  }

  auto options() const { return std::tuple{mode, feedback}; }
};

struct TransitionElementsKindOp
    : FixedArityOperationT<1, TransitionElementsKindOp> {
  ElementsTransition transition;

  static constexpr OpEffects effects = OpEffects().CanCallAnything();
  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex object() const { return Base::input(0); }

  TransitionElementsKindOp(OpIndex object, const ElementsTransition& transition)
      : Base(object), transition(transition) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{transition}; }
};

struct FindOrderedHashEntryOp
    : FixedArityOperationT<2, FindOrderedHashEntryOp> {
  enum class Kind : uint8_t {
    kFindOrderedHashMapEntry,
    kFindOrderedHashMapEntryForInt32Key,
    kFindOrderedHashSetEntry,
  };
  Kind kind;

  // TODO(tebbi): Can we have more precise effects here?
  static constexpr OpEffects effects = OpEffects().CanCallAnything();
  base::Vector<const RegisterRepresentation> outputs_rep() const {
    switch (kind) {
      case Kind::kFindOrderedHashMapEntry:
      case Kind::kFindOrderedHashSetEntry:
        return RepVector<RegisterRepresentation::Tagged()>();
      case Kind::kFindOrderedHashMapEntryForInt32Key:
        return RepVector<RegisterRepresentation::WordPtr()>();
    }
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return kind == Kind::kFindOrderedHashMapEntryForInt32Key
               ? MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                                MaybeRegisterRepresentation::Word32()>()
               : MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                                MaybeRegisterRepresentation::Tagged()>();
  }

  OpIndex data_structure() const { return Base::input(0); }
  OpIndex key() const { return Base::input(1); }

  FindOrderedHashEntryOp(OpIndex data_structure, OpIndex key, Kind kind)
      : Base(data_structure, key), kind(kind) {}

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           FindOrderedHashEntryOp::Kind kind);

struct CommentOp : FixedArityOperationT<0, CommentOp> {
  const char* message;

  // Comments should not be removed.
  static constexpr OpEffects effects = OpEffects().RequiredWhenUnused();

  explicit CommentOp(const char* message) : message(message) {}

  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{message}; }
};

struct SpeculativeNumberBinopOp
    : FixedArityOperationT<3, SpeculativeNumberBinopOp> {
  enum class Kind : uint8_t {
    kSafeIntegerAdd,
  };

  Kind kind;

  static constexpr OpEffects effects = OpEffects().CanDeopt().CanAllocate();

  OpIndex left() const { return Base::input(0); }
  OpIndex right() const { return Base::input(1); }
  OpIndex frame_state() const { return Base::input(2); }

  SpeculativeNumberBinopOp(OpIndex left, OpIndex right, OpIndex frame_state,
                           Kind kind)
      : Base(left, right, frame_state), kind(kind) {}

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {
    DCHECK(Get(graph, frame_state()).Is<FrameStateOp>());
  }

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           SpeculativeNumberBinopOp::Kind kind);

#if V8_ENABLE_WEBASSEMBLY

V8_EXPORT_PRIVATE const RegisterRepresentation& RepresentationFor(
    wasm::ValueType type);

struct GlobalGetOp : FixedArityOperationT<1, GlobalGetOp> {
  const wasm::WasmGlobal* global;
  static constexpr OpEffects effects = OpEffects().CanReadMemory();

  OpIndex instance() const { return Base::input(0); }

  GlobalGetOp(OpIndex instance, const wasm::WasmGlobal* global)
      : Base(instance), global(global) {}

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    const RegisterRepresentation& repr = RepresentationFor(global->type);
    return base::VectorOf(&repr, 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{global}; }
};

struct GlobalSetOp : FixedArityOperationT<2, GlobalSetOp> {
  const wasm::WasmGlobal* global;
  static constexpr OpEffects effects = OpEffects().CanWriteMemory();

  OpIndex instance() const { return Base::input(0); }
  OpIndex value() const { return Base::input(1); }

  explicit GlobalSetOp(OpIndex instance, OpIndex value,
                       const wasm::WasmGlobal* global)
      : Base(instance, value), global(global) {}

  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    storage.resize(2);
    storage[0] = MaybeRegisterRepresentation::Tagged();
    storage[1] = MaybeRegisterRepresentation(RepresentationFor(global->type));
    return base::VectorOf(storage);
  }

  void Validate(const Graph& graph) const { DCHECK(global->mutability); }

  auto options() const { return std::tuple{global}; }
};

struct NullOp : FixedArityOperationT<0, NullOp> {
  wasm::ValueType type;
  static constexpr OpEffects effects = OpEffects();

  explicit NullOp(wasm::ValueType type) : Base(), type(type) {}

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  void Validate(const Graph& graph) const {
    DCHECK(type.is_object_reference() && type.is_nullable());
  }

  auto options() const { return std::tuple{type}; }
};

struct IsNullOp : FixedArityOperationT<1, IsNullOp> {
  wasm::ValueType type;
  static constexpr OpEffects effects = OpEffects();

  OpIndex object() const { return Base::input(0); }

  IsNullOp(OpIndex object, wasm::ValueType type) : Base(object), type(type) {}

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {
  }

  auto options() const { return std::tuple{type}; }
};

// Traps on a null input, otherwise returns the input, type-cast to the
// respective non-nullable type.
struct AssertNotNullOp : FixedArityOperationT<1, AssertNotNullOp> {
  wasm::ValueType type;
  TrapId trap_id;

  // Lowers to a trap and inherits {TrapIf}'s effects.
  static constexpr OpEffects effects =
      OpEffects().CanDependOnChecks().CanLeaveCurrentFunction();

  OpIndex object() const { return Base::input(0); }

  AssertNotNullOp(OpIndex object, wasm::ValueType type, TrapId trap_id)
      : Base(object), type(type), trap_id(trap_id) {}

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {
    // TODO(14108): Validate.
  }

  auto options() const { return std::tuple{type, trap_id}; }
};

// The runtime type (RTT) is a value representing a conrete type (in this case
// heap-type). The canonical RTTs are implicitly created values and invisible to
// the user in wasm-gc MVP. (See
// https://github.com/WebAssembly/gc/blob/main/proposals/gc/MVP.md#runtime-types)
struct RttCanonOp : FixedArityOperationT<1, RttCanonOp> {
  uint32_t type_index;

  static constexpr OpEffects effects = OpEffects();

  explicit RttCanonOp(OpIndex rtts, uint32_t type_index)
      : Base(rtts), type_index(type_index) {}

  OpIndex rtts() const { return Base::input(0); }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }
  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{type_index}; }
};

struct WasmTypeCheckOp : OperationT<WasmTypeCheckOp> {
  WasmTypeCheckConfig config;

  static constexpr OpEffects effects = OpEffects().AssumesConsistentHeap();

  WasmTypeCheckOp(V<Object> object, OptionalV<Object> rtt,
                  WasmTypeCheckConfig config)
      : Base(1 + rtt.valid()), config(config) {
    input(0) = object;
    if (rtt.valid()) {
      input(1) = rtt.value();
    }
  }

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    return fn(mapper.Map(object()), mapper.Map(rtt()), config);
  }

  V<Object> object() const { return Base::input(0); }
  OptionalV<Object> rtt() const {
    return input_count > 1 ? input(1) : OpIndex::Invalid();
  }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return input_count > 1
               ? MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                                MaybeRegisterRepresentation::Tagged()>()
               : MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{config}; }

  static WasmTypeCheckOp& New(Graph* graph, V<Object> object,
                              OptionalV<Object> rtt,
                              WasmTypeCheckConfig config) {
    return Base::New(graph, 1 + rtt.valid(), object, rtt, config);
  }
};

struct WasmTypeCastOp : OperationT<WasmTypeCastOp> {
  WasmTypeCheckConfig config;

  static constexpr OpEffects effects = OpEffects().CanLeaveCurrentFunction();

  WasmTypeCastOp(V<Object> object, OptionalV<Object> rtt,
                 WasmTypeCheckConfig config)
      : Base(1 + rtt.valid()), config(config) {
    input(0) = object;
    if (rtt.valid()) {
      input(1) = rtt.value();
    }
  }

  template <typename Fn, typename Mapper>
  V8_INLINE auto Explode(Fn fn, Mapper& mapper) const {
    return fn(mapper.Map(object()), mapper.Map(rtt()), config);
  }

  V<Object> object() const { return Base::input(0); }
  OptionalV<Object> rtt() const {
    return input_count > 1 ? input(1) : OpIndex::Invalid();
  }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return input_count > 1
               ? MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                                MaybeRegisterRepresentation::Tagged()>()
               : MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{config}; }

  static WasmTypeCastOp& New(Graph* graph, V<Object> object,
                             OptionalV<Object> rtt,
                             WasmTypeCheckConfig config) {
    return Base::New(graph, 1 + rtt.valid(), object, rtt, config);
  }
};

// Annotate a value with a wasm type.
// This is a helper operation to propagate type information from the graph
// builder to type-based optimizations and will then be removed.
struct WasmTypeAnnotationOp : FixedArityOperationT<1, WasmTypeAnnotationOp> {
  static constexpr OpEffects effects = OpEffects();
  wasm::ValueType type;

  explicit WasmTypeAnnotationOp(OpIndex value, wasm::ValueType type)
      : Base(value), type(type) {}

  V<Object> value() const { return Base::input(0); }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {
    // In theory, the operation could be used for non-reference types as well.
    // This would require updating inputs_rep and outputs_rep to be based on
    // the wasm type.
    DCHECK(type.is_object_reference());
  }

  auto options() const { return std::tuple(type); }
};

struct AnyConvertExternOp : FixedArityOperationT<1, AnyConvertExternOp> {
  static constexpr OpEffects effects =
      SmiValuesAre31Bits() ? OpEffects().CanReadMemory()
                           : OpEffects().CanReadMemory().CanAllocate();

  explicit AnyConvertExternOp(V<Object> object) : Base(object) {}

  V<Object> object() const { return Base::input(0); }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple(); }
};

struct ExternConvertAnyOp : FixedArityOperationT<1, ExternConvertAnyOp> {
  static constexpr OpEffects effects = OpEffects();

  explicit ExternConvertAnyOp(V<Object> object) : Base(object) {}

  V<Object> object() const { return Base::input(0); }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple(); }
};

struct StructGetOp : FixedArityOperationT<1, StructGetOp> {
  bool is_signed;  // `false` only for unsigned packed type accesses.
  CheckForNull null_check;
  const wasm::StructType* type;
  uint32_t type_index;
  int field_index;

  OpEffects Effects() const {
    OpEffects result =
        OpEffects()
            // This should not float above a protective null check.
            .CanDependOnChecks()
            .CanReadMemory();
    if (null_check == kWithNullCheck) {
      // This may trap.
      result = result.CanLeaveCurrentFunction();
    }
    return result;
  }

  StructGetOp(OpIndex object, const wasm::StructType* type, uint32_t type_index,
              int field_index, bool is_signed, CheckForNull null_check)
      : Base(object),
        is_signed(is_signed),
        null_check(null_check),
        type(type),
        type_index(type_index),
        field_index(field_index) {}

  OpIndex object() const { return input(0); }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(&RepresentationFor(type->field(field_index)), 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {
    DCHECK_LT(field_index, type->field_count());
    DCHECK_IMPLIES(!is_signed, type->field(field_index).is_packed());
  }

  auto options() const {
    return std::tuple{type, type_index, field_index, is_signed, null_check};
  }
};

struct StructSetOp : FixedArityOperationT<2, StructSetOp> {
  CheckForNull null_check;
  const wasm::StructType* type;
  uint32_t type_index;
  int field_index;

  OpEffects Effects() const {
    OpEffects result =
        OpEffects()
            // This should not float above a protective null check.
            .CanDependOnChecks()
            .CanWriteMemory();
    if (null_check == kWithNullCheck) {
      // This may trap.
      result = result.CanLeaveCurrentFunction();
    }
    return result;
  }

  StructSetOp(OpIndex object, OpIndex value, const wasm::StructType* type,
              uint32_t type_index, int field_index, CheckForNull null_check)
      : Base(object, value),
        null_check(null_check),
        type(type),
        type_index(type_index),
        field_index(field_index) {}

  OpIndex object() const { return input(0); }
  OpIndex value() const { return input(1); }

  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    storage.resize(2);
    storage[0] = RegisterRepresentation::Tagged();
    storage[1] = RepresentationFor(type->field(field_index));
    return base::VectorOf(storage);
  }

  void Validate(const Graph& graph) const {
    DCHECK_LT(field_index, type->field_count());
  }

  auto options() const {
    return std::tuple{type, type_index, field_index, null_check};
  }
};

struct ArrayGetOp : FixedArityOperationT<2, ArrayGetOp> {
  bool is_signed;
  const wasm::ArrayType* array_type;

  // ArrayGetOp may never trap as it is always protected by a length check.
  static constexpr OpEffects effects =
      OpEffects()
          // This should not float above a protective null/length check.
          .CanDependOnChecks()
          .CanReadMemory();

  ArrayGetOp(OpIndex array, OpIndex index, const wasm::ArrayType* array_type,
             bool is_signed)
      : Base(array, index), is_signed(is_signed), array_type(array_type) {}

  OpIndex array() const { return input(0); }
  OpIndex index() const { return input(1); }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return base::VectorOf(&RepresentationFor(array_type->element_type()), 1);
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Word32()>();
  }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{array_type, is_signed}; }
  void PrintOptions(std::ostream& os) const;
};

struct ArraySetOp : FixedArityOperationT<3, ArraySetOp> {
  wasm::ValueType element_type;

  // ArraySetOp may never trap as it is always protected by a length check.
  static constexpr OpEffects effects =
      OpEffects()
          // This should not float above a protective null/length check.
          .CanDependOnChecks()
          .CanWriteMemory();

  ArraySetOp(OpIndex array, OpIndex index, OpIndex value,
             wasm::ValueType element_type)
      : Base(array, index, value), element_type(element_type) {}

  OpIndex array() const { return input(0); }
  OpIndex index() const { return input(1); }
  OpIndex value() const { return input(2); }

  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InitVectorOf(storage, {RegisterRepresentation::Tagged(),
                                  RegisterRepresentation::Word32(),
                                  RepresentationFor(element_type)});
  }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{element_type}; }
};

struct ArrayLengthOp : FixedArityOperationT<1, ArrayLengthOp> {
  CheckForNull null_check;

  OpEffects Effects() const {
    OpEffects result =
        OpEffects()
            // This should not float above a protective null check.
            .CanDependOnChecks()
            .CanReadMemory();
    if (null_check == kWithNullCheck) {
      // This may trap.
      result = result.CanLeaveCurrentFunction();
    }
    return result;
  }

  explicit ArrayLengthOp(OpIndex array, CheckForNull null_check)
      : Base(array), null_check(null_check) {}

  OpIndex array() const { return input(0); }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{null_check}; }
};

struct WasmAllocateArrayOp : FixedArityOperationT<2, WasmAllocateArrayOp> {
  static constexpr OpEffects effects =
      OpEffects().CanAllocate().CanLeaveCurrentFunction();

  const wasm::ArrayType* array_type;

  explicit WasmAllocateArrayOp(V<Map> rtt, V<Word32> length,
                               const wasm::ArrayType* array_type)
      : Base(rtt, length), array_type(array_type) {}

  V<Map> rtt() const { return Base::input(0); }
  V<Word32> length() const { return Base::input(1); }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged(),
                          MaybeRegisterRepresentation::Word32()>();
  }

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{array_type}; }
  void PrintOptions(std::ostream& os) const;
};

struct WasmAllocateStructOp : FixedArityOperationT<1, WasmAllocateStructOp> {
  static constexpr OpEffects effects =
      OpEffects().CanAllocate().CanLeaveCurrentFunction();

  const wasm::StructType* struct_type;

  explicit WasmAllocateStructOp(V<Map> rtt, const wasm::StructType* struct_type)
      : Base(rtt), struct_type(struct_type) {}

  V<Map> rtt() const { return Base::input(0); }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{struct_type}; }
};

struct WasmRefFuncOp : FixedArityOperationT<1, WasmRefFuncOp> {
  static constexpr OpEffects effects = OpEffects().CanAllocate();
  uint32_t function_index;

  explicit WasmRefFuncOp(V<Object> wasm_instance, uint32_t function_index)
      : Base(wasm_instance), function_index(function_index) {}

  OpIndex instance() const { return Base::input(0); }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{function_index}; }
};

// Casts a JavaScript string to a flattened wtf16 string.
// TODO(14108): Can we optimize stringref operations without adding this as a
// special operations?
struct StringAsWtf16Op : FixedArityOperationT<1, StringAsWtf16Op> {
  static constexpr OpEffects effects =
      OpEffects()
          // This should not float above a protective null/length check.
          .CanDependOnChecks()
          .CanReadMemory();

  explicit StringAsWtf16Op(V<Object> string) : Base(string) {}

  OpIndex string() const { return input(0); }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{}; }
};

// Takes a flattened string and extracts the first string pointer, the base
// offset and the character width shift.
struct StringPrepareForGetCodeUnitOp
    : FixedArityOperationT<1, StringPrepareForGetCodeUnitOp> {
  static constexpr OpEffects effects =
      OpEffects()
          // This should not float above a protective null/length check.
          .CanDependOnChecks();

  explicit StringPrepareForGetCodeUnitOp(V<Object> string) : Base(string) {}

  OpIndex string() const { return input(0); }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged(),
                     RegisterRepresentation::WordPtr(),
                     RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Tagged()>();
  }

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{}; }
};

struct Simd128ConstantOp : FixedArityOperationT<0, Simd128ConstantOp> {
  static constexpr uint8_t kZero[kSimd128Size] = {};
  uint8_t value[kSimd128Size];

  static constexpr OpEffects effects = OpEffects();

  explicit Simd128ConstantOp(const uint8_t incoming_value[kSimd128Size])
      : Base() {
    std::copy(incoming_value, incoming_value + kSimd128Size, value);
  }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd128()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  void Validate(const Graph& graph) const {
    // TODO(14108): Validate.
  }

  bool IsZero() const { return std::memcmp(kZero, value, kSimd128Size) == 0; }

  auto options() const { return std::tuple{value}; }
  void PrintOptions(std::ostream& os) const;
};

#define FOREACH_SIMD_128_BINARY_BASIC_OPCODE(V) \
  V(I8x16Eq)                                    \
  V(I8x16Ne)                                    \
  V(I8x16GtS)                                   \
  V(I8x16GtU)                                   \
  V(I8x16GeS)                                   \
  V(I8x16GeU)                                   \
  V(I16x8Eq)                                    \
  V(I16x8Ne)                                    \
  V(I16x8GtS)                                   \
  V(I16x8GtU)                                   \
  V(I16x8GeS)                                   \
  V(I16x8GeU)                                   \
  V(I32x4Eq)                                    \
  V(I32x4Ne)                                    \
  V(I32x4GtS)                                   \
  V(I32x4GtU)                                   \
  V(I32x4GeS)                                   \
  V(I32x4GeU)                                   \
  V(F32x4Eq)                                    \
  V(F32x4Ne)                                    \
  V(F32x4Lt)                                    \
  V(F32x4Le)                                    \
  V(F64x2Eq)                                    \
  V(F64x2Ne)                                    \
  V(F64x2Lt)                                    \
  V(F64x2Le)                                    \
  V(S128And)                                    \
  V(S128AndNot)                                 \
  V(S128Or)                                     \
  V(S128Xor)                                    \
  V(I8x16SConvertI16x8)                         \
  V(I8x16UConvertI16x8)                         \
  V(I8x16Add)                                   \
  V(I8x16AddSatS)                               \
  V(I8x16AddSatU)                               \
  V(I8x16Sub)                                   \
  V(I8x16SubSatS)                               \
  V(I8x16SubSatU)                               \
  V(I8x16MinS)                                  \
  V(I8x16MinU)                                  \
  V(I8x16MaxS)                                  \
  V(I8x16MaxU)                                  \
  V(I8x16RoundingAverageU)                      \
  V(I16x8Q15MulRSatS)                           \
  V(I16x8SConvertI32x4)                         \
  V(I16x8UConvertI32x4)                         \
  V(I16x8Add)                                   \
  V(I16x8AddSatS)                               \
  V(I16x8AddSatU)                               \
  V(I16x8Sub)                                   \
  V(I16x8SubSatS)                               \
  V(I16x8SubSatU)                               \
  V(I16x8Mul)                                   \
  V(I16x8MinS)                                  \
  V(I16x8MinU)                                  \
  V(I16x8MaxS)                                  \
  V(I16x8MaxU)                                  \
  V(I16x8RoundingAverageU)                      \
  V(I16x8ExtMulLowI8x16S)                       \
  V(I16x8ExtMulHighI8x16S)                      \
  V(I16x8ExtMulLowI8x16U)                       \
  V(I16x8ExtMulHighI8x16U)                      \
  V(I32x4Add)                                   \
  V(I32x4Sub)                                   \
  V(I32x4Mul)                                   \
  V(I32x4MinS)                                  \
  V(I32x4MinU)                                  \
  V(I32x4MaxS)                                  \
  V(I32x4MaxU)                                  \
  V(I32x4DotI16x8S)                             \
  V(I32x4ExtMulLowI16x8S)                       \
  V(I32x4ExtMulHighI16x8S)                      \
  V(I32x4ExtMulLowI16x8U)                       \
  V(I32x4ExtMulHighI16x8U)                      \
  V(I64x2Add)                                   \
  V(I64x2Sub)                                   \
  V(I64x2Mul)                                   \
  V(I64x2Eq)                                    \
  V(I64x2Ne)                                    \
  V(I64x2GtS)                                   \
  V(I64x2GeS)                                   \
  V(I64x2ExtMulLowI32x4S)                       \
  V(I64x2ExtMulHighI32x4S)                      \
  V(I64x2ExtMulLowI32x4U)                       \
  V(I64x2ExtMulHighI32x4U)                      \
  V(F32x4Add)                                   \
  V(F32x4Sub)                                   \
  V(F32x4Mul)                                   \
  V(F32x4Div)                                   \
  V(F32x4Min)                                   \
  V(F32x4Max)                                   \
  V(F32x4Pmin)                                  \
  V(F32x4Pmax)                                  \
  V(F64x2Add)                                   \
  V(F64x2Sub)                                   \
  V(F64x2Mul)                                   \
  V(F64x2Div)                                   \
  V(F64x2Min)                                   \
  V(F64x2Max)                                   \
  V(F64x2Pmin)                                  \
  V(F64x2Pmax)                                  \
  V(F32x4RelaxedMin)                            \
  V(F32x4RelaxedMax)                            \
  V(F64x2RelaxedMin)                            \
  V(F64x2RelaxedMax)                            \
  V(I16x8RelaxedQ15MulRS)                       \
  V(I16x8DotI8x16I7x16S)

#define FOREACH_SIMD_128_BINARY_SPECIAL_OPCODE(V) \
  V(I8x16Swizzle)                                 \
  V(I8x16RelaxedSwizzle)

#define FOREACH_SIMD_128_BINARY_OPCODE(V) \
  FOREACH_SIMD_128_BINARY_BASIC_OPCODE(V) \
  FOREACH_SIMD_128_BINARY_SPECIAL_OPCODE(V)

struct Simd128BinopOp : FixedArityOperationT<2, Simd128BinopOp> {
  enum class Kind : uint8_t {
#define DEFINE_KIND(kind) k##kind,
    FOREACH_SIMD_128_BINARY_OPCODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  Kind kind;

  static bool IsCommutative(Kind kind) {
    switch (kind) {
      // TODO(14108): Explicitly list all commutative SIMD operations.
      case Kind::kI64x2Add:
      case Kind::kI32x4Add:
      case Kind::kI16x8Add:
      case Kind::kI8x16Add:
        return true;
      default:
        return false;
    }
  }

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd128()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Simd128(),
                          RegisterRepresentation::Simd128()>();
  }

  Simd128BinopOp(OpIndex left, OpIndex right, Kind kind)
      : Base(left, right), kind(kind) {}

  OpIndex left() const { return input(0); }
  OpIndex right() const { return input(1); }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           Simd128BinopOp::Kind kind);

#define FOREACH_SIMD_128_UNARY_NON_OPTIONAL_OPCODE(V) \
  V(S128Not)                                          \
  V(F32x4DemoteF64x2Zero)                             \
  V(F64x2PromoteLowF32x4)                             \
  V(I8x16Abs)                                         \
  V(I8x16Neg)                                         \
  V(I8x16Popcnt)                                      \
  V(I16x8ExtAddPairwiseI8x16S)                        \
  V(I16x8ExtAddPairwiseI8x16U)                        \
  V(I32x4ExtAddPairwiseI16x8S)                        \
  V(I32x4ExtAddPairwiseI16x8U)                        \
  V(I16x8Abs)                                         \
  V(I16x8Neg)                                         \
  V(I16x8SConvertI8x16Low)                            \
  V(I16x8SConvertI8x16High)                           \
  V(I16x8UConvertI8x16Low)                            \
  V(I16x8UConvertI8x16High)                           \
  V(I32x4Abs)                                         \
  V(I32x4Neg)                                         \
  V(I32x4SConvertI16x8Low)                            \
  V(I32x4SConvertI16x8High)                           \
  V(I32x4UConvertI16x8Low)                            \
  V(I32x4UConvertI16x8High)                           \
  V(I64x2Abs)                                         \
  V(I64x2Neg)                                         \
  V(I64x2SConvertI32x4Low)                            \
  V(I64x2SConvertI32x4High)                           \
  V(I64x2UConvertI32x4Low)                            \
  V(I64x2UConvertI32x4High)                           \
  V(F32x4Abs)                                         \
  V(F32x4Neg)                                         \
  V(F32x4Sqrt)                                        \
  V(F64x2Abs)                                         \
  V(F64x2Neg)                                         \
  V(F64x2Sqrt)                                        \
  V(I32x4SConvertF32x4)                               \
  V(I32x4UConvertF32x4)                               \
  V(F32x4SConvertI32x4)                               \
  V(F32x4UConvertI32x4)                               \
  V(I32x4TruncSatF64x2SZero)                          \
  V(I32x4TruncSatF64x2UZero)                          \
  V(F64x2ConvertLowI32x4S)                            \
  V(F64x2ConvertLowI32x4U)                            \
  V(I32x4RelaxedTruncF32x4S)                          \
  V(I32x4RelaxedTruncF32x4U)                          \
  V(I32x4RelaxedTruncF64x2SZero)                      \
  V(I32x4RelaxedTruncF64x2UZero)

#define FOREACH_SIMD_128_UNARY_OPTIONAL_OPCODE(V)                             \
  V(F32x4Ceil)                                                                \
  V(F32x4Floor)                                                               \
  V(F32x4Trunc)                                                               \
  V(F32x4NearestInt)                                                          \
  V(F64x2Ceil)                                                                \
  V(F64x2Floor)                                                               \
  V(F64x2Trunc)                                                               \
  V(F64x2NearestInt)                                                          \
  /* TODO(mliedtke): Rename to ReverseBytes once the naming is decoupled from \
   * Turbofan naming. */                                                      \
  V(Simd128ReverseBytes)

#define FOREACH_SIMD_128_UNARY_OPCODE(V)        \
  FOREACH_SIMD_128_UNARY_NON_OPTIONAL_OPCODE(V) \
  FOREACH_SIMD_128_UNARY_OPTIONAL_OPCODE(V)

struct Simd128UnaryOp : FixedArityOperationT<1, Simd128UnaryOp> {
  enum class Kind : uint8_t {
#define DEFINE_KIND(kind) k##kind,
    FOREACH_SIMD_128_UNARY_OPCODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  Kind kind;

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd128()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Simd128()>();
  }

  Simd128UnaryOp(OpIndex input, Kind kind) : Base(input), kind(kind) {}

  OpIndex input() const { return Base::input(0); }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           Simd128UnaryOp::Kind kind);

#define FOREACH_SIMD_128_SHIFT_OPCODE(V) \
  V(I8x16Shl)                            \
  V(I8x16ShrS)                           \
  V(I8x16ShrU)                           \
  V(I16x8Shl)                            \
  V(I16x8ShrS)                           \
  V(I16x8ShrU)                           \
  V(I32x4Shl)                            \
  V(I32x4ShrS)                           \
  V(I32x4ShrU)                           \
  V(I64x2Shl)                            \
  V(I64x2ShrS)                           \
  V(I64x2ShrU)

struct Simd128ShiftOp : FixedArityOperationT<2, Simd128ShiftOp> {
  enum class Kind : uint8_t {
#define DEFINE_KIND(kind) k##kind,
    FOREACH_SIMD_128_SHIFT_OPCODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  Kind kind;

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd128()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Simd128(),
                          RegisterRepresentation::Word32()>();
  }

  Simd128ShiftOp(OpIndex input, OpIndex shift, Kind kind)
      : Base(input, shift), kind(kind) {}

  OpIndex input() const { return Base::input(0); }
  OpIndex shift() const { return Base::input(1); }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           Simd128ShiftOp::Kind kind);

#define FOREACH_SIMD_128_TEST_OPCODE(V) \
  V(V128AnyTrue)                        \
  V(I8x16AllTrue)                       \
  V(I8x16BitMask)                       \
  V(I16x8AllTrue)                       \
  V(I16x8BitMask)                       \
  V(I32x4AllTrue)                       \
  V(I32x4BitMask)                       \
  V(I64x2AllTrue)                       \
  V(I64x2BitMask)

struct Simd128TestOp : FixedArityOperationT<1, Simd128TestOp> {
  enum class Kind : uint8_t {
#define DEFINE_KIND(kind) k##kind,
    FOREACH_SIMD_128_TEST_OPCODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  Kind kind;

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Word32()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Simd128()>();
  }

  Simd128TestOp(OpIndex input, Kind kind) : Base(input), kind(kind) {}

  OpIndex input() const { return Base::input(0); }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           Simd128TestOp::Kind kind);

#define FOREACH_SIMD_128_SPLAT_OPCODE(V) \
  V(I8x16)                               \
  V(I16x8)                               \
  V(I32x4)                               \
  V(I64x2)                               \
  V(F32x4)                               \
  V(F64x2)

struct Simd128SplatOp : FixedArityOperationT<1, Simd128SplatOp> {
  enum class Kind : uint8_t {
#define DEFINE_KIND(kind) k##kind,
    FOREACH_SIMD_128_SPLAT_OPCODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  Kind kind;

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd128()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    switch (kind) {
      case Kind::kI8x16:
      case Kind::kI16x8:
      case Kind::kI32x4:
        return MaybeRepVector<RegisterRepresentation::Word32()>();
      case Kind::kI64x2:
        return MaybeRepVector<RegisterRepresentation::Word64()>();
      case Kind::kF32x4:
        return MaybeRepVector<RegisterRepresentation::Float32()>();
      case Kind::kF64x2:
        return MaybeRepVector<RegisterRepresentation::Float64()>();
    }
  }

  Simd128SplatOp(OpIndex input, Kind kind) : Base(input), kind(kind) {}

  OpIndex input() const { return Base::input(0); }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           Simd128SplatOp::Kind kind);

#define FOREACH_SIMD_128_TERNARY_MASK_OPCODE(V) \
  V(S128Select)                                 \
  V(I8x16RelaxedLaneSelect)                     \
  V(I16x8RelaxedLaneSelect)                     \
  V(I32x4RelaxedLaneSelect)                     \
  V(I64x2RelaxedLaneSelect)

#define FOREACH_SIMD_128_TERNARY_OTHER_OPCODE(V) \
  V(F32x4Qfma)                                   \
  V(F32x4Qfms)                                   \
  V(F64x2Qfma)                                   \
  V(F64x2Qfms)                                   \
  V(I32x4DotI8x16I7x16AddS)

#define FOREACH_SIMD_128_TERNARY_OPCODE(V) \
  FOREACH_SIMD_128_TERNARY_MASK_OPCODE(V)  \
  FOREACH_SIMD_128_TERNARY_OTHER_OPCODE(V)

struct Simd128TernaryOp : FixedArityOperationT<3, Simd128TernaryOp> {
  enum class Kind : uint8_t {
#define DEFINE_KIND(kind) k##kind,
    FOREACH_SIMD_128_TERNARY_OPCODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  Kind kind;

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd128()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Simd128(),
                          RegisterRepresentation::Simd128(),
                          RegisterRepresentation::Simd128()>();
  }

  Simd128TernaryOp(OpIndex first, OpIndex second, OpIndex third, Kind kind)
      : Base(first, second, third), kind(kind) {}

  OpIndex first() const { return input(0); }
  OpIndex second() const { return input(1); }
  OpIndex third() const { return input(2); }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           Simd128TernaryOp::Kind kind);

struct Simd128ExtractLaneOp : FixedArityOperationT<1, Simd128ExtractLaneOp> {
  enum class Kind : uint8_t {
    kI8x16S,
    kI8x16U,
    kI16x8S,
    kI16x8U,
    kI32x4,
    kI64x2,
    kF32x4,
    kF64x2,
  };

  Kind kind;
  uint8_t lane;

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    switch (kind) {
      case Kind::kI8x16S:
      case Kind::kI8x16U:
      case Kind::kI16x8S:
      case Kind::kI16x8U:
      case Kind::kI32x4:
        return RepVector<RegisterRepresentation::Word32()>();
      case Kind::kI64x2:
        return RepVector<RegisterRepresentation::Word64()>();
      case Kind::kF32x4:
        return RepVector<RegisterRepresentation::Float32()>();
      case Kind::kF64x2:
        return RepVector<RegisterRepresentation::Float64()>();
    }
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Simd128()>();
  }

  Simd128ExtractLaneOp(OpIndex input, Kind kind, uint8_t lane)
      : Base(input), kind(kind), lane(lane) {}

  OpIndex input() const { return Base::input(0); }

  void Validate(const Graph& graph) const {
#if DEBUG
    uint8_t lane_count;
    switch (kind) {
      case Kind::kI8x16S:
      case Kind::kI8x16U:
        lane_count = 16;
        break;
      case Kind::kI16x8S:
      case Kind::kI16x8U:
        lane_count = 8;
        break;
      case Kind::kI32x4:
      case Kind::kF32x4:
        lane_count = 4;
        break;
      case Kind::kI64x2:
      case Kind::kF64x2:
        lane_count = 2;
        break;
    }
    DCHECK_LT(lane, lane_count);
#endif
  }

  auto options() const { return std::tuple{kind, lane}; }
  void PrintOptions(std::ostream& os) const;
};

struct Simd128ReplaceLaneOp : FixedArityOperationT<2, Simd128ReplaceLaneOp> {
  enum class Kind : uint8_t {
    kI8x16,
    kI16x8,
    kI32x4,
    kI64x2,
    kF32x4,
    kF64x2,
  };

  Kind kind;
  uint8_t lane;

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd128()>();
  }
  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return InitVectorOf(storage,
                        {RegisterRepresentation::Simd128(), new_lane_rep()});
  }

  Simd128ReplaceLaneOp(OpIndex into, OpIndex new_lane, Kind kind, uint8_t lane)
      : Base(into, new_lane), kind(kind), lane(lane) {}

  OpIndex into() const { return Base::input(0); }
  OpIndex new_lane() const { return Base::input(1); }

  void Validate(const Graph& graph) const {
#if DEBUG
    uint8_t lane_count;
    switch (kind) {
      case Kind::kI8x16:
        lane_count = 16;
        break;
      case Kind::kI16x8:
        lane_count = 8;
        break;
      case Kind::kI32x4:
      case Kind::kF32x4:
        lane_count = 4;
        break;
      case Kind::kI64x2:
      case Kind::kF64x2:
        lane_count = 2;
        break;
    }
    DCHECK_LT(lane, lane_count);
#endif
  }

  auto options() const { return std::tuple{kind, lane}; }
  void PrintOptions(std::ostream& os) const;

  RegisterRepresentation new_lane_rep() const {
    switch (kind) {
      case Kind::kI8x16:
      case Kind::kI16x8:
      case Kind::kI32x4:
        return RegisterRepresentation::Word32();
      case Kind::kI64x2:
        return RegisterRepresentation::Word64();
      case Kind::kF32x4:
        return RegisterRepresentation::Float32();
      case Kind::kF64x2:
        return RegisterRepresentation::Float64();
    }
  }
};

// If `mode` is `kLoad`, load a value from `base() + index() + offset`, whose
// size is determinded by `lane_kind`, and return the Simd128 `value()` with
// the lane specified by `lane_kind` and `lane` replaced with the loaded value.
// If `mode` is `kStore`, extract the lane specified by `lane` with size
// `lane_kind` from `value()`, and store it to `base() + index() + offset`.
struct Simd128LaneMemoryOp : FixedArityOperationT<3, Simd128LaneMemoryOp> {
  enum class Mode : bool { kLoad, kStore };
  using Kind = LoadOp::Kind;
  // The values encode the element_size_log2.
  enum class LaneKind : uint8_t { k8 = 0, k16 = 1, k32 = 2, k64 = 3 };

  Mode mode;
  Kind kind;
  LaneKind lane_kind;
  uint8_t lane;
  int offset;

  OpEffects Effects() const {
    OpEffects effects = mode == Mode::kLoad ? OpEffects().CanReadMemory()
                                            : OpEffects().CanWriteMemory();
    effects = effects.CanDependOnChecks();
    if (kind.with_trap_handler) effects = effects.CanLeaveCurrentFunction();
    return effects;
  }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return mode == Mode::kLoad ? RepVector<RegisterRepresentation::Simd128()>()
                               : RepVector<>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::WordPtr(),
                          RegisterRepresentation::WordPtr(),
                          RegisterRepresentation::Simd128()>();
  }

  Simd128LaneMemoryOp(OpIndex base, OpIndex index, OpIndex value, Mode mode,
                      Kind kind, LaneKind lane_kind, uint8_t lane, int offset)
      : Base(base, index, value),
        mode(mode),
        kind(kind),
        lane_kind(lane_kind),
        lane(lane),
        offset(offset) {}

  OpIndex base() const { return input(0); }
  OpIndex index() const { return input(1); }
  OpIndex value() const { return input(2); }
  uint8_t lane_size() const { return 1 << static_cast<uint8_t>(lane_kind); }

  void Validate(const Graph& graph) {
    DCHECK(!kind.tagged_base);
#if DEBUG
    uint8_t lane_count;
    switch (lane_kind) {
      case LaneKind::k8:
        lane_count = 16;
        break;
      case LaneKind::k16:
        lane_count = 8;
        break;
      case LaneKind::k32:
        lane_count = 4;
        break;
      case LaneKind::k64:
        lane_count = 2;
        break;
    }
    DCHECK_LT(lane, lane_count);
#endif
  }

  auto options() const {
    return std::tuple{mode, kind, lane_kind, lane, offset};
  }
  void PrintOptions(std::ostream& os) const;
};

#define FOREACH_SIMD_128_LOAD_TRANSFORM_OPCODE(V) \
  V(8x8S)                                         \
  V(8x8U)                                         \
  V(16x4S)                                        \
  V(16x4U)                                        \
  V(32x2S)                                        \
  V(32x2U)                                        \
  V(8Splat)                                       \
  V(16Splat)                                      \
  V(32Splat)                                      \
  V(64Splat)                                      \
  V(32Zero)                                       \
  V(64Zero)

// Load a value from `base() + index() + offset`, whose size is determinded by
// `transform_kind`, and generate a Simd128 value as follows:
// - From 8x8S to 32x2U (extend kinds), the loaded value has size 8. It is
//   interpreted as a vector of values according to the size of the kind, which
//   populate the even lanes of the generated value. The odd lanes are zero- or
//   sign-extended according to the kind.
// - For splat kinds, the loaded value's size is determined by the kind, all
//   lanes of the generated value are populated with the loaded value.
// - For "zero" kinds, the loaded value's size is determined by the kind, and
//   the generated value zero-extends the loaded value.
struct Simd128LoadTransformOp
    : FixedArityOperationT<2, Simd128LoadTransformOp> {
  using LoadKind = LoadOp::Kind;
  enum class TransformKind : uint8_t {
#define DEFINE_KIND(kind) k##kind,
    FOREACH_SIMD_128_LOAD_TRANSFORM_OPCODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  LoadKind load_kind;
  TransformKind transform_kind;
  int offset;

  OpEffects Effects() const {
    OpEffects effects = OpEffects().CanReadMemory().CanDependOnChecks();
    if (load_kind.with_trap_handler) {
      effects = effects.CanLeaveCurrentFunction();
    }
    return effects;
  }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd128()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::WordPtr(),
                          RegisterRepresentation::WordPtr()>();
  }

  Simd128LoadTransformOp(OpIndex base, OpIndex index, LoadKind load_kind,
                         TransformKind transform_kind, int offset)
      : Base(base, index),
        load_kind(load_kind),
        transform_kind(transform_kind),
        offset(offset) {}

  OpIndex base() const { return input(0); }
  OpIndex index() const { return input(1); }

  void Validate(const Graph& graph) { DCHECK(!load_kind.tagged_base); }

  auto options() const { return std::tuple{load_kind, transform_kind, offset}; }
  void PrintOptions(std::ostream& os) const;
};

// Takes two Simd128 inputs and generates a Simd128 value. The 8-bit lanes of
// both inputs are numbered 0-31, and each output 8-bit lane is selected from
// among the input lanes according to `shuffle`.
struct Simd128ShuffleOp : FixedArityOperationT<2, Simd128ShuffleOp> {
  uint8_t shuffle[kSimd128Size];

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd128()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Simd128(),
                          RegisterRepresentation::Simd128()>();
  }

  Simd128ShuffleOp(OpIndex left, OpIndex right,
                   const uint8_t incoming_shuffle[kSimd128Size])
      : Base(left, right) {
    std::copy(incoming_shuffle, incoming_shuffle + kSimd128Size, shuffle);
  }

  OpIndex left() const { return input(0); }
  OpIndex right() const { return input(1); }

  void Validate(const Graph& graph) {
#if DEBUG
    constexpr uint8_t kNumberOfLanesForShuffle = 32;
    for (uint8_t index : shuffle) {
      DCHECK_LT(index, kNumberOfLanesForShuffle);
    }
#endif
  }

  auto options() const { return std::tuple{shuffle}; }
  void PrintOptions(std::ostream& os) const;
};

#if V8_ENABLE_WASM_SIMD256_REVEC
struct Simd256Extract128LaneOp
    : FixedArityOperationT<1, Simd256Extract128LaneOp> {
  uint8_t lane;

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd128()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Simd256()>();
  }

  Simd256Extract128LaneOp(OpIndex input, uint8_t lane)
      : Base(input), lane(lane) {}

  OpIndex input() const { return Base::input(0); }

  void Validate(const Graph& graph) const {
#if DEBUG
    DCHECK_LT(lane, 2);
#endif
  }

  auto options() const { return std::tuple{lane}; }
};

#define FOREACH_SIMD_256_LOAD_TRANSFORM_OPCODE(V) \
  V(8x16S)                                        \
  V(8x16U)                                        \
  V(16x8S)                                        \
  V(16x8U)                                        \
  V(32x4S)                                        \
  V(32x4U)                                        \
  V(8Splat)                                       \
  V(16Splat)                                      \
  V(32Splat)                                      \
  V(64Splat)

struct Simd256LoadTransformOp
    : FixedArityOperationT<2, Simd256LoadTransformOp> {
  using LoadKind = LoadOp::Kind;
  enum class TransformKind : uint8_t {
#define DEFINE_KIND(kind) k##kind,
    FOREACH_SIMD_256_LOAD_TRANSFORM_OPCODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  LoadKind load_kind;
  TransformKind transform_kind;
  int offset;

  OpEffects Effects() const {
    OpEffects effects = OpEffects().CanReadMemory().CanDependOnChecks();
    if (load_kind.with_trap_handler) {
      effects = effects.CanLeaveCurrentFunction();
    }
    return effects;
  }

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd256()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::WordPtr(),
                          RegisterRepresentation::WordPtr()>();
  }

  Simd256LoadTransformOp(V<WordPtr> base, V<WordPtr> index, LoadKind load_kind,
                         TransformKind transform_kind, int offset)
      : Base(base, index),
        load_kind(load_kind),
        transform_kind(transform_kind),
        offset(offset) {}

  V<WordPtr> base() const { return input(0); }
  V<WordPtr> index() const { return input(1); }

  void Validate(const Graph& graph) { DCHECK(!load_kind.tagged_base); }

  auto options() const { return std::tuple{load_kind, transform_kind, offset}; }
  void PrintOptions(std::ostream& os) const;
};

#define FOREACH_SIMD_256_UNARY_OPCODE(V) \
  V(S256Not)                             \
  V(I8x32Abs)                            \
  V(I8x32Neg)                            \
  V(I16x16ExtAddPairwiseI8x32S)          \
  V(I16x16ExtAddPairwiseI8x32U)          \
  V(I32x8ExtAddPairwiseI16x16S)          \
  V(I32x8ExtAddPairwiseI16x16U)          \
  V(I16x16Abs)                           \
  V(I16x16Neg)                           \
  V(I32x8Abs)                            \
  V(I32x8Neg)                            \
  V(F32x8Abs)                            \
  V(F32x8Neg)                            \
  V(F32x8Sqrt)                           \
  V(F64x4Sqrt)                           \
  V(I32x8UConvertF32x8)                  \
  V(F32x8UConvertI32x8)

struct Simd256UnaryOp : FixedArityOperationT<1, Simd256UnaryOp> {
  enum class Kind : uint8_t {
#define DEFINE_KIND(kind) k##kind,
    FOREACH_SIMD_256_UNARY_OPCODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  Kind kind;

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd256()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Simd256()>();
  }

  Simd256UnaryOp(OpIndex input, Kind kind) : Base(input), kind(kind) {}

  OpIndex input() const { return Base::input(0); }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{kind}; }
};
std::ostream& operator<<(std::ostream& os, Simd256UnaryOp::Kind kind);

#define FOREACH_SIMD_256_BINARY_BASIC_OPCODE(V) \
  V(I8x32Eq)                                    \
  V(I8x32Ne)                                    \
  V(I8x32GtS)                                   \
  V(I8x32GtU)                                   \
  V(I8x32GeS)                                   \
  V(I8x32GeU)                                   \
  V(I16x16Eq)                                   \
  V(I16x16Ne)                                   \
  V(I16x16GtS)                                  \
  V(I16x16GtU)                                  \
  V(I16x16GeS)                                  \
  V(I16x16GeU)                                  \
  V(I32x8Eq)                                    \
  V(I32x8Ne)                                    \
  V(I32x8GtS)                                   \
  V(I32x8GtU)                                   \
  V(I32x8GeS)                                   \
  V(I32x8GeU)                                   \
  V(F32x8Eq)                                    \
  V(F32x8Ne)                                    \
  V(F32x8Lt)                                    \
  V(F32x8Le)                                    \
  V(F64x4Eq)                                    \
  V(F64x4Ne)                                    \
  V(F64x4Lt)                                    \
  V(F64x4Le)                                    \
  V(S256And)                                    \
  V(S256AndNot)                                 \
  V(S256Or)                                     \
  V(S256Xor)                                    \
  V(I8x32SConvertI16x16)                        \
  V(I8x32UConvertI16x16)                        \
  V(I8x32Add)                                   \
  V(I8x32AddSatS)                               \
  V(I8x32AddSatU)                               \
  V(I8x32Sub)                                   \
  V(I8x32SubSatS)                               \
  V(I8x32SubSatU)                               \
  V(I8x32MinS)                                  \
  V(I8x32MinU)                                  \
  V(I8x32MaxS)                                  \
  V(I8x32MaxU)                                  \
  V(I8x32RoundingAverageU)                      \
  V(I16x16SConvertI32x8)                        \
  V(I16x16UConvertI32x8)                        \
  V(I16x16Add)                                  \
  V(I16x16AddSatS)                              \
  V(I16x16AddSatU)                              \
  V(I16x16Sub)                                  \
  V(I16x16SubSatS)                              \
  V(I16x16SubSatU)                              \
  V(I16x16Mul)                                  \
  V(I16x16MinS)                                 \
  V(I16x16MinU)                                 \
  V(I16x16MaxS)                                 \
  V(I16x16MaxU)                                 \
  V(I16x16RoundingAverageU)                     \
  V(I32x8Add)                                   \
  V(I32x8Sub)                                   \
  V(I32x8Mul)                                   \
  V(I32x8MinS)                                  \
  V(I32x8MinU)                                  \
  V(I32x8MaxS)                                  \
  V(I32x8MaxU)                                  \
  V(I32x8DotI16x16S)                            \
  V(I64x4Add)                                   \
  V(I64x4Sub)                                   \
  V(I64x4Mul)                                   \
  V(I64x4Eq)                                    \
  V(I64x4Ne)                                    \
  V(I64x4GtS)                                   \
  V(I64x4GeS)                                   \
  V(F32x8Add)                                   \
  V(F32x8Sub)                                   \
  V(F32x8Mul)                                   \
  V(F32x8Div)                                   \
  V(F32x8Min)                                   \
  V(F32x8Max)                                   \
  V(F32x8Pmin)                                  \
  V(F32x8Pmax)                                  \
  V(F64x4Add)                                   \
  V(F64x4Sub)                                   \
  V(F64x4Mul)                                   \
  V(F64x4Div)                                   \
  V(F64x4Min)                                   \
  V(F64x4Max)                                   \
  V(F64x4Pmin)                                  \
  V(F64x4Pmax)

#define FOREACH_SIMD_256_BINARY_OPCODE(V) \
  FOREACH_SIMD_256_BINARY_BASIC_OPCODE(V)

struct Simd256BinopOp : FixedArityOperationT<2, Simd256BinopOp> {
  enum class Kind : uint8_t {
#define DEFINE_KIND(kind) k##kind,
    FOREACH_SIMD_256_BINARY_OPCODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  Kind kind;

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd256()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Simd256(),
                          RegisterRepresentation::Simd256()>();
  }

  Simd256BinopOp(OpIndex left, OpIndex right, Kind kind)
      : Base(left, right), kind(kind) {}

  OpIndex left() const { return input(0); }
  OpIndex right() const { return input(1); }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{kind}; }
};

std::ostream& operator<<(std::ostream& os, Simd256BinopOp::Kind kind);

#define FOREACH_SIMD_256_SHIFT_OPCODE(V) \
  V(I16x16Shl)                           \
  V(I16x16ShrS)                          \
  V(I16x16ShrU)                          \
  V(I32x8Shl)                            \
  V(I32x8ShrS)                           \
  V(I32x8ShrU)                           \
  V(I64x4Shl)                            \
  V(I64x4ShrU)

struct Simd256ShiftOp : FixedArityOperationT<2, Simd256ShiftOp> {
  enum class Kind : uint8_t {
#define DEFINE_KIND(kind) k##kind,
    FOREACH_SIMD_256_SHIFT_OPCODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  Kind kind;

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd256()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Simd256(),
                          RegisterRepresentation::Word32()>();
  }

  Simd256ShiftOp(V<Simd256> input, V<Word32> shift, Kind kind)
      : Base(input, shift), kind(kind) {}

  V<Simd256> input() const { return Base::input(0); }
  V<Word32> shift() const { return Base::input(1); }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{kind}; }
};
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           Simd256ShiftOp::Kind kind);

#define FOREACH_SIMD_256_TERNARY_OPCODE(V) V(S256Select)
struct Simd256TernaryOp : FixedArityOperationT<3, Simd256TernaryOp> {
  enum class Kind : uint8_t {
#define DEFINE_KIND(kind) k##kind,
    FOREACH_SIMD_256_TERNARY_OPCODE(DEFINE_KIND)
#undef DEFINE_KIND
  };

  Kind kind;

  static constexpr OpEffects effects = OpEffects();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Simd256()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<RegisterRepresentation::Simd256(),
                          RegisterRepresentation::Simd256(),
                          RegisterRepresentation::Simd256()>();
  }

  Simd256TernaryOp(V<Simd256> first, V<Simd256> second, V<Simd256> third,
                   Kind kind)
      : Base(first, second, third), kind(kind) {}

  V<Simd256> first() const { return input(0); }
  V<Simd256> second() const { return input(1); }
  V<Simd256> third() const { return input(2); }

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{kind}; }
};
std::ostream& operator<<(std::ostream& os, Simd256TernaryOp::Kind kind);
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

struct LoadStackPointerOp : FixedArityOperationT<0, LoadStackPointerOp> {
  // TODO(nicohartmann@): Review effects.
  static constexpr OpEffects effects = OpEffects().CanReadMemory();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::WordPtr()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{}; }
};

struct SetStackPointerOp : FixedArityOperationT<1, SetStackPointerOp> {
  wasm::FPRelativeScope fp_scope;

  // TODO(nicohartmann@): Review effects.
  static constexpr OpEffects effects = OpEffects().CanCallAnything();

  OpIndex value() const { return Base::input(0); }

  SetStackPointerOp(OpIndex value, wasm::FPRelativeScope fp_scope)
      : Base(value), fp_scope(fp_scope) {}

  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::WordPtr()>();
  }

  void Validate(const Graph& graph) const {}
  auto options() const { return std::tuple{fp_scope}; }
};

#endif  // V8_ENABLE_WEBASSEMBLY

#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
struct GetContinuationPreservedEmbedderDataOp
    : FixedArityOperationT<0, GetContinuationPreservedEmbedderDataOp> {
  static constexpr OpEffects effects = OpEffects().CanReadOffHeapMemory();

  base::Vector<const RegisterRepresentation> outputs_rep() const {
    return RepVector<RegisterRepresentation::Tagged()>();
  }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return {};
  }

  GetContinuationPreservedEmbedderDataOp() : Base() {}

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{}; }
};

struct SetContinuationPreservedEmbedderDataOp
    : FixedArityOperationT<1, SetContinuationPreservedEmbedderDataOp> {
  static constexpr OpEffects effects = OpEffects().CanWriteOffHeapMemory();

  base::Vector<const RegisterRepresentation> outputs_rep() const { return {}; }

  base::Vector<const MaybeRegisterRepresentation> inputs_rep(
      ZoneVector<MaybeRegisterRepresentation>& storage) const {
    return MaybeRepVector<MaybeRegisterRepresentation::Tagged()>();
  }

  explicit SetContinuationPreservedEmbedderDataOp(V<Object> value)
      : Base(value) {}

  void Validate(const Graph& graph) const {}

  auto options() const { return std::tuple{}; }
};
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA

#define OPERATION_EFFECTS_CASE(Name) Name##Op::EffectsIfStatic(),
static constexpr base::Optional<OpEffects>
    kOperationEffectsTable[kNumberOfOpcodes] = {
        TURBOSHAFT_OPERATION_LIST(OPERATION_EFFECTS_CASE)};
#undef OPERATION_EFFECTS_CASE

template <class Op>
const Opcode OperationT<Op>::opcode = operation_to_opcode_map<Op>::value;

template <Opcode opcode>
struct opcode_to_operation_map {};

#define OPERATION_OPCODE_MAP_CASE(Name)             \
  template <>                                       \
  struct opcode_to_operation_map<Opcode::k##Name> { \
    using Op = Name##Op;                            \
  };
TURBOSHAFT_OPERATION_LIST(OPERATION_OPCODE_MAP_CASE)
#undef OPERATION_OPCODE_MAP_CASE

template <class Op, class = void>
struct static_operation_input_count : std::integral_constant<uint32_t, 0> {};
template <class Op>
struct static_operation_input_count<Op, std::void_t<decltype(Op::inputs)>>
    : std::integral_constant<uint32_t, sizeof(Op::inputs) / sizeof(OpIndex)> {};
constexpr size_t kOperationSizeTable[kNumberOfOpcodes] = {
#define OPERATION_SIZE(Name) sizeof(Name##Op),
    TURBOSHAFT_OPERATION_LIST(OPERATION_SIZE)
#undef OPERATION_SIZE
};
constexpr size_t kOperationSizeDividedBySizeofOpIndexTable[kNumberOfOpcodes] = {
#define OPERATION_SIZE(Name) (sizeof(Name##Op) / sizeof(OpIndex)),
    TURBOSHAFT_OPERATION_LIST(OPERATION_SIZE)
#undef OPERATION_SIZE
};

inline base::Vector<const OpIndex> Operation::inputs() const {
  // This is actually undefined behavior, since we use the `this` pointer to
  // access an adjacent object.
  const OpIndex* ptr = reinterpret_cast<const OpIndex*>(
      reinterpret_cast<const char*>(this) +
      kOperationSizeTable[OpcodeIndex(opcode)]);
  return {ptr, input_count};
}

inline OpEffects Operation::Effects() const {
  if (auto prop = kOperationEffectsTable[OpcodeIndex(opcode)]) {
    return *prop;
  }
  switch (opcode) {
    case Opcode::kLoad:
      return Cast<LoadOp>().Effects();
    case Opcode::kStore:
      return Cast<StoreOp>().Effects();
    case Opcode::kCall:
      return Cast<CallOp>().Effects();
    case Opcode::kTaggedBitcast:
      return Cast<TaggedBitcastOp>().Effects();
    case Opcode::kAtomicRMW:
      return Cast<AtomicRMWOp>().Effects();
    case Opcode::kAtomicWord32Pair:
      return Cast<AtomicWord32PairOp>().Effects();
#if V8_ENABLE_WEBASSEMBLY
    case Opcode::kStructGet:
      return Cast<StructGetOp>().Effects();
    case Opcode::kStructSet:
      return Cast<StructSetOp>().Effects();
    case Opcode::kArrayLength:
      return Cast<ArrayLengthOp>().Effects();
    case Opcode::kSimd128LaneMemory:
      return Cast<Simd128LaneMemoryOp>().Effects();
    case Opcode::kSimd128LoadTransform:
      return Cast<Simd128LoadTransformOp>().Effects();
#if V8_ENABLE_WASM_SIMD256_REVEC
    case Opcode::kSimd256LoadTransform:
      return Cast<Simd256LoadTransformOp>().Effects();
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif
    default:
      UNREACHABLE();
  }
}

// static
inline size_t Operation::StorageSlotCount(Opcode opcode, size_t input_count) {
  size_t size = kOperationSizeDividedBySizeofOpIndexTable[OpcodeIndex(opcode)];
  constexpr size_t r = sizeof(OperationStorageSlot) / sizeof(OpIndex);
  static_assert(sizeof(OperationStorageSlot) % sizeof(OpIndex) == 0);
  return std::max<size_t>(2, (r - 1 + size + input_count) / r);
}

V8_INLINE bool CanBeUsedAsInput(const Operation& op) {
  return op.Is<FrameStateOp>() || op.outputs_rep().size() > 0;
}

inline base::Vector<const RegisterRepresentation> Operation::outputs_rep()
    const {
  switch (opcode) {
#define CASE(type)                         \
  case Opcode::k##type: {                  \
    const type##Op& op = Cast<type##Op>(); \
    return op.outputs_rep();               \
  }
    TURBOSHAFT_OPERATION_LIST(CASE)
#undef CASE
  }
}

inline base::Vector<const MaybeRegisterRepresentation> Operation::inputs_rep(
    ZoneVector<MaybeRegisterRepresentation>& storage) const {
  switch (opcode) {
#define CASE(type)                         \
  case Opcode::k##type: {                  \
    const type##Op& op = Cast<type##Op>(); \
    return op.inputs_rep(storage);         \
  }
    TURBOSHAFT_OPERATION_LIST(CASE)
#undef CASE
  }
}

bool IsUnlikelySuccessor(const Block* block, const Block* successor,
                         const Graph& graph);

// All operations whose `saturated_use_count` is 0 are unused and can be
// skipped. Analyzers modify the input graph in-place when they want to mark
// some Operations as removeable. In order to make that work for operations that
// have no uses such as Goto and Branch, all operations that have the property
// `IsRequiredWhenUnused()` have a non-zero `saturated_use_count`.
V8_EXPORT_PRIVATE V8_INLINE bool ShouldSkipOperation(const Operation& op) {
  return op.saturated_use_count.IsZero();
}

namespace detail {
// Defining `input_count` to compute the number of OpIndex inputs of an
// operation.

// There is one overload for each possible type of parameters for all
// Operations rather than a default generic overload, so that we don't
// accidentally forget some types (eg, if a new Operation takes its inputs as a
// std::vector<OpIndex>, we shouldn't count this as "0 inputs because it's
// neither raw OpIndex nor base::Vector<OpIndex>", which a generic overload
// might do).

// Base case
constexpr size_t input_count() { return 0; }

// All parameters that are not OpIndex and should thus not count towards the
// "input_count" of the operations.
template <typename T, typename = std::enable_if_t<std::is_enum_v<T> ||
                                                  std::is_integral_v<T> ||
                                                  std::is_floating_point_v<T>>>
constexpr size_t input_count(T) {
  return 0;
}
template <typename T>
constexpr size_t input_count(const MaybeHandle<T>) {
  return 0;
}
template <typename T>
constexpr size_t input_count(const Handle<T>) {
  return 0;
}
template <typename T>
constexpr size_t input_count(const base::Flags<T>) {
  return 0;
}
constexpr size_t input_count(const Block*) { return 0; }
constexpr size_t input_count(const TSCallDescriptor*) { return 0; }
constexpr size_t input_count(const char*) { return 0; }
constexpr size_t input_count(const DeoptimizeParameters*) { return 0; }
constexpr size_t input_count(const FastApiCallParameters*) { return 0; }
constexpr size_t input_count(const FrameStateData*) { return 0; }
constexpr size_t input_count(const base::Vector<SwitchOp::Case>) { return 0; }
constexpr size_t input_count(LoadOp::Kind) { return 0; }
constexpr size_t input_count(RegisterRepresentation) { return 0; }
constexpr size_t input_count(MemoryRepresentation) { return 0; }
constexpr size_t input_count(OpEffects) { return 0; }
inline size_t input_count(const ElementsTransition) { return 0; }
inline size_t input_count(const FeedbackSource) { return 0; }
inline size_t input_count(const ZoneRefSet<Map>) { return 0; }
inline size_t input_count(ConstantOp::Storage) { return 0; }
inline size_t input_count(Type) { return 0; }
#ifdef V8_ENABLE_WEBASSEMBLY
constexpr size_t input_count(const wasm::WasmGlobal*) { return 0; }
constexpr size_t input_count(const wasm::StructType*) { return 0; }
constexpr size_t input_count(const wasm::ArrayType*) { return 0; }
constexpr size_t input_count(wasm::ValueType) { return 0; }
constexpr size_t input_count(WasmTypeCheckConfig) { return 0; }
#endif

// All parameters that are OpIndex-like (ie, OpIndex, and OpIndex containers)
constexpr size_t input_count(OpIndex) { return 1; }
constexpr size_t input_count(OptionalOpIndex) { return 1; }
constexpr size_t input_count(base::Vector<const OpIndex> inputs) {
  return inputs.size();
}
}  // namespace detail

template <typename Op, typename... Args>
Op* CreateOperation(base::SmallVector<OperationStorageSlot, 32>& storage,
                    Args... args) {
  size_t input_count = (0 + ... + detail::input_count(args));
  size_t size = Operation::StorageSlotCount(Op::opcode, input_count);
  storage.resize_no_init(size);
  Op* op = new (storage.data()) Op(args...);
  // Checking that the {input_count} we computed is at least the actual
  // input_count of the operation. {input_count} could be greater in the case of
  // OptionalOpIndex: they count for 1 input when computing {input_count} here,
  // but in Operations, they only count for 1 input when they are valid.
  DCHECK_GE(input_count, op->input_count);
  return op;
}

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_OPERATIONS_H_

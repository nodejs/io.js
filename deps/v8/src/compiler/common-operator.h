// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_COMMON_OPERATOR_H_
#define V8_COMPILER_COMMON_OPERATOR_H_

#include "src/base/compiler-specific.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/reloc-info.h"
#include "src/common/globals.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/frame-states.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/use-info.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream&, BranchHint);

namespace compiler {

// Forward declarations.
class CallDescriptor;
struct CommonOperatorGlobalCache;
class Operator;
class Type;
class Node;

// The semantics of IrOpcode::kBranch changes throughout the pipeline, and in
// particular is not the same before SimplifiedLowering (JS semantics) and after
// (machine branch semantics). Some passes are applied both before and after
// SimplifiedLowering, and use the BranchSemantics enum to know how branches
// should be treated.
// TODO(nicohartmann@): Need to remove BranchSemantics::kUnspecified once all
// branch uses have been updated.
enum class BranchSemantics { kJS, kMachine, kUnspecified };

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream&, BranchSemantics);

inline BranchHint NegateBranchHint(BranchHint hint) {
  switch (hint) {
    case BranchHint::kNone:
      return hint;
    case BranchHint::kTrue:
      return BranchHint::kFalse;
    case BranchHint::kFalse:
      return BranchHint::kTrue;
  }
  UNREACHABLE();
}

#if V8_ENABLE_WEBASSEMBLY
enum class TrapId : int32_t {
#define DEF_ENUM(Name, ...) \
  k##Name = static_cast<uint32_t>(Builtin::kThrowWasm##Name),
  FOREACH_WASM_TRAPREASON(DEF_ENUM)
#undef DEF_ENUM
};

static_assert(std::is_same_v<std::underlying_type_t<Builtin>,
                             std::underlying_type_t<TrapId>>);

inline size_t hash_value(TrapId id) { return static_cast<uint32_t>(id); }

std::ostream& operator<<(std::ostream&, TrapId trap_id);

TrapId TrapIdOf(const Operator* const op);
#endif

class BranchParameters final {
 public:
  BranchParameters(BranchSemantics semantics, BranchHint hint)
      : semantics_(semantics), hint_(hint) {}

  BranchSemantics semantics() const { return semantics_; }
  BranchHint hint() const { return hint_; }

 private:
  const BranchSemantics semantics_;
  const BranchHint hint_;
};

bool operator==(const BranchParameters& lhs, const BranchParameters& rhs);
inline bool operator!=(const BranchParameters& lhs,
                       const BranchParameters& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(const BranchParameters& p);

std::ostream& operator<<(std::ostream&, const BranchParameters& p);

V8_EXPORT_PRIVATE const BranchParameters& BranchParametersOf(
    const Operator* const) V8_WARN_UNUSED_RESULT;

V8_EXPORT_PRIVATE BranchHint BranchHintOf(const Operator* const)
    V8_WARN_UNUSED_RESULT;

class AssertParameters final {
 public:
  AssertParameters(BranchSemantics semantics, const char* condition_string,
                   const char* file, int line)
      : semantics_(semantics),
        condition_string_(condition_string),
        file_(file),
        line_(line) {}

  BranchSemantics semantics() const { return semantics_; }
  const char* condition_string() const { return condition_string_; }
  const char* file() const { return file_; }
  int line() const { return line_; }

 private:
  const BranchSemantics semantics_;
  const char* condition_string_;
  const char* file_;
  const int line_;
};

bool operator==(const AssertParameters& lhs, const AssertParameters& rhs);
size_t hash_value(const AssertParameters& p);
std::ostream& operator<<(std::ostream&, const AssertParameters& p);

V8_EXPORT_PRIVATE const AssertParameters& AssertParametersOf(
    const Operator* const) V8_WARN_UNUSED_RESULT;

// Helper function for return nodes, because returns have a hidden value input.
int ValueInputCountOfReturn(Operator const* const op);

// Parameters for the {Deoptimize} operator.
class DeoptimizeParameters final {
 public:
  DeoptimizeParameters(DeoptimizeReason reason, FeedbackSource const& feedback)
      : reason_(reason), feedback_(feedback) {}

  DeoptimizeReason reason() const { return reason_; }
  const FeedbackSource& feedback() const { return feedback_; }

 private:
  DeoptimizeReason const reason_;
  FeedbackSource const feedback_;
};

bool operator==(DeoptimizeParameters, DeoptimizeParameters);
bool operator!=(DeoptimizeParameters, DeoptimizeParameters);

size_t hast_value(DeoptimizeParameters p);

std::ostream& operator<<(std::ostream&, DeoptimizeParameters p);

DeoptimizeParameters const& DeoptimizeParametersOf(Operator const* const)
    V8_WARN_UNUSED_RESULT;

class SelectParameters final {
 public:
  explicit SelectParameters(MachineRepresentation representation,
                            BranchHint hint = BranchHint::kNone)
      : representation_(representation), hint_(hint) {}

  MachineRepresentation representation() const { return representation_; }
  BranchHint hint() const { return hint_; }

 private:
  const MachineRepresentation representation_;
  const BranchHint hint_;
};

bool operator==(SelectParameters const&, SelectParameters const&);
bool operator!=(SelectParameters const&, SelectParameters const&);

size_t hash_value(SelectParameters const& p);

std::ostream& operator<<(std::ostream&, SelectParameters const& p);

V8_EXPORT_PRIVATE SelectParameters const& SelectParametersOf(
    const Operator* const) V8_WARN_UNUSED_RESULT;

V8_EXPORT_PRIVATE CallDescriptor const* CallDescriptorOf(const Operator* const)
    V8_WARN_UNUSED_RESULT;

V8_EXPORT_PRIVATE size_t ProjectionIndexOf(const Operator* const)
    V8_WARN_UNUSED_RESULT;

V8_EXPORT_PRIVATE MachineRepresentation
LoopExitValueRepresentationOf(const Operator* const) V8_WARN_UNUSED_RESULT;

V8_EXPORT_PRIVATE MachineRepresentation
PhiRepresentationOf(const Operator* const) V8_WARN_UNUSED_RESULT;

// The {IrOpcode::kParameter} opcode represents an incoming parameter to the
// function. This class bundles the index and a debug name for such operators.
class ParameterInfo final {
 public:
  static constexpr int kMinIndex = Linkage::kJSCallClosureParamIndex;

  ParameterInfo(int index, const char* debug_name)
      : index_(index), debug_name_(debug_name) {
    DCHECK_LE(kMinIndex, index);
  }

  int index() const { return index_; }
  const char* debug_name() const { return debug_name_; }

 private:
  int index_;
  const char* debug_name_;
};

std::ostream& operator<<(std::ostream&, ParameterInfo const&);

V8_EXPORT_PRIVATE int ParameterIndexOf(const Operator* const)
    V8_WARN_UNUSED_RESULT;
const ParameterInfo& ParameterInfoOf(const Operator* const)
    V8_WARN_UNUSED_RESULT;

struct ObjectStateInfo final : std::pair<uint32_t, int> {
  ObjectStateInfo(uint32_t object_id, int size)
      : std::pair<uint32_t, int>(object_id, size) {}
  uint32_t object_id() const { return first; }
  int size() const { return second; }
};
std::ostream& operator<<(std::ostream&, ObjectStateInfo const&);
size_t hash_value(ObjectStateInfo const& p);

struct TypedObjectStateInfo final
    : std::pair<uint32_t, const ZoneVector<MachineType>*> {
  TypedObjectStateInfo(uint32_t object_id,
                       const ZoneVector<MachineType>* machine_types)
      : std::pair<uint32_t, const ZoneVector<MachineType>*>(object_id,
                                                            machine_types) {}
  uint32_t object_id() const { return first; }
  const ZoneVector<MachineType>* machine_types() const { return second; }
};
std::ostream& operator<<(std::ostream&, TypedObjectStateInfo const&);
size_t hash_value(TypedObjectStateInfo const& p);

class RelocatablePtrConstantInfo final {
 public:
  enum Type { kInt32, kInt64 };

  RelocatablePtrConstantInfo(int32_t value, RelocInfo::Mode rmode)
      : value_(value), rmode_(rmode), type_(kInt32) {}
  RelocatablePtrConstantInfo(int64_t value, RelocInfo::Mode rmode)
      : value_(value), rmode_(rmode), type_(kInt64) {}

  intptr_t value() const { return value_; }
  RelocInfo::Mode rmode() const { return rmode_; }
  Type type() const { return type_; }

 private:
  intptr_t value_;
  RelocInfo::Mode rmode_;
  Type type_;
};

bool operator==(RelocatablePtrConstantInfo const& lhs,
                RelocatablePtrConstantInfo const& rhs);
bool operator!=(RelocatablePtrConstantInfo const& lhs,
                RelocatablePtrConstantInfo const& rhs);

std::ostream& operator<<(std::ostream&, RelocatablePtrConstantInfo const&);

size_t hash_value(RelocatablePtrConstantInfo const& p);

// Used to define a sparse set of inputs. This can be used to efficiently encode
// nodes that can have a lot of inputs, but where many inputs can have the same
// value.
class SparseInputMask final {
 public:
  using BitMaskType = uint32_t;

  // The mask representing a dense input set.
  static const BitMaskType kDenseBitMask = 0x0;
  // The bits representing the end of a sparse input set.
  static const BitMaskType kEndMarker = 0x1;
  // The mask for accessing a sparse input entry in the bitmask.
  static const BitMaskType kEntryMask = 0x1;

  // The number of bits in the mask, minus one for the end marker.
  static const int kMaxSparseInputs = (sizeof(BitMaskType) * kBitsPerByte - 1);

  // An iterator over a node's sparse inputs.
  class InputIterator final {
   public:
    InputIterator() = default;
    InputIterator(BitMaskType bit_mask, Node* parent);

    Node* parent() const { return parent_; }
    int real_index() const { return real_index_; }

    // Advance the iterator to the next sparse input. Only valid if the iterator
    // has not reached the end.
    void Advance();

    // Get the current sparse input's real node value. Only valid if the
    // current sparse input is real.
    Node* GetReal() const;

    // Advance to the next real value or the end. Only valid if the iterator is
    // not dense. Returns the number of empty values that were skipped. This can
    // return 0 and in that case, it does not advance.
    size_t AdvanceToNextRealOrEnd();

    // Get the current sparse input, returning either a real input node if
    // the current sparse input is real, or the given {empty_value} if the
    // current sparse input is empty.
    Node* Get(Node* empty_value) const {
      return IsReal() ? GetReal() : empty_value;
    }

    // True if the current sparse input is a real input node.
    bool IsReal() const;

    // True if the current sparse input is an empty value.
    bool IsEmpty() const { return !IsReal(); }

    // True if the iterator has reached the end of the sparse inputs.
    bool IsEnd() const;

   private:
    BitMaskType bit_mask_;
    Node* parent_;
    int real_index_;
  };

  explicit SparseInputMask(BitMaskType bit_mask) : bit_mask_(bit_mask) {}

  // Provides a SparseInputMask representing a dense input set.
  static SparseInputMask Dense() { return SparseInputMask(kDenseBitMask); }

  BitMaskType mask() const { return bit_mask_; }

  bool IsDense() const { return bit_mask_ == SparseInputMask::kDenseBitMask; }

  // Counts how many real values are in the sparse array. Only valid for
  // non-dense masks.
  int CountReal() const;

  // Returns an iterator over the sparse inputs of {node}.
  InputIterator IterateOverInputs(Node* node);

 private:
  //
  // The sparse input mask has a bitmask specifying if the node's inputs are
  // represented sparsely. If the bitmask value is 0, then the inputs are dense;
  // otherwise, they should be interpreted as follows:
  //
  //   * The bitmask represents which values are real, with 1 for real values
  //     and 0 for empty values.
  //   * The inputs to the node are the real values, in the order of the 1s from
  //     least- to most-significant.
  //   * The top bit of the bitmask is a guard indicating the end of the values,
  //     whether real or empty (and is not representative of a real input
  //     itself). This is used so that we don't have to additionally store a
  //     value count.
  //
  // So, for N 1s in the bitmask, there are N - 1 inputs into the node.
  BitMaskType bit_mask_;
};

bool operator==(SparseInputMask const& lhs, SparseInputMask const& rhs);
bool operator!=(SparseInputMask const& lhs, SparseInputMask const& rhs);

class TypedStateValueInfo final {
 public:
  TypedStateValueInfo(ZoneVector<MachineType> const* machine_types,
                      SparseInputMask sparse_input_mask)
      : machine_types_(machine_types), sparse_input_mask_(sparse_input_mask) {}

  ZoneVector<MachineType> const* machine_types() const {
    return machine_types_;
  }
  SparseInputMask sparse_input_mask() const { return sparse_input_mask_; }

 private:
  ZoneVector<MachineType> const* machine_types_;
  SparseInputMask sparse_input_mask_;
};

bool operator==(TypedStateValueInfo const& lhs, TypedStateValueInfo const& rhs);
bool operator!=(TypedStateValueInfo const& lhs, TypedStateValueInfo const& rhs);

std::ostream& operator<<(std::ostream&, TypedStateValueInfo const&);

size_t hash_value(TypedStateValueInfo const& p);

// Used to mark a region (as identified by BeginRegion/FinishRegion) as either
// JavaScript-observable or not (i.e. allocations are not JavaScript observable
// themselves, but transitioning stores are).
enum class RegionObservability : uint8_t { kObservable, kNotObservable };

size_t hash_value(RegionObservability);

std::ostream& operator<<(std::ostream&, RegionObservability);

RegionObservability RegionObservabilityOf(Operator const*)
    V8_WARN_UNUSED_RESULT;

std::ostream& operator<<(std::ostream& os,
                         const ZoneVector<MachineType>* types);

Type TypeGuardTypeOf(Operator const*) V8_WARN_UNUSED_RESULT;

int OsrValueIndexOf(Operator const*) V8_WARN_UNUSED_RESULT;

SparseInputMask SparseInputMaskOf(Operator const*) V8_WARN_UNUSED_RESULT;

ZoneVector<MachineType> const* MachineTypesOf(Operator const*)
    V8_WARN_UNUSED_RESULT;

// The ArgumentsElementsState and ArgumentsLengthState can describe the layout
// for backing stores of arguments objects of various types:
//
//                        +------------------------------------+
//  - kUnmappedArguments: | arg0, ... argK-1, argK, ... argN-1 |  {length:N}
//                        +------------------------------------+
//                        +------------------------------------+
//  - kMappedArguments:   | hole, ...   hole, argK, ... argN-1 |  {length:N}
//                        +------------------------------------+
//                                          +------------------+
//  - kRestParameter:                       | argK, ... argN-1 |  {length:N-K}
//                                          +------------------+
//
// Here {K} represents the number for formal parameters of the active function,
// whereas {N} represents the actual number of arguments passed at runtime.
// Note that {N < K} can happen and causes {K} to be capped accordingly.
//
// Also note that it is possible for an arguments object of {kMappedArguments}
// type to carry a backing store of {kUnappedArguments} type when {K == 0}.
using ArgumentsStateType = CreateArgumentsType;

ArgumentsStateType ArgumentsStateTypeOf(Operator const*) V8_WARN_UNUSED_RESULT;

uint32_t ObjectIdOf(Operator const*);

MachineRepresentation DeadValueRepresentationOf(Operator const*)
    V8_WARN_UNUSED_RESULT;

class IfValueParameters final {
 public:
  IfValueParameters(int32_t value, int32_t comparison_order,
                    BranchHint hint = BranchHint::kNone)
      : value_(value), comparison_order_(comparison_order), hint_(hint) {}

  int32_t value() const { return value_; }
  int32_t comparison_order() const { return comparison_order_; }
  BranchHint hint() const { return hint_; }

 private:
  int32_t value_;
  int32_t comparison_order_;
  BranchHint hint_;
};

V8_EXPORT_PRIVATE bool operator==(IfValueParameters const&,
                                  IfValueParameters const&);

size_t hash_value(IfValueParameters const&);

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream&,
                                           IfValueParameters const&);

V8_EXPORT_PRIVATE IfValueParameters const& IfValueParametersOf(
    const Operator* op) V8_WARN_UNUSED_RESULT;

const FrameStateInfo& FrameStateInfoOf(const Operator* op)
    V8_WARN_UNUSED_RESULT;

V8_EXPORT_PRIVATE Handle<HeapObject> HeapConstantOf(const Operator* op)
    V8_WARN_UNUSED_RESULT;

const char* StaticAssertSourceOf(const Operator* op);

class SLVerifierHintParameters final {
 public:
  explicit SLVerifierHintParameters(const Operator* semantics,
                                    base::Optional<Type> override_output_type)
      : semantics_(semantics), override_output_type_(override_output_type) {}

  const Operator* semantics() const { return semantics_; }
  const base::Optional<Type>& override_output_type() const {
    return override_output_type_;
  }

 private:
  const Operator* semantics_;
  base::Optional<Type> override_output_type_;
};

V8_EXPORT_PRIVATE bool operator==(const SLVerifierHintParameters& p1,
                                  const SLVerifierHintParameters& p2);

size_t hash_value(const SLVerifierHintParameters& p);

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& out,
                                           const SLVerifierHintParameters& p);

V8_EXPORT_PRIVATE const SLVerifierHintParameters& SLVerifierHintParametersOf(
    const Operator* op) V8_WARN_UNUSED_RESULT;

class ExitMachineGraphParameters final {
 public:
  ExitMachineGraphParameters(MachineRepresentation output_representation,
                             Type output_type)
      : output_representation_(output_representation),
        output_type_(output_type) {}

  MachineRepresentation output_representation() const {
    return output_representation_;
  }

  const Type& output_type() const { return output_type_; }

 private:
  const MachineRepresentation output_representation_;
  const Type output_type_;
};

V8_EXPORT_PRIVATE bool operator==(const ExitMachineGraphParameters& lhs,
                                  const ExitMachineGraphParameters& rhs);

size_t hash_value(const ExitMachineGraphParameters& p);

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           const ExitMachineGraphParameters& p);

V8_EXPORT_PRIVATE const ExitMachineGraphParameters&
ExitMachineGraphParametersOf(const Operator* op) V8_WARN_UNUSED_RESULT;

// Interface for building common operators that can be used at any level of IR,
// including JavaScript, mid-level, and low-level.
class V8_EXPORT_PRIVATE CommonOperatorBuilder final
    : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  explicit CommonOperatorBuilder(Zone* zone);
  CommonOperatorBuilder(const CommonOperatorBuilder&) = delete;
  CommonOperatorBuilder& operator=(const CommonOperatorBuilder&) = delete;

  // A dummy value node temporarily used as input when the actual value doesn't
  // matter. This operator is inserted only in SimplifiedLowering and is
  // expected to not survive dead code elimination.
  const Operator* Plug();

  // Chained operator serves as a temporary solution to fix allocating operators
  // at a specific position in the effect and control chain during
  // effect control linearization, such that its position is non-floating
  // and cannot interfere with other inlined allocations when recomputing a
  // schedule (e.g. in Turboshaft's graph builder) when regions are gone.
  const Operator* Chained(const Operator* op);

  const Operator* Dead();
  const Operator* DeadValue(MachineRepresentation rep);
  const Operator* Unreachable();
  const Operator* StaticAssert(const char* source);
  // SLVerifierHint is used only during SimplifiedLowering. It may be introduced
  // during lowering to provide additional hints for the verifier. These nodes
  // are removed at the end of SimplifiedLowering after verification.
  const Operator* SLVerifierHint(
      const Operator* semantics,
      const base::Optional<Type>& override_output_type);
  const Operator* End(size_t control_input_count);
  // TODO(nicohartmann@): Remove the default argument for {semantics} once all
  // uses are updated.
  const Operator* Branch(
      BranchHint = BranchHint::kNone,
      BranchSemantics semantics = BranchSemantics::kUnspecified);
  const Operator* IfTrue();
  const Operator* IfFalse();
  const Operator* IfSuccess();
  const Operator* IfException();
  const Operator* Switch(size_t control_output_count);
  const Operator* IfValue(int32_t value, int32_t order = 0,
                          BranchHint hint = BranchHint::kNone);
  const Operator* IfDefault(BranchHint hint = BranchHint::kNone);
  const Operator* Throw();
  const Operator* Deoptimize(DeoptimizeReason reason,
                             FeedbackSource const& feedback);
  const Operator* DeoptimizeIf(DeoptimizeReason reason,
                               FeedbackSource const& feedback);
  const Operator* DeoptimizeUnless(DeoptimizeReason reason,
                                   FeedbackSource const& feedback);
  const Operator* Assert(BranchSemantics semantics,
                         const char* condition_string, const char* file,
                         int line);

#if V8_ENABLE_WEBASSEMBLY
  const Operator* TrapIf(TrapId trap_id, bool has_frame_state);
  const Operator* TrapUnless(TrapId trap_id, bool has_frame_state);
#endif
  const Operator* Return(int value_input_count = 1);
  const Operator* Terminate();

  const Operator* Start(int value_output_count);
  const Operator* Loop(int control_input_count);
  const Operator* Merge(int control_input_count);
  const Operator* Parameter(int index, const char* debug_name = nullptr);

  const Operator* OsrValue(int index);

  const Operator* Int32Constant(int32_t);
  const Operator* Int64Constant(int64_t);
  const Operator* TaggedIndexConstant(int32_t value);
  const Operator* Float32Constant(float);
  const Operator* Float64Constant(double);
  const Operator* ExternalConstant(const ExternalReference&);
  const Operator* NumberConstant(double);
  const Operator* PointerConstant(intptr_t);
  const Operator* HeapConstant(const Handle<HeapObject>&);
  const Operator* CompressedHeapConstant(const Handle<HeapObject>&);
  const Operator* ObjectId(uint32_t);

  const Operator* RelocatableInt32Constant(int32_t value,
                                           RelocInfo::Mode rmode);
  const Operator* RelocatableInt64Constant(int64_t value,
                                           RelocInfo::Mode rmode);

  const Operator* Select(MachineRepresentation, BranchHint = BranchHint::kNone);
  const Operator* Phi(MachineRepresentation representation,
                      int value_input_count);
  const Operator* EffectPhi(int effect_input_count);
  const Operator* InductionVariablePhi(int value_input_count);
  const Operator* LoopExit();
  const Operator* LoopExitValue(MachineRepresentation rep);
  const Operator* LoopExitEffect();
  const Operator* Checkpoint();
  const Operator* BeginRegion(RegionObservability);
  const Operator* FinishRegion();
  const Operator* StateValues(int arguments, SparseInputMask bitmask);
  const Operator* TypedStateValues(const ZoneVector<MachineType>* types,
                                   SparseInputMask bitmask);
  const Operator* ArgumentsElementsState(ArgumentsStateType type);
  const Operator* ArgumentsLengthState();
  const Operator* ObjectState(uint32_t object_id, int pointer_slots);
  const Operator* TypedObjectState(uint32_t object_id,
                                   const ZoneVector<MachineType>* types);
  const Operator* FrameState(BytecodeOffset bailout_id,
                             OutputFrameStateCombine state_combine,
                             const FrameStateFunctionInfo* function_info);
  const Operator* Call(const CallDescriptor* call_descriptor);
  const Operator* TailCall(const CallDescriptor* call_descriptor);
  const Operator* Projection(size_t index);
  const Operator* Retain();
  const Operator* TypeGuard(Type type);
  const Operator* EnterMachineGraph(UseInfo use_info);
  const Operator* ExitMachineGraph(MachineRepresentation output_representation,
                                   Type output_type);

  // Constructs a new merge or phi operator with the same opcode as {op}, but
  // with {size} inputs.
  const Operator* ResizeMergeOrPhi(const Operator* op, int size);

  // Constructs function info for frame state construction.
  const FrameStateFunctionInfo* CreateFrameStateFunctionInfo(
      FrameStateType type, int parameter_count, int local_count,
      Handle<SharedFunctionInfo> shared_info);
#if V8_ENABLE_WEBASSEMBLY
  const FrameStateFunctionInfo* CreateJSToWasmFrameStateFunctionInfo(
      FrameStateType type, int parameter_count, int local_count,
      Handle<SharedFunctionInfo> shared_info,
      const wasm::FunctionSig* signature);
#endif  // V8_ENABLE_WEBASSEMBLY

 private:
  Zone* zone() const { return zone_; }

  const CommonOperatorGlobalCache& cache_;
  Zone* const zone_;
};

// Node wrappers.

class CommonNodeWrapperBase : public NodeWrapper {
 public:
  explicit constexpr CommonNodeWrapperBase(Node* node) : NodeWrapper(node) {}

  // Valid iff this node has exactly one effect input.
  Effect effect() const {
    DCHECK_EQ(node()->op()->EffectInputCount(), 1);
    return Effect{NodeProperties::GetEffectInput(node())};
  }

  // Valid iff this node has exactly one control input.
  Control control() const {
    DCHECK_EQ(node()->op()->ControlInputCount(), 1);
    return Control{NodeProperties::GetControlInput(node())};
  }
};

#define DEFINE_INPUT_ACCESSORS(Name, name, TheIndex, Type) \
  static constexpr int Name##Index() { return TheIndex; }  \
  TNode<Type> name() const {                               \
    return TNode<Type>::UncheckedCast(                     \
        NodeProperties::GetValueInput(node(), TheIndex));  \
  }

// TODO(jgruber): This class doesn't match the usual OpcodeNode naming
// convention for historical reasons (it was originally a very basic typed node
// wrapper similar to Effect and Control). Consider updating the name, with low
// priority.
class FrameState : public CommonNodeWrapperBase {
 public:
  explicit constexpr FrameState(Node* node) : CommonNodeWrapperBase(node) {
    DCHECK_EQ(node->opcode(), IrOpcode::kFrameState);
  }

  const FrameStateInfo& frame_state_info() const {
    return FrameStateInfoOf(node()->op());
  }

  static constexpr int kFrameStateParametersInput = 0;
  static constexpr int kFrameStateLocalsInput = 1;
  static constexpr int kFrameStateStackInput = 2;
  static constexpr int kFrameStateContextInput = 3;
  static constexpr int kFrameStateFunctionInput = 4;
  static constexpr int kFrameStateOuterStateInput = 5;
  static constexpr int kFrameStateInputCount = 6;

  // Note: The parameters should be accessed through StateValuesAccess.
  Node* parameters() const {
    Node* n = node()->InputAt(kFrameStateParametersInput);
    DCHECK(n->opcode() == IrOpcode::kStateValues ||
           n->opcode() == IrOpcode::kTypedStateValues ||
           n->opcode() == IrOpcode::kDeadValue);
    return n;
  }
  Node* locals() const {
    Node* n = node()->InputAt(kFrameStateLocalsInput);
    DCHECK(n->opcode() == IrOpcode::kStateValues ||
           n->opcode() == IrOpcode::kTypedStateValues);
    return n;
  }
  // TODO(jgruber): Consider renaming this to the more meaningful
  // 'accumulator'.
  Node* stack() const { return node()->InputAt(kFrameStateStackInput); }
  Node* context() const { return node()->InputAt(kFrameStateContextInput); }
  Node* function() const { return node()->InputAt(kFrameStateFunctionInput); }

  // An outer frame state exists for inlined functions; otherwise it points at
  // the start node. Could also be dead.
  Node* outer_frame_state() const {
    Node* result = node()->InputAt(kFrameStateOuterStateInput);
    DCHECK(result->opcode() == IrOpcode::kFrameState ||
           result->opcode() == IrOpcode::kStart ||
           result->opcode() == IrOpcode::kDeadValue);
    return result;
  }
};

class StartNode final : public CommonNodeWrapperBase {
 public:
  explicit constexpr StartNode(Node* node) : CommonNodeWrapperBase(node) {
    DCHECK_EQ(IrOpcode::kStart, node->opcode());
  }

  // The receiver is counted as part of formal parameters.
  static constexpr int kReceiverOutputCount = 1;
  // These outputs are in addition to formal parameters.
  static constexpr int kExtraOutputCount = 4;

  // Takes the formal parameter count of the current function (including
  // receiver) and returns the number of value outputs of the start node.
  static constexpr int OutputArityForFormalParameterCount(int argc) {
    constexpr int kClosure = 1;
    constexpr int kNewTarget = 1;
    constexpr int kArgCount = 1;
    constexpr int kContext = 1;
    static_assert(kClosure + kNewTarget + kArgCount + kContext ==
                  kExtraOutputCount);
    // Checking related linkage methods here since they rely on Start node
    // layout.
    DCHECK_EQ(-1, Linkage::kJSCallClosureParamIndex);
    DCHECK_EQ(argc + 0, Linkage::GetJSCallNewTargetParamIndex(argc));
    DCHECK_EQ(argc + 1, Linkage::GetJSCallArgCountParamIndex(argc));
    DCHECK_EQ(argc + 2, Linkage::GetJSCallContextParamIndex(argc));
    return argc + kClosure + kNewTarget + kArgCount + kContext;
  }

  int FormalParameterCount() const {
    DCHECK_GE(node()->op()->ValueOutputCount(),
              kExtraOutputCount + kReceiverOutputCount);
    return node()->op()->ValueOutputCount() - kExtraOutputCount;
  }

  int FormalParameterCountWithoutReceiver() const {
    DCHECK_GE(node()->op()->ValueOutputCount(),
              kExtraOutputCount + kReceiverOutputCount);
    return node()->op()->ValueOutputCount() - kExtraOutputCount -
           kReceiverOutputCount;
  }

  // Note these functions don't return the index of the Start output; instead
  // they return the index assigned to the Parameter node.
  // TODO(jgruber): Consider unifying the two.
  int NewTargetParameterIndex() const {
    return Linkage::GetJSCallNewTargetParamIndex(FormalParameterCount());
  }
  int ArgCountParameterIndex() const {
    return Linkage::GetJSCallArgCountParamIndex(FormalParameterCount());
  }
  int ContextParameterIndex() const {
    return Linkage::GetJSCallContextParamIndex(FormalParameterCount());
  }

  // TODO(jgruber): Remove this function and use
  // Linkage::GetJSCallContextParamIndex instead. This currently doesn't work
  // because tests don't create valid Start nodes - for example, they may add
  // only two context outputs (and not the closure, new target, argc). Once
  // tests are fixed, remove this function.
  int ContextParameterIndex_MaybeNonStandardLayout() const {
    // The context is always the last parameter to a JavaScript function, and
    // {Parameter} indices start at -1, so value outputs of {Start} look like
    // this: closure, receiver, param0, ..., paramN, context.
    //
    // TODO(jgruber): This function is called from spots that operate on
    // CSA/Torque graphs; Start node layout appears to be different there.
    // These should be unified to avoid confusion. Once done, enable this
    // DCHECK: DCHECK_EQ(LastOutputIndex(), ContextOutputIndex());
    return node()->op()->ValueOutputCount() - 2;
  }
  int LastParameterIndex_MaybeNonStandardLayout() const {
    return ContextParameterIndex_MaybeNonStandardLayout();
  }

  // Unlike ContextParameterIndex_MaybeNonStandardLayout above, these return
  // output indices (and not the index assigned to a Parameter).
  int NewTargetOutputIndex() const {
    // Indices assigned to parameters are off-by-one (Parameters indices start
    // at -1).
    // TODO(jgruber): Consider starting at 0.
    DCHECK_EQ(Linkage::GetJSCallNewTargetParamIndex(FormalParameterCount()) + 1,
              node()->op()->ValueOutputCount() - 3);
    return node()->op()->ValueOutputCount() - 3;
  }
  int ArgCountOutputIndex() const {
    // Indices assigned to parameters are off-by-one (Parameters indices start
    // at -1).
    // TODO(jgruber): Consider starting at 0.
    DCHECK_EQ(Linkage::GetJSCallArgCountParamIndex(FormalParameterCount()) + 1,
              node()->op()->ValueOutputCount() - 2);
    return node()->op()->ValueOutputCount() - 2;
  }
  int ContextOutputIndex() const {
    // Indices assigned to parameters are off-by-one (Parameters indices start
    // at -1).
    // TODO(jgruber): Consider starting at 0.
    DCHECK_EQ(Linkage::GetJSCallContextParamIndex(FormalParameterCount()) + 1,
              node()->op()->ValueOutputCount() - 1);
    return node()->op()->ValueOutputCount() - 1;
  }
  int LastOutputIndex() const { return ContextOutputIndex(); }
};

#undef DEFINE_INPUT_ACCESSORS

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_COMMON_OPERATOR_H_

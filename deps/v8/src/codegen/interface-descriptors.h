// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_INTERFACE_DESCRIPTORS_H_
#define V8_CODEGEN_INTERFACE_DESCRIPTORS_H_

#include <memory>

#include "src/base/logging.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/register.h"
#include "src/codegen/tnode.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"

namespace v8 {
namespace internal {

#define TORQUE_BUILTIN_LIST_TFC(V)                                            \
  BUILTIN_LIST_FROM_TORQUE(IGNORE_BUILTIN, IGNORE_BUILTIN, V, IGNORE_BUILTIN, \
                           IGNORE_BUILTIN, IGNORE_BUILTIN)

#define INTERFACE_DESCRIPTOR_LIST(V)                 \
  V(Abort)                                           \
  V(Allocate)                                        \
  V(CallApiCallbackGeneric)                          \
  V(CallApiCallbackOptimized)                        \
  V(ApiGetter)                                       \
  V(ArrayConstructor)                                \
  V(ArrayNArgumentsConstructor)                      \
  V(ArrayNoArgumentConstructor)                      \
  V(ArraySingleArgumentConstructor)                  \
  V(AsyncFunctionStackParameter)                     \
  V(BaselineLeaveFrame)                              \
  V(BaselineOutOfLinePrologue)                       \
  V(BigIntToI32Pair)                                 \
  V(BigIntToI64)                                     \
  V(BinaryOp)                                        \
  V(BinaryOp_Baseline)                               \
  V(BinaryOp_WithFeedback)                           \
  V(BinarySmiOp_Baseline)                            \
  V(CallForwardVarargs)                              \
  V(CallFunctionTemplate)                            \
  V(CallFunctionTemplateGeneric)                     \
  V(CallTrampoline)                                  \
  V(CallTrampoline_Baseline)                         \
  V(CallTrampoline_Baseline_Compact)                 \
  V(CallTrampoline_WithFeedback)                     \
  V(CallVarargs)                                     \
  V(CallWithArrayLike)                               \
  V(CallWithArrayLike_WithFeedback)                  \
  V(CallWithSpread)                                  \
  V(CallWithSpread_Baseline)                         \
  V(CallWithSpread_WithFeedback)                     \
  V(CCall)                                           \
  V(CEntryDummy)                                     \
  V(CEntry1ArgvOnStack)                              \
  V(CloneObjectBaseline)                             \
  V(CloneObjectWithVector)                           \
  V(Compare)                                         \
  V(CompareNoContext)                                \
  V(StringEqual)                                     \
  V(Compare_Baseline)                                \
  V(Compare_WithFeedback)                            \
  V(Construct_Baseline)                              \
  V(ConstructForwardVarargs)                         \
  V(ConstructForwardAllArgs)                         \
  V(ConstructForwardAllArgs_Baseline)                \
  V(ConstructForwardAllArgs_WithFeedback)            \
  V(ConstructStub)                                   \
  V(ConstructVarargs)                                \
  V(ConstructWithArrayLike)                          \
  V(Construct_WithFeedback)                          \
  V(ConstructWithSpread)                             \
  V(ConstructWithSpread_Baseline)                    \
  V(ConstructWithSpread_WithFeedback)                \
  V(ContextOnly)                                     \
  V(CopyDataPropertiesWithExcludedProperties)        \
  V(CopyDataPropertiesWithExcludedPropertiesOnStack) \
  V(CppBuiltinAdaptor)                               \
  V(CreateFromSlowBoilerplateHelper)                 \
  V(DefineKeyedOwn)                                  \
  V(DefineKeyedOwnBaseline)                          \
  V(DefineKeyedOwnWithVector)                        \
  V(FastNewObject)                                   \
  V(FindNonDefaultConstructorOrConstruct)            \
  V(ForInPrepare)                                    \
  V(GetIteratorStackParameter)                       \
  V(GetProperty)                                     \
  V(GrowArrayElements)                               \
  V(I32PairToBigInt)                                 \
  V(I64ToBigInt)                                     \
  V(InterpreterCEntry1)                              \
  V(InterpreterCEntry2)                              \
  V(InterpreterDispatch)                             \
  V(InterpreterPushArgsThenCall)                     \
  V(InterpreterPushArgsThenConstruct)                \
  V(JSTrampoline)                                    \
  V(KeyedHasICBaseline)                              \
  V(KeyedHasICWithVector)                            \
  V(KeyedLoad)                                       \
  V(KeyedLoadBaseline)                               \
  V(EnumeratedKeyedLoadBaseline)                     \
  V(KeyedLoadWithVector)                             \
  V(EnumeratedKeyedLoad)                             \
  V(Load)                                            \
  V(LoadBaseline)                                    \
  V(LoadGlobal)                                      \
  V(LoadGlobalBaseline)                              \
  V(LoadGlobalNoFeedback)                            \
  V(LoadGlobalWithVector)                            \
  V(LoadNoFeedback)                                  \
  V(LoadWithReceiverAndVector)                       \
  V(LoadWithReceiverBaseline)                        \
  V(LoadWithVector)                                  \
  V(LookupWithVector)                                \
  V(LookupTrampoline)                                \
  V(LookupBaseline)                                  \
  V(MaglevOptimizeCodeOrTailCallOptimizedCodeSlot)   \
  V(NewHeapNumber)                                   \
  V(NoContext)                                       \
  V(OnStackReplacement)                              \
  V(RegExpTrampoline)                                \
  V(RestartFrameTrampoline)                          \
  V(ResumeGenerator)                                 \
  V(ResumeGeneratorBaseline)                         \
  V(RunMicrotasks)                                   \
  V(RunMicrotasksEntry)                              \
  V(SingleParameterOnStack)                          \
  V(Store)                                           \
  V(StoreNoFeedback)                                 \
  V(StoreBaseline)                                   \
  V(StoreGlobal)                                     \
  V(StoreGlobalBaseline)                             \
  V(StoreGlobalWithVector)                           \
  V(StoreTransition)                                 \
  V(StoreWithVector)                                 \
  V(StringAtAsString)                                \
  V(StringSubstring)                                 \
  V(SuspendGeneratorBaseline)                        \
  V(TypeConversion)                                  \
  V(TypeConversion_Baseline)                         \
  V(TypeConversionNoContext)                         \
  V(Typeof)                                          \
  V(UnaryOp_Baseline)                                \
  V(UnaryOp_WithFeedback)                            \
  V(Void)                                            \
  V(WasmDummy)                                       \
  V(WasmDummyWithJSLinkage)                          \
  V(WasmFloat32ToNumber)                             \
  V(WasmFloat64ToTagged)                             \
  V(WasmJSToWasmWrapper)                             \
  V(WasmToJSWrapper)                                 \
  V(WasmSuspend)                                     \
  V(WriteBarrier)                                    \
  V(IndirectPointerWriteBarrier)                     \
  IF_TSAN(V, TSANLoad)                               \
  IF_TSAN(V, TSANStore)                              \
  BUILTIN_LIST_TFS(V)                                \
  TORQUE_BUILTIN_LIST_TFC(V)

enum class StackArgumentOrder {
  kDefault,  // Arguments in the stack are pushed in the default/stub order (the
             // first argument is pushed first).
  kJS,  // Arguments in the stack are pushed in the same order as the one used
        // by JS-to-JS function calls. This should be used if calling a
        // JSFunction or if the builtin is expected to be called directly from a
        // JSFunction. This order is reversed compared to kDefault.
};

class V8_EXPORT_PRIVATE CallInterfaceDescriptorData {
 public:
  enum Flag {
    kNoFlags = 0u,
    kNoContext = 1u << 0,
    // This indicates that the code uses a special frame that does not scan the
    // stack arguments, e.g. EntryFrame. And this allows the code to use
    // untagged stack arguments.
    kNoStackScan = 1u << 1,
    // In addition to the specified parameters, additional arguments can be
    // passed on the stack.
    // This does not indicate if arguments adaption is used or not.
    kAllowVarArgs = 1u << 2,
    // Callee save allocatable_registers.
    kCalleeSaveRegisters = 1u << 3,
  };
  using Flags = base::Flags<Flag>;

  static constexpr int kUninitializedCount = -1;

  CallInterfaceDescriptorData() = default;

  CallInterfaceDescriptorData(const CallInterfaceDescriptorData&) = delete;
  CallInterfaceDescriptorData& operator=(const CallInterfaceDescriptorData&) =
      delete;

  // The passed registers are owned by the caller, and their lifetime is
  // expected to exceed that of this data. In practice, they are expected to
  // be in a static local.
  void InitializeRegisters(Flags flags, CodeEntrypointTag tag, int return_count,
                           int parameter_count, StackArgumentOrder stack_order,
                           int register_parameter_count,
                           const Register* registers,
                           const DoubleRegister* double_registers,
                           const Register* return_registers,
                           const DoubleRegister* return_double_registers);

  // if machine_types is null, then an array of size
  // (return_count + parameter_count) will be created with
  // MachineType::AnyTagged() for each member.
  //
  // if machine_types is not null, then it should be of the size
  // (return_count + parameter_count). Those members of the parameter array will
  // be initialized from {machine_types}, and the rest initialized to
  // MachineType::AnyTagged().
  void InitializeTypes(const MachineType* machine_types,
                       int machine_types_length);

  void Reset();

  bool IsInitialized() const {
    return IsInitializedRegisters() && IsInitializedTypes();
  }

  Flags flags() const { return flags_; }
  CodeEntrypointTag tag() const { return tag_; }
  int return_count() const { return return_count_; }
  int param_count() const { return param_count_; }
  int register_param_count() const { return register_param_count_; }
  Register register_param(int index) const { return register_params_[index]; }
  DoubleRegister double_register_param(int index) const {
    return double_register_params_[index];
  }
  Register register_return(int index) const { return register_returns_[index]; }
  DoubleRegister double_register_return(int index) const {
    return double_register_returns_[index];
  }
  MachineType return_type(int index) const {
    DCHECK_LT(index, return_count_);
    return machine_types_[index];
  }
  MachineType param_type(int index) const {
    DCHECK_LT(index, param_count_);
    return machine_types_[return_count_ + index];
  }
  StackArgumentOrder stack_order() const { return stack_order_; }

  void RestrictAllocatableRegisters(const Register* registers, size_t num) {
    DCHECK(allocatable_registers_.is_empty());
    for (size_t i = 0; i < num; ++i) {
      allocatable_registers_.set(registers[i]);
    }
    DCHECK(!allocatable_registers_.is_empty());
  }

  RegList allocatable_registers() const { return allocatable_registers_; }

 private:
  bool IsInitializedRegisters() const {
    const bool initialized =
        return_count_ != kUninitializedCount &&
        param_count_ != kUninitializedCount &&
        (register_param_count_ == 0 || register_params_ != nullptr);
    // Register initialization happens before type initialization.
    return initialized;
  }
  bool IsInitializedTypes() const {
    const bool initialized = machine_types_ != nullptr;
    // Register initialization happens before type initialization.
    return initialized;
  }

#ifdef DEBUG
  bool AllStackParametersAreTagged() const;
#endif  // DEBUG

  int register_param_count_ = kUninitializedCount;
  int return_count_ = kUninitializedCount;
  int param_count_ = kUninitializedCount;
  Flags flags_ = kNoFlags;
  CodeEntrypointTag tag_ = kDefaultCodeEntrypointTag;
  StackArgumentOrder stack_order_ = StackArgumentOrder::kDefault;

  // Specifying the set of registers that could be used by the register
  // allocator. Currently, it's only used by RecordWrite code stub.
  RegList allocatable_registers_;

  // |registers_params_| defines registers that are used for parameter passing.
  // |machine_types_| defines machine types for resulting values and incomping
  // parameters.
  // The register params array is owned by the caller, and it's expected that it
  // is a static local stored in the caller function. The machine types are
  // allocated dynamically by the InterfaceDescriptor and freed on destruction.
  const Register* register_params_ = nullptr;
  const DoubleRegister* double_register_params_ = nullptr;
  const Register* register_returns_ = nullptr;
  const DoubleRegister* double_register_returns_ = nullptr;
  MachineType* machine_types_ = nullptr;
};

class V8_EXPORT_PRIVATE CallDescriptors : public AllStatic {
 public:
  enum Key {
#define DEF_ENUM(name, ...) name,
    INTERFACE_DESCRIPTOR_LIST(DEF_ENUM)
#undef DEF_ENUM
        NUMBER_OF_DESCRIPTORS
  };

  static void InitializeOncePerProcess();
  static void TearDown();

  static CallInterfaceDescriptorData* call_descriptor_data(
      CallDescriptors::Key key) {
    return &call_descriptor_data_[key];
  }

  static Key GetKey(const CallInterfaceDescriptorData* data) {
    ptrdiff_t index = data - call_descriptor_data_;
    DCHECK_LE(0, index);
    DCHECK_LT(index, CallDescriptors::NUMBER_OF_DESCRIPTORS);
    return static_cast<CallDescriptors::Key>(index);
  }

 private:
  static CallInterfaceDescriptorData
      call_descriptor_data_[NUMBER_OF_DESCRIPTORS];
};

#if defined(V8_TARGET_ARCH_IA32)
// To support all possible cases, we must limit the number of register args for
// TFS builtins on ia32 to 3. Out of the 6 allocatable registers, esi is taken
// as the context register and ebx is the root register. One register must
// remain available to store the jump/call target. Thus 3 registers remain for
// arguments. The reason this applies to TFS builtins specifically is because
// this becomes relevant for builtins used as targets of Torque function
// pointers (which must have a register available to store the target).
// TODO(jgruber): Ideally we should just decrement kMaxBuiltinRegisterParams but
// that comes with its own set of complications. It's possible, but requires
// refactoring the calling convention of other existing stubs.
constexpr int kMaxBuiltinRegisterParams = 4;
constexpr int kMaxTFSBuiltinRegisterParams = 3;
#else
constexpr int kMaxBuiltinRegisterParams = 5;
constexpr int kMaxTFSBuiltinRegisterParams = kMaxBuiltinRegisterParams;
#endif
static_assert(kMaxTFSBuiltinRegisterParams <= kMaxBuiltinRegisterParams);
constexpr int kJSBuiltinRegisterParams = 4;

// Polymorphic base class for call interface descriptors, which defines getters
// for the various descriptor properties via a runtime-loaded
// CallInterfaceDescriptorData field.
class V8_EXPORT_PRIVATE CallInterfaceDescriptor {
 public:
  using Flags = CallInterfaceDescriptorData::Flags;

  CallInterfaceDescriptor() : data_(nullptr) {}
  ~CallInterfaceDescriptor() = default;

  explicit CallInterfaceDescriptor(CallDescriptors::Key key)
      : data_(CallDescriptors::call_descriptor_data(key)) {}

  Flags flags() const { return data()->flags(); }

  CodeEntrypointTag tag() const { return data()->tag(); }

  bool HasContextParameter() const {
    return (flags() & CallInterfaceDescriptorData::kNoContext) == 0;
  }

  bool AllowVarArgs() const {
    return flags() & CallInterfaceDescriptorData::kAllowVarArgs;
  }

  bool CalleeSaveRegisters() const {
    return flags() & CallInterfaceDescriptorData::kCalleeSaveRegisters;
  }

  int GetReturnCount() const { return data()->return_count(); }

  MachineType GetReturnType(int index) const {
    DCHECK_LT(index, data()->return_count());
    return data()->return_type(index);
  }

  int GetParameterCount() const { return data()->param_count(); }

  int GetRegisterParameterCount() const {
    return data()->register_param_count();
  }

  int GetStackParameterCount() const {
    return data()->param_count() - data()->register_param_count();
  }

  Register GetRegisterParameter(int index) const {
    DCHECK_LT(index, data()->register_param_count());
    return data()->register_param(index);
  }

  DoubleRegister GetDoubleRegisterParameter(int index) const {
    DCHECK_LT(index, data()->register_param_count());
    return data()->double_register_param(index);
  }

  Register GetRegisterReturn(int index) const {
    DCHECK_LT(index, data()->return_count());
    return data()->register_return(index);
  }

  DoubleRegister GetDoubleRegisterReturn(int index) const {
    DCHECK_LT(index, data()->return_count());
    return data()->double_register_return(index);
  }

  MachineType GetParameterType(int index) const {
    DCHECK_LT(index, data()->param_count());
    return data()->param_type(index);
  }

  RegList allocatable_registers() const {
    return data()->allocatable_registers();
  }

  StackArgumentOrder GetStackArgumentOrder() const {
    return data()->stack_order();
  }

  static constexpr inline Register ContextRegister() {
    return kContextRegister;
  }

  const char* DebugName() const;

  bool operator==(const CallInterfaceDescriptor& other) const {
    return data() == other.data();
  }

 protected:
  const CallInterfaceDescriptorData* data() const { return data_; }

  // Helper for defining the default register set.
  //
  // Use auto for the return type to allow different architectures to have
  // differently sized default register arrays.
  static constexpr inline auto DefaultRegisterArray();
  static constexpr inline auto DefaultDoubleRegisterArray();
  static constexpr inline auto DefaultReturnRegisterArray();
  static constexpr inline auto DefaultReturnDoubleRegisterArray();
  static constexpr inline std::array<Register, kJSBuiltinRegisterParams>
  DefaultJSRegisterArray();

  // Checks if float parameters are not assigned invalid registers.
  bool CheckFloatingPointParameters(CallInterfaceDescriptorData* data) {
    for (int i = 0; i < data->register_param_count(); i++) {
      if (IsFloatingPoint(data->param_type(i).representation())) {
        if (!IsValidFloatParameterRegister(data->register_param(i))) {
          return false;
        }
      }
    }
    return true;
  }

  bool IsValidFloatParameterRegister(Register reg);

 private:
  const CallInterfaceDescriptorData* data_;
};

// CRTP base class for call interface descriptors, which defines static getters
// for the various descriptor properties based on static values defined in the
// subclass.
template <typename DerivedDescriptor>
class StaticCallInterfaceDescriptor : public CallInterfaceDescriptor {
 public:
  // ===========================================================================
  // The following are the descriptor's CRTP configuration points, overwritable
  // by DerivedDescriptor.
  static constexpr int kReturnCount =
      CallInterfaceDescriptorData::kUninitializedCount;
  static constexpr int kParameterCount =
      CallInterfaceDescriptorData::kUninitializedCount;
  static constexpr bool kNoContext = false;
  static constexpr bool kAllowVarArgs = false;
  static constexpr bool kNoStackScan = false;
  static constexpr auto kStackArgumentOrder = StackArgumentOrder::kDefault;

  // The set of registers available to the parameters, as a
  // std::array<Register,N>. Can be larger or smaller than kParameterCount; if
  // larger then any remaining registers are ignored; if smaller, any parameters
  // after registers().size() will be stack registers.
  //
  // Defaults to CallInterfaceDescriptor::DefaultRegisterArray().
  static constexpr inline auto registers();
  static constexpr inline auto double_registers();
  static constexpr inline auto return_registers();
  static constexpr inline auto return_double_registers();

  // An additional limit on the number of register parameters allowed. This is
  // here so that it can be overwritten to kMaxTFSBuiltinRegisterParams for TFS
  // builtins, see comment on kMaxTFSBuiltinRegisterParams above.
  static constexpr int kMaxRegisterParams = kMaxBuiltinRegisterParams;

  // If set to true, the descriptor will restrict the set of allocatable
  // registers to the set returned by registers(). Then, it is expected that
  // the first kParameterCount registers() are the parameters of the builtin.
  static constexpr bool kRestrictAllocatableRegisters = false;

  // If set to true, builtins will callee save the set returned by registers().
  static constexpr bool kCalleeSaveRegisters = false;

  // If set to true, the descriptor will define a kMachineTypes array with the
  // types of each result value and parameter.
  static constexpr bool kCustomMachineTypes = false;

  // End of customization points.
  // ===========================================================================

  static constexpr inline Flags flags() {
    return Flags((DerivedDescriptor::kNoContext
                      ? CallInterfaceDescriptorData::kNoContext
                      : 0) |
                 (DerivedDescriptor::kAllowVarArgs
                      ? CallInterfaceDescriptorData::kAllowVarArgs
                      : 0) |
                 (DerivedDescriptor::kNoStackScan
                      ? CallInterfaceDescriptorData::kNoStackScan
                      : 0) |
                 (DerivedDescriptor::kCalleeSaveRegisters
                      ? CallInterfaceDescriptorData::kCalleeSaveRegisters
                      : 0));
  }
  static constexpr inline bool AllowVarArgs() {
    return DerivedDescriptor::kAllowVarArgs;
  }
  static constexpr inline bool HasContextParameter() {
    return !DerivedDescriptor::kNoContext;
  }

  static constexpr inline int GetReturnCount();
  static constexpr inline int GetParameterCount();
  static constexpr inline int GetRegisterParameterCount();
  static constexpr inline int GetStackParameterCount();
  static constexpr inline Register* GetRegisterData();
  static constexpr inline Register GetRegisterParameter(int i);
  static constexpr inline int GetStackParameterIndex(int i);
  static constexpr inline MachineType GetParameterType(int i);

  // Interface descriptors don't really support double registers.
  // This reinterprets the i-th register as a double with the same code.
  static constexpr inline DoubleRegister GetDoubleRegisterParameter(int i);

  explicit StaticCallInterfaceDescriptor(CallDescriptors::Key key)
      : CallInterfaceDescriptor(key) {}

#if DEBUG
  // Overwritten in DerivedDescriptor.
  static void Verify(CallInterfaceDescriptorData* data);
  // Verify that the CallInterfaceDescriptorData contains the default
  // argument registers for {argc} arguments.
  static inline void VerifyArgumentRegisterCount(
      CallInterfaceDescriptorData* data, int nof_expected_args);
#endif

 private:
  // {CallDescriptors} is allowed to call the private {Initialize} method.
  friend class CallDescriptors;

  inline void Initialize(CallInterfaceDescriptorData* data);

  // Set up the types of the descriptor. This is a static function, so that it
  // is overwritable by subclasses. By default, all parameters have
  // MachineType::AnyTagged() type.
  static void InitializeTypes(CallInterfaceDescriptorData* data) {
    DCHECK(!kCustomMachineTypes);
    data->InitializeTypes(nullptr, 0);
  }
};

template <typename Descriptor>
class StaticJSCallInterfaceDescriptor
    : public StaticCallInterfaceDescriptor<Descriptor> {
 public:
  static constexpr auto kStackArgumentOrder = StackArgumentOrder::kJS;
  static constexpr inline auto registers();

  using StaticCallInterfaceDescriptor<
      Descriptor>::StaticCallInterfaceDescriptor;
};

template <Builtin kBuiltin>
struct CallInterfaceDescriptorFor;

// Stub class replacing std::array<Register, 0>, as a workaround for MSVC's
// https://github.com/microsoft/STL/issues/942
struct EmptyRegisterArray {
  const Register* data() const { return nullptr; }
  size_t size() const { return 0; }
  Register operator[](size_t i) const { UNREACHABLE(); }
};

// Helper method for defining an array of unique registers for the various
// Descriptor::registers() methods.
template <typename... Registers>
constexpr std::array<Register, 1 + sizeof...(Registers)> RegisterArray(
    Register first_reg, Registers... regs) {
  DCHECK(!AreAliased(first_reg, regs...));
  return {first_reg, regs...};
}
constexpr EmptyRegisterArray RegisterArray() { return {}; }

// Stub class replacing std::array<Register, 0>, as a workaround for MSVC's
// https://github.com/microsoft/STL/issues/942
struct EmptyDoubleRegisterArray {
  const DoubleRegister* data() const { return nullptr; }
  size_t size() const { return 0; }
  DoubleRegister operator[](size_t i) const { UNREACHABLE(); }
};

// Helper method for defining an array of unique registers for the various
// Descriptor::double_registers() methods.
template <typename... Registers>
constexpr std::array<DoubleRegister, 1 + sizeof...(Registers)>
DoubleRegisterArray(DoubleRegister first_reg, Registers... regs) {
  DCHECK(!AreAliased(first_reg, regs...));
  return {first_reg, regs...};
}

constexpr EmptyDoubleRegisterArray DoubleRegisterArray() { return {}; }

#define DECLARE_DESCRIPTOR_WITH_BASE(name, base)                  \
 public:                                                          \
  /* StaticCallInterfaceDescriptor can call Initialize methods */ \
  friend class StaticCallInterfaceDescriptor<name>;               \
  explicit name() : base(key()) {}                                \
  static inline CallDescriptors::Key key();

#define DECLARE_DEFAULT_DESCRIPTOR(name)                                  \
  DECLARE_DESCRIPTOR_WITH_BASE(name, StaticCallInterfaceDescriptor)       \
  static constexpr int kMaxRegisterParams = kMaxTFSBuiltinRegisterParams; \
                                                                          \
 protected:                                                               \
  explicit name(CallDescriptors::Key key)                                 \
      : StaticCallInterfaceDescriptor(key) {}                             \
                                                                          \
 public:

#define DECLARE_JS_COMPATIBLE_DESCRIPTOR(name)                        \
  DECLARE_DESCRIPTOR_WITH_BASE(name, StaticJSCallInterfaceDescriptor) \
 protected:                                                           \
  explicit name(CallDescriptors::Key key)                             \
      : StaticJSCallInterfaceDescriptor(key) {}                       \
                                                                      \
 public:

#define DEFINE_RESULT_AND_PARAMETERS(return_count, ...)   \
  static constexpr int kReturnCount = return_count;       \
  enum ParameterIndices {                                 \
    __dummy = -1, /* to be able to pass zero arguments */ \
    ##__VA_ARGS__,                                        \
                                                          \
    kParameterCount,                                      \
    kContext = kParameterCount /* implicit parameter */   \
  };

// This is valid only for builtins that use EntryFrame, which does not scan
// stack arguments on GC.
#define DEFINE_PARAMETERS_ENTRY(...)                        \
  static constexpr bool kNoContext = true;                  \
  static constexpr bool kNoStackScan = true;                \
  static constexpr StackArgumentOrder kStackArgumentOrder = \
      StackArgumentOrder::kDefault;                         \
  static constexpr int kReturnCount = 1;                    \
  enum ParameterIndices {                                   \
    __dummy = -1, /* to be able to pass zero arguments */   \
    ##__VA_ARGS__,                                          \
                                                            \
    kParameterCount                                         \
  };

#define DEFINE_PARAMETERS(...) DEFINE_RESULT_AND_PARAMETERS(1, ##__VA_ARGS__)

#define DEFINE_PARAMETERS_NO_CONTEXT(...) \
  DEFINE_PARAMETERS(__VA_ARGS__)          \
  static constexpr bool kNoContext = true;

#define DEFINE_PARAMETERS_VARARGS(...)                      \
  DEFINE_PARAMETERS(__VA_ARGS__)                            \
  static constexpr bool kAllowVarArgs = true;               \
  static constexpr StackArgumentOrder kStackArgumentOrder = \
      StackArgumentOrder::kJS;

#define DEFINE_PARAMETERS_NO_CONTEXT_VARARGS(...)           \
  DEFINE_PARAMETERS_NO_CONTEXT(__VA_ARGS__)                 \
  static constexpr bool kAllowVarArgs = true;               \
  static constexpr StackArgumentOrder kStackArgumentOrder = \
      StackArgumentOrder::kJS;

#define DEFINE_RESULT_AND_PARAMETERS_NO_CONTEXT(return_count, ...) \
  DEFINE_RESULT_AND_PARAMETERS(return_count, ##__VA_ARGS__)        \
  static constexpr bool kNoContext = true;

#define DEFINE_RESULT_AND_PARAMETER_TYPES(...)                                \
  static constexpr bool kCustomMachineTypes = true;                           \
  static constexpr MachineType kMachineTypes[] = {__VA_ARGS__};               \
  static void InitializeTypes(CallInterfaceDescriptorData* data) {            \
    static_assert(                                                            \
        kReturnCount + kParameterCount == arraysize(kMachineTypes),           \
        "Parameter names definition is not consistent with parameter types"); \
    data->InitializeTypes(kMachineTypes, arraysize(kMachineTypes));           \
  }

#define DEFINE_PARAMETER_TYPES(...)                                        \
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::AnyTagged() /* result */, \
                                    ##__VA_ARGS__)

// When the extra arguments described here are located in the stack, they are
// just above the return address in the frame (first arguments).
#define DEFINE_JS_PARAMETERS(...)                           \
  static constexpr bool kAllowVarArgs = true;               \
  static constexpr int kReturnCount = 1;                    \
  static constexpr StackArgumentOrder kStackArgumentOrder = \
      StackArgumentOrder::kJS;                              \
  enum ParameterIndices {                                   \
    kTarget,                                                \
    kNewTarget,                                             \
    kActualArgumentsCount,                                  \
    ##__VA_ARGS__,                                          \
    kParameterCount,                                        \
    kContext = kParameterCount /* implicit parameter */     \
  };

#define DEFINE_JS_PARAMETERS_NO_CONTEXT(...)                \
  static constexpr bool kAllowVarArgs = true;               \
  static constexpr bool kNoContext = true;                  \
  static constexpr int kReturnCount = 1;                    \
  static constexpr StackArgumentOrder kStackArgumentOrder = \
      StackArgumentOrder::kJS;                              \
  enum ParameterIndices {                                   \
    kTarget,                                                \
    kNewTarget,                                             \
    kActualArgumentsCount,                                  \
    ##__VA_ARGS__,                                          \
    kParameterCount,                                        \
  };

#define DEFINE_JS_PARAMETER_TYPES(...)                                         \
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(), /* kTarget */               \
                         MachineType::AnyTagged(), /* kNewTarget */            \
                         MachineType::Int32(),     /* kActualArgumentsCount */ \
                         ##__VA_ARGS__)

// Code/Builtins using this descriptor are referenced from inside the sandbox
// through a code pointer and must therefore be exposed via the code pointer
// table (CPT). They should use a code entrypoint tag which will be used to tag
// the entry in the CPT and will be checked to match the tag expected at the
// callsite. Only "compatible" builtins should use the same code entrypoint tag
// as it must be assumed that an attacker can swap code pointers (the indices
// into the CPT) and therefore can invoke all builtins that use the same tag
// from a given callsite.
#define SANDBOX_EXPOSED_DESCRIPTOR(tag) \
  static constexpr CodeEntrypointTag kEntrypointTag = tag;

// Code/Builtins using this descriptor are not referenced from inside the
// sandbox but only called directly from other code. They are therefore not
// exposed to the sandbox via the CPT and so use the kInvalidEntrypointTag.
#define INTERNAL_DESCRIPTOR() \
  static constexpr CodeEntrypointTag kEntrypointTag = kInvalidEntrypointTag;

#define DECLARE_DESCRIPTOR(name)                                    \
  DECLARE_DESCRIPTOR_WITH_BASE(name, StaticCallInterfaceDescriptor) \
 protected:                                                         \
  explicit name(CallDescriptors::Key key)                           \
      : StaticCallInterfaceDescriptor(key) {}                       \
                                                                    \
 public:

class V8_EXPORT_PRIVATE VoidDescriptor
    : public StaticCallInterfaceDescriptor<VoidDescriptor> {
 public:
  // The void descriptor could (and indeed probably should) also be NO_CONTEXT,
  // but this breaks some code assembler unittests.
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS()
  DEFINE_PARAMETER_TYPES()
  DECLARE_DESCRIPTOR(VoidDescriptor)

  static constexpr auto registers();
};

// Marks deoptimization entry builtins. Precise calling conventions currently
// differ based on the platform.
// TODO(jgruber): Once this is unified, we could create a better description
// here.
using DeoptimizationEntryDescriptor = VoidDescriptor;

// TODO(jgruber): Consider filling in the details here; however, this doesn't
// make too much sense as long as the descriptor isn't used or verified.
using JSEntryDescriptor = VoidDescriptor;

// TODO(jgruber): Consider filling in the details here; however, this doesn't
// make too much sense as long as the descriptor isn't used or verified.
using ContinueToBuiltinDescriptor = VoidDescriptor;

// Dummy descriptor that marks builtins with C calling convention.
// TODO(jgruber): Define real descriptors for C calling conventions.
class CCallDescriptor : public StaticCallInterfaceDescriptor<CCallDescriptor> {
 public:
  SANDBOX_EXPOSED_DESCRIPTOR(kDefaultCodeEntrypointTag)
  DEFINE_PARAMETERS()
  DEFINE_PARAMETER_TYPES()
  DECLARE_DESCRIPTOR(CCallDescriptor)
};

// TODO(jgruber): Consider filling in the details here; however, this doesn't
// make too much sense as long as the descriptor isn't used or verified.
class CEntryDummyDescriptor
    : public StaticCallInterfaceDescriptor<CEntryDummyDescriptor> {
 public:
  SANDBOX_EXPOSED_DESCRIPTOR(kDefaultCodeEntrypointTag)
  DEFINE_PARAMETERS()
  DEFINE_PARAMETER_TYPES()
  DECLARE_DESCRIPTOR(CEntryDummyDescriptor)
};

// TODO(wasm): Consider filling in details / defining real descriptors for all
// builtins still using this placeholder descriptor.
class WasmDummyDescriptor
    : public StaticCallInterfaceDescriptor<WasmDummyDescriptor> {
 public:
  SANDBOX_EXPOSED_DESCRIPTOR(kWasmEntrypointTag)
  DEFINE_PARAMETERS()
  DEFINE_PARAMETER_TYPES()
  DECLARE_DESCRIPTOR(WasmDummyDescriptor)
};

// TODO(wasm): Consider filling in details / defining real descriptors for all
// builtins still using this placeholder descriptor.
class WasmDummyWithJSLinkageDescriptor
    : public StaticCallInterfaceDescriptor<WasmDummyWithJSLinkageDescriptor> {
 public:
  SANDBOX_EXPOSED_DESCRIPTOR(kJSEntrypointTag)
  DEFINE_PARAMETERS()
  DEFINE_PARAMETER_TYPES()
  DECLARE_DESCRIPTOR(WasmDummyWithJSLinkageDescriptor)
};

class AllocateDescriptor
    : public StaticCallInterfaceDescriptor<AllocateDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kRequestedSize)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::TaggedPointer(),  // result 1
                                    MachineType::IntPtr())  // kRequestedSize
  DECLARE_DESCRIPTOR(AllocateDescriptor)

  static constexpr auto registers();
};

class NewHeapNumberDescriptor
    : public StaticCallInterfaceDescriptor<NewHeapNumberDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kValue)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::TaggedPointer(),  // Result
                                    MachineType::Float64())        // kValue
  DECLARE_DESCRIPTOR(NewHeapNumberDescriptor)
};

// This descriptor defines the JavaScript calling convention that can be used
// by stubs: target, new.target, argc and context are passed in registers while
// receiver and the rest of the JS arguments are passed on the stack.
class JSTrampolineDescriptor
    : public StaticJSCallInterfaceDescriptor<JSTrampolineDescriptor> {
 public:
  SANDBOX_EXPOSED_DESCRIPTOR(kJSEntrypointTag)
  DEFINE_JS_PARAMETERS()
  DEFINE_JS_PARAMETER_TYPES()

  DECLARE_JS_COMPATIBLE_DESCRIPTOR(JSTrampolineDescriptor)
};

// Descriptor used for code using the RegExp calling convention, in particular
// the RegExp interpreter trampolines.
class RegExpTrampolineDescriptor
    : public StaticCallInterfaceDescriptor<RegExpTrampolineDescriptor> {
 public:
  SANDBOX_EXPOSED_DESCRIPTOR(kRegExpEntrypointTag)
  DEFINE_PARAMETERS()
  DEFINE_PARAMETER_TYPES()
  DECLARE_DESCRIPTOR(RegExpTrampolineDescriptor)
};

class ContextOnlyDescriptor
    : public StaticCallInterfaceDescriptor<ContextOnlyDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS()
  DEFINE_PARAMETER_TYPES()
  DECLARE_DESCRIPTOR(ContextOnlyDescriptor)

  static constexpr auto registers();
};

class NoContextDescriptor
    : public StaticCallInterfaceDescriptor<NoContextDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT()
  DEFINE_PARAMETER_TYPES()
  DECLARE_DESCRIPTOR(NoContextDescriptor)

  static constexpr auto registers();
};

// LoadDescriptor is used by all stubs that implement Load ICs.
class LoadDescriptor : public StaticCallInterfaceDescriptor<LoadDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kReceiver, kName, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(LoadDescriptor)

  static constexpr inline Register ReceiverRegister();
  static constexpr inline Register NameRegister();
  static constexpr inline Register SlotRegister();

  static constexpr auto registers();
};

// LoadBaselineDescriptor is a load descriptor that does not take a context as
// input.
class LoadBaselineDescriptor
    : public StaticCallInterfaceDescriptor<LoadBaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kReceiver, kName, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(LoadBaselineDescriptor)

  static constexpr auto registers();
};

class LoadGlobalNoFeedbackDescriptor
    : public StaticCallInterfaceDescriptor<LoadGlobalNoFeedbackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kName, kICKind)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned())  // kICKind
  DECLARE_DESCRIPTOR(LoadGlobalNoFeedbackDescriptor)

  static constexpr inline Register ICKindRegister();

  static constexpr auto registers();
};

class LoadNoFeedbackDescriptor
    : public StaticCallInterfaceDescriptor<LoadNoFeedbackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kReceiver, kName, kICKind)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned())  // kICKind
  DECLARE_DESCRIPTOR(LoadNoFeedbackDescriptor)

  static constexpr inline Register ICKindRegister();

  static constexpr auto registers();
};

class LoadGlobalDescriptor
    : public StaticCallInterfaceDescriptor<LoadGlobalDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kName, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(LoadGlobalDescriptor)

  static constexpr auto registers();
};

class LoadGlobalBaselineDescriptor
    : public StaticCallInterfaceDescriptor<LoadGlobalBaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kName, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(LoadGlobalBaselineDescriptor)

  static constexpr auto registers();
};

class LookupWithVectorDescriptor
    : public StaticCallInterfaceDescriptor<LookupWithVectorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kName, kDepth, kSlot, kVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kName
                         MachineType::AnyTagged(),  // kDepth
                         MachineType::AnyTagged(),  // kSlot
                         MachineType::AnyTagged())  // kVector
  DECLARE_DESCRIPTOR(LookupWithVectorDescriptor)
};

class LookupTrampolineDescriptor
    : public StaticCallInterfaceDescriptor<LookupTrampolineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kName, kDepth, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kName
                         MachineType::AnyTagged(),  // kDepth
                         MachineType::AnyTagged())  // kSlot
  DECLARE_DESCRIPTOR(LookupTrampolineDescriptor)
};

class LookupBaselineDescriptor
    : public StaticCallInterfaceDescriptor<LookupBaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kName, kDepth, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kName
                         MachineType::AnyTagged(),  // kDepth
                         MachineType::AnyTagged())  // kSlot
  DECLARE_DESCRIPTOR(LookupBaselineDescriptor)
};

class MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor
    : public StaticCallInterfaceDescriptor<
          MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kFlags, kFeedbackVector, kTemporary)
  DEFINE_PARAMETER_TYPES(MachineType::Int32(),          // kFlags
                         MachineType::TaggedPointer(),  // kFeedbackVector
                         MachineType::AnyTagged())      // kTemporary
  DECLARE_DESCRIPTOR(MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor)

  static constexpr inline Register FlagsRegister();
  static constexpr inline Register FeedbackVectorRegister();

  static constexpr inline Register TemporaryRegister();

  static constexpr inline auto registers();
};

class StoreDescriptor : public StaticCallInterfaceDescriptor<StoreDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kReceiver, kName, kValue, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::AnyTagged(),     // kValue
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(StoreDescriptor)

  static constexpr inline Register ReceiverRegister();
  static constexpr inline Register NameRegister();
  static constexpr inline Register ValueRegister();
  static constexpr inline Register SlotRegister();

  static constexpr auto registers();
};

class StoreNoFeedbackDescriptor
    : public StaticCallInterfaceDescriptor<StoreNoFeedbackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kReceiver, kName, kValue)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kReceiver
                         MachineType::AnyTagged(),  // kName
                         MachineType::AnyTagged())  // kValue
  DECLARE_DESCRIPTOR(StoreNoFeedbackDescriptor)

  static constexpr auto registers();
};

class StoreBaselineDescriptor
    : public StaticCallInterfaceDescriptor<StoreBaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kReceiver, kName, kValue, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::AnyTagged(),     // kValue
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(StoreBaselineDescriptor)

  static constexpr auto registers();
};

class StoreTransitionDescriptor
    : public StaticCallInterfaceDescriptor<StoreTransitionDescriptor> {
 public:
  SANDBOX_EXPOSED_DESCRIPTOR(kStoreTransitionICHandlerEntrypointTag)
  DEFINE_PARAMETERS(kReceiver, kName, kMap, kValue, kSlot, kVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::AnyTagged(),     // kMap
                         MachineType::AnyTagged(),     // kValue
                         MachineType::TaggedSigned(),  // kSlot
                         MachineType::AnyTagged())     // kVector
  DECLARE_DESCRIPTOR(StoreTransitionDescriptor)

  static constexpr inline Register MapRegister();

  static constexpr auto registers();
};

class StoreWithVectorDescriptor
    : public StaticCallInterfaceDescriptor<StoreWithVectorDescriptor> {
 public:
  SANDBOX_EXPOSED_DESCRIPTOR(kStoreWithVectorICHandlerEntrypointTag)
  DEFINE_PARAMETERS(kReceiver, kName, kValue, kSlot, kVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::AnyTagged(),     // kValue
                         MachineType::TaggedSigned(),  // kSlot
                         MachineType::AnyTagged())     // kVector
  DECLARE_DESCRIPTOR(StoreWithVectorDescriptor)

  static constexpr inline Register VectorRegister();

  static constexpr auto registers();
};

class StoreGlobalDescriptor
    : public StaticCallInterfaceDescriptor<StoreGlobalDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kName, kValue, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kName
                         MachineType::AnyTagged(),     // kValue
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(StoreGlobalDescriptor)

  static constexpr auto registers();
};

class StoreGlobalBaselineDescriptor
    : public StaticCallInterfaceDescriptor<StoreGlobalBaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kName, kValue, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kName
                         MachineType::AnyTagged(),     // kValue
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(StoreGlobalBaselineDescriptor)

  static constexpr auto registers();
};

class StoreGlobalWithVectorDescriptor
    : public StaticCallInterfaceDescriptor<StoreGlobalWithVectorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kName, kValue, kSlot, kVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kName
                         MachineType::AnyTagged(),     // kValue
                         MachineType::TaggedSigned(),  // kSlot
                         MachineType::AnyTagged())     // kVector
  DECLARE_DESCRIPTOR(StoreGlobalWithVectorDescriptor)

  static constexpr auto registers();
};

class DefineKeyedOwnDescriptor
    : public StaticCallInterfaceDescriptor<DefineKeyedOwnDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kReceiver, kName, kValue, kFlags, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::AnyTagged(),     // kValue
                         MachineType::TaggedSigned(),  // kFlags
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(DefineKeyedOwnDescriptor)

  static constexpr inline Register FlagsRegister();

  static constexpr auto registers();
};

class DefineKeyedOwnBaselineDescriptor
    : public StaticCallInterfaceDescriptor<DefineKeyedOwnBaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kReceiver, kName, kValue, kFlags, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::AnyTagged(),     // kValue
                         MachineType::TaggedSigned(),  // kFlags
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(DefineKeyedOwnBaselineDescriptor)

  static constexpr auto registers();
};

class DefineKeyedOwnWithVectorDescriptor
    : public StaticCallInterfaceDescriptor<DefineKeyedOwnWithVectorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kReceiver, kName, kValue, kFlags,
                    kSlot,   // register argument
                    kVector  // stack argument
  )
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::AnyTagged(),     // kValue
                         MachineType::TaggedSigned(),  // kFlags
                         MachineType::TaggedSigned(),  // kSlot
                         MachineType::AnyTagged())     // kVector
  DECLARE_DESCRIPTOR(DefineKeyedOwnWithVectorDescriptor)

  static constexpr auto registers();
};

class LoadWithVectorDescriptor
    : public StaticCallInterfaceDescriptor<LoadWithVectorDescriptor> {
 public:
  SANDBOX_EXPOSED_DESCRIPTOR(kLoadWithVectorICHandlerEntrypointTag)
  // TODO(v8:9497): Revert the Machine type for kSlot to the
  // TaggedSigned once Torque can emit better call descriptors
  DEFINE_PARAMETERS(kReceiver, kName, kSlot, kVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kReceiver
                         MachineType::AnyTagged(),  // kName
                         MachineType::AnyTagged(),  // kSlot
                         MachineType::AnyTagged())  // kVector
  DECLARE_DESCRIPTOR(LoadWithVectorDescriptor)

  static constexpr inline Register VectorRegister();

  static constexpr auto registers();
};

class KeyedLoadBaselineDescriptor
    : public StaticCallInterfaceDescriptor<KeyedLoadBaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kReceiver, kName, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(KeyedLoadBaselineDescriptor)

  static constexpr inline Register ReceiverRegister();
  static constexpr inline Register NameRegister();
  static constexpr inline Register SlotRegister();

  static constexpr auto registers();
};

class KeyedLoadDescriptor
    : public StaticCallInterfaceDescriptor<KeyedLoadDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kReceiver, kName, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(KeyedLoadDescriptor)

  static constexpr auto registers();
};

class KeyedLoadWithVectorDescriptor
    : public StaticCallInterfaceDescriptor<KeyedLoadWithVectorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kReceiver, kName, kSlot, kVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned(),  // kSlot
                         MachineType::AnyTagged())     // kVector
  DECLARE_DESCRIPTOR(KeyedLoadWithVectorDescriptor)

  static constexpr inline Register VectorRegister();

  static constexpr auto registers();
};

class EnumeratedKeyedLoadBaselineDescriptor
    : public StaticCallInterfaceDescriptor<
          EnumeratedKeyedLoadBaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kReceiver, kName, kEnumIndex, kCacheType, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned(),  // kEnumIndex
                         MachineType::AnyTagged(),     // kCacheType
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(EnumeratedKeyedLoadBaselineDescriptor)

  static constexpr inline Register EnumIndexRegister();
  static constexpr inline Register CacheTypeRegister();
  static constexpr inline Register SlotRegister();

  static constexpr auto registers();
};

class EnumeratedKeyedLoadDescriptor
    : public StaticCallInterfaceDescriptor<EnumeratedKeyedLoadDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kReceiver, kName, kEnumIndex, kCacheType, kSlot, kVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned(),  // kEnumIndex
                         MachineType::AnyTagged(),     // kCacheType
                         MachineType::TaggedSigned(),  // kSlot
                         MachineType::AnyTagged())     // kVector
  DECLARE_DESCRIPTOR(EnumeratedKeyedLoadDescriptor)

  static constexpr auto registers();
};

class KeyedHasICBaselineDescriptor
    : public StaticCallInterfaceDescriptor<KeyedHasICBaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kReceiver, kName, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(KeyedHasICBaselineDescriptor)

  static constexpr inline Register ReceiverRegister();
  static constexpr inline Register NameRegister();
  static constexpr inline Register SlotRegister();

  static constexpr auto registers();
};

class KeyedHasICWithVectorDescriptor
    : public StaticCallInterfaceDescriptor<KeyedHasICWithVectorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kReceiver, kName, kSlot, kVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kReceiver
                         MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned(),  // kSlot
                         MachineType::AnyTagged())     // kVector
  DECLARE_DESCRIPTOR(KeyedHasICWithVectorDescriptor)

  static constexpr inline Register VectorRegister();

  static constexpr auto registers();
};

// Like LoadWithVectorDescriptor, except we pass the receiver (the object which
// should be used as the receiver for accessor function calls) and the lookup
// start object separately.
class LoadWithReceiverAndVectorDescriptor
    : public StaticCallInterfaceDescriptor<
          LoadWithReceiverAndVectorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  // TODO(v8:9497): Revert the Machine type for kSlot to the
  // TaggedSigned once Torque can emit better call descriptors
  DEFINE_PARAMETERS(kReceiver, kLookupStartObject, kName, kSlot, kVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kReceiver
                         MachineType::AnyTagged(),  // kLookupStartObject
                         MachineType::AnyTagged(),  // kName
                         MachineType::AnyTagged(),  // kSlot
                         MachineType::AnyTagged())  // kVector
  DECLARE_DESCRIPTOR(LoadWithReceiverAndVectorDescriptor)

  static constexpr inline Register LookupStartObjectRegister();

  static constexpr auto registers();
};

class LoadWithReceiverBaselineDescriptor
    : public StaticCallInterfaceDescriptor<LoadWithReceiverBaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  // TODO(v8:9497): Revert the Machine type for kSlot to the
  // TaggedSigned once Torque can emit better call descriptors
  DEFINE_PARAMETERS_NO_CONTEXT(kReceiver, kLookupStartObject, kName, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kReceiver
                         MachineType::AnyTagged(),  // kLookupStartObject
                         MachineType::AnyTagged(),  // kName
                         MachineType::AnyTagged())  // kSlot
  DECLARE_DESCRIPTOR(LoadWithReceiverBaselineDescriptor)

  static constexpr auto registers();
};

class LoadGlobalWithVectorDescriptor
    : public StaticCallInterfaceDescriptor<LoadGlobalWithVectorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kName, kSlot, kVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kName
                         MachineType::TaggedSigned(),  // kSlot
                         MachineType::AnyTagged())     // kVector
  DECLARE_DESCRIPTOR(LoadGlobalWithVectorDescriptor)

  static constexpr inline Register VectorRegister();

  static constexpr auto registers();
};

class FastNewObjectDescriptor
    : public StaticCallInterfaceDescriptor<FastNewObjectDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kTarget, kNewTarget)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kTarget
                         MachineType::AnyTagged())  // kNewTarget
  DECLARE_DESCRIPTOR(FastNewObjectDescriptor)

  static constexpr inline Register TargetRegister();
  static constexpr inline Register NewTargetRegister();

  static constexpr auto registers();
};

class WriteBarrierDescriptor final
    : public StaticCallInterfaceDescriptor<WriteBarrierDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kObject, kSlotAddress)
  DEFINE_PARAMETER_TYPES(MachineType::TaggedPointer(),  // kObject
                         MachineType::Pointer())        // kSlotAddress

  DECLARE_DESCRIPTOR(WriteBarrierDescriptor)
  static constexpr auto registers();
  static constexpr bool kRestrictAllocatableRegisters = true;
  static constexpr bool kCalleeSaveRegisters = true;
  static constexpr inline Register ObjectRegister();
  static constexpr inline Register SlotAddressRegister();
  // A temporary register used in helpers.
  static constexpr inline Register ValueRegister();
  static constexpr inline RegList ComputeSavedRegisters(
      Register object, Register slot_address = no_reg);
#if DEBUG
  static void Verify(CallInterfaceDescriptorData* data);
#endif
};

// Write barriers for indirect pointer field writes require one additional
// parameter (the IndirectPointerTag associated with the stored field).
// Otherwise, they are identical to the other write barriers.
class IndirectPointerWriteBarrierDescriptor final
    : public StaticCallInterfaceDescriptor<
          IndirectPointerWriteBarrierDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kObject, kSlotAddress, kIndirectPointerTag)
  DEFINE_PARAMETER_TYPES(MachineType::TaggedPointer(),  // kObject
                         MachineType::Pointer(),        // kSlotAddress
                         MachineType::Uint64())         // kIndirectPointerTag

  DECLARE_DESCRIPTOR(IndirectPointerWriteBarrierDescriptor)
  static constexpr auto registers();
  static constexpr bool kRestrictAllocatableRegisters = true;
  static constexpr bool kCalleeSaveRegisters = true;
  static constexpr inline Register ObjectRegister();
  static constexpr inline Register SlotAddressRegister();
  static constexpr inline Register IndirectPointerTagRegister();
  static constexpr inline RegList ComputeSavedRegisters(
      Register object, Register slot_address = no_reg);
#if DEBUG
  static void Verify(CallInterfaceDescriptorData* data);
#endif
};

#ifdef V8_IS_TSAN
class TSANStoreDescriptor final
    : public StaticCallInterfaceDescriptor<TSANStoreDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kAddress, kValue)
  DEFINE_PARAMETER_TYPES(MachineType::Pointer(),    // kAddress
                         MachineType::AnyTagged())  // kValue

  DECLARE_DESCRIPTOR(TSANStoreDescriptor)

  static constexpr auto registers();
  static constexpr bool kRestrictAllocatableRegisters = true;
};

class TSANLoadDescriptor final
    : public StaticCallInterfaceDescriptor<TSANLoadDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kAddress)
  DEFINE_PARAMETER_TYPES(MachineType::Pointer())  // kAddress

  DECLARE_DESCRIPTOR(TSANLoadDescriptor)

  static constexpr auto registers();
  static constexpr bool kRestrictAllocatableRegisters = true;
};

#endif  // V8_IS_TSAN

class TypeConversionDescriptor final
    : public StaticCallInterfaceDescriptor<TypeConversionDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kArgument)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged())
  DECLARE_DESCRIPTOR(TypeConversionDescriptor)

  static constexpr inline Register ArgumentRegister();

  static constexpr auto registers();
};

class TypeConversionNoContextDescriptor final
    : public StaticCallInterfaceDescriptor<TypeConversionNoContextDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kArgument)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged())
  DECLARE_DESCRIPTOR(TypeConversionNoContextDescriptor)

  static constexpr auto registers();
};

class TypeConversion_BaselineDescriptor final
    : public StaticCallInterfaceDescriptor<TypeConversion_BaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kArgument, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(), MachineType::UintPtr())
  DECLARE_DESCRIPTOR(TypeConversion_BaselineDescriptor)
};

class SingleParameterOnStackDescriptor final
    : public StaticCallInterfaceDescriptor<SingleParameterOnStackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kArgument)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged())
  DECLARE_DESCRIPTOR(SingleParameterOnStackDescriptor)

  static constexpr auto registers();
};

class AsyncFunctionStackParameterDescriptor final
    : public StaticCallInterfaceDescriptor<
          AsyncFunctionStackParameterDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kPromise, kResult)
  DEFINE_PARAMETER_TYPES(MachineType::TaggedPointer(), MachineType::AnyTagged())
  DECLARE_DESCRIPTOR(AsyncFunctionStackParameterDescriptor)

  static constexpr auto registers();
};

class GetIteratorStackParameterDescriptor final
    : public StaticCallInterfaceDescriptor<
          GetIteratorStackParameterDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kReceiver, kCallSlot, kFeedback, kResult)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(), MachineType::AnyTagged(),
                         MachineType::AnyTagged(), MachineType::AnyTagged())
  DECLARE_DESCRIPTOR(GetIteratorStackParameterDescriptor)

  static constexpr auto registers();
};

class GetPropertyDescriptor final
    : public StaticCallInterfaceDescriptor<GetPropertyDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kObject, kKey)
  DECLARE_DEFAULT_DESCRIPTOR(GetPropertyDescriptor)
};

class TypeofDescriptor
    : public StaticCallInterfaceDescriptor<TypeofDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kObject)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged())
  DECLARE_DESCRIPTOR(TypeofDescriptor)

  static constexpr inline auto registers();
};

class CallTrampolineDescriptor
    : public StaticCallInterfaceDescriptor<CallTrampolineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_VARARGS(kFunction, kActualArgumentsCount)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kFunction
                         MachineType::Int32())      // kActualArgumentsCount
  DECLARE_DESCRIPTOR(CallTrampolineDescriptor)

  static constexpr inline auto registers();
};

class CopyDataPropertiesWithExcludedPropertiesDescriptor
    : public StaticCallInterfaceDescriptor<
          CopyDataPropertiesWithExcludedPropertiesDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_VARARGS(kSource, kExcludedPropertyCount)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kSource
                         MachineType::AnyTagged())  // kExcludedPropertyCount
  DECLARE_DESCRIPTOR(CopyDataPropertiesWithExcludedPropertiesDescriptor)

  static constexpr inline auto registers();
};

class CopyDataPropertiesWithExcludedPropertiesOnStackDescriptor
    : public StaticCallInterfaceDescriptor<
          CopyDataPropertiesWithExcludedPropertiesOnStackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kSource, kExcludedPropertyCount, kExcludedPropertyBase)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kSource
                         MachineType::IntPtr(),
                         MachineType::IntPtr())  // kExcludedPropertyCount
  DECLARE_DESCRIPTOR(CopyDataPropertiesWithExcludedPropertiesOnStackDescriptor)

  static constexpr inline auto registers();
};

class CallVarargsDescriptor
    : public StaticCallInterfaceDescriptor<CallVarargsDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_VARARGS(kTarget, kActualArgumentsCount, kArgumentsLength,
                            kArgumentsList)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kTarget
                         MachineType::Int32(),      // kActualArgumentsCount
                         MachineType::Int32(),      // kArgumentsLength
                         MachineType::AnyTagged())  // kArgumentsList
  DECLARE_DESCRIPTOR(CallVarargsDescriptor)

  static constexpr inline auto registers();
};

class CallForwardVarargsDescriptor
    : public StaticCallInterfaceDescriptor<CallForwardVarargsDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_VARARGS(kTarget, kActualArgumentsCount, kStartIndex)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kTarget
                         MachineType::Int32(),      // kActualArgumentsCount
                         MachineType::Int32())      // kStartIndex
  DECLARE_DESCRIPTOR(CallForwardVarargsDescriptor)

  static constexpr inline auto registers();
};

class CallFunctionTemplateDescriptor
    : public StaticCallInterfaceDescriptor<CallFunctionTemplateDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_VARARGS(kFunctionTemplateInfo, kArgumentsCount)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kFunctionTemplateInfo
                         MachineType::Int32())      // kArgumentsCount
  DECLARE_DESCRIPTOR(CallFunctionTemplateDescriptor)

  static constexpr inline auto registers();
};

class CallFunctionTemplateGenericDescriptor
    : public StaticCallInterfaceDescriptor<
          CallFunctionTemplateGenericDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_VARARGS(kFunctionTemplateInfo, kArgumentsCount,
                            kTopmostScriptHavingContext)
  DEFINE_PARAMETER_TYPES(
      MachineType::AnyTagged(),  // kFunctionTemplateInfo
      MachineType::Int32(),      // kArgumentsCount
      MachineType::AnyTagged())  // kTopmostScriptHavingContext
  DECLARE_DESCRIPTOR(CallFunctionTemplateGenericDescriptor)

  static constexpr inline auto registers();
};

class CallWithSpreadDescriptor
    : public StaticCallInterfaceDescriptor<CallWithSpreadDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_VARARGS(kTarget, kArgumentsCount, kSpread)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kTarget
                         MachineType::Int32(),      // kArgumentsCount
                         MachineType::AnyTagged())  // kSpread
  DECLARE_DESCRIPTOR(CallWithSpreadDescriptor)

  static constexpr inline auto registers();
};

class CallWithSpread_BaselineDescriptor
    : public StaticCallInterfaceDescriptor<CallWithSpread_BaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT_VARARGS(kTarget, kArgumentsCount, kSpread, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kTarget
                         MachineType::Int32(),      // kArgumentsCount
                         MachineType::AnyTagged(),  // kSpread
                         MachineType::UintPtr())    // kSlot
  DECLARE_DESCRIPTOR(CallWithSpread_BaselineDescriptor)
};

class CallWithSpread_WithFeedbackDescriptor
    : public StaticCallInterfaceDescriptor<
          CallWithSpread_WithFeedbackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_VARARGS(kTarget, kArgumentsCount, kSpread, kSlot,
                            kFeedbackVector, kReceiver)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kTarget
                         MachineType::Int32(),      // kArgumentsCount
                         MachineType::AnyTagged(),  // kSpread
                         MachineType::UintPtr(),    // kSlot
                         MachineType::AnyTagged(),  // kFeedbackVector
                         MachineType::AnyTagged())  // kReceiver
  DECLARE_DESCRIPTOR(CallWithSpread_WithFeedbackDescriptor)
};

class CallWithArrayLikeDescriptor
    : public StaticCallInterfaceDescriptor<CallWithArrayLikeDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kTarget, kArgumentsList)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kTarget
                         MachineType::AnyTagged())  // kArgumentsList
  DECLARE_DESCRIPTOR(CallWithArrayLikeDescriptor)

  static constexpr inline auto registers();
};

class CallWithArrayLike_WithFeedbackDescriptor
    : public StaticCallInterfaceDescriptor<
          CallWithArrayLike_WithFeedbackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kTarget, kArgumentsList, kSlot, kFeedbackVector, kReceiver)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kTarget
                         MachineType::AnyTagged(),  // kArgumentsList
                         MachineType::UintPtr(),    // kSlot
                         MachineType::AnyTagged(),  // kFeedbackVector
                         MachineType::AnyTagged())  // kReceiver
  DECLARE_DESCRIPTOR(CallWithArrayLike_WithFeedbackDescriptor)
};

class ConstructVarargsDescriptor
    : public StaticCallInterfaceDescriptor<ConstructVarargsDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_JS_PARAMETERS(kArgumentsLength, kArgumentsList)
  DEFINE_JS_PARAMETER_TYPES(MachineType::Int32(),      // kArgumentsLength
                            MachineType::AnyTagged())  // kArgumentsList

  DECLARE_DESCRIPTOR(ConstructVarargsDescriptor)

  static constexpr inline auto registers();
};

class ConstructForwardVarargsDescriptor
    : public StaticCallInterfaceDescriptor<ConstructForwardVarargsDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_JS_PARAMETERS(kStartIndex)
  DEFINE_JS_PARAMETER_TYPES(MachineType::Int32())
  DECLARE_DESCRIPTOR(ConstructForwardVarargsDescriptor)

  static constexpr inline auto registers();
};

class ConstructWithSpreadDescriptor
    : public StaticCallInterfaceDescriptor<ConstructWithSpreadDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_JS_PARAMETERS(kSpread)
  DEFINE_JS_PARAMETER_TYPES(MachineType::AnyTagged())
  DECLARE_DESCRIPTOR(ConstructWithSpreadDescriptor)

  static constexpr inline auto registers();
};

class ConstructWithSpread_BaselineDescriptor
    : public StaticCallInterfaceDescriptor<
          ConstructWithSpread_BaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_JS_PARAMETERS_NO_CONTEXT(kSpread, kSlot)
  DEFINE_JS_PARAMETER_TYPES(MachineType::AnyTagged(),  // kSpread
                            MachineType::AnyTagged())  // kSlot
  DECLARE_DESCRIPTOR(ConstructWithSpread_BaselineDescriptor)
};

class ConstructWithSpread_WithFeedbackDescriptor
    : public StaticCallInterfaceDescriptor<
          ConstructWithSpread_WithFeedbackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_JS_PARAMETERS(kSpread, kSlot, kVector)
  DEFINE_JS_PARAMETER_TYPES(MachineType::AnyTagged(),  // kSpread
                            MachineType::AnyTagged(),  // kSlot
                            MachineType::AnyTagged())  // kVector
  DECLARE_DESCRIPTOR(ConstructWithSpread_WithFeedbackDescriptor)
};

class ConstructWithArrayLikeDescriptor
    : public StaticCallInterfaceDescriptor<ConstructWithArrayLikeDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kTarget, kNewTarget, kArgumentsList)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kTarget
                         MachineType::AnyTagged(),  // kNewTarget
                         MachineType::AnyTagged())  // kArgumentsList
  DECLARE_DESCRIPTOR(ConstructWithArrayLikeDescriptor)

  static constexpr inline auto registers();
};

class ConstructForwardAllArgsDescriptor
    : public StaticCallInterfaceDescriptor<ConstructForwardAllArgsDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kConstructor, kNewTarget)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kConstructor
                         MachineType::AnyTagged())  // kNewTarget
  DECLARE_DESCRIPTOR(ConstructForwardAllArgsDescriptor)

  static constexpr inline auto registers();
};

class ConstructForwardAllArgs_BaselineDescriptor
    : public StaticCallInterfaceDescriptor<
          ConstructForwardAllArgs_BaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kTarget, kNewTarget, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kTarget
                         MachineType::AnyTagged(),  // kNewTarget
                         MachineType::AnyTagged())  // kSlot
  DECLARE_DESCRIPTOR(ConstructForwardAllArgs_BaselineDescriptor)
};

class ConstructForwardAllArgs_WithFeedbackDescriptor
    : public StaticCallInterfaceDescriptor<
          ConstructForwardAllArgs_WithFeedbackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kTarget, kNewTarget, kSlot, kVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kTarget
                         MachineType::AnyTagged(),  // kNewTarget
                         MachineType::AnyTagged(),  // kSlot
                         MachineType::AnyTagged())  // kVector
  DECLARE_DESCRIPTOR(ConstructForwardAllArgs_WithFeedbackDescriptor)
};

// TODO(ishell): consider merging this with ArrayConstructorDescriptor
class ConstructStubDescriptor
    : public StaticCallInterfaceDescriptor<ConstructStubDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_JS_PARAMETERS()
  DEFINE_JS_PARAMETER_TYPES()

  // TODO(ishell): Use DECLARE_JS_COMPATIBLE_DESCRIPTOR if registers match
  DECLARE_DESCRIPTOR(ConstructStubDescriptor)

  static constexpr inline auto registers();
};

class AbortDescriptor : public StaticCallInterfaceDescriptor<AbortDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kMessageOrMessageId)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged())
  DECLARE_DESCRIPTOR(AbortDescriptor)

  static constexpr inline auto registers();
};

class ArrayConstructorDescriptor
    : public StaticJSCallInterfaceDescriptor<ArrayConstructorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_JS_PARAMETERS(kAllocationSite)
  DEFINE_JS_PARAMETER_TYPES(MachineType::AnyTagged())

  DECLARE_JS_COMPATIBLE_DESCRIPTOR(ArrayConstructorDescriptor)
};

class ArrayNArgumentsConstructorDescriptor
    : public StaticCallInterfaceDescriptor<
          ArrayNArgumentsConstructorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  // This descriptor declares only register arguments while respective number
  // of JS arguments stay on the expression stack.
  // The ArrayNArgumentsConstructor builtin does not access stack arguments
  // directly it just forwards them to the runtime function.
  DEFINE_PARAMETERS(kFunction, kAllocationSite, kActualArgumentsCount)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kFunction,
                         MachineType::AnyTagged(),  // kAllocationSite
                         MachineType::Int32())      // kActualArgumentsCount
  DECLARE_DESCRIPTOR(ArrayNArgumentsConstructorDescriptor)

  static constexpr auto registers();
};

class ArrayNoArgumentConstructorDescriptor
    : public StaticCallInterfaceDescriptor<
          ArrayNoArgumentConstructorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  // This descriptor declares same register arguments as the parent
  // ArrayNArgumentsConstructorDescriptor and it declares indices for
  // JS arguments passed on the expression stack.
  DEFINE_PARAMETERS(kFunction, kAllocationSite, kActualArgumentsCount,
                    kFunctionParameter)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kFunction
                         MachineType::AnyTagged(),  // kAllocationSite
                         MachineType::Int32(),      // kActualArgumentsCount
                         MachineType::AnyTagged())  // kFunctionParameter
  DECLARE_DESCRIPTOR(ArrayNoArgumentConstructorDescriptor)

  static constexpr auto registers();
};

class ArraySingleArgumentConstructorDescriptor
    : public StaticCallInterfaceDescriptor<
          ArraySingleArgumentConstructorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  // This descriptor declares same register arguments as the parent
  // ArrayNArgumentsConstructorDescriptor and it declares indices for
  // JS arguments passed on the expression stack.
  DEFINE_PARAMETERS(kFunction, kAllocationSite, kActualArgumentsCount,
                    kArraySizeSmiParameter, kReceiverParameter)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kFunction
                         MachineType::AnyTagged(),  // kAllocationSite
                         MachineType::Int32(),      // kActualArgumentsCount
                         // JS arguments on the stack
                         MachineType::AnyTagged(),  // kArraySizeSmiParameter
                         MachineType::AnyTagged())  // kReceiverParameter
  DECLARE_DESCRIPTOR(ArraySingleArgumentConstructorDescriptor)

  static constexpr auto registers();
};

class CompareDescriptor
    : public StaticCallInterfaceDescriptor<CompareDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kLeft, kRight)
  DECLARE_DESCRIPTOR(CompareDescriptor)

  static constexpr inline auto registers();
};

class CompareNoContextDescriptor
    : public StaticCallInterfaceDescriptor<CompareNoContextDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kLeft, kRight)
  DECLARE_DESCRIPTOR(CompareNoContextDescriptor)

  static constexpr inline auto registers();
};

class StringEqualDescriptor
    : public StaticCallInterfaceDescriptor<StringEqualDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kLeft, kRight, kLength)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kLeft
                         MachineType::AnyTagged(),  // kRight
                         MachineType::IntPtr())     // kLength
  DECLARE_DEFAULT_DESCRIPTOR(StringEqualDescriptor)
};

class BinaryOpDescriptor
    : public StaticCallInterfaceDescriptor<BinaryOpDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kLeft, kRight)
  DECLARE_DESCRIPTOR(BinaryOpDescriptor)

  static constexpr inline auto registers();
};

class BinaryOp_BaselineDescriptor
    : public StaticCallInterfaceDescriptor<BinaryOp_BaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kLeft, kRight, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kLeft
                         MachineType::AnyTagged(),  // kRight
                         MachineType::UintPtr())    // kSlot
  DECLARE_DESCRIPTOR(BinaryOp_BaselineDescriptor)

  static constexpr inline auto registers();
};

class BinarySmiOp_BaselineDescriptor
    : public StaticCallInterfaceDescriptor<BinarySmiOp_BaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kLeft, kRight, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kLeft
                         MachineType::TaggedSigned(),  // kRight
                         MachineType::UintPtr())       // kSlot
  DECLARE_DESCRIPTOR(BinarySmiOp_BaselineDescriptor)

  static constexpr inline auto registers();
};

class StringAtAsStringDescriptor final
    : public StaticCallInterfaceDescriptor<StringAtAsStringDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kReceiver, kPosition)
  // TODO(turbofan): Return untagged value here.
  DEFINE_RESULT_AND_PARAMETER_TYPES(
      MachineType::TaggedPointer(),  // result string
      MachineType::AnyTagged(),      // kReceiver
      MachineType::IntPtr())         // kPosition
  DECLARE_DESCRIPTOR(StringAtAsStringDescriptor)
};

class StringSubstringDescriptor final
    : public StaticCallInterfaceDescriptor<StringSubstringDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kString, kFrom, kTo)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kString
                         MachineType::IntPtr(),     // kFrom
                         MachineType::IntPtr())     // kTo

  // TODO(turbofan): Allow builtins to return untagged values.
  DECLARE_DESCRIPTOR(StringSubstringDescriptor)
};

class CppBuiltinAdaptorDescriptor
    : public StaticJSCallInterfaceDescriptor<CppBuiltinAdaptorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_JS_PARAMETERS(kCFunction)
  DEFINE_JS_PARAMETER_TYPES(MachineType::Pointer())
  DECLARE_JS_COMPATIBLE_DESCRIPTOR(CppBuiltinAdaptorDescriptor)
};

class CreateFromSlowBoilerplateHelperDescriptor
    : public StaticCallInterfaceDescriptor<
          CreateFromSlowBoilerplateHelperDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_RESULT_AND_PARAMETERS(2, kAllocationSite, kBoilerplate)
  DEFINE_RESULT_AND_PARAMETER_TYPES(
      MachineType::AnyTagged(),  // result 1 (object)
      MachineType::AnyTagged(),  // result 2 (allocation site)
      MachineType::AnyTagged(),  // kAllocationSite
      MachineType::AnyTagged())  // kBoilerplate
  DECLARE_DESCRIPTOR(CreateFromSlowBoilerplateHelperDescriptor)
};

class CEntry1ArgvOnStackDescriptor
    : public StaticCallInterfaceDescriptor<CEntry1ArgvOnStackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kArity,          // register argument
                    kCFunction,      // register argument
                    kPadding,        // stack argument 1 (just padding)
                    kArgcSmi,        // stack argument 2
                    kTargetCopy,     // stack argument 3
                    kNewTargetCopy)  // stack argument 4
  DEFINE_PARAMETER_TYPES(MachineType::Int32(),      // kArity
                         MachineType::Pointer(),    // kCFunction
                         MachineType::AnyTagged(),  // kPadding
                         MachineType::AnyTagged(),  // kArgcSmi
                         MachineType::AnyTagged(),  // kTargetCopy
                         MachineType::AnyTagged())  // kNewTargetCopy
  DECLARE_DESCRIPTOR(CEntry1ArgvOnStackDescriptor)

  static constexpr auto registers();
};

class CallApiCallbackOptimizedDescriptor
    : public StaticCallInterfaceDescriptor<CallApiCallbackOptimizedDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_VARARGS(kApiFunctionAddress, kActualArgumentsCount,
                            kFunctionTemplateInfo, kHolder)
  //                           receiver is implicit stack argument 1
  //                           argv are implicit stack arguments [2, 2 + kArgc[
  DEFINE_PARAMETER_TYPES(MachineType::Pointer(),    // kApiFunctionAddress
                         MachineType::Int32(),      // kActualArgumentsCount
                         MachineType::AnyTagged(),  // kFunctionTemplateInfo
                         MachineType::AnyTagged())  // kHolder
  DECLARE_DESCRIPTOR(CallApiCallbackOptimizedDescriptor)

  static constexpr inline Register ApiFunctionAddressRegister();
  static constexpr inline Register ActualArgumentsCountRegister();
  static constexpr inline Register FunctionTemplateInfoRegister();
  static constexpr inline Register HolderRegister();

  static constexpr inline auto registers();
};

class CallApiCallbackGenericDescriptor
    : public StaticCallInterfaceDescriptor<CallApiCallbackGenericDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_VARARGS(kActualArgumentsCount, kTopmostScriptHavingContext,
                            kFunctionTemplateInfo, kHolder)
  //                           receiver is implicit stack argument 1
  //                           argv are implicit stack arguments [2, 2 + kArgc[
  DEFINE_PARAMETER_TYPES(
      MachineType::Int32(),      // kActualArgumentsCount
      MachineType::AnyTagged(),  // kTopmostScriptHavingContext
      MachineType::AnyTagged(),  // kFunctionTemplateInfo
      MachineType::AnyTagged())  // kHolder
  DECLARE_DESCRIPTOR(CallApiCallbackGenericDescriptor)

  static constexpr inline Register ActualArgumentsCountRegister();
  static constexpr inline Register TopmostScriptHavingContextRegister();
  static constexpr inline Register FunctionTemplateInfoRegister();
  static constexpr inline Register HolderRegister();

  static constexpr inline auto registers();
};

class ApiGetterDescriptor
    : public StaticCallInterfaceDescriptor<ApiGetterDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kReceiver, kHolder, kCallback)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kReceiver
                         MachineType::AnyTagged(),  // kHolder
                         MachineType::AnyTagged())  // kCallback
  DECLARE_DESCRIPTOR(ApiGetterDescriptor)

  static constexpr inline Register ReceiverRegister();
  static constexpr inline Register HolderRegister();
  static constexpr inline Register CallbackRegister();

  static constexpr auto registers();
};

// TODO(turbofan): We should probably rename this to GrowFastElementsDescriptor.
class GrowArrayElementsDescriptor
    : public StaticCallInterfaceDescriptor<GrowArrayElementsDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kObject, kKey)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kObject
                         MachineType::AnyTagged())  // kKey
  DECLARE_DESCRIPTOR(GrowArrayElementsDescriptor)

  static constexpr inline Register ObjectRegister();
  static constexpr inline Register KeyRegister();

  static constexpr auto registers();
};

class BaselineOutOfLinePrologueDescriptor
    : public StaticCallInterfaceDescriptor<
          BaselineOutOfLinePrologueDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kCalleeContext, kClosure,
                               kJavaScriptCallArgCount, kStackFrameSize,
                               kJavaScriptCallNewTarget,
                               kInterpreterBytecodeArray)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kCalleeContext
                         MachineType::AnyTagged(),  // kClosure
                         MachineType::Int32(),      // kJavaScriptCallArgCount
                         MachineType::Int32(),      // kStackFrameSize
                         MachineType::AnyTagged(),  // kJavaScriptCallNewTarget
                         MachineType::AnyTagged())  // kInterpreterBytecodeArray
  DECLARE_DESCRIPTOR(BaselineOutOfLinePrologueDescriptor)

  static constexpr inline auto registers();

  // We pass the context manually, so we have one extra register.
  static constexpr int kMaxRegisterParams =
      StaticCallInterfaceDescriptor::kMaxRegisterParams + 1;
};

class BaselineLeaveFrameDescriptor
    : public StaticCallInterfaceDescriptor<BaselineLeaveFrameDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kParamsSize, kWeight)
  DEFINE_PARAMETER_TYPES(MachineType::Int32(),  // kParamsSize
                         MachineType::Int32())  // kWeight
  DECLARE_DESCRIPTOR(BaselineLeaveFrameDescriptor)

  static constexpr inline Register ParamsSizeRegister();
  static constexpr inline Register WeightRegister();

  static constexpr inline auto registers();
};

class OnStackReplacementDescriptor
    : public StaticCallInterfaceDescriptor<OnStackReplacementDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kMaybeTargetCode)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged())  // kMaybeTargetCode
  DECLARE_DESCRIPTOR(OnStackReplacementDescriptor)

  static constexpr inline Register MaybeTargetCodeRegister();

  static constexpr inline auto registers();
};

class V8_EXPORT_PRIVATE InterpreterDispatchDescriptor
    : public StaticCallInterfaceDescriptor<InterpreterDispatchDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kAccumulator, kBytecodeOffset, kBytecodeArray,
                    kDispatchTable)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kAccumulator
                         MachineType::IntPtr(),     // kBytecodeOffset
                         MachineType::AnyTagged(),  // kBytecodeArray
                         MachineType::IntPtr())     // kDispatchTable
  DECLARE_DESCRIPTOR(InterpreterDispatchDescriptor)

  static constexpr inline auto registers();
};

class InterpreterPushArgsThenCallDescriptor
    : public StaticCallInterfaceDescriptor<
          InterpreterPushArgsThenCallDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kNumberOfArguments, kFirstArgument, kFunction)
  DEFINE_PARAMETER_TYPES(MachineType::Int32(),      // kNumberOfArguments
                         MachineType::Pointer(),    // kFirstArgument
                         MachineType::AnyTagged())  // kFunction
  DECLARE_DESCRIPTOR(InterpreterPushArgsThenCallDescriptor)

  static constexpr inline auto registers();
};

class InterpreterPushArgsThenConstructDescriptor
    : public StaticCallInterfaceDescriptor<
          InterpreterPushArgsThenConstructDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kNumberOfArguments, kFirstArgument, kConstructor,
                    kNewTarget, kFeedbackElement)
  DEFINE_PARAMETER_TYPES(MachineType::Int32(),      // kNumberOfArguments
                         MachineType::Pointer(),    // kFirstArgument
                         MachineType::AnyTagged(),  // kConstructor
                         MachineType::AnyTagged(),  // kNewTarget
                         MachineType::AnyTagged())  // kFeedbackElement
  DECLARE_DESCRIPTOR(InterpreterPushArgsThenConstructDescriptor)

  static constexpr inline auto registers();
};

class InterpreterCEntry1Descriptor
    : public StaticCallInterfaceDescriptor<InterpreterCEntry1Descriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_RESULT_AND_PARAMETERS(1, kNumberOfArguments, kFirstArgument,
                               kFunctionEntry)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::AnyTagged(),  // result 1
                                    MachineType::Int32(),  // kNumberOfArguments
                                    MachineType::Pointer(),  // kFirstArgument
                                    MachineType::Pointer())  // kFunctionEntry
  DECLARE_DESCRIPTOR(InterpreterCEntry1Descriptor)

  static constexpr auto registers();
};

class InterpreterCEntry2Descriptor
    : public StaticCallInterfaceDescriptor<InterpreterCEntry2Descriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_RESULT_AND_PARAMETERS(2, kNumberOfArguments, kFirstArgument,
                               kFunctionEntry)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::AnyTagged(),  // result 1
                                    MachineType::AnyTagged(),  // result 2
                                    MachineType::Int32(),  // kNumberOfArguments
                                    MachineType::Pointer(),  // kFirstArgument
                                    MachineType::Pointer())  // kFunctionEntry
  DECLARE_DESCRIPTOR(InterpreterCEntry2Descriptor)

  static constexpr auto registers();
};

class FindNonDefaultConstructorOrConstructDescriptor
    : public StaticCallInterfaceDescriptor<
          FindNonDefaultConstructorOrConstructDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_RESULT_AND_PARAMETERS(2, kThisFunction, kNewTarget)
  DEFINE_RESULT_AND_PARAMETER_TYPES(
      MachineType::AnyTagged(),  // result 1 (true / false)
      MachineType::AnyTagged(),  // result 2 (constructor_or_instance)
      MachineType::AnyTagged(),  // kThisFunction
      MachineType::AnyTagged())  // kNewTarget
  DECLARE_DESCRIPTOR(FindNonDefaultConstructorOrConstructDescriptor)
};

class ForInPrepareDescriptor
    : public StaticCallInterfaceDescriptor<ForInPrepareDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_RESULT_AND_PARAMETERS(2, kEnumerator, kVectorIndex, kFeedbackVector)
  DEFINE_RESULT_AND_PARAMETER_TYPES(
      MachineType::AnyTagged(),     // result 1 (cache array)
      MachineType::AnyTagged(),     // result 2 (cache length)
      MachineType::AnyTagged(),     // kEnumerator
      MachineType::TaggedSigned(),  // kVectorIndex
      MachineType::AnyTagged())     // kFeedbackVector
  DECLARE_DESCRIPTOR(ForInPrepareDescriptor)
};

class ResumeGeneratorDescriptor final
    : public StaticCallInterfaceDescriptor<ResumeGeneratorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kValue, kGenerator)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kValue
                         MachineType::AnyTagged())  // kGenerator
  DECLARE_DESCRIPTOR(ResumeGeneratorDescriptor)

  static constexpr inline auto registers();
};

class ResumeGeneratorBaselineDescriptor final
    : public StaticCallInterfaceDescriptor<ResumeGeneratorBaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kGeneratorObject, kRegisterCount)
  DEFINE_RESULT_AND_PARAMETER_TYPES(
      MachineType::TaggedSigned(),  // return type
      MachineType::AnyTagged(),     // kGeneratorObject
      MachineType::IntPtr(),        // kRegisterCount
  )
  DECLARE_DESCRIPTOR(ResumeGeneratorBaselineDescriptor)
};

class SuspendGeneratorBaselineDescriptor final
    : public StaticCallInterfaceDescriptor<SuspendGeneratorBaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kGeneratorObject, kSuspendId, kBytecodeOffset,
                               kRegisterCount)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kGeneratorObject
                         MachineType::IntPtr(),     // kSuspendId
                         MachineType::IntPtr(),     // kBytecodeOffset
                         MachineType::IntPtr(),     // kRegisterCount
  )
  DECLARE_DESCRIPTOR(SuspendGeneratorBaselineDescriptor)
};

class RestartFrameTrampolineDescriptor final
    : public StaticCallInterfaceDescriptor<RestartFrameTrampolineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS()
  DECLARE_DESCRIPTOR(RestartFrameTrampolineDescriptor)
};

class RunMicrotasksEntryDescriptor final
    : public StaticCallInterfaceDescriptor<RunMicrotasksEntryDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_ENTRY(kRootRegisterValue, kMicrotaskQueue)
  DEFINE_PARAMETER_TYPES(MachineType::Pointer(),  // kRootRegisterValue
                         MachineType::Pointer())  // kMicrotaskQueue
  DECLARE_DESCRIPTOR(RunMicrotasksEntryDescriptor)

  static constexpr inline auto registers();
};

class RunMicrotasksDescriptor final
    : public StaticCallInterfaceDescriptor<RunMicrotasksDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kMicrotaskQueue)
  DEFINE_PARAMETER_TYPES(MachineType::Pointer())
  DECLARE_DESCRIPTOR(RunMicrotasksDescriptor)

  static constexpr inline Register MicrotaskQueueRegister();
};

class WasmFloat32ToNumberDescriptor final
    : public StaticCallInterfaceDescriptor<WasmFloat32ToNumberDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kValue)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::AnyTagged(),  // result
                                    MachineType::Float32())    // value
  DECLARE_DESCRIPTOR(WasmFloat32ToNumberDescriptor)
};

class WasmFloat64ToTaggedDescriptor final
    : public StaticCallInterfaceDescriptor<WasmFloat64ToTaggedDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kValue)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::AnyTagged(),  // result
                                    MachineType::Float64())    // value
  DECLARE_DESCRIPTOR(WasmFloat64ToTaggedDescriptor)
};

class WasmJSToWasmWrapperDescriptor final
    : public StaticCallInterfaceDescriptor<WasmJSToWasmWrapperDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kWrapperBuffer, kInstance, kResultJSArray)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::AnyTagged(),  // result
                                    MachineType::IntPtr(),     // ParamBuffer
                                    MachineType::AnyTagged(),  // Instance
                                    MachineType::AnyTagged())  // Result jsarray
  DECLARE_DESCRIPTOR(WasmJSToWasmWrapperDescriptor)

  static constexpr int kMaxRegisterParams = 1;
  // Only the first parameter, `WrapperBuffer` gets passed over a register, the
  // instance and the js-array get passed over the stack. The reason is that
  // these parameters get forwarded to another function, and GC's may happen
  // until this other function gets called. By passing these parameters over the
  // stack the references get scanned as part of the caller frame, and the GC
  // does not have to scan anything on the `WasmJSToWasmWrapper` frame.
  static constexpr inline auto registers();
  static constexpr inline Register WrapperBufferRegister();
};

class WasmToJSWrapperDescriptor final
    : public StaticCallInterfaceDescriptor<WasmToJSWrapperDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_RESULT_AND_PARAMETERS_NO_CONTEXT(4, kWasmImportData)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::IntPtr(),     // GP return 1
                                    MachineType::IntPtr(),     // GP return 2
                                    MachineType::Float64(),    // FP return 1
                                    MachineType::Float64(),    // FP return 2
                                    MachineType::AnyTagged())  // WasmImportData
  DECLARE_DESCRIPTOR(WasmToJSWrapperDescriptor)

  static constexpr int kMaxRegisterParams = 1;
  static constexpr inline auto registers();
  static constexpr inline auto return_registers();
  static constexpr inline auto return_double_registers();
};

class WasmSuspendDescriptor final
    : public StaticCallInterfaceDescriptor<WasmSuspendDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_RESULT_AND_PARAMETERS_NO_CONTEXT(1, kArg0)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::AnyTagged(),  // result
                                    MachineType::AnyTagged())  // value
  DECLARE_DESCRIPTOR(WasmSuspendDescriptor)
};

class V8_EXPORT_PRIVATE I64ToBigIntDescriptor final
    : public StaticCallInterfaceDescriptor<I64ToBigIntDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kArgument)
  DEFINE_PARAMETER_TYPES(MachineType::Int64())  // kArgument
  DECLARE_DESCRIPTOR(I64ToBigIntDescriptor)
};

// 32 bits version of the I64ToBigIntDescriptor call interface descriptor
class V8_EXPORT_PRIVATE I32PairToBigIntDescriptor final
    : public StaticCallInterfaceDescriptor<I32PairToBigIntDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kLow, kHigh)
  DEFINE_PARAMETER_TYPES(MachineType::Uint32(),  // kLow
                         MachineType::Uint32())  // kHigh
  DECLARE_DESCRIPTOR(I32PairToBigIntDescriptor)
};

class V8_EXPORT_PRIVATE BigIntToI64Descriptor final
    : public StaticCallInterfaceDescriptor<BigIntToI64Descriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kArgument)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::Int64(),      // result 1
                                    MachineType::AnyTagged())  // kArgument
  DECLARE_DESCRIPTOR(BigIntToI64Descriptor)
};

class V8_EXPORT_PRIVATE BigIntToI32PairDescriptor final
    : public StaticCallInterfaceDescriptor<BigIntToI32PairDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_RESULT_AND_PARAMETERS(2, kArgument)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::Uint32(),     // result 1
                                    MachineType::Uint32(),     // result 2
                                    MachineType::AnyTagged())  // kArgument
  DECLARE_DESCRIPTOR(BigIntToI32PairDescriptor)
};

class CloneObjectWithVectorDescriptor final
    : public StaticCallInterfaceDescriptor<CloneObjectWithVectorDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kSource, kFlags, kSlot, kVector)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::TaggedPointer(),  // result 1
                                    MachineType::AnyTagged(),      // kSource
                                    MachineType::TaggedSigned(),   // kFlags
                                    MachineType::TaggedSigned(),   // kSlot
                                    MachineType::AnyTagged())      // kVector
  DECLARE_DESCRIPTOR(CloneObjectWithVectorDescriptor)
};

class CloneObjectBaselineDescriptor final
    : public StaticCallInterfaceDescriptor<CloneObjectBaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kSource, kFlags, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),     // kSource
                         MachineType::TaggedSigned(),  // kFlags
                         MachineType::TaggedSigned())  // kSlot
  DECLARE_DESCRIPTOR(CloneObjectBaselineDescriptor)
};

class BinaryOp_WithFeedbackDescriptor
    : public StaticCallInterfaceDescriptor<BinaryOp_WithFeedbackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kLeft, kRight, kSlot, kFeedbackVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kLeft
                         MachineType::AnyTagged(),  // kRight
                         MachineType::UintPtr(),    // kSlot
                         MachineType::AnyTagged())  // kFeedbackVector
  DECLARE_DESCRIPTOR(BinaryOp_WithFeedbackDescriptor)
};

class CallTrampoline_Baseline_CompactDescriptor
    : public StaticCallInterfaceDescriptor<
          CallTrampoline_Baseline_CompactDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  using ArgumentCountField = base::BitField<uint32_t, 0, 8>;
  using SlotField = base::BitField<uintptr_t, 8, 24>;

  static bool EncodeBitField(uint32_t argc, uintptr_t slot, uint32_t* out) {
    if (ArgumentCountField::is_valid(argc) && SlotField::is_valid(slot)) {
      *out = ArgumentCountField::encode(argc) | SlotField::encode(slot);
      return true;
    }
    return false;
  }

  DEFINE_PARAMETERS_NO_CONTEXT_VARARGS(kFunction, kBitField)
  DEFINE_PARAMETER_TYPES(
      MachineType::AnyTagged(),  // kFunction
      MachineType::Uint32())     // kBitField = ArgumentCountField | SlotField
  DECLARE_DESCRIPTOR(CallTrampoline_Baseline_CompactDescriptor)
};

class CallTrampoline_BaselineDescriptor
    : public StaticCallInterfaceDescriptor<CallTrampoline_BaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT_VARARGS(kFunction, kActualArgumentsCount, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kFunction
                         MachineType::Int32(),      // kActualArgumentsCount
                         MachineType::UintPtr())    // kSlot
  DECLARE_DESCRIPTOR(CallTrampoline_BaselineDescriptor)
};

class CallTrampoline_WithFeedbackDescriptor
    : public StaticCallInterfaceDescriptor<
          CallTrampoline_WithFeedbackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_VARARGS(kFunction, kActualArgumentsCount, kSlot,
                            kFeedbackVector, kReceiver)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kFunction
                         MachineType::Int32(),      // kActualArgumentsCount
                         MachineType::UintPtr(),    // kSlot
                         MachineType::AnyTagged(),  // kFeedbackVector
                         MachineType::AnyTagged())  // kReceiver
  DECLARE_DESCRIPTOR(CallTrampoline_WithFeedbackDescriptor)
};

class Compare_WithFeedbackDescriptor
    : public StaticCallInterfaceDescriptor<Compare_WithFeedbackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kLeft, kRight, kSlot, kFeedbackVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kLeft
                         MachineType::AnyTagged(),  // kRight
                         MachineType::UintPtr(),    // kSlot
                         MachineType::AnyTagged())  // kFeedbackVector
  DECLARE_DESCRIPTOR(Compare_WithFeedbackDescriptor)
};

class Compare_BaselineDescriptor
    : public StaticCallInterfaceDescriptor<Compare_BaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kLeft, kRight, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kLeft
                         MachineType::AnyTagged(),  // kRight
                         MachineType::UintPtr())    // kSlot
  DECLARE_DESCRIPTOR(Compare_BaselineDescriptor)

  static constexpr inline auto registers();
};

class Construct_BaselineDescriptor
    : public StaticJSCallInterfaceDescriptor<Construct_BaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_JS_PARAMETERS_NO_CONTEXT(kSlot)
  DEFINE_JS_PARAMETER_TYPES(MachineType::UintPtr())  // kSlot
  DECLARE_JS_COMPATIBLE_DESCRIPTOR(Construct_BaselineDescriptor)
};

class Construct_WithFeedbackDescriptor
    : public StaticJSCallInterfaceDescriptor<Construct_WithFeedbackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  // kSlot is passed in a register, kFeedbackVector on the stack.
  DEFINE_JS_PARAMETERS(kSlot, kFeedbackVector)
  DEFINE_JS_PARAMETER_TYPES(MachineType::UintPtr(),    // kSlot
                            MachineType::AnyTagged())  // kFeedbackVector
  DECLARE_JS_COMPATIBLE_DESCRIPTOR(Construct_WithFeedbackDescriptor)
};

class UnaryOp_WithFeedbackDescriptor
    : public StaticCallInterfaceDescriptor<UnaryOp_WithFeedbackDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS(kValue, kSlot, kFeedbackVector)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kValue
                         MachineType::UintPtr(),    // kSlot
                         MachineType::AnyTagged())  // kFeedbackVector
  DECLARE_DESCRIPTOR(UnaryOp_WithFeedbackDescriptor)
};

class UnaryOp_BaselineDescriptor
    : public StaticCallInterfaceDescriptor<UnaryOp_BaselineDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_PARAMETERS_NO_CONTEXT(kValue, kSlot)
  DEFINE_PARAMETER_TYPES(MachineType::AnyTagged(),  // kValue
                         MachineType::UintPtr())    // kSlot
  DECLARE_DESCRIPTOR(UnaryOp_BaselineDescriptor)
};

class CheckTurboshaftFloat32TypeDescriptor
    : public StaticCallInterfaceDescriptor<
          CheckTurboshaftFloat32TypeDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_RESULT_AND_PARAMETERS(1, kValue, kExpectedType, kNodeId)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::TaggedPointer(),
                                    MachineTypeOf<Float32T>::value,
                                    MachineType::TaggedPointer(),
                                    MachineType::TaggedSigned())
  DECLARE_DEFAULT_DESCRIPTOR(CheckTurboshaftFloat32TypeDescriptor)
};

class CheckTurboshaftFloat64TypeDescriptor
    : public StaticCallInterfaceDescriptor<
          CheckTurboshaftFloat64TypeDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_RESULT_AND_PARAMETERS(1, kValue, kExpectedType, kNodeId)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::TaggedPointer(),
                                    MachineTypeOf<Float64T>::value,
                                    MachineType::TaggedPointer(),
                                    MachineType::TaggedSigned())
  DECLARE_DEFAULT_DESCRIPTOR(CheckTurboshaftFloat64TypeDescriptor)
};

class DebugPrintWordPtrDescriptor
    : public StaticCallInterfaceDescriptor<DebugPrintWordPtrDescriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_RESULT_AND_PARAMETERS(1, kValue)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::TaggedPointer(),
                                    MachineType::UintPtr())
  DECLARE_DEFAULT_DESCRIPTOR(DebugPrintWordPtrDescriptor)
};

class DebugPrintFloat64Descriptor
    : public StaticCallInterfaceDescriptor<DebugPrintFloat64Descriptor> {
 public:
  INTERNAL_DESCRIPTOR()
  DEFINE_RESULT_AND_PARAMETERS(1, kValue)
  DEFINE_RESULT_AND_PARAMETER_TYPES(MachineType::TaggedPointer(),
                                    MachineType::Float64())
  DECLARE_DEFAULT_DESCRIPTOR(DebugPrintFloat64Descriptor)
};

#define DEFINE_TFS_BUILTIN_DESCRIPTOR(Name, DoesNeedContext, ...)            \
  class Name##Descriptor                                                     \
      : public StaticCallInterfaceDescriptor<Name##Descriptor> {             \
   public:                                                                   \
    INTERNAL_DESCRIPTOR()                                                    \
    DEFINE_PARAMETERS(__VA_ARGS__)                                           \
    static constexpr bool kNoContext = DoesNeedContext == NeedsContext::kNo; \
    DECLARE_DEFAULT_DESCRIPTOR(Name##Descriptor)                             \
  };
BUILTIN_LIST_TFS(DEFINE_TFS_BUILTIN_DESCRIPTOR)
#undef DEFINE_TFS_BUILTIN_DESCRIPTOR

// This file contains interface descriptor class definitions for builtins
// defined in Torque. It is included here because the class definitions need to
// precede the definition of name##Descriptor::key() below.
#include "torque-generated/interface-descriptors.inc"

#undef DECLARE_DEFAULT_DESCRIPTOR
#undef DECLARE_DESCRIPTOR_WITH_BASE
#undef DECLARE_DESCRIPTOR
#undef DECLARE_JS_COMPATIBLE_DESCRIPTOR
#undef DEFINE_RESULT_AND_PARAMETERS
#undef DEFINE_PARAMETERS_ENTRY
#undef DEFINE_PARAMETERS
#undef DEFINE_PARAMETERS_VARARGS
#undef DEFINE_PARAMETERS_NO_CONTEXT
#undef DEFINE_RESULT_AND_PARAMETERS_NO_CONTEXT
#undef DEFINE_RESULT_AND_PARAMETER_TYPES
#undef DEFINE_PARAMETER_TYPES
#undef DEFINE_JS_PARAMETERS
#undef DEFINE_JS_PARAMETER_TYPES

// We define the association between CallDescriptors::Key and the specialized
// descriptor here to reduce boilerplate and mistakes.
#define DEF_KEY(name, ...) \
  CallDescriptors::Key name##Descriptor::key() { return CallDescriptors::name; }
INTERFACE_DESCRIPTOR_LIST(DEF_KEY)
#undef DEF_KEY
}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_INTERFACE_DESCRIPTORS_H_

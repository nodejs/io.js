// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/external-reference.h"

#include <optional>

#include "include/v8-fast-api-calls.h"
#include "src/api/api-inl.h"
#include "src/base/bits.h"
#include "src/base/ieee754.h"
#include "src/codegen/cpu-features.h"
#include "src/common/globals.h"
#include "src/date/date.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/encoded-c-signature.h"
#include "src/execution/isolate-utils.h"
#include "src/execution/isolate.h"
#include "src/execution/microtask-queue.h"
#include "src/execution/simulator.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/ic/stub-cache.h"
#include "src/interpreter/interpreter.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/numbers/hash-seed-inl.h"
#include "src/numbers/math-random.h"
#include "src/objects/elements-kind.h"
#include "src/objects/elements.h"
#include "src/objects/object-type.h"
#include "src/objects/objects-inl.h"
#include "src/objects/ordered-hash-table.h"
#include "src/objects/simd.h"
#include "src/regexp/experimental/experimental.h"
#include "src/regexp/regexp-interpreter.h"
#include "src/regexp/regexp-macro-assembler-arch.h"
#include "src/regexp/regexp-stack.h"
#include "src/strings/string-search.h"
#include "src/strings/unicode-inl.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-external-refs.h"
#include "src/wasm/wasm-js.h"
#endif  // V8_ENABLE_WEBASSEMBLY

#ifdef V8_INTL_SUPPORT
#include "src/base/strings.h"
#include "src/objects/intl-objects.h"
#endif  // V8_INTL_SUPPORT

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// Common double constants.

constexpr double double_min_int_constant = kMinInt;
constexpr double double_one_half_constant = 0.5;
constexpr uint64_t double_the_hole_nan_constant = kHoleNanInt64;
constexpr double double_uint32_bias_constant =
    static_cast<double>(kMaxUInt32) + 1;

constexpr struct alignas(16) {
  uint16_t a;
  uint16_t b;
  uint16_t c;
  uint16_t d;
  uint16_t e;
  uint16_t f;
  uint16_t g;
  uint16_t h;
} fp16_absolute_constant = {0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
                            0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF};

constexpr struct alignas(16) {
  uint16_t a;
  uint16_t b;
  uint16_t c;
  uint16_t d;
  uint16_t e;
  uint16_t f;
  uint16_t g;
  uint16_t h;
} fp16_negate_constant = {0x8000, 0x8000, 0x8000, 0x8000,
                          0x8000, 0x8000, 0x8000, 0x8000};

constexpr struct alignas(16) {
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
} float_absolute_constant = {0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF};

constexpr struct alignas(16) {
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
} float_negate_constant = {0x80000000, 0x80000000, 0x80000000, 0x80000000};

constexpr struct alignas(16) {
  uint64_t a;
  uint64_t b;
} double_absolute_constant = {uint64_t{0x7FFFFFFFFFFFFFFF},
                              uint64_t{0x7FFFFFFFFFFFFFFF}};

constexpr struct alignas(16) {
  uint64_t a;
  uint64_t b;
} double_negate_constant = {uint64_t{0x8000000000000000},
                            uint64_t{0x8000000000000000}};

constexpr struct alignas(16) {
  uint64_t a;
  uint64_t b;
} wasm_i8x16_swizzle_mask = {uint64_t{0x70707070'70707070},
                             uint64_t{0x70707070'70707070}};

constexpr struct alignas(16) {
  uint64_t a;
  uint64_t b;
} wasm_i8x16_popcnt_mask = {uint64_t{0x03020201'02010100},
                            uint64_t{0x04030302'03020201}};

constexpr struct alignas(16) {
  uint64_t a;
  uint64_t b;
} wasm_i8x16_splat_0x01 = {uint64_t{0x01010101'01010101},
                           uint64_t{0x01010101'01010101}};

constexpr struct alignas(16) {
  uint64_t a;
  uint64_t b;
} wasm_i8x16_splat_0x0f = {uint64_t{0x0F0F0F0F'0F0F0F0F},
                           uint64_t{0x0F0F0F0F'0F0F0F0F}};

constexpr struct alignas(16) {
  uint64_t a;
  uint64_t b;
} wasm_i8x16_splat_0x33 = {uint64_t{0x33333333'33333333},
                           uint64_t{0x33333333'33333333}};

constexpr struct alignas(16) {
  uint64_t a;
  uint64_t b;
} wasm_i8x16_splat_0x55 = {uint64_t{0x55555555'55555555},
                           uint64_t{0x55555555'55555555}};

constexpr struct alignas(16) {
  uint64_t a;
  uint64_t b;
} wasm_i16x8_splat_0x0001 = {uint64_t{0x00010001'00010001},
                             uint64_t{0x00010001'00010001}};

constexpr struct alignas(16) {
  uint64_t a;
  uint64_t b;
} wasm_f64x2_convert_low_i32x4_u_int_mask = {uint64_t{0x4330000043300000},
                                             uint64_t{0x4330000043300000}};

constexpr struct alignas(16) {
  uint64_t a;
  uint64_t b;
} wasm_double_2_power_52 = {uint64_t{0x4330000000000000},
                            uint64_t{0x4330000000000000}};

constexpr struct alignas(16) {
  uint64_t a;
  uint64_t b;
} wasm_int32_max_as_double = {uint64_t{0x41dfffffffc00000},
                              uint64_t{0x41dfffffffc00000}};

constexpr struct alignas(16) {
  uint64_t a;
  uint64_t b;
} wasm_uint32_max_as_double = {uint64_t{0x41efffffffe00000},
                               uint64_t{0x41efffffffe00000}};

// This is 2147483648.0, which is 1 more than INT32_MAX.
constexpr struct alignas(16) {
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
} wasm_int32_overflow_as_float = {
    uint32_t{0x4f00'0000},
    uint32_t{0x4f00'0000},
    uint32_t{0x4f00'0000},
    uint32_t{0x4f00'0000},
};

constexpr struct alignas(16) {
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
} wasm_i32x8_int32_overflow_as_float = {
    uint32_t{0x4f00'0000}, uint32_t{0x4f00'0000}, uint32_t{0x4f00'0000},
    uint32_t{0x4f00'0000}, uint32_t{0x4f00'0000}, uint32_t{0x4f00'0000},
    uint32_t{0x4f00'0000}, uint32_t{0x4f00'0000},
};

// Implementation of ExternalReference

bool ExternalReference::IsIsolateFieldId() const {
  return (raw_ > 0 && raw_ <= static_cast<Address>(kNumIsolateFieldIds));
}

Address ExternalReference::address() const {
  // If this CHECK triggers, then an ExternalReference gets created with an
  // IsolateFieldId where the root register is not available, and therefore
  // IsolateFieldIds cannot be used, or ExternalReferences with IsolateFieldIds
  // don't get supported yet and support should be added.
  CHECK(!IsIsolateFieldId());
  return raw_;
}

int32_t ExternalReference::offset_from_root_register() const {
  CHECK(IsIsolateFieldId());
  return static_cast<int32_t>(
      IsolateData::GetOffset(static_cast<IsolateFieldId>(raw_)));
}

static ExternalReference::Type BuiltinCallTypeForResultSize(int result_size) {
  switch (result_size) {
    case 1:
      return ExternalReference::BUILTIN_CALL;
    case 2:
      return ExternalReference::BUILTIN_CALL_PAIR;
  }
  UNREACHABLE();
}

// static
ExternalReference ExternalReference::Create(ApiFunction* fun, Type type) {
  return ExternalReference(Redirect(fun->address(), type));
}

// static
ExternalReference ExternalReference::Create(
    Isolate* isolate, ApiFunction* fun, Type type, Address* c_functions,
    const CFunctionInfo* const* c_signatures, unsigned num_functions) {
#ifdef V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  isolate->simulator_data()->RegisterFunctionsAndSignatures(
      c_functions, c_signatures, num_functions);
#endif  //  V8_USE_SIMULATOR_WITH_GENERIC_C_CALLS
  return ExternalReference(Redirect(fun->address(), type));
}

// static
ExternalReference ExternalReference::Create(Runtime::FunctionId id) {
  return Create(Runtime::FunctionForId(id));
}

// static
ExternalReference ExternalReference::Create(IsolateFieldId id) {
  return ExternalReference(id);
}

// static
ExternalReference ExternalReference::Create(const Runtime::Function* f) {
  return ExternalReference(
      Redirect(f->entry, BuiltinCallTypeForResultSize(f->result_size)));
}

// static
ExternalReference ExternalReference::Create(Address address, Type type) {
  return ExternalReference(Redirect(address, type));
}

ExternalReference ExternalReference::isolate_address(Isolate* isolate) {
  return ExternalReference(isolate);
}

ExternalReference ExternalReference::isolate_address() {
  return ExternalReference(IsolateFieldId::kIsolateAddress);
}

ExternalReference ExternalReference::handle_scope_implementer_address(
    Isolate* isolate) {
  return ExternalReference(isolate->handle_scope_implementer_address());
}

#ifdef V8_ENABLE_SANDBOX
ExternalReference ExternalReference::sandbox_base_address() {
  return ExternalReference(GetProcessWideSandbox()->base_address());
}

ExternalReference ExternalReference::sandbox_end_address() {
  return ExternalReference(GetProcessWideSandbox()->end_address());
}

ExternalReference ExternalReference::empty_backing_store_buffer() {
  return ExternalReference(GetProcessWideSandbox()
                               ->constants()
                               .empty_backing_store_buffer_address());
}

ExternalReference ExternalReference::external_pointer_table_address(
    Isolate* isolate) {
  return ExternalReference(isolate->external_pointer_table_address());
}

ExternalReference
ExternalReference::shared_external_pointer_table_address_address(
    Isolate* isolate) {
  return ExternalReference(
      isolate->shared_external_pointer_table_address_address());
}

ExternalReference ExternalReference::trusted_pointer_table_base_address(
    Isolate* isolate) {
  // TODO(saelo): maybe the external pointer table external references should
  // also directly return the table base address?
  return ExternalReference(isolate->trusted_pointer_table_base_address());
}

ExternalReference ExternalReference::code_pointer_table_address() {
  // TODO(saelo): maybe rename to code_pointer_table_base_address?
  return ExternalReference(GetProcessWideCodePointerTable()->base_address());
}

ExternalReference ExternalReference::memory_chunk_metadata_table_address() {
  return ExternalReference(MemoryChunk::MetadataTableAddress());
}

ExternalReference ExternalReference::js_dispatch_table_address() {
  // TODO(saelo): maybe rename to js_dispatch_table_base_address?
  return ExternalReference(GetProcessWideJSDispatchTable()->base_address());
}

#endif  // V8_ENABLE_SANDBOX

ExternalReference ExternalReference::interpreter_dispatch_table_address(
    Isolate* isolate) {
  return ExternalReference(isolate->interpreter()->dispatch_table_address());
}

ExternalReference ExternalReference::interpreter_dispatch_counters(
    Isolate* isolate) {
  return ExternalReference(
      isolate->interpreter()->bytecode_dispatch_counters_table());
}

ExternalReference
ExternalReference::address_of_interpreter_entry_trampoline_instruction_start(
    Isolate* isolate) {
  return ExternalReference(
      isolate->interpreter()
          ->address_of_interpreter_entry_trampoline_instruction_start());
}

ExternalReference ExternalReference::bytecode_size_table_address() {
  return ExternalReference(
      interpreter::Bytecodes::bytecode_size_table_address());
}

// static
ExternalReference ExternalReference::Create(StatsCounter* counter) {
  return ExternalReference(
      reinterpret_cast<Address>(counter->GetInternalPointer()));
}

// static
ExternalReference ExternalReference::Create(IsolateAddressId id,
                                            Isolate* isolate) {
  return ExternalReference(isolate->get_address_from_id(id));
}

// static
ExternalReference ExternalReference::Create(const SCTableReference& table_ref) {
  return ExternalReference(table_ref.address());
}

namespace {

// Helper function to verify that all types in a list of types are scalar.
// This includes primitive types (int, Address) and pointer types. We also
// allow void.
template <typename T>
constexpr bool AllScalar() {
  return std::is_scalar<T>::value || std::is_void<T>::value;
}

template <typename T1, typename T2, typename... Rest>
constexpr bool AllScalar() {
  return AllScalar<T1>() && AllScalar<T2, Rest...>();
}

// Checks a function pointer's type for compatibility with the
// ExternalReference calling mechanism. Specifically, all arguments
// as well as the result type must pass the AllScalar check above,
// because we expect each item to fit into one register or stack slot.
template <typename T>
struct IsValidExternalReferenceType;

template <typename Result, typename... Args>
struct IsValidExternalReferenceType<Result (*)(Args...)> {
  static const bool value = AllScalar<Result, Args...>();
};

template <typename Result, typename Class, typename... Args>
struct IsValidExternalReferenceType<Result (Class::*)(Args...)> {
  static const bool value = AllScalar<Result, Args...>();
};

}  // namespace

// .. for functions that will not be called through CallCFunction. For these,
// all signatures are valid.
#define RAW_FUNCTION_REFERENCE(Name, Target)         \
  ExternalReference ExternalReference::Name() {      \
    return ExternalReference(FUNCTION_ADDR(Target)); \
  }

// .. for functions that will be called through CallCFunction.
#define FUNCTION_REFERENCE(Name, Target)                                   \
  ExternalReference ExternalReference::Name() {                            \
    static_assert(IsValidExternalReferenceType<decltype(&Target)>::value); \
    return ExternalReference(Redirect(FUNCTION_ADDR(Target)));             \
  }

#define FUNCTION_REFERENCE_WITH_TYPE(Name, Target, Type)                   \
  ExternalReference ExternalReference::Name() {                            \
    static_assert(IsValidExternalReferenceType<decltype(&Target)>::value); \
    return ExternalReference(Redirect(FUNCTION_ADDR(Target), Type));       \
  }

FUNCTION_REFERENCE(write_barrier_marking_from_code_function,
                   WriteBarrier::MarkingFromCode)

FUNCTION_REFERENCE(write_barrier_indirect_pointer_marking_from_code_function,
                   WriteBarrier::IndirectPointerMarkingFromCode)

FUNCTION_REFERENCE(write_barrier_shared_marking_from_code_function,
                   WriteBarrier::SharedMarkingFromCode)

FUNCTION_REFERENCE(shared_barrier_from_code_function,
                   WriteBarrier::SharedFromCode)

FUNCTION_REFERENCE(insert_remembered_set_function,
                   Heap::InsertIntoRememberedSetFromCode)

namespace {

intptr_t DebugBreakAtEntry(Isolate* isolate, Address raw_sfi) {
  DisallowGarbageCollection no_gc;
  Tagged<SharedFunctionInfo> sfi =
      Cast<SharedFunctionInfo>(Tagged<Object>(raw_sfi));
  return isolate->debug()->BreakAtEntry(sfi) ? 1 : 0;
}

Address DebugGetCoverageInfo(Isolate* isolate, Address raw_sfi) {
  DisallowGarbageCollection no_gc;
  Tagged<SharedFunctionInfo> sfi =
      Cast<SharedFunctionInfo>(Tagged<Object>(raw_sfi));
  std::optional<Tagged<DebugInfo>> debug_info =
      isolate->debug()->TryGetDebugInfo(sfi);
  if (debug_info.has_value() && debug_info.value()->HasCoverageInfo()) {
    return debug_info.value()->coverage_info().ptr();
  }
  return Smi::zero().ptr();
}

}  // namespace

FUNCTION_REFERENCE(debug_break_at_entry_function, DebugBreakAtEntry)
FUNCTION_REFERENCE(debug_get_coverage_info_function, DebugGetCoverageInfo)

FUNCTION_REFERENCE(delete_handle_scope_extensions,
                   HandleScope::DeleteExtensions)

FUNCTION_REFERENCE(ephemeron_key_write_barrier_function,
                   Heap::EphemeronKeyWriteBarrierFromCode)

ExternalPointerHandle AllocateAndInitializeYoungExternalPointerTableEntry(
    Isolate* isolate, Address pointer) {
#ifdef V8_ENABLE_SANDBOX
  return isolate->external_pointer_table().AllocateAndInitializeEntry(
      isolate->heap()->young_external_pointer_space(), pointer,
      kExternalObjectValueTag);
#else
  return 0;
#endif  // V8_ENABLE_SANDBOX
}

FUNCTION_REFERENCE(allocate_and_initialize_young_external_pointer_table_entry,
                   AllocateAndInitializeYoungExternalPointerTableEntry)

FUNCTION_REFERENCE(get_date_field_function, JSDate::GetField)

ExternalReference ExternalReference::date_cache_stamp(Isolate* isolate) {
  return ExternalReference(isolate->date_cache()->stamp_address());
}

// static
ExternalReference
ExternalReference::runtime_function_table_address_for_unittests(
    Isolate* isolate) {
  return runtime_function_table_address(isolate);
}

// static
Address ExternalReference::Redirect(Address external_function, Type type) {
#ifdef USE_SIMULATOR
  return SimulatorBase::RedirectExternalReference(external_function, type);
#else
  return external_function;
#endif
}

// static
Address ExternalReference::UnwrapRedirection(Address redirection_trampoline) {
#ifdef USE_SIMULATOR
  return SimulatorBase::UnwrapRedirection(redirection_trampoline);
#else
  return redirection_trampoline;
#endif
}

ExternalReference ExternalReference::stress_deopt_count(Isolate* isolate) {
  return ExternalReference(isolate->stress_deopt_count_address());
}

ExternalReference ExternalReference::force_slow_path(Isolate* isolate) {
  return ExternalReference(isolate->force_slow_path_address());
}

FUNCTION_REFERENCE(new_deoptimizer_function, Deoptimizer::New)

FUNCTION_REFERENCE(compute_output_frames_function,
                   Deoptimizer::ComputeOutputFrames)

#ifdef V8_ENABLE_WEBASSEMBLY
FUNCTION_REFERENCE(wasm_sync_stack_limit, wasm::sync_stack_limit)
FUNCTION_REFERENCE(wasm_return_switch, wasm::return_switch)
FUNCTION_REFERENCE(wasm_switch_to_the_central_stack,
                   wasm::switch_to_the_central_stack)
FUNCTION_REFERENCE(wasm_switch_from_the_central_stack,
                   wasm::switch_from_the_central_stack)
FUNCTION_REFERENCE(wasm_switch_to_the_central_stack_for_js,
                   wasm::switch_to_the_central_stack_for_js)
FUNCTION_REFERENCE(wasm_switch_from_the_central_stack_for_js,
                   wasm::switch_from_the_central_stack_for_js)
FUNCTION_REFERENCE(wasm_f32_trunc, wasm::f32_trunc_wrapper)
FUNCTION_REFERENCE(wasm_f32_floor, wasm::f32_floor_wrapper)
FUNCTION_REFERENCE(wasm_f32_ceil, wasm::f32_ceil_wrapper)
FUNCTION_REFERENCE(wasm_f32_nearest_int, wasm::f32_nearest_int_wrapper)
FUNCTION_REFERENCE(wasm_f64_trunc, wasm::f64_trunc_wrapper)
FUNCTION_REFERENCE(wasm_f64_floor, wasm::f64_floor_wrapper)
FUNCTION_REFERENCE(wasm_f64_ceil, wasm::f64_ceil_wrapper)
FUNCTION_REFERENCE(wasm_f64_nearest_int, wasm::f64_nearest_int_wrapper)
FUNCTION_REFERENCE(wasm_int64_to_float32, wasm::int64_to_float32_wrapper)
FUNCTION_REFERENCE(wasm_uint64_to_float32, wasm::uint64_to_float32_wrapper)
FUNCTION_REFERENCE(wasm_int64_to_float64, wasm::int64_to_float64_wrapper)
FUNCTION_REFERENCE(wasm_uint64_to_float64, wasm::uint64_to_float64_wrapper)
FUNCTION_REFERENCE(wasm_float32_to_int64, wasm::float32_to_int64_wrapper)
FUNCTION_REFERENCE(wasm_float32_to_uint64, wasm::float32_to_uint64_wrapper)
FUNCTION_REFERENCE(wasm_float64_to_int64, wasm::float64_to_int64_wrapper)
FUNCTION_REFERENCE(wasm_float64_to_uint64, wasm::float64_to_uint64_wrapper)
FUNCTION_REFERENCE(wasm_float32_to_int64_sat,
                   wasm::float32_to_int64_sat_wrapper)
FUNCTION_REFERENCE(wasm_float32_to_uint64_sat,
                   wasm::float32_to_uint64_sat_wrapper)
FUNCTION_REFERENCE(wasm_float64_to_int64_sat,
                   wasm::float64_to_int64_sat_wrapper)
FUNCTION_REFERENCE(wasm_float64_to_uint64_sat,
                   wasm::float64_to_uint64_sat_wrapper)
FUNCTION_REFERENCE(wasm_float16_to_float32, wasm::float16_to_float32_wrapper)
FUNCTION_REFERENCE(wasm_float32_to_float16, wasm::float32_to_float16_wrapper)
FUNCTION_REFERENCE(wasm_int64_div, wasm::int64_div_wrapper)
FUNCTION_REFERENCE(wasm_int64_mod, wasm::int64_mod_wrapper)
FUNCTION_REFERENCE(wasm_uint64_div, wasm::uint64_div_wrapper)
FUNCTION_REFERENCE(wasm_uint64_mod, wasm::uint64_mod_wrapper)
FUNCTION_REFERENCE(wasm_word32_ctz, base::bits::CountTrailingZeros<uint32_t>)
FUNCTION_REFERENCE(wasm_word64_ctz, base::bits::CountTrailingZeros<uint64_t>)
FUNCTION_REFERENCE(wasm_word32_popcnt, base::bits::CountPopulation<uint32_t>)
FUNCTION_REFERENCE(wasm_word64_popcnt, base::bits::CountPopulation<uint64_t>)
FUNCTION_REFERENCE(wasm_word32_rol, wasm::word32_rol_wrapper)
FUNCTION_REFERENCE(wasm_word32_ror, wasm::word32_ror_wrapper)
FUNCTION_REFERENCE(wasm_word64_rol, wasm::word64_rol_wrapper)
FUNCTION_REFERENCE(wasm_word64_ror, wasm::word64_ror_wrapper)
FUNCTION_REFERENCE(wasm_f64x2_ceil, wasm::f64x2_ceil_wrapper)
FUNCTION_REFERENCE(wasm_f64x2_floor, wasm::f64x2_floor_wrapper)
FUNCTION_REFERENCE(wasm_f64x2_trunc, wasm::f64x2_trunc_wrapper)
FUNCTION_REFERENCE(wasm_f64x2_nearest_int, wasm::f64x2_nearest_int_wrapper)
FUNCTION_REFERENCE(wasm_f32x4_ceil, wasm::f32x4_ceil_wrapper)
FUNCTION_REFERENCE(wasm_f32x4_floor, wasm::f32x4_floor_wrapper)
FUNCTION_REFERENCE(wasm_f32x4_trunc, wasm::f32x4_trunc_wrapper)
FUNCTION_REFERENCE(wasm_f32x4_nearest_int, wasm::f32x4_nearest_int_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_abs, wasm::f16x8_abs_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_neg, wasm::f16x8_neg_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_sqrt, wasm::f16x8_sqrt_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_ceil, wasm::f16x8_ceil_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_floor, wasm::f16x8_floor_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_trunc, wasm::f16x8_trunc_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_nearest_int, wasm::f16x8_nearest_int_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_eq, wasm::f16x8_eq_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_ne, wasm::f16x8_ne_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_lt, wasm::f16x8_lt_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_le, wasm::f16x8_le_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_add, wasm::f16x8_add_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_sub, wasm::f16x8_sub_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_mul, wasm::f16x8_mul_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_div, wasm::f16x8_div_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_min, wasm::f16x8_min_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_max, wasm::f16x8_max_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_pmin, wasm::f16x8_pmin_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_pmax, wasm::f16x8_pmax_wrapper)
FUNCTION_REFERENCE(wasm_i16x8_sconvert_f16x8,
                   wasm::i16x8_sconvert_f16x8_wrapper)
FUNCTION_REFERENCE(wasm_i16x8_uconvert_f16x8,
                   wasm::i16x8_uconvert_f16x8_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_sconvert_i16x8,
                   wasm::f16x8_sconvert_i16x8_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_uconvert_i16x8,
                   wasm::f16x8_uconvert_i16x8_wrapper)
FUNCTION_REFERENCE(wasm_f32x4_promote_low_f16x8,
                   wasm::f32x4_promote_low_f16x8_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_demote_f32x4_zero,
                   wasm::f16x8_demote_f32x4_zero_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_demote_f64x2_zero,
                   wasm::f16x8_demote_f64x2_zero_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_qfma, wasm::f16x8_qfma_wrapper)
FUNCTION_REFERENCE(wasm_f16x8_qfms, wasm::f16x8_qfms_wrapper)
FUNCTION_REFERENCE(wasm_memory_init, wasm::memory_init_wrapper)
FUNCTION_REFERENCE(wasm_memory_copy, wasm::memory_copy_wrapper)
FUNCTION_REFERENCE(wasm_memory_fill, wasm::memory_fill_wrapper)
FUNCTION_REFERENCE(wasm_float64_pow, wasm::float64_pow_wrapper)
FUNCTION_REFERENCE(wasm_array_copy, wasm::array_copy_wrapper)
FUNCTION_REFERENCE(wasm_array_fill, wasm::array_fill_wrapper)
FUNCTION_REFERENCE_WITH_TYPE(wasm_string_to_f64, wasm::flat_string_to_f64,
                             BUILTIN_FP_POINTER_CALL)

int32_t (&futex_emulation_wake)(void*, uint32_t) = FutexEmulation::Wake;
FUNCTION_REFERENCE(wasm_atomic_notify, futex_emulation_wake)

void WasmSignatureCheckFail(Address raw_internal_function,
                            uintptr_t expected_hash) {
  // WasmInternalFunction::signature_hash doesn't exist in non-sandbox builds.
#if V8_ENABLE_SANDBOX
  Tagged<WasmInternalFunction> internal_function =
      Cast<WasmInternalFunction>(Tagged<Object>(raw_internal_function));
  PrintF("Wasm sandbox violation! Expected signature hash %lx, got %lx\n",
         expected_hash, internal_function->signature_hash());
  SBXCHECK_EQ(expected_hash, internal_function->signature_hash());
#endif
}
FUNCTION_REFERENCE(wasm_signature_check_fail, WasmSignatureCheckFail)

#define V(Name) RAW_FUNCTION_REFERENCE(wasm_##Name, wasm::Name)
WASM_JS_EXTERNAL_REFERENCE_LIST(V)
#undef V
#endif  // V8_ENABLE_WEBASSEMBLY

static void f64_acos_wrapper(Address data) {
  double input = ReadUnalignedValue<double>(data);
  WriteUnalignedValue(data, base::ieee754::acos(input));
}

FUNCTION_REFERENCE(f64_acos_wrapper_function, f64_acos_wrapper)

static void f64_asin_wrapper(Address data) {
  double input = ReadUnalignedValue<double>(data);
  WriteUnalignedValue<double>(data, base::ieee754::asin(input));
}

FUNCTION_REFERENCE(f64_asin_wrapper_function, f64_asin_wrapper)


static void f64_mod_wrapper(Address data) {
  double dividend = ReadUnalignedValue<double>(data);
  double divisor = ReadUnalignedValue<double>(data + sizeof(dividend));
  WriteUnalignedValue<double>(data, Modulo(dividend, divisor));
}

FUNCTION_REFERENCE(f64_mod_wrapper_function, f64_mod_wrapper)

ExternalReference ExternalReference::isolate_root(Isolate* isolate) {
  return ExternalReference(isolate->isolate_root());
}

ExternalReference ExternalReference::allocation_sites_list_address(
    Isolate* isolate) {
  return ExternalReference(isolate->heap()->allocation_sites_list_address());
}

ExternalReference ExternalReference::address_of_jslimit(Isolate* isolate) {
  Address address = isolate->stack_guard()->address_of_jslimit();
  // For efficient generated code, this should be root-register-addressable.
  DCHECK(isolate->root_register_addressable_region().contains(address));
  return ExternalReference(address);
}

ExternalReference ExternalReference::address_of_no_heap_write_interrupt_request(
    Isolate* isolate) {
  Address address = isolate->stack_guard()->address_of_interrupt_request(
      StackGuard::InterruptLevel::kNoHeapWrites);
  // For efficient generated code, this should be root-register-addressable.
  DCHECK(isolate->root_register_addressable_region().contains(address));
  return ExternalReference(address);
}

ExternalReference ExternalReference::address_of_real_jslimit(Isolate* isolate) {
  Address address = isolate->stack_guard()->address_of_real_jslimit();
  // For efficient generated code, this should be root-register-addressable.
  DCHECK(isolate->root_register_addressable_region().contains(address));
  return ExternalReference(address);
}

ExternalReference ExternalReference::heap_is_marking_flag_address(
    Isolate* isolate) {
  return ExternalReference(isolate->heap()->IsMarkingFlagAddress());
}

ExternalReference ExternalReference::heap_is_minor_marking_flag_address(
    Isolate* isolate) {
  return ExternalReference(isolate->heap()->IsMinorMarkingFlagAddress());
}

ExternalReference ExternalReference::is_shared_space_isolate_flag_address(
    Isolate* isolate) {
  return ExternalReference(
      isolate->isolate_data()->is_shared_space_isolate_flag_address());
}

ExternalReference ExternalReference::new_space_allocation_top_address(
    Isolate* isolate) {
  return ExternalReference(isolate->heap()->NewSpaceAllocationTopAddress());
}

ExternalReference ExternalReference::new_space_allocation_limit_address(
    Isolate* isolate) {
  return ExternalReference(isolate->heap()->NewSpaceAllocationLimitAddress());
}

ExternalReference ExternalReference::old_space_allocation_top_address(
    Isolate* isolate) {
  return ExternalReference(isolate->heap()->OldSpaceAllocationTopAddress());
}

ExternalReference ExternalReference::old_space_allocation_limit_address(
    Isolate* isolate) {
  return ExternalReference(isolate->heap()->OldSpaceAllocationLimitAddress());
}

ExternalReference ExternalReference::handle_scope_level_address(
    Isolate* isolate) {
  return ExternalReference(HandleScope::current_level_address(isolate));
}

ExternalReference ExternalReference::handle_scope_next_address(
    Isolate* isolate) {
  return ExternalReference(HandleScope::current_next_address(isolate));
}

ExternalReference ExternalReference::handle_scope_limit_address(
    Isolate* isolate) {
  return ExternalReference(HandleScope::current_limit_address(isolate));
}

ExternalReference ExternalReference::exception_address(Isolate* isolate) {
  return ExternalReference(isolate->exception_address());
}

ExternalReference ExternalReference::address_of_pending_message(
    Isolate* isolate) {
  return ExternalReference(isolate->pending_message_address());
}

ExternalReference ExternalReference::address_of_pending_message(
    LocalIsolate* local_isolate) {
  return ExternalReference(local_isolate->pending_message_address());
}

FUNCTION_REFERENCE(abort_with_reason, i::abort_with_reason)

ExternalReference ExternalReference::address_of_min_int() {
  return ExternalReference(reinterpret_cast<Address>(&double_min_int_constant));
}

ExternalReference
ExternalReference::address_of_mock_arraybuffer_allocator_flag() {
  return ExternalReference(&v8_flags.mock_arraybuffer_allocator);
}

// TODO(jgruber): Update the other extrefs pointing at v8_flags. addresses to be
// called address_of_FLAG_foo (easier grep-ability).
ExternalReference ExternalReference::address_of_log_or_trace_osr() {
  return ExternalReference(&v8_flags.log_or_trace_osr);
}

ExternalReference ExternalReference::address_of_builtin_subclassing_flag() {
  return ExternalReference(&v8_flags.builtin_subclassing);
}

ExternalReference ExternalReference::address_of_runtime_stats_flag() {
  return ExternalReference(&TracingFlags::runtime_stats);
}

ExternalReference ExternalReference::address_of_shared_string_table_flag() {
  return ExternalReference(&v8_flags.shared_string_table);
}

ExternalReference ExternalReference::address_of_load_from_stack_count(
    const char* function_name) {
  return ExternalReference(
      Isolate::load_from_stack_count_address(function_name));
}

ExternalReference ExternalReference::address_of_store_to_stack_count(
    const char* function_name) {
  return ExternalReference(
      Isolate::store_to_stack_count_address(function_name));
}

ExternalReference ExternalReference::address_of_one_half() {
  return ExternalReference(
      reinterpret_cast<Address>(&double_one_half_constant));
}

ExternalReference ExternalReference::address_of_the_hole_nan() {
  return ExternalReference(
      reinterpret_cast<Address>(&double_the_hole_nan_constant));
}

ExternalReference ExternalReference::address_of_uint32_bias() {
  return ExternalReference(
      reinterpret_cast<Address>(&double_uint32_bias_constant));
}

ExternalReference ExternalReference::address_of_fp16_abs_constant() {
  return ExternalReference(reinterpret_cast<Address>(&fp16_absolute_constant));
}

ExternalReference ExternalReference::address_of_fp16_neg_constant() {
  return ExternalReference(reinterpret_cast<Address>(&fp16_negate_constant));
}

ExternalReference ExternalReference::address_of_float_abs_constant() {
  return ExternalReference(reinterpret_cast<Address>(&float_absolute_constant));
}

ExternalReference ExternalReference::address_of_float_neg_constant() {
  return ExternalReference(reinterpret_cast<Address>(&float_negate_constant));
}

ExternalReference ExternalReference::address_of_double_abs_constant() {
  return ExternalReference(
      reinterpret_cast<Address>(&double_absolute_constant));
}

ExternalReference ExternalReference::address_of_double_neg_constant() {
  return ExternalReference(reinterpret_cast<Address>(&double_negate_constant));
}

ExternalReference ExternalReference::address_of_wasm_i8x16_swizzle_mask() {
  return ExternalReference(reinterpret_cast<Address>(&wasm_i8x16_swizzle_mask));
}

ExternalReference ExternalReference::address_of_wasm_i8x16_popcnt_mask() {
  return ExternalReference(reinterpret_cast<Address>(&wasm_i8x16_popcnt_mask));
}

ExternalReference ExternalReference::address_of_wasm_i8x16_splat_0x01() {
  return ExternalReference(reinterpret_cast<Address>(&wasm_i8x16_splat_0x01));
}

ExternalReference ExternalReference::address_of_wasm_i8x16_splat_0x0f() {
  return ExternalReference(reinterpret_cast<Address>(&wasm_i8x16_splat_0x0f));
}

ExternalReference ExternalReference::address_of_wasm_i8x16_splat_0x33() {
  return ExternalReference(reinterpret_cast<Address>(&wasm_i8x16_splat_0x33));
}

ExternalReference ExternalReference::address_of_wasm_i8x16_splat_0x55() {
  return ExternalReference(reinterpret_cast<Address>(&wasm_i8x16_splat_0x55));
}

ExternalReference ExternalReference::address_of_wasm_i16x8_splat_0x0001() {
  return ExternalReference(reinterpret_cast<Address>(&wasm_i16x8_splat_0x0001));
}

ExternalReference
ExternalReference::address_of_wasm_f64x2_convert_low_i32x4_u_int_mask() {
  return ExternalReference(
      reinterpret_cast<Address>(&wasm_f64x2_convert_low_i32x4_u_int_mask));
}

ExternalReference ExternalReference::supports_wasm_simd_128_address() {
  return ExternalReference(
      reinterpret_cast<Address>(&CpuFeatures::supports_wasm_simd_128_));
}

ExternalReference ExternalReference::address_of_wasm_double_2_power_52() {
  return ExternalReference(reinterpret_cast<Address>(&wasm_double_2_power_52));
}

ExternalReference ExternalReference::address_of_wasm_int32_max_as_double() {
  return ExternalReference(
      reinterpret_cast<Address>(&wasm_int32_max_as_double));
}

ExternalReference ExternalReference::address_of_wasm_uint32_max_as_double() {
  return ExternalReference(
      reinterpret_cast<Address>(&wasm_uint32_max_as_double));
}

ExternalReference ExternalReference::address_of_wasm_int32_overflow_as_float() {
  return ExternalReference(
      reinterpret_cast<Address>(&wasm_int32_overflow_as_float));
}

ExternalReference
ExternalReference::address_of_wasm_i32x8_int32_overflow_as_float() {
  return ExternalReference(
      reinterpret_cast<Address>(&wasm_i32x8_int32_overflow_as_float));
}

ExternalReference ExternalReference::supports_cetss_address() {
  return ExternalReference(
      reinterpret_cast<Address>(&CpuFeatures::supports_cetss_));
}

ExternalReference
ExternalReference::address_of_enable_experimental_regexp_engine() {
  return ExternalReference(&v8_flags.enable_experimental_regexp_engine);
}

namespace {

static uintptr_t BaselinePCForBytecodeOffset(Address raw_code_obj,
                                             int bytecode_offset,
                                             Address raw_bytecode_array) {
  Tagged<Code> code_obj = Cast<Code>(Tagged<Object>(raw_code_obj));
  Tagged<BytecodeArray> bytecode_array =
      Cast<BytecodeArray>(Tagged<Object>(raw_bytecode_array));
  return code_obj->GetBaselineStartPCForBytecodeOffset(bytecode_offset,
                                                       bytecode_array);
}

static uintptr_t BaselinePCForNextExecutedBytecode(Address raw_code_obj,
                                                   int bytecode_offset,
                                                   Address raw_bytecode_array) {
  Tagged<Code> code_obj = Cast<Code>(Tagged<Object>(raw_code_obj));
  Tagged<BytecodeArray> bytecode_array =
      Cast<BytecodeArray>(Tagged<Object>(raw_bytecode_array));
  return code_obj->GetBaselinePCForNextExecutedBytecode(bytecode_offset,
                                                        bytecode_array);
}

}  // namespace

FUNCTION_REFERENCE(baseline_pc_for_bytecode_offset, BaselinePCForBytecodeOffset)
FUNCTION_REFERENCE(baseline_pc_for_next_executed_bytecode,
                   BaselinePCForNextExecutedBytecode)

ExternalReference ExternalReference::thread_in_wasm_flag_address_address(
    Isolate* isolate) {
  return ExternalReference(isolate->thread_in_wasm_flag_address_address());
}

ExternalReference ExternalReference::invoke_function_callback_generic() {
  Address thunk_address = FUNCTION_ADDR(&InvokeFunctionCallbackGeneric);
  ExternalReference::Type thunk_type = ExternalReference::DIRECT_API_CALL;
  ApiFunction thunk_fun(thunk_address);
  return ExternalReference::Create(&thunk_fun, thunk_type);
}

ExternalReference ExternalReference::invoke_function_callback_optimized() {
  Address thunk_address = FUNCTION_ADDR(&InvokeFunctionCallbackOptimized);
  ExternalReference::Type thunk_type = ExternalReference::DIRECT_API_CALL;
  ApiFunction thunk_fun(thunk_address);
  return ExternalReference::Create(&thunk_fun, thunk_type);
}

// static
ExternalReference ExternalReference::invoke_function_callback(
    CallApiCallbackMode mode) {
  switch (mode) {
    case CallApiCallbackMode::kGeneric:
      return invoke_function_callback_generic();
    case CallApiCallbackMode::kOptimized:
      return invoke_function_callback_optimized();
    case CallApiCallbackMode::kOptimizedNoProfiling:
      return ExternalReference();
  }
}

ExternalReference ExternalReference::invoke_accessor_getter_callback() {
  Address thunk_address = FUNCTION_ADDR(&InvokeAccessorGetterCallback);
  ExternalReference::Type thunk_type = ExternalReference::DIRECT_GETTER_CALL;
  ApiFunction thunk_fun(thunk_address);
  return ExternalReference::Create(&thunk_fun, thunk_type);
}

#if V8_TARGET_ARCH_X64
#define re_stack_check_func RegExpMacroAssemblerX64::CheckStackGuardState
#elif V8_TARGET_ARCH_IA32
#define re_stack_check_func RegExpMacroAssemblerIA32::CheckStackGuardState
#elif V8_TARGET_ARCH_ARM64
#define re_stack_check_func RegExpMacroAssemblerARM64::CheckStackGuardState
#elif V8_TARGET_ARCH_ARM
#define re_stack_check_func RegExpMacroAssemblerARM::CheckStackGuardState
#elif V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
#define re_stack_check_func RegExpMacroAssemblerPPC::CheckStackGuardState
#elif V8_TARGET_ARCH_MIPS64
#define re_stack_check_func RegExpMacroAssemblerMIPS::CheckStackGuardState
#elif V8_TARGET_ARCH_LOONG64
#define re_stack_check_func RegExpMacroAssemblerLOONG64::CheckStackGuardState
#elif V8_TARGET_ARCH_S390
#define re_stack_check_func RegExpMacroAssemblerS390::CheckStackGuardState
#elif V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64
#define re_stack_check_func RegExpMacroAssemblerRISCV::CheckStackGuardState
#else
UNREACHABLE();
#endif

FUNCTION_REFERENCE(re_check_stack_guard_state, re_stack_check_func)
#undef re_stack_check_func

FUNCTION_REFERENCE(re_grow_stack, NativeRegExpMacroAssembler::GrowStack)

FUNCTION_REFERENCE(re_match_for_call_from_js,
                   IrregexpInterpreter::MatchForCallFromJs)

FUNCTION_REFERENCE(re_experimental_match_for_call_from_js,
                   ExperimentalRegExp::MatchForCallFromJs)

FUNCTION_REFERENCE(re_case_insensitive_compare_unicode,
                   NativeRegExpMacroAssembler::CaseInsensitiveCompareUnicode)

FUNCTION_REFERENCE(re_case_insensitive_compare_non_unicode,
                   NativeRegExpMacroAssembler::CaseInsensitiveCompareNonUnicode)

FUNCTION_REFERENCE(re_is_character_in_range_array,
                   RegExpMacroAssembler::IsCharacterInRangeArray)

ExternalReference ExternalReference::re_word_character_map() {
  return ExternalReference(
      NativeRegExpMacroAssembler::word_character_map_address());
}

ExternalReference ExternalReference::address_of_static_offsets_vector(
    Isolate* isolate) {
  return ExternalReference(
      reinterpret_cast<Address>(isolate->jsregexp_static_offsets_vector()));
}

ExternalReference ExternalReference::address_of_regexp_stack_limit_address(
    Isolate* isolate) {
  return ExternalReference(isolate->regexp_stack()->limit_address_address());
}

ExternalReference ExternalReference::address_of_regexp_stack_memory_top_address(
    Isolate* isolate) {
  return ExternalReference(
      isolate->regexp_stack()->memory_top_address_address());
}

ExternalReference ExternalReference::address_of_regexp_stack_stack_pointer(
    Isolate* isolate) {
  return ExternalReference(isolate->regexp_stack()->stack_pointer_address());
}

FUNCTION_REFERENCE_WITH_TYPE(ieee754_acos_function, base::ieee754::acos,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_acosh_function, base::ieee754::acosh,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_asin_function, base::ieee754::asin,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_asinh_function, base::ieee754::asinh,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_atan_function, base::ieee754::atan,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_atanh_function, base::ieee754::atanh,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_atan2_function, base::ieee754::atan2,
                             BUILTIN_FP_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_cbrt_function, base::ieee754::cbrt,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_cosh_function, base::ieee754::cosh,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_exp_function, base::ieee754::exp,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_expm1_function, base::ieee754::expm1,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_log_function, base::ieee754::log,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_log1p_function, base::ieee754::log1p,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_log10_function, base::ieee754::log10,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_log2_function, base::ieee754::log2,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_sinh_function, base::ieee754::sinh,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_tan_function, base::ieee754::tan,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_tanh_function, base::ieee754::tanh,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_pow_function, base::ieee754::pow,
                             BUILTIN_FP_FP_CALL)

#if defined(V8_USE_LIBM_TRIG_FUNCTIONS)
ExternalReference ExternalReference::ieee754_sin_function() {
  static_assert(
      IsValidExternalReferenceType<decltype(&base::ieee754::libm_sin)>::value);
  static_assert(IsValidExternalReferenceType<
                decltype(&base::ieee754::fdlibm_sin)>::value);
  auto* f = v8_flags.use_libm_trig_functions ? base::ieee754::libm_sin
                                             : base::ieee754::fdlibm_sin;
  return ExternalReference(Redirect(FUNCTION_ADDR(f), BUILTIN_FP_CALL));
}
ExternalReference ExternalReference::ieee754_cos_function() {
  static_assert(
      IsValidExternalReferenceType<decltype(&base::ieee754::libm_cos)>::value);
  static_assert(IsValidExternalReferenceType<
                decltype(&base::ieee754::fdlibm_cos)>::value);
  auto* f = v8_flags.use_libm_trig_functions ? base::ieee754::libm_cos
                                             : base::ieee754::fdlibm_cos;
  return ExternalReference(Redirect(FUNCTION_ADDR(f), BUILTIN_FP_CALL));
}
#else
FUNCTION_REFERENCE_WITH_TYPE(ieee754_sin_function, base::ieee754::sin,
                             BUILTIN_FP_CALL)
FUNCTION_REFERENCE_WITH_TYPE(ieee754_cos_function, base::ieee754::cos,
                             BUILTIN_FP_CALL)
#endif

void* libc_memchr(void* string, int character, size_t search_length) {
  return memchr(string, character, search_length);
}

FUNCTION_REFERENCE(libc_memchr_function, libc_memchr)

void* libc_memcpy(void* dest, const void* src, size_t n) {
  return memcpy(dest, src, n);
}

FUNCTION_REFERENCE(libc_memcpy_function, libc_memcpy)

void* libc_memmove(void* dest, const void* src, size_t n) {
  return memmove(dest, src, n);
}

FUNCTION_REFERENCE(libc_memmove_function, libc_memmove)

void* libc_memset(void* dest, int value, size_t n) {
  DCHECK_EQ(static_cast<uint8_t>(value), value);
  return memset(dest, value, n);
}

FUNCTION_REFERENCE(libc_memset_function, libc_memset)

void relaxed_memcpy(volatile base::Atomic8* dest,
                    volatile const base::Atomic8* src, size_t n) {
  base::Relaxed_Memcpy(dest, src, n);
}

FUNCTION_REFERENCE(relaxed_memcpy_function, relaxed_memcpy)

void relaxed_memmove(volatile base::Atomic8* dest,
                     volatile const base::Atomic8* src, size_t n) {
  base::Relaxed_Memmove(dest, src, n);
}

FUNCTION_REFERENCE(relaxed_memmove_function, relaxed_memmove)

ExternalReference ExternalReference::printf_function() {
  return ExternalReference(Redirect(FUNCTION_ADDR(std::printf)));
}

FUNCTION_REFERENCE(refill_math_random, MathRandom::RefillCache)

template <typename SubjectChar, typename PatternChar>
ExternalReference ExternalReference::search_string_raw() {
  auto f = SearchStringRaw<SubjectChar, PatternChar>;
  return ExternalReference(Redirect(FUNCTION_ADDR(f)));
}

FUNCTION_REFERENCE(jsarray_array_join_concat_to_sequential_string,
                   JSArray::ArrayJoinConcatToSequentialString)

FUNCTION_REFERENCE(gsab_byte_length, JSArrayBuffer::GsabByteLength)

ExternalReference ExternalReference::search_string_raw_one_one() {
  return search_string_raw<const uint8_t, const uint8_t>();
}

ExternalReference ExternalReference::search_string_raw_one_two() {
  return search_string_raw<const uint8_t, const base::uc16>();
}

ExternalReference ExternalReference::search_string_raw_two_one() {
  return search_string_raw<const base::uc16, const uint8_t>();
}

ExternalReference ExternalReference::search_string_raw_two_two() {
  return search_string_raw<const base::uc16, const base::uc16>();
}

ExternalReference
ExternalReference::typed_array_and_rab_gsab_typed_array_elements_kind_shifts() {
  uint8_t* ptr =
      const_cast<uint8_t*>(TypedArrayAndRabGsabTypedArrayElementsKindShifts());
  return ExternalReference(reinterpret_cast<Address>(ptr));
}

ExternalReference
ExternalReference::typed_array_and_rab_gsab_typed_array_elements_kind_sizes() {
  uint8_t* ptr =
      const_cast<uint8_t*>(TypedArrayAndRabGsabTypedArrayElementsKindSizes());
  return ExternalReference(reinterpret_cast<Address>(ptr));
}

namespace {

void StringWriteToFlatOneByte(Address source, uint8_t* sink, int32_t start,
                              int32_t length) {
  return String::WriteToFlat<uint8_t>(Cast<String>(Tagged<Object>(source)),
                                      sink, start, length);
}

void StringWriteToFlatTwoByte(Address source, uint16_t* sink, int32_t start,
                              int32_t length) {
  return String::WriteToFlat<uint16_t>(Cast<String>(Tagged<Object>(source)),
                                       sink, start, length);
}

const uint8_t* ExternalOneByteStringGetChars(Address string) {
  // The following CHECK is a workaround to prevent a CFI bug where
  // ExternalOneByteStringGetChars() and ExternalTwoByteStringGetChars() are
  // merged by the linker, resulting in one of the input type's vtable address
  // failing the address range check.
  // TODO(chromium:1160961): Consider removing the CHECK when CFI is fixed.
  CHECK(IsExternalOneByteString(Tagged<Object>(string)));
  return Cast<ExternalOneByteString>(Tagged<Object>(string))->GetChars();
}
const uint16_t* ExternalTwoByteStringGetChars(Address string) {
  // The following CHECK is a workaround to prevent a CFI bug where
  // ExternalOneByteStringGetChars() and ExternalTwoByteStringGetChars() are
  // merged by the linker, resulting in one of the input type's vtable address
  // failing the address range check.
  // TODO(chromium:1160961): Consider removing the CHECK when CFI is fixed.
  CHECK(IsExternalTwoByteString(Tagged<Object>(string)));
  return Cast<ExternalTwoByteString>(Tagged<Object>(string))->GetChars();
}

}  // namespace

FUNCTION_REFERENCE(string_write_to_flat_one_byte, StringWriteToFlatOneByte)
FUNCTION_REFERENCE(string_write_to_flat_two_byte, StringWriteToFlatTwoByte)

FUNCTION_REFERENCE(external_one_byte_string_get_chars,
                   ExternalOneByteStringGetChars)
FUNCTION_REFERENCE(external_two_byte_string_get_chars,
                   ExternalTwoByteStringGetChars)

// See:
// https://lemire.me/blog/2021/06/03/computing-the-number-of-digits-of-an-integer-even-faster/
static constexpr uint64_t kLog10OffsetTable[] = {
    0x100000000, 0x1fffffff6, 0x1fffffff6, 0x1fffffff6, 0x2ffffff9c,
    0x2ffffff9c, 0x2ffffff9c, 0x3fffffc18, 0x3fffffc18, 0x3fffffc18,
    0x4ffffd8f0, 0x4ffffd8f0, 0x4ffffd8f0, 0x4ffffd8f0, 0x5fffe7960,
    0x5fffe7960, 0x5fffe7960, 0x6fff0bdc0, 0x6fff0bdc0, 0x6fff0bdc0,
    0x7ff676980, 0x7ff676980, 0x7ff676980, 0x7ff676980, 0x8fa0a1f00,
    0x8fa0a1f00, 0x8fa0a1f00, 0x9c4653600, 0x9c4653600, 0x9c4653600,
    0xa00000000, 0xa00000000,
};

ExternalReference ExternalReference::address_of_log10_offset_table() {
  return ExternalReference(reinterpret_cast<Address>(&kLog10OffsetTable[0]));
}

FUNCTION_REFERENCE(orderedhashmap_gethash_raw, OrderedHashMap::GetHash)

Address GetOrCreateHash(Isolate* isolate, Address raw_key) {
  DisallowGarbageCollection no_gc;
  return Object::GetOrCreateHash(Tagged<Object>(raw_key), isolate).ptr();
}

FUNCTION_REFERENCE(get_or_create_hash_raw, GetOrCreateHash)

static Address JSReceiverCreateIdentityHash(Isolate* isolate, Address raw_key) {
  Tagged<JSReceiver> key = Cast<JSReceiver>(Tagged<Object>(raw_key));
  return JSReceiver::CreateIdentityHash(isolate, key).ptr();
}

FUNCTION_REFERENCE(jsreceiver_create_identity_hash,
                   JSReceiverCreateIdentityHash)

static uint32_t ComputeSeededIntegerHash(Isolate* isolate, int32_t key) {
  DisallowGarbageCollection no_gc;
  return ComputeSeededHash(static_cast<uint32_t>(key), HashSeed(isolate));
}

FUNCTION_REFERENCE(compute_integer_hash, ComputeSeededIntegerHash)

enum LookupMode { kFindExisting, kFindInsertionEntry };
template <typename Dictionary, LookupMode mode>
static size_t NameDictionaryLookupForwardedString(Isolate* isolate,
                                                  Address raw_dict,
                                                  Address raw_key) {
  // This function cannot allocate, but there is a HandleScope because it needs
  // to pass Handle<Name> to the dictionary methods.
  DisallowGarbageCollection no_gc;
  HandleScope handle_scope(isolate);

  Handle<String> key(Cast<String>(Tagged<Object>(raw_key)), isolate);
  // This function should only be used as the slow path for forwarded strings.
  DCHECK(Name::IsForwardingIndex(key->raw_hash_field()));

  Tagged<Dictionary> dict = Cast<Dictionary>(Tagged<Object>(raw_dict));
  ReadOnlyRoots roots(isolate);
  uint32_t hash = key->hash();
  InternalIndex entry = mode == kFindExisting
                            ? dict->FindEntry(isolate, roots, key, hash)
                            : dict->FindInsertionEntry(isolate, roots, hash);
  return entry.raw_value();
}

FUNCTION_REFERENCE(
    name_dictionary_lookup_forwarded_string,
    (NameDictionaryLookupForwardedString<NameDictionary, kFindExisting>))
FUNCTION_REFERENCE(
    name_dictionary_find_insertion_entry_forwarded_string,
    (NameDictionaryLookupForwardedString<NameDictionary, kFindInsertionEntry>))
FUNCTION_REFERENCE(
    global_dictionary_lookup_forwarded_string,
    (NameDictionaryLookupForwardedString<GlobalDictionary, kFindExisting>))
FUNCTION_REFERENCE(global_dictionary_find_insertion_entry_forwarded_string,
                   (NameDictionaryLookupForwardedString<GlobalDictionary,
                                                        kFindInsertionEntry>))
FUNCTION_REFERENCE(
    name_to_index_hashtable_lookup_forwarded_string,
    (NameDictionaryLookupForwardedString<NameToIndexHashTable, kFindExisting>))
FUNCTION_REFERENCE(
    name_to_index_hashtable_find_insertion_entry_forwarded_string,
    (NameDictionaryLookupForwardedString<NameToIndexHashTable,
                                         kFindInsertionEntry>))

FUNCTION_REFERENCE(copy_fast_number_jsarray_elements_to_typed_array,
                   CopyFastNumberJSArrayElementsToTypedArray)
FUNCTION_REFERENCE(copy_typed_array_elements_to_typed_array,
                   CopyTypedArrayElementsToTypedArray)
FUNCTION_REFERENCE(copy_typed_array_elements_slice, CopyTypedArrayElementsSlice)
FUNCTION_REFERENCE(try_string_to_index_or_lookup_existing,
                   StringTable::TryStringToIndexOrLookupExisting)
FUNCTION_REFERENCE(string_from_forward_table,
                   StringForwardingTable::GetForwardStringAddress)
FUNCTION_REFERENCE(raw_hash_from_forward_table,
                   StringForwardingTable::GetRawHashStatic)
FUNCTION_REFERENCE(string_to_array_index_function, String::ToArrayIndex)
FUNCTION_REFERENCE(array_indexof_includes_smi_or_object,
                   ArrayIndexOfIncludesSmiOrObject)
FUNCTION_REFERENCE(array_indexof_includes_double, ArrayIndexOfIncludesDouble)

static Address LexicographicCompareWrapper(Isolate* isolate, Address smi_x,
                                           Address smi_y) {
  Tagged<Smi> x(smi_x);
  Tagged<Smi> y(smi_y);
  return Smi::LexicographicCompare(isolate, x, y);
}

FUNCTION_REFERENCE(smi_lexicographic_compare_function,
                   LexicographicCompareWrapper)

uint32_t HasUnpairedSurrogate(const uint16_t* code_units, size_t length) {
  // Use uint32_t to avoid complexity around bool return types.
  static constexpr uint32_t kTrue = 1;
  static constexpr uint32_t kFalse = 0;
  return unibrow::Utf16::HasUnpairedSurrogate(code_units, length) ? kTrue
                                                                  : kFalse;
}

FUNCTION_REFERENCE(has_unpaired_surrogate, HasUnpairedSurrogate)

void ReplaceUnpairedSurrogates(const uint16_t* source_code_units,
                               uint16_t* dest_code_units, size_t length) {
  return unibrow::Utf16::ReplaceUnpairedSurrogates(source_code_units,
                                                   dest_code_units, length);
}

FUNCTION_REFERENCE(replace_unpaired_surrogates, ReplaceUnpairedSurrogates)

FUNCTION_REFERENCE(mutable_big_int_absolute_add_and_canonicalize_function,
                   MutableBigInt_AbsoluteAddAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_absolute_compare_function,
                   MutableBigInt_AbsoluteCompare)

FUNCTION_REFERENCE(mutable_big_int_absolute_sub_and_canonicalize_function,
                   MutableBigInt_AbsoluteSubAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_absolute_mul_and_canonicalize_function,
                   MutableBigInt_AbsoluteMulAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_absolute_div_and_canonicalize_function,
                   MutableBigInt_AbsoluteDivAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_absolute_mod_and_canonicalize_function,
                   MutableBigInt_AbsoluteModAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_bitwise_and_pp_and_canonicalize_function,
                   MutableBigInt_BitwiseAndPosPosAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_bitwise_and_nn_and_canonicalize_function,
                   MutableBigInt_BitwiseAndNegNegAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_bitwise_and_pn_and_canonicalize_function,
                   MutableBigInt_BitwiseAndPosNegAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_bitwise_or_pp_and_canonicalize_function,
                   MutableBigInt_BitwiseOrPosPosAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_bitwise_or_nn_and_canonicalize_function,
                   MutableBigInt_BitwiseOrNegNegAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_bitwise_or_pn_and_canonicalize_function,
                   MutableBigInt_BitwiseOrPosNegAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_bitwise_xor_pp_and_canonicalize_function,
                   MutableBigInt_BitwiseXorPosPosAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_bitwise_xor_nn_and_canonicalize_function,
                   MutableBigInt_BitwiseXorNegNegAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_bitwise_xor_pn_and_canonicalize_function,
                   MutableBigInt_BitwiseXorPosNegAndCanonicalize)

FUNCTION_REFERENCE(mutable_big_int_left_shift_and_canonicalize_function,
                   MutableBigInt_LeftShiftAndCanonicalize)

FUNCTION_REFERENCE(big_int_right_shift_result_length_function,
                   RightShiftResultLength)

FUNCTION_REFERENCE(mutable_big_int_right_shift_and_canonicalize_function,
                   MutableBigInt_RightShiftAndCanonicalize)

FUNCTION_REFERENCE(check_object_type, CheckObjectType)

#ifdef V8_INTL_SUPPORT

static Address ConvertOneByteToLower(Address raw_src, Address raw_dst) {
  Tagged<String> src = Cast<String>(Tagged<Object>(raw_src));
  Tagged<String> dst = Cast<String>(Tagged<Object>(raw_dst));
  return Intl::ConvertOneByteToLower(src, dst).ptr();
}
FUNCTION_REFERENCE(intl_convert_one_byte_to_lower, ConvertOneByteToLower)

ExternalReference ExternalReference::intl_to_latin1_lower_table() {
  uint8_t* ptr = const_cast<uint8_t*>(Intl::ToLatin1LowerTable());
  return ExternalReference(reinterpret_cast<Address>(ptr));
}

ExternalReference ExternalReference::intl_ascii_collation_weights_l1() {
  uint8_t* ptr = const_cast<uint8_t*>(Intl::AsciiCollationWeightsL1());
  return ExternalReference(reinterpret_cast<Address>(ptr));
}

ExternalReference ExternalReference::intl_ascii_collation_weights_l3() {
  uint8_t* ptr = const_cast<uint8_t*>(Intl::AsciiCollationWeightsL3());
  return ExternalReference(reinterpret_cast<Address>(ptr));
}

#endif  // V8_INTL_SUPPORT

// Explicit instantiations for all combinations of 1- and 2-byte strings.
template ExternalReference
ExternalReference::search_string_raw<const uint8_t, const uint8_t>();
template ExternalReference
ExternalReference::search_string_raw<const uint8_t, const base::uc16>();
template ExternalReference
ExternalReference::search_string_raw<const base::uc16, const uint8_t>();
template ExternalReference
ExternalReference::search_string_raw<const base::uc16, const base::uc16>();

ExternalReference ExternalReference::FromRawAddress(Address address) {
  if (address <= static_cast<Address>(kNumIsolateFieldIds)) {
    return ExternalReference(static_cast<IsolateFieldId>(address));
  }
  return ExternalReference(address);
}

ExternalReference ExternalReference::cpu_features() {
  DCHECK(CpuFeatures::initialized_);
  return ExternalReference(&CpuFeatures::supported_);
}

ExternalReference ExternalReference::promise_hook_flags_address(
    Isolate* isolate) {
  return ExternalReference(isolate->promise_hook_flags_address());
}

ExternalReference ExternalReference::promise_hook_address(Isolate* isolate) {
  return ExternalReference(isolate->promise_hook_address());
}

ExternalReference ExternalReference::async_event_delegate_address(
    Isolate* isolate) {
  return ExternalReference(isolate->async_event_delegate_address());
}

ExternalReference ExternalReference::debug_is_active_address(Isolate* isolate) {
  return ExternalReference(isolate->debug()->is_active_address());
}

ExternalReference ExternalReference::debug_hook_on_function_call_address(
    Isolate* isolate) {
  return ExternalReference(isolate->debug()->hook_on_function_call_address());
}

ExternalReference ExternalReference::runtime_function_table_address(
    Isolate* isolate) {
  return ExternalReference(
      const_cast<Runtime::Function*>(Runtime::RuntimeFunctionTable(isolate)));
}

static Address InvalidatePrototypeChainsWrapper(Address raw_map) {
  Tagged<Map> map = Cast<Map>(Tagged<Object>(raw_map));
  return JSObject::InvalidatePrototypeChains(map).ptr();
}

FUNCTION_REFERENCE(invalidate_prototype_chains_function,
                   InvalidatePrototypeChainsWrapper)

double modulo_double_double(double x, double y) { return Modulo(x, y); }

FUNCTION_REFERENCE_WITH_TYPE(mod_two_doubles_operation, modulo_double_double,
                             BUILTIN_FP_FP_CALL)

ExternalReference ExternalReference::debug_suspended_generator_address(
    Isolate* isolate) {
  return ExternalReference(isolate->debug()->suspended_generator_address());
}

ExternalReference ExternalReference::context_address(Isolate* isolate) {
  return ExternalReference(isolate->context_address());
}

FUNCTION_REFERENCE(call_enqueue_microtask_function,
                   MicrotaskQueue::CallEnqueueMicrotask)

ExternalReference ExternalReference::int64_mul_high_function() {
  return ExternalReference(
      Redirect(FUNCTION_ADDR(base::bits::SignedMulHigh64)));
}

static int64_t atomic_pair_load(intptr_t address) {
  return std::atomic_load(reinterpret_cast<std::atomic<int64_t>*>(address));
}

ExternalReference ExternalReference::atomic_pair_load_function() {
  return ExternalReference(Redirect(FUNCTION_ADDR(atomic_pair_load)));
}

static void atomic_pair_store(intptr_t address, int value_low, int value_high) {
  int64_t value =
      static_cast<int64_t>(value_high) << 32 | (value_low & 0xFFFFFFFF);
  std::atomic_store(reinterpret_cast<std::atomic<int64_t>*>(address), value);
}

ExternalReference ExternalReference::atomic_pair_store_function() {
  return ExternalReference(Redirect(FUNCTION_ADDR(atomic_pair_store)));
}

static int64_t atomic_pair_add(intptr_t address, int value_low,
                               int value_high) {
  int64_t value =
      static_cast<int64_t>(value_high) << 32 | (value_low & 0xFFFFFFFF);
  return std::atomic_fetch_add(reinterpret_cast<std::atomic<int64_t>*>(address),
                               value);
}

ExternalReference ExternalReference::atomic_pair_add_function() {
  return ExternalReference(Redirect(FUNCTION_ADDR(atomic_pair_add)));
}

static int64_t atomic_pair_sub(intptr_t address, int value_low,
                               int value_high) {
  int64_t value =
      static_cast<int64_t>(value_high) << 32 | (value_low & 0xFFFFFFFF);
  return std::atomic_fetch_sub(reinterpret_cast<std::atomic<int64_t>*>(address),
                               value);
}

ExternalReference ExternalReference::atomic_pair_sub_function() {
  return ExternalReference(Redirect(FUNCTION_ADDR(atomic_pair_sub)));
}

static int64_t atomic_pair_and(intptr_t address, int value_low,
                               int value_high) {
  int64_t value =
      static_cast<int64_t>(value_high) << 32 | (value_low & 0xFFFFFFFF);
  return std::atomic_fetch_and(reinterpret_cast<std::atomic<int64_t>*>(address),
                               value);
}

ExternalReference ExternalReference::atomic_pair_and_function() {
  return ExternalReference(Redirect(FUNCTION_ADDR(atomic_pair_and)));
}

static int64_t atomic_pair_or(intptr_t address, int value_low, int value_high) {
  int64_t value =
      static_cast<int64_t>(value_high) << 32 | (value_low & 0xFFFFFFFF);
  return std::atomic_fetch_or(reinterpret_cast<std::atomic<int64_t>*>(address),
                              value);
}

ExternalReference ExternalReference::atomic_pair_or_function() {
  return ExternalReference(Redirect(FUNCTION_ADDR(atomic_pair_or)));
}

static int64_t atomic_pair_xor(intptr_t address, int value_low,
                               int value_high) {
  int64_t value =
      static_cast<int64_t>(value_high) << 32 | (value_low & 0xFFFFFFFF);
  return std::atomic_fetch_xor(reinterpret_cast<std::atomic<int64_t>*>(address),
                               value);
}

ExternalReference ExternalReference::atomic_pair_xor_function() {
  return ExternalReference(Redirect(FUNCTION_ADDR(atomic_pair_xor)));
}

static int64_t atomic_pair_exchange(intptr_t address, int value_low,
                                    int value_high) {
  int64_t value =
      static_cast<int64_t>(value_high) << 32 | (value_low & 0xFFFFFFFF);
  return std::atomic_exchange(reinterpret_cast<std::atomic<int64_t>*>(address),
                              value);
}

ExternalReference ExternalReference::atomic_pair_exchange_function() {
  return ExternalReference(Redirect(FUNCTION_ADDR(atomic_pair_exchange)));
}

static uint64_t atomic_pair_compare_exchange(intptr_t address,
                                             int old_value_low,
                                             int old_value_high,
                                             int new_value_low,
                                             int new_value_high) {
  uint64_t old_value = static_cast<uint64_t>(old_value_high) << 32 |
                       (old_value_low & 0xFFFFFFFF);
  uint64_t new_value = static_cast<uint64_t>(new_value_high) << 32 |
                       (new_value_low & 0xFFFFFFFF);
  std::atomic_compare_exchange_strong(
      reinterpret_cast<std::atomic<uint64_t>*>(address), &old_value, new_value);
  return old_value;
}

FUNCTION_REFERENCE(atomic_pair_compare_exchange_function,
                   atomic_pair_compare_exchange)

#ifdef V8_IS_TSAN
namespace {
// Mimics the store in generated code by having a relaxed store to the same
// address, with the same value. This is done in order for TSAN to see these
// stores from generated code.
// Note that {value} is an int64_t irrespective of the store size. This is on
// purpose to keep the function signatures the same across stores. The
// static_cast inside the method will ignore the bits which will not be stored.
void tsan_relaxed_store_8_bits(Address addr, int64_t value) {
#if V8_TARGET_ARCH_X64
  base::Relaxed_Store(reinterpret_cast<base::Atomic8*>(addr),
                      static_cast<base::Atomic8>(value));
#else
  UNREACHABLE();
#endif  // V8_TARGET_ARCH_X64
}

void tsan_relaxed_store_16_bits(Address addr, int64_t value) {
#if V8_TARGET_ARCH_X64
  base::Relaxed_Store(reinterpret_cast<base::Atomic16*>(addr),
                      static_cast<base::Atomic16>(value));
#else
  UNREACHABLE();
#endif  // V8_TARGET_ARCH_X64
}

void tsan_relaxed_store_32_bits(Address addr, int64_t value) {
#if V8_TARGET_ARCH_X64
    base::Relaxed_Store(reinterpret_cast<base::Atomic32*>(addr),
                        static_cast<base::Atomic32>(value));
#else
  UNREACHABLE();
#endif  // V8_TARGET_ARCH_X64
}

void tsan_relaxed_store_64_bits(Address addr, int64_t value) {
#if V8_TARGET_ARCH_X64
  base::Relaxed_Store(reinterpret_cast<base::Atomic64*>(addr),
                      static_cast<base::Atomic64>(value));
#else
  UNREACHABLE();
#endif  // V8_TARGET_ARCH_X64
}

// Same as above, for sequentially consistent stores.
void tsan_seq_cst_store_8_bits(Address addr, int64_t value) {
#if V8_TARGET_ARCH_X64
  base::SeqCst_Store(reinterpret_cast<base::Atomic8*>(addr),
                     static_cast<base::Atomic8>(value));
#else
  UNREACHABLE();
#endif  // V8_TARGET_ARCH_X64
}

void tsan_seq_cst_store_16_bits(Address addr, int64_t value) {
#if V8_TARGET_ARCH_X64
  base::SeqCst_Store(reinterpret_cast<base::Atomic16*>(addr),
                     static_cast<base::Atomic16>(value));
#else
  UNREACHABLE();
#endif  // V8_TARGET_ARCH_X64
}

void tsan_seq_cst_store_32_bits(Address addr, int64_t value) {
#if V8_TARGET_ARCH_X64
  base::SeqCst_Store(reinterpret_cast<base::Atomic32*>(addr),
                     static_cast<base::Atomic32>(value));
#else
  UNREACHABLE();
#endif  // V8_TARGET_ARCH_X64
}

void tsan_seq_cst_store_64_bits(Address addr, int64_t value) {
#if V8_TARGET_ARCH_X64
  base::SeqCst_Store(reinterpret_cast<base::Atomic64*>(addr),
                     static_cast<base::Atomic64>(value));
#else
  UNREACHABLE();
#endif  // V8_TARGET_ARCH_X64
}

// Same as above, for relaxed loads.
base::Atomic32 tsan_relaxed_load_32_bits(Address addr, int64_t value) {
#if V8_TARGET_ARCH_X64
  return base::Relaxed_Load(reinterpret_cast<base::Atomic32*>(addr));
#else
  UNREACHABLE();
#endif  // V8_TARGET_ARCH_X64
}

base::Atomic64 tsan_relaxed_load_64_bits(Address addr, int64_t value) {
#if V8_TARGET_ARCH_X64
  return base::Relaxed_Load(reinterpret_cast<base::Atomic64*>(addr));
#else
  UNREACHABLE();
#endif  // V8_TARGET_ARCH_X64
}

}  // namespace
#endif  // V8_IS_TSAN

IF_TSAN(FUNCTION_REFERENCE, tsan_relaxed_store_function_8_bits,
        tsan_relaxed_store_8_bits)
IF_TSAN(FUNCTION_REFERENCE, tsan_relaxed_store_function_16_bits,
        tsan_relaxed_store_16_bits)
IF_TSAN(FUNCTION_REFERENCE, tsan_relaxed_store_function_32_bits,
        tsan_relaxed_store_32_bits)
IF_TSAN(FUNCTION_REFERENCE, tsan_relaxed_store_function_64_bits,
        tsan_relaxed_store_64_bits)
IF_TSAN(FUNCTION_REFERENCE, tsan_seq_cst_store_function_8_bits,
        tsan_seq_cst_store_8_bits)
IF_TSAN(FUNCTION_REFERENCE, tsan_seq_cst_store_function_16_bits,
        tsan_seq_cst_store_16_bits)
IF_TSAN(FUNCTION_REFERENCE, tsan_seq_cst_store_function_32_bits,
        tsan_seq_cst_store_32_bits)
IF_TSAN(FUNCTION_REFERENCE, tsan_seq_cst_store_function_64_bits,
        tsan_seq_cst_store_64_bits)
IF_TSAN(FUNCTION_REFERENCE, tsan_relaxed_load_function_32_bits,
        tsan_relaxed_load_32_bits)
IF_TSAN(FUNCTION_REFERENCE, tsan_relaxed_load_function_64_bits,
        tsan_relaxed_load_64_bits)

static int EnterContextWrapper(HandleScopeImplementer* hsi,
                               Address raw_context) {
  Tagged<NativeContext> context =
      Cast<NativeContext>(Tagged<Object>(raw_context));
  hsi->EnterContext(context);
  return 0;
}

FUNCTION_REFERENCE(call_enter_context_function, EnterContextWrapper)

FUNCTION_REFERENCE(
    js_finalization_registry_remove_cell_from_unregister_token_map,
    JSFinalizationRegistry::RemoveCellFromUnregisterTokenMap)

bool operator==(ExternalReference lhs, ExternalReference rhs) {
  return lhs.raw() == rhs.raw();
}

bool operator!=(ExternalReference lhs, ExternalReference rhs) {
  return !(lhs == rhs);
}

size_t hash_value(ExternalReference reference) {
  if (v8_flags.predictable) {
    // Avoid ASLR non-determinism in predictable mode. For this, just take the
    // lowest 12 bit corresponding to a 4K page size.
    return base::hash<Address>()(reference.raw() & 0xfff);
  }
  return base::hash<Address>()(reference.raw());
}

namespace {
static constexpr const char* GetNameOfIsolateFieldId(IsolateFieldId id) {
  switch (id) {
#define CASE(id, name, camel)    \
  case IsolateFieldId::k##camel: \
    return name;
    EXTERNAL_REFERENCE_LIST_ISOLATE_FIELDS(CASE)
#undef CASE
#define CASE(camel, size, name)  \
  case IsolateFieldId::k##camel: \
    return #name;
    ISOLATE_DATA_FIELDS(CASE)
#undef CASE
    default:
      return "unknown";
  }
}
}  // namespace

std::ostream& operator<<(std::ostream& os, ExternalReference reference) {
  os << reinterpret_cast<const void*>(reference.raw());
  if (reference.IsIsolateFieldId()) {
    os << "<"
       << GetNameOfIsolateFieldId(static_cast<IsolateFieldId>(reference.raw()))
       << ">";
  } else {
    const Runtime::Function* fn =
        Runtime::FunctionForEntry(reference.address());
    if (fn) os << "<" << fn->name << ".entry>";
  }
  return os;
}

void abort_with_reason(int reason) {
  if (IsValidAbortReason(reason)) {
    const char* message = GetAbortReason(static_cast<AbortReason>(reason));
    base::OS::PrintError("abort: %s\n", message);
  } else {
    base::OS::PrintError("abort: <unknown reason: %d>\n", reason);
  }
  base::OS::Abort();
  UNREACHABLE();
}

#undef RAW_FUNCTION_REFERENCE
#undef FUNCTION_REFERENCE
#undef FUNCTION_REFERENCE_WITH_TYPE

}  // namespace internal
}  // namespace v8

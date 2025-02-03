// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "src/base/vector.h"
#include "src/codegen/signature.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/struct-types.h"
#include "src/wasm/wasm-arguments.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/common/wasm/wasm-module-runner.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_gc {

using F = std::pair<ValueType, bool>;

class WasmGCTester {
 public:
  explicit WasmGCTester(
      TestExecutionTier execution_tier = TestExecutionTier::kTurbofan)
      : flag_liftoff(&v8::internal::v8_flags.liftoff,
                     execution_tier == TestExecutionTier::kLiftoff),
        flag_liftoff_only(&v8::internal::v8_flags.liftoff_only,
                          execution_tier == TestExecutionTier::kLiftoff),
        flag_wasm_dynamic_tiering(&v8::internal::v8_flags.wasm_dynamic_tiering,
                                  v8::internal::v8_flags.liftoff_only != true),
        flag_tierup(&v8::internal::v8_flags.wasm_tier_up, false),
        // Manually apply flag implication by disabling deopts in case of
        // --no-liftoff.
        flag_wasm_deopt(&v8::internal::v8_flags.wasm_deopt,
                        v8_flags.wasm_deopt && v8_flags.liftoff),
        zone_(&allocator, ZONE_NAME),
        builder_(&zone_),
        isolate_(CcTest::InitIsolateOnce()),
        scope(isolate_),
        thrower(isolate_, "Test wasm GC") {
    testing::SetupIsolateForWasmModule(isolate_);
  }

  uint8_t AddGlobal(ValueType type, bool mutability, WasmInitExpr init) {
    return builder_.AddGlobal(type, mutability, init);
  }

  uint8_t DefineFunction(FunctionSig* sig,
                         std::initializer_list<ValueType> locals,
                         std::initializer_list<uint8_t> code) {
    return DefineFunctionImpl(builder_.AddFunction(sig), locals, code);
  }

  uint8_t DefineFunction(uint32_t sig_index,
                         std::initializer_list<ValueType> locals,
                         std::initializer_list<uint8_t> code) {
    return DefineFunctionImpl(builder_.AddFunction(sig_index), locals, code);
  }

  void DefineExportedFunction(const char* name, FunctionSig* sig,
                              std::initializer_list<uint8_t> code) {
    WasmFunctionBuilder* fun = builder_.AddFunction(sig);
    fun->EmitCode(code.begin(), static_cast<uint32_t>(code.size()));
    builder_.AddExport(base::CStrVector(name), fun);
  }

  MaybeHandle<Object> CallExportedFunction(const char* name, int argc,
                                           Handle<Object> args[]) {
    Handle<WasmExportedFunction> func =
        testing::GetExportedFunction(isolate_, instance_object_, name)
            .ToHandleChecked();
    return Execution::Call(isolate_, func,
                           isolate_->factory()->undefined_value(), argc, args);
  }

  uint8_t DefineStruct(std::initializer_list<F> fields,
                       uint32_t supertype = kNoSuperType,
                       bool is_final = false) {
    StructType::Builder type_builder(&zone_,
                                     static_cast<uint32_t>(fields.size()));
    for (F field : fields) {
      type_builder.AddField(field.first, field.second);
    }
    return builder_.AddStructType(type_builder.Build(), is_final, supertype);
  }

  uint8_t DefineArray(ValueType element_type, bool mutability,
                      uint32_t supertype = kNoSuperType,
                      bool is_final = false) {
    return builder_.AddArrayType(zone_.New<ArrayType>(element_type, mutability),
                                 is_final, supertype);
  }

  uint8_t DefineSignature(FunctionSig* sig, uint32_t supertype = kNoSuperType,
                          bool is_final = false) {
    return builder_.ForceAddSignature(sig, is_final, supertype);
  }

  uint8_t DefineTable(ValueType type, uint32_t min_size, uint32_t max_size) {
    return builder_.AddTable(type, min_size, max_size);
  }

  void CompileModule() {
    ZoneBuffer buffer(&zone_);
    builder_.WriteTo(&buffer);
    MaybeHandle<WasmInstanceObject> maybe_instance =
        testing::CompileAndInstantiateForTesting(
            isolate_, &thrower, ModuleWireBytes(buffer.begin(), buffer.end()));
    if (thrower.error()) FATAL("%s", thrower.error_msg());
    instance_object_ = maybe_instance.ToHandleChecked();
    trusted_instance_data_ =
        handle(instance_object_->trusted_data(isolate_), isolate_);
  }

  void CheckResult(uint32_t function_index, int32_t expected) {
    const FunctionSig* sig = sigs.i_v();
    DCHECK(*sig == *instance_object_->module()->functions[function_index].sig);
    CWasmArgumentsPacker packer(CWasmArgumentsPacker::TotalSize(sig));
    CheckResultImpl(function_index, sig, &packer, expected);
  }

  void CheckResult(uint32_t function_index, int32_t expected, int32_t arg) {
    const FunctionSig* sig = sigs.i_i();
    DCHECK(*sig == *instance_object_->module()->functions[function_index].sig);
    CWasmArgumentsPacker packer(CWasmArgumentsPacker::TotalSize(sig));
    packer.Push(arg);
    CheckResultImpl(function_index, sig, &packer, expected);
  }

  MaybeHandle<Object> GetResultObject(uint32_t function_index) {
    const FunctionSig* sig =
        instance_object_->module()->functions[function_index].sig;
    DCHECK_EQ(sig->parameter_count(), 0);
    DCHECK_EQ(sig->return_count(), 1);
    CWasmArgumentsPacker packer(CWasmArgumentsPacker::TotalSize(sig));
    CallFunctionImpl(function_index, sig, &packer);
    CHECK(!isolate_->has_exception());
    packer.Reset();
    return Handle<Object>(Tagged<Object>(packer.Pop<Address>()), isolate_);
  }

  MaybeHandle<Object> GetResultObject(uint32_t function_index, int32_t arg) {
    const FunctionSig* sig =
        instance_object_->module()->functions[function_index].sig;
    DCHECK_EQ(sig->parameter_count(), 1);
    DCHECK_EQ(sig->return_count(), 1);
    DCHECK(sig->parameters()[0] == kWasmI32);
    CWasmArgumentsPacker packer(CWasmArgumentsPacker::TotalSize(sig));
    packer.Push(arg);
    CallFunctionImpl(function_index, sig, &packer);
    CHECK(!isolate_->has_exception());
    packer.Reset();
    return Handle<Object>(Tagged<Object>(packer.Pop<Address>()), isolate_);
  }

  void CheckHasThrown(uint32_t function_index, const char* expected = "") {
    const FunctionSig* sig =
        instance_object_->module()->functions[function_index].sig;
    DCHECK_EQ(sig->parameter_count(), 0);
    CWasmArgumentsPacker packer(CWasmArgumentsPacker::TotalSize(sig));
    CheckHasThrownImpl(function_index, sig, &packer, expected);
  }

  void CheckHasThrown(uint32_t function_index, int32_t arg,
                      const char* expected = "") {
    const FunctionSig* sig =
        instance_object_->module()->functions[function_index].sig;
    DCHECK_EQ(sig->parameter_count(), 1);
    DCHECK(sig->parameters()[0] == kWasmI32);
    CWasmArgumentsPacker packer(CWasmArgumentsPacker::TotalSize(sig));
    packer.Push(arg);
    CheckHasThrownImpl(function_index, sig, &packer, expected);
  }

  bool HasSimdSupport(TestExecutionTier tier) const {
#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_IA32
    // Liftoff does not have a fallback for executing SIMD instructions if
    // SSE4_1 is not available.
    if (tier == TestExecutionTier::kLiftoff &&
        !CpuFeatures::IsSupported(SSE4_1)) {
      return false;
    }
#endif
    USE(tier);
    return true;
  }

  Handle<WasmInstanceObject> instance_object() const {
    return instance_object_;
  }
  Handle<WasmTrustedInstanceData> trusted_instance_data() const {
    return trusted_instance_data_;
  }
  Isolate* isolate() const { return isolate_; }
  WasmModuleBuilder* builder() { return &builder_; }
  Zone* zone() { return &zone_; }

  TestSignatures sigs;

 private:
  const FlagScope<bool> flag_liftoff;
  const FlagScope<bool> flag_liftoff_only;
  const FlagScope<bool> flag_wasm_dynamic_tiering;
  const FlagScope<bool> flag_tierup;
  const FlagScope<bool> flag_wasm_deopt;

  uint8_t DefineFunctionImpl(WasmFunctionBuilder* fun,
                             std::initializer_list<ValueType> locals,
                             std::initializer_list<uint8_t> code) {
    for (ValueType local : locals) {
      fun->AddLocal(local);
    }
    fun->EmitCode(code.begin(), static_cast<uint32_t>(code.size()));
    return fun->func_index();
  }

  void CheckResultImpl(uint32_t function_index, const FunctionSig* sig,
                       CWasmArgumentsPacker* packer, int32_t expected) {
    CallFunctionImpl(function_index, sig, packer);
    if (isolate_->has_exception()) {
      DirectHandle<String> message =
          ErrorUtils::ToString(isolate_,
                               handle(isolate_->exception(), isolate_))
              .ToHandleChecked();
      FATAL("%s", message->ToCString().get());
    }
    packer->Reset();
    CHECK_EQ(expected, packer->Pop<int32_t>());
  }

  void CheckHasThrownImpl(uint32_t function_index, const FunctionSig* sig,
                          CWasmArgumentsPacker* packer, const char* expected) {
    CallFunctionImpl(function_index, sig, packer);
    CHECK(isolate_->has_exception());
    DirectHandle<String> message =
        ErrorUtils::ToString(isolate_, handle(isolate_->exception(), isolate_))
            .ToHandleChecked();
    std::string message_str(message->ToCString().get());
    CHECK_NE(message_str.find(expected), std::string::npos);
    isolate_->clear_exception();
  }

  void CallFunctionImpl(uint32_t function_index, const FunctionSig* sig,
                        CWasmArgumentsPacker* packer) {
    WasmCodeRefScope code_ref_scope;
    const WasmModule* module = trusted_instance_data_->module();
    Address wasm_call_target =
        trusted_instance_data_->GetCallTarget(function_index);
    DirectHandle<Object> object_ref = instance_object_;
    DirectHandle<Code> c_wasm_entry =
        compiler::CompileCWasmEntry(isolate_, sig, module);
    Execution::CallWasm(isolate_, c_wasm_entry, wasm_call_target, object_ref,
                        packer->argv());
  }

  v8::internal::AccountingAllocator allocator;
  Zone zone_;
  WasmModuleBuilder builder_;

  Isolate* const isolate_;
  const HandleScope scope;
  Handle<WasmInstanceObject> instance_object_;
  Handle<WasmTrustedInstanceData> trusted_instance_data_;
  ErrorThrower thrower;
};

ValueType ref(uint32_t type_index) { return ValueType::Ref(type_index); }
ValueType refNull(uint32_t type_index) {
  return ValueType::RefNull(type_index);
}

WASM_COMPILED_EXEC_TEST(WasmBasicStruct) {
  WasmGCTester tester(execution_tier);

  const uint8_t type_index =
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)});
  const uint8_t empty_struct_index = tester.DefineStruct({});
  ValueType kRefType = ref(type_index);
  ValueType kEmptyStructType = ref(empty_struct_index);
  ValueType kRefNullType = refNull(type_index);
  FunctionSig sig_q_v(1, 0, &kRefType);
  FunctionSig sig_qe_v(1, 0, &kEmptyStructType);

  // Test struct.new and struct.get.
  const uint8_t kGet1 = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_STRUCT_GET(
           type_index, 0,
           WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64))),
       kExprEnd});

  // Test struct.new and struct.get.
  const uint8_t kGet2 = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_STRUCT_GET(
           type_index, 1,
           WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64))),
       kExprEnd});

  // Test struct.new, returning struct reference.
  const uint8_t kGetStruct = tester.DefineFunction(
      &sig_q_v, {},
      {WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64)), kExprEnd});

  const uint8_t kGetStructNominal = tester.DefineFunction(
      &sig_q_v, {},
      {WASM_STRUCT_NEW_DEFAULT(type_index), WASM_DROP,
       WASM_STRUCT_NEW(type_index, WASM_I32V(42), WASM_I32V(64)), kExprEnd});

  // Test struct.new, returning reference to an empty struct.
  const uint8_t kGetEmptyStruct = tester.DefineFunction(
      &sig_qe_v, {},
      {WASM_GC_OP(kExprStructNew), empty_struct_index, kExprEnd});

  // Test struct.set, struct refs types in locals.
  const uint8_t j_local_index = 0;
  const uint8_t j_field_index = 0;
  const uint8_t kSet = tester.DefineFunction(
      tester.sigs.i_v(), {kRefNullType},
      {WASM_LOCAL_SET(j_local_index, WASM_STRUCT_NEW(type_index, WASM_I32V(42),
                                                     WASM_I32V(64))),
       WASM_STRUCT_SET(type_index, j_field_index, WASM_LOCAL_GET(j_local_index),
                       WASM_I32V(-99)),
       WASM_STRUCT_GET(type_index, j_field_index,
                       WASM_LOCAL_GET(j_local_index)),
       kExprEnd});

  const uint8_t kNullDereference = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_STRUCT_GET(type_index, 0, WASM_REF_NULL(type_index)), kExprEnd});

  tester.CompileModule();

  tester.CheckResult(kGet1, 42);
  tester.CheckResult(kGet2, 64);
  CHECK(IsWasmStruct(*tester.GetResultObject(kGetStruct).ToHandleChecked()));
  CHECK(IsWasmStruct(
      *tester.GetResultObject(kGetStructNominal).ToHandleChecked()));
  CHECK(
      IsWasmStruct(*tester.GetResultObject(kGetEmptyStruct).ToHandleChecked()));
  tester.CheckResult(kSet, -99);
  tester.CheckHasThrown(kNullDereference);
}

// Test struct.get, ref.as_non_null and ref-typed globals.
WASM_COMPILED_EXEC_TEST(WasmRefAsNonNull) {
  WasmGCTester tester(execution_tier);
  const uint8_t type_index =
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)});
  ValueType kRefTypes[] = {ref(type_index)};
  ValueType kRefNullType = refNull(type_index);
  FunctionSig sig_q_v(1, 0, kRefTypes);

  const uint8_t global_index =
      tester.AddGlobal(kRefNullType, true,
                       WasmInitExpr::RefNullConst(
                           static_cast<HeapType::Representation>(type_index)));
  const uint8_t field_index = 0;
  const uint8_t kNonNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_GLOBAL_SET(global_index, WASM_STRUCT_NEW(type_index, WASM_I32V(55),
                                                     WASM_I32V(66))),
       WASM_STRUCT_GET(type_index, field_index,
                       WASM_REF_AS_NON_NULL(WASM_GLOBAL_GET(global_index))),
       kExprEnd});
  const uint8_t kNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_GLOBAL_SET(global_index, WASM_REF_NULL(type_index)),
       WASM_STRUCT_GET(type_index, field_index,
                       WASM_REF_AS_NON_NULL(WASM_GLOBAL_GET(global_index))),
       kExprEnd});

  tester.CompileModule();
  tester.CheckResult(kNonNull, 55);
  tester.CheckHasThrown(kNull);
}

WASM_COMPILED_EXEC_TEST(WasmRefAsNonNullSkipCheck) {
  FlagScope<bool> no_check(&v8_flags.experimental_wasm_skip_null_checks, true);
  WasmGCTester tester(execution_tier);
  const uint8_t type_index =
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)});
  ValueType kRefType = ref(type_index);
  FunctionSig sig_q_v(1, 0, &kRefType);

  const uint8_t global_index =
      tester.AddGlobal(refNull(type_index), true,
                       WasmInitExpr::RefNullConst(
                           static_cast<HeapType::Representation>(type_index)));
  const uint8_t kFunc = tester.DefineFunction(
      &sig_q_v, {},
      {WASM_GLOBAL_SET(global_index, WASM_REF_NULL(type_index)),
       WASM_REF_AS_NON_NULL(WASM_GLOBAL_GET(global_index)), kExprEnd});

  tester.CompileModule();
  DirectHandle<Object> result = tester.GetResultObject(kFunc).ToHandleChecked();
  // Without null checks, ref.as_non_null can actually return null.
  CHECK(IsWasmNull(*result));
}

WASM_COMPILED_EXEC_TEST(WasmBrOnNull) {
  WasmGCTester tester(execution_tier);
  const uint8_t type_index =
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)});
  ValueType kRefTypes[] = {ref(type_index)};
  ValueType kRefNullType = refNull(type_index);
  FunctionSig sig_q_v(1, 0, kRefTypes);
  const uint8_t local_index = 0;
  const uint8_t kTaken = tester.DefineFunction(
      tester.sigs.i_v(), {kRefNullType},
      {WASM_BLOCK_I(WASM_I32V(42),
                    // Branch will be taken.
                    // 42 left on stack outside the block (not 52).
                    WASM_BR_ON_NULL(0, WASM_LOCAL_GET(local_index)),
                    WASM_I32V(52), WASM_BR(0)),
       kExprEnd});

  const uint8_t field_index = 0;
  const uint8_t kNotTaken = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_BLOCK_I(
           WASM_I32V(42),
           WASM_STRUCT_GET(
               type_index, field_index,
               // Branch will not be taken.
               // 52 left on stack outside the block (not 42).
               WASM_BR_ON_NULL(0, WASM_STRUCT_NEW(type_index, WASM_I32V(52),
                                                  WASM_I32V(62)))),
           WASM_BR(0)),
       kExprEnd});

  tester.CompileModule();
  tester.CheckResult(kTaken, 42);
  tester.CheckResult(kNotTaken, 52);
}

WASM_COMPILED_EXEC_TEST(WasmBrOnNonNull) {
  WasmGCTester tester(execution_tier);
  const uint8_t type_index =
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)});
  ValueType kRefType = ref(type_index);
  ValueType kRefNullType = refNull(type_index);
  FunctionSig sig_q_v(1, 0, &kRefType);
  const uint8_t field_index = 0;

  const uint8_t kTaken = tester.DefineFunction(
      tester.sigs.i_v(), {kRefNullType, kRefNullType},
      {WASM_LOCAL_SET(
           0, WASM_STRUCT_NEW(type_index, WASM_I32V(52), WASM_I32V(62))),
       WASM_LOCAL_SET(
           1, WASM_STRUCT_NEW(type_index, WASM_I32V(11), WASM_I32V(22))),
       WASM_STRUCT_GET(type_index, field_index,
                       WASM_BLOCK_R(ref(type_index),
                                    // Branch will be taken, and the block will
                                    // return struct(52, 62).
                                    WASM_BR_ON_NON_NULL(0, WASM_LOCAL_GET(0)),
                                    WASM_REF_AS_NON_NULL(WASM_LOCAL_GET(1)))),
       kExprEnd});

  const uint8_t kNotTaken = tester.DefineFunction(
      tester.sigs.i_v(), {kRefNullType, kRefNullType},
      {WASM_LOCAL_SET(0, WASM_REF_NULL(type_index)),
       WASM_LOCAL_SET(
           1, WASM_STRUCT_NEW(type_index, WASM_I32V(11), WASM_I32V(22))),
       WASM_STRUCT_GET(type_index, field_index,
                       WASM_BLOCK_R(ref(type_index),
                                    // Branch will not be taken, and the block
                                    // will return struct(11, 22).
                                    WASM_BR_ON_NON_NULL(0, WASM_LOCAL_GET(0)),
                                    WASM_REF_AS_NON_NULL(WASM_LOCAL_GET(1)))),
       kExprEnd});
  tester.CompileModule();
  tester.CheckResult(kTaken, 52);
  tester.CheckResult(kNotTaken, 11);
}

WASM_COMPILED_EXEC_TEST(RefCast) {
  WasmGCTester tester(execution_tier);

  const uint8_t supertype_index = tester.DefineStruct({F(kWasmI32, true)});
  const uint8_t subtype1_index = tester.DefineStruct(
      {F(kWasmI32, true), F(kWasmF32, false)}, supertype_index);
  const uint8_t subtype2_index = tester.DefineStruct(
      {F(kWasmI32, true), F(kWasmI64, false)}, supertype_index);
  auto super_sig =
      FixedSizeSignature<ValueType>::Params(ValueType::RefNull(subtype1_index))
          .Returns(ValueType::RefNull(supertype_index));
  auto sub_sig1 =
      FixedSizeSignature<ValueType>::Params(ValueType::RefNull(supertype_index))
          .Returns(ValueType::RefNull(subtype1_index));
  auto sub_sig2 =
      FixedSizeSignature<ValueType>::Params(ValueType::RefNull(supertype_index))
          .Returns(ValueType::RefNull(subtype2_index));
  const uint8_t function_type_index = tester.DefineSignature(&super_sig);
  const uint8_t function_subtype1_index =
      tester.DefineSignature(&sub_sig1, function_type_index);
  const uint8_t function_subtype2_index =
      tester.DefineSignature(&sub_sig2, function_type_index);
  const uint8_t function_index = tester.DefineFunction(
      function_subtype1_index, {},
      {WASM_STRUCT_NEW(subtype1_index, WASM_I32V(10), WASM_F32(20)), WASM_END});
  // Just so this function counts as "declared".
  tester.AddGlobal(ValueType::RefNull(function_type_index), false,
                   WasmInitExpr::RefFuncConst(function_index));

  const uint8_t kTestSuccessful = tester.DefineFunction(
      tester.sigs.i_v(), {ValueType::RefNull(supertype_index)},
      {WASM_LOCAL_SET(
           0, WASM_STRUCT_NEW(subtype1_index, WASM_I32V(10), WASM_F32(20))),
       WASM_STRUCT_GET(subtype1_index, 0,
                       WASM_REF_CAST(WASM_LOCAL_GET(0), subtype1_index)),
       WASM_END});

  const uint8_t kTestFailed = tester.DefineFunction(
      tester.sigs.i_v(), {ValueType::RefNull(supertype_index)},
      {WASM_LOCAL_SET(
           0, WASM_STRUCT_NEW(subtype1_index, WASM_I32V(10), WASM_F32(20))),
       WASM_STRUCT_GET(subtype2_index, 0,
                       WASM_REF_CAST(WASM_LOCAL_GET(0), subtype2_index)),
       WASM_END});

  const uint8_t kFuncTestSuccessfulSuper = tester.DefineFunction(
      tester.sigs.i_v(), {ValueType::RefNull(function_type_index)},
      {WASM_LOCAL_SET(0, WASM_REF_FUNC(function_index)),
       WASM_REF_CAST(WASM_LOCAL_GET(0), function_type_index), WASM_DROP,
       WASM_I32V(0), WASM_END});

  const uint8_t kFuncTestSuccessfulSub = tester.DefineFunction(
      tester.sigs.i_v(), {ValueType::RefNull(function_type_index)},
      {WASM_LOCAL_SET(0, WASM_REF_FUNC(function_index)),
       WASM_REF_CAST(WASM_LOCAL_GET(0), function_subtype1_index), WASM_DROP,
       WASM_I32V(0), WASM_END});

  const uint8_t kFuncTestFailed = tester.DefineFunction(
      tester.sigs.i_v(), {ValueType::RefNull(function_type_index)},
      {WASM_LOCAL_SET(0, WASM_REF_FUNC(function_index)),
       WASM_REF_CAST(WASM_LOCAL_GET(0), function_subtype2_index), WASM_DROP,
       WASM_I32V(1), WASM_END});

  tester.CompileModule();
  tester.CheckResult(kTestSuccessful, 10);
  tester.CheckHasThrown(kTestFailed);
  tester.CheckResult(kFuncTestSuccessfulSuper, 0);
  tester.CheckResult(kFuncTestSuccessfulSub, 0);
  tester.CheckHasThrown(kFuncTestFailed);
}

WASM_COMPILED_EXEC_TEST(RefCastNoChecks) {
  FlagScope<bool> scope(&v8_flags.experimental_wasm_assume_ref_cast_succeeds,
                        true);
  WasmGCTester tester(execution_tier);

  const uint8_t supertype_index = tester.DefineStruct({F(kWasmI32, true)});
  const uint8_t subtype1_index = tester.DefineStruct(
      {F(kWasmI32, true), F(kWasmF32, true)}, supertype_index);

  const uint8_t kTestSuccessful = tester.DefineFunction(
      tester.sigs.i_v(), {ValueType::RefNull(supertype_index)},
      {WASM_LOCAL_SET(0, WASM_STRUCT_NEW_DEFAULT(subtype1_index)),
       WASM_STRUCT_GET(subtype1_index, 0,
                       WASM_REF_CAST(WASM_LOCAL_GET(0), subtype1_index)),
       WASM_END});

  tester.CompileModule();
  tester.CheckResult(kTestSuccessful, 0);
}

WASM_COMPILED_EXEC_TEST(BrOnCast) {
  WasmGCTester tester(execution_tier);
  const uint8_t type_index = tester.DefineStruct({F(kWasmI32, true)});
  const uint8_t other_type_index = tester.DefineStruct({F(kWasmF32, true)});

  const uint8_t kTestStructStatic = tester.DefineFunction(
      tester.sigs.i_v(), {kWasmI32, kWasmStructRef},
      {WASM_BLOCK_R(
           ValueType::RefNull(type_index), WASM_LOCAL_SET(0, WASM_I32V(111)),
           // Pipe a struct through a local so it's statically typed
           // as structref.
           WASM_LOCAL_SET(1, WASM_STRUCT_NEW(other_type_index, WASM_F32(1.0))),
           WASM_LOCAL_GET(1),
           // The type check fails, so this branch isn't taken.
           WASM_BR_ON_CAST(0, kStructRefCode, type_index), WASM_DROP,

           WASM_LOCAL_SET(0, WASM_I32V(221)),  // (Final result) - 1
           WASM_LOCAL_SET(1, WASM_STRUCT_NEW(type_index, WASM_I32V(1))),
           WASM_LOCAL_GET(1),
           // This branch is taken.
           WASM_BR_ON_CAST(0, kStructRefCode, type_index),
           WASM_GC_OP(kExprRefCast), type_index,

           // Not executed due to the branch.
           WASM_LOCAL_SET(0, WASM_I32V(333))),
       WASM_GC_OP(kExprStructGet), type_index, 0, WASM_LOCAL_GET(0),
       kExprI32Add, kExprEnd});

  const uint8_t kTestStructStaticNull = tester.DefineFunction(
      tester.sigs.i_v(), {kWasmI32, kWasmStructRef},
      {WASM_BLOCK_R(
           ValueType::RefNull(type_index), WASM_LOCAL_SET(0, WASM_I32V(111)),
           // Pipe a struct through a local so it's statically typed as
           // structref.
           WASM_LOCAL_SET(1, WASM_STRUCT_NEW(other_type_index, WASM_F32(1.0))),
           WASM_LOCAL_GET(1),
           // The type check fails, so this branch isn't taken.
           WASM_BR_ON_CAST(0, kStructRefCode, type_index), WASM_DROP,

           WASM_LOCAL_SET(0, WASM_I32V(221)),  // (Final result) - 1
           WASM_LOCAL_SET(1, WASM_STRUCT_NEW(type_index, WASM_I32V(1))),
           WASM_LOCAL_GET(1),
           // This branch is taken.
           WASM_BR_ON_CAST_NULL(0, kStructRefCode, type_index),
           WASM_GC_OP(kExprRefCast), type_index,

           // Not executed due to the branch.
           WASM_LOCAL_SET(0, WASM_I32V(333))),
       WASM_GC_OP(kExprStructGet), type_index, 0, WASM_LOCAL_GET(0),
       kExprI32Add, kExprEnd});

  const uint8_t kTestNull = tester.DefineFunction(
      tester.sigs.i_v(), {kWasmI32, kWasmStructRef},
      {WASM_BLOCK_R(ValueType::RefNull(type_index),
                    WASM_LOCAL_SET(0, WASM_I32V(111)),
                    WASM_LOCAL_GET(1),  // Put a nullref onto the value stack.
                    // Not taken for nullref.
                    WASM_BR_ON_CAST(0, kStructRefCode, type_index),
                    WASM_GC_OP(kExprRefCast),
                    type_index),  // Traps
       WASM_DROP, WASM_LOCAL_GET(0), kExprEnd});

  // "br_on_cast null" also branches on null, treating it as a successful cast.
  const uint8_t kTestNullNull = tester.DefineFunction(
      tester.sigs.i_v(), {kWasmI32, kWasmStructRef},
      {WASM_BLOCK_R(ValueType::RefNull(type_index),
                    WASM_LOCAL_SET(0, WASM_I32V(111)),
                    WASM_LOCAL_GET(1),  // Put a nullref onto the value stack.
                    // Taken for nullref with br_on_cast null.
                    WASM_BR_ON_CAST_NULL(0, kStructRefCode, type_index),
                    WASM_GC_OP(kExprRefCast), type_index),
       WASM_DROP, WASM_LOCAL_GET(0), kExprEnd});

  const uint8_t kTypedAfterBranch = tester.DefineFunction(
      tester.sigs.i_v(), {kWasmI32, kWasmStructRef},
      {WASM_LOCAL_SET(1, WASM_STRUCT_NEW(type_index, WASM_I32V(42))),
       WASM_BLOCK_I(
           // The inner block should take the early branch with a struct
           // on the stack.
           WASM_BLOCK_R(ValueType::Ref(type_index), WASM_LOCAL_GET(1),
                        WASM_BR_ON_CAST(0, kStructRefCode, type_index),
                        // Returning 123 is the unreachable failure case.
                        WASM_I32V(123), WASM_BR(1)),
           // The outer block catches the struct left behind by the inner block
           // and reads its field.
           WASM_GC_OP(kExprStructGet), type_index, 0),
       kExprEnd});

  tester.CompileModule();
  tester.CheckResult(kTestStructStatic, 222);
  tester.CheckResult(kTestStructStaticNull, 222);
  tester.CheckHasThrown(kTestNull);
  tester.CheckResult(kTestNullNull, 111);
  tester.CheckResult(kTypedAfterBranch, 42);
}

WASM_COMPILED_EXEC_TEST(BrOnCastFail) {
  WasmGCTester tester(execution_tier);
  const uint8_t type0 = tester.DefineStruct({F(kWasmI32, true)});
  const uint8_t type1 =
      tester.DefineStruct({F(kWasmI64, true), F(kWasmI32, true)});

  const int field0_value = 5;
  const int field1_value = 25;
  const int null_value = 45;

  //  local_0 = value;
  //  if (!(local_0 instanceof type0)) goto block1;
  //  return static_cast<type0>(local_0).field_0;
  // block1:
  //  if (local_0 == nullptr) goto block2;
  //  return static_cast<type1>(local_0).field_1;
  // block2:
  //  return null_value;
#define FUNCTION_BODY(value)                                               \
  WASM_LOCAL_SET(0, WASM_SEQ(value)),                                      \
      WASM_BLOCK(                                                          \
          WASM_BLOCK_R(kWasmStructRef, WASM_LOCAL_GET(0),                  \
                       WASM_BR_ON_CAST_FAIL(0, kStructRefCode, type0),     \
                       WASM_GC_OP(kExprStructGet), type0, 0, kExprReturn), \
          kExprBrOnNull, 0, WASM_GC_OP(kExprRefCast), type1,               \
          WASM_GC_OP(kExprStructGet), type1, 1, kExprReturn),              \
      WASM_I32V(null_value), kExprEnd

  const uint8_t kBranchTaken = tester.DefineFunction(
      tester.sigs.i_v(), {kWasmStructRef},
      {FUNCTION_BODY(
          WASM_STRUCT_NEW(type1, WASM_I64V(10), WASM_I32V(field1_value)))});

  const uint8_t kBranchNotTaken = tester.DefineFunction(
      tester.sigs.i_v(), {kWasmStructRef},
      {FUNCTION_BODY(WASM_STRUCT_NEW(type0, WASM_I32V(field0_value)))});

  const uint8_t kNull =
      tester.DefineFunction(tester.sigs.i_v(), {kWasmStructRef},
                            {FUNCTION_BODY(WASM_REF_NULL(type0))});

  const uint8_t kUnrelatedTypes = tester.DefineFunction(
      tester.sigs.i_v(), {ValueType::RefNull(type1)},
      {FUNCTION_BODY(
          WASM_STRUCT_NEW(type1, WASM_I64V(10), WASM_I32V(field1_value)))});
#undef FUNCTION_BODY

  const uint8_t kBranchTakenStatic = tester.DefineFunction(
      tester.sigs.i_v(), {kWasmStructRef},
      {WASM_LOCAL_SET(
           0, WASM_STRUCT_NEW(type1, WASM_I64V(10), WASM_I32V(field1_value))),
       WASM_BLOCK(
           WASM_BLOCK_R(kWasmStructRef, WASM_LOCAL_GET(0),
                        WASM_BR_ON_CAST_FAIL(0, kStructRefCode, type0),
                        WASM_GC_OP(kExprStructGet), type0, 0, kExprReturn),
           kExprBrOnNull, 0, WASM_GC_OP(kExprRefCast), type1,
           WASM_GC_OP(kExprStructGet), type1, 1, kExprReturn),
       WASM_I32V(null_value), kExprEnd});

  tester.CompileModule();
  tester.CheckResult(kBranchTaken, field1_value);
  tester.CheckResult(kBranchTakenStatic, field1_value);
  tester.CheckResult(kBranchNotTaken, field0_value);
  tester.CheckResult(kNull, null_value);
  tester.CheckResult(kUnrelatedTypes, field1_value);
}

WASM_COMPILED_EXEC_TEST(WasmRefEq) {
  WasmGCTester tester(execution_tier);
  uint8_t type_index =
      tester.DefineStruct({F(kWasmI32, true), F(kWasmI32, true)});
  ValueType kRefTypes[] = {ref(type_index)};
  ValueType kRefNullType = refNull(type_index);
  FunctionSig sig_q_v(1, 0, kRefTypes);

  uint8_t local_index = 0;
  const uint8_t kFunc = tester.DefineFunction(
      tester.sigs.i_v(), {kRefNullType},
      {WASM_LOCAL_SET(local_index, WASM_STRUCT_NEW(type_index, WASM_I32V(55),
                                                   WASM_I32V(66))),
       WASM_I32_ADD(
           WASM_I32_SHL(
               WASM_REF_EQ(  // true
                   WASM_LOCAL_GET(local_index), WASM_LOCAL_GET(local_index)),
               WASM_I32V(0)),
           WASM_I32_ADD(
               WASM_I32_SHL(WASM_REF_EQ(  // false
                                WASM_LOCAL_GET(local_index),
                                WASM_STRUCT_NEW(type_index, WASM_I32V(55),
                                                WASM_I32V(66))),
                            WASM_I32V(1)),
               WASM_I32_ADD(WASM_I32_SHL(  // false
                                WASM_REF_EQ(WASM_LOCAL_GET(local_index),
                                            WASM_REF_NULL(type_index)),
                                WASM_I32V(2)),
                            WASM_I32_SHL(WASM_REF_EQ(  // true
                                             WASM_REF_NULL(type_index),
                                             WASM_REF_NULL(type_index)),
                                         WASM_I32V(3))))),
       kExprEnd});

  tester.CompileModule();
  tester.CheckResult(kFunc, 0b1001);
}

WASM_COMPILED_EXEC_TEST(WasmPackedStructU) {
  WasmGCTester tester(execution_tier);

  const uint8_t type_index = tester.DefineStruct(
      {F(kWasmI8, true), F(kWasmI16, true), F(kWasmI32, true)});
  ValueType struct_type = refNull(type_index);

  const uint8_t local_index = 0;

  int32_t expected_output_0 = 0x1234;
  int32_t expected_output_1 = -1;

  const uint8_t kF0 = tester.DefineFunction(
      tester.sigs.i_v(), {struct_type},
      {WASM_LOCAL_SET(local_index,
                      WASM_STRUCT_NEW(type_index, WASM_I32V(expected_output_0),
                                      WASM_I32V(expected_output_1),
                                      WASM_I32V(0x12345678))),
       WASM_STRUCT_GET_U(type_index, 0, WASM_LOCAL_GET(local_index)),
       kExprEnd});

  const uint8_t kF1 = tester.DefineFunction(
      tester.sigs.i_v(), {struct_type},
      {WASM_LOCAL_SET(local_index,
                      WASM_STRUCT_NEW(type_index, WASM_I32V(expected_output_0),
                                      WASM_I32V(expected_output_1),
                                      WASM_I32V(0x12345678))),
       WASM_STRUCT_GET_U(type_index, 1, WASM_LOCAL_GET(local_index)),
       kExprEnd});
  tester.CompileModule();

  tester.CheckResult(kF0, static_cast<uint8_t>(expected_output_0));
  tester.CheckResult(kF1, static_cast<uint16_t>(expected_output_1));
}

WASM_COMPILED_EXEC_TEST(WasmPackedStructS) {
  WasmGCTester tester(execution_tier);

  const uint8_t type_index = tester.DefineStruct(
      {F(kWasmI8, true), F(kWasmI16, true), F(kWasmI32, true)});
  ValueType struct_type = refNull(type_index);

  const uint8_t local_index = 0;

  int32_t expected_output_0 = 0x80;
  int32_t expected_output_1 = 42;

  const uint8_t kF0 = tester.DefineFunction(
      tester.sigs.i_v(), {struct_type},
      {WASM_LOCAL_SET(
           local_index,
           WASM_STRUCT_NEW(type_index, WASM_I32V(expected_output_0),
                           WASM_I32V(expected_output_1), WASM_I32V(0))),
       WASM_STRUCT_GET_S(type_index, 0, WASM_LOCAL_GET(local_index)),
       kExprEnd});

  const uint8_t kF1 = tester.DefineFunction(
      tester.sigs.i_v(), {struct_type},
      {WASM_LOCAL_SET(local_index, WASM_STRUCT_NEW(type_index, WASM_I32V(0x80),
                                                   WASM_I32V(expected_output_1),
                                                   WASM_I32V(0))),
       WASM_STRUCT_GET_S(type_index, 1, WASM_LOCAL_GET(local_index)),
       kExprEnd});

  tester.CompileModule();

  tester.CheckResult(kF0, static_cast<int8_t>(expected_output_0));
  tester.CheckResult(kF1, static_cast<int16_t>(expected_output_1));
}

WASM_COMPILED_EXEC_TEST(WasmBasicArray) {
  WasmGCTester tester(execution_tier);
  if (!tester.HasSimdSupport(execution_tier)) return;

  const uint8_t type_index = tester.DefineArray(wasm::kWasmI32, true);
  const uint8_t fp_type_index = tester.DefineArray(wasm::kWasmF64, true);
  const uint8_t immut_type_index = tester.DefineArray(wasm::kWasmI32, false);
  ValueType kRefTypes[] = {ref(type_index)};
  FunctionSig sig_q_v(1, 0, kRefTypes);
  ValueType kRefNullType = refNull(type_index);

  // f: a = [12, 12, 12]; a[1] = 42; return a[arg0]
  const uint8_t local_index = 1;
  const uint8_t kGetElem = tester.DefineFunction(
      tester.sigs.i_i(), {kRefNullType},
      {WASM_LOCAL_SET(local_index,
                      WASM_ARRAY_NEW(type_index, WASM_I32V(12), WASM_I32V(3))),
       WASM_ARRAY_SET(type_index, WASM_LOCAL_GET(local_index), WASM_I32V(1),
                      WASM_I32V(42)),
       WASM_ARRAY_GET(type_index, WASM_LOCAL_GET(local_index),
                      WASM_LOCAL_GET(0)),
       kExprEnd});

  // Reads and returns an array's length.
  const uint8_t kGetLength = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_ARRAY_LEN(WASM_ARRAY_NEW(type_index, WASM_I32V(0), WASM_I32V(42))),
       kExprEnd});

  // Create an array of length 2, initialized to [42, 42].
  const uint8_t kAllocate = tester.DefineFunction(
      &sig_q_v, {},
      {WASM_ARRAY_NEW(type_index, WASM_I32V(42), WASM_I32V(2)), kExprEnd});

  const uint8_t kAllocateStatic = tester.DefineFunction(
      &sig_q_v, {},
      {WASM_ARRAY_NEW_DEFAULT(type_index, WASM_I32V(2)), WASM_DROP,
       WASM_ARRAY_NEW(type_index, WASM_I32V(42), WASM_I32V(2)), kExprEnd});

  const uint8_t kInit =
      tester.DefineFunction(&sig_q_v, {},
                            {WASM_ARRAY_NEW_FIXED(type_index, 3, WASM_I32V(10),
                                                  WASM_I32V(20), WASM_I32V(30)),
                             kExprEnd});

  const uint8_t kImmutable = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_ARRAY_GET(immut_type_index,
                      WASM_ARRAY_NEW_FIXED(immut_type_index, 2, WASM_I32V(42),
                                           WASM_I32V(43)),
                      WASM_I32V(0)),
       kExprEnd});

  const uint32_t kLongLength = 1u << 16;
  const uint8_t kAllocateLarge = tester.DefineFunction(
      &sig_q_v, {},
      {WASM_ARRAY_NEW_DEFAULT(type_index, WASM_I32V(kLongLength)), kExprEnd});

  ArrayType array_type(kWasmI32, true);
  const uint32_t kTooLong = WasmArray::MaxLength(&array_type) + 1;
  const uint8_t kAllocateTooLarge = tester.DefineFunction(
      &sig_q_v, {},
      {WASM_ARRAY_NEW_DEFAULT(type_index, WASM_I32V(kTooLong)), kExprEnd});

  // Tests that fp arrays work properly.
  // f: a = [10.0, 10.0, 10.0]; a[1] = 42.42; return static_cast<int64>(a[1]);
  double result_value = 42.42;
  const uint8_t kTestFpArray = tester.DefineFunction(
      tester.sigs.i_v(), {refNull(fp_type_index)},
      {WASM_LOCAL_SET(
           0, WASM_ARRAY_NEW(fp_type_index, WASM_F64(10.0), WASM_I32V(3))),
       WASM_ARRAY_SET(fp_type_index, WASM_LOCAL_GET(0), WASM_I32V(1),
                      WASM_F64(result_value)),
       WASM_I32_SCONVERT_F64(
           WASM_ARRAY_GET(fp_type_index, WASM_LOCAL_GET(0), WASM_I32V(1))),
       kExprEnd});

  tester.CompileModule();

  tester.CheckResult(kGetElem, 12, 0);
  tester.CheckResult(kGetElem, 42, 1);
  tester.CheckResult(kGetElem, 12, 2);
  tester.CheckHasThrown(kGetElem, 3);
  tester.CheckHasThrown(kGetElem, -1);
  tester.CheckResult(kGetLength, 42);
  tester.CheckResult(kImmutable, 42);
  tester.CheckResult(kTestFpArray, static_cast<int32_t>(result_value));

  Handle<Object> h_result = tester.GetResultObject(kAllocate).ToHandleChecked();
  CHECK(IsWasmArray(*h_result));
  CHECK_EQ(2, Cast<WasmArray>(h_result)->length());

  h_result = tester.GetResultObject(kAllocateStatic).ToHandleChecked();
  CHECK(IsWasmArray(*h_result));
  CHECK_EQ(2, Cast<WasmArray>(h_result)->length());

  Handle<Object> init_result = tester.GetResultObject(kInit).ToHandleChecked();
  CHECK(IsWasmArray(*init_result));
  CHECK_EQ(3, Cast<WasmArray>(init_result)->length());
  CHECK_EQ(10, Cast<WasmArray>(init_result)->GetElement(0).to_i32());
  CHECK_EQ(20, Cast<WasmArray>(init_result)->GetElement(1).to_i32());
  CHECK_EQ(30, Cast<WasmArray>(init_result)->GetElement(2).to_i32());

  MaybeHandle<Object> maybe_large_result =
      tester.GetResultObject(kAllocateLarge);
  Handle<Object> large_result = maybe_large_result.ToHandleChecked();
  CHECK(IsWasmArray(*large_result));
  CHECK(Cast<WasmArray>(large_result)->Size() > kMaxRegularHeapObjectSize);

  tester.CheckHasThrown(kAllocateTooLarge, "requested new array is too large");
}

WASM_COMPILED_EXEC_TEST(WasmPackedArrayU) {
  WasmGCTester tester(execution_tier);
  const uint8_t array_index = tester.DefineArray(kWasmI8, true);
  ValueType array_type = refNull(array_index);

  const uint8_t param_index = 0;
  const uint8_t local_index = 1;

  int32_t expected_output_3 = 258;

  const uint8_t kF = tester.DefineFunction(
      tester.sigs.i_i(), {array_type},
      {WASM_LOCAL_SET(local_index,
                      WASM_ARRAY_NEW(array_index, WASM_I32V(0), WASM_I32V(4))),
       WASM_ARRAY_SET(array_index, WASM_LOCAL_GET(local_index), WASM_I32V(0),
                      WASM_I32V(1)),
       WASM_ARRAY_SET(array_index, WASM_LOCAL_GET(local_index), WASM_I32V(1),
                      WASM_I32V(10)),
       WASM_ARRAY_SET(array_index, WASM_LOCAL_GET(local_index), WASM_I32V(2),
                      WASM_I32V(200)),
       WASM_ARRAY_SET(array_index, WASM_LOCAL_GET(local_index), WASM_I32V(3),
                      WASM_I32V(expected_output_3)),
       WASM_ARRAY_GET_U(array_index, WASM_LOCAL_GET(local_index),
                        WASM_LOCAL_GET(param_index)),
       kExprEnd});

  tester.CompileModule();
  tester.CheckResult(kF, 1, 0);
  tester.CheckResult(kF, 10, 1);
  tester.CheckResult(kF, 200, 2);
  // Only the 2 lsb's of 258 should be stored in the array.
  tester.CheckResult(kF, static_cast<uint8_t>(expected_output_3), 3);
}

WASM_COMPILED_EXEC_TEST(WasmPackedArrayS) {
  WasmGCTester tester(execution_tier);
  const uint8_t array_index = tester.DefineArray(kWasmI16, true);
  ValueType array_type = refNull(array_index);

  int32_t array_elements[] = {0x12345678, 10, 0xFEDC, 0xFF1234};

  const uint8_t param_index = 0;
  const uint8_t local_index = 1;
  const uint8_t kF = tester.DefineFunction(
      tester.sigs.i_i(), {array_type},
      {WASM_LOCAL_SET(local_index,
                      WASM_ARRAY_NEW(array_index, WASM_I32V(array_elements[0]),
                                     WASM_I32V(4))),
       WASM_ARRAY_SET(array_index, WASM_LOCAL_GET(local_index), WASM_I32V(1),
                      WASM_I32V(array_elements[1])),
       WASM_ARRAY_SET(array_index, WASM_LOCAL_GET(local_index), WASM_I32V(2),
                      WASM_I32V(array_elements[2])),
       WASM_ARRAY_SET(array_index, WASM_LOCAL_GET(local_index), WASM_I32V(3),
                      WASM_I32V(array_elements[3])),
       WASM_ARRAY_GET_S(array_index, WASM_LOCAL_GET(local_index),
                        WASM_LOCAL_GET(param_index)),
       kExprEnd});

  tester.CompileModule();
  // Exactly the 2 lsb's should be stored by array.new.
  tester.CheckResult(kF, static_cast<int16_t>(array_elements[0]), 0);
  tester.CheckResult(kF, static_cast<int16_t>(array_elements[1]), 1);
  // Sign should be extended.
  tester.CheckResult(kF, static_cast<int16_t>(array_elements[2]), 2);
  // Exactly the 2 lsb's should be stored by array.set.
  tester.CheckResult(kF, static_cast<int16_t>(array_elements[3]), 3);
}

WASM_COMPILED_EXEC_TEST(WasmArrayCopy) {
  WasmGCTester tester(execution_tier);
  const uint8_t array32_index = tester.DefineArray(kWasmI32, true);
  const uint8_t array16_index = tester.DefineArray(kWasmI16, true);
  const uint8_t arrayref_index =
      tester.DefineArray(refNull(array32_index), true);

  // Copies i32 ranges: local1[0..3] to local2[6..9].
  const uint8_t kCopyI32 = tester.DefineFunction(
      tester.sigs.i_i(), {refNull(array32_index), refNull(array32_index)},
      {WASM_LOCAL_SET(1, WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(10))),
       WASM_ARRAY_SET(array32_index, WASM_LOCAL_GET(1), WASM_I32V(0),
                      WASM_I32V(0)),
       WASM_ARRAY_SET(array32_index, WASM_LOCAL_GET(1), WASM_I32V(1),
                      WASM_I32V(1)),
       WASM_ARRAY_SET(array32_index, WASM_LOCAL_GET(1), WASM_I32V(2),
                      WASM_I32V(2)),
       WASM_ARRAY_SET(array32_index, WASM_LOCAL_GET(1), WASM_I32V(3),
                      WASM_I32V(3)),
       WASM_LOCAL_SET(2, WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(10))),
       WASM_ARRAY_COPY(array32_index, array32_index, WASM_LOCAL_GET(2),
                       WASM_I32V(6), WASM_LOCAL_GET(1), WASM_I32V(0),
                       WASM_I32V(4)),
       WASM_ARRAY_GET(array32_index, WASM_LOCAL_GET(2), WASM_LOCAL_GET(0)),
       kExprEnd});

  // Copies i16 ranges: local1[0..3] to local2[6..9].
  const uint8_t kCopyI16 = tester.DefineFunction(
      tester.sigs.i_i(), {refNull(array16_index), refNull(array16_index)},
      {WASM_LOCAL_SET(1, WASM_ARRAY_NEW_DEFAULT(array16_index, WASM_I32V(10))),
       WASM_ARRAY_SET(array16_index, WASM_LOCAL_GET(1), WASM_I32V(0),
                      WASM_I32V(0)),
       WASM_ARRAY_SET(array16_index, WASM_LOCAL_GET(1), WASM_I32V(1),
                      WASM_I32V(1)),
       WASM_ARRAY_SET(array16_index, WASM_LOCAL_GET(1), WASM_I32V(2),
                      WASM_I32V(2)),
       WASM_ARRAY_SET(array16_index, WASM_LOCAL_GET(1), WASM_I32V(3),
                      WASM_I32V(3)),
       WASM_LOCAL_SET(2, WASM_ARRAY_NEW_DEFAULT(array16_index, WASM_I32V(10))),
       WASM_ARRAY_COPY(array16_index, array16_index, WASM_LOCAL_GET(2),
                       WASM_I32V(6), WASM_LOCAL_GET(1), WASM_I32V(0),
                       WASM_I32V(4)),
       WASM_ARRAY_GET_S(array16_index, WASM_LOCAL_GET(2), WASM_LOCAL_GET(0)),
       kExprEnd});

  // Copies reference ranges: local1[0..3] to local2[6..9].
  const uint8_t kCopyRef = tester.DefineFunction(
      FunctionSig::Build(tester.zone(), {refNull(array32_index)}, {kWasmI32}),
      {refNull(arrayref_index), refNull(arrayref_index)},
      {WASM_LOCAL_SET(1, WASM_ARRAY_NEW_DEFAULT(arrayref_index, WASM_I32V(10))),
       WASM_ARRAY_SET(arrayref_index, WASM_LOCAL_GET(1), WASM_I32V(0),
                      WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(6))),
       WASM_ARRAY_SET(arrayref_index, WASM_LOCAL_GET(1), WASM_I32V(1),
                      WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(7))),
       WASM_ARRAY_SET(arrayref_index, WASM_LOCAL_GET(1), WASM_I32V(2),
                      WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(8))),
       WASM_ARRAY_SET(arrayref_index, WASM_LOCAL_GET(1), WASM_I32V(3),
                      WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(9))),
       WASM_LOCAL_SET(2, WASM_ARRAY_NEW_DEFAULT(arrayref_index, WASM_I32V(10))),
       WASM_ARRAY_COPY(arrayref_index, arrayref_index, WASM_LOCAL_GET(2),
                       WASM_I32V(6), WASM_LOCAL_GET(1), WASM_I32V(0),
                       WASM_I32V(4)),
       WASM_ARRAY_GET(arrayref_index, WASM_LOCAL_GET(2), WASM_LOCAL_GET(0)),
       kExprEnd});

  // Copies overlapping reference ranges: local1[0..3] to local1[2..5].
  const uint8_t kCopyRefOverlapping = tester.DefineFunction(
      FunctionSig::Build(tester.zone(), {refNull(array32_index)}, {kWasmI32}),
      {refNull(arrayref_index)},
      {WASM_LOCAL_SET(1, WASM_ARRAY_NEW_DEFAULT(arrayref_index, WASM_I32V(10))),
       WASM_ARRAY_SET(arrayref_index, WASM_LOCAL_GET(1), WASM_I32V(0),
                      WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(2))),
       WASM_ARRAY_SET(arrayref_index, WASM_LOCAL_GET(1), WASM_I32V(1),
                      WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(3))),
       WASM_ARRAY_SET(arrayref_index, WASM_LOCAL_GET(1), WASM_I32V(2),
                      WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(4))),
       WASM_ARRAY_SET(arrayref_index, WASM_LOCAL_GET(1), WASM_I32V(3),
                      WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(5))),
       WASM_ARRAY_COPY(arrayref_index, arrayref_index, WASM_LOCAL_GET(1),
                       WASM_I32V(2), WASM_LOCAL_GET(1), WASM_I32V(0),
                       WASM_I32V(4)),
       WASM_ARRAY_GET(arrayref_index, WASM_LOCAL_GET(1), WASM_LOCAL_GET(0)),
       kExprEnd});

  const uint8_t kOobSource = tester.DefineFunction(
      tester.sigs.v_v(), {refNull(array32_index), refNull(array32_index)},
      {WASM_LOCAL_SET(0, WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(10))),
       WASM_LOCAL_SET(1, WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(10))),
       WASM_ARRAY_COPY(array32_index, array32_index, WASM_LOCAL_GET(1),
                       WASM_I32V(6), WASM_LOCAL_GET(0), WASM_I32V(8),
                       WASM_I32V(4)),
       kExprEnd});

  const uint8_t kOobDestination = tester.DefineFunction(
      tester.sigs.v_v(), {refNull(array32_index), refNull(array32_index)},
      {WASM_LOCAL_SET(0, WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(10))),
       WASM_LOCAL_SET(1, WASM_ARRAY_NEW_DEFAULT(array32_index, WASM_I32V(10))),
       WASM_ARRAY_COPY(array32_index, array32_index, WASM_LOCAL_GET(1),
                       WASM_I32V(6), WASM_LOCAL_GET(0), WASM_I32V(3),
                       WASM_I32V(5)),
       kExprEnd});

  const uint8_t kZeroLength = tester.DefineFunction(
      tester.sigs.i_v(), {refNull(arrayref_index), refNull(arrayref_index)},
      {WASM_LOCAL_SET(0, WASM_ARRAY_NEW_DEFAULT(arrayref_index, WASM_I32V(10))),
       WASM_LOCAL_SET(1, WASM_ARRAY_NEW_DEFAULT(arrayref_index, WASM_I32V(10))),
       WASM_ARRAY_COPY(arrayref_index, arrayref_index, WASM_LOCAL_GET(1),
                       WASM_I32V(6), WASM_LOCAL_GET(0), WASM_I32V(3),
                       WASM_I32V(0)),
       WASM_I32V(0), kExprEnd});

  tester.CompileModule();

  tester.CheckResult(kCopyI32, 0, 5);
  tester.CheckResult(kCopyI32, 0, 6);
  tester.CheckResult(kCopyI32, 1, 7);
  tester.CheckResult(kCopyI32, 2, 8);
  tester.CheckResult(kCopyI32, 3, 9);

  tester.CheckResult(kCopyI16, 0, 5);
  tester.CheckResult(kCopyI16, 0, 6);
  tester.CheckResult(kCopyI16, 1, 7);
  tester.CheckResult(kCopyI16, 2, 8);
  tester.CheckResult(kCopyI16, 3, 9);

  {
    DirectHandle<Object> result5 =
        tester.GetResultObject(kCopyRef, 5).ToHandleChecked();
    CHECK(IsWasmNull(*result5));
    for (int i = 6; i <= 9; i++) {
      Handle<Object> res =
          tester.GetResultObject(kCopyRef, i).ToHandleChecked();
      CHECK(IsWasmArray(*res));
      CHECK_EQ(Cast<WasmArray>(res)->length(), static_cast<uint32_t>(i));
    }
  }
  CHECK(IsWasmNull(
      *tester.GetResultObject(kCopyRefOverlapping, 6).ToHandleChecked()));
  Handle<Object> res0 =
      tester.GetResultObject(kCopyRefOverlapping, 0).ToHandleChecked();
  CHECK(IsWasmArray(*res0));
  CHECK_EQ(Cast<WasmArray>(res0)->length(), static_cast<uint32_t>(2));
  for (int i = 2; i <= 5; i++) {
    Handle<Object> res =
        tester.GetResultObject(kCopyRefOverlapping, i).ToHandleChecked();
    CHECK(IsWasmArray(*res));
    CHECK_EQ(Cast<WasmArray>(res)->length(), static_cast<uint32_t>(i));
  }

  tester.CheckHasThrown(kOobSource);
  tester.CheckHasThrown(kOobDestination);
  tester.CheckResult(kZeroLength, 0);  // Does not throw.
}

WASM_COMPILED_EXEC_TEST(NewDefault) {
  WasmGCTester tester(execution_tier);
  if (!tester.HasSimdSupport(execution_tier)) return;

  tester.builder()->StartRecursiveTypeGroup();
  const uint8_t struct_type = tester.DefineStruct(
      {F(wasm::kWasmI32, true), F(wasm::kWasmF64, true), F(refNull(0), true)});
  tester.builder()->EndRecursiveTypeGroup();

  const uint8_t array_type = tester.DefineArray(wasm::kWasmI32, true);
  // Returns: struct[0] + f64_to_i32(struct[1]) + (struct[2].is_null ^ 1) == 0.
  const uint8_t allocate_struct = tester.DefineFunction(
      tester.sigs.i_v(), {refNull(struct_type)},
      {WASM_LOCAL_SET(0, WASM_STRUCT_NEW_DEFAULT(struct_type)),
       WASM_I32_ADD(
           WASM_I32_ADD(WASM_STRUCT_GET(struct_type, 0, WASM_LOCAL_GET(0)),
                        WASM_I32_SCONVERT_F64(WASM_STRUCT_GET(
                            struct_type, 1, WASM_LOCAL_GET(0)))),
           WASM_I32_XOR(WASM_REF_IS_NULL(
                            WASM_STRUCT_GET(struct_type, 2, WASM_LOCAL_GET(0))),
                        WASM_I32V(1))),
       kExprEnd});
  const uint8_t allocate_array = tester.DefineFunction(
      tester.sigs.i_v(), {refNull(array_type)},
      {WASM_LOCAL_SET(0, WASM_ARRAY_NEW_DEFAULT(array_type, WASM_I32V(2))),
       WASM_I32_ADD(
           WASM_ARRAY_GET(array_type, WASM_LOCAL_GET(0), WASM_I32V(0)),
           WASM_ARRAY_GET(array_type, WASM_LOCAL_GET(0), WASM_I32V(1))),
       kExprEnd});

  tester.CompileModule();

  tester.CheckResult(allocate_struct, 0);
  tester.CheckResult(allocate_array, 0);
}

WASM_COMPILED_EXEC_TEST(RefTrivialCastsStatic) {
  WasmGCTester tester(execution_tier);
  uint8_t type_index = tester.DefineStruct({F(wasm::kWasmI32, true)});
  uint8_t subtype_index = tester.DefineStruct(
      {F(wasm::kWasmI32, true), F(wasm::kWasmS128, true)}, type_index);
  uint8_t subtype2_index = tester.DefineStruct(
      {F(wasm::kWasmI32, true), F(wasm::kWasmF64, true)}, type_index);

  const uint8_t kRefTestNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(WASM_REF_NULL(type_index), subtype_index), kExprEnd});
  // Upcasts should be optimized away for nominal types.
  const uint8_t kRefTestUpcast = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(WASM_STRUCT_NEW_DEFAULT(subtype_index), type_index),
       kExprEnd});
  const uint8_t kRefTestUpcastNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(WASM_REF_NULL(subtype_index), type_index), kExprEnd});
  const uint8_t kRefTestUnrelatedNullable = tester.DefineFunction(
      tester.sigs.i_v(), {refNull(subtype_index)},
      {WASM_LOCAL_SET(0, WASM_STRUCT_NEW_DEFAULT(subtype_index)),
       WASM_REF_TEST(WASM_LOCAL_GET(0), subtype2_index), kExprEnd});
  const uint8_t kRefTestUnrelatedNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(WASM_REF_NULL(subtype_index), subtype2_index), kExprEnd});
  const uint8_t kRefTestUnrelatedNonNullable = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(WASM_STRUCT_NEW_DEFAULT(type_index), subtype2_index),
       kExprEnd});

  const uint8_t kRefCastNull =
      tester.DefineFunction(tester.sigs.i_v(), {},
                            {WASM_REF_IS_NULL(WASM_REF_CAST(
                                 WASM_REF_NULL(type_index), subtype_index)),
                             kExprEnd});
  const uint8_t kRefCastUpcast = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_IS_NULL(
           WASM_REF_CAST(WASM_STRUCT_NEW_DEFAULT(subtype_index), type_index)),
       kExprEnd});
  const uint8_t kRefCastUpcastNull =
      tester.DefineFunction(tester.sigs.i_v(), {},
                            {WASM_REF_IS_NULL(WASM_REF_CAST(
                                 WASM_REF_NULL(subtype_index), type_index)),
                             kExprEnd});

  const uint8_t kBrOnCastNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_BLOCK_R(refNull(subtype_index), WASM_REF_NULL(type_index),
                    WASM_BR_ON_CAST(0, type_index, subtype_index), WASM_DROP,
                    WASM_RETURN(WASM_I32V(0))),
       WASM_DROP, WASM_I32V(1), WASM_END});

  // Real upcasts are invalid for br_on_cast. Casting to the same type is
  // however similar to an upcast.
  const uint8_t kBrOnCastUpcast = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_BLOCK_R(refNull(type_index), WASM_STRUCT_NEW_DEFAULT(type_index),
                    WASM_BR_ON_CAST(0, type_index, type_index), WASM_DROP,
                    WASM_RETURN(WASM_I32V(0))),
       WASM_DROP, WASM_I32V(1), WASM_END});

  const uint8_t kBrOnCastUpcastNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_BLOCK_R(refNull(type_index), WASM_REF_NULL(type_index),
                    WASM_BR_ON_CAST(0, type_index, type_index), WASM_DROP,
                    WASM_RETURN(WASM_I32V(0))),
       WASM_DROP, WASM_I32V(1), WASM_END});

  const uint8_t kBrOnCastFailNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_BLOCK_R(refNull(type_index), WASM_REF_NULL(type_index),
                    WASM_BR_ON_CAST_FAIL(0, type_index, subtype_index),
                    WASM_DROP, WASM_RETURN(WASM_I32V(0))),
       WASM_DROP, WASM_I32V(1), WASM_END});

  // Real upcasts are invalid for br_on_cast. Casting to the same type is
  // however similar to an upcast.
  const uint8_t kBrOnCastFailUpcast = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_BLOCK_R(refNull(subtype_index),
                    WASM_STRUCT_NEW_DEFAULT(subtype_index),
                    WASM_BR_ON_CAST_FAIL(0, subtype_index, subtype_index),
                    WASM_DROP, WASM_RETURN(WASM_I32V(0))),
       WASM_DROP, WASM_I32V(1), WASM_END});

  const uint8_t kBrOnCastFailUpcastNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_BLOCK_R(refNull(subtype_index), WASM_REF_NULL(subtype_index),
                    WASM_BR_ON_CAST_FAIL(0, subtype_index, subtype_index),
                    WASM_DROP, WASM_RETURN(WASM_I32V(0))),
       WASM_DROP, WASM_I32V(1), WASM_END});

  tester.CompileModule();

  tester.CheckResult(kRefTestNull, 0);
  tester.CheckResult(kRefTestUpcast, 1);
  tester.CheckResult(kRefTestUpcastNull, 0);
  tester.CheckResult(kRefTestUnrelatedNullable, 0);
  tester.CheckResult(kRefTestUnrelatedNull, 0);
  tester.CheckResult(kRefTestUnrelatedNonNullable, 0);

  tester.CheckHasThrown(kRefCastNull);
  tester.CheckResult(kRefCastUpcast, 0);
  tester.CheckHasThrown(kRefCastUpcastNull);

  tester.CheckResult(kBrOnCastNull, 0);
  tester.CheckResult(kBrOnCastUpcast, 1);
  tester.CheckResult(kBrOnCastUpcastNull, 0);

  tester.CheckResult(kBrOnCastFailNull, 1);
  tester.CheckResult(kBrOnCastFailUpcast, 0);
  tester.CheckResult(kBrOnCastFailUpcastNull, 1);
}

WASM_COMPILED_EXEC_TEST(TrivialAbstractCasts) {
  WasmGCTester tester(execution_tier);
  uint8_t type_index = tester.DefineArray(wasm::kWasmI32, true);
  uint8_t struct_type_index = tester.DefineStruct({F(wasm::kWasmI32, true)});
  ValueType sig_types[] = {kWasmS128, kWasmI32, kWasmF64};
  FunctionSig sig(1, 2, sig_types);

  const uint8_t kIsArrayNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(WASM_REF_NULL(kAnyRefCode), kArrayRefCode), kExprEnd});
  const uint8_t kIsArrayUpcast = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(WASM_ARRAY_NEW_DEFAULT(type_index, WASM_I32V(10)),
                     kArrayRefCode),
       kExprEnd});
  const uint8_t kIsArrayUpcastNullable = tester.DefineFunction(
      tester.sigs.i_v(), {ValueType::RefNull(type_index)},
      {WASM_LOCAL_SET(0, WASM_ARRAY_NEW_DEFAULT(type_index, WASM_I32V(10))),
       WASM_REF_TEST(WASM_LOCAL_GET(0), kArrayRefCode), kExprEnd});
  const uint8_t kIsArrayUpcastNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(WASM_REF_NULL(type_index), kArrayRefCode), kExprEnd});
  const uint8_t kIsArrayUnrelated = tester.DefineFunction(
      tester.sigs.i_v(), {ValueType::RefNull(struct_type_index)},
      {WASM_LOCAL_SET(0, WASM_STRUCT_NEW_DEFAULT(struct_type_index)),
       WASM_REF_TEST(WASM_LOCAL_GET(0), kArrayRefCode), kExprEnd});
  const uint8_t kIsArrayUnrelatedNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(WASM_REF_NULL(kI31RefCode), kArrayRefCode), kExprEnd});
  const uint8_t kIsArrayUnrelatedNonNullable = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(WASM_REF_I31(WASM_I32V(10)), kArrayRefCode), kExprEnd});

  const uint8_t kAsArrayNull =
      tester.DefineFunction(tester.sigs.i_v(), {},
                            {WASM_REF_IS_NULL(WASM_REF_CAST(
                                 WASM_REF_NULL(kAnyRefCode), kArrayRefCode)),
                             kExprEnd});
  const uint8_t kAsArrayUpcast = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_IS_NULL(WASM_REF_CAST(
           WASM_ARRAY_NEW_DEFAULT(type_index, WASM_I32V(10)), kArrayRefCode)),
       kExprEnd});
  const uint8_t kAsArrayUpcastNullable = tester.DefineFunction(
      tester.sigs.i_v(), {ValueType::RefNull(type_index)},
      {WASM_LOCAL_SET(0, WASM_ARRAY_NEW_DEFAULT(type_index, WASM_I32V(10))),
       WASM_REF_IS_NULL(WASM_REF_CAST(WASM_LOCAL_GET(0), kArrayRefCode)),
       kExprEnd});
  const uint8_t kAsArrayUpcastNull =
      tester.DefineFunction(tester.sigs.i_v(), {},
                            {WASM_REF_IS_NULL(WASM_REF_CAST(
                                 WASM_REF_NULL(type_index), kArrayRefCode)),
                             kExprEnd});
  const uint8_t kAsArrayUnrelated = tester.DefineFunction(
      tester.sigs.i_v(), {ValueType::RefNull(struct_type_index)},
      {WASM_LOCAL_SET(0, WASM_STRUCT_NEW_DEFAULT(struct_type_index)),
       WASM_REF_IS_NULL(WASM_REF_CAST(WASM_LOCAL_GET(0), kArrayRefCode)),
       kExprEnd});
  const uint8_t kAsArrayUnrelatedNull =
      tester.DefineFunction(tester.sigs.i_v(), {},
                            {WASM_REF_IS_NULL(WASM_REF_CAST(
                                 WASM_REF_NULL(kI31RefCode), kArrayRefCode)),
                             kExprEnd});
  const uint8_t kAsArrayUnrelatedNonNullable =
      tester.DefineFunction(tester.sigs.i_v(), {},
                            {WASM_REF_IS_NULL(WASM_REF_CAST(
                                 WASM_REF_I31(WASM_I32V(10)), kArrayRefCode)),
                             kExprEnd});

  tester.CompileModule();

  tester.CheckResult(kIsArrayNull, 0);
  tester.CheckResult(kIsArrayUpcast, 1);
  tester.CheckResult(kIsArrayUpcastNullable, 1);
  tester.CheckResult(kIsArrayUpcastNull, 0);
  tester.CheckResult(kIsArrayUnrelated, 0);
  tester.CheckResult(kIsArrayUnrelatedNull, 0);
  tester.CheckResult(kIsArrayUnrelatedNonNullable, 0);

  tester.CheckHasThrown(kAsArrayNull);
  tester.CheckResult(kAsArrayUpcast, 0);
  tester.CheckResult(kAsArrayUpcastNullable, 0);
  tester.CheckHasThrown(kAsArrayUpcastNull);
  tester.CheckHasThrown(kAsArrayUnrelated);
  tester.CheckHasThrown(kAsArrayUnrelatedNull);
  tester.CheckHasThrown(kAsArrayUnrelatedNonNullable);
}

WASM_COMPILED_EXEC_TEST(ArrayNewMap) {
  WasmGCTester tester(execution_tier);

  const uint8_t type_index = tester.DefineArray(kWasmI32, true);

  ValueType array_type = ValueType::Ref(type_index);
  FunctionSig sig(1, 0, &array_type);
  const uint8_t array_new = tester.DefineFunction(
      &sig, {},
      {WASM_ARRAY_NEW(type_index, WASM_I32V(10), WASM_I32V(42)), kExprEnd});

  tester.CompileModule();
  Handle<Object> result = tester.GetResultObject(array_new).ToHandleChecked();
  CHECK(IsWasmArray(*result));
  CHECK_EQ(
      Cast<WasmArray>(result)->map(),
      tester.trusted_instance_data()->managed_object_maps()->get(type_index));
}

WASM_COMPILED_EXEC_TEST(FunctionRefs) {
  WasmGCTester tester(execution_tier);
  const uint8_t func_index =
      tester.DefineFunction(tester.sigs.i_v(), {}, {WASM_I32V(42), kExprEnd});
  const uint8_t sig_index = 0;

  const uint8_t other_sig_index = tester.DefineSignature(tester.sigs.d_d());

  // This is just so func_index counts as "declared".
  tester.AddGlobal(ValueType::RefNull(sig_index), false,
                   WasmInitExpr::RefFuncConst(func_index));

  ValueType func_type = ValueType::RefNull(sig_index);
  FunctionSig sig_func(1, 0, &func_type);

  const uint8_t cast = tester.DefineFunction(
      &sig_func, {kWasmFuncRef},
      {WASM_LOCAL_SET(0, WASM_REF_FUNC(func_index)),
       WASM_REF_CAST(WASM_LOCAL_GET(0), sig_index), kExprEnd});

  const uint8_t cast_reference = tester.DefineFunction(
      &sig_func, {}, {WASM_REF_FUNC(sig_index), kExprEnd});

  const uint8_t test = tester.DefineFunction(
      tester.sigs.i_v(), {kWasmFuncRef},
      {WASM_LOCAL_SET(0, WASM_REF_FUNC(func_index)),
       WASM_REF_TEST(WASM_LOCAL_GET(0), sig_index), kExprEnd});

  const uint8_t test_fail = tester.DefineFunction(
      tester.sigs.i_v(), {kWasmFuncRef},
      {WASM_LOCAL_SET(0, WASM_REF_FUNC(func_index)),
       WASM_REF_TEST(WASM_LOCAL_GET(0), other_sig_index), kExprEnd});

  tester.CompileModule();

  i::Isolate* i_isolate = CcTest::i_isolate();
  DirectHandle<Object> result_cast =
      tester.GetResultObject(cast).ToHandleChecked();
  CHECK(IsWasmFuncRef(*result_cast));
  DirectHandle<WasmInternalFunction> result_cast_internal{
      Cast<WasmFuncRef>(*result_cast)->internal(i_isolate), i_isolate};
  DirectHandle<JSFunction> cast_function =
      WasmInternalFunction::GetOrCreateExternal(result_cast_internal);

  DirectHandle<Object> result_cast_reference =
      tester.GetResultObject(cast_reference).ToHandleChecked();
  CHECK(IsWasmFuncRef(*result_cast_reference));
  DirectHandle<WasmInternalFunction> result_cast_reference_internal{
      Cast<WasmFuncRef>(*result_cast_reference)->internal(i_isolate),
      i_isolate};
  DirectHandle<JSFunction> cast_function_reference =
      WasmInternalFunction::GetOrCreateExternal(result_cast_reference_internal);

  CHECK_EQ(cast_function->code(i_isolate)->instruction_start(),
           cast_function_reference->code(i_isolate)->instruction_start());

  tester.CheckResult(test, 1);
  tester.CheckResult(test_fail, 0);
}

WASM_COMPILED_EXEC_TEST(CallRef) {
  WasmGCTester tester(execution_tier);
  uint8_t sig_index = tester.DefineSignature(tester.sigs.i_ii());
  uint8_t callee = tester.DefineFunction(
      sig_index, {},
      {WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)), kExprEnd});
  uint8_t caller =
      tester.DefineFunction(tester.sigs.i_i(), {},
                            {WASM_CALL_REF(WASM_REF_FUNC(callee), sig_index,
                                           WASM_I32V(42), WASM_LOCAL_GET(0)),
                             kExprEnd});

  // This is just so func_index counts as "declared".
  tester.AddGlobal(ValueType::RefNull(sig_index), false,
                   WasmInitExpr::RefFuncConst(callee));

  tester.CompileModule();

  tester.CheckResult(caller, 47, 5);
}

// Test that calling a function expecting any ref accepts the abstract null
// type argument (nullref, nullfuncref, nullexternref).
WASM_COMPILED_EXEC_TEST(CallAbstractNullTypeImplicitConversion) {
  FlagScope<bool> exnref(&v8_flags.experimental_wasm_exnref, true);
  const struct {
    ValueType super_type;
    ValueTypeCode sub_type_code;
  } null_ref_types[] = {
      {kWasmFuncRef, kNoFuncCode},
      {kWasmEqRef, kNoneCode},
      {kWasmI31Ref.AsNullable(), kNoneCode},
      {kWasmStructRef.AsNullable(), kNoneCode},
      {kWasmArrayRef.AsNullable(), kNoneCode},
      {kWasmAnyRef, kNoneCode},
      {kWasmExternRef, kNoExternCode},
      {kWasmExnRef, kNoExnCode},
      {refNull(0), kNoneCode},    // struct
      {refNull(1), kNoneCode},    // array
      {refNull(2), kNoFuncCode},  // signature
  };

  for (auto [super_type, sub_type_code] : null_ref_types) {
    CHECK(super_type.is_nullable());
    WasmGCTester tester(execution_tier);
    uint8_t struct_idx = tester.DefineStruct({F(wasm::kWasmI32, true)});
    CHECK_EQ(struct_idx, 0);
    uint8_t array_idx = tester.DefineArray(kWasmI32, true);
    CHECK_EQ(array_idx, 1);
    FunctionSig dummySig(1, 0, &kWasmI32);
    uint8_t signature_idx = tester.DefineSignature(&dummySig);
    CHECK_EQ(signature_idx, 2);

    ValueType ref_sig_types[] = {kWasmI32, super_type};
    FunctionSig sig_ref(1, 1, ref_sig_types);
    uint8_t callee = tester.DefineFunction(
        &sig_ref, {}, {WASM_REF_IS_NULL(WASM_LOCAL_GET(0)), kExprEnd});
    uint8_t caller = tester.DefineFunction(
        tester.sigs.i_v(), {},
        {WASM_CALL_FUNCTION(callee, WASM_REF_NULL(sub_type_code)), kExprEnd});

    tester.CompileModule();
    tester.CheckResult(caller, 1);
  }
}

WASM_COMPILED_EXEC_TEST(CastNullRef) {
  WasmGCTester tester(execution_tier);
  uint8_t to_non_null = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_IS_NULL(WASM_REF_AS_NON_NULL(WASM_REF_NULL(kNoneCode))),
       kExprEnd});
  uint8_t to_array = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_IS_NULL(WASM_REF_CAST(WASM_REF_NULL(kNoneCode), kArrayRefCode)),
       kExprEnd});
  uint8_t to_struct =
      tester.DefineFunction(tester.sigs.i_v(), {},
                            {WASM_REF_IS_NULL(WASM_REF_CAST(
                                 WASM_REF_NULL(kNoneCode), kStructRefCode)),
                             kExprEnd});
  uint8_t to_i31 = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_IS_NULL(WASM_REF_CAST(WASM_REF_NULL(kNoneCode), kI31RefCode)),
       kExprEnd});
  uint8_t struct_idx = tester.DefineStruct({F(wasm::kWasmI32, true)});
  uint8_t to_struct_idx = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_IS_NULL(WASM_REF_CAST(WASM_REF_NULL(kNoneCode), struct_idx)),
       kExprEnd});
  tester.CompileModule();
  // ref.cast traps on null.
  tester.CheckHasThrown(to_non_null);
  tester.CheckHasThrown(to_array);
  tester.CheckHasThrown(to_struct);
  tester.CheckHasThrown(to_i31);
  tester.CheckHasThrown(to_struct_idx);
}

WASM_COMPILED_EXEC_TEST(CallReftypeParameters) {
  WasmGCTester tester(execution_tier);
  uint8_t type_index = tester.DefineStruct({F(wasm::kWasmI32, true)});
  ValueType kRefType{refNull(type_index)};
  ValueType sig_types[] = {kWasmI32, kRefType, kRefType, kRefType, kRefType,
                           kWasmI32, kWasmI32, kWasmI32, kWasmI32};
  FunctionSig sig(1, 8, sig_types);
  uint8_t adder = tester.DefineFunction(
      &sig, {},
      {WASM_I32_ADD(
           WASM_STRUCT_GET(type_index, 0, WASM_LOCAL_GET(0)),
           WASM_I32_ADD(
               WASM_STRUCT_GET(type_index, 0, WASM_LOCAL_GET(1)),
               WASM_I32_ADD(
                   WASM_STRUCT_GET(type_index, 0, WASM_LOCAL_GET(2)),
                   WASM_I32_ADD(
                       WASM_STRUCT_GET(type_index, 0, WASM_LOCAL_GET(3)),
                       WASM_I32_ADD(
                           WASM_LOCAL_GET(4),
                           WASM_I32_ADD(WASM_LOCAL_GET(5),
                                        WASM_I32_ADD(WASM_LOCAL_GET(6),
                                                     WASM_LOCAL_GET(7)))))))),
       kExprEnd});
  uint8_t caller = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_CALL_FUNCTION(adder, WASM_STRUCT_NEW(type_index, WASM_I32V(2)),
                          WASM_STRUCT_NEW(type_index, WASM_I32V(4)),
                          WASM_STRUCT_NEW(type_index, WASM_I32V(8)),
                          WASM_STRUCT_NEW(type_index, WASM_I32V(16)),
                          WASM_I32V(32), WASM_I32V(64), WASM_I32V(128),
                          WASM_I32V(256)),
       kExprEnd});

  tester.CompileModule();
  tester.CheckResult(caller, 510);
}

WASM_COMPILED_EXEC_TEST(AbstractTypeChecks) {
  WasmGCTester tester(execution_tier);

  uint8_t array_index = tester.DefineArray(kWasmI32, true);
  uint8_t struct_index = tester.DefineStruct({F(kWasmI32, true)});
  uint8_t function_index =
      tester.DefineFunction(tester.sigs.v_v(), {}, {kExprEnd});
  uint8_t sig_index = 2;

  // This is just so func_index counts as "declared".
  tester.AddGlobal(ValueType::RefNull(sig_index), false,
                   WasmInitExpr::RefFuncConst(function_index));

  uint8_t kStructCheckNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(WASM_REF_NULL(kAnyRefCode), kStructRefCode), kExprEnd});
  uint8_t kArrayCheckNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(WASM_REF_NULL(kAnyRefCode), kArrayRefCode), kExprEnd});
  uint8_t kI31CheckNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_TEST(WASM_REF_NULL(kAnyRefCode), kI31RefCode), kExprEnd});

  uint8_t kStructCastNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_CAST(WASM_REF_NULL(kAnyRefCode), kStructRefCode), WASM_DROP,
       WASM_I32V(1), kExprEnd});
  uint8_t kArrayCastNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_CAST(WASM_REF_NULL(kAnyRefCode), kArrayRefCode), WASM_DROP,
       WASM_I32V(1), kExprEnd});
  uint8_t kI31CastNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_CAST(WASM_REF_NULL(kAnyRefCode), kI31RefCode), WASM_DROP,
       WASM_I32V(1), kExprEnd});

#define TYPE_CHECK(type, value)            \
  tester.DefineFunction(                   \
      tester.sigs.i_v(), {kWasmAnyRef},    \
      {WASM_LOCAL_SET(0, WASM_SEQ(value)), \
       WASM_REF_TEST(WASM_LOCAL_GET(0), k##type##RefCode), kExprEnd})

  uint8_t kStructCheckSuccess =
      TYPE_CHECK(Struct, WASM_STRUCT_NEW_DEFAULT(struct_index));
  uint8_t kStructCheckFailure = TYPE_CHECK(Struct, WASM_REF_I31(WASM_I32V(42)));
  uint8_t kArrayCheckSuccess =
      TYPE_CHECK(Array, WASM_ARRAY_NEW_DEFAULT(array_index, WASM_I32V(10)));
  uint8_t kArrayCheckFailure =
      TYPE_CHECK(Array, WASM_STRUCT_NEW_DEFAULT(struct_index));
  uint8_t kI31CheckSuccess = TYPE_CHECK(I31, WASM_REF_I31(WASM_I32V(42)));
  uint8_t kI31CheckFailure =
      TYPE_CHECK(I31, WASM_ARRAY_NEW_DEFAULT(array_index, WASM_I32V(10)));
#undef TYPE_CHECK

#define TYPE_CAST(type, value)                                               \
  tester.DefineFunction(tester.sigs.i_v(), {kWasmAnyRef},                    \
                        {WASM_LOCAL_SET(0, WASM_SEQ(value)),                 \
                         WASM_REF_CAST(WASM_LOCAL_GET(0), k##type##RefCode), \
                         WASM_DROP, WASM_I32V(1), kExprEnd})

  uint8_t kStructCastSuccess =
      TYPE_CAST(Struct, WASM_STRUCT_NEW_DEFAULT(struct_index));
  uint8_t kStructCastFailure = TYPE_CAST(Struct, WASM_REF_I31(WASM_I32V(42)));
  uint8_t kArrayCastSuccess =
      TYPE_CAST(Array, WASM_ARRAY_NEW_DEFAULT(array_index, WASM_I32V(10)));
  uint8_t kArrayCastFailure = TYPE_CAST(Array, WASM_REF_I31(WASM_I32V(42)));
  uint8_t kI31CastSuccess = TYPE_CAST(I31, WASM_REF_I31(WASM_I32V(42)));
  uint8_t kI31CastFailure =
      TYPE_CAST(I31, WASM_ARRAY_NEW_DEFAULT(array_index, WASM_I32V(10)));
#undef TYPE_CAST

// If the branch is not taken, we return 0. If it is taken, then the respective
// type check should succeed, and we return 1.
#define BR_ON(type, value)                                                 \
  tester.DefineFunction(                                                   \
      tester.sigs.i_v(), {kWasmAnyRef},                                    \
      {WASM_LOCAL_SET(0, WASM_SEQ(value)),                                 \
       WASM_REF_TEST(                                                      \
           WASM_BLOCK_R(kWasm##type##Ref, WASM_LOCAL_GET(0),               \
                        WASM_BR_ON_CAST(0, kAnyRefCode, k##type##RefCode), \
                        WASM_RETURN(WASM_I32V(0))),                        \
           k##type##RefCode),                                              \
       kExprEnd})

  uint8_t kBrOnStructTaken =
      BR_ON(Struct, WASM_STRUCT_NEW_DEFAULT(struct_index));
  uint8_t kBrOnStructNotTaken = BR_ON(Struct, WASM_REF_NULL(kNoneCode));
  uint8_t kBrOnArrayTaken =
      BR_ON(Array, WASM_ARRAY_NEW_DEFAULT(array_index, WASM_I32V(10)));
  uint8_t kBrOnArrayNotTaken = BR_ON(Array, WASM_REF_I31(WASM_I32V(42)));
  uint8_t kBrOnI31Taken = BR_ON(I31, WASM_REF_I31(WASM_I32V(42)));
  uint8_t kBrOnI31NotTaken =
      BR_ON(I31, WASM_ARRAY_NEW_DEFAULT(array_index, WASM_I32V(10)));
#undef BR_ON

// If the branch is not taken, we return 1. If it is taken, then the respective
// type check should fail, and we return 0.
#define BR_ON_NON(type, value)                                            \
  tester.DefineFunction(                                                  \
      tester.sigs.i_v(), {kWasmAnyRef},                                   \
      {WASM_LOCAL_SET(0, WASM_SEQ(value)),                                \
       WASM_REF_TEST(WASM_BLOCK_R(kWasmAnyRef, WASM_LOCAL_GET(0),         \
                                  WASM_BR_ON_CAST_FAIL(0, kAnyRefCode,    \
                                                       k##type##RefCode), \
                                  WASM_RETURN(WASM_I32V(1))),             \
                     k##type##RefCode),                                   \
       kExprEnd})

  uint8_t kBrOnNonStructNotTaken =
      BR_ON_NON(Struct, WASM_STRUCT_NEW_DEFAULT(struct_index));
  uint8_t kBrOnNonStructTaken = BR_ON_NON(Struct, WASM_REF_NULL(kNoneCode));
  uint8_t kBrOnNonArrayNotTaken =
      BR_ON_NON(Array, WASM_ARRAY_NEW_DEFAULT(array_index, WASM_I32V(10)));
  uint8_t kBrOnNonArrayTaken = BR_ON_NON(Array, WASM_REF_I31(WASM_I32V(42)));
  uint8_t kBrOnNonI31NotTaken = BR_ON_NON(I31, WASM_REF_I31(WASM_I32V(42)));
  uint8_t kBrOnNonI31Taken =
      BR_ON_NON(I31, WASM_ARRAY_NEW_DEFAULT(array_index, WASM_I32V(10)));
#undef BR_ON_NON

  tester.CompileModule();

  tester.CheckResult(kStructCheckNull, 0);
  tester.CheckResult(kArrayCheckNull, 0);
  tester.CheckResult(kI31CheckNull, 0);

  tester.CheckHasThrown(kStructCastNull);
  tester.CheckHasThrown(kArrayCastNull);
  tester.CheckHasThrown(kI31CastNull);

  tester.CheckResult(kStructCheckSuccess, 1);
  tester.CheckResult(kArrayCheckSuccess, 1);
  tester.CheckResult(kI31CheckSuccess, 1);

  tester.CheckResult(kStructCheckFailure, 0);
  tester.CheckResult(kArrayCheckFailure, 0);
  tester.CheckResult(kI31CheckFailure, 0);

  tester.CheckResult(kStructCastSuccess, 1);
  tester.CheckResult(kArrayCastSuccess, 1);
  tester.CheckResult(kI31CastSuccess, 1);

  tester.CheckHasThrown(kStructCastFailure);
  tester.CheckHasThrown(kArrayCastFailure);
  tester.CheckHasThrown(kI31CastFailure);

  tester.CheckResult(kBrOnStructTaken, 1);
  tester.CheckResult(kBrOnStructNotTaken, 0);
  tester.CheckResult(kBrOnArrayTaken, 1);
  tester.CheckResult(kBrOnArrayNotTaken, 0);
  tester.CheckResult(kBrOnI31Taken, 1);
  tester.CheckResult(kBrOnI31NotTaken, 0);

  tester.CheckResult(kBrOnNonStructTaken, 0);
  tester.CheckResult(kBrOnNonStructNotTaken, 1);
  tester.CheckResult(kBrOnNonArrayTaken, 0);
  tester.CheckResult(kBrOnNonArrayNotTaken, 1);
  tester.CheckResult(kBrOnNonI31Taken, 0);
  tester.CheckResult(kBrOnNonI31NotTaken, 1);
}

// This flushed out a few bugs, so it serves as a regression test. It can also
// be modified (made to run longer) to measure performance of casts.
WASM_COMPILED_EXEC_TEST(CastsBenchmark) {
  WasmGCTester tester(execution_tier);
  const uint8_t SuperType = tester.DefineStruct({F(wasm::kWasmI32, true)});
  const uint8_t SubType = tester.DefineStruct(
      {F(wasm::kWasmI32, true), F(wasm::kWasmI32, true)}, SuperType);

  const uint8_t ListType = tester.DefineArray(kWasmStructRef, true);

  const uint8_t List =
      tester.AddGlobal(ValueType::RefNull(ListType), true,
                       WasmInitExpr::RefNullConst(
                           static_cast<HeapType::Representation>(ListType)));

  const uint32_t kListLength = 1024;
  const uint32_t i = 0;
  const uint8_t Prepare = tester.DefineFunction(
      tester.sigs.i_v(), {wasm::kWasmI32},
      {// List = new eqref[kListLength];
       WASM_GLOBAL_SET(
           List, WASM_ARRAY_NEW_DEFAULT(ListType, WASM_I32V(kListLength))),
       // for (int i = 0; i < kListLength; ) {
       //   List[i] = new Super(i);
       //   i++;
       //   List[i] = new Sub(i, 0);
       //   i++;
       // }
       WASM_LOCAL_SET(i, WASM_I32V_1(0)),
       WASM_LOOP(
           WASM_ARRAY_SET(ListType, WASM_GLOBAL_GET(List), WASM_LOCAL_GET(i),
                          WASM_STRUCT_NEW(SuperType, WASM_LOCAL_GET(i))),
           WASM_LOCAL_SET(i, WASM_I32_ADD(WASM_LOCAL_GET(i), WASM_I32V_1(1))),
           WASM_ARRAY_SET(
               ListType, WASM_GLOBAL_GET(List), WASM_LOCAL_GET(i),
               WASM_STRUCT_NEW(SubType, WASM_LOCAL_GET(i), WASM_I32V_1(0))),
           WASM_LOCAL_SET(i, WASM_I32_ADD(WASM_LOCAL_GET(i), WASM_I32V_1(1))),
           WASM_BR_IF(0,
                      WASM_I32_NE(WASM_LOCAL_GET(i), WASM_I32V(kListLength)))),
       // return 42;  // Dummy value, due to test framework.
       WASM_I32V_1(42), kExprEnd});

  const uint32_t sum = 1;  // Index of the local.
  const uint32_t list = 2;
  const uint32_t kLoops = 2;
  const uint32_t kIterations = kLoops * kListLength;
  const uint8_t Main = tester.DefineFunction(
      tester.sigs.i_v(),
      {
          wasm::kWasmI32,
          wasm::kWasmI32,
          ValueType::RefNull(ListType),
      },
      {WASM_LOCAL_SET(list, WASM_GLOBAL_GET(List)),
       // sum = 0;
       WASM_LOCAL_SET(sum, WASM_I32V_1(0)),
       // for (int i = 0; i < kIterations; i++) {
       //   sum += ref.cast<super>(List[i & kListLength]).x
       // }
       WASM_LOCAL_SET(i, WASM_I32V_1(0)),
       WASM_LOOP(
           WASM_LOCAL_SET(
               sum, WASM_I32_ADD(
                        WASM_LOCAL_GET(sum),
                        WASM_STRUCT_GET(
                            SuperType, 0,
                            WASM_REF_CAST(
                                WASM_ARRAY_GET(
                                    ListType, WASM_LOCAL_GET(list),
                                    WASM_I32_AND(WASM_LOCAL_GET(i),
                                                 WASM_I32V(kListLength - 1))),
                                SuperType)))),
           WASM_LOCAL_SET(i, WASM_I32_ADD(WASM_LOCAL_GET(i), WASM_I32V_1(1))),
           WASM_BR_IF(0,
                      WASM_I32_LTS(WASM_LOCAL_GET(i), WASM_I32V(kIterations)))),
       // return sum;
       WASM_LOCAL_GET(sum), kExprEnd});

  tester.CompileModule();
  tester.CheckResult(Prepare, 42);

  // Time this section to get a benchmark for subtyping checks.
  // Note: if you bump kIterations or kListLength, you may have to take i32
  // overflow into account.
  tester.CheckResult(Main, (kListLength * (kListLength - 1) / 2) * kLoops);
}

WASM_COMPILED_EXEC_TEST(GlobalInitReferencingGlobal) {
  WasmGCTester tester(execution_tier);
  const uint8_t from = tester.AddGlobal(kWasmI32, false, WasmInitExpr(42));
  const uint8_t to =
      tester.AddGlobal(kWasmI32, false, WasmInitExpr::GlobalGet(from));

  const uint8_t func = tester.DefineFunction(tester.sigs.i_v(), {},
                                             {WASM_GLOBAL_GET(to), kExprEnd});

  tester.CompileModule();

  tester.CheckResult(func, 42);
}

WASM_COMPILED_EXEC_TEST(GCTables) {
  WasmGCTester tester(execution_tier);

  tester.builder()->StartRecursiveTypeGroup();
  uint8_t super_struct = tester.DefineStruct({F(kWasmI32, false)});
  uint8_t sub_struct = tester.DefineStruct(
      {F(kWasmI32, false), F(kWasmI32, true)}, super_struct);
  FunctionSig* super_sig =
      FunctionSig::Build(tester.zone(), {kWasmI32}, {refNull(sub_struct)});
  uint8_t super_sig_index = tester.DefineSignature(super_sig);
  FunctionSig* sub_sig =
      FunctionSig::Build(tester.zone(), {kWasmI32}, {refNull(super_struct)});
  uint8_t sub_sig_index = tester.DefineSignature(sub_sig, super_sig_index);
  uint8_t unrelated_sig_index =
      tester.DefineSignature(sub_sig, super_sig_index);
  tester.builder()->EndRecursiveTypeGroup();

  tester.DefineTable(refNull(super_sig_index), 10, 10);

  uint8_t super_func = tester.DefineFunction(
      super_sig_index, {},
      {WASM_I32_ADD(WASM_STRUCT_GET(sub_struct, 0, WASM_LOCAL_GET(0)),
                    WASM_STRUCT_GET(sub_struct, 1, WASM_LOCAL_GET(0))),
       WASM_END});

  uint8_t sub_func = tester.DefineFunction(
      sub_sig_index, {},
      {WASM_STRUCT_GET(super_struct, 0, WASM_LOCAL_GET(0)), WASM_END});

  uint8_t setup_func = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_TABLE_SET(0, WASM_I32V(0), WASM_REF_NULL(super_sig_index)),
       WASM_TABLE_SET(0, WASM_I32V(1), WASM_REF_FUNC(super_func)),
       WASM_TABLE_SET(0, WASM_I32V(2), WASM_REF_FUNC(sub_func)),  // --
       WASM_I32V(0), WASM_END});

  uint8_t super_struct_producer = tester.DefineFunction(
      FunctionSig::Build(tester.zone(), {ref(super_struct)}, {}), {},
      {WASM_STRUCT_NEW(super_struct, WASM_I32V(-5)), WASM_END});
  uint8_t sub_struct_producer = tester.DefineFunction(
      FunctionSig::Build(tester.zone(), {ref(sub_struct)}, {}), {},
      {WASM_STRUCT_NEW(sub_struct, WASM_I32V(7), WASM_I32V(11)), WASM_END});

  // Calling a null entry should trap.
  uint8_t call_null = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_CALL_INDIRECT(super_sig_index,
                          WASM_CALL_FUNCTION0(sub_struct_producer),
                          WASM_I32V(0)),
       WASM_END});
  // Calling with a signature identical to the type of the table should work,
  // provided the entry has the same signature.
  uint8_t call_same_type = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_CALL_INDIRECT(super_sig_index,
                          WASM_CALL_FUNCTION0(sub_struct_producer),
                          WASM_I32V(1)),
       WASM_END});
  // Calling with a signature that is a subtype of the type of the table should
  // work, provided the entry has the same signature.
  uint8_t call_subtype = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_CALL_INDIRECT(sub_sig_index,
                          WASM_CALL_FUNCTION0(super_struct_producer),
                          WASM_I32V(2)),
       WASM_END});
  // Calling with a signature that is a subtype of the type of the table should
  // work, provided the entry has a subtype of the declared signature.
  uint8_t call_table_subtype_entry_subtype = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_CALL_INDIRECT(super_sig_index,
                          WASM_CALL_FUNCTION0(sub_struct_producer),
                          WASM_I32V(2)),
       WASM_END});
  // Calling with a signature that is mismatched to that of the entry should
  // trap.
  uint8_t call_type_mismatch = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_CALL_INDIRECT(unrelated_sig_index,
                          WASM_CALL_FUNCTION0(super_struct_producer),
                          WASM_I32V(2)),
       WASM_END});
  // Getting a table element and then calling it with call_ref should work.
  uint8_t table_get_and_call_ref = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_CALL_REF(WASM_TABLE_GET(0, WASM_I32V(2)), super_sig_index,
                     WASM_CALL_FUNCTION0(sub_struct_producer)),
       WASM_END});

  // Only here so these functions count as "declared".
  tester.AddGlobal(refNull(super_sig_index), false,
                   WasmInitExpr::RefFuncConst(super_func));
  tester.AddGlobal(refNull(sub_sig_index), false,
                   WasmInitExpr::RefFuncConst(sub_func));

  tester.CompileModule();

  tester.CheckResult(setup_func, 0);
  tester.CheckHasThrown(call_null);
  tester.CheckResult(call_same_type, 18);
  tester.CheckResult(call_subtype, -5);
  tester.CheckResult(call_table_subtype_entry_subtype, 7);
  tester.CheckHasThrown(call_type_mismatch);
  tester.CheckResult(table_get_and_call_ref, 7);
}

WASM_COMPILED_EXEC_TEST(JsAccess) {
  WasmGCTester tester(execution_tier);
  const uint8_t type_index = tester.DefineStruct({F(wasm::kWasmI32, true)});
  ValueType kRefType = ref(type_index);
  ValueType kSupertypeToI[] = {kWasmI32, kWasmStructRef};
  FunctionSig sig_t_v(1, 0, &kRefType);
  FunctionSig sig_super_v(1, 0, &kWasmStructRef);
  FunctionSig sig_i_super(1, 1, kSupertypeToI);

  tester.DefineExportedFunction(
      "typed_producer", &sig_t_v,
      {WASM_STRUCT_NEW(type_index, WASM_I32V(42)), kExprEnd});
  // Same code, different signature.
  tester.DefineExportedFunction(
      "untyped_producer", &sig_super_v,
      {WASM_STRUCT_NEW(type_index, WASM_I32V(42)), kExprEnd});
  tester.DefineExportedFunction(
      "consumer", &sig_i_super,
      {WASM_STRUCT_GET(type_index, 0,
                       WASM_REF_CAST(WASM_LOCAL_GET(0), type_index)),
       kExprEnd});

  tester.CompileModule();
  Isolate* isolate = tester.isolate();
  TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate));
  for (const char* producer : {"typed_producer", "untyped_producer"}) {
    MaybeHandle<Object> maybe_result =
        tester.CallExportedFunction(producer, 0, nullptr);
    if (maybe_result.is_null()) {
      FATAL("Calling %s failed: %s", producer,
            *v8::String::Utf8Value(reinterpret_cast<v8::Isolate*>(isolate),
                                   try_catch.Message()->Get()));
    }
    {
      Handle<Object> args[] = {maybe_result.ToHandleChecked()};
      maybe_result = tester.CallExportedFunction("consumer", 1, args);
    }
    if (maybe_result.is_null()) {
      FATAL("Calling 'consumer' failed: %s",
            *v8::String::Utf8Value(reinterpret_cast<v8::Isolate*>(isolate),
                                   try_catch.Message()->Get()));
    }
    Handle<Object> result = maybe_result.ToHandleChecked();
    CHECK(IsSmi(*result));
    CHECK_EQ(42, Cast<Smi>(*result).value());
    // Calling {consumer} with any other object (e.g. the Smi we just got as
    // {result}) should trap.
    {
      Handle<Object> args[] = {result};
      maybe_result = tester.CallExportedFunction("consumer", 1, args);
    }
    CHECK(maybe_result.is_null());
    CHECK(try_catch.HasCaught());
    try_catch.Reset();
    isolate->clear_exception();
  }
}

WASM_COMPILED_EXEC_TEST(WasmAnyConvertExtern) {
  WasmGCTester tester(execution_tier);

  const uint8_t kNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_IS_NULL(
           WASM_GC_ANY_CONVERT_EXTERN(WASM_REF_NULL(kNoExternCode))),
       kExprEnd});

  tester.CompileModule();
  tester.CheckResult(kNull, 1);
}

WASM_COMPILED_EXEC_TEST(WasmExternConvertAny) {
  WasmGCTester tester(execution_tier);

  const uint8_t kNull = tester.DefineFunction(
      tester.sigs.i_v(), {},
      {WASM_REF_IS_NULL(WASM_GC_EXTERN_CONVERT_ANY(WASM_REF_NULL(kNoneCode))),
       kExprEnd});

  tester.CompileModule();
  tester.CheckResult(kNull, 1);
}

}  // namespace test_gc
}  // namespace wasm
}  // namespace internal
}  // namespace v8

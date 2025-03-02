// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/common/wasm/flag-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"
#include "test/common/wasm/wasm-module-runner.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_run_wasm_wrappers {

using testing::CompileAndInstantiateForTesting;

#if V8_COMPRESS_POINTERS &&                                               \
    (V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_IA32 || \
     V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_LOONG64)
namespace {
DirectHandle<WasmInstanceObject> CompileModule(Zone* zone, Isolate* isolate,
                                               WasmModuleBuilder* builder) {
  ZoneBuffer buffer(zone);
  builder->WriteTo(&buffer);
  testing::SetupIsolateForWasmModule(isolate);
  ErrorThrower thrower(isolate, "CompileAndRunWasmModule");
  MaybeDirectHandle<WasmInstanceObject> maybe_instance =
      CompileAndInstantiateForTesting(isolate, &thrower,
                                      base::VectorOf(buffer));
  CHECK_WITH_MSG(!thrower.error(), thrower.error_msg());
  return maybe_instance.ToHandleChecked();
}

bool IsGeneric(Tagged<Code> wrapper) {
  return wrapper->is_builtin() &&
         wrapper->builtin_id() == Builtin::kJSToWasmWrapper;
}

bool IsSpecific(Tagged<Code> wrapper) {
  return wrapper->kind() == CodeKind::JS_TO_WASM_FUNCTION;
}

DirectHandle<Object> SmiHandle(Isolate* isolate, int value) {
  return DirectHandle<Object>(Smi::FromInt(value), isolate);
}

void SmiCall(Isolate* isolate,
             DirectHandle<WasmExportedFunction> exported_function,
             base::Vector<const DirectHandle<Object>> args,
             int expected_result) {
  DirectHandle<Object> receiver = isolate->factory()->undefined_value();
  DirectHandle<Object> result =
      Execution::Call(isolate, exported_function, receiver, args)
          .ToHandleChecked();
  CHECK(IsSmi(*result));
  CHECK_EQ(expected_result, Smi::ToInt(*result));
}

void Cleanup() {
  // By sending a low memory notifications, we will try hard to collect all
  // garbage and will therefore also invoke all weak callbacks of actually
  // unreachable persistent handles.
  Isolate* isolate = CcTest::InitIsolateOnce();
  reinterpret_cast<v8::Isolate*>(isolate)->LowMemoryNotification();
}

}  // namespace

TEST(WrapperBudget) {
  {
    // This test assumes use of the generic wrapper.
    FlagScope<bool> use_wasm_generic_wrapper(&v8_flags.wasm_generic_wrapper,
                                             true);

    // Initialize the environment and create a module builder.
    AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);
    Isolate* isolate = CcTest::InitIsolateOnce();
    HandleScope scope(isolate);
    WasmModuleBuilder* builder = zone.New<WasmModuleBuilder>(&zone);

    // Define the Wasm function.
    TestSignatures sigs;
    WasmFunctionBuilder* f = builder->AddFunction(sigs.i_ii());
    f->builder()->AddExport(base::CStrVector("main"), f);
    f->EmitCode({WASM_I32_MUL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)), WASM_END});

    // Compile the module.
    DirectHandle<WasmInstanceObject> instance =
        CompileModule(&zone, isolate, builder);

    // Get the exported function and the function data.
    DirectHandle<WasmExportedFunction> main_export =
        testing::GetExportedFunction(isolate, instance, "main")
            .ToHandleChecked();
    DirectHandle<WasmExportedFunctionData> main_function_data(
        main_export->shared()->wasm_exported_function_data(), isolate);

    // Check that the generic-wrapper budget has initially a value of
    // kGenericWrapperBudget.
    CHECK_EQ(Smi::ToInt(main_function_data->wrapper_budget()->value()),
             kGenericWrapperBudget);
    static_assert(kGenericWrapperBudget > 0);

    // Call the exported Wasm function.
    DirectHandle<Object> params[] = {SmiHandle(isolate, 6),
                                     SmiHandle(isolate, 7)};
    SmiCall(isolate, main_export, base::VectorOf(params), 42);

    // Check that the budget has now a value of (kGenericWrapperBudget - 1).
    CHECK_EQ(Smi::ToInt(main_function_data->wrapper_budget()->value()),
             kGenericWrapperBudget - 1);
  }
  Cleanup();
}

TEST(WrapperReplacement) {
  {
    // This test assumes use of the generic wrapper.
    FlagScope<bool> use_wasm_generic_wrapper(&v8_flags.wasm_generic_wrapper,
                                             true);

    // Initialize the environment and create a module builder.
    AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);
    Isolate* isolate = CcTest::InitIsolateOnce();
    HandleScope scope(isolate);
    WasmModuleBuilder* builder = zone.New<WasmModuleBuilder>(&zone);

    // Define the Wasm function.
    TestSignatures sigs;
    WasmFunctionBuilder* f = builder->AddFunction(sigs.i_i());
    f->builder()->AddExport(base::CStrVector("main"), f);
    f->EmitCode({WASM_LOCAL_GET(0), WASM_END});

    // Compile the module.
    DirectHandle<WasmInstanceObject> instance =
        CompileModule(&zone, isolate, builder);

    // Get the exported function and the function data.
    DirectHandle<WasmExportedFunction> main_export =
        testing::GetExportedFunction(isolate, instance, "main")
            .ToHandleChecked();
    DirectHandle<WasmExportedFunctionData> main_function_data(
        main_export->shared()->wasm_exported_function_data(), isolate);

    // Check that the generic-wrapper budget has initially a value of
    // kGenericWrapperBudget.
    CHECK_EQ(Smi::ToInt(main_function_data->wrapper_budget()->value()),
             kGenericWrapperBudget);
    static_assert(kGenericWrapperBudget > 0);

    // Set the generic-wrapper budget to a value that allows for a few
    // more calls through the generic wrapper.
    const int remaining_budget =
        std::min(static_cast<int>(kGenericWrapperBudget), 2);
    main_function_data->wrapper_budget()->set_value(
        Smi::FromInt(remaining_budget));

    // Call the exported Wasm function as many times as required to almost
    // exhaust the remaining budget for using the generic wrapper.
    DirectHandle<Code> wrapper_before_call;
    for (int i = remaining_budget; i > 0; --i) {
      // Verify that the wrapper to be used is the generic one.
      wrapper_before_call =
          direct_handle(main_function_data->wrapper_code(isolate), isolate);
      CHECK(IsGeneric(*wrapper_before_call));
      // Call the function.
      DirectHandle<Object> params[] = {SmiHandle(isolate, i)};
      SmiCall(isolate, main_export, base::VectorOf(params), i);
      // Verify that the budget has now a value of (i - 1).
      CHECK_EQ(Smi::ToInt(main_function_data->wrapper_budget()->value()),
               i - 1);
    }

    // Get the wrapper-code object after the wrapper replacement.
    Tagged<Code> wrapper_after_call = main_function_data->wrapper_code(isolate);

    // Verify that the budget has been exhausted.
    CHECK_EQ(Smi::ToInt(main_function_data->wrapper_budget()->value()), 0);
    // Verify that the wrapper-code object has changed and the wrapper is now a
    // specific one.
    // TODO(saelo): here we have to use full pointer comparison while not all
    // Code objects have been moved into trusted space.
    static_assert(!kAllCodeObjectsLiveInTrustedSpace);
    CHECK(!wrapper_after_call.SafeEquals(*wrapper_before_call));
    CHECK(IsSpecific(wrapper_after_call));
  }
  Cleanup();
}

TEST(EagerWrapperReplacement) {
  {
    // This test assumes use of the generic wrapper.
    FlagScope<bool> use_wasm_generic_wrapper(&v8_flags.wasm_generic_wrapper,
                                             true);

    // Initialize the environment and create a module builder.
    AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);
    Isolate* isolate = CcTest::InitIsolateOnce();
    HandleScope scope(isolate);
    WasmModuleBuilder* builder = zone.New<WasmModuleBuilder>(&zone);

    // Define three Wasm functions.
    // Two of these functions (add and mult) will share the same signature,
    // while the other one (id) won't.
    TestSignatures sigs;
    WasmFunctionBuilder* add = builder->AddFunction(sigs.i_ii());
    add->builder()->AddExport(base::CStrVector("add"), add);
    add->EmitCode(
        {WASM_I32_ADD(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)), WASM_END});
    WasmFunctionBuilder* mult = builder->AddFunction(sigs.i_ii());
    mult->builder()->AddExport(base::CStrVector("mult"), mult);
    mult->EmitCode(
        {WASM_I32_MUL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(1)), WASM_END});
    WasmFunctionBuilder* id = builder->AddFunction(sigs.i_i());
    id->builder()->AddExport(base::CStrVector("id"), id);
    id->EmitCode({WASM_LOCAL_GET(0), WASM_END});

    // Compile the module.
    DirectHandle<WasmInstanceObject> instance =
        CompileModule(&zone, isolate, builder);

    // Get the exported functions.
    DirectHandle<WasmExportedFunction> add_export =
        testing::GetExportedFunction(isolate, instance, "add")
            .ToHandleChecked();
    DirectHandle<WasmExportedFunction> mult_export =
        testing::GetExportedFunction(isolate, instance, "mult")
            .ToHandleChecked();
    DirectHandle<WasmExportedFunction> id_export =
        testing::GetExportedFunction(isolate, instance, "id").ToHandleChecked();

    // Get the function data for all exported functions.
    DirectHandle<WasmExportedFunctionData> add_function_data(
        add_export->shared()->wasm_exported_function_data(), isolate);
    DirectHandle<WasmExportedFunctionData> mult_function_data(
        mult_export->shared()->wasm_exported_function_data(), isolate);
    DirectHandle<WasmExportedFunctionData> id_function_data(
        id_export->shared()->wasm_exported_function_data(), isolate);

    // Set the remaining generic-wrapper budget for add to 1,
    // so that the next call to it will cause the function to tier up.
    add_function_data->wrapper_budget()->set_value(Smi::FromInt(1));

    // Verify that the generic-wrapper budgets for all functions are correct.
    CHECK_EQ(Smi::ToInt(add_function_data->wrapper_budget()->value()), 1);
    CHECK_EQ(Smi::ToInt(mult_function_data->wrapper_budget()->value()),
             kGenericWrapperBudget);
    CHECK_EQ(Smi::ToInt(id_function_data->wrapper_budget()->value()),
             kGenericWrapperBudget);

    // Verify that all functions are set to use the generic wrapper.
    CHECK(IsGeneric(add_function_data->wrapper_code(isolate)));
    CHECK(IsGeneric(mult_function_data->wrapper_code(isolate)));
    CHECK(IsGeneric(id_function_data->wrapper_code(isolate)));

    // Call the add function to trigger the tier up.
    {
      DirectHandle<Object> params[] = {SmiHandle(isolate, 10),
                                       SmiHandle(isolate, 11)};
      SmiCall(isolate, add_export, base::VectorOf(params), 21);
      // Verify that the generic-wrapper budgets for all functions are correct.
      CHECK_EQ(Smi::ToInt(add_function_data->wrapper_budget()->value()), 0);
      CHECK_EQ(Smi::ToInt(mult_function_data->wrapper_budget()->value()),
               kGenericWrapperBudget);
      CHECK_EQ(Smi::ToInt(id_function_data->wrapper_budget()->value()),
               kGenericWrapperBudget);
      // Verify that the tier-up of the add function replaced the wrapper
      // for both the add and the mult functions, but not the id function.
      CHECK(IsSpecific(add_function_data->wrapper_code(isolate)));
      CHECK(IsSpecific(mult_function_data->wrapper_code(isolate)));
      CHECK(IsGeneric(id_function_data->wrapper_code(isolate)));
    }

    // Call the mult function to verify that the compiled wrapper is used.
    {
      DirectHandle<Object> params[] = {SmiHandle(isolate, 6),
                                       SmiHandle(isolate, 7)};
      SmiCall(isolate, mult_export, base::VectorOf(params), 42);
      // Verify that mult's budget is still intact, which means that the call
      // didn't go through the generic wrapper.
      CHECK_EQ(Smi::ToInt(mult_function_data->wrapper_budget()->value()),
               kGenericWrapperBudget);
    }

    // Call the id function to verify that the generic wrapper is used.
    {
      DirectHandle<Object> params[] = {SmiHandle(isolate, 6)};
      SmiCall(isolate, id_export, base::VectorOf(params), 6);
      // Verify that id's budget decreased by 1, which means that the call
      // used the generic wrapper.
      CHECK_EQ(Smi::ToInt(id_function_data->wrapper_budget()->value()),
               kGenericWrapperBudget - 1);
    }
  }
  Cleanup();
}

TEST(WrapperReplacement_IndirectExport) {
  {
    // This test assumes use of the generic wrapper.
    FlagScope<bool> use_wasm_generic_wrapper(&v8_flags.wasm_generic_wrapper,
                                             true);

    // Initialize the environment and create a module builder.
    AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);
    Isolate* isolate = CcTest::InitIsolateOnce();
    HandleScope scope(isolate);
    WasmModuleBuilder* builder = zone.New<WasmModuleBuilder>(&zone);

    // Define a Wasm function, but do not add it to the exports.
    TestSignatures sigs;
    WasmFunctionBuilder* f = builder->AddFunction(sigs.i_i());
    f->EmitCode({WASM_LOCAL_GET(0), WASM_END});
    uint32_t function_index = f->func_index();

    // Export a table of indirect functions.
    const uint32_t table_size = 2;
    const uint32_t table_index =
        builder->AddTable(kWasmFuncRef, table_size, table_size);
    builder->AddExport(base::CStrVector("exported_table"), kExternalTable, 0);

    // Point from the exported table to the Wasm function.
    builder->SetIndirectFunction(
        table_index, 0, function_index,
        WasmModuleBuilder::WasmElemSegment::kRelativeToImports);

    // Compile the module.
    DirectHandle<WasmInstanceObject> instance =
        CompileModule(&zone, isolate, builder);

    // Get the exported table.
    DirectHandle<WasmTableObject> table(
        Cast<WasmTableObject>(
            instance->trusted_data(isolate)->tables()->get(table_index)),
        isolate);
    // Get the Wasm function through the exported table.
    DirectHandle<WasmFuncRef> func_ref =
        Cast<WasmFuncRef>(WasmTableObject::Get(isolate, table, function_index));
    DirectHandle<WasmInternalFunction> internal_function{
        func_ref->internal(isolate), isolate};
    DirectHandle<WasmExportedFunction> indirect_function =
        Cast<WasmExportedFunction>(
            WasmInternalFunction::GetOrCreateExternal(internal_function));
    // Get the function data.
    DirectHandle<WasmExportedFunctionData> indirect_function_data(
        indirect_function->shared()->wasm_exported_function_data(), isolate);

    // Verify that the generic-wrapper budget has initially a value of
    // kGenericWrapperBudget and the wrapper to be used for calls to the
    // indirect function is the generic one.
    CHECK(IsGeneric(indirect_function_data->wrapper_code(isolate)));
    CHECK(Smi::ToInt(indirect_function_data->wrapper_budget()->value()) ==
          kGenericWrapperBudget);

    // Set the remaining generic-wrapper budget for the indirect function to 1,
    // so that the next call to it will cause the function to tier up.
    indirect_function_data->wrapper_budget()->set_value(Smi::FromInt(1));

    // Call the Wasm function.
    DirectHandle<Object> params[] = {SmiHandle(isolate, 6)};
    SmiCall(isolate, indirect_function, base::VectorOf(params), 6);

    // Verify that the budget is now exhausted and the generic wrapper has been
    // replaced by a specific one.
    CHECK_EQ(Smi::ToInt(indirect_function_data->wrapper_budget()->value()), 0);
    CHECK(IsSpecific(indirect_function_data->wrapper_code(isolate)));
  }
  Cleanup();
}

TEST(JSToWasmWrapperGarbageCollection) {
  Isolate* isolate = CcTest::InitIsolateOnce();
  auto NumCompiledJSToWasmWrappers = [isolate]() {
    int num_wrappers = 0;
    Tagged<WeakFixedArray> wrappers = isolate->heap()->js_to_wasm_wrappers();
    for (int i = 0, e = wrappers->length(); i < e; ++i) {
      // Entries are either weak code wrappers, cleared entries, or undefined.
      Tagged<MaybeObject> maybe_wrapper = wrappers->get(i);
      if (maybe_wrapper.IsCleared()) continue;
      CHECK(maybe_wrapper.IsWeak());
      CHECK(IsCodeWrapper(maybe_wrapper.GetHeapObjectAssumeWeak()));
      Tagged<Code> code =
          Cast<CodeWrapper>(maybe_wrapper.GetHeapObjectAssumeWeak())
              ->code(isolate);
      CHECK_EQ(CodeKind::JS_TO_WASM_FUNCTION, code->kind());
      ++num_wrappers;
    }
    return num_wrappers;
  };

  {
    // Initialize the environment and create a module builder.
    AccountingAllocator allocator;
    Zone zone{&allocator, ZONE_NAME};
    HandleScope scope{isolate};
    WasmModuleBuilder builder{&zone};

    // Define an exported Wasm function.
    TestSignatures sigs;
    WasmFunctionBuilder* f = builder.AddFunction(sigs.i_v());
    builder.AddExport(base::CStrVector("main"), f);
    f->EmitCode({WASM_ONE, WASM_END});

    // Before compilation there should be no compiled wrappers.
    CHECK_EQ(0, NumCompiledJSToWasmWrappers());

    // Compile the module.
    DirectHandle<WasmInstanceObject> instance =
        CompileModule(&zone, isolate, &builder);

    // If the generic wrapper is disabled, this should have compiled a wrapper.
    CHECK_EQ(v8_flags.wasm_generic_wrapper ? 0 : 1,
             NumCompiledJSToWasmWrappers());

    // Get the exported function and the function data.
    DirectHandle<WasmExportedFunction> main_function =
        testing::GetExportedFunction(isolate, instance, "main")
            .ToHandleChecked();
    DirectHandle<WasmExportedFunctionData> main_function_data(
        main_function->shared()->wasm_exported_function_data(), isolate);

    // Set the remaining generic-wrapper budget for add to 1,
    // so that the next call to it will cause the function to tier up.
    main_function_data->wrapper_budget()->set_value(Smi::FromInt(1));

    // Call the Wasm function.
    SmiCall(isolate, main_function, {}, 1);

    // There should be exactly one compiled wrapper now.
    CHECK_EQ(1, NumCompiledJSToWasmWrappers());
  }

  // After GC all compiled wrappers must be cleared again.
  {
    // Disable stack scanning in case CSS is being used to prevent references
    // that leaked to the C++ stack from keeping the wrapper alive.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(
        isolate->heap());
    Cleanup();
  }

  CHECK_EQ(0, NumCompiledJSToWasmWrappers());
}
#endif

}  // namespace test_run_wasm_wrappers
}  // namespace wasm
}  // namespace internal
}  // namespace v8

// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/fuzzer/wasm-fuzzer-common.h"

#include <ctime>

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-metrics.h"
#include "src/execution/isolate.h"
#include "src/utils/ostreams.h"
#include "src/wasm/baseline/liftoff-compiler.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/module-decoder-impl.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-feature-flags.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-opcodes-inl.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/zone.h"
#include "test/common/flag-utils.h"
#include "test/common/wasm/wasm-module-runner.h"
#include "test/fuzzer/fuzzer-support.h"

namespace v8::internal::wasm::fuzzing {

constexpr CompileTimeImports CompileTimeImportsForFuzzing() {
  return CompileTimeImports({CompileTimeImport::kJsString,
                             CompileTimeImport::kTextEncoder,
                             CompileTimeImport::kTextDecoder});
}

// Compile a baseline module. We pass a pointer to a max step counter and a
// nondeterminsm flag that are updated during execution by Liftoff.
Handle<WasmModuleObject> CompileReferenceModule(
    Isolate* isolate, base::Vector<const uint8_t> wire_bytes,
    int32_t* max_steps, int32_t* nondeterminism) {
  // Create the native module.
  std::shared_ptr<NativeModule> native_module;
  constexpr bool kNoVerifyFunctions = false;
  auto enabled_features = WasmFeatures::FromIsolate(isolate);
  ModuleResult module_res =
      DecodeWasmModule(enabled_features, wire_bytes, kNoVerifyFunctions,
                       ModuleOrigin::kWasmOrigin);
  CHECK(module_res.ok());
  std::shared_ptr<WasmModule> module = module_res.value();
  CHECK_NOT_NULL(module);
  WasmError imports_error = ValidateAndSetBuiltinImports(
      module.get(), wire_bytes, CompileTimeImportsForFuzzing());
  CHECK(!imports_error.has_error());  // The module was compiled before.
  native_module = GetWasmEngine()->NewNativeModule(
      isolate, enabled_features, CompileTimeImportsForFuzzing(), module, 0);
  native_module->SetWireBytes(base::OwnedVector<uint8_t>::Of(wire_bytes));
  // The module is known to be valid as this point (it was compiled by the
  // caller before).
  module->set_all_functions_validated();

  // Compile all functions with Liftoff.
  WasmCodeRefScope code_ref_scope;
  CompilationEnv env = CompilationEnv::ForModule(native_module.get());
  ModuleWireBytes wire_bytes_accessor{wire_bytes};
  for (size_t i = module->num_imported_functions; i < module->functions.size();
       ++i) {
    auto& func = module->functions[i];
    base::Vector<const uint8_t> func_code =
        wire_bytes_accessor.GetFunctionBytes(&func);
    constexpr bool kIsShared = false;
    FunctionBody func_body(func.sig, func.code.offset(), func_code.begin(),
                           func_code.end(), kIsShared);
    auto result =
        ExecuteLiftoffCompilation(&env, func_body,
                                  LiftoffOptions{}
                                      .set_func_index(func.func_index)
                                      .set_for_debugging(kForDebugging)
                                      .set_max_steps(max_steps)
                                      .set_nondeterminism(nondeterminism));
    if (!result.succeeded()) {
      FATAL(
          "Liftoff compilation failed on a valid module. Run with "
          "--trace-wasm-decoder (in a debug build) to see why.");
    }
    native_module->PublishCode(native_module->AddCompiledCode(result));
  }

  // Create the module object.
  constexpr base::Vector<const char> kNoSourceUrl;
  Handle<Script> script =
      GetWasmEngine()->GetOrCreateScript(isolate, native_module, kNoSourceUrl);
  isolate->heap()->EnsureWasmCanonicalRttsSize(module->MaxCanonicalTypeIndex() +
                                               1);
  return WasmModuleObject::New(isolate, std::move(native_module), script);
}

void ExecuteAgainstReference(Isolate* isolate,
                             Handle<WasmModuleObject> module_object,
                             int32_t max_executed_instructions) {
  // We do not instantiate the module if there is a start function, because a
  // start function can contain an infinite loop which we cannot handle.
  if (module_object->module()->start_function_index >= 0) return;

  int32_t max_steps = max_executed_instructions;
  int32_t nondeterminism = 0;

  HandleScope handle_scope(isolate);  // Avoid leaking handles.
  Zone reference_module_zone(isolate->allocator(), "wasm reference module");
  Handle<WasmModuleObject> module_ref = CompileReferenceModule(
      isolate, module_object->native_module()->wire_bytes(), &max_steps,
      &nondeterminism);
  Handle<WasmInstanceObject> instance_ref;

  // Try to instantiate the reference instance, return if it fails.
  {
    ErrorThrower thrower(isolate, "ExecuteAgainstReference");
    if (!GetWasmEngine()
             ->SyncInstantiate(isolate, &thrower, module_ref, {},
                               {})  // no imports & memory
             .ToHandle(&instance_ref)) {
      isolate->clear_exception();
      thrower.Reset();  // Ignore errors.
      return;
    }
  }

  // Get the "main" exported function. Do nothing if it does not exist.
  Handle<WasmExportedFunction> main_function;
  if (!testing::GetExportedFunction(isolate, instance_ref, "main")
           .ToHandle(&main_function)) {
    return;
  }

  struct OomCallbackData {
    Isolate* isolate;
    bool heap_limit_reached{false};
    size_t initial_limit{0};
  };
  OomCallbackData oom_callback_data{isolate};
  auto heap_limit_callback = [](void* raw_data, size_t current_limit,
                                size_t initial_limit) -> size_t {
    OomCallbackData* data = reinterpret_cast<OomCallbackData*>(raw_data);
    data->heap_limit_reached = true;
    data->isolate->TerminateExecution();
    data->initial_limit = initial_limit;
    // Return a slightly raised limit, just to make it to the next
    // interrupt check point, where execution will terminate.
    return initial_limit * 1.25;
  };
  isolate->heap()->AddNearHeapLimitCallback(heap_limit_callback,
                                            &oom_callback_data);

  base::OwnedVector<Handle<Object>> compiled_args =
      testing::MakeDefaultArguments(isolate, main_function->sig());
  std::unique_ptr<const char[]> exception_ref;
  int32_t result_ref = testing::CallWasmFunctionForTesting(
      isolate, instance_ref, "main", compiled_args.as_vector(), &exception_ref);
  bool execute = true;
  // Reached max steps, do not try to execute the test module as it might
  // never terminate.
  if (max_steps < 0) execute = false;
  // If there is nondeterminism, we cannot guarantee the behavior of the test
  // module, and in particular it may not terminate.
  if (nondeterminism != 0) execute = false;
  // Similar to max steps reached, also discard modules that need too much
  // memory.
  isolate->heap()->RemoveNearHeapLimitCallback(heap_limit_callback,
                                               oom_callback_data.initial_limit);
  if (oom_callback_data.heap_limit_reached) {
    execute = false;
    isolate->CancelTerminateExecution();
  }

  if (exception_ref) {
    if (strcmp(exception_ref.get(),
               "RangeError: Maximum call stack size exceeded") == 0) {
      // There was a stack overflow, which may happen nondeterministically. We
      // cannot guarantee the behavior of the test module, and in particular it
      // may not terminate.
      execute = false;
    }
  }
  if (!execute) {
    // Before discarding the module, see if Turbofan runs into any DCHECKs.
    TierUpAllForTesting(isolate, instance_ref->trusted_data(isolate));
    return;
  }

  // Instantiate a fresh instance for the actual (non-ref) execution.
  Handle<WasmInstanceObject> instance;
  {
    ErrorThrower thrower(isolate, "ExecuteAgainstReference (second)");
    // We instantiated before, so the second instantiation must also succeed.
    if (!GetWasmEngine()
             ->SyncInstantiate(isolate, &thrower, module_object, {},
                               {})  // no imports & memory
             .ToHandle(&instance)) {
      DCHECK(thrower.error());
      // The only reason to fail the second instantiation should be OOM. Make
      // this a proper OOM crash so that ClusterFuzz categorizes it as such.
      if (strstr(thrower.error_msg(), "Out of memory")) {
        V8::FatalProcessOutOfMemory(isolate, "Wasm fuzzer second instantiation",
                                    thrower.error_msg());
      }
      FATAL("Second instantiation failed unexpectedly: %s",
            thrower.error_msg());
    }
    DCHECK(!thrower.error());
  }

  std::unique_ptr<const char[]> exception;
  int32_t result = testing::CallWasmFunctionForTesting(
      isolate, instance, "main", compiled_args.as_vector(), &exception);

  if ((exception_ref != nullptr) != (exception != nullptr)) {
    FATAL("Exception mismatch! Expected: <%s>; got: <%s>",
          exception_ref ? exception_ref.get() : "<no exception>",
          exception ? exception.get() : "<no exception>");
  }

  if (!exception) {
    CHECK_EQ(result_ref, result);
  }
}

namespace {

struct PrintSig {
  const size_t num;
  const std::function<ValueType(size_t)> getter;
};

PrintSig PrintParameters(const FunctionSig* sig) {
  return {sig->parameter_count(), [=](size_t i) { return sig->GetParam(i); }};
}

PrintSig PrintReturns(const FunctionSig* sig) {
  return {sig->return_count(), [=](size_t i) { return sig->GetReturn(i); }};
}

std::string index_raw(uint32_t arg) {
  return arg < 128 ? std::to_string(arg)
                   : "wasmUnsignedLeb(" + std::to_string(arg) + ")";
}

std::string index(uint32_t arg) { return index_raw(arg) + ", "; }

std::string HeapTypeToJSByteEncoding(HeapType heap_type) {
  switch (heap_type.representation()) {
    case HeapType::kFunc:
      return "kFuncRefCode";
    case HeapType::kEq:
      return "kEqRefCode";
    case HeapType::kI31:
      return "kI31RefCode";
    case HeapType::kStruct:
      return "kStructRefCode";
    case HeapType::kArray:
      return "kArrayRefCode";
    case HeapType::kAny:
      return "kAnyRefCode";
    case HeapType::kExtern:
      return "kExternRefCode";
    case HeapType::kNone:
      return "kNullRefCode";
    case HeapType::kNoFunc:
      return "kNullFuncRefCode";
    case HeapType::kNoExtern:
      return "kNullExternRefCode";
    case HeapType::kBottom:
      UNREACHABLE();
    default:
      return index_raw(heap_type.ref_index());
  }
}

std::string HeapTypeToConstantName(HeapType heap_type) {
  switch (heap_type.representation()) {
    case HeapType::kFunc:
      return "kWasmFuncRef";
    case HeapType::kEq:
      return "kWasmEqRef";
    case HeapType::kI31:
      return "kWasmI31Ref";
    case HeapType::kStruct:
      return "kWasmStructRef";
    case HeapType::kArray:
      return "kWasmArrayRef";
    case HeapType::kExtern:
      return "kWasmExternRef";
    case HeapType::kAny:
      return "kWasmAnyRef";
    case HeapType::kNone:
      return "kWasmNullRef";
    case HeapType::kNoFunc:
      return "kWasmNullFuncRef";
    case HeapType::kNoExtern:
      return "kWasmNullExternRef";
    case HeapType::kExn:
      return "kWasmExnRef";
    case HeapType::kNoExn:
      return "kWasmNullExnRef";
    case HeapType::kBottom:
      UNREACHABLE();
    default:
      return std::to_string(heap_type.ref_index());
  }
}

std::string ValueTypeToConstantName(ValueType type) {
  switch (type.kind()) {
    case kI8:
      return "kWasmI8";
    case kI16:
      return "kWasmI16";
    case kI32:
      return "kWasmI32";
    case kI64:
      return "kWasmI64";
    case kF32:
      return "kWasmF32";
    case kF64:
      return "kWasmF64";
    case kS128:
      return "kWasmS128";
    case kRefNull:
      switch (type.heap_representation()) {
        case HeapType::kFunc:
          return "kWasmFuncRef";
        case HeapType::kEq:
          return "kWasmEqRef";
        case HeapType::kExtern:
          return "kWasmExternRef";
        case HeapType::kAny:
          return "kWasmAnyRef";
        case HeapType::kBottom:
          UNREACHABLE();
        case HeapType::kStruct:
        case HeapType::kArray:
        case HeapType::kI31:
        default:
          return "wasmRefNullType(" + HeapTypeToConstantName(type.heap_type()) +
                 ")";
      }
    case kRef:
      return "wasmRefType(" + HeapTypeToConstantName(type.heap_type()) + ")";
    case kRtt:
    case kVoid:
    case kBottom:
      UNREACHABLE();
  }
}

std::ostream& operator<<(std::ostream& os, const PrintSig& print) {
  os << "[";
  for (size_t i = 0; i < print.num; ++i) {
    os << (i == 0 ? "" : ", ") << ValueTypeToConstantName(print.getter(i));
  }
  return os << "]";
}

struct PrintName {
  WasmName name;
  PrintName(ModuleWireBytes wire_bytes, WireBytesRef ref)
      : name(wire_bytes.GetNameOrNull(ref)) {}
};
std::ostream& operator<<(std::ostream& os, const PrintName& name) {
  return os.put('\'').write(name.name.begin(), name.name.size()).put('\'');
}

// An interface for WasmFullDecoder which appends to a stream a textual
// representation of the expression, compatible with wasm-module-builder.js.
class InitExprInterface {
 public:
  using ValidationTag = Decoder::FullValidationTag;
  static constexpr DecodingMode decoding_mode = kConstantExpression;
  static constexpr bool kUsesPoppedArgs = false;

  struct Value : public ValueBase<ValidationTag> {
    template <typename... Args>
    explicit Value(Args&&... args) V8_NOEXCEPT
        : ValueBase(std::forward<Args>(args)...) {}
  };

  using Control = ControlBase<Value, ValidationTag>;
  using FullDecoder =
      WasmFullDecoder<ValidationTag, InitExprInterface, decoding_mode>;

  explicit InitExprInterface(StdoutStream& os) : os_(os) { os_ << "["; }

#define EMPTY_INTERFACE_FUNCTION(name, ...) \
  V8_INLINE void name(FullDecoder* decoder, ##__VA_ARGS__) {}
  INTERFACE_META_FUNCTIONS(EMPTY_INTERFACE_FUNCTION)
#undef EMPTY_INTERFACE_FUNCTION
#define UNREACHABLE_INTERFACE_FUNCTION(name, ...) \
  V8_INLINE void name(FullDecoder* decoder, ##__VA_ARGS__) { UNREACHABLE(); }
  INTERFACE_NON_CONSTANT_FUNCTIONS(UNREACHABLE_INTERFACE_FUNCTION)
#undef UNREACHABLE_INTERFACE_FUNCTION

  void I32Const(FullDecoder* decoder, Value* result, int32_t value) {
    os_ << "...wasmI32Const(" << value << "), ";
  }

  void I64Const(FullDecoder* decoder, Value* result, int64_t value) {
    os_ << "...wasmI64Const(" << value << "), ";
  }

  void F32Const(FullDecoder* decoder, Value* result, float value) {
    os_ << "...wasmF32Const(" << value << "), ";
  }

  void F64Const(FullDecoder* decoder, Value* result, double value) {
    os_ << "...wasmF64Const(" << value << "), ";
  }

  void S128Const(FullDecoder* decoder, Simd128Immediate& imm, Value* result) {
    os_ << "kSimdPrefix, kExprS128Const, " << std::hex;
    for (int i = 0; i < kSimd128Size; i++) {
      os_ << "0x" << static_cast<int>(imm.value[i]) << ", ";
    }
    os_ << std::dec;
  }

  void BinOp(FullDecoder* decoder, WasmOpcode opcode, const Value& lhs,
             const Value& rhs, Value* result) {
    switch (opcode) {
      case kExprI32Add:
        os_ << "kExprI32Add, ";
        break;
      case kExprI32Sub:
        os_ << "kExprI32Sub, ";
        break;
      case kExprI32Mul:
        os_ << "kExprI32Mul, ";
        break;
      case kExprI64Add:
        os_ << "kExprI64Add, ";
        break;
      case kExprI64Sub:
        os_ << "kExprI64Sub, ";
        break;
      case kExprI64Mul:
        os_ << "kExprI64Mul, ";
        break;
      default:
        UNREACHABLE();
    }
  }

  void UnOp(FullDecoder* decoder, WasmOpcode opcode, const Value& value,
            Value* result) {
    switch (opcode) {
      case kExprAnyConvertExtern:
        os_ << "kGCPrefix, kExprAnyConvertExtern, ";
        break;
      case kExprExternConvertAny:
        os_ << "kGCPrefix, kExprExternConvertAny, ";
        break;
      default:
        UNREACHABLE();
    }
  }

  void RefNull(FullDecoder* decoder, ValueType type, Value* result) {
    os_ << "kExprRefNull, " << HeapTypeToJSByteEncoding(type.heap_type())
        << ", ";
  }

  void RefFunc(FullDecoder* decoder, uint32_t function_index, Value* result) {
    os_ << "kExprRefFunc, " << index(function_index);
  }

  void GlobalGet(FullDecoder* decoder, Value* result,
                 const GlobalIndexImmediate& imm) {
    os_ << "kWasmGlobalGet, " << index(imm.index);
  }

  // The following operations assume non-rtt versions of the instructions.
  void StructNew(FullDecoder* decoder, const StructIndexImmediate& imm,
                 const Value args[], Value* result) {
    os_ << "kGCPrefix, kExprStructNew, " << index(imm.index);
  }

  void StructNewDefault(FullDecoder* decoder, const StructIndexImmediate& imm,
                        Value* result) {
    os_ << "kGCPrefix, kExprStructNewDefault, " << index(imm.index);
  }

  void ArrayNew(FullDecoder* decoder, const ArrayIndexImmediate& imm,
                const Value& length, const Value& initial_value,
                Value* result) {
    os_ << "kGCPrefix, kExprArrayNew, " << index(imm.index);
  }

  void ArrayNewDefault(FullDecoder* decoder, const ArrayIndexImmediate& imm,
                       const Value& length, Value* result) {
    os_ << "kGCPrefix, kExprArrayNewDefault, " << index(imm.index);
  }

  void ArrayNewFixed(FullDecoder* decoder, const ArrayIndexImmediate& array_imm,
                     const IndexImmediate& length_imm, const Value elements[],
                     Value* result) {
    os_ << "kGCPrefix, kExprArrayNewFixed, " << index(array_imm.index)
        << index(length_imm.index);
  }

  void ArrayNewSegment(FullDecoder* decoder,
                       const ArrayIndexImmediate& array_imm,
                       const IndexImmediate& data_segment_imm,
                       const Value& offset_value, const Value& length_value,
                       Value* result) {
    // TODO(14034): Implement when/if array.new_data/element becomes const.
    UNIMPLEMENTED();
  }

  void RefI31(FullDecoder* decoder, const Value& input, Value* result) {
    os_ << "kGCPrefix, kExprRefI31, ";
  }

  // Since we treat all instructions as rtt-less, we should not print rtts.
  void RttCanon(FullDecoder* decoder, uint32_t type_index, Value* result) {}

  void StringConst(FullDecoder* decoder, const StringConstImmediate& imm,
                   Value* result) {
    os_ << "...GCInstr(kExprStringConst), " << index(imm.index);
  }

  void DoReturn(FullDecoder* decoder, uint32_t /*drop_values*/) { os_ << "]"; }

 private:
  StdoutStream& os_;
};

void DecodeAndAppendInitExpr(StdoutStream& os, Zone* zone,
                             const WasmModule* module,
                             ModuleWireBytes module_bytes,
                             ConstantExpression init, ValueType expected) {
  switch (init.kind()) {
    case ConstantExpression::kEmpty:
      UNREACHABLE();
    case ConstantExpression::kI32Const:
      os << "wasmI32Const(" << init.i32_value() << ")";
      break;
    case ConstantExpression::kRefNull:
      os << "[kExprRefNull, " << HeapTypeToJSByteEncoding(HeapType(init.repr()))
         << "]";
      break;
    case ConstantExpression::kRefFunc:
      os << "[kExprRefFunc, " << index(init.index()) << "]";
      break;
    case ConstantExpression::kWireBytesRef: {
      WireBytesRef ref = init.wire_bytes_ref();
      auto sig = FixedSizeSignature<ValueType>::Returns(expected);
      constexpr bool kIsShared = false;  // TODO(14616): Extend this.
      FunctionBody body(&sig, ref.offset(), module_bytes.start() + ref.offset(),
                        module_bytes.start() + ref.end_offset(), kIsShared);
      WasmFeatures detected;
      WasmFullDecoder<Decoder::FullValidationTag, InitExprInterface,
                      kConstantExpression>
          decoder(zone, module, WasmFeatures::All(), &detected, body, os);
      decoder.DecodeFunctionBody();
      break;
    }
  }
}
}  // namespace

void GenerateTestCase(Isolate* isolate, ModuleWireBytes wire_bytes,
                      bool compiles) {
  constexpr bool kVerifyFunctions = false;
  auto enabled_features = WasmFeatures::FromIsolate(isolate);
  ModuleResult module_res = DecodeWasmModule(
      enabled_features, wire_bytes.module_bytes(), kVerifyFunctions,
      ModuleOrigin::kWasmOrigin, kPopulateExplicitRecGroups);
  CHECK_WITH_MSG(module_res.ok(), module_res.error().message().c_str());
  WasmModule* module = module_res.value().get();
  CHECK_NOT_NULL(module);

  AccountingAllocator allocator;
  Zone zone(&allocator, "constant expression zone");

  StdoutStream os;

  tzset();
  time_t current_time = time(nullptr);
  struct tm current_localtime;
#ifdef V8_OS_WIN
  localtime_s(&current_localtime, &current_time);
#else
  localtime_r(&current_time, &current_localtime);
#endif
  int year = 1900 + current_localtime.tm_year;

  os << "// Copyright " << year
     << " the V8 project authors. All rights reserved.\n"
        "// Use of this source code is governed by a BSD-style license that "
        "can be\n"
        "// found in the LICENSE file.\n"
        "\n"
        "// Flags: --wasm-staging\n"
        "\n"
        "d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');\n"
        "\n"
        "const builder = new WasmModuleBuilder();\n";

  int recursive_group_end = -1;
  for (int i = 0; i < static_cast<int>(module->types.size()); i++) {
    auto rec_group = module->explicit_recursive_type_groups.find(i);
    if (rec_group != module->explicit_recursive_type_groups.end()) {
      os << "builder.startRecGroup();\n";
      recursive_group_end = rec_group->first + rec_group->second - 1;
    }
    if (module->has_struct(i)) {
      const StructType* struct_type = module->types[i].struct_type;
      os << "builder.addStruct([";
      int field_count = struct_type->field_count();
      for (int index = 0; index < field_count; index++) {
        os << "makeField(" << ValueTypeToConstantName(struct_type->field(index))
           << ", " << (struct_type->mutability(index) ? "true" : "false")
           << ")";
        if (index + 1 < field_count) os << ", ";
      }
      os << "], ";
      if (module->types[i].supertype != kNoSuperType) {
        os << module->types[i].supertype;
      } else {
        os << "kNoSuperType";
      }
      os << ", " << (module->types[i].is_final ? "true" : "false") << ");\n";
    } else if (module->has_array(i)) {
      const ArrayType* array_type = module->types[i].array_type;
      os << "builder.addArray("
         << ValueTypeToConstantName(array_type->element_type()) << ", "
         << (array_type->mutability() ? "true" : "false") << ", ";
      if (module->types[i].supertype != kNoSuperType) {
        os << module->types[i].supertype;
      } else {
        os << "kNoSuperType";
      }
      os << ", " << (module->types[i].is_final ? "true" : "false") << ");\n";
    } else {
      DCHECK(module->has_signature(i));
      const FunctionSig* sig = module->types[i].function_sig;
      os << "builder.addType(makeSig(" << PrintParameters(sig) << ", "
         << PrintReturns(sig) << "));\n";
    }

    if (i == recursive_group_end) {
      os << "builder.endRecGroup();\n";
    }
  }

  for (WasmImport imported : module->import_table) {
    // TODO(wasm): Support other imports when needed.
    CHECK_EQ(kExternalFunction, imported.kind);
    auto module_name = PrintName(wire_bytes, imported.module_name);
    auto field_name = PrintName(wire_bytes, imported.field_name);
    int sig_index = module->functions[imported.index].sig_index;
    os << "builder.addImport(" << module_name << ", " << field_name << ", "
       << sig_index << " /* sig */);\n";
  }

  for (const WasmMemory& memory : module->memories) {
    os << "builder.addMemory(" << memory.initial_pages;
    if (memory.has_maximum_pages) {
      os << ", " << memory.maximum_pages;
    } else {
      os << ", undefined";
    }
    if (memory.is_shared) {
      os << ", true";
    }
    os << ");\n";
  }

  for (WasmDataSegment segment : module->data_segments) {
    base::Vector<const uint8_t> data = wire_bytes.module_bytes().SubVector(
        segment.source.offset(), segment.source.end_offset());
    if (segment.active) {
      // TODO(wasm): Add other expressions when needed.
      CHECK_EQ(ConstantExpression::kI32Const, segment.dest_addr.kind());
      os << "builder.addActiveDataSegment(0, ";
      DecodeAndAppendInitExpr(os, &zone, module, wire_bytes, segment.dest_addr,
                              kWasmI32);
      os << ", ";
    } else {
      os << "builder.addPassiveDataSegment(";
    }
    os << "[";
    if (!data.empty()) {
      os << unsigned{data[0]};
      for (unsigned byte : data + 1) os << ", " << byte;
    }
    os << "]";
    if (segment.shared) os << ", true";
    os << ");\n";
  }

  for (WasmGlobal& global : module->globals) {
    os << "builder.addGlobal(" << ValueTypeToConstantName(global.type) << ", "
       << global.mutability << ", " << global.shared << ", ";
    DecodeAndAppendInitExpr(os, &zone, module, wire_bytes, global.init,
                            global.type);
    os << ");\n";
  }

  Zone tmp_zone(isolate->allocator(), ZONE_NAME);

  for (const WasmTable& table : module->tables) {
    os << "builder.addTable(" << ValueTypeToConstantName(table.type) << ", "
       << table.initial_size << ", "
       << (table.has_maximum_size ? std::to_string(table.maximum_size)
                                  : "undefined")
       << ", ";
    if (table.initial_value.is_set()) {
      DecodeAndAppendInitExpr(os, &zone, module, wire_bytes,
                              table.initial_value, table.type);
    } else {
      os << "undefined";
    }
    if (table.shared) os << ", true";
    os << ");\n";
  }
  for (const WasmElemSegment& elem_segment : module->elem_segments) {
    const char* status_str =
        elem_segment.status == WasmElemSegment::kStatusActive
            ? "Active"
            : elem_segment.status == WasmElemSegment::kStatusPassive
                  ? "Passive"
                  : "Declarative";
    os << "builder.add" << status_str << "ElementSegment(";
    if (elem_segment.status == WasmElemSegment::kStatusActive) {
      os << elem_segment.table_index << ", ";
      DecodeAndAppendInitExpr(os, &zone, module, wire_bytes,
                              elem_segment.offset, kWasmI32);
      os << ", ";
    }
    os << "[";
    ModuleDecoderImpl decoder(WasmFeatures::All(),
                              wire_bytes.module_bytes().SubVectorFrom(
                                  elem_segment.elements_wire_bytes_offset),
                              ModuleOrigin::kWasmOrigin);
    for (uint32_t i = 0; i < elem_segment.element_count; i++) {
      ConstantExpression expr =
          decoder.consume_element_segment_entry(module, elem_segment);
      if (elem_segment.element_type == WasmElemSegment::kExpressionElements) {
        DecodeAndAppendInitExpr(os, &zone, module, wire_bytes, expr,
                                elem_segment.type);
      } else {
        os << expr.index();
      }
      if (i < elem_segment.element_count - 1) os << ", ";
    }
    os << "], "
       << (elem_segment.element_type == WasmElemSegment::kExpressionElements
               ? ValueTypeToConstantName(elem_segment.type)
               : "undefined");
    if (elem_segment.shared) os << ", true";
    os << ");\n";
  }

  for (const WasmTag& tag : module->tags) {
    os << "builder.addTag(makeSig(" << PrintParameters(tag.ToFunctionSig())
       << ", []));\n";
  }

  for (const WasmFunction& func : module->functions) {
    if (func.imported) continue;

    base::Vector<const uint8_t> func_code = wire_bytes.GetFunctionBytes(&func);
    os << "// Generate function " << (func.func_index + 1) << " (out of "
       << module->functions.size() << ").\n";

    // Add function.
    os << "builder.addFunction(undefined, " << func.sig_index
       << " /* sig */)\n";

    // Add locals.
    BodyLocalDecls decls;
    DecodeLocalDecls(enabled_features, &decls, func_code.begin(),
                     func_code.end(), &tmp_zone);
    if (decls.num_locals) {
      os << "  ";
      for (size_t pos = 0, count = 1, locals = decls.num_locals; pos < locals;
           pos += count, count = 1) {
        ValueType type = decls.local_types[pos];
        while (pos + count < locals && decls.local_types[pos + count] == type) {
          ++count;
        }
        os << ".addLocals(" << ValueTypeToConstantName(type) << ", " << count
           << ")";
      }
      os << "\n";
    }

    // Add body.
    os << "  .addBodyWithEnd([\n";

    constexpr bool kIsShared = false;  // TODO(14616): Extend this.
    FunctionBody func_body(func.sig, func.code.offset(), func_code.begin(),
                           func_code.end(), kIsShared);
    PrintRawWasmCode(isolate->allocator(), func_body, module, kOmitLocals);
    os << "]);\n";
  }

  for (WasmExport& exp : module->export_table) {
    switch (exp.kind) {
      case kExternalFunction:
        os << "builder.addExport(" << PrintName(wire_bytes, exp.name) << ", "
           << exp.index << ");\n";
        break;
      case kExternalMemory:
        os << "builder.exportMemoryAs(" << PrintName(wire_bytes, exp.name)
           << ", " << exp.index << ");\n";
        break;
      default:
        os << "// Unsupported export of '" << PrintName(wire_bytes, exp.name)
           << "'.\n";
        break;
    }
  }

  if (compiles) {
    os << "let kBuiltins = { builtins: ['js-string', 'text-decoder', "
          "'text-encoder'] };\n"
          "const instance = builder.instantiate({}, kBuiltins);\n"
          "try {\n"
          "  print(instance.exports.main(1, 2, 3));\n"
          "} catch (e) {\n"
          "  print('caught exception', e);\n"
          "}\n";
  } else {
    os << "assertThrows(function() { builder.instantiate(); }, "
          "WebAssembly.CompileError);\n";
  }
}

void EnableExperimentalWasmFeatures(v8::Isolate* isolate) {
  struct EnableExperimentalWasmFeatures {
    explicit EnableExperimentalWasmFeatures(v8::Isolate* isolate) {
      // Enable all staged features.
#define ENABLE_STAGED_FEATURES(feat, ...) \
  v8_flags.experimental_wasm_##feat = true;
      FOREACH_WASM_STAGING_FEATURE_FLAG(ENABLE_STAGED_FEATURES)
#undef ENABLE_STAGED_FEATURES

      // Enable non-staged experimental features that we also want to fuzz.
      // <currently none>
      // Note: If you add something here, you will also have to add the
      // respective flag(s) to the mjsunit/wasm/generate-random-module test.

      // Enforce implications from enabling features.
      FlagList::EnforceFlagImplications();

      // Last, install any conditional features. Implications are handled
      // implicitly.
      isolate->InstallConditionalFeatures(isolate->GetCurrentContext());
    }
  };
  // The compiler will properly synchronize the constructor call.
  static EnableExperimentalWasmFeatures one_time_enable_experimental_features(
      isolate);
}

void WasmExecutionFuzzer::FuzzWasmModule(base::Vector<const uint8_t> data,
                                         bool require_valid) {
  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();

  // Strictly enforce the input size limit. Note that setting "max_len" on the
  // fuzzer target is not enough, since different fuzzers are used and not all
  // respect that limit.
  if (data.size() > max_input_size()) return;

  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);

  v8::Isolate::Scope isolate_scope(isolate);

  // Clear recursive groups: The fuzzer creates random types in every run. These
  // are saved as recursive groups as part of the type canonicalizer, but types
  // from previous runs just waste memory.
  GetTypeCanonicalizer()->EmptyStorageForTesting();
  i_isolate->heap()->ClearWasmCanonicalRttsForTesting();

  // Clear any exceptions from a prior run.
  if (i_isolate->has_exception()) {
    i_isolate->clear_exception();
  }

  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());

  // We explicitly enable staged WebAssembly features here to increase fuzzer
  // coverage. For libfuzzer fuzzers it is not possible that the fuzzer enables
  // the flag by itself.
  EnableExperimentalWasmFeatures(isolate);

  v8::TryCatch try_catch(isolate);
  HandleScope scope(i_isolate);

  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  ZoneBuffer buffer(&zone);

  // The first byte specifies some internal configuration, like which function
  // is compiled with with compiler, and other flags.
  uint8_t configuration_byte = data.empty() ? 0 : data[0];
  if (!data.empty()) data += 1;

  // Derive the compiler configuration for the first four functions from the
  // configuration byte, to choose for each function between:
  // 0: TurboFan
  // 1: Liftoff
  // 2: Liftoff for debugging
  // 3: Turboshaft
  uint8_t tier_mask = 0;
  uint8_t debug_mask = 0;
  uint8_t turboshaft_mask = 0;
  for (int i = 0; i < 4; ++i, configuration_byte /= 4) {
    int compiler_config = configuration_byte % 4;
    tier_mask |= (compiler_config == 0) << i;
    debug_mask |= (compiler_config == 2) << i;
    turboshaft_mask |= (compiler_config == 3) << i;
  }
  // Enable tierup for all turboshaft functions.
  tier_mask |= turboshaft_mask;

  if (!GenerateModule(i_isolate, &zone, data, &buffer)) {
    return;
  }

  testing::SetupIsolateForWasmModule(i_isolate);

  ModuleWireBytes wire_bytes(buffer.begin(), buffer.end());

  auto enabled_features = WasmFeatures::FromIsolate(i_isolate);
  CompileTimeImports compile_imports = CompileTimeImportsForFuzzing();

  bool valid = GetWasmEngine()->SyncValidate(i_isolate, enabled_features,
                                             compile_imports, wire_bytes);

  if (v8_flags.wasm_fuzzer_gen_test) {
    GenerateTestCase(i_isolate, wire_bytes, valid);
  }

  FlagScope<bool> eager_compile(&v8_flags.wasm_lazy_compilation, false);
  // We want to keep dynamic tiering enabled because that changes the code
  // Liftoff generates as well as optimizing compilers' behavior (especially
  // around inlining). We switch it to synchronous mode to avoid the
  // nondeterminism of background jobs finishing at random times.
  FlagScope<bool> sync_tier_up(&v8_flags.wasm_sync_tier_up, true);
  // The purpose of setting the tier mask (which affects the initial
  // compilation of each function) is to deterministically test a combination
  // of Liftoff and Turbofan.
  FlagScope<int> tier_mask_scope(&v8_flags.wasm_tier_mask_for_testing,
                                 tier_mask);
  FlagScope<int> debug_mask_scope(&v8_flags.wasm_debug_mask_for_testing,
                                  debug_mask);
  FlagScope<int> turboshaft_mask_scope(
      &v8_flags.wasm_turboshaft_mask_for_testing, turboshaft_mask);

  ErrorThrower thrower(i_isolate, "WasmFuzzerSyncCompile");
  MaybeHandle<WasmModuleObject> compiled_module = GetWasmEngine()->SyncCompile(
      i_isolate, enabled_features, compile_imports, &thrower, wire_bytes);
  CHECK_EQ(valid, !compiled_module.is_null());
  CHECK_EQ(!valid, thrower.error());
  if (require_valid && !valid) {
    FATAL("Generated module should validate, but got: %s", thrower.error_msg());
  }
  thrower.Reset();

  if (valid) {
    ExecuteAgainstReference(i_isolate, compiled_module.ToHandleChecked(),
                            kDefaultMaxFuzzerExecutedInstructions);
  }
}

}  // namespace v8::internal::wasm::fuzzing

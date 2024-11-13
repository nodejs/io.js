// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime.h"

#include "src/base/hashmap.h"
#include "src/execution/isolate.h"
#include "src/runtime/runtime-utils.h"
#include "src/strings/string-hasher-inl.h"

namespace v8 {
namespace internal {

// Header of runtime functions.
#define F(name, number_of_args, result_size)                    \
  Address Runtime_##name(int args_length, Address* args_object, \
                         Isolate* isolate);
FOR_EACH_INTRINSIC_RETURN_OBJECT(F)
#undef F

#define P(name, number_of_args, result_size)                       \
  ObjectPair Runtime_##name(int args_length, Address* args_object, \
                            Isolate* isolate);
FOR_EACH_INTRINSIC_RETURN_PAIR(P)
#undef P

#define F(name, number_of_args, result_size)                                  \
  {                                                                           \
    Runtime::k##name, Runtime::RUNTIME, #name, FUNCTION_ADDR(Runtime_##name), \
        number_of_args, result_size                                           \
  }                                                                           \
  ,


#define I(name, number_of_args, result_size)                       \
  {                                                                \
    Runtime::kInline##name, Runtime::INLINE, "_" #name,            \
        FUNCTION_ADDR(Runtime_##name), number_of_args, result_size \
  }                                                                \
  ,

static const Runtime::Function kIntrinsicFunctions[] = {
    FOR_EACH_INTRINSIC(F) FOR_EACH_INLINE_INTRINSIC(I)};

#undef I
#undef F

namespace {

V8_DECLARE_ONCE(initialize_function_name_map_once);
static const base::CustomMatcherHashMap* kRuntimeFunctionNameMap;

struct IntrinsicFunctionIdentifier {
  IntrinsicFunctionIdentifier(const unsigned char* data, const int length)
      : data_(data), length_(length) {}

  static bool Match(void* key1, void* key2) {
    const IntrinsicFunctionIdentifier* lhs =
        static_cast<IntrinsicFunctionIdentifier*>(key1);
    const IntrinsicFunctionIdentifier* rhs =
        static_cast<IntrinsicFunctionIdentifier*>(key2);
    if (lhs->length_ != rhs->length_) return false;
    return CompareCharsEqual(lhs->data_, rhs->data_, rhs->length_);
  }

  uint32_t Hash() {
    return StringHasher::HashSequentialString<uint8_t>(
        data_, length_, v8::internal::kZeroHashSeed);
  }

  const unsigned char* data_;
  const int length_;
};

void InitializeIntrinsicFunctionNames() {
  base::CustomMatcherHashMap* function_name_map =
      new base::CustomMatcherHashMap(IntrinsicFunctionIdentifier::Match);
  for (size_t i = 0; i < arraysize(kIntrinsicFunctions); ++i) {
    const Runtime::Function* function = &kIntrinsicFunctions[i];
    IntrinsicFunctionIdentifier* identifier = new IntrinsicFunctionIdentifier(
        reinterpret_cast<const unsigned char*>(function->name),
        static_cast<int>(strlen(function->name)));
    base::HashMap::Entry* entry =
        function_name_map->InsertNew(identifier, identifier->Hash());
    entry->value = const_cast<Runtime::Function*>(function);
  }
  kRuntimeFunctionNameMap = function_name_map;
}

}  // namespace

bool Runtime::NeedsExactContext(FunctionId id) {
  switch (id) {
    case Runtime::kInlineAsyncFunctionReject:
    case Runtime::kInlineAsyncFunctionResolve:
      // For %_AsyncFunctionReject and %_AsyncFunctionResolve we don't
      // really need the current context, which in particular allows
      // us to usually eliminate the catch context for the implicit
      // try-catch in async function.
      return false;
    case Runtime::kCreatePrivateAccessors:
    case Runtime::kCopyDataProperties:
    case Runtime::kCreateDataProperty:
    case Runtime::kCreatePrivateNameSymbol:
    case Runtime::kCreatePrivateBrandSymbol:
    case Runtime::kLoadPrivateGetter:
    case Runtime::kLoadPrivateSetter:
    case Runtime::kReThrow:
    case Runtime::kReThrowWithMessage:
    case Runtime::kThrow:
    case Runtime::kThrowApplyNonFunction:
    case Runtime::kThrowCalledNonCallable:
    case Runtime::kThrowConstAssignError:
    case Runtime::kThrowConstructorNonCallableError:
    case Runtime::kThrowConstructedNonConstructable:
    case Runtime::kThrowConstructorReturnedNonObject:
    case Runtime::kThrowInvalidStringLength:
    case Runtime::kThrowInvalidTypedArrayAlignment:
    case Runtime::kThrowIteratorError:
    case Runtime::kThrowIteratorResultNotAnObject:
    case Runtime::kThrowNotConstructor:
    case Runtime::kThrowRangeError:
    case Runtime::kThrowReferenceError:
    case Runtime::kThrowAccessedUninitializedVariable:
    case Runtime::kThrowStackOverflow:
    case Runtime::kThrowStaticPrototypeError:
    case Runtime::kThrowSuperAlreadyCalledError:
    case Runtime::kThrowSuperNotCalled:
    case Runtime::kThrowSymbolAsyncIteratorInvalid:
    case Runtime::kThrowSymbolIteratorInvalid:
    case Runtime::kThrowThrowMethodMissing:
    case Runtime::kThrowTypeError:
    case Runtime::kThrowUnsupportedSuperError:
    case Runtime::kTerminateExecution:
#if V8_ENABLE_WEBASSEMBLY
    case Runtime::kThrowWasmError:
    case Runtime::kThrowWasmStackOverflow:
#endif  // V8_ENABLE_WEBASSEMBLY
      return false;
    default:
      return true;
  }
}

bool Runtime::IsNonReturning(FunctionId id) {
  switch (id) {
    case Runtime::kThrowUnsupportedSuperError:
    case Runtime::kThrowConstructorNonCallableError:
    case Runtime::kThrowStaticPrototypeError:
    case Runtime::kThrowSuperAlreadyCalledError:
    case Runtime::kThrowSuperNotCalled:
    case Runtime::kReThrow:
    case Runtime::kReThrowWithMessage:
    case Runtime::kThrow:
    case Runtime::kThrowApplyNonFunction:
    case Runtime::kThrowCalledNonCallable:
    case Runtime::kThrowConstructedNonConstructable:
    case Runtime::kThrowConstructorReturnedNonObject:
    case Runtime::kThrowInvalidStringLength:
    case Runtime::kThrowInvalidTypedArrayAlignment:
    case Runtime::kThrowIteratorError:
    case Runtime::kThrowIteratorResultNotAnObject:
    case Runtime::kThrowThrowMethodMissing:
    case Runtime::kThrowSymbolIteratorInvalid:
    case Runtime::kThrowNotConstructor:
    case Runtime::kThrowRangeError:
    case Runtime::kThrowReferenceError:
    case Runtime::kThrowAccessedUninitializedVariable:
    case Runtime::kThrowStackOverflow:
    case Runtime::kThrowSymbolAsyncIteratorInvalid:
    case Runtime::kThrowTypeError:
    case Runtime::kThrowConstAssignError:
    case Runtime::kTerminateExecution:
#if V8_ENABLE_WEBASSEMBLY
    case Runtime::kThrowWasmError:
    case Runtime::kThrowWasmStackOverflow:
#endif  // V8_ENABLE_WEBASSEMBLY
      return true;
    default:
      return false;
  }
}

bool Runtime::MayAllocate(FunctionId id) {
  switch (id) {
    case Runtime::kCompleteInobjectSlackTracking:
    case Runtime::kCompleteInobjectSlackTrackingForMap:
    case Runtime::kGlobalPrint:
      return false;
    default:
      return true;
  }
}

bool Runtime::IsEnabledForFuzzing(FunctionId id) {
  CHECK(v8_flags.fuzzing);

  // In general, all runtime functions meant for testing should also be exposed
  // to the fuzzers. That way, the fuzzers are able to import and mutate
  // regression tests that use those functions. Internal runtime functions
  // (which are e.g. only called from other builtins, etc.) should not directly
  // be exposed as they are not meant to be called directly from JavaScript.
  // However, exceptions exist: some test functions cannot be used for certain
  // types of fuzzing (e.g. differential fuzzing), or would cause false
  // positive crashes and therefore should not be exposed to fuzzers at all.

  // For differential fuzzing, only a handful of functions are allowed,
  // everything else is disabled. Many runtime functions are unsuited for
  // differential fuzzing as they for example expose internal engine state
  // (e.g. functions such as %HasFastProperties). To avoid having to maintain a
  // large denylist of such functions, we instead use an allowlist for
  // differential fuzzing.
  bool is_differential_fuzzing =
      v8_flags.allow_natives_for_differential_fuzzing;
  if (is_differential_fuzzing) {
    switch (id) {
      case Runtime::kArrayBufferDetach:
      case Runtime::kDeoptimizeFunction:
      case Runtime::kDeoptimizeNow:
      case Runtime::kDisableOptimizationFinalization:
      case Runtime::kEnableCodeLoggingForTesting:
      case Runtime::kFinalizeOptimization:
      case Runtime::kGetUndetectable:
      case Runtime::kNeverOptimizeFunction:
      case Runtime::kOptimizeFunctionOnNextCall:
      case Runtime::kOptimizeMaglevOnNextCall:
      case Runtime::kOptimizeOsr:
      case Runtime::kPrepareFunctionForOptimization:
      case Runtime::kPretenureAllocationSite:
      case Runtime::kSetAllocationTimeout:
      case Runtime::kSetForceSlowPath:
      case Runtime::kSimulateNewspaceFull:
      case Runtime::kWaitForBackgroundOptimization:
      case Runtime::kSetBatterySaverMode:
      case Runtime::kSetPriorityBestEffort:
      case Runtime::kSetPriorityUserVisible:
      case Runtime::kSetPriorityUserBlocking:
      case Runtime::kIsEfficiencyModeEnabled:
      case Runtime::kBaselineOsr:
      case Runtime::kCompileBaseline:
#if V8_ENABLE_WEBASSEMBLY && !OFFICIAL_BUILD
      case Runtime::kWasmGenerateRandomModule:
#endif  // V8_ENABLE_WEBASSEMBLY && !OFFICIAL_BUILD
#if V8_ENABLE_WEBASSEMBLY
      case Runtime::kWasmStruct:
      case Runtime::kWasmArray:
#endif  // V8_ENABLE_WEBASSEMBLY
        return true;

      default:
        return false;
    }
  }

  // Runtime functions disabled for all/most types of fuzzing.
  // Reasons for a function to be in this list include that it is not useful
  // for fuzzing (e.g. %DebugPrint) or not fuzzing-safe and therefore would
  // cause false-positive crashes (e.g. %AbortJS).
  switch (id) {
    case Runtime::kAbort:
    case Runtime::kAbortCSADcheck:
    case Runtime::kAbortJS:
    case Runtime::kSystemBreak:
    case Runtime::kBenchMaglev:
    case Runtime::kBenchTurbofan:
    case Runtime::kDebugPrint:
    case Runtime::kDisassembleFunction:
    case Runtime::kGetCallable:
    case Runtime::kTurbofanStaticAssert:
    case Runtime::kClearFunctionFeedback:
#ifdef V8_ENABLE_WEBASSEMBLY
    case Runtime::kWasmTraceEnter:
    case Runtime::kWasmTraceExit:
    case Runtime::kCheckIsOnCentralStack:
    case Runtime::kSetWasmInstantiateControls:
    case Runtime::kWasmNull:
    case Runtime::kFreezeWasmLazyCompilation:
#endif  // V8_ENABLE_WEBASSEMBLY
    // TODO(353685107): investigate whether these should be exposed to fuzzers.
    case Runtime::kConstructDouble:
    case Runtime::kConstructConsString:
    case Runtime::kConstructSlicedString:
    case Runtime::kConstructInternalizedString:
    case Runtime::kConstructThinString:
    // TODO(353971258): investigate whether this should be exposed to fuzzers.
    case Runtime::kSerializeDeserializeNow:
    // TODO(353928347): investigate whether this should be exposed to fuzzers.
    case Runtime::kCompleteInobjectSlackTracking:
    // TODO(354005312): investigate whether this should be exposed to fuzzers.
    case Runtime::kShareObject:
    // TODO(354310130): investigate whether this should be exposed to fuzzers.
    case Runtime::kForceFlush:
      return false;

    case Runtime::kLeakHole:
      return v8_flags.hole_fuzzing;

    default:
      break;
  }

  // The default case: test functions are exposed, everything else is not.
  switch (id) {
#define F(name, nargs, ressize) case k##name:
#define I(name, nargs, ressize) case kInline##name:
    FOR_EACH_INTRINSIC_TEST(F, I)
    IF_WASM(FOR_EACH_INTRINSIC_WASM_TEST, F, I)
#undef I
#undef F
    return true;
    default:
      return false;
  }
}

const Runtime::Function* Runtime::FunctionForName(const unsigned char* name,
                                                  int length) {
  base::CallOnce(&initialize_function_name_map_once,
                 &InitializeIntrinsicFunctionNames);
  IntrinsicFunctionIdentifier identifier(name, length);
  base::HashMap::Entry* entry =
      kRuntimeFunctionNameMap->Lookup(&identifier, identifier.Hash());
  if (entry) {
    return reinterpret_cast<Function*>(entry->value);
  }
  return nullptr;
}


const Runtime::Function* Runtime::FunctionForEntry(Address entry) {
  for (size_t i = 0; i < arraysize(kIntrinsicFunctions); ++i) {
    if (entry == kIntrinsicFunctions[i].entry) {
      return &(kIntrinsicFunctions[i]);
    }
  }
  return nullptr;
}


const Runtime::Function* Runtime::FunctionForId(Runtime::FunctionId id) {
  return &(kIntrinsicFunctions[static_cast<int>(id)]);
}

const Runtime::Function* Runtime::RuntimeFunctionTable(Isolate* isolate) {
#ifdef USE_SIMULATOR
  // When running with the simulator we need to provide a table which has
  // redirected runtime entry addresses.
  if (!isolate->runtime_state()->redirected_intrinsic_functions()) {
    size_t function_count = arraysize(kIntrinsicFunctions);
    Function* redirected_functions = new Function[function_count];
    memcpy(redirected_functions, kIntrinsicFunctions,
           sizeof(kIntrinsicFunctions));
    for (size_t i = 0; i < function_count; i++) {
      ExternalReference redirected_entry =
          ExternalReference::Create(static_cast<Runtime::FunctionId>(i));
      redirected_functions[i].entry = redirected_entry.address();
    }
    isolate->runtime_state()->set_redirected_intrinsic_functions(
        redirected_functions);
  }

  return isolate->runtime_state()->redirected_intrinsic_functions();
#else
  return kIntrinsicFunctions;
#endif
}

std::ostream& operator<<(std::ostream& os, Runtime::FunctionId id) {
  return os << Runtime::FunctionForId(id)->name;
}

int g_num_isolates_for_testing = 1;

}  // namespace internal
}  // namespace v8

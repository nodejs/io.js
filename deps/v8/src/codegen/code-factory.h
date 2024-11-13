// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_CODE_FACTORY_H_
#define V8_CODEGEN_CODE_FACTORY_H_

#include "src/codegen/callable.h"
#include "src/codegen/interface-descriptors.h"
#include "src/common/globals.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

// For ArrayNoArgumentConstructor and ArraySingleArgumentConstructor.
enum AllocationSiteOverrideMode {
  DONT_OVERRIDE,
  DISABLE_ALLOCATION_SITES,
};

class V8_EXPORT_PRIVATE CodeFactory final {
 public:
  // CEntry has var-args semantics (all the arguments are passed on the
  // stack and the arguments count is passed via register) which currently
  // can't be expressed in CallInterfaceDescriptor. Therefore only the code
  // is exported here.
  static Handle<Code> RuntimeCEntry(Isolate* isolate, int result_size = 1,
                                    bool switch_to_central_stack = false);

  static Handle<Code> CEntry(Isolate* isolate, int result_size = 1,
                             ArgvMode argv_mode = ArgvMode::kStack,
                             bool builtin_exit_frame = false,
                             bool switch_to_central_stack = false);

  // Initial states for ICs.
  static Callable LoadGlobalIC(Isolate* isolate, TypeofMode typeof_mode);
  static Callable LoadGlobalICInOptimizedCode(Isolate* isolate,
                                              TypeofMode typeof_mode);
  static Callable DefineNamedOwnIC(Isolate* isolate);
  static Callable DefineNamedOwnICInOptimizedCode(Isolate* isolate);

  static Callable CallApiCallback(Isolate* isolate);

  static Callable StringAdd(Isolate* isolate,
                            StringAddFlags flags = STRING_ADD_CHECK_NONE);

  static Callable FastNewFunctionContext(Isolate* isolate,
                                         ScopeType scope_type);

  static Callable Call(Isolate* isolate,
                       ConvertReceiverMode mode = ConvertReceiverMode::kAny);
  // TODO(ishell): not used, remove.
  static Callable Call_WithFeedback(Isolate* isolate, ConvertReceiverMode mode);
  static Callable CallWithArrayLike(Isolate* isolate);
  static Callable CallWithSpread(Isolate* isolate);
  static Callable CallFunction(
      Isolate* isolate, ConvertReceiverMode mode = ConvertReceiverMode::kAny);

  static Callable CallForwardVarargs(Isolate* isolate);
  static Callable CallFunctionForwardVarargs(Isolate* isolate);
  static Callable Construct(Isolate* isolate);
  static Callable ConstructWithSpread(Isolate* isolate);
  static Callable ConstructForwardVarargs(Isolate* isolate);
  static Callable ConstructFunctionForwardVarargs(Isolate* isolate);

  static Callable ArrayNoArgumentConstructor(
      Isolate* isolate, ElementsKind kind,
      AllocationSiteOverrideMode override_mode);
  static Callable ArraySingleArgumentConstructor(
      Isolate* isolate, ElementsKind kind,
      AllocationSiteOverrideMode override_mode);

#ifdef V8_IS_TSAN
  static Builtin GetTSANStoreStub(SaveFPRegsMode fp_mode, int size,
                                  std::memory_order order);
  static Builtin GetTSANRelaxedLoadStub(SaveFPRegsMode fp_mode, int size);
#endif  // V8_IS_TSAN
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_CODE_FACTORY_H_

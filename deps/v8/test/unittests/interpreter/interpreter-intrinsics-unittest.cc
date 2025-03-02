// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/interpreter-intrinsics.h"

#include "src/api/api-inl.h"
#include "src/heap/heap-inl.h"
#include "src/init/v8.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/interpreter/interpreter-tester.h"

namespace v8 {
namespace internal {
namespace interpreter {

namespace {

class InvokeIntrinsicHelper {
 public:
  InvokeIntrinsicHelper(Isolate* isolate, Zone* zone,
                        Runtime::FunctionId function_id)
      : isolate_(isolate),
        zone_(zone),
        factory_(isolate->factory()),
        function_id_(function_id) {}

  template <class... A>
  DirectHandle<Object> Invoke(A... args) {
    CHECK(IntrinsicsHelper::IsSupported(function_id_));
    int parameter_count = sizeof...(args);
    // Move the parameter to locals, since the order of the
    // arguments in the stack is reversed.
    BytecodeArrayBuilder builder(zone_, parameter_count + 1, parameter_count,
                                 nullptr);
    for (int i = 0; i < parameter_count; i++) {
      builder.MoveRegister(builder.Parameter(i), builder.Local(i));
    }
    RegisterList reg_list =
        InterpreterTester::NewRegisterList(0, parameter_count);
    builder.CallRuntime(function_id_, reg_list).Return();
    InterpreterTester tester(isolate_, builder.ToBytecodeArray(isolate_));
    auto callable = tester.GetCallable<A...>();
    return callable(args...).ToHandleChecked();
  }

  DirectHandle<Object> NewObject(const char* script) {
    return v8::Utils::OpenDirectHandle(*CompileRun(script));
  }

  DirectHandle<Object> Undefined() { return factory_->undefined_value(); }
  DirectHandle<Object> Null() { return factory_->null_value(); }

 private:
  Isolate* isolate_;
  Zone* zone_;
  Factory* factory_;
  Runtime::FunctionId function_id_;
};

}  // namespace

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

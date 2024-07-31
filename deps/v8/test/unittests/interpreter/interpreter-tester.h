// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEST_UNITTESTS_INTERPRETER_INTERPRETER_TESTER_H_
#define V8_TEST_UNITTESTS_INTERPRETER_INTERPRETER_TESTER_H_

#include "include/v8-function.h"
#include "src/api/api.h"
#include "src/execution/execution.h"
#include "src/handles/handles.h"
#include "src/init/v8.h"
#include "src/interpreter/bytecode-array-builder.h"
#include "src/interpreter/interpreter.h"
#include "src/objects/feedback-cell.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {
namespace interpreter {

template <class... A>
static MaybeHandle<Object> CallInterpreter(Isolate* isolate,
                                           Handle<JSFunction> function,
                                           Handle<Object> receiver, A... args) {
  // Pad the array with an empty handle to ensure that argv size is at least 1.
  // It avoids MSVC error C2466.
  Handle<Object> argv[] = {args..., Handle<Object>()};
  return Execution::Call(isolate, function, receiver, sizeof...(args), argv);
}

template <class... A>
class InterpreterCallable {
 public:
  virtual ~InterpreterCallable() = default;

  Tagged<FeedbackVector> vector() const { return function_->feedback_vector(); }

 protected:
  InterpreterCallable(Isolate* isolate, Handle<JSFunction> function)
      : isolate_(isolate), function_(function) {}

  Isolate* isolate_;
  Handle<JSFunction> function_;
};

template <class... A>
class InterpreterCallableUndefinedReceiver : public InterpreterCallable<A...> {
 public:
  InterpreterCallableUndefinedReceiver(Isolate* isolate,
                                       Handle<JSFunction> function)
      : InterpreterCallable<A...>(isolate, function) {}

  MaybeHandle<Object> operator()(A... args) {
    return CallInterpreter(this->isolate_, this->function_,
                           this->isolate_->factory()->undefined_value(),
                           args...);
  }
};

template <class... A>
class InterpreterCallableWithReceiver : public InterpreterCallable<A...> {
 public:
  InterpreterCallableWithReceiver(Isolate* isolate, Handle<JSFunction> function)
      : InterpreterCallable<A...>(isolate, function) {}

  MaybeHandle<Object> operator()(Handle<Object> receiver, A... args) {
    return CallInterpreter(this->isolate_, this->function_, receiver, args...);
  }
};

static inline v8::Local<v8::Value> CompileRun(const char* source) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Script> script =
      v8::Script::Compile(
          context, v8::String::NewFromUtf8(isolate, source).ToLocalChecked())
          .ToLocalChecked();
  return script->Run(context).ToLocalChecked();
}

class InterpreterTester {
 public:
  InterpreterTester(Isolate* isolate, const char* source,
                    MaybeHandle<BytecodeArray> bytecode,
                    MaybeHandle<FeedbackMetadata> feedback_metadata,
                    const char* filter);

  InterpreterTester(Isolate* isolate, Handle<BytecodeArray> bytecode,
                    MaybeHandle<FeedbackMetadata> feedback_metadata =
                        MaybeHandle<FeedbackMetadata>(),
                    const char* filter = kFunctionName);

  InterpreterTester(Isolate* isolate, const char* source,
                    const char* filter = kFunctionName);

  virtual ~InterpreterTester();
  InterpreterTester(const InterpreterTester&) = delete;
  InterpreterTester& operator=(const InterpreterTester&) = delete;

  template <class... A>
  InterpreterCallableUndefinedReceiver<A...> GetCallable() {
    return InterpreterCallableUndefinedReceiver<A...>(
        isolate_, GetBytecodeFunction<A...>());
  }

  template <class... A>
  InterpreterCallableWithReceiver<A...> GetCallableWithReceiver() {
    return InterpreterCallableWithReceiver<A...>(isolate_,
                                                 GetBytecodeFunction<A...>());
  }

  Local<Message> CheckThrowsReturnMessage();

  static Handle<Object> NewObject(const char* script);

  static Handle<String> GetName(Isolate* isolate, const char* name);

  static std::string SourceForBody(const char* body);

  static std::string function_name();

  static const char kFunctionName[];

  // Expose raw RegisterList construction to tests.
  static RegisterList NewRegisterList(int first_reg_index, int register_count) {
    return RegisterList(first_reg_index, register_count);
  }

  inline bool HasFeedbackMetadata() { return !feedback_metadata_.is_null(); }

 private:
  Isolate* isolate_;
  const char* source_;
  MaybeHandle<BytecodeArray> bytecode_;
  MaybeHandle<FeedbackMetadata> feedback_metadata_;

  template <class... A>
  Handle<JSFunction> GetBytecodeFunction() {
    Handle<JSFunction> function;
    IsCompiledScope is_compiled_scope;
    v8::Isolate* isolate = reinterpret_cast<v8::Isolate*>(isolate_);
    if (source_) {
      CompileRun(source_);
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      Local<Function> api_function = Local<Function>::Cast(
          context->Global()
              ->Get(context, v8::String::NewFromUtf8(isolate, kFunctionName)
                                 .ToLocalChecked())

              .ToLocalChecked());
      function = Handle<JSFunction>::cast(v8::Utils::OpenHandle(*api_function));
      is_compiled_scope = function->shared()->is_compiled_scope(isolate_);
    } else {
      int arg_count = sizeof...(A);
      std::string source("(function " + function_name() + "(");
      for (int i = 0; i < arg_count; i++) {
        source += i == 0 ? "a" : ", a";
      }
      source += "){})";
      function = Handle<JSFunction>::cast(v8::Utils::OpenHandle(
          *v8::Local<v8::Function>::Cast(CompileRun(source.c_str()))));
      function->set_code(*BUILTIN_CODE(isolate_, InterpreterEntryTrampoline));
      is_compiled_scope = function->shared()->is_compiled_scope(isolate_);
    }

    if (!bytecode_.is_null()) {
      function->shared()->overwrite_bytecode_array(
          *bytecode_.ToHandleChecked());
      is_compiled_scope = function->shared()->is_compiled_scope(isolate_);
    }
    if (HasFeedbackMetadata()) {
      function->set_raw_feedback_cell(isolate_->heap()->many_closures_cell());
      // Set the raw feedback metadata to circumvent checks that we are not
      // overwriting existing metadata.
      function->shared()->set_raw_outer_scope_info_or_feedback_metadata(
          *feedback_metadata_.ToHandleChecked());
      JSFunction::EnsureFeedbackVector(isolate_, function, &is_compiled_scope);
    }
    return function;
  }
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_TEST_UNITTESTS_INTERPRETER_INTERPRETER_TESTER_H_

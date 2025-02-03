// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <windows.h>

// This has to come after windows.h.
#include <versionhelpers.h>  // For IsWindows8OrGreater().

#include "include/v8-external.h"
#include "include/v8-function.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-template.h"
#include "src/base/macros.h"
#include "test/cctest/cctest.h"

#if defined(V8_OS_WIN_X64)  // Native x64 compilation
#define CONTEXT_PC(context) (context.Rip)
#elif defined(V8_OS_WIN_ARM64)
#if defined(V8_HOST_ARCH_ARM64)  // Native ARM64 compilation
#define CONTEXT_PC(context) (context.Pc)
#else  // x64 to ARM64 cross-compilation
#define CONTEXT_PC(context) (context.Rip)
#endif
#endif

class UnwindingWin64Callbacks {
 public:
  UnwindingWin64Callbacks() = default;

  static void Getter(v8::Local<v8::Name> name,
                     const v8::PropertyCallbackInfo<v8::Value>& info) {
    // Expects to find at least 15 stack frames in the call stack.
    // The stack walking should fail on stack frames for builtin functions if
    // stack unwinding data has not been correctly registered.
    int stack_frames = CountCallStackFrames(15);
    CHECK_GE(stack_frames, 15);
  }
  static void Setter(v8::Local<v8::Name> name, v8::Local<v8::Value> value,
                     const v8::PropertyCallbackInfo<void>& info) {}

 private:
  // Windows-specific code to walk the stack starting from the current
  // instruction pointer.
  static int CountCallStackFrames(int max_frames) {
    CONTEXT context_record;
    ::RtlCaptureContext(&context_record);

    int iframe = 0;
    while (++iframe < max_frames) {
      uint64_t image_base;
      PRUNTIME_FUNCTION function_entry = ::RtlLookupFunctionEntry(
          CONTEXT_PC(context_record), &image_base, nullptr);
      if (!function_entry) break;

      void* handler_data;
      uint64_t establisher_frame;
      ::RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base,
                         CONTEXT_PC(context_record), function_entry,
                         &context_record, &handler_data, &establisher_frame,
                         NULL);
    }
    return iframe;
  }
};

// Verifies that stack unwinding data has been correctly registered on Win64.
UNINITIALIZED_TEST(StackUnwindingWin64) {
#ifdef V8_WIN64_UNWINDING_INFO

  static const char* unwinding_win64_test_source =
      "function start(count) {\n"
      "  for (var i = 0; i < count; i++) {\n"
      "    var o = instance.foo;\n"
      "    instance.foo = o + 1;\n"
      "  }\n"
      "};\n"
      "%PrepareFunctionForOptimization(start);\n";

  // This test may fail on Windows 7
  if (!::IsWindows8OrGreater()) {
    return;
  }

  i::v8_flags.allow_natives_syntax = true;
  i::v8_flags.win64_unwinding_info = true;

  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  isolate->Enter();
  {
    v8::HandleScope scope(isolate);
    LocalContext env(isolate);

    v8::Local<v8::FunctionTemplate> func_template =
        v8::FunctionTemplate::New(isolate);
    v8::Local<v8::ObjectTemplate> instance_template =
        func_template->InstanceTemplate();

    UnwindingWin64Callbacks accessors;
    v8::Local<v8::External> data = v8::External::New(isolate, &accessors);
    instance_template->SetNativeDataProperty(
        v8_str("foo"), &UnwindingWin64Callbacks::Getter,
        &UnwindingWin64Callbacks::Setter, data);
    v8::Local<v8::Function> func =
        func_template->GetFunction(env.local()).ToLocalChecked();
    v8::Local<v8::Object> instance =
        func->NewInstance(env.local()).ToLocalChecked();
    env->Global()->Set(env.local(), v8_str("instance"), instance).FromJust();

    CompileRun(unwinding_win64_test_source);
    v8::Local<v8::Function> function = v8::Local<v8::Function>::Cast(
        env->Global()->Get(env.local(), v8_str("start")).ToLocalChecked());

    CompileRun("start(1); %OptimizeFunctionOnNextCall(start);");

    int32_t repeat_count = 100;
    v8::Local<v8::Value> args[] = {v8::Integer::New(isolate, repeat_count)};
    function->Call(env.local(), env.local()->Global(), arraysize(args), args)
        .ToLocalChecked();
  }
  isolate->Exit();
  isolate->Dispose();

#endif  // V8_WIN64_UNWINDING_INFO
}

#undef CONTEXT_PC

// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/fuzzer/fuzzer-support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/libplatform/libplatform.h"
#include "include/v8-context.h"
#include "include/v8-initialization.h"
#include "src/flags/flags.h"
#include "src/trap-handler/trap-handler.h"

namespace v8_fuzzer {

FuzzerSupport::FuzzerSupport(int* argc, char*** argv) {
  // Disable hard abort, which generates a trap instead of a proper abortion.
  // Traps by default do not cause libfuzzer to generate a crash file.
  i::v8_flags.hard_abort = false;

  i::v8_flags.expose_gc = true;
  i::v8_flags.fuzzing = true;

  // Allow changing flags in fuzzers.
  // TODO(12887): Refactor fuzzers to not change flags after initialization.
  i::v8_flags.freeze_flags_after_init = false;

#if V8_ENABLE_WEBASSEMBLY
  if (V8_TRAP_HANDLER_SUPPORTED) {
    constexpr bool kUseDefaultTrapHandler = true;
    if (!v8::V8::EnableWebAssemblyTrapHandler(kUseDefaultTrapHandler)) {
      FATAL("Could not register trap handler");
    }
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  v8::V8::SetFlagsFromCommandLine(argc, *argv, true);
  for (int arg_idx = 1; arg_idx < *argc; ++arg_idx) {
    const char* const arg = (*argv)[arg_idx];
    if (arg[0] != '-' || arg[1] != '-') continue;
    // Stop processing args at '--'.
    if (arg[2] == '\0') break;
    fprintf(stderr, "Unrecognized flag %s\n", arg);
    // Move remaining flags down.
    std::move(*argv + arg_idx + 1, *argv + *argc, *argv + arg_idx);
    --*argc, --arg_idx;
  }
  i::FlagList::ResolveContradictionsWhenFuzzing();

  v8::V8::InitializeICUDefaultLocation((*argv)[0]);
  v8::V8::InitializeExternalStartupData((*argv)[0]);
  platform_ = v8::platform::NewDefaultPlatform();
  v8::V8::InitializePlatform(platform_.get());
  v8::V8::Initialize();

  allocator_ = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = allocator_;
  create_params.allow_atomics_wait = false;
  isolate_ = v8::Isolate::New(create_params);

  {
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    context_.Reset(isolate_, v8::Context::New(isolate_));
  }
}

FuzzerSupport::~FuzzerSupport() {
  {
    v8::Isolate::Scope isolate_scope(isolate_);
    while (PumpMessageLoop()) {
      // empty
    }

    v8::HandleScope handle_scope(isolate_);
    context_.Reset();
  }

  isolate_->LowMemoryNotification();
  isolate_->Dispose();
  isolate_ = nullptr;

  delete allocator_;
  allocator_ = nullptr;

  v8::V8::Dispose();
  v8::V8::DisposePlatform();
}

std::unique_ptr<FuzzerSupport> FuzzerSupport::fuzzer_support_;

// static
void FuzzerSupport::InitializeFuzzerSupport(int* argc, char*** argv) {
  DCHECK_NULL(FuzzerSupport::fuzzer_support_);
  FuzzerSupport::fuzzer_support_ =
      std::make_unique<v8_fuzzer::FuzzerSupport>(argc, argv);
}

// static
FuzzerSupport* FuzzerSupport::Get() {
  DCHECK_NOT_NULL(FuzzerSupport::fuzzer_support_);
  return FuzzerSupport::fuzzer_support_.get();
}

v8::Local<v8::Context> FuzzerSupport::GetContext() {
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::EscapableHandleScope handle_scope(isolate_);
  v8::Local<v8::Context> context =
      v8::Local<v8::Context>::New(isolate_, context_);
  return handle_scope.Escape(context);
}

bool FuzzerSupport::PumpMessageLoop(
    v8::platform::MessageLoopBehavior behavior) {
  return v8::platform::PumpMessageLoop(platform_.get(), isolate_, behavior);
}

}  // namespace v8_fuzzer

// Explicitly specify some attributes to avoid issues with the linker dead-
// stripping the following function on macOS, as it is not called directly
// by fuzz target. LibFuzzer runtime uses dlsym() to resolve that function.
#if V8_OS_DARWIN
__attribute__((used)) __attribute__((visibility("default")))
#endif  // V8_OS_DARWIN
extern "C" int
LLVMFuzzerInitialize(int* argc, char*** argv) {
  v8_fuzzer::FuzzerSupport::InitializeFuzzerSupport(argc, argv);
  return 0;
}

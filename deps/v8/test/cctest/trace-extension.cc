// Copyright 2014 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "test/cctest/trace-extension.h"

#include "include/v8-profiler.h"
#include "include/v8-template.h"
#include "src/execution/vm-state-inl.h"
#include "src/objects/smi.h"
#include "src/profiler/tick-sample.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

const char* TraceExtension::kSource =
    "native function trace();"
    "native function js_trace();"
    "native function js_entry_sp();"
    "native function js_entry_sp_level2();";


v8::Local<v8::FunctionTemplate> TraceExtension::GetNativeFunctionTemplate(
    v8::Isolate* isolate, v8::Local<v8::String> name) {
  if (name->StrictEquals(v8::String::NewFromUtf8Literal(isolate, "trace"))) {
    return v8::FunctionTemplate::New(isolate, TraceExtension::Trace);
  } else if (name->StrictEquals(
                 v8::String::NewFromUtf8Literal(isolate, "js_trace"))) {
    return v8::FunctionTemplate::New(isolate, TraceExtension::JSTrace);
  } else if (name->StrictEquals(
                 v8::String::NewFromUtf8Literal(isolate, "js_entry_sp"))) {
    return v8::FunctionTemplate::New(isolate, TraceExtension::JSEntrySP);
  } else if (name->StrictEquals(v8::String::NewFromUtf8Literal(
                 isolate, "js_entry_sp_level2"))) {
    return v8::FunctionTemplate::New(isolate, TraceExtension::JSEntrySPLevel2);
  }
  UNREACHABLE();
}

Address TraceExtension::GetFP(const v8::FunctionCallbackInfo<v8::Value>& info) {
  // Convert frame pointer from encoding as smis in the arguments to a pointer.
  CHECK_EQ(2, info.Length());  // Ignore second argument on 32-bit platform.
  CHECK(i::ValidateCallbackInfo(info));
#if defined(V8_HOST_ARCH_32_BIT)
  Address fp = internal::ValueHelper::ValueAsAddress(*info[0]);
#elif defined(V8_HOST_ARCH_64_BIT)
  uint64_t kSmiValueMask =
      (static_cast<uintptr_t>(1) << (kSmiValueSize - 1)) - 1;
  uint64_t low_bits =
      Tagged<Smi>(internal::ValueHelper::ValueAsAddress(*info[0])).value() &
      kSmiValueMask;
  uint64_t high_bits =
      Tagged<Smi>(internal::ValueHelper::ValueAsAddress(*info[1])).value() &
      kSmiValueMask;
  Address fp =
      static_cast<Address>((high_bits << (kSmiValueSize - 1)) | low_bits);
#else
#error Host architecture is neither 32-bit nor 64-bit.
#endif
  printf("Trace: %p\n", reinterpret_cast<void*>(fp));
  return fp;
}

static struct { TickSample* sample; } trace_env = {nullptr};

void TraceExtension::InitTraceEnv(TickSample* sample) {
  trace_env.sample = sample;
}

void TraceExtension::DoTrace(Address fp) {
  RegisterState regs;
  regs.fp = reinterpret_cast<void*>(fp);
  // sp is only used to define stack high bound
  regs.sp = reinterpret_cast<void*>(
      reinterpret_cast<Address>(trace_env.sample) - 10240);
  trace_env.sample->Init(CcTest::i_isolate(), regs,
                         TickSample::kSkipCEntryFrame, true);
}

void TraceExtension::Trace(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  i::VMState<EXTERNAL> state(isolate);
  Address address = reinterpret_cast<Address>(&TraceExtension::Trace);
  i::ExternalCallbackScope call_scope(isolate, address);
  DoTrace(GetFP(info));
}

// Hide c_entry_fp to emulate situation when sampling is done while
// pure JS code is being executed
static void DoTraceHideCEntryFPAddress(Address fp) {
  v8::internal::Address saved_c_frame_fp =
      *(CcTest::i_isolate()->c_entry_fp_address());
  CHECK(saved_c_frame_fp);
  *(CcTest::i_isolate()->c_entry_fp_address()) = 0;
  i::TraceExtension::DoTrace(fp);
  *(CcTest::i_isolate()->c_entry_fp_address()) = saved_c_frame_fp;
}

void TraceExtension::JSTrace(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(info.GetIsolate());
  i::VMState<EXTERNAL> state(isolate);
  Address address = reinterpret_cast<Address>(&TraceExtension::JSTrace);
  i::ExternalCallbackScope call_scope(isolate, address);
  DoTraceHideCEntryFPAddress(GetFP(info));
}

Address TraceExtension::GetJsEntrySp() {
  CHECK(CcTest::i_isolate()->thread_local_top());
  return CcTest::i_isolate()->js_entry_sp();
}

void TraceExtension::JSEntrySP(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  CHECK(GetJsEntrySp());
}

void TraceExtension::JSEntrySPLevel2(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  v8::HandleScope scope(info.GetIsolate());
  const Address js_entry_sp = GetJsEntrySp();
  CHECK(js_entry_sp);
  CompileRun("js_entry_sp();");
  CHECK_EQ(js_entry_sp, GetJsEntrySp());
}

}  // namespace internal
}  // namespace v8

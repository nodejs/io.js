// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-function.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-unwinder-state.h"
#include "src/api/api-inl.h"
#include "src/builtins/builtins.h"
#include "src/execution/isolate.h"
#include "src/heap/spaces.h"
#include "src/objects/code-inl.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace test_unwinder_code_pages {

namespace {

#define CHECK_EQ_VALUE_REGISTER(uiuntptr_value, register_value) \
  CHECK_EQ(reinterpret_cast<void*>(uiuntptr_value), register_value)

#ifdef V8_TARGET_ARCH_X64
// How much the JSEntry frame occupies in the stack.
constexpr int kJSEntryFrameSpace = 3;

// Offset where the FP, PC and SP live from the beginning of the JSEntryFrame.
constexpr int kFPOffset = 0;
constexpr int kPCOffset = 1;
constexpr int kSPOffset = 2;

// Builds the stack from {stack} as x64 expects it.
// TODO(solanes): Build the JSEntry stack in the way the builtin builds it.
void BuildJSEntryStack(uintptr_t* stack) {
  stack[0] = reinterpret_cast<uintptr_t>(stack + 0);  // saved FP.
  stack[1] = 100;  // Return address into C++ code.
  stack[2] = reinterpret_cast<uintptr_t>(stack + 2);  // saved SP.
}

// Dummy method since we don't save callee saved registers in x64.
void CheckCalleeSavedRegisters(const RegisterState& register_state) {}

#elif V8_TARGET_ARCH_ARM
// How much the JSEntry frame occupies in the stack.
constexpr int kJSEntryFrameSpace = 26;

// Offset where the FP, PC and SP live from the beginning of the JSEntryFrame.
constexpr int kFPOffset = 0;
constexpr int kPCOffset = 1;
constexpr int kSPOffset = 25;

// Builds the stack from {stack} as it is explained in frame-constants-arm.h.
void BuildJSEntryStack(uintptr_t* stack) {
  stack[0] = reinterpret_cast<uintptr_t>(stack);  // saved FP.
  stack[1] = 100;  // Return address into C++ code (i.e lr/pc)
  // Set d8 = 150, d9 = 151, ..., d15 = 157.
  for (int i = 0; i < 8; ++i) {
    // Double registers occupy two slots. Therefore, upper bits are zeroed.
    stack[2 + i * 2] = 0;
    stack[2 + i * 2 + 1] = 150 + i;
  }
  // Set r4 = 160, ..., r10 = 166.
  for (int i = 0; i < 7; ++i) {
    stack[18 + i] = 160 + i;
  }
  stack[25] = reinterpret_cast<uintptr_t>(stack + 25);  // saved SP.
}

// Checks that the values in the calee saved registers are the same as the ones
// we saved in BuildJSEntryStack.
void CheckCalleeSavedRegisters(const RegisterState& register_state) {
  CHECK_EQ_VALUE_REGISTER(160, register_state.callee_saved->arm_r4);
  CHECK_EQ_VALUE_REGISTER(161, register_state.callee_saved->arm_r5);
  CHECK_EQ_VALUE_REGISTER(162, register_state.callee_saved->arm_r6);
  CHECK_EQ_VALUE_REGISTER(163, register_state.callee_saved->arm_r7);
  CHECK_EQ_VALUE_REGISTER(164, register_state.callee_saved->arm_r8);
  CHECK_EQ_VALUE_REGISTER(165, register_state.callee_saved->arm_r9);
  CHECK_EQ_VALUE_REGISTER(166, register_state.callee_saved->arm_r10);
}

#elif V8_TARGET_ARCH_ARM64
// How much the JSEntry frame occupies in the stack.
constexpr int kJSEntryFrameSpace = 21;

// Offset where the FP, PC and SP live from the beginning of the JSEntryFrame.
constexpr int kFPOffset = 0;
constexpr int kPCOffset = 1;
constexpr int kSPOffset = 20;

// Builds the stack from {stack} as it is explained in frame-constants-arm64.h.
void BuildJSEntryStack(uintptr_t* stack) {
  stack[0] = reinterpret_cast<uintptr_t>(stack);  // saved FP.
  stack[1] = 100;  // Return address into C++ code (i.e lr/pc)
  // Set x19 = 150, ..., x28 = 159.
  for (int i = 0; i < 10; ++i) {
    stack[2 + i] = 150 + i;
  }
  // Set d8 = 160, ..., d15 = 167.
  for (int i = 0; i < 8; ++i) {
    stack[12 + i] = 160 + i;
  }
  stack[20] = reinterpret_cast<uintptr_t>(stack + 20);  // saved SP.
}

// Dummy method since we don't save callee saved registers in arm64.
void CheckCalleeSavedRegisters(const RegisterState& register_state) {}

#else
// Dummy constants for the rest of the archs which are not supported.
constexpr int kJSEntryFrameSpace = 1;
constexpr int kFPOffset = 0;
constexpr int kPCOffset = 0;
constexpr int kSPOffset = 0;

// Dummy methods to be able to compile.
void BuildJSEntryStack(uintptr_t* stack) { UNREACHABLE(); }
void CheckCalleeSavedRegisters(const RegisterState& register_state) {
  UNREACHABLE();
}
#endif  // V8_TARGET_ARCH_X64

}  // namespace

static const void* fake_stack_base = nullptr;

TEST(Unwind_BadState_Fail_CodePagesAPI) {
  JSEntryStubs entry_stubs;  // Fields are initialized to nullptr.
  RegisterState register_state;
  size_t pages_length = 0;
  MemoryRange* code_pages = nullptr;

  bool unwound = v8::Unwinder::TryUnwindV8Frames(
      entry_stubs, pages_length, code_pages, &register_state, fake_stack_base);
  CHECK(!unwound);
  // The register state should not change when unwinding fails.
  CHECK_NULL(register_state.fp);
  CHECK_NULL(register_state.sp);
  CHECK_NULL(register_state.pc);
}

// Unwind a middle JS frame (i.e not the JSEntry one).
TEST(Unwind_BuiltinPCInMiddle_Success_CodePagesAPI) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);

  JSEntryStubs entry_stubs = isolate->GetJSEntryStubs();
  MemoryRange code_pages[v8::Isolate::kMinCodePagesBufferSize];
  size_t pages_length =
      isolate->CopyCodePages(arraysize(code_pages), code_pages);
  CHECK_LE(pages_length, arraysize(code_pages));
  RegisterState register_state;

  // {stack} here mocks the stack, where the top of the stack (i.e the lowest
  // addresses) are represented by lower indices.
  uintptr_t stack[3];
  void* stack_base = stack + arraysize(stack);
  // Index on the stack for the topmost fp (i.e the one right before the C++
  // frame).
  const int topmost_fp_index = 0;
  stack[0] = reinterpret_cast<uintptr_t>(stack + 2);  // saved FP.
  stack[1] = 202;  // Return address into C++ code.
  stack[2] = reinterpret_cast<uintptr_t>(stack + 2);  // saved SP.

  register_state.sp = stack;
  register_state.fp = stack;

  // Put the current PC inside of a valid builtin.
  Tagged<Code> builtin = *BUILTIN_CODE(i_isolate, StringEqual);
  const uintptr_t offset = 40;
  CHECK_LT(offset, builtin->instruction_size());
  register_state.pc =
      reinterpret_cast<void*>(builtin->instruction_start() + offset);

  bool unwound = v8::Unwinder::TryUnwindV8Frames(
      entry_stubs, pages_length, code_pages, &register_state, stack_base);
  CHECK(unwound);
  CHECK_EQ_VALUE_REGISTER(stack[topmost_fp_index], register_state.fp);
  CHECK_EQ_VALUE_REGISTER(stack[topmost_fp_index + 1], register_state.pc);
  CHECK_EQ_VALUE_REGISTER(stack[topmost_fp_index + 2], register_state.sp);
}

// The unwinder should be able to unwind even if we haven't properly set up the
// current frame, as long as there is another JS frame underneath us (i.e. as
// long as the PC isn't in JSEntry). This test puts the PC at the start
// of a JS builtin and creates a fake JSEntry frame before it on the stack. The
// unwinder should be able to unwind to the C++ frame before the JSEntry frame.
TEST(Unwind_BuiltinPCAtStart_Success_CodePagesAPI) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);

  JSEntryStubs entry_stubs = isolate->GetJSEntryStubs();
  MemoryRange code_pages[v8::Isolate::kMinCodePagesBufferSize];
  RegisterState register_state;

  const size_t code_length = 40;
  uintptr_t code[code_length] = {0};

  // We use AddCodeRange so that |code| is inserted in order.
  i_isolate->AddCodeRange(reinterpret_cast<Address>(code),
                          code_length * sizeof(uintptr_t));
  size_t pages_length =
      isolate->CopyCodePages(arraysize(code_pages), code_pages);
  CHECK_LE(pages_length, arraysize(code_pages));

  uintptr_t stack[6];
  void* stack_base = stack + arraysize(stack);
  stack[0] = 101;
  // Return address into JS code. It doesn't matter that this is not actually in
  // JSEntry, because we only check that for the top frame.
  stack[1] = reinterpret_cast<uintptr_t>(code + 10);
  // Index on the stack for the topmost fp (i.e the one right before the C++
  // frame).
  const int topmost_fp_index = 2;
  stack[2] = reinterpret_cast<uintptr_t>(stack + 5);  // saved FP.
  stack[3] = 303;  // Return address into C++ code.
  stack[4] = reinterpret_cast<uintptr_t>(stack + 4);
  stack[5] = 505;

  register_state.sp = stack;
  register_state.fp = stack + 2;  // FP to the JSEntry frame.

  // Put the current PC at the start of a valid builtin, so that we are setting
  // up the frame.
  Tagged<Code> builtin = *BUILTIN_CODE(i_isolate, StringEqual);
  register_state.pc = reinterpret_cast<void*>(builtin->instruction_start());

  bool unwound = v8::Unwinder::TryUnwindV8Frames(
      entry_stubs, pages_length, code_pages, &register_state, stack_base);

  CHECK(unwound);
  CHECK_EQ_VALUE_REGISTER(stack[topmost_fp_index], register_state.fp);
  CHECK_EQ_VALUE_REGISTER(stack[topmost_fp_index + 1], register_state.pc);
  CHECK_EQ_VALUE_REGISTER(stack[topmost_fp_index + 2], register_state.sp);
}

const char* foo_source = R"(
  function foo(a, b) {
    let x = a * b;
    let y = x ^ b;
    let z = y / a;
    return x + y - z;
  };
  %PrepareFunctionForOptimization(foo);
  foo(1, 2);
  foo(1, 2);
  %OptimizeFunctionOnNextCall(foo);
  foo(1, 2);
)";

bool PagesContainsAddress(size_t length, MemoryRange* pages,
                          Address search_address) {
  uint8_t* addr = reinterpret_cast<uint8_t*>(search_address);
  auto it = std::find_if(pages, pages + length, [addr](const MemoryRange& r) {
    const uint8_t* page_start = reinterpret_cast<const uint8_t*>(r.start);
    const uint8_t* page_end = page_start + r.length_in_bytes;
    return addr >= page_start && addr < page_end;
  });
  return it != pages + length;
}

// Check that we can unwind when the pc is within an optimized code object on
// the V8 heap.
TEST(Unwind_CodeObjectPCInMiddle_Success_CodePagesAPI) {
  v8_flags.allow_natives_syntax = true;
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  HandleScope scope(i_isolate);

  JSEntryStubs entry_stubs = isolate->GetJSEntryStubs();
  MemoryRange code_pages[v8::Isolate::kMinCodePagesBufferSize];
  RegisterState register_state;

  uintptr_t stack[3];
  void* stack_base = stack + arraysize(stack);
  // Index on the stack for the topmost fp (i.e the one right before the C++
  // frame).
  const int topmost_fp_index = 0;
  stack[0] = reinterpret_cast<uintptr_t>(stack + 2);  // saved FP.
  stack[1] = 202;  // Return address into C++ code.
  stack[2] = reinterpret_cast<uintptr_t>(stack + 2);  // saved SP.

  register_state.sp = stack;
  register_state.fp = stack;

  // Create an on-heap code object. Make sure we run the function so that it is
  // compiled and not just marked for lazy compilation.
  CompileRun(foo_source);
  v8::Local<v8::Function> local_foo = v8::Local<v8::Function>::Cast(
      env.local()->Global()->Get(env.local(), v8_str("foo")).ToLocalChecked());
  DirectHandle<JSFunction> foo =
      Cast<JSFunction>(v8::Utils::OpenDirectHandle(*local_foo));

  // Put the current PC inside of the created code object.
  Tagged<Code> code = foo->code(i_isolate);
  // We don't produce optimized code when run with --no-turbofan and
  // --no-maglev.
  if (!code->is_optimized_code()) return;

  // We don't want the offset too early or it could be the `push rbp`
  // instruction (which is not at the start of generated code, because the lazy
  // deopt check happens before frame setup).
  const uintptr_t offset = code->instruction_size() - 20;
  CHECK_LT(offset, code->instruction_size());
  Address pc = code->instruction_start() + offset;
  register_state.pc = reinterpret_cast<void*>(pc);

  // Get code pages from the API now that the code obejct exists and check that
  // our code objects is on one of the pages.
  size_t pages_length =
      isolate->CopyCodePages(arraysize(code_pages), code_pages);
  CHECK_LE(pages_length, arraysize(code_pages));
  CHECK(PagesContainsAddress(pages_length, code_pages, pc));

  bool unwound = v8::Unwinder::TryUnwindV8Frames(
      entry_stubs, pages_length, code_pages, &register_state, stack_base);
  CHECK(unwound);
  CHECK_EQ_VALUE_REGISTER(stack[topmost_fp_index], register_state.fp);
  CHECK_EQ_VALUE_REGISTER(stack[topmost_fp_index + 1], register_state.pc);
  CHECK_EQ_VALUE_REGISTER(stack[topmost_fp_index + 2], register_state.sp);
}

// If the PC is within JSEntry but we haven't set up the frame yet, then we
// cannot unwind.
TEST(Unwind_JSEntryBeforeFrame_Fail_CodePagesAPI) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();

  JSEntryStubs entry_stubs = isolate->GetJSEntryStubs();
  MemoryRange code_pages[1];
  size_t pages_length = 1;
  RegisterState register_state;

  const size_t code_length = 40;
  uintptr_t code[code_length] = {0};
  code_pages[0].start = code;
  code_pages[0].length_in_bytes = code_length * sizeof(uintptr_t);

  // Pretend that it takes 5 instructions to set up the frame in JSEntry.
  entry_stubs.js_entry_stub.code.start = code + 10;
  entry_stubs.js_entry_stub.code.length_in_bytes = 10 * sizeof(uintptr_t);

  uintptr_t stack[10];
  void* stack_base = stack + arraysize(stack);
  stack[0] = 101;
  stack[1] = 111;
  stack[2] = 121;
  stack[3] = 131;
  stack[4] = 141;
  stack[5] = 151;  // Here's where the saved fp would be. We are not going to be
                   // unwinding so we do not need to set it up correctly.
  stack[6] = 100;  // Return address into C++ code.
  stack[7] = 303;  // Here's where the saved SP would be.
  stack[8] = 404;
  stack[9] = 505;

  register_state.sp = &stack[5];
  register_state.fp = &stack[9];

  // Put the current PC inside of JSEntry, before the frame is set up.
  uintptr_t* jsentry_pc_value = code + 12;
  register_state.pc = jsentry_pc_value;
  bool unwound = v8::Unwinder::TryUnwindV8Frames(
      entry_stubs, pages_length, code_pages, &register_state, stack_base);
  CHECK(!unwound);
  // The register state should not change when unwinding fails.
  CHECK_EQ_VALUE_REGISTER(&stack[9], register_state.fp);
  CHECK_EQ_VALUE_REGISTER(&stack[5], register_state.sp);
  CHECK_EQ(jsentry_pc_value, register_state.pc);

  // Change the PC to a few instructions later, after the frame is set up.
  jsentry_pc_value = code + 16;
  register_state.pc = jsentry_pc_value;
  unwound = v8::Unwinder::TryUnwindV8Frames(
      entry_stubs, pages_length, code_pages, &register_state, stack_base);
  // TODO(petermarshall): More precisely check position within JSEntry rather
  // than just assuming the frame is unreadable.
  CHECK(!unwound);
  // The register state should not change when unwinding fails.
  CHECK_EQ_VALUE_REGISTER(&stack[9], register_state.fp);
  CHECK_EQ_VALUE_REGISTER(&stack[5], register_state.sp);
  CHECK_EQ(jsentry_pc_value, register_state.pc);
}

// Creates a fake stack with two JS frames on top of a C++ frame and checks that
// the unwinder correctly unwinds past the JS frames and returns the C++ frame's
// details.
TEST(Unwind_TwoJSFrames_Success_CodePagesAPI) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();

  JSEntryStubs entry_stubs = isolate->GetJSEntryStubs();
  MemoryRange code_pages[1];
  size_t pages_length = 1;
  RegisterState register_state;

  // Use a fake code range so that we can initialize it to 0s.
  const size_t code_length = 40;
  uintptr_t code[code_length] = {0};
  code_pages[0].start = code;
  code_pages[0].length_in_bytes = code_length * sizeof(uintptr_t);

  // Our fake stack has three frames - one C++ frame and two JS frames (on top).
  // The stack grows from high addresses to low addresses.
  uintptr_t stack[5 + kJSEntryFrameSpace];
  void* stack_base = stack + arraysize(stack);
  stack[0] = 101;
  stack[1] = 111;
  stack[2] = reinterpret_cast<uintptr_t>(stack + 5);  // saved FP.
  // The fake return address is in the JS code range.
  const void* jsentry_pc = code + 10;
  stack[3] = reinterpret_cast<uintptr_t>(jsentry_pc);
  stack[4] = 141;
  const int top_of_js_entry = 5;
  BuildJSEntryStack(&stack[top_of_js_entry]);

  register_state.sp = stack;
  register_state.fp = stack + 2;

  // Put the current PC inside of the code range so it looks valid.
  register_state.pc = code + 30;

  // Put the PC in the JSEntryRange.
  entry_stubs.js_entry_stub.code.start = jsentry_pc;
  entry_stubs.js_entry_stub.code.length_in_bytes = sizeof(uintptr_t);

  bool unwound = v8::Unwinder::TryUnwindV8Frames(
      entry_stubs, pages_length, code_pages, &register_state, stack_base);

  CHECK(unwound);
  CHECK_EQ_VALUE_REGISTER(stack[top_of_js_entry + kFPOffset],
                          register_state.fp);
  CHECK_EQ_VALUE_REGISTER(stack[top_of_js_entry + kPCOffset],
                          register_state.pc);
  CHECK_EQ_VALUE_REGISTER(stack[top_of_js_entry + kSPOffset],
                          register_state.sp);
  CheckCalleeSavedRegisters(register_state);
}

// If the PC is in JSEntry then the frame might not be set up correctly, meaning
// we can't unwind the stack properly.
TEST(Unwind_JSEntry_Fail_CodePagesAPI) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);

  JSEntryStubs entry_stubs = isolate->GetJSEntryStubs();
  MemoryRange code_pages[v8::Isolate::kMinCodePagesBufferSize];
  size_t pages_length =
      isolate->CopyCodePages(arraysize(code_pages), code_pages);
  CHECK_LE(pages_length, arraysize(code_pages));
  RegisterState register_state;

  Tagged<Code> js_entry = *BUILTIN_CODE(i_isolate, JSEntry);
  uint8_t* start = reinterpret_cast<uint8_t*>(js_entry->instruction_start());
  register_state.pc = start + 10;

  bool unwound = v8::Unwinder::TryUnwindV8Frames(
      entry_stubs, pages_length, code_pages, &register_state, fake_stack_base);
  CHECK(!unwound);
  // The register state should not change when unwinding fails.
  CHECK_NULL(register_state.fp);
  CHECK_NULL(register_state.sp);
  CHECK_EQ(start + 10, register_state.pc);
}

// Tries to unwind a middle frame (i.e not a JSEntry frame) first with a wrong
// stack base, and then with the correct one.
TEST(Unwind_StackBounds_Basic_CodePagesAPI) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();

  JSEntryStubs entry_stubs = isolate->GetJSEntryStubs();
  MemoryRange code_pages[1];
  size_t pages_length = 1;
  RegisterState register_state;

  const size_t code_length = 10;
  uintptr_t code[code_length] = {0};
  code_pages[0].start = code;
  code_pages[0].length_in_bytes = code_length * sizeof(uintptr_t);

  uintptr_t stack[3];
  stack[0] = reinterpret_cast<uintptr_t>(stack + 2);  // saved FP.
  stack[1] = 202;                                     // saved PC.
  stack[2] = 303;  // saved SP.

  register_state.sp = stack;
  register_state.fp = stack;
  register_state.pc = code;

  void* wrong_stack_base = reinterpret_cast<void*>(
      reinterpret_cast<uintptr_t>(stack) - sizeof(uintptr_t));
  bool unwound = v8::Unwinder::TryUnwindV8Frames(
      entry_stubs, pages_length, code_pages, &register_state, wrong_stack_base);
  CHECK(!unwound);

  // Correct the stack base and unwinding should succeed.
  void* correct_stack_base = stack + arraysize(stack);
  unwound =
      v8::Unwinder::TryUnwindV8Frames(entry_stubs, pages_length, code_pages,
                                      &register_state, correct_stack_base);
  CHECK(unwound);
}

TEST(Unwind_StackBounds_WithUnwinding_CodePagesAPI) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();

  JSEntryStubs entry_stubs = isolate->GetJSEntryStubs();
  MemoryRange code_pages[1];
  size_t pages_length = 1;
  RegisterState register_state;

  // Use a fake code range so that we can initialize it to 0s.
  const size_t code_length = 40;
  uintptr_t code[code_length] = {0};
  code_pages[0].start = code;
  code_pages[0].length_in_bytes = code_length * sizeof(uintptr_t);

  // Our fake stack has two frames - one C++ frame and one JS frame (on top).
  // The stack grows from high addresses to low addresses.
  uintptr_t stack[9 + kJSEntryFrameSpace];
  void* stack_base = stack + arraysize(stack);
  stack[0] = 101;
  stack[1] = 111;
  stack[2] = 121;
  stack[3] = 131;
  stack[4] = 141;
  stack[5] = reinterpret_cast<uintptr_t>(stack + 9);  // saved FP.
  const void* jsentry_pc = code + 20;
  stack[6] = reinterpret_cast<uintptr_t>(jsentry_pc);  // JS code.
  stack[7] = 303;                                      // saved SP.
  stack[8] = 404;
  const int top_of_js_entry = 9;
  BuildJSEntryStack(&stack[top_of_js_entry]);
  // Override FP and PC
  stack[top_of_js_entry + kFPOffset] =
      reinterpret_cast<uintptr_t>(stack) +
      (9 + kJSEntryFrameSpace + 1) * sizeof(uintptr_t);  // saved FP (OOB).
  stack[top_of_js_entry + kPCOffset] =
      reinterpret_cast<uintptr_t>(code + 20);  // JS code.

  register_state.sp = stack;
  register_state.fp = stack + 5;

  // Put the current PC inside of the code range so it looks valid.
  register_state.pc = code + 30;

  // Put the PC in the JSEntryRange.
  entry_stubs.js_entry_stub.code.start = jsentry_pc;
  entry_stubs.js_entry_stub.code.length_in_bytes = sizeof(uintptr_t);

  // Unwind will fail because stack[9] FP points outside of the stack.
  bool unwound = v8::Unwinder::TryUnwindV8Frames(
      entry_stubs, pages_length, code_pages, &register_state, stack_base);
  CHECK(!unwound);

  // Change the return address so that it is not in range. We will not range
  // check the stack's FP value because we have finished unwinding and the
  // contents of rbp does not necessarily have to be the FP in this case.
  stack[top_of_js_entry + kPCOffset] = 202;
  unwound = v8::Unwinder::TryUnwindV8Frames(
      entry_stubs, pages_length, code_pages, &register_state, stack_base);
  CHECK(unwound);
  CheckCalleeSavedRegisters(register_state);
}

TEST(PCIsInV8_BadState_Fail_CodePagesAPI) {
  void* pc = nullptr;
  size_t pages_length = 0;
  MemoryRange* code_pages = nullptr;

  CHECK(!v8::Unwinder::PCIsInV8(pages_length, code_pages, pc));
}

TEST(PCIsInV8_ValidStateNullPC_Fail_CodePagesAPI) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();

  void* pc = nullptr;

  MemoryRange code_pages[v8::Isolate::kMinCodePagesBufferSize];
  size_t pages_length =
      isolate->CopyCodePages(arraysize(code_pages), code_pages);
  CHECK_LE(pages_length, arraysize(code_pages));

  CHECK(!v8::Unwinder::PCIsInV8(pages_length, code_pages, pc));
}

void TestRangeBoundaries(size_t pages_length, MemoryRange* code_pages,
                         uint8_t* range_start, size_t range_length) {
  void* pc = range_start - 1;
  CHECK(!v8::Unwinder::PCIsInV8(pages_length, code_pages, pc));
  pc = range_start;
  CHECK(v8::Unwinder::PCIsInV8(pages_length, code_pages, pc));
  pc = range_start + 1;
  CHECK(v8::Unwinder::PCIsInV8(pages_length, code_pages, pc));
  pc = range_start + range_length - 1;
  CHECK(v8::Unwinder::PCIsInV8(pages_length, code_pages, pc));
  pc = range_start + range_length;
  CHECK(!v8::Unwinder::PCIsInV8(pages_length, code_pages, pc));
  pc = range_start + range_length + 1;
  CHECK(!v8::Unwinder::PCIsInV8(pages_length, code_pages, pc));
}

TEST(PCIsInV8_InAllCodePages_CodePagesAPI) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();

  MemoryRange code_pages[v8::Isolate::kMinCodePagesBufferSize];
  size_t pages_length =
      isolate->CopyCodePages(arraysize(code_pages), code_pages);
  CHECK_LE(pages_length, arraysize(code_pages));

  for (size_t i = 0; i < pages_length; i++) {
    uint8_t* range_start = const_cast<uint8_t*>(
        reinterpret_cast<const uint8_t*>(code_pages[i].start));
    size_t range_length = code_pages[i].length_in_bytes;
    TestRangeBoundaries(pages_length, code_pages, range_start, range_length);
  }
}

// PCIsInV8 doesn't check if the PC is in JSEntry directly. It's assumed that
// the CodeRange or EmbeddedCodeRange contain JSEntry.
TEST(PCIsInV8_InJSEntryRange_CodePagesAPI) {
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);

  MemoryRange code_pages[v8::Isolate::kMinCodePagesBufferSize];
  size_t pages_length =
      isolate->CopyCodePages(arraysize(code_pages), code_pages);
  CHECK_LE(pages_length, arraysize(code_pages));

  Tagged<Code> js_entry = *BUILTIN_CODE(i_isolate, JSEntry);
  uint8_t* start = reinterpret_cast<uint8_t*>(js_entry->instruction_start());
  size_t length = js_entry->instruction_size();

  void* pc = start;
  CHECK(v8::Unwinder::PCIsInV8(pages_length, code_pages, pc));
  pc = start + 1;
  CHECK(v8::Unwinder::PCIsInV8(pages_length, code_pages, pc));
  pc = start + length - 1;
  CHECK(v8::Unwinder::PCIsInV8(pages_length, code_pages, pc));
}

// Large code objects can be allocated in large object space. Check that this is
// inside the CodeRange.
TEST(PCIsInV8_LargeCodeObject_CodePagesAPI) {
  v8_flags.allow_natives_syntax = true;
  LocalContext env;
  v8::Isolate* isolate = env->GetIsolate();
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  HandleScope scope(i_isolate);

  // Create a big function that ends up in CODE_LO_SPACE.
  const int instruction_size = PageMetadata::kPageSize + 1;
  CHECK_GT(instruction_size, MemoryChunkLayout::MaxRegularCodeObjectSize());
  std::unique_ptr<uint8_t[]> instructions(new uint8_t[instruction_size]);

  CodeDesc desc;
  desc.buffer = instructions.get();
  desc.buffer_size = instruction_size;
  desc.instr_size = instruction_size;
  desc.reloc_size = 0;
  desc.constant_pool_size = 0;
  desc.unwinding_info = nullptr;
  desc.unwinding_info_size = 0;
  desc.origin = nullptr;
  DirectHandle<Code> foo_code =
      Factory::CodeBuilder(i_isolate, desc, CodeKind::FOR_TESTING).Build();

  CHECK(i_isolate->heap()->InSpace(foo_code->instruction_stream(),
                                   CODE_LO_SPACE));
  uint8_t* start = reinterpret_cast<uint8_t*>(foo_code->instruction_start());

  MemoryRange code_pages[v8::Isolate::kMinCodePagesBufferSize];
  size_t pages_length =
      isolate->CopyCodePages(arraysize(code_pages), code_pages);
  CHECK_LE(pages_length, arraysize(code_pages));

  void* pc = start;
  CHECK(v8::Unwinder::PCIsInV8(pages_length, code_pages, pc));
}

#ifdef USE_SIMULATOR
// TODO(v8:10026): Make this also work without the simulator. The part that
// needs modifications is getting the RegisterState.
class UnwinderTestHelper {
 public:
  explicit UnwinderTestHelper(const std::string& test_function)
      : isolate_(CcTest::isolate()) {
    CHECK(!instance_);
    instance_ = this;
    v8::HandleScope scope(isolate_);
    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate_);
    global->Set(v8_str("TryUnwind"),
                v8::FunctionTemplate::New(isolate_, TryUnwind));
    LocalContext env(isolate_, nullptr, global);
    CompileRun(v8_str(test_function.c_str()));
  }

  ~UnwinderTestHelper() { instance_ = nullptr; }

 private:
  static void TryUnwind(const v8::FunctionCallbackInfo<v8::Value>& info) {
    CHECK(i::ValidateCallbackInfo(info));
    instance_->DoTryUnwind();
  }

  void DoTryUnwind() {
    // Set up RegisterState.
    v8::RegisterState register_state;
    SimulatorHelper simulator_helper;
    if (!simulator_helper.Init(isolate_)) return;
    simulator_helper.FillRegisters(&register_state);
    // At this point, the PC will point to a Redirection object, which is not
    // in V8 as far as the unwinder is concerned. To make this work, point to
    // the return address, which is in V8, instead.
    register_state.pc = register_state.lr;

    JSEntryStubs entry_stubs = isolate_->GetJSEntryStubs();
    MemoryRange code_pages[v8::Isolate::kMinCodePagesBufferSize];
    size_t pages_length =
        isolate_->CopyCodePages(arraysize(code_pages), code_pages);
    CHECK_LE(pages_length, arraysize(code_pages));

    void* stack_base = reinterpret_cast<void*>(0xffffffffffffffffL);
    bool unwound = v8::Unwinder::TryUnwindV8Frames(
        entry_stubs, pages_length, code_pages, &register_state, stack_base);
    // Check that we have successfully unwound past js_entry_sp.
    CHECK(unwound);
    CHECK_GT(register_state.sp,
             reinterpret_cast<void*>(CcTest::i_isolate()->js_entry_sp()));
  }

  v8::Isolate* isolate_;

  static UnwinderTestHelper* instance_;
};

UnwinderTestHelper* UnwinderTestHelper::instance_;

TEST(Unwind_TwoNestedFunctions_CodePagesAPI) {
  i::v8_flags.allow_natives_syntax = true;
  const char* test_script =
      "function test_unwinder_api_inner() {"
      "  TryUnwind();"
      "  return 0;"
      "}"
      "function test_unwinder_api_outer() {"
      "  return test_unwinder_api_inner();"
      "}"
      "%NeverOptimizeFunction(test_unwinder_api_inner);"
      "%NeverOptimizeFunction(test_unwinder_api_outer);"
      "test_unwinder_api_outer();";

  UnwinderTestHelper helper(test_script);
}
#endif

#undef CHECK_EQ_VALUE_REGISTER
}  // namespace test_unwinder_code_pages
}  // namespace internal
}  // namespace v8

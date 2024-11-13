// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/profiler/tick-sample.h"

#include <cinttypes>

#include "include/v8-profiler.h"
#include "src/base/sanitizer/asan.h"
#include "src/base/sanitizer/msan.h"
#include "src/execution/embedder-state.h"
#include "src/execution/frames-inl.h"
#include "src/execution/simulator.h"
#include "src/execution/vm-state-inl.h"
#include "src/heap/heap-inl.h"  // For Heap::code_range.
#include "src/logging/counters.h"
#include "src/profiler/profiler-stats.h"

namespace v8 {
namespace internal {
namespace {

bool IsSamePage(i::Address ptr1, i::Address ptr2) {
  const uint32_t kPageSize = 4096;
  i::Address mask = ~static_cast<i::Address>(kPageSize - 1);
  return (ptr1 & mask) == (ptr2 & mask);
}

// Check if the code at specified address could potentially be a
// frame setup code.
bool IsNoFrameRegion(i::Address address) {
  struct Pattern {
    int bytes_count;
    uint8_t bytes[8];
    int offsets[4];
  };
  static Pattern patterns[] = {
#if V8_HOST_ARCH_IA32
    // push %ebp
    // mov %esp,%ebp
    {3, {0x55, 0x89, 0xE5}, {0, 1, -1}},
    // pop %ebp
    // ret N
    {2, {0x5D, 0xC2}, {0, 1, -1}},
    // pop %ebp
    // ret
    {2, {0x5D, 0xC3}, {0, 1, -1}},
#elif V8_HOST_ARCH_X64
    // pushq %rbp
    // movq %rsp,%rbp
    {4, {0x55, 0x48, 0x89, 0xE5}, {0, 1, -1}},
    // popq %rbp
    // ret N
    {2, {0x5D, 0xC2}, {0, 1, -1}},
    // popq %rbp
    // ret
    {2, {0x5D, 0xC3}, {0, 1, -1}},
#endif
    {0, {}, {}}
  };
  uint8_t* pc = reinterpret_cast<uint8_t*>(address);
  for (Pattern* pattern = patterns; pattern->bytes_count; ++pattern) {
    for (int* offset_ptr = pattern->offsets; *offset_ptr != -1; ++offset_ptr) {
      int offset = *offset_ptr;
      if (!offset || IsSamePage(address, address - offset)) {
        MSAN_MEMORY_IS_INITIALIZED(pc - offset, pattern->bytes_count);
        if (!memcmp(pc - offset, pattern->bytes, pattern->bytes_count))
          return true;
      } else {
        // It is not safe to examine bytes on another page as it might not be
        // allocated thus causing a SEGFAULT.
        // Check the pattern part that's on the same page and
        // pessimistically assume it could be the entire pattern match.
        MSAN_MEMORY_IS_INITIALIZED(pc, pattern->bytes_count - offset);
        if (!memcmp(pc, pattern->bytes + offset, pattern->bytes_count - offset))
          return true;
      }
    }
  }
  return false;
}

#if defined(USE_SIMULATOR)
class SimulatorHelper {
 public:
  // Returns true if register values were successfully retrieved
  // from the simulator, otherwise returns false.
  static bool FillRegisters(Isolate* isolate, v8::RegisterState* state);
};

bool SimulatorHelper::FillRegisters(Isolate* isolate,
                                    v8::RegisterState* state) {
  Simulator* simulator = isolate->thread_local_top()->simulator_;
  // Check if there is active simulator.
  if (simulator == nullptr) return false;
#if V8_TARGET_ARCH_ARM
  if (!simulator->has_bad_pc()) {
    state->pc = reinterpret_cast<void*>(simulator->get_pc());
  }
  state->sp = reinterpret_cast<void*>(simulator->get_register(Simulator::sp));
  state->fp = reinterpret_cast<void*>(simulator->get_register(Simulator::r11));
  state->lr = reinterpret_cast<void*>(simulator->get_register(Simulator::lr));
#elif V8_TARGET_ARCH_ARM64
  state->pc = reinterpret_cast<void*>(simulator->pc());
  state->sp = reinterpret_cast<void*>(simulator->sp());
  state->fp = reinterpret_cast<void*>(simulator->fp());
  state->lr = reinterpret_cast<void*>(simulator->lr());
#elif V8_TARGET_ARCH_MIPS64 || V8_TARGET_ARCH_LOONG64
  if (!simulator->has_bad_pc()) {
    state->pc = reinterpret_cast<void*>(simulator->get_pc());
  }
  state->sp = reinterpret_cast<void*>(simulator->get_register(Simulator::sp));
  state->fp = reinterpret_cast<void*>(simulator->get_register(Simulator::fp));
#elif V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
  if (!simulator->has_bad_pc()) {
    state->pc = reinterpret_cast<void*>(simulator->get_pc());
  }
  state->sp = reinterpret_cast<void*>(simulator->get_register(Simulator::sp));
  state->fp = reinterpret_cast<void*>(simulator->get_register(Simulator::fp));
  state->lr = reinterpret_cast<void*>(simulator->get_lr());
#elif V8_TARGET_ARCH_S390
  if (!simulator->has_bad_pc()) {
    state->pc = reinterpret_cast<void*>(simulator->get_pc());
  }
  state->sp = reinterpret_cast<void*>(simulator->get_register(Simulator::sp));
  state->fp = reinterpret_cast<void*>(simulator->get_register(Simulator::fp));
  state->lr = reinterpret_cast<void*>(simulator->get_register(Simulator::ra));
#elif V8_TARGET_ARCH_RISCV64
  if (!simulator->has_bad_pc()) {
    state->pc = reinterpret_cast<void*>(simulator->get_pc());
  }
  state->sp = reinterpret_cast<void*>(simulator->get_register(Simulator::sp));
  state->fp = reinterpret_cast<void*>(simulator->get_register(Simulator::fp));
  state->lr = reinterpret_cast<void*>(simulator->get_register(Simulator::ra));
#elif V8_TARGET_ARCH_RISCV32
  if (!simulator->has_bad_pc()) {
    state->pc = reinterpret_cast<void*>(simulator->get_pc());
  }
  state->sp = reinterpret_cast<void*>(simulator->get_register(Simulator::sp));
  state->fp = reinterpret_cast<void*>(simulator->get_register(Simulator::fp));
  state->lr = reinterpret_cast<void*>(simulator->get_register(Simulator::ra));
#endif
  if (state->sp == 0 || state->fp == 0) {
    // It possible that the simulator is interrupted while it is updating
    // the sp or fp register. ARM64 simulator does this in two steps:
    // first setting it to zero and then setting it to the new value.
    // Bailout if sp/fp doesn't contain the new value.
    //
    // FIXME: The above doesn't really solve the issue.
    // If a 64-bit target is executed on a 32-bit host even the final
    // write is non-atomic, so it might obtain a half of the result.
    // Moreover as long as the register set code uses memcpy (as of now),
    // it is not guaranteed to be atomic even when both host and target
    // are of same bitness.
    return false;
  }
  return true;
}
#endif  // USE_SIMULATOR

}  // namespace

DISABLE_ASAN void TickSample::Init(Isolate* v8_isolate,
                                   const RegisterState& reg_state,
                                   RecordCEntryFrame record_c_entry_frame,
                                   bool update_stats,
                                   bool use_simulator_reg_state,
                                   base::TimeDelta sampling_interval) {
  update_stats_ = update_stats;
  SampleInfo info;
  RegisterState regs = reg_state;
  if (!GetStackSample(v8_isolate, &regs, record_c_entry_frame, stack,
                      kMaxFramesCount, &info, &state,
                      use_simulator_reg_state)) {
    // It is executing JS but failed to collect a stack trace.
    // Mark the sample as spoiled.
    pc = nullptr;
    return;
  }

  if (state != StateTag::EXTERNAL) {
    state = info.vm_state;
  }
  pc = regs.pc;
  frames_count = static_cast<unsigned>(info.frames_count);
  has_external_callback = info.external_callback_entry != nullptr;
  context = info.context;
  embedder_context = info.embedder_context;
  embedder_state = info.embedder_state;
  if (has_external_callback) {
    external_callback_entry = info.external_callback_entry;
  } else if (frames_count) {
    // sp register may point at an arbitrary place in memory, make
    // sure sanitizers don't complain about it.
    ASAN_UNPOISON_MEMORY_REGION(regs.sp, sizeof(void*));
    MSAN_MEMORY_IS_INITIALIZED(regs.sp, sizeof(void*));
    // Sample potential return address value for frameless invocation of
    // stubs (we'll figure out later, if this value makes sense).

    // TODO(petermarshall): This read causes guard page violations on Windows.
    // Either fix this mechanism for frameless stubs or remove it.
    // tos =
    // i::ReadUnalignedValue<void*>(reinterpret_cast<i::Address>(regs.sp));
    tos = nullptr;
  } else {
    tos = nullptr;
  }
  sampling_interval_ = sampling_interval;
  timestamp = base::TimeTicks::Now();
}

// IMPORTANT: 'GetStackSample' is sensitive to stack overflows. For this reason
// we try not to use any function/method marked as V8_EXPORT_PRIVATE with their
// only use-site in 'GetStackSample': The resulting linker stub needs quite
// a bit of stack space and has caused stack overflow crashes in the past.
bool TickSample::GetStackSample(Isolate* v8_isolate, RegisterState* regs,
                                RecordCEntryFrame record_c_entry_frame,
                                void** frames, size_t frames_limit,
                                v8::SampleInfo* sample_info,
                                StateTag* out_state,
                                bool use_simulator_reg_state) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(v8_isolate);
  sample_info->frames_count = 0;
  sample_info->vm_state = isolate->current_vm_state();
  sample_info->external_callback_entry = nullptr;
  sample_info->embedder_state = EmbedderStateTag::EMPTY;
  sample_info->embedder_context = nullptr;
  sample_info->context = nullptr;

  if (sample_info->vm_state == GC || v8_isolate->heap()->IsInGC()) {
    // GC can happen any time, not directly caused by its caller. Don't collect
    // stacks for it. We check for both GC VMState and IsInGC, since we can
    // observe LOGGING VM states during GC.
    // TODO(leszeks): We could still consider GC stacks (as long as this isn't a
    // moving GC), e.g. to surface if one particular function is triggering all
    // the GCs. However, this is a user-visible change, and we would need to
    // adjust the symbolizer and devtools to expose this information.
    return true;
  }

  EmbedderState* embedder_state = isolate->current_embedder_state();
  if (embedder_state != nullptr) {
    sample_info->embedder_context =
        reinterpret_cast<void*>(embedder_state->native_context_address());
    sample_info->embedder_state = embedder_state->GetState();
  }

  Tagged<Context> top_context = isolate->context();
  if (top_context.ptr() != i::Context::kNoContext &&
      top_context.ptr() != i::Context::kInvalidContext) {
    Tagged<NativeContext> top_native_context = top_context->native_context();
    sample_info->context = reinterpret_cast<void*>(top_native_context.ptr());
  }

  i::Address js_entry_sp = isolate->js_entry_sp();
  if (js_entry_sp == 0) return true;  // Not executing JS now.

#if defined(USE_SIMULATOR)
  if (use_simulator_reg_state) {
    if (!i::SimulatorHelper::FillRegisters(isolate, regs)) {
      i::ProfilerStats::Instance()->AddReason(
          i::ProfilerStats::Reason::kSimulatorFillRegistersFailed);
      return false;
    }
  }
#else
  USE(use_simulator_reg_state);
#endif
  DCHECK(regs->sp);

  // Check whether we interrupted setup/teardown of a stack frame in JS code.
  // Avoid this check for C++ code, as that would trigger false positives.
  // TODO(petermarshall): Code range is always null on ia32 so this check for
  // IsNoFrameRegion will never actually run there.
  if (regs->pc &&
      isolate->heap()->code_region().contains(
          reinterpret_cast<i::Address>(regs->pc)) &&
      IsNoFrameRegion(reinterpret_cast<i::Address>(regs->pc))) {
    // The frame is not setup, so it'd be hard to iterate the stack. Bailout.
    i::ProfilerStats::Instance()->AddReason(
        i::ProfilerStats::Reason::kNoFrameRegion);
    return false;
  }

  i::ExternalCallbackScope* scope = isolate->external_callback_scope();
  i::Address handler = i::Isolate::handler(isolate->thread_local_top());
  // If there is a handler on top of the external callback scope then
  // we have already entered JavaScript again and the external callback
  // is not the top function.
  if (scope && scope->scope_address() < handler) {
    i::Address* external_callback_entry_ptr =
        scope->callback_entrypoint_address();
    sample_info->external_callback_entry =
        external_callback_entry_ptr == nullptr
            ? nullptr
            : reinterpret_cast<void*>(*external_callback_entry_ptr);
  }
  // 'Fast API calls' are similar to fast C calls (see frames.cc) in that
  // they don't build an exit frame when entering C from JS. They have the
  // added speciality of having separate "fast" and "default" callbacks, the
  // latter being the regular API callback called before the JS function is
  // optimized. When TurboFan optimizes the JS caller, the fast callback
  // gets executed instead of the default one, therefore we need to store
  // its address in the sample.
  IsolateData* isolate_data = isolate->isolate_data();
  Address fast_c_fp = isolate_data->fast_c_call_caller_fp();
  if (fast_c_fp != kNullAddress &&
      isolate_data->fast_api_call_target() != kNullAddress) {
    sample_info->external_callback_entry =
        reinterpret_cast<void*>(isolate_data->fast_api_call_target());
    if (out_state) {
      *out_state = StateTag::EXTERNAL;
    }
  }

  i::StackFrameIteratorForProfiler it(
      isolate, reinterpret_cast<i::Address>(regs->pc),
      reinterpret_cast<i::Address>(regs->fp),
      reinterpret_cast<i::Address>(regs->sp),
      reinterpret_cast<i::Address>(regs->lr), js_entry_sp);

  if (it.done()) return true;

  size_t i = 0;
  if (record_c_entry_frame == kIncludeCEntryFrame &&
      (it.top_frame_type() == internal::StackFrame::EXIT ||
       it.top_frame_type() == internal::StackFrame::BUILTIN_EXIT)) {
    // While BUILTIN_EXIT definitely represents a call to CEntry the EXIT frame
    // might represent either a call to CEntry or an optimized call to
    // Api callback. In the latter case the ExternalCallbackScope points to
    // the same function, so skip adding a frame in that case in order to avoid
    // double-reporting.
    void* c_function = reinterpret_cast<void*>(isolate->c_function());
    if (sample_info->external_callback_entry != c_function) {
      frames[i] = c_function;
      i++;
    }
  }
#ifdef V8_RUNTIME_CALL_STATS
  i::RuntimeCallTimer* timer =
      isolate->counters()->runtime_call_stats()->current_timer();
#endif  // V8_RUNTIME_CALL_STATS
  for (; !it.done() && i < frames_limit; it.Advance()) {
#ifdef V8_RUNTIME_CALL_STATS
    while (timer && reinterpret_cast<i::Address>(timer) < it.frame()->fp() &&
           i < frames_limit) {
      frames[i++] = reinterpret_cast<void*>(timer->counter());
      timer = timer->parent();
    }
    if (i == frames_limit) break;
#endif  // V8_RUNTIME_CALL_STATS

    if (it.frame()->is_interpreted()) {
      // For interpreted frames use the bytecode array pointer as the pc.
      i::InterpretedFrame* frame =
          static_cast<i::InterpretedFrame*>(it.frame());
      // Since the sampler can interrupt execution at any point the
      // bytecode_array might be garbage, so don't actually dereference it. We
      // avoid the frame->GetXXX functions since they call Cast<BytecodeArray>,
      // which has a heap access in its DCHECK.
      i::Address bytecode_array = base::Memory<i::Address>(
          frame->fp() + i::InterpreterFrameConstants::kBytecodeArrayFromFp);
      i::Address bytecode_offset = base::Memory<i::Address>(
          frame->fp() + i::InterpreterFrameConstants::kBytecodeOffsetFromFp);

      // If the bytecode array is a heap object and the bytecode offset is a
      // Smi, use those, otherwise fall back to using the frame's pc.
      if (HAS_STRONG_HEAP_OBJECT_TAG(bytecode_array) &&
          HAS_SMI_TAG(bytecode_offset)) {
        frames[i++] = reinterpret_cast<void*>(
            bytecode_array + i::Internals::SmiValue(bytecode_offset));
        continue;
      }
    }
    // For arm64, the PC for the frame sometimes doesn't come from the stack,
    // but from the link register instead. For this reason, we skip
    // authenticating it.
    frames[i++] = reinterpret_cast<void*>(it.frame()->unauthenticated_pc());
  }
  sample_info->frames_count = i;
  return true;
}

void TickSample::print() const {
  PrintF("TickSample: at %p\n", this);
  PrintF(" - state: %s\n", StateToString(state));
  PrintF(" - pc: %p\n", pc);
  PrintF(" - stack: (%u frames)\n", frames_count);
  for (unsigned i = 0; i < frames_count; i++) {
    PrintF("    %p\n", stack[i]);
  }
  PrintF(" - has_external_callback: %d\n", has_external_callback);
  PrintF(" - %s: %p\n",
         has_external_callback ? "external_callback_entry" : "tos", tos);
  PrintF(" - update_stats: %d\n", update_stats_);
  PrintF(" - sampling_interval: %" PRId64 "\n",
         sampling_interval_.InMicroseconds());
  PrintF("\n");
}

}  // namespace internal
}  // namespace v8

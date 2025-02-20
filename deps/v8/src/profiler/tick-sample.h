// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROFILER_TICK_SAMPLE_H_
#define V8_PROFILER_TICK_SAMPLE_H_

#include "include/v8-unwinder.h"
#include "src/base/platform/time.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class Isolate;

// TickSample captures the information collected for each sample.
struct V8_EXPORT TickSample {
  // Internal profiling (with --prof + tools/$OS-tick-processor) wants to
  // include the runtime function we're calling. Externally exposed tick
  // samples don't care.
  enum RecordCEntryFrame { kIncludeCEntryFrame, kSkipCEntryFrame };

  TickSample() {}

  /**
   * Initialize a tick sample from the isolate.
   * \param isolate The isolate.
   * \param state Execution state.
   * \param record_c_entry_frame Include or skip the runtime function.
   * \param update_stats Whether update the sample to the aggregated stats.
   * \param use_simulator_reg_state When set to true and V8 is running under a
   *                                simulator, the method will use the simulator
   *                                register state rather than the one provided
   *                                with |state| argument. Otherwise the method
   *                                will use provided register |state| as is.
   */
  void Init(Isolate* isolate, const v8::RegisterState& state,
            RecordCEntryFrame record_c_entry_frame, bool update_stats,
            bool use_simulator_reg_state = true,
            base::TimeDelta sampling_interval = base::TimeDelta(),
            const std::optional<uint64_t> trace_id = std::nullopt);
  /**
   * Get a call stack sample from the isolate.
   * \param isolate The isolate.
   * \param state Register state.
   * \param record_c_entry_frame Include or skip the runtime function.
   * \param frames Caller allocated buffer to store stack frames.
   * \param frames_limit Maximum number of frames to capture. The buffer must
   *                     be large enough to hold the number of frames.
   * \param sample_info The sample info is filled up by the function
   *                    provides number of actual captured stack frames and
   *                    the current VM state.
   * \param out_state Output parameter. If non-nullptr pointer is provided,
   *                  and the execution is currently in a fast API call,
   *                  records StateTag::EXTERNAL to it. The caller could then
   *                  use this as a marker to not take into account the actual
   *                  VM state recorded in |sample_info|. In the case of fast
   *                  API calls, the VM state must be EXTERNAL, as the callback
   *                  is always an external C++ function.
   * \param use_simulator_reg_state When set to true and V8 is running under a
   *                                simulator, the method will use the simulator
   *                                register state rather than the one provided
   *                                with |state| argument. Otherwise the method
   *                                will use provided register |state| as is.
   * \note GetStackSample is thread and signal safe and should only be called
   *                      when the JS thread is paused or interrupted.
   *                      Otherwise the behavior is undefined.
   */
  static bool GetStackSample(Isolate* isolate, v8::RegisterState* state,
                             RecordCEntryFrame record_c_entry_frame,
                             void** frames, size_t frames_limit,
                             v8::SampleInfo* sample_info,
                             StateTag* out_state = nullptr,
                             bool use_simulator_reg_state = true);

  void print() const;

  static constexpr unsigned kMaxFramesCountLog2 = 8;
  static constexpr unsigned kMaxFramesCount = (1 << kMaxFramesCountLog2) - 1;

  void* pc = nullptr;  // Instruction pointer.
  union {
    void* tos;  // Top stack value (*sp).
    void* external_callback_entry = nullptr;
  };
  void* context = nullptr;          // Address of the incumbent native context.
  void* embedder_context = nullptr;  // Address of the embedder native context.

  base::TimeTicks timestamp;
  base::TimeDelta sampling_interval_;  // Sampling interval used to capture.

  StateTag state = OTHER;  // The state of the VM.
  EmbedderStateTag embedder_state = EmbedderStateTag::EMPTY;

  uint16_t frames_count = 0;  // Number of captured frames.
  static_assert(sizeof(frames_count) * kBitsPerByte >= kMaxFramesCountLog2);
  bool has_external_callback = false;
  // Whether the sample should update aggregated stats.
  bool update_stats_ = true;
  // An identifier to associate the sample with a trace event.
  std::optional<uint64_t> trace_id_;

  void* stack[kMaxFramesCount];  // Call stack.
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PROFILER_TICK_SAMPLE_H_

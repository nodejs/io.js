// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROFILER_CPU_PROFILER_H_
#define V8_PROFILER_CPU_PROFILER_H_

#include <atomic>
#include <memory>

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/profiler/circular-queue.h"
#include "src/profiler/profiler-listener.h"
#include "src/profiler/tick-sample.h"
#include "src/utils/locked-queue.h"

#if V8_OS_WIN
#include "src/base/platform/platform-win32.h"
#endif

namespace v8 {
namespace sampler {
class Sampler;
}  // namespace sampler
namespace internal {

// Forward declarations.
class CodeEntry;
class InstructionStreamMap;
class CpuProfilesCollection;
class Isolate;
class Symbolizer;

#define CODE_EVENTS_TYPE_LIST(V)                \
  V(kCodeCreation, CodeCreateEventRecord)       \
  V(kCodeMove, CodeMoveEventRecord)             \
  V(kCodeDisableOpt, CodeDisableOptEventRecord) \
  V(kCodeDeopt, CodeDeoptEventRecord)           \
  V(kReportBuiltin, ReportBuiltinEventRecord)   \
  V(kCodeDelete, CodeDeleteEventRecord)

#define VM_EVENTS_TYPE_LIST(V) \
  CODE_EVENTS_TYPE_LIST(V)     \
  V(kNativeContextMove, NativeContextMoveEventRecord)

class CodeEventRecord {
 public:
#define DECLARE_TYPE(type, ignore) type,
  enum class Type { kNoEvent = 0, VM_EVENTS_TYPE_LIST(DECLARE_TYPE) };
#undef DECLARE_TYPE

  Type type;
  mutable unsigned order;
};


class CodeCreateEventRecord : public CodeEventRecord {
 public:
  Address instruction_start;
  CodeEntry* entry;
  unsigned instruction_size;

  V8_INLINE void UpdateCodeMap(InstructionStreamMap* instruction_stream_map);
};


class CodeMoveEventRecord : public CodeEventRecord {
 public:
  Address from_instruction_start;
  Address to_instruction_start;

  V8_INLINE void UpdateCodeMap(InstructionStreamMap* instruction_stream_map);
};


class CodeDisableOptEventRecord : public CodeEventRecord {
 public:
  Address instruction_start;
  const char* bailout_reason;

  V8_INLINE void UpdateCodeMap(InstructionStreamMap* instruction_stream_map);
};


class CodeDeoptEventRecord : public CodeEventRecord {
 public:
  Address instruction_start;
  const char* deopt_reason;
  int deopt_id;
  Address pc;
  int fp_to_sp_delta;
  CpuProfileDeoptFrame* deopt_frames;
  int deopt_frame_count;

  V8_INLINE void UpdateCodeMap(InstructionStreamMap* instruction_stream_map);
};


class ReportBuiltinEventRecord : public CodeEventRecord {
 public:
  Address instruction_start;
  unsigned instruction_size;
  Builtin builtin;

  V8_INLINE void UpdateCodeMap(InstructionStreamMap* instruction_stream_map);
};

// Signals that a native context's address has changed.
class NativeContextMoveEventRecord : public CodeEventRecord {
 public:
  Address from_address;
  Address to_address;
};

// A record type for sending samples from the main thread/signal handler to the
// profiling thread.
class TickSampleEventRecord {
 public:
  // The parameterless constructor is used when we dequeue data from
  // the ticks buffer.
  TickSampleEventRecord() = default;
  explicit TickSampleEventRecord(unsigned order) : order(order) { }

  unsigned order;
  TickSample sample;
};

class CodeDeleteEventRecord : public CodeEventRecord {
 public:
  CodeEntry* entry;

  V8_INLINE void UpdateCodeMap(InstructionStreamMap* instruction_stream_map);
};

// A record type for sending code events (e.g. create, move, delete) to the
// profiling thread.
class CodeEventsContainer {
 public:
  explicit CodeEventsContainer(
      CodeEventRecord::Type type = CodeEventRecord::Type::kNoEvent) {
    generic.type = type;
  }
  union  {
    CodeEventRecord generic;
#define DECLARE_CLASS(ignore, type) type type##_;
    VM_EVENTS_TYPE_LIST(DECLARE_CLASS)
#undef DECLARE_CLASS
  };
};

// Maintains the number of active CPU profilers in an isolate, and routes
// logging to a given ProfilerListener.
class V8_NODISCARD ProfilingScope {
 public:
  ProfilingScope(Isolate* isolate, ProfilerListener* listener);
  ~ProfilingScope();

 private:
  Isolate* const isolate_;
  ProfilerListener* const listener_;
};

class ProfilerCodeObserver;

// This class implements both the profile events processor thread and
// methods called by event producers: VM and stack sampler threads.
class V8_EXPORT_PRIVATE ProfilerEventsProcessor : public base::Thread,
                                                  public CodeEventObserver {
 public:
  ~ProfilerEventsProcessor() override;

  void CodeEventHandler(const CodeEventsContainer& evt_rec) override;

  // Thread control.
  void Run() override = 0;
  void StopSynchronously();
  bool running() { return running_.load(std::memory_order_relaxed); }
  void Enqueue(const CodeEventsContainer& event);

  // Puts current stack into the tick sample events buffer.
  void AddCurrentStack(bool update_stats = false,
                       const std::optional<uint64_t> trace_id = std::nullopt);
  void AddDeoptStack(Address from, int fp_to_sp_delta);
  // Add a sample into the tick sample events buffer. Used for testing.
  void AddSample(TickSample sample);

  virtual void SetSamplingInterval(base::TimeDelta) {}

 protected:
  ProfilerEventsProcessor(Isolate* isolate, Symbolizer* symbolizer,
                          ProfilerCodeObserver* code_observer,
                          CpuProfilesCollection* profiles);

  // Called from events processing thread (Run() method.)
  bool ProcessCodeEvent();

  enum SampleProcessingResult {
    OneSampleProcessed,
    FoundSampleForNextCodeEvent,
    NoSamplesInQueue
  };
  virtual SampleProcessingResult ProcessOneSample() = 0;

  Symbolizer* symbolizer_;
  ProfilerCodeObserver* code_observer_;
  CpuProfilesCollection* profiles_;
  std::atomic_bool running_{true};
  base::ConditionVariable running_cond_;
  base::Mutex running_mutex_;
  LockedQueue<CodeEventsContainer> events_buffer_;
  LockedQueue<TickSampleEventRecord> ticks_from_vm_buffer_;
  std::atomic<unsigned> last_code_event_id_;
  unsigned last_processed_code_event_id_;
  Isolate* isolate_;
};

class V8_EXPORT_PRIVATE SamplingEventsProcessor
    : public ProfilerEventsProcessor {
 public:
  SamplingEventsProcessor(Isolate* isolate, Symbolizer* symbolizer,
                          ProfilerCodeObserver* code_observer,
                          CpuProfilesCollection* profiles,
                          base::TimeDelta period, bool use_precise_sampling);
  ~SamplingEventsProcessor() override;

  // SamplingCircularQueue has stricter alignment requirements than a normal new
  // can fulfil, so we need to provide our own new/delete here.
  void* operator new(size_t size);
  void operator delete(void* ptr);

  void Run() override;

  void SetSamplingInterval(base::TimeDelta period) override;

  // Tick sample events are filled directly in the buffer of the circular
  // queue (because the structure is of fixed width, but usually not all
  // stack frame entries are filled.) This method returns a pointer to the
  // next record of the buffer.
  // These methods are not thread-safe and should only ever be called by one
  // producer (from CpuSampler::SampleStack()). For testing, use AddSample.
  inline TickSample* StartTickSample();
  inline void FinishTickSample();

  sampler::Sampler* sampler() { return sampler_.get(); }
  base::TimeDelta period() const { return period_; }

 private:
  SampleProcessingResult ProcessOneSample() override;
  void SymbolizeAndAddToProfiles(const TickSampleEventRecord* record);

  static const size_t kTickSampleBufferSize = 512 * KB;
  static const size_t kTickSampleQueueLength =
      kTickSampleBufferSize / sizeof(TickSampleEventRecord);
  SamplingCircularQueue<TickSampleEventRecord,
                        kTickSampleQueueLength> ticks_buffer_;
  std::unique_ptr<sampler::Sampler> sampler_;
  base::TimeDelta period_;           // Samples & code events processing period.
  const bool use_precise_sampling_;  // Whether or not busy-waiting is used for
                                     // low sampling intervals on Windows.
#if V8_OS_WIN
  base::PreciseSleepTimer precise_sleep_timer_;
#endif  // V8_OS_WIN
};

// Builds and maintains an InstructionStreamMap tracking code objects on the VM
// heap. While alive, logs generated code, callbacks, and builtins from the
// isolate. Redirects events to the profiler events processor when present.
// CodeEntry lifetime is associated with the given CodeEntryStorage.
class V8_EXPORT_PRIVATE ProfilerCodeObserver : public CodeEventObserver {
 public:
  explicit ProfilerCodeObserver(Isolate*, CodeEntryStorage&);

  void CodeEventHandler(const CodeEventsContainer& evt_rec) override;
  CodeEntryStorage* code_entries() { return &code_entries_; }
  InstructionStreamMap* instruction_stream_map() { return &code_map_; }
  WeakCodeRegistry* weak_code_registry() { return &weak_code_registry_; }
  size_t GetEstimatedMemoryUsage() const;

  void ClearCodeMap();

 private:
  friend class ProfilerEventsProcessor;

  void CodeEventHandlerInternal(const CodeEventsContainer& evt_rec);

  void CreateEntriesForRuntimeCallStats();
  void LogBuiltins();

  ProfilerEventsProcessor* processor() { return processor_; }

  // Redirects code events to be enqueued on the given events processor.
  void set_processor(ProfilerEventsProcessor* processor) {
    processor_ = processor;
  }

  // Stops redirection of code events onto an events processor.
  void clear_processor() { processor_ = nullptr; }

  Isolate* const isolate_;
  CodeEntryStorage& code_entries_;
  InstructionStreamMap code_map_;
  WeakCodeRegistry weak_code_registry_;
  ProfilerEventsProcessor* processor_;
};

// The CpuProfiler is a sampling CPU profiler for JS frames. It corresponds to
// v8::CpuProfiler at the API level. It spawns an additional thread which is
// responsible for triggering samples and then symbolizing the samples with
// function names. To symbolize on a background thread, the profiler copies
// metadata about generated code off-heap.
//
// Sampling is done using posix signals (except on Windows). The profiling
// thread sends a signal to the main thread, based on a timer. The signal
// handler can interrupt the main thread between any arbitrary instructions.
// This means we are very careful about reading stack values during the signal
// handler as we could be in the middle of an operation that is modifying the
// stack.
//
// The story on Windows is similar except we use thread suspend and resume.
//
// Samples are passed to the profiling thread via a circular buffer. The
// profiling thread symbolizes the samples by looking up the code pointers
// against its own list of code objects. The profiling thread also listens for
// code creation/move/deletion events (from the GC), to maintain its list of
// code objects accurately.
class V8_EXPORT_PRIVATE CpuProfiler {
 public:
  explicit CpuProfiler(Isolate* isolate, CpuProfilingNamingMode = kDebugNaming,
                       CpuProfilingLoggingMode = kLazyLogging);

  CpuProfiler(Isolate* isolate, CpuProfilingNamingMode naming_mode,
              CpuProfilingLoggingMode logging_mode,
              CpuProfilesCollection* profiles, Symbolizer* test_symbolizer,
              ProfilerEventsProcessor* test_processor,
              ProfilerCodeObserver* test_code_observer);

  ~CpuProfiler();
  CpuProfiler(const CpuProfiler&) = delete;
  CpuProfiler& operator=(const CpuProfiler&) = delete;

  static void CollectSample(Isolate* isolate,
                            std::optional<uint64_t> trace_id = std::nullopt);
  static size_t GetAllProfilersMemorySize(Isolate* isolate);

  using ProfilingMode = v8::CpuProfilingMode;
  using CpuProfilingResult = v8::CpuProfilingResult;
  using NamingMode = v8::CpuProfilingNamingMode;
  using LoggingMode = v8::CpuProfilingLoggingMode;
  using StartProfilingStatus = CpuProfilingStatus;

  base::TimeDelta sampling_interval() const { return base_sampling_interval_; }
  void set_sampling_interval(base::TimeDelta value);
  void set_use_precise_sampling(bool);
  void CollectSample(const std::optional<uint64_t> trace_id = std::nullopt);
  size_t GetEstimatedMemoryUsage() const;
  CpuProfilingResult StartProfiling(
      CpuProfilingOptions options = {},
      std::unique_ptr<DiscardedSamplesDelegate> delegate = nullptr);
  CpuProfilingResult StartProfiling(
      const char* title, CpuProfilingOptions options = {},
      std::unique_ptr<DiscardedSamplesDelegate> delegate = nullptr);
  CpuProfilingResult StartProfiling(
      Tagged<String> title, CpuProfilingOptions options = {},
      std::unique_ptr<DiscardedSamplesDelegate> delegate = nullptr);

  CpuProfile* StopProfiling(const char* title);
  CpuProfile* StopProfiling(Tagged<String> title);
  CpuProfile* StopProfiling(ProfilerId id);

  int GetProfilesCount();
  CpuProfile* GetProfile(int index);
  void DeleteAllProfiles();
  void DeleteProfile(CpuProfile* profile);

  bool is_profiling() const { return is_profiling_; }

  Symbolizer* symbolizer() const { return symbolizer_.get(); }
  ProfilerEventsProcessor* processor() const { return processor_.get(); }
  Isolate* isolate() const { return isolate_; }
  CodeEntryStorage* code_entries() { return &code_entries_; }

  ProfilerListener* profiler_listener_for_test() const {
    return profiler_listener_.get();
  }
  InstructionStreamMap* code_map_for_test() {
    return code_observer_->instruction_stream_map();
  }

 private:
  void StartProcessorIfNotStarted();
  void StopProcessor();
  void ResetProfiles();

  void EnableLogging();
  void DisableLogging();

  // Computes a sampling interval sufficient to accommodate attached profiles.
  base::TimeDelta ComputeSamplingInterval();
  // Dynamically updates the sampler to use a sampling interval sufficient for
  // child profiles.
  void AdjustSamplingInterval();

  Isolate* const isolate_;
  const NamingMode naming_mode_;
  const LoggingMode logging_mode_;
  bool use_precise_sampling_ = true;
  // Sampling interval to which per-profile sampling intervals will be clamped
  // to a multiple of, or used as the default if unspecified.
  base::TimeDelta base_sampling_interval_;

  // Storage for CodeEntry objects allocated by the profiler. May live for
  // multiple profiling sessions, independent of heap listener state.
  CodeEntryStorage code_entries_;

  std::unique_ptr<ProfilerCodeObserver> code_observer_;
  std::unique_ptr<CpuProfilesCollection> profiles_;
  std::unique_ptr<Symbolizer> symbolizer_;
  std::unique_ptr<ProfilerEventsProcessor> processor_;
  std::unique_ptr<ProfilerListener> profiler_listener_;
  std::unique_ptr<ProfilingScope> profiling_scope_;
  bool is_profiling_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PROFILER_CPU_PROFILER_H_

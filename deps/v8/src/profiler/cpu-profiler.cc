// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/profiler/cpu-profiler.h"

#include <unordered_map>
#include <utility>

#include "include/v8-locker.h"
#include "src/base/lazy-instance.h"
#include "src/base/template-utils.h"
#include "src/debug/debug.h"
#include "src/execution/frames-inl.h"
#include "src/execution/v8threads.h"
#include "src/execution/vm-state-inl.h"
#include "src/libsampler/sampler.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/profiler/cpu-profiler-inl.h"
#include "src/profiler/profiler-stats.h"
#include "src/profiler/symbolizer.h"
#include "src/utils/locked-queue-inl.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-engine.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

static const int kProfilerStackSize = 256 * KB;

class CpuSampler : public sampler::Sampler {
 public:
  CpuSampler(Isolate* isolate, SamplingEventsProcessor* processor)
      : sampler::Sampler(reinterpret_cast<v8::Isolate*>(isolate)),
        processor_(processor),
        perThreadData_(isolate->FindPerThreadDataForThisThread()) {}

  void SampleStack(const v8::RegisterState& regs) override {
    Isolate* isolate = reinterpret_cast<Isolate*>(this->isolate());
    if (isolate->was_locker_ever_used() &&
        (!isolate->thread_manager()->IsLockedByThread(
             perThreadData_->thread_id()) ||
         perThreadData_->thread_state() != nullptr)) {
      ProfilerStats::Instance()->AddReason(
          ProfilerStats::Reason::kIsolateNotLocked);
      return;
    }
#if V8_HEAP_USE_PKU_JIT_WRITE_PROTECT
    i::RwxMemoryWriteScope::SetDefaultPermissionsForSignalHandler();
#endif
    TickSample* sample = processor_->StartTickSample();
    if (sample == nullptr) {
      ProfilerStats::Instance()->AddReason(
          ProfilerStats::Reason::kTickBufferFull);
      return;
    }
    // Every bailout up until here resulted in a dropped sample. From now on,
    // the sample is created in the buffer.
    sample->Init(isolate, regs, TickSample::kIncludeCEntryFrame,
                 /* update_stats */ true,
                 /* use_simulator_reg_state */ true, processor_->period());
    if (is_counting_samples_ && !sample->timestamp.IsNull()) {
      if (sample->state == JS) ++js_sample_count_;
      if (sample->state == EXTERNAL) ++external_sample_count_;
    }
    processor_->FinishTickSample();
  }

 private:
  SamplingEventsProcessor* processor_;
  Isolate::PerIsolateThreadData* perThreadData_;
};

ProfilingScope::ProfilingScope(Isolate* isolate, ProfilerListener* listener)
    : isolate_(isolate), listener_(listener) {
  size_t profiler_count = isolate_->num_cpu_profilers();
  profiler_count++;
  isolate_->set_num_cpu_profilers(profiler_count);
  isolate_->SetIsProfiling(true);
#if V8_ENABLE_WEBASSEMBLY
  wasm::GetWasmEngine()->EnableCodeLogging(isolate_);
#endif  // V8_ENABLE_WEBASSEMBLY

  CHECK(isolate_->logger()->AddListener(listener_));
  V8FileLogger* file_logger = isolate_->v8_file_logger();
  // Populate the ProfilerCodeObserver with the initial functions and
  // callbacks on the heap.
  DCHECK(isolate_->heap()->HasBeenSetUp());

  if (!v8_flags.prof_browser_mode) {
    file_logger->LogCodeObjects();
  }
  file_logger->LogCompiledFunctions();
  file_logger->LogAccessorCallbacks();
}

ProfilingScope::~ProfilingScope() {
  CHECK(isolate_->logger()->RemoveListener(listener_));

  size_t profiler_count = isolate_->num_cpu_profilers();
  DCHECK_GT(profiler_count, 0);
  profiler_count--;
  isolate_->set_num_cpu_profilers(profiler_count);
  if (profiler_count == 0) isolate_->SetIsProfiling(false);
}

ProfilerEventsProcessor::ProfilerEventsProcessor(
    Isolate* isolate, Symbolizer* symbolizer,
    ProfilerCodeObserver* code_observer, CpuProfilesCollection* profiles)
    : Thread(Thread::Options("v8:ProfEvntProc", kProfilerStackSize)),
      symbolizer_(symbolizer),
      code_observer_(code_observer),
      profiles_(profiles),
      last_code_event_id_(0),
      last_processed_code_event_id_(0),
      isolate_(isolate) {
  DCHECK(!code_observer_->processor());
  code_observer_->set_processor(this);
}

SamplingEventsProcessor::SamplingEventsProcessor(
    Isolate* isolate, Symbolizer* symbolizer,
    ProfilerCodeObserver* code_observer, CpuProfilesCollection* profiles,
    base::TimeDelta period, bool use_precise_sampling)
    : ProfilerEventsProcessor(isolate, symbolizer, code_observer, profiles),
      sampler_(new CpuSampler(isolate, this)),
      period_(period),
      use_precise_sampling_(use_precise_sampling) {
#if V8_OS_WIN
  precise_sleep_timer_.TryInit();
#endif  // V8_OS_WIN

  sampler_->Start();
}

SamplingEventsProcessor::~SamplingEventsProcessor() { sampler_->Stop(); }

ProfilerEventsProcessor::~ProfilerEventsProcessor() {
  DCHECK_EQ(code_observer_->processor(), this);
  code_observer_->clear_processor();
}

void ProfilerEventsProcessor::Enqueue(const CodeEventsContainer& event) {
  event.generic.order = ++last_code_event_id_;
  events_buffer_.Enqueue(event);
}

void ProfilerEventsProcessor::AddDeoptStack(Address from, int fp_to_sp_delta) {
  TickSampleEventRecord record(last_code_event_id_);
  RegisterState regs;
  Address fp = isolate_->c_entry_fp(isolate_->thread_local_top());
  regs.sp = reinterpret_cast<void*>(fp - fp_to_sp_delta);
  regs.fp = reinterpret_cast<void*>(fp);
  regs.pc = reinterpret_cast<void*>(from);
  record.sample.Init(isolate_, regs, TickSample::kSkipCEntryFrame, false,
                     false);
  ticks_from_vm_buffer_.Enqueue(record);
}

void ProfilerEventsProcessor::AddCurrentStack(bool update_stats) {
  TickSampleEventRecord record(last_code_event_id_);
  RegisterState regs;
  StackFrameIterator it(isolate_);
  if (!it.done()) {
    StackFrame* frame = it.frame();
    regs.sp = reinterpret_cast<void*>(frame->sp());
    regs.fp = reinterpret_cast<void*>(frame->fp());
    regs.pc = reinterpret_cast<void*>(frame->pc());
  }
  record.sample.Init(isolate_, regs, TickSample::kSkipCEntryFrame, update_stats,
                     false);
  ticks_from_vm_buffer_.Enqueue(record);
}

void ProfilerEventsProcessor::AddSample(TickSample sample) {
  TickSampleEventRecord record(last_code_event_id_);
  record.sample = sample;
  ticks_from_vm_buffer_.Enqueue(record);
}

void ProfilerEventsProcessor::StopSynchronously() {
  bool expected = true;
  if (!running_.compare_exchange_strong(expected, false,
                                        std::memory_order_relaxed))
    return;
  {
    base::MutexGuard guard(&running_mutex_);
    running_cond_.NotifyOne();
  }
  Join();
}


bool ProfilerEventsProcessor::ProcessCodeEvent() {
  CodeEventsContainer record;
  if (events_buffer_.Dequeue(&record)) {
    if (record.generic.type == CodeEventRecord::Type::kNativeContextMove) {
      NativeContextMoveEventRecord& nc_record =
          record.NativeContextMoveEventRecord_;
      profiles_->UpdateNativeContextAddressForCurrentProfiles(
          nc_record.from_address, nc_record.to_address);
    } else {
      code_observer_->CodeEventHandlerInternal(record);
    }
    last_processed_code_event_id_ = record.generic.order;
    return true;
  }
  return false;
}

void ProfilerEventsProcessor::CodeEventHandler(
    const CodeEventsContainer& evt_rec) {
  switch (evt_rec.generic.type) {
    case CodeEventRecord::Type::kCodeCreation:
    case CodeEventRecord::Type::kCodeMove:
    case CodeEventRecord::Type::kCodeDisableOpt:
    case CodeEventRecord::Type::kCodeDelete:
    case CodeEventRecord::Type::kNativeContextMove:
      Enqueue(evt_rec);
      break;
    case CodeEventRecord::Type::kCodeDeopt: {
      const CodeDeoptEventRecord* rec = &evt_rec.CodeDeoptEventRecord_;
      Address pc = rec->pc;
      int fp_to_sp_delta = rec->fp_to_sp_delta;
      Enqueue(evt_rec);
      AddDeoptStack(pc, fp_to_sp_delta);
      break;
    }
    case CodeEventRecord::Type::kNoEvent:
    case CodeEventRecord::Type::kReportBuiltin:
      UNREACHABLE();
  }
}

void SamplingEventsProcessor::SymbolizeAndAddToProfiles(
    const TickSampleEventRecord* record) {
  const TickSample& tick_sample = record->sample;
  Symbolizer::SymbolizedSample symbolized =
      symbolizer_->SymbolizeTickSample(tick_sample);
  profiles_->AddPathToCurrentProfiles(
      tick_sample.timestamp, symbolized.stack_trace, symbolized.src_line,
      tick_sample.update_stats_, tick_sample.sampling_interval_,
      tick_sample.state, tick_sample.embedder_state,
      reinterpret_cast<Address>(tick_sample.context),
      reinterpret_cast<Address>(tick_sample.embedder_context));
}

ProfilerEventsProcessor::SampleProcessingResult
SamplingEventsProcessor::ProcessOneSample() {
  TickSampleEventRecord record1;
  if (ticks_from_vm_buffer_.Peek(&record1) &&
      (record1.order == last_processed_code_event_id_)) {
    TickSampleEventRecord record;
    ticks_from_vm_buffer_.Dequeue(&record);
    SymbolizeAndAddToProfiles(&record);
    return OneSampleProcessed;
  }

  const TickSampleEventRecord* record = ticks_buffer_.Peek();
  if (record == nullptr) {
    if (ticks_from_vm_buffer_.IsEmpty()) return NoSamplesInQueue;
    return FoundSampleForNextCodeEvent;
  }
  if (record->order != last_processed_code_event_id_) {
    return FoundSampleForNextCodeEvent;
  }
  SymbolizeAndAddToProfiles(record);
  ticks_buffer_.Remove();
  return OneSampleProcessed;
}

void SamplingEventsProcessor::Run() {
  base::MutexGuard guard(&running_mutex_);
  while (running_.load(std::memory_order_relaxed)) {
    base::TimeTicks nextSampleTime = base::TimeTicks::Now() + period_;
    base::TimeTicks now;
    SampleProcessingResult result;
    // Keep processing existing events until we need to do next sample
    // or the ticks buffer is empty.
    do {
      result = ProcessOneSample();
      if (result == FoundSampleForNextCodeEvent) {
        // All ticks of the current last_processed_code_event_id_ are
        // processed, proceed to the next code event.
        ProcessCodeEvent();
      }
      now = base::TimeTicks::Now();
    } while (result != NoSamplesInQueue && now < nextSampleTime);

    if (nextSampleTime > now) {
#if V8_OS_WIN
      if (use_precise_sampling_ &&
          nextSampleTime - now < base::TimeDelta::FromMilliseconds(100)) {
        if (precise_sleep_timer_.IsInitialized()) {
          precise_sleep_timer_.Sleep(nextSampleTime - now);
        } else {
          // Do not use Sleep on Windows as it is very imprecise, with up to
          // 16ms jitter, which is unacceptable for short profile intervals.
          while (base::TimeTicks::Now() < nextSampleTime) {
          }
        }
      } else  // NOLINT
#else
      USE(use_precise_sampling_);
#endif  // V8_OS_WIN
      {
        // Allow another thread to interrupt the delay between samples in the
        // event of profiler shutdown.
        while (now < nextSampleTime &&
               running_cond_.WaitFor(&running_mutex_, nextSampleTime - now)) {
          // If true was returned, we got interrupted before the timeout
          // elapsed. If this was not due to a change in running state, a
          // spurious wakeup occurred (thus we should continue to wait).
          if (!running_.load(std::memory_order_relaxed)) {
            break;
          }
          now = base::TimeTicks::Now();
        }
      }
    }

    // Schedule next sample.
    sampler_->DoSample();
  }

  // Process remaining tick events.
  do {
    SampleProcessingResult result;
    do {
      result = ProcessOneSample();
    } while (result == OneSampleProcessed);
  } while (ProcessCodeEvent());
}

void SamplingEventsProcessor::SetSamplingInterval(base::TimeDelta period) {
  if (period_ == period) return;
  StopSynchronously();

  period_ = period;
  running_.store(true, std::memory_order_relaxed);

  CHECK(StartSynchronously());
}

void* SamplingEventsProcessor::operator new(size_t size) {
  return AlignedAllocWithRetry(size, alignof(SamplingEventsProcessor));
}

void SamplingEventsProcessor::operator delete(void* ptr) { AlignedFree(ptr); }

ProfilerCodeObserver::ProfilerCodeObserver(Isolate* isolate,
                                           CodeEntryStorage& storage)
    : isolate_(isolate),
      code_entries_(storage),
      code_map_(storage),
      weak_code_registry_(isolate),
      processor_(nullptr) {
  CreateEntriesForRuntimeCallStats();
  LogBuiltins();
}

void ProfilerCodeObserver::ClearCodeMap() {
  weak_code_registry_.Clear();
  code_map_.Clear();
}

void ProfilerCodeObserver::CodeEventHandler(
    const CodeEventsContainer& evt_rec) {
  if (processor_) {
    processor_->CodeEventHandler(evt_rec);
    return;
  }
  CodeEventHandlerInternal(evt_rec);
}

size_t ProfilerCodeObserver::GetEstimatedMemoryUsage() const {
  // To avoid race condition in codemap,
  // for now limit computation in kEagerLogging mode
  if (!processor_) {
    return sizeof(*this) + code_map_.GetEstimatedMemoryUsage() +
           code_entries_.strings().GetStringSize();
  }
  return 0;
}

void ProfilerCodeObserver::CodeEventHandlerInternal(
    const CodeEventsContainer& evt_rec) {
  CodeEventsContainer record = evt_rec;
  switch (evt_rec.generic.type) {
#define PROFILER_TYPE_CASE(type, clss)        \
  case CodeEventRecord::Type::type:           \
    record.clss##_.UpdateCodeMap(&code_map_); \
    break;

    CODE_EVENTS_TYPE_LIST(PROFILER_TYPE_CASE)

#undef PROFILER_TYPE_CASE
    default:
      break;
  }
}

void ProfilerCodeObserver::CreateEntriesForRuntimeCallStats() {
#ifdef V8_RUNTIME_CALL_STATS
  RuntimeCallStats* rcs = isolate_->counters()->runtime_call_stats();
  for (int i = 0; i < RuntimeCallStats::kNumberOfCounters; ++i) {
    RuntimeCallCounter* counter = rcs->GetCounter(i);
    DCHECK(counter->name());
    auto entry = code_entries_.Create(LogEventListener::CodeTag::kFunction,
                                      counter->name(), "native V8Runtime");
    code_map_.AddCode(reinterpret_cast<Address>(counter), entry, 1);
  }
#endif  // V8_RUNTIME_CALL_STATS
}

void ProfilerCodeObserver::LogBuiltins() {
  Builtins* builtins = isolate_->builtins();
  DCHECK(builtins->is_initialized());
  for (Builtin builtin = Builtins::kFirst; builtin <= Builtins::kLast;
       ++builtin) {
    CodeEventsContainer evt_rec(CodeEventRecord::Type::kReportBuiltin);
    ReportBuiltinEventRecord* rec = &evt_rec.ReportBuiltinEventRecord_;
    Tagged<Code> code = builtins->code(builtin);
    rec->instruction_start = code->instruction_start();
    rec->instruction_size = code->instruction_size();
    rec->builtin = builtin;
    CodeEventHandlerInternal(evt_rec);
  }
}

int CpuProfiler::GetProfilesCount() {
  // The count of profiles doesn't depend on a security token.
  return static_cast<int>(profiles_->profiles()->size());
}


CpuProfile* CpuProfiler::GetProfile(int index) {
  return profiles_->profiles()->at(index).get();
}


void CpuProfiler::DeleteAllProfiles() {
  if (is_profiling_) StopProcessor();
  ResetProfiles();
}


void CpuProfiler::DeleteProfile(CpuProfile* profile) {
  profiles_->RemoveProfile(profile);
  if (profiles_->profiles()->empty() && !is_profiling_) {
    // If this was the last profile, clean up all accessory data as well.
    ResetProfiles();
  }
}

namespace {

class CpuProfilersManager {
 public:
  void AddProfiler(Isolate* isolate, CpuProfiler* profiler) {
    base::MutexGuard lock(&mutex_);
    profilers_.emplace(isolate, profiler);
  }

  void RemoveProfiler(Isolate* isolate, CpuProfiler* profiler) {
    base::MutexGuard lock(&mutex_);
    auto range = profilers_.equal_range(isolate);
    for (auto it = range.first; it != range.second; ++it) {
      if (it->second != profiler) continue;
      profilers_.erase(it);
      return;
    }
    UNREACHABLE();
  }

  void CallCollectSample(Isolate* isolate) {
    base::MutexGuard lock(&mutex_);
    auto range = profilers_.equal_range(isolate);
    for (auto it = range.first; it != range.second; ++it) {
      it->second->CollectSample();
    }
  }

  size_t GetAllProfilersMemorySize(Isolate* isolate) {
    base::MutexGuard lock(&mutex_);
    size_t estimated_memory = 0;
    auto range = profilers_.equal_range(isolate);
    for (auto it = range.first; it != range.second; ++it) {
      estimated_memory += it->second->GetEstimatedMemoryUsage();
    }
    return estimated_memory;
  }

 private:
  std::unordered_multimap<Isolate*, CpuProfiler*> profilers_;
  base::Mutex mutex_;
};

DEFINE_LAZY_LEAKY_OBJECT_GETTER(CpuProfilersManager, GetProfilersManager)

}  // namespace

CpuProfiler::CpuProfiler(Isolate* isolate, CpuProfilingNamingMode naming_mode,
                         CpuProfilingLoggingMode logging_mode)
    : CpuProfiler(isolate, naming_mode, logging_mode,
                  new CpuProfilesCollection(isolate), nullptr, nullptr,
                  new ProfilerCodeObserver(isolate, code_entries_)) {}

CpuProfiler::CpuProfiler(Isolate* isolate, CpuProfilingNamingMode naming_mode,
                         CpuProfilingLoggingMode logging_mode,
                         CpuProfilesCollection* test_profiles,
                         Symbolizer* test_symbolizer,
                         ProfilerEventsProcessor* test_processor,
                         ProfilerCodeObserver* test_code_observer)
    : isolate_(isolate),
      naming_mode_(naming_mode),
      logging_mode_(logging_mode),
      base_sampling_interval_(base::TimeDelta::FromMicroseconds(
          v8_flags.cpu_profiler_sampling_interval)),
      code_observer_(test_code_observer),
      profiles_(test_profiles),
      symbolizer_(test_symbolizer),
      processor_(test_processor),
      is_profiling_(false) {
  profiles_->set_cpu_profiler(this);
  GetProfilersManager()->AddProfiler(isolate, this);

  if (logging_mode == kEagerLogging) EnableLogging();
}

CpuProfiler::~CpuProfiler() {
  DCHECK(!is_profiling_);
  GetProfilersManager()->RemoveProfiler(isolate_, this);

  DisableLogging();
  profiles_.reset();

  // We don't currently expect any references to refcounted strings to be
  // maintained with zero profiles after the code map is cleared.
  DCHECK(code_entries_.strings().empty());
}

void CpuProfiler::set_sampling_interval(base::TimeDelta value) {
  DCHECK(!is_profiling_);
  base_sampling_interval_ = value;
}

void CpuProfiler::set_use_precise_sampling(bool value) {
  DCHECK(!is_profiling_);
  use_precise_sampling_ = value;
}

void CpuProfiler::ResetProfiles() {
  profiles_.reset(new CpuProfilesCollection(isolate_));
  profiles_->set_cpu_profiler(this);
}

void CpuProfiler::EnableLogging() {
  if (profiling_scope_) return;

  if (!profiler_listener_) {
    profiler_listener_.reset(new ProfilerListener(
        isolate_, code_observer_.get(), *code_observer_->code_entries(),
        *code_observer_->weak_code_registry(), naming_mode_));
  }
  profiling_scope_.reset(
      new ProfilingScope(isolate_, profiler_listener_.get()));
}

void CpuProfiler::DisableLogging() {
  if (!profiling_scope_) return;

  DCHECK(profiler_listener_);
  profiling_scope_.reset();
  profiler_listener_.reset();
  code_observer_->ClearCodeMap();
}

base::TimeDelta CpuProfiler::ComputeSamplingInterval() {
  return profiles_->GetCommonSamplingInterval();
}

void CpuProfiler::AdjustSamplingInterval() {
  if (!processor_) return;

  base::TimeDelta base_interval = ComputeSamplingInterval();
  processor_->SetSamplingInterval(base_interval);
}

// static
void CpuProfiler::CollectSample(Isolate* isolate) {
  GetProfilersManager()->CallCollectSample(isolate);
}

void CpuProfiler::CollectSample() {
  if (processor_) {
    processor_->AddCurrentStack();
  }
}

// static
size_t CpuProfiler::GetAllProfilersMemorySize(Isolate* isolate) {
  return GetProfilersManager()->GetAllProfilersMemorySize(isolate);
}

size_t CpuProfiler::GetEstimatedMemoryUsage() const {
  return code_observer_->GetEstimatedMemoryUsage();
}

CpuProfilingResult CpuProfiler::StartProfiling(
    CpuProfilingOptions options,
    std::unique_ptr<DiscardedSamplesDelegate> delegate) {
  return StartProfiling(nullptr, std::move(options), std::move(delegate));
}

CpuProfilingResult CpuProfiler::StartProfiling(
    const char* title, CpuProfilingOptions options,
    std::unique_ptr<DiscardedSamplesDelegate> delegate) {
  CpuProfilingResult result =
      profiles_->StartProfiling(title, std::move(options), std::move(delegate));

  // TODO(nicodubus): Revisit logic for if we want to do anything different for
  // kAlreadyStarted
  if (result.status == CpuProfilingStatus::kStarted ||
      result.status == CpuProfilingStatus::kAlreadyStarted) {
    TRACE_EVENT0("v8", "CpuProfiler::StartProfiling");
    AdjustSamplingInterval();
    StartProcessorIfNotStarted();
  }

  return result;
}

CpuProfilingResult CpuProfiler::StartProfiling(
    Tagged<String> title, CpuProfilingOptions options,
    std::unique_ptr<DiscardedSamplesDelegate> delegate) {
  return StartProfiling(profiles_->GetName(title), std::move(options),
                        std::move(delegate));
}

void CpuProfiler::StartProcessorIfNotStarted() {
  if (processor_) {
    processor_->AddCurrentStack();
    return;
  }

  if (!profiling_scope_) {
    DCHECK_EQ(logging_mode_, kLazyLogging);
    EnableLogging();
  }

  if (!symbolizer_) {
    symbolizer_ =
        std::make_unique<Symbolizer>(code_observer_->instruction_stream_map());
  }

  base::TimeDelta sampling_interval = ComputeSamplingInterval();
  processor_.reset(new SamplingEventsProcessor(
      isolate_, symbolizer_.get(), code_observer_.get(), profiles_.get(),
      sampling_interval, use_precise_sampling_));
  is_profiling_ = true;

  // Enable stack sampling.
  processor_->AddCurrentStack();
  CHECK(processor_->StartSynchronously());
}

CpuProfile* CpuProfiler::StopProfiling(const char* title) {
  CpuProfile* profile = profiles_->Lookup(title);
  if (profile) {
    return StopProfiling(profile->id());
  }
  return nullptr;
}

CpuProfile* CpuProfiler::StopProfiling(ProfilerId id) {
  if (!is_profiling_) return nullptr;
  const bool last_profile = profiles_->IsLastProfileLeft(id);
  if (last_profile) StopProcessor();

  CpuProfile* profile = profiles_->StopProfiling(id);

  AdjustSamplingInterval();

  DCHECK(profiling_scope_);
  if (last_profile && logging_mode_ == kLazyLogging) {
    DisableLogging();
  }

  return profile;
}

CpuProfile* CpuProfiler::StopProfiling(Tagged<String> title) {
  return StopProfiling(profiles_->GetName(title));
}

void CpuProfiler::StopProcessor() {
  is_profiling_ = false;
  processor_->StopSynchronously();
  processor_.reset();
}
}  // namespace internal
}  // namespace v8

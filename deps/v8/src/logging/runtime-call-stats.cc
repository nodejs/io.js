// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef V8_RUNTIME_CALL_STATS

#include "src/logging/runtime-call-stats.h"

#include <iomanip>

#include "src/flags/flags.h"
#include "src/tracing/tracing-category-observer.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

base::TimeTicks (*RuntimeCallTimer::Now)() = &base::TimeTicks::Now;

base::TimeTicks RuntimeCallTimer::NowCPUTime() {
  base::ThreadTicks ticks = base::ThreadTicks::Now();
  return base::TimeTicks::FromInternalValue(ticks.ToInternalValue());
}

class RuntimeCallStatEntries {
 public:
  void Print(std::ostream& os) {
    if (total_call_count_ == 0) return;
    std::sort(entries_.rbegin(), entries_.rend());
    os << std::setw(50) << "Runtime Function/C++ Builtin" << std::setw(12)
       << "Time" << std::setw(18) << "Count" << std::endl
       << std::string(88, '=') << std::endl;
    for (Entry& entry : entries_) {
      entry.SetTotal(total_time_, total_call_count_);
      entry.Print(os);
    }
    os << std::string(88, '-') << std::endl;
    Entry("Total", total_time_, total_call_count_).Print(os);
  }

  // By default, the compiler will usually inline this, which results in a large
  // binary size increase: std::vector::push_back expands to a large amount of
  // instructions, and this function is invoked repeatedly by macros.
  V8_NOINLINE void Add(RuntimeCallCounter* counter) {
    if (counter->count() == 0) return;
    entries_.push_back(
        Entry(counter->name(), counter->time(), counter->count()));
    total_time_ += counter->time();
    total_call_count_ += counter->count();
  }

 private:
  class Entry {
   public:
    Entry(const char* name, base::TimeDelta time, uint64_t count)
        : name_(name),
          time_(time.InMicroseconds()),
          count_(count),
          time_percent_(100),
          count_percent_(100) {}

    bool operator<(const Entry& other) const {
      if (time_ < other.time_) return true;
      if (time_ > other.time_) return false;
      return count_ < other.count_;
    }

    V8_NOINLINE void Print(std::ostream& os) {
      os.precision(2);
      os << std::fixed << std::setprecision(2);
      os << std::setw(50) << name_;
      os << std::setw(10) << static_cast<double>(time_) / 1000 << "ms ";
      os << std::setw(6) << time_percent_ << "%";
      os << std::setw(10) << count_ << " ";
      os << std::setw(6) << count_percent_ << "%";
      os << std::endl;
    }

    V8_NOINLINE void SetTotal(base::TimeDelta total_time,
                              uint64_t total_count) {
      if (total_time.InMicroseconds() == 0) {
        time_percent_ = 0;
      } else {
        time_percent_ = 100.0 * time_ / total_time.InMicroseconds();
      }
      count_percent_ = 100.0 * count_ / total_count;
    }

   private:
    const char* name_;
    int64_t time_;
    uint64_t count_;
    double time_percent_;
    double count_percent_;
  };

  uint64_t total_call_count_ = 0;
  base::TimeDelta total_time_;
  std::vector<Entry> entries_;
};

void RuntimeCallCounter::Reset() {
  count_ = 0;
  time_ = 0;
}

void RuntimeCallCounter::Dump(v8::tracing::TracedValue* value) {
  value->BeginArray(name_);
  value->AppendDouble(count_);
  value->AppendDouble(time_);
  value->EndArray();
}

void RuntimeCallCounter::Add(RuntimeCallCounter* other) {
  count_ += other->count();
  time_ += other->time().InMicroseconds();
}

void RuntimeCallTimer::Snapshot() {
  base::TimeTicks now = Now();
  // Pause only / topmost timer in the timer stack.
  Pause(now);
  // Commit all the timer's elapsed time to the counters.
  RuntimeCallTimer* timer = this;
  while (timer != nullptr) {
    timer->CommitTimeToCounter();
    timer = timer->parent();
  }
  Resume(now);
}

RuntimeCallStats::RuntimeCallStats(ThreadType thread_type)
    : in_use_(false), thread_type_(thread_type) {
  static const char* const kNames[] = {
#define CALL_BUILTIN_COUNTER(name) "GC_" #name,
      FOR_EACH_GC_COUNTER(CALL_BUILTIN_COUNTER)  //
#undef CALL_BUILTIN_COUNTER
#define CALL_RUNTIME_COUNTER(name) #name,
      FOR_EACH_MANUAL_COUNTER(CALL_RUNTIME_COUNTER)  //
#undef CALL_RUNTIME_COUNTER
#define CALL_RUNTIME_COUNTER(name, nargs, ressize) #name,
      FOR_EACH_INTRINSIC(CALL_RUNTIME_COUNTER)  //
#undef CALL_RUNTIME_COUNTER
#define CALL_BUILTIN_COUNTER(name) #name,
      BUILTIN_LIST_C(CALL_BUILTIN_COUNTER)  //
#undef CALL_BUILTIN_COUNTER
#define CALL_BUILTIN_COUNTER(name) "API_" #name,
      FOR_EACH_API_COUNTER(CALL_BUILTIN_COUNTER)  //
#undef CALL_BUILTIN_COUNTER
#define CALL_BUILTIN_COUNTER(name) #name,
      FOR_EACH_HANDLER_COUNTER(CALL_BUILTIN_COUNTER)  //
#undef CALL_BUILTIN_COUNTER
#define THREAD_SPECIFIC_COUNTER(name) #name,
      FOR_EACH_THREAD_SPECIFIC_COUNTER(THREAD_SPECIFIC_COUNTER)  //
#undef THREAD_SPECIFIC_COUNTER
  };
  for (int i = 0; i < kNumberOfCounters; i++) {
    this->counters_[i] = RuntimeCallCounter(kNames[i]);
  }
  if (v8_flags.rcs_cpu_time) {
    CHECK(base::ThreadTicks::IsSupported());
    base::ThreadTicks::WaitUntilInitialized();
    RuntimeCallTimer::Now = &RuntimeCallTimer::NowCPUTime;
  }
}

namespace {
constexpr RuntimeCallCounterId FirstCounter(RuntimeCallCounterId first, ...) {
  return first;
}

#define THREAD_SPECIFIC_COUNTER(name) k##name,
constexpr RuntimeCallCounterId kFirstThreadVariantCounter =
    FirstCounter(FOR_EACH_THREAD_SPECIFIC_COUNTER(THREAD_SPECIFIC_COUNTER) 0);
#undef THREAD_SPECIFIC_COUNTER

#define THREAD_SPECIFIC_COUNTER(name) +1
constexpr int kThreadVariantCounterCount =
    0 FOR_EACH_THREAD_SPECIFIC_COUNTER(THREAD_SPECIFIC_COUNTER);
#undef THREAD_SPECIFIC_COUNTER

constexpr auto kLastThreadVariantCounter = static_cast<RuntimeCallCounterId>(
    kFirstThreadVariantCounter + kThreadVariantCounterCount - 1);
}  // namespace

bool RuntimeCallStats::HasThreadSpecificCounterVariants(
    RuntimeCallCounterId id) {
  // Check that it's in the range of the thread-specific variant counters and
  // also that it's one of the background counters.
  return id >= kFirstThreadVariantCounter && id <= kLastThreadVariantCounter;
}

bool RuntimeCallStats::IsBackgroundThreadSpecificVariant(
    RuntimeCallCounterId id) {
  return HasThreadSpecificCounterVariants(id) &&
         (id - kFirstThreadVariantCounter) % 2 == 1;
}

void RuntimeCallStats::Enter(RuntimeCallTimer* timer,
                             RuntimeCallCounterId counter_id) {
  DCHECK(IsCalledOnTheSameThread());
  RuntimeCallCounter* counter = GetCounter(counter_id);
  DCHECK_NOT_NULL(counter->name());
  timer->Start(counter, current_timer());
  current_timer_.SetValue(timer);
  current_counter_.SetValue(counter);
}

void RuntimeCallStats::Leave(RuntimeCallTimer* timer) {
  DCHECK(IsCalledOnTheSameThread());
  RuntimeCallTimer* stack_top = current_timer();
  if (stack_top == nullptr) return;  // Missing timer is a result of Reset().
  CHECK(stack_top == timer);
  current_timer_.SetValue(timer->Stop());
  RuntimeCallTimer* cur_timer = current_timer();
  current_counter_.SetValue(cur_timer ? cur_timer->counter() : nullptr);
}

void RuntimeCallStats::Add(RuntimeCallStats* other) {
  for (int i = 0; i < kNumberOfCounters; i++) {
    GetCounter(i)->Add(other->GetCounter(i));
  }
}

// static
void RuntimeCallStats::CorrectCurrentCounterId(RuntimeCallCounterId counter_id,
                                               CounterMode mode) {
  DCHECK(IsCalledOnTheSameThread());
  if (mode == RuntimeCallStats::CounterMode::kThreadSpecific) {
    counter_id = CounterIdForThread(counter_id);
  }
  DCHECK(IsCounterAppropriateForThread(counter_id));

  RuntimeCallTimer* timer = current_timer();
  if (timer == nullptr) return;
  RuntimeCallCounter* counter = GetCounter(counter_id);
  timer->set_counter(counter);
  current_counter_.SetValue(counter);
}

bool RuntimeCallStats::IsCalledOnTheSameThread() {
  if (thread_id_.IsValid()) return thread_id_ == ThreadId::Current();
  thread_id_ = ThreadId::Current();
  return true;
}

void RuntimeCallStats::Print() {
  StdoutStream os;
  Print(os);
}

void RuntimeCallStats::Print(std::ostream& os) {
  RuntimeCallStatEntries entries;
  if (current_timer_.Value() != nullptr) {
    current_timer_.Value()->Snapshot();
  }
  for (int i = 0; i < kNumberOfCounters; i++) {
    entries.Add(GetCounter(i));
  }
  entries.Print(os);
}

void RuntimeCallStats::Reset() {
  if (V8_LIKELY(!TracingFlags::is_runtime_stats_enabled())) return;

  // In tracing, we only what to trace the time spent on top level trace events,
  // if runtime counter stack is not empty, we should clear the whole runtime
  // counter stack, and then reset counters so that we can dump counters into
  // top level trace events accurately.
  while (current_timer_.Value()) {
    current_timer_.SetValue(current_timer_.Value()->Stop());
  }

  for (int i = 0; i < kNumberOfCounters; i++) {
    GetCounter(i)->Reset();
  }

  in_use_ = true;
}

void RuntimeCallStats::Dump(v8::tracing::TracedValue* value) {
  for (int i = 0; i < kNumberOfCounters; i++) {
    if (GetCounter(i)->count() > 0) GetCounter(i)->Dump(value);
  }
  in_use_ = false;
}

WorkerThreadRuntimeCallStats::WorkerThreadRuntimeCallStats()
    : isolate_thread_id_(ThreadId::Current()) {}

WorkerThreadRuntimeCallStats::~WorkerThreadRuntimeCallStats() {
  if (tls_key_) base::Thread::DeleteThreadLocalKey(*tls_key_);
}

base::Thread::LocalStorageKey WorkerThreadRuntimeCallStats::GetKey() {
  base::MutexGuard lock(&mutex_);
  if (!tls_key_) tls_key_ = base::Thread::CreateThreadLocalKey();
  return *tls_key_;
}

RuntimeCallStats* WorkerThreadRuntimeCallStats::NewTable() {
  // Never create a new worker table on the isolate's main thread.
  DCHECK_NE(ThreadId::Current(), isolate_thread_id_);
  std::unique_ptr<RuntimeCallStats> new_table =
      std::make_unique<RuntimeCallStats>(RuntimeCallStats::kWorkerThread);
  RuntimeCallStats* result = new_table.get();

  base::MutexGuard lock(&mutex_);
  tables_.push_back(std::move(new_table));
  return result;
}

void WorkerThreadRuntimeCallStats::AddToMainTable(
    RuntimeCallStats* main_call_stats) {
  base::MutexGuard lock(&mutex_);
  for (auto& worker_stats : tables_) {
    DCHECK_NE(main_call_stats, worker_stats.get());
    main_call_stats->Add(worker_stats.get());
    worker_stats->Reset();
  }
}

WorkerThreadRuntimeCallStatsScope::WorkerThreadRuntimeCallStatsScope(
    WorkerThreadRuntimeCallStats* worker_stats) {
  if (V8_LIKELY(!TracingFlags::is_runtime_stats_enabled())) return;

  table_ = reinterpret_cast<RuntimeCallStats*>(
      base::Thread::GetThreadLocal(worker_stats->GetKey()));
  if (table_ == nullptr) {
    if (V8_UNLIKELY(!TracingFlags::is_runtime_stats_enabled())) return;
    table_ = worker_stats->NewTable();
    base::Thread::SetThreadLocal(worker_stats->GetKey(), table_);
  }

  if ((TracingFlags::runtime_stats.load(std::memory_order_relaxed) &
       v8::tracing::TracingCategoryObserver::ENABLED_BY_TRACING)) {
    table_->Reset();
  }
}

WorkerThreadRuntimeCallStatsScope::~WorkerThreadRuntimeCallStatsScope() {
  if (V8_LIKELY(table_ == nullptr)) return;

  if ((TracingFlags::runtime_stats.load(std::memory_order_relaxed) &
       v8::tracing::TracingCategoryObserver::ENABLED_BY_TRACING)) {
    auto value = v8::tracing::TracedValue::Create();
    table_->Dump(value.get());
    TRACE_EVENT_INSTANT1(TRACE_DISABLED_BY_DEFAULT("v8.runtime_stats"),
                         "V8.RuntimeStats", TRACE_EVENT_SCOPE_THREAD,
                         "runtime-call-stats", std::move(value));
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_RUNTIME_CALL_STATS

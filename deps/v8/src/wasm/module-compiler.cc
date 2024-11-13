// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/module-compiler.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <queue>

#include "src/api/api-inl.h"
#include "src/base/enum-set.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/semaphore.h"
#include "src/base/platform/time.h"
#include "src/codegen/compiler.h"
#include "src/compiler/wasm-compiler.h"
#include "src/debug/debug.h"
#include "src/handles/global-handles-inl.h"
#include "src/logging/counters-scopes.h"
#include "src/logging/metrics.h"
#include "src/tracing/trace-event.h"
#include "src/wasm/code-space-access.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/jump-table-assembler.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/pgo.h"
#include "src/wasm/std-object-sizes.h"
#include "src/wasm/streaming-decoder.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-feature-flags.h"
#include "src/wasm/wasm-import-wrapper-cache.h"
#include "src/wasm/wasm-js.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-result.h"
#include "src/wasm/wasm-serialization.h"

#define TRACE_COMPILE(...)                                 \
  do {                                                     \
    if (v8_flags.trace_wasm_compiler) PrintF(__VA_ARGS__); \
  } while (false)

#define TRACE_STREAMING(...)                                \
  do {                                                      \
    if (v8_flags.trace_wasm_streaming) PrintF(__VA_ARGS__); \
  } while (false)

#define TRACE_LAZY(...)                                            \
  do {                                                             \
    if (v8_flags.trace_wasm_lazy_compilation) PrintF(__VA_ARGS__); \
  } while (false)

namespace v8::internal::wasm {

namespace {

enum class CompileStrategy : uint8_t {
  // Compiles functions on first use. In this case, execution will block until
  // the function's baseline is reached and top tier compilation starts in
  // background (if applicable).
  // Lazy compilation can help to reduce startup time and code size at the risk
  // of blocking execution.
  kLazy,
  // Compiles baseline ahead of execution and starts top tier compilation in
  // background (if applicable).
  kEager,
  // Triggers baseline compilation on first use (just like {kLazy}) with the
  // difference that top tier compilation is started eagerly.
  // This strategy can help to reduce startup time at the risk of blocking
  // execution, but only in its early phase (until top tier compilation
  // finishes).
  kLazyBaselineEagerTopTier,
  // Marker for default strategy.
  kDefault = kEager,
};

class CompilationStateImpl;
class CompilationUnitBuilder;

class V8_NODISCARD BackgroundCompileScope {
 public:
  explicit BackgroundCompileScope(std::weak_ptr<NativeModule> native_module)
      : native_module_(native_module.lock()) {}

  NativeModule* native_module() const {
    DCHECK(native_module_);
    return native_module_.get();
  }
  inline CompilationStateImpl* compilation_state() const;

  bool cancelled() const;

 private:
  // Keep the native module alive while in this scope.
  std::shared_ptr<NativeModule> native_module_;
};

enum CompilationTier { kBaseline = 0, kTopTier = 1, kNumTiers = kTopTier + 1 };

// A set of work-stealing queues (vectors of units). Each background compile
// task owns one of the queues and steals from all others once its own queue
// runs empty.
class CompilationUnitQueues {
 public:
  // Public API for QueueImpl.
  struct Queue {
    bool ShouldPublish(int num_processed_units) const;
  };

  explicit CompilationUnitQueues(int num_declared_functions)
      : num_declared_functions_(num_declared_functions) {
    // Add one first queue, to add units to.
    queues_.emplace_back(std::make_unique<QueueImpl>(0));

#if !defined(__cpp_lib_atomic_value_initialization) || \
    __cpp_lib_atomic_value_initialization < 201911L
    for (auto& atomic_counter : num_units_) {
      std::atomic_init(&atomic_counter, size_t{0});
    }
#endif

    top_tier_compiled_ =
        std::make_unique<std::atomic<bool>[]>(num_declared_functions);

#if !defined(__cpp_lib_atomic_value_initialization) || \
    __cpp_lib_atomic_value_initialization < 201911L
    for (int i = 0; i < num_declared_functions; i++) {
      std::atomic_init(&top_tier_compiled_.get()[i], false);
    }
#endif
  }

  Queue* GetQueueForTask(int task_id) {
    int required_queues = task_id + 1;
    {
      base::SharedMutexGuard<base::kShared> queues_guard{&queues_mutex_};
      if (V8_LIKELY(static_cast<int>(queues_.size()) >= required_queues)) {
        return queues_[task_id].get();
      }
    }

    // Otherwise increase the number of queues.
    base::SharedMutexGuard<base::kExclusive> queues_guard{&queues_mutex_};
    int num_queues = static_cast<int>(queues_.size());
    while (num_queues < required_queues) {
      int steal_from = num_queues + 1;
      queues_.emplace_back(std::make_unique<QueueImpl>(steal_from));
      ++num_queues;
    }

    // Update the {publish_limit}s of all queues.

    // We want background threads to publish regularly (to avoid contention when
    // they are all publishing at the end). On the other side, each publishing
    // has some overhead (part of it for synchronizing between threads), so it
    // should not happen *too* often. Thus aim for 4-8 publishes per thread, but
    // distribute it such that publishing is likely to happen at different
    // times.
    int units_per_thread = num_declared_functions_ / num_queues;
    int min = std::max(10, units_per_thread / 8);
    int queue_id = 0;
    for (auto& queue : queues_) {
      // Set a limit between {min} and {2*min}, but not smaller than {10}.
      int limit = min + (min * queue_id / num_queues);
      queue->publish_limit.store(limit, std::memory_order_relaxed);
      ++queue_id;
    }

    return queues_[task_id].get();
  }

  std::optional<WasmCompilationUnit> GetNextUnit(Queue* queue,
                                                 CompilationTier tier) {
    DCHECK_LT(tier, CompilationTier::kNumTiers);
    if (auto unit = GetNextUnitOfTier(queue, tier)) {
      [[maybe_unused]] size_t old_units_count =
          num_units_[tier].fetch_sub(1, std::memory_order_relaxed);
      DCHECK_LE(1, old_units_count);
      return unit;
    }
    return {};
  }

  void AddUnits(base::Vector<WasmCompilationUnit> baseline_units,
                base::Vector<WasmCompilationUnit> top_tier_units,
                const WasmModule* module) {
    DCHECK_LT(0, baseline_units.size() + top_tier_units.size());
    // Add to the individual queues in a round-robin fashion. No special care is
    // taken to balance them; they will be balanced by work stealing.
    QueueImpl* queue;
    {
      int queue_to_add = next_queue_to_add.load(std::memory_order_relaxed);
      base::SharedMutexGuard<base::kShared> queues_guard{&queues_mutex_};
      while (!next_queue_to_add.compare_exchange_weak(
          queue_to_add, next_task_id(queue_to_add, queues_.size()),
          std::memory_order_relaxed)) {
        // Retry with updated {queue_to_add}.
      }
      queue = queues_[queue_to_add].get();
    }

    base::MutexGuard guard(&queue->mutex);
    std::optional<base::MutexGuard> big_units_guard;
    for (auto pair :
         {std::make_pair(CompilationTier::kBaseline, baseline_units),
          std::make_pair(CompilationTier::kTopTier, top_tier_units)}) {
      int tier = pair.first;
      base::Vector<WasmCompilationUnit> units = pair.second;
      if (units.empty()) continue;
      num_units_[tier].fetch_add(units.size(), std::memory_order_relaxed);
      for (WasmCompilationUnit unit : units) {
        size_t func_size = module->functions[unit.func_index()].code.length();
        if (func_size <= kBigUnitsLimit) {
          queue->units[tier].push_back(unit);
        } else {
          if (!big_units_guard) {
            big_units_guard.emplace(&big_units_queue_.mutex);
          }
          big_units_queue_.has_units[tier].store(true,
                                                 std::memory_order_relaxed);
          big_units_queue_.units[tier].emplace(func_size, unit);
        }
      }
    }
  }

  void AddTopTierPriorityUnit(WasmCompilationUnit unit, size_t priority) {
    base::SharedMutexGuard<base::kShared> queues_guard{&queues_mutex_};
    // Add to the individual queues in a round-robin fashion. No special care is
    // taken to balance them; they will be balanced by work stealing.
    // Priorities should only be seen as a hint here; without balancing, we
    // might pop a unit with lower priority from one queue while other queues
    // still hold higher-priority units.
    // Since updating priorities in a std::priority_queue is difficult, we just
    // add new units with higher priorities, and use the
    // {CompilationUnitQueues::top_tier_compiled_} array to discard units for
    // functions which are already being compiled.
    int queue_to_add = next_queue_to_add.load(std::memory_order_relaxed);
    while (!next_queue_to_add.compare_exchange_weak(
        queue_to_add, next_task_id(queue_to_add, queues_.size()),
        std::memory_order_relaxed)) {
      // Retry with updated {queue_to_add}.
    }

    {
      auto* queue = queues_[queue_to_add].get();
      base::MutexGuard guard(&queue->mutex);
      queue->top_tier_priority_units.emplace(priority, unit);
      num_priority_units_.fetch_add(1, std::memory_order_relaxed);
      num_units_[CompilationTier::kTopTier].fetch_add(
          1, std::memory_order_relaxed);
    }
  }

  // Get the current number of units in the queue for |tier|. This is only a
  // momentary snapshot, it's not guaranteed that {GetNextUnit} returns a unit
  // if this method returns non-zero.
  size_t GetSizeForTier(CompilationTier tier) const {
    DCHECK_LT(tier, CompilationTier::kNumTiers);
    return num_units_[tier].load(std::memory_order_relaxed);
  }

  void AllowAnotherTopTierJob(uint32_t func_index) {
    top_tier_compiled_[func_index].store(false, std::memory_order_relaxed);
  }

  void AllowAnotherTopTierJobForAllFunctions() {
    for (int i = 0; i < num_declared_functions_; i++) {
      AllowAnotherTopTierJob(i);
    }
  }

  size_t EstimateCurrentMemoryConsumption() const;

 private:
  // Functions bigger than {kBigUnitsLimit} will be compiled first, in ascending
  // order of their function body size.
  static constexpr size_t kBigUnitsLimit = 4096;

  struct BigUnit {
    BigUnit(size_t func_size, WasmCompilationUnit unit)
        : func_size{func_size}, unit(unit) {}

    size_t func_size;
    WasmCompilationUnit unit;

    bool operator<(const BigUnit& other) const {
      return func_size < other.func_size;
    }
  };

  struct TopTierPriorityUnit {
    TopTierPriorityUnit(int priority, WasmCompilationUnit unit)
        : priority(priority), unit(unit) {}

    size_t priority;
    WasmCompilationUnit unit;

    bool operator<(const TopTierPriorityUnit& other) const {
      return priority < other.priority;
    }
  };

  struct BigUnitsQueue {
    BigUnitsQueue() {
#if !defined(__cpp_lib_atomic_value_initialization) || \
    __cpp_lib_atomic_value_initialization < 201911L
      for (auto& atomic : has_units) std::atomic_init(&atomic, false);
#endif
    }

    mutable base::Mutex mutex;

    // Can be read concurrently to check whether any elements are in the queue.
    std::atomic<bool> has_units[CompilationTier::kNumTiers];

    // Protected by {mutex}:
    std::priority_queue<BigUnit> units[CompilationTier::kNumTiers];
  };

  struct QueueImpl : public Queue {
    explicit QueueImpl(int next_steal_task_id)
        : next_steal_task_id(next_steal_task_id) {}

    // Number of units after which the task processing this queue should publish
    // compilation results. Updated (reduced, using relaxed ordering) when new
    // queues are allocated. If there is only one thread running, we can delay
    // publishing arbitrarily.
    std::atomic<int> publish_limit{kMaxInt};

    base::Mutex mutex;

    // All fields below are protected by {mutex}.
    std::vector<WasmCompilationUnit> units[CompilationTier::kNumTiers];
    std::priority_queue<TopTierPriorityUnit> top_tier_priority_units;
    int next_steal_task_id;
  };

  int next_task_id(int task_id, size_t num_queues) const {
    int next = task_id + 1;
    return next == static_cast<int>(num_queues) ? 0 : next;
  }

  std::optional<WasmCompilationUnit> GetNextUnitOfTier(Queue* public_queue,
                                                       int tier) {
    QueueImpl* queue = static_cast<QueueImpl*>(public_queue);

    // First check whether there is a priority unit. Execute that first.
    if (tier == CompilationTier::kTopTier) {
      if (auto unit = GetTopTierPriorityUnit(queue)) {
        return unit;
      }
    }

    // Then check whether there is a big unit of that tier.
    if (auto unit = GetBigUnitOfTier(tier)) return unit;

    // Finally check whether our own queue has a unit of the wanted tier. If
    // so, return it, otherwise get the task id to steal from.
    int steal_task_id;
    {
      base::MutexGuard mutex_guard(&queue->mutex);
      if (!queue->units[tier].empty()) {
        auto unit = queue->units[tier].back();
        queue->units[tier].pop_back();
        return unit;
      }
      steal_task_id = queue->next_steal_task_id;
    }

    // Try to steal from all other queues. If this succeeds, return one of the
    // stolen units.
    {
      base::SharedMutexGuard<base::kShared> guard{&queues_mutex_};
      for (size_t steal_trials = 0; steal_trials < queues_.size();
           ++steal_trials, ++steal_task_id) {
        if (steal_task_id >= static_cast<int>(queues_.size())) {
          steal_task_id = 0;
        }
        if (auto unit = StealUnitsAndGetFirst(queue, steal_task_id, tier)) {
          return unit;
        }
      }
    }

    // If we reach here, we didn't find any unit of the requested tier.
    return {};
  }

  std::optional<WasmCompilationUnit> GetBigUnitOfTier(int tier) {
    // Fast path without locking.
    if (!big_units_queue_.has_units[tier].load(std::memory_order_relaxed)) {
      return {};
    }
    base::MutexGuard guard(&big_units_queue_.mutex);
    if (big_units_queue_.units[tier].empty()) return {};
    WasmCompilationUnit unit = big_units_queue_.units[tier].top().unit;
    big_units_queue_.units[tier].pop();
    if (big_units_queue_.units[tier].empty()) {
      big_units_queue_.has_units[tier].store(false, std::memory_order_relaxed);
    }
    return unit;
  }

  std::optional<WasmCompilationUnit> GetTopTierPriorityUnit(QueueImpl* queue) {
    // Fast path without locking.
    if (num_priority_units_.load(std::memory_order_relaxed) == 0) {
      return {};
    }

    int steal_task_id;
    {
      base::MutexGuard mutex_guard(&queue->mutex);
      while (!queue->top_tier_priority_units.empty()) {
        auto unit = queue->top_tier_priority_units.top().unit;
        queue->top_tier_priority_units.pop();
        num_priority_units_.fetch_sub(1, std::memory_order_relaxed);

        if (!top_tier_compiled_[unit.func_index()].exchange(
                true, std::memory_order_relaxed)) {
          return unit;
        }
        num_units_[CompilationTier::kTopTier].fetch_sub(
            1, std::memory_order_relaxed);
      }
      steal_task_id = queue->next_steal_task_id;
    }

    // Try to steal from all other queues. If this succeeds, return one of the
    // stolen units.
    {
      base::SharedMutexGuard<base::kShared> guard{&queues_mutex_};
      for (size_t steal_trials = 0; steal_trials < queues_.size();
           ++steal_trials, ++steal_task_id) {
        if (steal_task_id >= static_cast<int>(queues_.size())) {
          steal_task_id = 0;
        }
        if (auto unit = StealTopTierPriorityUnit(queue, steal_task_id)) {
          return unit;
        }
      }
    }

    return {};
  }

  // Steal units of {wanted_tier} from {steal_from_task_id} to {queue}. Return
  // first stolen unit (rest put in queue of {task_id}), or {nullopt} if
  // {steal_from_task_id} had no units of {wanted_tier}.
  // Hold a shared lock on {queues_mutex_} when calling this method.
  std::optional<WasmCompilationUnit> StealUnitsAndGetFirst(
      QueueImpl* queue, int steal_from_task_id, int wanted_tier) {
    auto* steal_queue = queues_[steal_from_task_id].get();
    // Cannot steal from own queue.
    if (steal_queue == queue) return {};
    std::vector<WasmCompilationUnit> stolen;
    std::optional<WasmCompilationUnit> returned_unit;
    {
      base::MutexGuard guard(&steal_queue->mutex);
      auto* steal_from_vector = &steal_queue->units[wanted_tier];
      if (steal_from_vector->empty()) return {};
      size_t remaining = steal_from_vector->size() / 2;
      auto steal_begin = steal_from_vector->begin() + remaining;
      returned_unit = *steal_begin;
      stolen.assign(steal_begin + 1, steal_from_vector->end());
      steal_from_vector->erase(steal_begin, steal_from_vector->end());
    }
    base::MutexGuard guard(&queue->mutex);
    auto* target_queue = &queue->units[wanted_tier];
    target_queue->insert(target_queue->end(), stolen.begin(), stolen.end());
    queue->next_steal_task_id = steal_from_task_id + 1;
    return returned_unit;
  }

  // Steal one priority unit from {steal_from_task_id} to {task_id}. Return
  // stolen unit, or {nullopt} if {steal_from_task_id} had no priority units.
  // Hold a shared lock on {queues_mutex_} when calling this method.
  std::optional<WasmCompilationUnit> StealTopTierPriorityUnit(
      QueueImpl* queue, int steal_from_task_id) {
    auto* steal_queue = queues_[steal_from_task_id].get();
    // Cannot steal from own queue.
    if (steal_queue == queue) return {};
    std::optional<WasmCompilationUnit> returned_unit;
    {
      base::MutexGuard guard(&steal_queue->mutex);
      while (true) {
        if (steal_queue->top_tier_priority_units.empty()) return {};

        auto unit = steal_queue->top_tier_priority_units.top().unit;
        steal_queue->top_tier_priority_units.pop();
        num_priority_units_.fetch_sub(1, std::memory_order_relaxed);

        if (!top_tier_compiled_[unit.func_index()].exchange(
                true, std::memory_order_relaxed)) {
          returned_unit = unit;
          break;
        }
        num_units_[CompilationTier::kTopTier].fetch_sub(
            1, std::memory_order_relaxed);
      }
    }
    base::MutexGuard guard(&queue->mutex);
    queue->next_steal_task_id = steal_from_task_id + 1;
    return returned_unit;
  }

  // {queues_mutex_} protectes {queues_};
  mutable base::SharedMutex queues_mutex_;
  std::vector<std::unique_ptr<QueueImpl>> queues_;

  const int num_declared_functions_;

  BigUnitsQueue big_units_queue_;

  std::atomic<size_t> num_units_[CompilationTier::kNumTiers];
  std::atomic<size_t> num_priority_units_{0};
  std::unique_ptr<std::atomic<bool>[]> top_tier_compiled_;
  std::atomic<int> next_queue_to_add{0};
};

size_t CompilationUnitQueues::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(CompilationUnitQueues, 248);
  UPDATE_WHEN_CLASS_CHANGES(QueueImpl, 144);
  UPDATE_WHEN_CLASS_CHANGES(BigUnitsQueue, 120);
  // Not including sizeof(CompilationUnitQueues) because that's included in
  // sizeof(CompilationStateImpl).
  size_t result = 0;
  {
    base::SharedMutexGuard<base::kShared> lock(&queues_mutex_);
    result += ContentSize(queues_) + queues_.size() * sizeof(QueueImpl);
    for (const auto& q : queues_) {
      base::MutexGuard guard(&q->mutex);
      result += ContentSize(*q->units);
      result += q->top_tier_priority_units.size() * sizeof(TopTierPriorityUnit);
    }
  }
  {
    base::MutexGuard lock(&big_units_queue_.mutex);
    result += big_units_queue_.units[0].size() * sizeof(BigUnit);
    result += big_units_queue_.units[1].size() * sizeof(BigUnit);
  }
  // For {top_tier_compiled_}.
  result += sizeof(std::atomic<bool>) * num_declared_functions_;
  return result;
}

bool CompilationUnitQueues::Queue::ShouldPublish(
    int num_processed_units) const {
  auto* queue = static_cast<const QueueImpl*>(this);
  return num_processed_units >=
         queue->publish_limit.load(std::memory_order_relaxed);
}

// The {CompilationStateImpl} keeps track of the compilation state of the
// owning NativeModule, i.e. which functions are left to be compiled.
// It contains a task manager to allow parallel and asynchronous background
// compilation of functions.
// Its public interface {CompilationState} lives in compilation-environment.h.
class CompilationStateImpl {
 public:
  CompilationStateImpl(const std::shared_ptr<NativeModule>& native_module,
                       std::shared_ptr<Counters> async_counters,
                       DynamicTiering dynamic_tiering);
  ~CompilationStateImpl() {
    if (js_to_wasm_wrapper_job_ && js_to_wasm_wrapper_job_->IsValid())
      js_to_wasm_wrapper_job_->CancelAndDetach();
    if (baseline_compile_job_->IsValid())
      baseline_compile_job_->CancelAndDetach();
    if (top_tier_compile_job_->IsValid())
      top_tier_compile_job_->CancelAndDetach();
  }

  // Call right after the constructor, after the {compilation_state_} field in
  // the {NativeModule} has been initialized.
  void InitCompileJob();

  // {kCancelUnconditionally}: Cancel all compilation.
  // {kCancelInitialCompilation}: Cancel all compilation if initial (baseline)
  // compilation is not finished yet.
  enum CancellationPolicy { kCancelUnconditionally, kCancelInitialCompilation };
  void CancelCompilation(CancellationPolicy);

  bool cancelled() const;

  // Apply a compilation hint to the initial compilation progress, updating all
  // internal fields accordingly.
  void ApplyCompilationHintToInitialProgress(const WasmCompilationHint& hint,
                                             size_t hint_idx);

  // Use PGO information to choose a better initial compilation progress
  // (tiering decisions).
  void ApplyPgoInfoToInitialProgress(ProfileInformation* pgo_info);

  // Apply PGO information to a fully initialized compilation state. Also
  // trigger compilation as needed.
  void ApplyPgoInfoLate(ProfileInformation* pgo_info);

  // Initialize compilation progress. Set compilation tiers to expect for
  // baseline and top tier compilation. Must be set before
  // {CommitCompilationUnits} is invoked which triggers background compilation.
  void InitializeCompilationProgress(int num_import_wrappers,
                                     int num_export_wrappers,
                                     ProfileInformation* pgo_info);

  void InitializeCompilationProgressAfterDeserialization(
      base::Vector<const int> lazy_functions,
      base::Vector<const int> eager_functions);

  // Initializes compilation units based on the information encoded in the
  // {compilation_progress_}.
  void InitializeCompilationUnits(
      std::unique_ptr<CompilationUnitBuilder> builder);

  // Adds compilation units for another function to the
  // {CompilationUnitBuilder}. This function is the streaming compilation
  // equivalent to {InitializeCompilationUnits}.
  void AddCompilationUnit(CompilationUnitBuilder* builder, int func_index);

  // Add the callback to be called on compilation events. Needs to be
  // set before {CommitCompilationUnits} is run to ensure that it receives all
  // events. The callback object must support being deleted from any thread.
  void AddCallback(std::unique_ptr<CompilationEventCallback> callback);

  // Inserts new functions to compile and kicks off compilation.
  void CommitCompilationUnits(
      base::Vector<WasmCompilationUnit> baseline_units,
      base::Vector<WasmCompilationUnit> top_tier_units,
      base::Vector<JSToWasmWrapperCompilationUnit> js_to_wasm_wrapper_units);
  void CommitTopTierCompilationUnit(WasmCompilationUnit);
  void AddTopTierPriorityCompilationUnit(WasmCompilationUnit, size_t);

  CompilationUnitQueues::Queue* GetQueueForCompileTask(int task_id);

  std::optional<WasmCompilationUnit> GetNextCompilationUnit(
      CompilationUnitQueues::Queue*, CompilationTier tier);

  JSToWasmWrapperCompilationUnit* GetJSToWasmWrapperCompilationUnit(
      size_t index);
  void FinalizeJSToWasmWrappers(Isolate* isolate, const WasmModule* module);

  void OnFinishedUnits(base::Vector<WasmCode*>);
  void OnFinishedJSToWasmWrapperUnits();

  void OnCompilationStopped(WasmDetectedFeatures detected);
  void PublishDetectedFeaturesAfterCompilation(Isolate*);
  void SchedulePublishCompilationResults(
      std::vector<std::unique_ptr<WasmCode>> unpublished_code,
      CompilationTier tier);

  size_t NumOutstandingCompilations(CompilationTier tier) const;

  void SetError();

  void WaitForCompilationEvent(CompilationEvent event);

  void TierUpAllFunctions();

  void AllowAnotherTopTierJob(uint32_t func_index) {
    compilation_unit_queues_.AllowAnotherTopTierJob(func_index);
  }

  void AllowAnotherTopTierJobForAllFunctions() {
    compilation_unit_queues_.AllowAnotherTopTierJobForAllFunctions();
  }

  bool failed() const {
    return compile_failed_.load(std::memory_order_relaxed);
  }

  bool baseline_compilation_finished() const {
    base::MutexGuard guard(&callbacks_mutex_);
    return outstanding_baseline_units_ == 0 &&
           !has_outstanding_export_wrappers_;
  }

  DynamicTiering dynamic_tiering() const { return dynamic_tiering_; }

  Counters* counters() const { return async_counters_.get(); }

  void SetWireBytesStorage(
      std::shared_ptr<WireBytesStorage> wire_bytes_storage) {
    base::MutexGuard guard(&mutex_);
    wire_bytes_storage_ = std::move(wire_bytes_storage);
  }

  std::shared_ptr<WireBytesStorage> GetWireBytesStorage() const {
    base::MutexGuard guard(&mutex_);
    DCHECK_NOT_NULL(wire_bytes_storage_);
    return wire_bytes_storage_;
  }

  void set_compilation_id(int compilation_id) {
    DCHECK_EQ(compilation_id_, kInvalidCompilationID);
    compilation_id_ = compilation_id;
  }

  size_t EstimateCurrentMemoryConsumption() const;

  // Called from the delayed task to trigger caching if the timeout
  // (--wasm-caching-timeout-ms) has passed since the last top-tier compilation.
  // This either triggers caching or re-schedules the task if more code has
  // been compiled to the top tier in the meantime.
  void TriggerCachingAfterTimeout();

  std::vector<WasmCode*> PublishCode(
      base::Vector<std::unique_ptr<WasmCode>> codes);

 private:
  void AddCompilationUnitInternal(CompilationUnitBuilder* builder,
                                  int function_index,
                                  uint8_t function_progress);

  // Trigger callbacks according to the internal counters below
  // (outstanding_...).
  // Hold the {callbacks_mutex_} when calling this method.
  void TriggerOutstandingCallbacks();
  // Trigger an exact set of callbacks. Hold the {callbacks_mutex_} when calling
  // this method.
  void TriggerCallbacks(base::EnumSet<CompilationEvent>);

  void PublishCompilationResults(
      std::vector<std::unique_ptr<WasmCode>> unpublished_code);

  NativeModule* const native_module_;
  std::weak_ptr<NativeModule> const native_module_weak_;
  const std::shared_ptr<Counters> async_counters_;

  // Compilation error, atomically updated. This flag can be updated and read
  // using relaxed semantics.
  std::atomic<bool> compile_failed_{false};

  // True if compilation was cancelled and worker threads should return. This
  // flag can be updated and read using relaxed semantics.
  std::atomic<bool> compile_cancelled_{false};

  CompilationUnitQueues compilation_unit_queues_;

  std::vector<JSToWasmWrapperCompilationUnit> js_to_wasm_wrapper_units_;

  // Cache the dynamic tiering configuration to be consistent for the whole
  // compilation.
  const DynamicTiering dynamic_tiering_;

  // This mutex protects all information of this {CompilationStateImpl} which is
  // being accessed concurrently.
  mutable base::Mutex mutex_;

  // The compile job handles, initialized right after construction of
  // {CompilationStateImpl}.
  std::unique_ptr<JobHandle> js_to_wasm_wrapper_job_;
  std::unique_ptr<JobHandle> baseline_compile_job_;
  std::unique_ptr<JobHandle> top_tier_compile_job_;

  // The compilation id to identify trace events linked to this compilation.
  static constexpr int kInvalidCompilationID = -1;
  int compilation_id_ = kInvalidCompilationID;

  //////////////////////////////////////////////////////////////////////////////
  // Protected by {mutex_}:

  // Features detected to be used in this module. Features can be detected
  // as a module is being compiled.
  WasmDetectedFeatures detected_features_;

  // Abstraction over the storage of the wire bytes. Held in a shared_ptr so
  // that background compilation jobs can keep the storage alive while
  // compiling.
  std::shared_ptr<WireBytesStorage> wire_bytes_storage_;

  // End of fields protected by {mutex_}.
  //////////////////////////////////////////////////////////////////////////////

  // This mutex protects the callbacks vector, and the counters used to
  // determine which callbacks to call. The counters plus the callbacks
  // themselves need to be synchronized to ensure correct order of events.
  mutable base::Mutex callbacks_mutex_;

  //////////////////////////////////////////////////////////////////////////////
  // Protected by {callbacks_mutex_}:

  // Callbacks to be called on compilation events.
  std::vector<std::unique_ptr<CompilationEventCallback>> callbacks_;

  // Events that already happened.
  base::EnumSet<CompilationEvent> finished_events_;

  int outstanding_baseline_units_ = 0;
  bool has_outstanding_export_wrappers_ = false;
  // The amount of generated top tier code since the last
  // {kFinishedCompilationChunk} event.
  size_t bytes_since_last_chunk_ = 0;
  std::vector<uint8_t> compilation_progress_;

  // The timestamp of the last top-tier compilation.
  // This field is updated on every publishing of top-tier code, and is reset
  // once caching is triggered. Hence it also informs whether a caching task is
  // currently being scheduled (whenever this is set).
  base::TimeTicks last_top_tier_compilation_timestamp_;

  // End of fields protected by {callbacks_mutex_}.
  //////////////////////////////////////////////////////////////////////////////

  struct PublishState {
    // {mutex_} protects {publish_queue_} and {publisher_running_}.
    base::Mutex mutex_;
    std::vector<std::unique_ptr<WasmCode>> publish_queue_;
    bool publisher_running_ = false;
  };
  PublishState publish_state_[CompilationTier::kNumTiers];

  // Encoding of fields in the {compilation_progress_} vector.
  using RequiredBaselineTierField = base::BitField8<ExecutionTier, 0, 2>;
  using RequiredTopTierField = base::BitField8<ExecutionTier, 2, 2>;
  using ReachedTierField = base::BitField8<ExecutionTier, 4, 2>;
};

CompilationStateImpl* Impl(CompilationState* compilation_state) {
  return reinterpret_cast<CompilationStateImpl*>(compilation_state);
}
const CompilationStateImpl* Impl(const CompilationState* compilation_state) {
  return reinterpret_cast<const CompilationStateImpl*>(compilation_state);
}

CompilationStateImpl* BackgroundCompileScope::compilation_state() const {
  DCHECK(native_module_);
  return Impl(native_module_->compilation_state());
}

size_t CompilationStateImpl::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(CompilationStateImpl, 712);
  UPDATE_WHEN_CLASS_CHANGES(JSToWasmWrapperCompilationUnit, 32);
  size_t result = sizeof(CompilationStateImpl);

  {
    base::MutexGuard guard{&mutex_};
    result += compilation_unit_queues_.EstimateCurrentMemoryConsumption();

    result += ContentSize(js_to_wasm_wrapper_units_);
    result += js_to_wasm_wrapper_units_.size() *
              (sizeof(JSToWasmWrapperCompilationUnit) +
               sizeof(TurbofanCompilationJob));
  }

  // To read the size of {callbacks_} and {compilation_progress_}, we'd
  // need to acquire the {callbacks_mutex_}, which can cause deadlocks
  // when that mutex is already held elsewhere and another thread calls
  // into this function. So we rely on heuristics and informed guesses
  // instead: {compilation_progress_} contains an entry for every declared
  // function in the module...
  result += sizeof(uint8_t) * native_module_->module()->num_declared_functions;
  // ...and there are typically no more than a handful of {callbacks_}.
  constexpr size_t kAssumedNumberOfCallbacks = 4;
  constexpr size_t size_of_vector =
      kAssumedNumberOfCallbacks *
      sizeof(std::unique_ptr<CompilationEventCallback>);
  // Concrete subclasses of CompilationEventCallback will be bigger, but we
  // can't know that here.
  constexpr size_t size_of_payload =
      kAssumedNumberOfCallbacks * sizeof(CompilationEventCallback);
  result += size_of_vector + size_of_payload;

  if (v8_flags.trace_wasm_offheap_memory) {
    PrintF("CompilationStateImpl: %zu\n", result);
  }
  return result;
}

bool BackgroundCompileScope::cancelled() const {
  return native_module_ == nullptr ||
         Impl(native_module_->compilation_state())->cancelled();
}

}  // namespace

//////////////////////////////////////////////////////
// PIMPL implementation of {CompilationState}.

CompilationState::~CompilationState() { Impl(this)->~CompilationStateImpl(); }

void CompilationState::InitCompileJob() { Impl(this)->InitCompileJob(); }

void CompilationState::CancelCompilation() {
  Impl(this)->CancelCompilation(CompilationStateImpl::kCancelUnconditionally);
}

void CompilationState::CancelInitialCompilation() {
  Impl(this)->CancelCompilation(
      CompilationStateImpl::kCancelInitialCompilation);
}

void CompilationState::SetError() { Impl(this)->SetError(); }

void CompilationState::SetWireBytesStorage(
    std::shared_ptr<WireBytesStorage> wire_bytes_storage) {
  Impl(this)->SetWireBytesStorage(std::move(wire_bytes_storage));
}

std::shared_ptr<WireBytesStorage> CompilationState::GetWireBytesStorage()
    const {
  return Impl(this)->GetWireBytesStorage();
}

void CompilationState::AddCallback(
    std::unique_ptr<CompilationEventCallback> callback) {
  return Impl(this)->AddCallback(std::move(callback));
}

void CompilationState::TierUpAllFunctions() {
  Impl(this)->TierUpAllFunctions();
}

void CompilationState::AllowAnotherTopTierJob(uint32_t func_index) {
  Impl(this)->AllowAnotherTopTierJob(func_index);
}

void CompilationState::AllowAnotherTopTierJobForAllFunctions() {
  Impl(this)->AllowAnotherTopTierJobForAllFunctions();
}

void CompilationState::InitializeAfterDeserialization(
    base::Vector<const int> lazy_functions,
    base::Vector<const int> eager_functions) {
  Impl(this)->InitializeCompilationProgressAfterDeserialization(
      lazy_functions, eager_functions);
}

bool CompilationState::failed() const { return Impl(this)->failed(); }

bool CompilationState::baseline_compilation_finished() const {
  return Impl(this)->baseline_compilation_finished();
}

void CompilationState::set_compilation_id(int compilation_id) {
  Impl(this)->set_compilation_id(compilation_id);
}

DynamicTiering CompilationState::dynamic_tiering() const {
  return Impl(this)->dynamic_tiering();
}

size_t CompilationState::EstimateCurrentMemoryConsumption() const {
  return Impl(this)->EstimateCurrentMemoryConsumption();
}

std::vector<WasmCode*> CompilationState::PublishCode(
    base::Vector<std::unique_ptr<WasmCode>> unpublished_code) {
  return Impl(this)->PublishCode(unpublished_code);
}

// static
std::unique_ptr<CompilationState> CompilationState::New(
    const std::shared_ptr<NativeModule>& native_module,
    std::shared_ptr<Counters> async_counters, DynamicTiering dynamic_tiering) {
  return std::unique_ptr<CompilationState>(reinterpret_cast<CompilationState*>(
      new CompilationStateImpl(std::move(native_module),
                               std::move(async_counters), dynamic_tiering)));
}

// End of PIMPL implementation of {CompilationState}.
//////////////////////////////////////////////////////

namespace {

ExecutionTier ApplyHintToExecutionTier(WasmCompilationHintTier hint,
                                       ExecutionTier default_tier) {
  switch (hint) {
    case WasmCompilationHintTier::kDefault:
      return default_tier;
    case WasmCompilationHintTier::kBaseline:
      return ExecutionTier::kLiftoff;
    case WasmCompilationHintTier::kOptimized:
      return ExecutionTier::kTurbofan;
  }
  UNREACHABLE();
}

const WasmCompilationHint* GetCompilationHint(const WasmModule* module,
                                              uint32_t func_index) {
  DCHECK_LE(module->num_imported_functions, func_index);
  uint32_t hint_index = declared_function_index(module, func_index);
  const std::vector<WasmCompilationHint>& compilation_hints =
      module->compilation_hints;
  if (hint_index < compilation_hints.size()) {
    return &compilation_hints[hint_index];
  }
  return nullptr;
}

CompileStrategy GetCompileStrategy(const WasmModule* module,
                                   WasmEnabledFeatures enabled_features,
                                   uint32_t func_index, bool lazy_module) {
  if (lazy_module) return CompileStrategy::kLazy;
  if (!enabled_features.has_compilation_hints()) {
    return CompileStrategy::kDefault;
  }
  auto* hint = GetCompilationHint(module, func_index);
  if (hint == nullptr) return CompileStrategy::kDefault;
  switch (hint->strategy) {
    case WasmCompilationHintStrategy::kLazy:
      return CompileStrategy::kLazy;
    case WasmCompilationHintStrategy::kEager:
      return CompileStrategy::kEager;
    case WasmCompilationHintStrategy::kLazyBaselineEagerTopTier:
      return CompileStrategy::kLazyBaselineEagerTopTier;
    case WasmCompilationHintStrategy::kDefault:
      return CompileStrategy::kDefault;
  }
}

struct ExecutionTierPair {
  ExecutionTier baseline_tier;
  ExecutionTier top_tier;
};

// Pass the debug state as a separate parameter to avoid data races: the debug
// state may change between its use here and its use at the call site. To have
// a consistent view on the debug state, the caller reads the debug state once
// and then passes it to this function.
ExecutionTierPair GetDefaultTiersPerModule(NativeModule* native_module,
                                           DynamicTiering dynamic_tiering,
                                           DebugState is_in_debug_state,
                                           bool lazy_module) {
  const WasmModule* module = native_module->module();
  if (lazy_module) {
    return {ExecutionTier::kNone, ExecutionTier::kNone};
  }
  if (is_asmjs_module(module)) {
    DCHECK(!is_in_debug_state);
    return {ExecutionTier::kTurbofan, ExecutionTier::kTurbofan};
  }
  if (is_in_debug_state) {
    return {ExecutionTier::kLiftoff, ExecutionTier::kLiftoff};
  }
  ExecutionTier baseline_tier =
      v8_flags.liftoff ? ExecutionTier::kLiftoff : ExecutionTier::kTurbofan;
  bool eager_tier_up = !dynamic_tiering && v8_flags.wasm_tier_up;
  ExecutionTier top_tier =
      eager_tier_up ? ExecutionTier::kTurbofan : baseline_tier;
  return {baseline_tier, top_tier};
}

ExecutionTierPair GetLazyCompilationTiers(NativeModule* native_module,
                                          uint32_t func_index,
                                          DebugState is_in_debug_state) {
  DynamicTiering dynamic_tiering =
      Impl(native_module->compilation_state())->dynamic_tiering();
  // For lazy compilation, get the tiers we would use if lazy compilation is
  // disabled.
  constexpr bool kNotLazy = false;
  ExecutionTierPair tiers = GetDefaultTiersPerModule(
      native_module, dynamic_tiering, is_in_debug_state, kNotLazy);
  // If we are in debug mode, we ignore compilation hints.
  if (is_in_debug_state) return tiers;

  // Check if compilation hints override default tiering behaviour.
  if (native_module->enabled_features().has_compilation_hints()) {
    if (auto* hint = GetCompilationHint(native_module->module(), func_index)) {
      tiers.baseline_tier =
          ApplyHintToExecutionTier(hint->baseline_tier, tiers.baseline_tier);
      tiers.top_tier = ApplyHintToExecutionTier(hint->top_tier, tiers.top_tier);
    }
  }

  if (V8_UNLIKELY(v8_flags.wasm_tier_up_filter >= 0 &&
                  func_index !=
                      static_cast<uint32_t>(v8_flags.wasm_tier_up_filter))) {
    tiers.top_tier = tiers.baseline_tier;
  }

  // Correct top tier if necessary.
  static_assert(ExecutionTier::kLiftoff < ExecutionTier::kTurbofan,
                "Assume an order on execution tiers");
  if (tiers.baseline_tier > tiers.top_tier) {
    tiers.top_tier = tiers.baseline_tier;
  }
  return tiers;
}

// The {CompilationUnitBuilder} builds compilation units and stores them in an
// internal buffer. The buffer is moved into the working queue of the
// {CompilationStateImpl} when {Commit} is called.
class CompilationUnitBuilder {
 public:
  explicit CompilationUnitBuilder(NativeModule* native_module)
      : native_module_(native_module) {}

  void AddJSToWasmWrapperUnit(JSToWasmWrapperCompilationUnit unit) {
    js_to_wasm_wrapper_units_.emplace_back(std::move(unit));
  }

  void AddBaselineUnit(int func_index, ExecutionTier tier) {
    baseline_units_.emplace_back(func_index, tier, kNotForDebugging);
  }

  void AddTopTierUnit(int func_index, ExecutionTier tier) {
    tiering_units_.emplace_back(func_index, tier, kNotForDebugging);
  }

  void Commit() {
    if (baseline_units_.empty() && tiering_units_.empty() &&
        js_to_wasm_wrapper_units_.empty()) {
      return;
    }
    compilation_state()->CommitCompilationUnits(
        base::VectorOf(baseline_units_), base::VectorOf(tiering_units_),
        base::VectorOf(js_to_wasm_wrapper_units_));
    Clear();
  }

  void Clear() {
    baseline_units_.clear();
    tiering_units_.clear();
    js_to_wasm_wrapper_units_.clear();
  }

  const WasmModule* module() { return native_module_->module(); }

 private:
  CompilationStateImpl* compilation_state() const {
    return Impl(native_module_->compilation_state());
  }

  NativeModule* const native_module_;
  std::vector<WasmCompilationUnit> baseline_units_;
  std::vector<WasmCompilationUnit> tiering_units_;
  std::vector<JSToWasmWrapperCompilationUnit> js_to_wasm_wrapper_units_;
};

DecodeResult ValidateSingleFunction(Zone* zone, const WasmModule* module,
                                    int func_index,
                                    base::Vector<const uint8_t> code,
                                    WasmEnabledFeatures enabled_features) {
  // Sometimes functions get validated unpredictably in the background, for
  // debugging or when inlining one function into another. We check here if that
  // is the case, and exit early if so.
  if (module->function_was_validated(func_index)) return {};
  const WasmFunction* func = &module->functions[func_index];
  bool is_shared = module->types[func->sig_index].is_shared;
  FunctionBody body{func->sig, func->code.offset(), code.begin(), code.end(),
                    is_shared};
  WasmDetectedFeatures detected_features;
  DecodeResult result = ValidateFunctionBody(zone, enabled_features, module,
                                             &detected_features, body);
  if (result.ok()) module->set_function_validated(func_index);
  return result;
}

enum OnlyLazyFunctions : bool {
  kAllFunctions = false,
  kOnlyLazyFunctions = true,
};

bool IsLazyModule(const WasmModule* module) {
  return v8_flags.wasm_lazy_compilation ||
         (v8_flags.asm_wasm_lazy_compilation && is_asmjs_module(module));
}

class CompileLazyTimingScope {
 public:
  CompileLazyTimingScope(Counters* counters, NativeModule* native_module)
      : counters_(counters), native_module_(native_module) {
    timer_.Start();
  }

  ~CompileLazyTimingScope() {
    base::TimeDelta elapsed = timer_.Elapsed();
    native_module_->AddLazyCompilationTimeSample(elapsed.InMicroseconds());
    counters_->wasm_lazy_compile_time()->AddTimedSample(elapsed);
  }

 private:
  Counters* counters_;
  NativeModule* native_module_;
  base::ElapsedTimer timer_;
};

}  // namespace

bool CompileLazy(Isolate* isolate,
                 Tagged<WasmTrustedInstanceData> trusted_instance_data,
                 int func_index) {
  DisallowGarbageCollection no_gc;
  NativeModule* native_module = trusted_instance_data->native_module();
  Counters* counters = isolate->counters();

  // Put the timer scope around everything, including the {CodeSpaceWriteScope}
  // and its destruction, to measure complete overhead (apart from the runtime
  // function itself, which has constant overhead).
  std::optional<CompileLazyTimingScope> lazy_compile_time_scope;
  if (base::TimeTicks::IsHighResolution()) {
    lazy_compile_time_scope.emplace(counters, native_module);
  }

  DCHECK(!native_module->lazy_compile_frozen());

  TRACE_LAZY("Compiling wasm-function#%d.\n", func_index);

  CompilationStateImpl* compilation_state =
      Impl(native_module->compilation_state());
  DebugState is_in_debug_state = native_module->IsInDebugState();
  ExecutionTierPair tiers =
      GetLazyCompilationTiers(native_module, func_index, is_in_debug_state);

  DCHECK_LE(native_module->num_imported_functions(), func_index);
  DCHECK_LT(func_index, native_module->num_functions());
  WasmCompilationUnit baseline_unit{
      func_index, tiers.baseline_tier,
      is_in_debug_state ? kForDebugging : kNotForDebugging};
  CompilationEnv env = CompilationEnv::ForModule(native_module);
  WasmDetectedFeatures detected_features;
  WasmCompilationResult result = baseline_unit.ExecuteCompilation(
      &env, compilation_state->GetWireBytesStorage().get(), counters,
      &detected_features);
  compilation_state->OnCompilationStopped(detected_features);

  // During lazy compilation, we can only get compilation errors when
  // {--wasm-lazy-validation} is enabled. Otherwise, the module was fully
  // verified before starting its execution.
  CHECK_IMPLIES(result.failed(), v8_flags.wasm_lazy_validation);
  if (result.failed()) {
    return false;
  }

  WasmCodeRefScope code_ref_scope;
  WasmCode* code =
      native_module->PublishCode(native_module->AddCompiledCode(result));
  DCHECK_EQ(func_index, code->index());

  if (V8_UNLIKELY(native_module->log_code())) {
    GetWasmEngine()->LogCode(base::VectorOf(&code, 1));
    // Log the code immediately in the current isolate.
    GetWasmEngine()->LogOutstandingCodesForIsolate(isolate);
  }

  counters->wasm_lazily_compiled_functions()->Increment();

  const WasmModule* module = native_module->module();
  const bool lazy_module = IsLazyModule(module);
  if (GetCompileStrategy(module, native_module->enabled_features(), func_index,
                         lazy_module) == CompileStrategy::kLazy &&
      tiers.baseline_tier < tiers.top_tier) {
    WasmCompilationUnit tiering_unit{func_index, tiers.top_tier,
                                     kNotForDebugging};
    compilation_state->CommitTopTierCompilationUnit(tiering_unit);
  }
  return true;
}

void ThrowLazyCompilationError(Isolate* isolate,
                               const NativeModule* native_module,
                               int func_index) {
  const WasmModule* module = native_module->module();

  CompilationStateImpl* compilation_state =
      Impl(native_module->compilation_state());
  const WasmFunction* func = &module->functions[func_index];
  base::Vector<const uint8_t> code =
      compilation_state->GetWireBytesStorage()->GetCode(func->code);

  auto enabled_features = native_module->enabled_features();
  // This path is unlikely, so the overhead for creating an extra Zone is
  // not important.
  Zone validation_zone{GetWasmEngine()->allocator(), ZONE_NAME};
  DecodeResult decode_result = ValidateSingleFunction(
      &validation_zone, module, func_index, code, enabled_features);

  CHECK(decode_result.failed());
  wasm::ErrorThrower thrower(isolate, nullptr);
  thrower.CompileFailed(GetWasmErrorWithName(native_module->wire_bytes(),
                                             func_index, module,
                                             std::move(decode_result).error()));
}

// The main purpose of this class is to copy the feedback vectors that live in
// `FixedArray`s on the JavaScript heap to a C++ datastructure on the `module`
// that is accessible to the background compilation threads.
// While we are at it, we also do some light processing here, e.g., mapping the
// feedback to functions, identified by their function index, and filtering out
// feedback for calls to imported functions (which we currently don't inline).
class TransitiveTypeFeedbackProcessor {
 public:
  static void Process(Isolate* isolate,
                      Tagged<WasmTrustedInstanceData> trusted_instance_data,
                      int func_index) {
    TransitiveTypeFeedbackProcessor{isolate, trusted_instance_data, func_index}
        .ProcessQueue();
  }

 private:
  TransitiveTypeFeedbackProcessor(
      Isolate* isolate, Tagged<WasmTrustedInstanceData> trusted_instance_data,
      int func_index)
      : isolate_(isolate),
        instance_data_(trusted_instance_data),
        module_(trusted_instance_data->module()),
        mutex_guard(&module_->type_feedback.mutex),
        feedback_for_function_(module_->type_feedback.feedback_for_function) {
    queue_.insert(func_index);
  }

  ~TransitiveTypeFeedbackProcessor() { DCHECK(queue_.empty()); }

  void ProcessQueue() {
    while (!queue_.empty()) {
      auto next = queue_.cbegin();
      ProcessFunction(*next);
      queue_.erase(next);
    }
  }

  void ProcessFunction(int func_index);

  void EnqueueCallees(const std::vector<CallSiteFeedback>& feedback) {
    for (size_t i = 0; i < feedback.size(); i++) {
      const CallSiteFeedback& csf = feedback[i];
      for (int j = 0; j < csf.num_cases(); j++) {
        int func = csf.function_index(j);
        // Don't spend time on calls that have never been executed.
        if (csf.call_count(j) == 0) continue;
        // Don't recompute feedback that has already been processed.
        auto existing = feedback_for_function_.find(func);
        if (existing != feedback_for_function_.end() &&
            !existing->second.feedback_vector.empty()) {
          if (!existing->second.needs_reprocessing_after_deopt) {
            continue;
          }
          DCHECK(v8_flags.wasm_deopt);
          existing->second.needs_reprocessing_after_deopt = false;
        }
        queue_.insert(func);
      }
    }
  }

  DisallowGarbageCollection no_gc_scope_;
  Isolate* const isolate_;
  const Tagged<WasmTrustedInstanceData> instance_data_;
  const WasmModule* const module_;
  // TODO(jkummerow): Check if it makes a difference to apply any updates
  // as a single batch at the end.
  base::SharedMutexGuard<base::kExclusive> mutex_guard;
  std::unordered_map<uint32_t, FunctionTypeFeedback>& feedback_for_function_;
  std::set<int> queue_;
};

class FeedbackMaker {
 public:
  FeedbackMaker(IsolateForSandbox isolate,
                Tagged<WasmTrustedInstanceData> trusted_instance_data,
                int func_index, int num_calls)
      : isolate_(isolate),
        instance_data_(trusted_instance_data),
        num_imported_functions_(static_cast<int>(
            trusted_instance_data->module()->num_imported_functions)),
        func_index_(func_index) {
    result_.reserve(num_calls);
  }

  void AddCallRefCandidate(Tagged<WasmFuncRef> funcref, int count) {
    Tagged<WasmInternalFunction> internal_function =
        Cast<WasmFuncRef>(funcref)->internal(isolate_);
    // Only consider wasm function declared in this instance.
    if (internal_function->implicit_arg() != instance_data_) {
      has_non_inlineable_targets_ = true;
      return;
    }
    // Discard imports for now.
    if (internal_function->function_index() < num_imported_functions_) {
      has_non_inlineable_targets_ = true;
      return;
    }
    AddCall(internal_function->function_index(), count);
  }

  void AddCallIndirectCandidate(Tagged<Smi> target_truncated_smi, int count) {
    // We need to map a truncated call target back to a function index.
    // Generally there may be multiple jump tables if code spaces are far apart
    // (to ensure that direct calls can always use a near call to the closest
    // jump table).
    // However, here we are always handling call targets that are originally
    // from the `WasmDispatchTable`, whose entries are always targets pointing
    // into the main jump table, so we only need to check against that.

    Address jt_start = instance_data_->native_module()->jump_table_start();
    uint32_t jt_size = JumpTableAssembler::SizeForNumberOfSlots(
        instance_data_->module()->num_declared_functions);
    Address jt_end = jt_start + jt_size;

    uint32_t jt_start_truncated = jt_start & kSmiMaxValue;
    uint32_t jt_end_truncated = jt_end & kSmiMaxValue;
    uint32_t target_truncated = target_truncated_smi.value();

    if (target_truncated < jt_start_truncated ||
        target_truncated >= jt_end_truncated) {
      // Was not in the main table (e.g., because it's an imported function).
      has_non_inlineable_targets_ = true;
      return;
    }

    uint32_t jt_offset = target_truncated - jt_start_truncated;
    uint32_t jt_slot_idx = JumpTableAssembler::SlotOffsetToIndex(jt_offset);
    uint32_t func_idx =
        instance_data_->module()->num_imported_functions + jt_slot_idx;
    AddCall(func_idx, count);
  }

  void AddCall(int target, int count) {
    // Keep the cache sorted (using insertion-sort), highest count first.
    int insertion_index = 0;
    while (insertion_index < cache_usage_ &&
           counts_cache_[insertion_index] >= count) {
      insertion_index++;
    }
    for (int shifted_index = cache_usage_ - 1; shifted_index >= insertion_index;
         shifted_index--) {
      targets_cache_[shifted_index + 1] = targets_cache_[shifted_index];
      counts_cache_[shifted_index + 1] = counts_cache_[shifted_index];
    }
    targets_cache_[insertion_index] = target;
    counts_cache_[insertion_index] = count;
    cache_usage_++;
  }

  bool HasTargetCached(int target) {
    auto end = targets_cache_ + cache_usage_;
    return std::find(targets_cache_, end, target) != end;
  }

  void FinalizeCall() {
    if (cache_usage_ == 0) {
      result_.emplace_back();
    } else if (cache_usage_ == 1) {
      if (v8_flags.trace_wasm_inlining) {
        PrintF("[function %d: call #%zu inlineable (monomorphic)]\n",
               func_index_, result_.size());
      }
      result_.emplace_back(targets_cache_[0], counts_cache_[0]);
    } else {
      if (v8_flags.trace_wasm_inlining) {
        PrintF("[function %d: call #%zu inlineable (polymorphic %d)]\n",
               func_index_, result_.size(), cache_usage_);
      }
      CallSiteFeedback::PolymorphicCase* polymorphic =
          new CallSiteFeedback::PolymorphicCase[cache_usage_];
      for (int i = 0; i < cache_usage_; i++) {
        polymorphic[i].function_index = targets_cache_[i];
        polymorphic[i].absolute_call_frequency = counts_cache_[i];
      }
      result_.emplace_back(polymorphic, cache_usage_);
    }
    result_.back().set_has_non_inlineable_targets(has_non_inlineable_targets_);
    has_non_inlineable_targets_ = false;
    cache_usage_ = 0;
  }

  void set_has_non_inlineable_targets() { has_non_inlineable_targets_ = true; }

  // {GetResult} can only be called on a r-value reference to make it more
  // obvious at call sites that {this} should not be used after this operation.
  std::vector<CallSiteFeedback>&& GetResult() && { return std::move(result_); }

 private:
  const IsolateForSandbox isolate_;
  const Tagged<WasmTrustedInstanceData> instance_data_;
  std::vector<CallSiteFeedback> result_;
  const int num_imported_functions_;
  const int func_index_;
  int cache_usage_{0};
  int targets_cache_[kMaxPolymorphism];
  int counts_cache_[kMaxPolymorphism];
  bool has_non_inlineable_targets_ = false;
};

void TransitiveTypeFeedbackProcessor::ProcessFunction(int func_index) {
  int which_vector = declared_function_index(module_, func_index);
  Tagged<Object> maybe_feedback =
      instance_data_->feedback_vectors()->get(which_vector);
  if (!IsFixedArray(maybe_feedback)) return;
  Tagged<FixedArray> feedback = Cast<FixedArray>(maybe_feedback);
  base::Vector<uint32_t> call_targets =
      module_->type_feedback.feedback_for_function[func_index]
          .call_targets.as_vector();

  // For each entry in {call_targets}, there are two {Object} slots in the
  // {feedback} vector:
  // +-------------------------+-----------------------+-------------------+
  // |        Call Type        |   Feedback: Entry 1   |      Entry 2      |
  // +-------------------------+-----------------------+-------------------+
  // | direct                  | Smi(count)            | Smi(0), unused    |
  // +-------------------------+-----------------------+-------------------+
  // | ref, uninitialized      | Smi(0)                | Smi(0)            |
  // | ref, monomorphic        | WasmFuncRef(target)   | Smi(count>0)      |
  // | ref, polymorphic        | FixedArray            | Undefined         |
  // | ref, megamorphic        | MegamorphicSymbol     | Undefined         |
  // +-------------------------+-----------------------+-------------------+
  // | indirect, uninitialized | Smi(0)                | Smi(0)            |
  // | indirect, monomorphic   | Smi(truncated_target) | Smi(count>0)      |
  // | indirect, polymorphic   | FixedArray            | Undefined         |
  // | indirect, megamorphic   | MegamorphicSymbol     | Undefined         |
  // +-------------------------+-----------------------+-------------------+
  // The FixedArray entries for the polymorphic cases look like the monomorphic
  // entries in the feedback vector itself.
  // See {UpdateCallRefOrIndirectIC} in {wasm.tq} for how this is written.
  // Since this is combining untrusted data ({feedback} vector on the JS heap)
  // with trusted data ({call_targets}), make sure to avoid an OOB access.
  int checked_feedback_length = feedback->length();
  SBXCHECK_EQ(checked_feedback_length, call_targets.size() * 2);
  FeedbackMaker fm(isolate_, instance_data_, func_index,
                   checked_feedback_length / 2);
  for (int i = 0; i < checked_feedback_length; i += 2) {
    uint32_t sentinel_or_target = call_targets[i / 2];
    Tagged<Object> first_slot = feedback->get(i);
    Tagged<Object> second_slot = feedback->get(i + 1);

    if (sentinel_or_target != FunctionTypeFeedback::kCallRef &&
        sentinel_or_target != FunctionTypeFeedback::kCallIndirect) {
      // Direct call counts.
      int count = Smi::ToInt(first_slot);
      DCHECK_EQ(Smi::ToInt(second_slot), 0);
      // TODO(dlehmann): Currently, TurboFan assumes that we add feedback even
      // if the call count is zero. Once TurboFan is gone, revisit if we can
      // avoid this (similar to how we do for call_ref/call_indirect today).
      fm.AddCall(static_cast<int>(sentinel_or_target), count);
    } else if (IsSmi(second_slot) && Smi::ToInt(second_slot) == 0) {
      // Uninitialized call_ref or call_indirect.
      DCHECK_EQ(Smi::ToInt(first_slot), 0);
      if (v8_flags.trace_wasm_inlining) {
        PrintF("[function %d: call #%d: uninitialized]\n", func_index, i / 2);
      }
    } else if (IsWasmFuncRef(first_slot)) {
      // Monomorphic call_ref.
      DCHECK_EQ(sentinel_or_target, FunctionTypeFeedback::kCallRef);
      int count = Smi::ToInt(second_slot);
      fm.AddCallRefCandidate(Cast<WasmFuncRef>(first_slot), count);
    } else if (IsSmi(first_slot)) {
      // Monomorphic call_indirect.
      DCHECK_EQ(sentinel_or_target, FunctionTypeFeedback::kCallIndirect);
      int count = Smi::ToInt(second_slot);
      fm.AddCallIndirectCandidate(Cast<Smi>(first_slot), count);
    } else if (IsFixedArray(first_slot)) {
      // Polymorphic call_ref or call_indirect.
      Tagged<FixedArray> polymorphic = Cast<FixedArray>(first_slot);
      DCHECK(IsUndefined(second_slot));
      int checked_polymorphic_length = polymorphic->length();
      SBXCHECK_LE(checked_polymorphic_length, 2 * kMaxPolymorphism);
      if (sentinel_or_target == FunctionTypeFeedback::kCallRef) {
        for (int j = 0; j < checked_polymorphic_length; j += 2) {
          Tagged<WasmFuncRef> target = Cast<WasmFuncRef>(polymorphic->get(j));
          int count = Smi::ToInt(polymorphic->get(j + 1));
          fm.AddCallRefCandidate(target, count);
        }
      } else {
        DCHECK_EQ(sentinel_or_target, FunctionTypeFeedback::kCallIndirect);
        for (int j = 0; j < checked_polymorphic_length; j += 2) {
          Tagged<Smi> target = Cast<Smi>(polymorphic->get(j));
          int count = Smi::ToInt(polymorphic->get(j + 1));
          fm.AddCallIndirectCandidate(target, count);
        }
      }
    } else if (first_slot == ReadOnlyRoots{isolate_}.megamorphic_symbol()) {
      DCHECK(IsUndefined(second_slot));
      if (v8_flags.trace_wasm_inlining) {
        PrintF("[function %d: call #%d: megamorphic]\n", func_index, i / 2);
      }
    } else {
      UNREACHABLE();
    }

    if (v8_flags.wasm_deopt &&
        first_slot != ReadOnlyRoots{isolate_}.megamorphic_symbol()) {
      // If we already had feedback for this call, also add the already existing
      // feedback to prevent deopt loops where two different instantiations
      // (which have their own on-heap feedback vector) to "flip-flop" between
      // their inlining decisions potentially causing deopt loops.
      const std::vector<CallSiteFeedback>& existing =
          feedback_for_function_[func_index].feedback_vector;
      size_t feedback_index = i / 2;
      if (feedback_index < existing.size()) {
        const CallSiteFeedback& old_feedback = existing[feedback_index];
        if (old_feedback.has_non_inlineable_targets()) {
          fm.set_has_non_inlineable_targets();
        }
        for (int i = 0; i < old_feedback.num_cases(); ++i) {
          int old_target_function_index = old_feedback.function_index(i);
          // If the new feedback already contains the target, we do not touch
          // the call count.
          if (!fm.HasTargetCached(old_target_function_index)) {
            fm.AddCall(old_target_function_index, old_feedback.call_count(i));
            // There shouldn't be any imported functions in there as they can't
            // be inlined. If this DCHECK is invalidated,
            // has_non_inlineable_targets_ would need to be updated here to
            // reflect that.
            DCHECK_GE(static_cast<uint32_t>(old_target_function_index),
                      instance_data_->module()->num_imported_functions);
          }
        }
      }
    }

    fm.FinalizeCall();
  }
  std::vector<CallSiteFeedback> result = std::move(fm).GetResult();
  EnqueueCallees(result);
  feedback_for_function_[func_index].feedback_vector = std::move(result);
}

void TriggerTierUp(Isolate* isolate,
                   Tagged<WasmTrustedInstanceData> trusted_instance_data,
                   int func_index) {
  NativeModule* native_module = trusted_instance_data->native_module();
  CompilationStateImpl* compilation_state =
      Impl(native_module->compilation_state());
  WasmCompilationUnit tiering_unit{func_index, ExecutionTier::kTurbofan,
                                   kNotForDebugging};

  const WasmModule* module = native_module->module();
  int priority;
  {
    base::SharedMutexGuard<base::kExclusive> mutex_guard(
        &module->type_feedback.mutex);
    int array_index = wasm::declared_function_index(module, func_index);
    trusted_instance_data->tiering_budget_array()[array_index].store(
        v8_flags.wasm_tiering_budget, std::memory_order_relaxed);
    int& stored_priority =
        module->type_feedback.feedback_for_function[func_index].tierup_priority;
    if (stored_priority < kMaxInt) ++stored_priority;
    priority = stored_priority;
  }
  // Only create a compilation unit if this is the first time we detect this
  // function as hot (priority == 1), or if the priority increased
  // significantly. The latter is assumed to be the case if the priority
  // increased at least to four, and is a power of two.
  if (priority == 2 || !base::bits::IsPowerOfTwo(priority)) return;

  // Before adding the tier-up unit or increasing priority, process type
  // feedback for best code generation.
  if (native_module->enabled_features().has_inlining() || module->is_wasm_gc) {
    // TODO(jkummerow): we could have collisions here if different instances
    // of the same module have collected different feedback. If that ever
    // becomes a problem, figure out a solution.
    TransitiveTypeFeedbackProcessor::Process(isolate, trusted_instance_data,
                                             func_index);
  }

  compilation_state->AddTopTierPriorityCompilationUnit(tiering_unit, priority);
}

void TierUpNowForTesting(Isolate* isolate,
                         Tagged<WasmTrustedInstanceData> trusted_instance_data,
                         int func_index) {
  NativeModule* native_module = trusted_instance_data->native_module();
  if (native_module->enabled_features().has_inlining() ||
      native_module->module()->is_wasm_gc) {
    TransitiveTypeFeedbackProcessor::Process(isolate, trusted_instance_data,
                                             func_index);
  }
  wasm::GetWasmEngine()->CompileFunction(isolate->counters(), native_module,
                                         func_index,
                                         wasm::ExecutionTier::kTurbofan);
  CHECK(!native_module->compilation_state()->failed());
}

void TierUpAllForTesting(
    Isolate* isolate, Tagged<WasmTrustedInstanceData> trusted_instance_data) {
  NativeModule* native_module = trusted_instance_data->native_module();
  const WasmModule* mod = native_module->module();
  WasmCodeRefScope code_ref_scope;

  uint32_t start = mod->num_imported_functions;
  uint32_t end = start + mod->num_declared_functions;
  for (uint32_t func_index = start; func_index < end; func_index++) {
    if (!native_module->HasCodeWithTier(func_index, ExecutionTier::kTurbofan)) {
      TierUpNowForTesting(isolate, trusted_instance_data, func_index);
    }
  }
}

namespace {

bool IsI16Array(wasm::ValueType type, const WasmModule* module) {
  if (!type.is_object_reference() || !type.has_index()) return false;
  uint32_t reftype = type.ref_index();
  if (!module->has_array(reftype)) return false;
  return module->isorecursive_canonical_type_ids[reftype] ==
         TypeCanonicalizer::kPredefinedArrayI16Index;
}

bool IsI8Array(wasm::ValueType type, const WasmModule* module,
               bool allow_nullable) {
  if (!type.is_object_reference() || !type.has_index()) return false;
  if (!allow_nullable && type.is_nullable()) return false;
  uint32_t reftype = type.ref_index();
  if (!module->has_array(reftype)) return false;
  return module->isorecursive_canonical_type_ids[reftype] ==
         TypeCanonicalizer::kPredefinedArrayI8Index;
}

// Returns the start offset of a given import, for use in error messages.
// The module_name payload is preceded by an i32v giving its length. That i32v
// is preceded by another i32v, which is either a type index (specifying the
// type of the previous import) or the imports count (in case of the first
// import). So we scan backwards as long as we find non-last LEB bytes there.
uint32_t ImportStartOffset(base::Vector<const uint8_t> wire_bytes,
                           uint32_t module_name_start) {
  DCHECK_LT(0, module_name_start);
  uint32_t offset = module_name_start - 1;  // Last byte of the string length.
  DCHECK_EQ(wire_bytes[offset] & 0x80, 0);
  while (offset > 0 && (wire_bytes[offset - 1] & 0x80) != 0) {
    offset--;
  }
  return offset;
}

}  // namespace

// Validates the signatures of recognized compile-time imports, and stores
// them on the {module}'s {well_known_imports} list.
WasmError ValidateAndSetBuiltinImports(const WasmModule* module,
                                       base::Vector<const uint8_t> wire_bytes,
                                       const CompileTimeImports& imports) {
  DCHECK_EQ(module->origin, kWasmOrigin);
  if (imports.empty()) return {};

  static constexpr ValueType kRefExtern = ValueType::Ref(HeapType::kExtern);
  static constexpr ValueType kExternRef = kWasmExternRef;
  static constexpr ValueType kI32 = kWasmI32;

  // Shorthands: "r" = nullable "externref", "e" = non-nullable "ref extern".
  static constexpr ValueType kReps_e_i[] = {kRefExtern, kI32};
  static constexpr ValueType kReps_e_rr[] = {kRefExtern, kExternRef,
                                             kExternRef};
  static constexpr ValueType kReps_e_rii[] = {kRefExtern, kExternRef, kI32,
                                              kI32};
  static constexpr ValueType kReps_i_ri[] = {kI32, kExternRef, kI32};
  static constexpr ValueType kReps_i_rr[] = {kI32, kExternRef, kExternRef};

  static constexpr FunctionSig kSig_e_i(1, 1, kReps_e_i);
  static constexpr FunctionSig kSig_e_r(1, 1, kReps_e_rr);
  static constexpr FunctionSig kSig_e_rr(1, 2, kReps_e_rr);
  static constexpr FunctionSig kSig_e_rii(1, 3, kReps_e_rii);

  static constexpr FunctionSig kSig_i_r(1, 1, kReps_i_ri);
  static constexpr FunctionSig kSig_i_ri(1, 2, kReps_i_ri);
  static constexpr FunctionSig kSig_i_rr(1, 2, kReps_i_rr);

  std::vector<WellKnownImport> statuses;
  statuses.reserve(module->num_imported_functions);
  for (size_t i = 0; i < module->import_table.size(); i++) {
    const WasmImport& import = module->import_table[i];

    // When magic string imports are requested, check that imports with the
    // string constant module name are globals of the right type.
    if (imports.has_string_constants(wire_bytes.SubVector(
            import.module_name.offset(), import.module_name.end_offset()))) {
      if (import.kind != kExternalGlobal ||
          !module->globals[import.index].type.is_reference_to(
              HeapType::kExtern) ||
          module->globals[import.index].mutability != false) {
        TruncatedUserString<> name(
            wire_bytes.data() + import.field_name.offset(),
            import.field_name.length());
        return WasmError(
            ImportStartOffset(wire_bytes, import.module_name.offset()),
            "String constant import #%zu \"%.*s\" must be an immutable global "
            "subtyping externref",
            i, name.length(), name.start());
      }
    }

    // Check compile-time imported functions.
    if (import.kind != kExternalFunction) continue;
    base::Vector<const uint8_t> module_name = wire_bytes.SubVector(
        import.module_name.offset(), import.module_name.end_offset());
    constexpr size_t kMinInterestingLength = 10;
    if (module_name.size() < kMinInterestingLength ||
        module_name.SubVector(0, 5) != base::StaticOneByteVector("wasm:")) {
      statuses.push_back(WellKnownImport::kUninstantiated);
      continue;
    }
    base::Vector<const uint8_t> collection = module_name.SubVectorFrom(5);
    WellKnownImport status = WellKnownImport::kUninstantiated;
    const WasmFunction& func = module->functions[import.index];
    const FunctionSig* sig = func.sig;
    WireBytesRef field_name = import.field_name;
    base::Vector<const uint8_t> name =
        wire_bytes.SubVector(field_name.offset(), field_name.end_offset());
    if (collection == base::StaticOneByteVector("js-string") &&
        imports.contains(CompileTimeImport::kJsString)) {
#define RETURN_ERROR(module_name_string, import_name)                     \
  uint32_t error_offset =                                                 \
      ImportStartOffset(wire_bytes, import.module_name.offset());         \
  return WasmError(error_offset,                                          \
                   "Imported builtin function \"wasm:" module_name_string \
                   "\" \"" import_name "\" has incorrect signature")

#define CHECK_SIG(import_name, kSigName, kEnumName)      \
  if (name == base::StaticOneByteVector(#import_name)) { \
    if (*sig != kSigName) {                              \
      RETURN_ERROR("js-string", #import_name);           \
    }                                                    \
    status = WellKnownImport::kEnumName;                 \
  } else  // NOLINT(readability/braces)

      CHECK_SIG(cast, kSig_e_r, kStringCast)
      CHECK_SIG(test, kSig_i_r, kStringTest)
      CHECK_SIG(fromCharCode, kSig_e_i, kStringFromCharCode)
      CHECK_SIG(fromCodePoint, kSig_e_i, kStringFromCodePoint)
      CHECK_SIG(charCodeAt, kSig_i_ri, kStringCharCodeAt)
      CHECK_SIG(codePointAt, kSig_i_ri, kStringCodePointAt)
      CHECK_SIG(length, kSig_i_r, kStringLength)
      CHECK_SIG(concat, kSig_e_rr, kStringConcat)
      CHECK_SIG(substring, kSig_e_rii, kStringSubstring)
      CHECK_SIG(equals, kSig_i_rr, kStringEquals)
      CHECK_SIG(compare, kSig_i_rr, kStringCompare)
      if (name == base::StaticOneByteVector("fromCharCodeArray")) {
        if (sig->parameter_count() != 3 || sig->return_count() != 1 ||
            !IsI16Array(sig->GetParam(0), module) ||  // --
            sig->GetParam(1) != kI32 ||               // --
            sig->GetParam(2) != kI32 ||               // --
            sig->GetReturn() != kRefExtern) {
          RETURN_ERROR("js-string", "fromCharCodeArray");
        }
        status = WellKnownImport::kStringFromWtf16Array;
      } else if (name == base::StaticOneByteVector("intoCharCodeArray")) {
        if (sig->parameter_count() != 3 || sig->return_count() != 1 ||
            sig->GetParam(0) != kExternRef ||
            !IsI16Array(sig->GetParam(1), module) ||  // --
            sig->GetParam(2) != kI32 ||               // --
            sig->GetReturn() != kI32) {
          RETURN_ERROR("js-string", "intoCharCodeArray");
        }
        status = WellKnownImport::kStringToWtf16Array;
      }
#undef CHECK_SIG
    } else if (collection == base::StaticOneByteVector("text-encoder") &&
               imports.contains(CompileTimeImport::kTextEncoder)) {
      if (name == base::StaticOneByteVector("measureStringAsUTF8")) {
        if (*sig != kSig_i_r) {
          RETURN_ERROR("text-encoder", "measureStringAsUTF8");
        }
        status = WellKnownImport::kStringMeasureUtf8;
      } else if (name ==
                 base::StaticOneByteVector("encodeStringIntoUTF8Array")) {
        if (sig->parameter_count() != 3 || sig->return_count() != 1 ||
            sig->GetParam(0) != kExternRef ||              // --
            !IsI8Array(sig->GetParam(1), module, true) ||  // --
            sig->GetParam(2) != kI32 ||                    // --
            sig->GetReturn() != kI32) {
          RETURN_ERROR("text-encoder", "encodeStringIntoUTF8Array");
        }
        status = WellKnownImport::kStringIntoUtf8Array;
      } else if (name == base::StaticOneByteVector("encodeStringToUTF8Array")) {
        if (sig->parameter_count() != 1 || sig->return_count() != 1 ||
            sig->GetParam(0) != kExternRef ||
            !IsI8Array(sig->GetReturn(), module, false)) {
          RETURN_ERROR("text-encoder", "encodeStringToUTF8Array");
        }
        status = WellKnownImport::kStringToUtf8Array;
      }
    } else if (collection == base::StaticOneByteVector("text-decoder") &&
               imports.contains(CompileTimeImport::kTextDecoder)) {
      if (name == base::StaticOneByteVector("decodeStringFromUTF8Array")) {
        if (sig->parameter_count() != 3 || sig->return_count() != 1 ||
            !IsI8Array(sig->GetParam(0), module, true) ||  // --
            sig->GetParam(1) != kI32 ||                    // --
            sig->GetParam(2) != kI32 ||                    // --
            sig->GetReturn() != kRefExtern) {
          RETURN_ERROR("text-decoder", "decodeStringFromUTF8Array");
        }
        status = WellKnownImport::kStringFromUtf8Array;
      }
    }
#undef RETURN_ERROR
    statuses.push_back(status);
  }
  // We're operating on a fresh WasmModule instance here, so we don't need to
  // check for incompatibilities with previously seen imports.
  DCHECK_EQ(module->num_imported_functions, statuses.size());
  // The "Initialize" call is currently only safe when the decoder has allocated
  // storage, which it allocates when there is an imports section.
  if (module->num_imported_functions != 0) {
    module->type_feedback.well_known_imports.Initialize(
        base::VectorOf(statuses));
  }
  return {};
}

namespace {

void RecordStats(Tagged<Code> code, Counters* counters) {
  if (!code->has_instruction_stream()) return;
  counters->wasm_generated_code_size()->Increment(code->body_size());
  counters->wasm_reloc_size()->Increment(code->relocation_size());
}

enum CompilationExecutionResult : int8_t { kNoMoreUnits, kYield };

const char* GetCompilationEventName(const WasmCompilationUnit& unit,
                                    const CompilationEnv& env) {
  ExecutionTier tier = unit.tier();
  if (tier == ExecutionTier::kLiftoff) {
    return "wasm.BaselineCompilation";
  }
  if (tier == ExecutionTier::kTurbofan) {
    return "wasm.TopTierCompilation";
  }
  if (unit.func_index() <
      static_cast<int>(env.module->num_imported_functions)) {
    return "wasm.WasmToJSWrapperCompilation";
  }
  return "wasm.OtherCompilation";
}

constexpr uint8_t kMainTaskId = 0;

// Run by the {BackgroundCompileJob} (on any thread).
CompilationExecutionResult ExecuteCompilationUnits(
    std::weak_ptr<NativeModule> native_module, Counters* counters,
    JobDelegate* delegate, CompilationTier tier) {
  TRACE_EVENT0("v8.wasm", "wasm.ExecuteCompilationUnits");

  // Compilation must be disabled in jitless mode.
  CHECK(!v8_flags.wasm_jitless);

  // These fields are initialized in a {BackgroundCompileScope} before
  // starting compilation.
  std::optional<CompilationEnv> env;
  std::shared_ptr<WireBytesStorage> wire_bytes;
  std::shared_ptr<const WasmModule> module;
  // Task 0 is any main thread (there might be multiple from multiple isolates),
  // worker threads start at 1 (thus the "+ 1").
  static_assert(kMainTaskId == 0);
  int task_id = delegate ? (int{delegate->GetTaskId()} + 1) : kMainTaskId;
  DCHECK_LE(0, task_id);
  CompilationUnitQueues::Queue* queue;
  std::optional<WasmCompilationUnit> unit;

  WasmDetectedFeatures global_detected_features;

  // Preparation (synchronized): Initialize the fields above and get the first
  // compilation unit.
  {
    BackgroundCompileScope compile_scope(native_module);
    if (compile_scope.cancelled()) return kYield;
    env.emplace(CompilationEnv::ForModule(compile_scope.native_module()));
    wire_bytes = compile_scope.compilation_state()->GetWireBytesStorage();
    module = compile_scope.native_module()->shared_module();
    queue = compile_scope.compilation_state()->GetQueueForCompileTask(task_id);
    unit =
        compile_scope.compilation_state()->GetNextCompilationUnit(queue, tier);
    if (!unit) return kNoMoreUnits;
  }
  TRACE_COMPILE("ExecuteCompilationUnits (task id %d)\n", task_id);

  std::vector<WasmCompilationResult> results_to_publish;
  while (true) {
    ExecutionTier current_tier = unit->tier();
    const char* event_name = GetCompilationEventName(unit.value(), env.value());
    TRACE_EVENT0("v8.wasm", event_name);
    while (unit->tier() == current_tier) {
      // Track detected features on a per-function basis before collecting them
      // into {global_detected_features}.
      WasmDetectedFeatures per_function_detected_features;
      // (asynchronous): Execute the compilation.
      WasmCompilationResult result =
          unit->ExecuteCompilation(&env.value(), wire_bytes.get(), counters,
                                   &per_function_detected_features);
      global_detected_features.Add(per_function_detected_features);
      bool compilation_succeeded = result.succeeded();
      ExecutionTier result_tier = result.result_tier;
      // We don't eagerly compile import wrappers any more.
      DCHECK_GE(unit->func_index(), env->module->num_imported_functions);
      results_to_publish.emplace_back(std::move(result));

      bool yield = delegate && delegate->ShouldYield();

      // (synchronized): Publish the compilation result and get the next unit.
      BackgroundCompileScope compile_scope(native_module);
      if (compile_scope.cancelled()) return kYield;

      if (!compilation_succeeded) {
        compile_scope.compilation_state()->SetError();
        return kNoMoreUnits;
      }

      if (!unit->for_debugging() && result_tier != current_tier) {
        compile_scope.native_module()->AddLiftoffBailout();
      }

      // Yield or get next unit.
      if (yield ||
          !(unit = compile_scope.compilation_state()->GetNextCompilationUnit(
                queue, tier))) {
        std::vector<std::unique_ptr<WasmCode>> unpublished_code =
            compile_scope.native_module()->AddCompiledCode(
                base::VectorOf(results_to_publish));
        results_to_publish.clear();
        compile_scope.compilation_state()->SchedulePublishCompilationResults(
            std::move(unpublished_code), tier);
        compile_scope.compilation_state()->OnCompilationStopped(
            global_detected_features);
        return yield ? kYield : kNoMoreUnits;
      }

      // Publish after finishing a certain amount of units, to avoid
      // contention when all threads publish at the end.
      bool batch_full =
          queue->ShouldPublish(static_cast<int>(results_to_publish.size()));
      // Also publish each time the compilation tier changes from Liftoff to
      // TurboFan, such that we immediately publish the baseline compilation
      // results to start execution, and do not wait for a batch to fill up.
      bool liftoff_finished = unit->tier() != current_tier &&
                              unit->tier() == ExecutionTier::kTurbofan;
      if (batch_full || liftoff_finished) {
        std::vector<std::unique_ptr<WasmCode>> unpublished_code =
            compile_scope.native_module()->AddCompiledCode(
                base::VectorOf(results_to_publish));
        results_to_publish.clear();
        compile_scope.compilation_state()->SchedulePublishCompilationResults(
            std::move(unpublished_code), tier);
      }
    }
  }
  UNREACHABLE();
}

// Returns the number of units added.
int AddExportWrapperUnits(Isolate* isolate, NativeModule* native_module,
                          CompilationUnitBuilder* builder) {
  // Remember units already triggered for compilation.
  std::unordered_set<uint32_t> keys;

  const WasmModule* module = native_module->module();
  for (auto exp : module->export_table) {
    if (exp.kind != kExternalFunction) continue;
    auto& function = module->functions[exp.index];
    uint32_t canonical_type_index =
        module->isorecursive_canonical_type_ids[function.sig_index];
    if (static_cast<int>(canonical_type_index) <
        isolate->heap()->js_to_wasm_wrappers()->length()) {
      Tagged<MaybeObject> existing_wrapper =
          isolate->heap()->js_to_wasm_wrappers()->Get(canonical_type_index);
      if (existing_wrapper.IsStrongOrWeak() &&
          !IsUndefined(existing_wrapper.GetHeapObject())) {
        DCHECK(IsCodeWrapper(existing_wrapper.GetHeapObject()));
        // Skip wrapper compilation as the wrapper is already cached.
        // Note that this does not guarantee that the wrapper is still cached
        // at the moment at which the WasmInternalFunction is instantiated.
        continue;
      }
    }
    if (!keys.insert(canonical_type_index).second)
      continue;  // Already triggered.
    builder->AddJSToWasmWrapperUnit(JSToWasmWrapperCompilationUnit{
        isolate, function.sig, canonical_type_index, module,
        native_module->enabled_features()});
  }

  return static_cast<int>(keys.size());
}

std::unique_ptr<CompilationUnitBuilder> InitializeCompilation(
    Isolate* isolate, NativeModule* native_module,
    ProfileInformation* pgo_info) {
  CompilationStateImpl* compilation_state =
      Impl(native_module->compilation_state());
  auto builder = std::make_unique<CompilationUnitBuilder>(native_module);
  // Support for eagerly compiling import wrappers concurrently has been
  // dropped. We usually use the generic wrapper. If we don't (for testing
  // or unsupported signatures), we'll have to compile the required wrappers
  // during instantiation.
  int num_import_wrappers = 0;
  // Assume that the generic js-to-wasm wrapper can be used if it is enabled and
  // skip eager compilation of any export wrapper. Note that the generic
  // js-to-wasm wrapper does not support asm.js (yet).
  int num_export_wrappers =
      v8_flags.wasm_jitless || (v8_flags.wasm_generic_wrapper &&
                                !is_asmjs_module(native_module->module()))
          ? 0
          : AddExportWrapperUnits(isolate, native_module, builder.get());
  compilation_state->InitializeCompilationProgress(
      num_import_wrappers, num_export_wrappers, pgo_info);
  return builder;
}

bool MayCompriseLazyFunctions(const WasmModule* module,
                              WasmEnabledFeatures enabled_features) {
  if (IsLazyModule(module)) return true;
  if (enabled_features.has_compilation_hints()) return true;
#ifdef ENABLE_SLOW_DCHECKS
  int start = module->num_imported_functions;
  int end = start + module->num_declared_functions;
  for (int func_index = start; func_index < end; func_index++) {
    SLOW_DCHECK(GetCompileStrategy(module, enabled_features, func_index,
                                   false) != CompileStrategy::kLazy);
  }
#endif
  return false;
}

class CompilationTimeCallback : public CompilationEventCallback {
 public:
  enum CompileMode { kSynchronous, kAsync, kStreaming };
  explicit CompilationTimeCallback(
      std::shared_ptr<Counters> async_counters,
      std::shared_ptr<metrics::Recorder> metrics_recorder,
      v8::metrics::Recorder::ContextId context_id,
      std::weak_ptr<NativeModule> native_module, CompileMode compile_mode)
      : start_time_(base::TimeTicks::Now()),
        async_counters_(std::move(async_counters)),
        metrics_recorder_(std::move(metrics_recorder)),
        context_id_(context_id),
        native_module_(std::move(native_module)),
        compile_mode_(compile_mode) {}

  void call(CompilationEvent compilation_event) override {
    DCHECK(base::TimeTicks::IsHighResolution());
    std::shared_ptr<NativeModule> native_module = native_module_.lock();
    if (!native_module) return;
    auto now = base::TimeTicks::Now();
    auto duration = now - start_time_;
    if (compilation_event == CompilationEvent::kFinishedBaselineCompilation) {
      // Reset {start_time_} to measure tier-up time.
      start_time_ = now;
      if (compile_mode_ != kSynchronous) {
        TimedHistogram* histogram =
            compile_mode_ == kAsync
                ? async_counters_->wasm_async_compile_wasm_module_time()
                : async_counters_->wasm_streaming_compile_wasm_module_time();
        histogram->AddSample(static_cast<int>(duration.InMicroseconds()));
      }

      v8::metrics::WasmModuleCompiled event{
          (compile_mode_ != kSynchronous),         // async
          (compile_mode_ == kStreaming),           // streamed
          false,                                   // cached
          false,                                   // deserialized
          v8_flags.wasm_lazy_compilation,          // lazy
          true,                                    // success
          native_module->liftoff_code_size(),      // code_size_in_bytes
          native_module->liftoff_bailout_count(),  // liftoff_bailout_count
          duration.InMicroseconds()};              // wall_clock_duration_in_us
      metrics_recorder_->DelayMainThreadEvent(event, context_id_);
    }
    if (compilation_event == CompilationEvent::kFailedCompilation) {
      v8::metrics::WasmModuleCompiled event{
          (compile_mode_ != kSynchronous),         // async
          (compile_mode_ == kStreaming),           // streamed
          false,                                   // cached
          false,                                   // deserialized
          v8_flags.wasm_lazy_compilation,          // lazy
          false,                                   // success
          native_module->liftoff_code_size(),      // code_size_in_bytes
          native_module->liftoff_bailout_count(),  // liftoff_bailout_count
          duration.InMicroseconds()};              // wall_clock_duration_in_us
      metrics_recorder_->DelayMainThreadEvent(event, context_id_);
    }
  }

 private:
  base::TimeTicks start_time_;
  const std::shared_ptr<Counters> async_counters_;
  std::shared_ptr<metrics::Recorder> metrics_recorder_;
  v8::metrics::Recorder::ContextId context_id_;
  std::weak_ptr<NativeModule> native_module_;
  const CompileMode compile_mode_;
};

WasmError ValidateFunctions(const WasmModule* module,
                            base::Vector<const uint8_t> wire_bytes,
                            WasmEnabledFeatures enabled_features,
                            OnlyLazyFunctions only_lazy_functions) {
  DCHECK_EQ(module->origin, kWasmOrigin);
  if (only_lazy_functions &&
      !MayCompriseLazyFunctions(module, enabled_features)) {
    return {};
  }

  std::function<bool(int)> filter;  // Initially empty for "all functions".
  if (only_lazy_functions) {
    const bool is_lazy_module = IsLazyModule(module);
    filter = [module, enabled_features, is_lazy_module](int func_index) {
      CompileStrategy strategy = GetCompileStrategy(module, enabled_features,
                                                    func_index, is_lazy_module);
      return strategy == CompileStrategy::kLazy ||
             strategy == CompileStrategy::kLazyBaselineEagerTopTier;
    };
  }
  // Call {ValidateFunctions} in the module decoder.
  return ValidateFunctions(module, enabled_features, wire_bytes, filter);
}

WasmError ValidateFunctions(const NativeModule& native_module,
                            OnlyLazyFunctions only_lazy_functions) {
  return ValidateFunctions(native_module.module(), native_module.wire_bytes(),
                           native_module.enabled_features(),
                           only_lazy_functions);
}

void CompileNativeModule(Isolate* isolate,
                         v8::metrics::Recorder::ContextId context_id,
                         ErrorThrower* thrower,
                         std::shared_ptr<NativeModule> native_module,
                         ProfileInformation* pgo_info) {
  CHECK(!v8_flags.jitless || v8_flags.wasm_jitless);
  const WasmModule* module = native_module->module();

  // The callback captures a shared ptr to the semaphore.
  auto* compilation_state = Impl(native_module->compilation_state());
  if (base::TimeTicks::IsHighResolution()) {
    compilation_state->AddCallback(std::make_unique<CompilationTimeCallback>(
        isolate->async_counters(), isolate->metrics_recorder(), context_id,
        native_module, CompilationTimeCallback::kSynchronous));
  }

  // Initialize the compilation units and kick off background compile tasks.
  std::unique_ptr<CompilationUnitBuilder> builder =
      InitializeCompilation(isolate, native_module.get(), pgo_info);
  compilation_state->InitializeCompilationUnits(std::move(builder));

  // Wrapper compilation jobs keep a pointer to the function signatures, so
  // finish them before we validate and potentially free the module.
  compilation_state->WaitForCompilationEvent(
      CompilationEvent::kFinishedExportWrappers);

  // Validate wasm modules for lazy compilation if requested. Never validate
  // asm.js modules as these are valid by construction (additionally a CHECK
  // will catch this during lazy compilation).
  if (!v8_flags.wasm_lazy_validation && module->origin == kWasmOrigin) {
    DCHECK(!thrower->error());
    if (WasmError validation_error =
            ValidateFunctions(*native_module, kOnlyLazyFunctions)) {
      thrower->CompileFailed(std::move(validation_error));
      return;
    }
  }

  if (!compilation_state->failed()) {
    compilation_state->FinalizeJSToWasmWrappers(isolate, module);
    compilation_state->WaitForCompilationEvent(
        CompilationEvent::kFinishedBaselineCompilation);

    compilation_state->PublishDetectedFeaturesAfterCompilation(isolate);
  }

  if (compilation_state->failed()) {
    DCHECK_IMPLIES(IsLazyModule(module), !v8_flags.wasm_lazy_validation);
    WasmError validation_error =
        ValidateFunctions(*native_module, kAllFunctions);
    CHECK(validation_error.has_error());
    thrower->CompileFailed(std::move(validation_error));
  }
}

class BaseCompileJSToWasmWrapperJob : public JobTask {
 public:
  explicit BaseCompileJSToWasmWrapperJob(size_t compilation_units)
      : outstanding_units_(compilation_units),
        total_units_(compilation_units) {}

  size_t GetMaxConcurrency(size_t worker_count) const override {
    size_t flag_limit = static_cast<size_t>(
        std::max(1, v8_flags.wasm_num_compilation_tasks.value()));
    // {outstanding_units_} includes the units that other workers are currently
    // working on, so we can safely ignore the {worker_count} and just return
    // the current number of outstanding units.
    return std::min(flag_limit,
                    outstanding_units_.load(std::memory_order_relaxed));
  }

 protected:
  // Returns {true} and places the index of the next unit to process in
  // {index_out} if there are still units to be processed. Returns {false}
  // otherwise.
  bool GetNextUnitIndex(size_t* index_out) {
    size_t next_index = unit_index_.fetch_add(1, std::memory_order_relaxed);
    if (next_index >= total_units_) {
      // {unit_index_} may exceed {total_units_}, but only by the number of
      // workers at worst, thus it can't exceed 2 * {total_units_} and overflow
      // shouldn't happen.
      DCHECK_GE(2 * total_units_, next_index);
      return false;
    }
    *index_out = next_index;
    return true;
  }

  // Returns true if the last unit was completed.
  bool CompleteUnit() {
    size_t outstanding_units =
        outstanding_units_.fetch_sub(1, std::memory_order_relaxed);
    DCHECK_GE(outstanding_units, 1);
    return outstanding_units == 1;
  }

  // When external cancellation is detected, call this method to bump
  // {unit_index_} and reset {outstanding_units_} such that no more tasks are
  // being scheduled for this job and all tasks exit as soon as possible.
  void FlushRemainingUnits() {
    // After being cancelled, make sure to reduce outstanding_units_ to
    // *basically* zero, but leave the count positive if other workers are still
    // running, to avoid underflow in {CompleteUnit}.
    size_t next_undone_unit =
        unit_index_.exchange(total_units_, std::memory_order_relaxed);
    size_t undone_units =
        next_undone_unit >= total_units_ ? 0 : total_units_ - next_undone_unit;
    // Note that the caller requested one unit that we also still need to remove
    // from {outstanding_units_}.
    ++undone_units;
    size_t previous_outstanding_units =
        outstanding_units_.fetch_sub(undone_units, std::memory_order_relaxed);
    CHECK_LE(undone_units, previous_outstanding_units);
  }

 private:
  std::atomic<size_t> unit_index_{0};
  std::atomic<size_t> outstanding_units_;
  const size_t total_units_;
};

class AsyncCompileJSToWasmWrapperJob final
    : public BaseCompileJSToWasmWrapperJob {
 public:
  explicit AsyncCompileJSToWasmWrapperJob(
      std::weak_ptr<NativeModule> native_module, size_t compilation_units)
      : BaseCompileJSToWasmWrapperJob(compilation_units),
        native_module_(std::move(native_module)),
        engine_barrier_(GetWasmEngine()->GetBarrierForBackgroundCompile()) {}

  void Run(JobDelegate* delegate) override {
    auto engine_scope = engine_barrier_->TryLock();
    if (!engine_scope) return;


    size_t index;
    if (!GetNextUnitIndex(&index)) return;

    // The wrapper units keep a pointer to the signature, so execute the units
    // inside the compile scope to keep the WasmModule's signature_zone alive.
    // This also allows to hold the JSToWasmWrapperCompilationUnits as raw
    // pointers.
    BackgroundCompileScope compile_scope(native_module_);
    if (compile_scope.cancelled()) return FlushRemainingUnits();

    JSToWasmWrapperCompilationUnit* wrapper_unit =
        compile_scope.compilation_state()->GetJSToWasmWrapperCompilationUnit(
            index);
    Isolate* isolate = wrapper_unit->isolate();
    OperationsBarrier::Token wrapper_compilation_token =
        wasm::GetWasmEngine()->StartWrapperCompilation(isolate);
    if (!wrapper_compilation_token) return FlushRemainingUnits();

    TRACE_EVENT0("v8.wasm", "wasm.JSToWasmWrapperCompilation");
    // In case multi-cage pointer compression mode is enabled ensure that
    // current thread's cage base values are properly initialized.
    PtrComprCageAccessScope ptr_compr_cage_access_scope(isolate);
    while (true) {
      DCHECK_EQ(isolate, wrapper_unit->isolate());
      wrapper_unit->Execute();
      bool complete_last_unit = CompleteUnit();
      bool yield = delegate && delegate->ShouldYield();
      if (yield && !complete_last_unit) return;

      if (complete_last_unit) {
        compile_scope.compilation_state()->OnFinishedJSToWasmWrapperUnits();
      }
      if (yield) return;
      if (!GetNextUnitIndex(&index)) return;
      wrapper_unit =
          compile_scope.compilation_state()->GetJSToWasmWrapperCompilationUnit(
              index);
    }
  }

 private:
  std::weak_ptr<NativeModule> native_module_;
  std::shared_ptr<OperationsBarrier> engine_barrier_;
};

class BackgroundCompileJob final : public JobTask {
 public:
  explicit BackgroundCompileJob(std::weak_ptr<NativeModule> native_module,
                                std::shared_ptr<Counters> async_counters,
                                CompilationTier tier)
      : native_module_(std::move(native_module)),
        engine_barrier_(GetWasmEngine()->GetBarrierForBackgroundCompile()),
        async_counters_(std::move(async_counters)),
        tier_(tier) {}

  void Run(JobDelegate* delegate) override {
    auto engine_scope = engine_barrier_->TryLock();
    if (!engine_scope) return;
    ExecuteCompilationUnits(native_module_, async_counters_.get(), delegate,
                            tier_);
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    BackgroundCompileScope compile_scope(native_module_);
    if (compile_scope.cancelled()) return 0;
    size_t flag_limit = static_cast<size_t>(
        std::max(1, v8_flags.wasm_num_compilation_tasks.value()));
    // NumOutstandingCompilations() does not reflect the units that running
    // workers are processing, thus add the current worker count to that number.
    return std::min(flag_limit,
                    worker_count + compile_scope.compilation_state()
                                       ->NumOutstandingCompilations(tier_));
  }

 private:
  std::weak_ptr<NativeModule> native_module_;
  std::shared_ptr<OperationsBarrier> engine_barrier_;
  const std::shared_ptr<Counters> async_counters_;
  const CompilationTier tier_;
};

}  // namespace

std::shared_ptr<NativeModule> CompileToNativeModule(
    Isolate* isolate, WasmEnabledFeatures enabled_features,
    CompileTimeImports compile_imports, ErrorThrower* thrower,
    std::shared_ptr<const WasmModule> module, ModuleWireBytes wire_bytes,
    int compilation_id, v8::metrics::Recorder::ContextId context_id,
    ProfileInformation* pgo_info) {
  WasmEngine* engine = GetWasmEngine();
  base::OwnedVector<uint8_t> wire_bytes_copy =
      base::OwnedVector<uint8_t>::Of(wire_bytes.module_bytes());
  // Prefer {wire_bytes_copy} to {wire_bytes.module_bytes()} for the temporary
  // cache key. When we eventually install the module in the cache, the wire
  // bytes of the temporary key and the new key have the same base pointer and
  // we can skip the full bytes comparison.
  std::shared_ptr<NativeModule> native_module = engine->MaybeGetNativeModule(
      module->origin, wire_bytes_copy.as_vector(), compile_imports, isolate);
  if (native_module) {
    // Ensure that we have the right wrappers in this isolate.
    CompileJsToWasmWrappers(isolate, module.get());
    return native_module;
  }

  std::optional<TimedHistogramScope> wasm_compile_module_time_scope;
  if (base::TimeTicks::IsHighResolution()) {
    wasm_compile_module_time_scope.emplace(SELECT_WASM_COUNTER(
        isolate->counters(), module->origin, wasm_compile, module_time));
  }

  // Embedder usage count for declared shared memories.
  const bool has_shared_memory =
      std::any_of(module->memories.begin(), module->memories.end(),
                  [](auto& memory) { return memory.is_shared; });
  if (has_shared_memory) {
    isolate->CountUsage(v8::Isolate::UseCounterFeature::kWasmSharedMemory);
  }

  // Create a new {NativeModule} first.
  const bool include_liftoff =
      module->origin == kWasmOrigin && v8_flags.liftoff;
  size_t code_size_estimate =
      wasm::WasmCodeManager::EstimateNativeModuleCodeSize(
          module.get(), include_liftoff,
          DynamicTiering{v8_flags.wasm_dynamic_tiering.value()});
  native_module = engine->NewNativeModule(isolate, enabled_features,
                                          std::move(compile_imports), module,
                                          code_size_estimate);
  native_module->SetWireBytes(std::move(wire_bytes_copy));
  native_module->compilation_state()->set_compilation_id(compilation_id);

  if (!v8_flags.wasm_jitless) {
    CompileNativeModule(isolate, context_id, thrower, native_module, pgo_info);
  }

  if (thrower->error()) {
    engine->UpdateNativeModuleCache(true, std::move(native_module), isolate);
    return {};
  }

  std::shared_ptr<NativeModule> cached_native_module =
      engine->UpdateNativeModuleCache(false, native_module, isolate);

  if (cached_native_module != native_module) {
    // Do not use {module} or {native_module} any more; use
    // {cached_native_module} instead.
    module.reset();
    native_module.reset();
    return cached_native_module;
#if V8_ENABLE_DRUMBRAKE
  } else if (v8_flags.wasm_jitless) {
    CompileJsToWasmWrappers(isolate, cached_native_module->module());
    return native_module;
#endif  // V8_ENABLE_DRUMBRAKE
  }

  // Ensure that the code objects are logged before returning.
  engine->LogOutstandingCodesForIsolate(isolate);

  return native_module;
}

AsyncCompileJob::AsyncCompileJob(
    Isolate* isolate, WasmEnabledFeatures enabled_features,
    CompileTimeImports compile_imports, base::OwnedVector<const uint8_t> bytes,
    DirectHandle<Context> context,
    DirectHandle<NativeContext> incumbent_context, const char* api_method_name,
    std::shared_ptr<CompilationResultResolver> resolver, int compilation_id)
    : isolate_(isolate),
      api_method_name_(api_method_name),
      enabled_features_(enabled_features),
      compile_imports_(std::move(compile_imports)),
      dynamic_tiering_(DynamicTiering{v8_flags.wasm_dynamic_tiering.value()}),
      start_time_(base::TimeTicks::Now()),
      bytes_copy_(std::move(bytes)),
      wire_bytes_(bytes_copy_.as_vector()),
      resolver_(std::move(resolver)),
      compilation_id_(compilation_id) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.AsyncCompileJob");
  CHECK(v8_flags.wasm_async_compilation);
  CHECK(!v8_flags.jitless || v8_flags.wasm_jitless);
  v8::Isolate* v8_isolate = reinterpret_cast<v8::Isolate*>(isolate);
  v8::Platform* platform = V8::GetCurrentPlatform();
  foreground_task_runner_ = platform->GetForegroundTaskRunner(v8_isolate);
  native_context_ =
      isolate->global_handles()->Create(context->native_context());
  incumbent_context_ = isolate->global_handles()->Create(*incumbent_context);
  DCHECK(IsNativeContext(*native_context_));
  context_id_ = isolate->GetOrRegisterRecorderContextId(native_context_);
  metrics_event_.async = true;
}

void AsyncCompileJob::Start() {
  DoAsync<DecodeModule>(isolate_->counters(),
                        isolate_->metrics_recorder());  // --
}

void AsyncCompileJob::Abort() {
  // Removing this job will trigger the destructor, which will cancel all
  // compilation.
  GetWasmEngine()->RemoveCompileJob(this);
}

// {ValidateFunctionsStreamingJobData} holds information that is shared between
// the {AsyncStreamingProcessor} and the {ValidateFunctionsStreamingJob}. It
// lives in the {AsyncStreamingProcessor} and is updated from both classes.
struct ValidateFunctionsStreamingJobData {
  struct Unit {
    // {func_index == -1} represents an "invalid" unit.
    int func_index = -1;
    base::Vector<const uint8_t> code;

    // Check whether the unit is valid.
    operator bool() const {
      DCHECK_LE(-1, func_index);
      return func_index >= 0;
    }
  };

  void Initialize(int num_declared_functions) {
    DCHECK_NULL(units);
    units = base::OwnedVector<Unit>::NewForOverwrite(num_declared_functions);
    // Initially {next == end}.
    next_available_unit.store(units.begin(), std::memory_order_relaxed);
    end_of_available_units.store(units.begin(), std::memory_order_relaxed);
  }

  void AddUnit(int declared_func_index, base::Vector<const uint8_t> code,
               JobHandle* job_handle) {
    DCHECK_NOT_NULL(units);
    // Write new unit to {*end}, then increment {end}. There is only one thread
    // adding new units, so no further synchronization needed.
    Unit* ptr = end_of_available_units.load(std::memory_order_relaxed);
    // Check invariant: {next <= end}.
    DCHECK_LE(next_available_unit.load(std::memory_order_relaxed), ptr);
    *ptr++ = {declared_func_index, code};
    // Use release semantics, so whoever loads this pointer (using acquire
    // semantics) sees all our previous stores.
    end_of_available_units.store(ptr, std::memory_order_release);
    size_t total_units_added = ptr - units.begin();
    // Periodically notify concurrency increase. This has overhead, so avoid
    // calling it too often. As long as threads are still running they will
    // continue processing new units anyway, and if background threads validate
    // faster than we can add units, then only notifying after increasingly long
    // delays is the right thing to do to avoid too many small validation tasks.
    // We notify on each power of two after 16 units, and every 16k units (just
    // to have *some* upper limit and avoiding to pile up too many units).
    // Additionally, notify after receiving the last unit of the module.
    if ((total_units_added >= 16 &&
         base::bits::IsPowerOfTwo(total_units_added)) ||
        (total_units_added % (16 * 1024)) == 0 || ptr == units.end()) {
      job_handle->NotifyConcurrencyIncrease();
    }
  }

  size_t NumOutstandingUnits() const {
    Unit* next = next_available_unit.load(std::memory_order_relaxed);
    Unit* end = end_of_available_units.load(std::memory_order_relaxed);
    DCHECK_LE(next, end);
    return end - next;
  }

  // Retrieve one unit to validate; returns an "invalid" unit if nothing is in
  // the queue.
  Unit GetUnit() {
    // Use an acquire load to synchronize with the store in {AddUnit}. All units
    // before this {end} are fully initialized and ready to execute.
    Unit* end = end_of_available_units.load(std::memory_order_acquire);
    Unit* next = next_available_unit.load(std::memory_order_relaxed);
    while (next < end) {
      if (next_available_unit.compare_exchange_weak(
              next, next + 1, std::memory_order_relaxed)) {
        return *next;
      }
      // Otherwise retry with updated {next} pointer.
    }
    return {};
  }

  base::OwnedVector<Unit> units;
  std::atomic<Unit*> next_available_unit;
  std::atomic<Unit*> end_of_available_units;
  std::atomic<bool> found_error{false};
};

class ValidateFunctionsStreamingJob final : public JobTask {
 public:
  ValidateFunctionsStreamingJob(const WasmModule* module,
                                WasmEnabledFeatures enabled_features,
                                ValidateFunctionsStreamingJobData* data)
      : module_(module), enabled_features_(enabled_features), data_(data) {}

  void Run(JobDelegate* delegate) override {
    TRACE_EVENT0("v8.wasm", "wasm.ValidateFunctionsStreaming");
    using Unit = ValidateFunctionsStreamingJobData::Unit;
    Zone validation_zone{GetWasmEngine()->allocator(), ZONE_NAME};
    while (Unit unit = data_->GetUnit()) {
      validation_zone.Reset();
      DecodeResult result =
          ValidateSingleFunction(&validation_zone, module_, unit.func_index,
                                 unit.code, enabled_features_);

      if (result.failed()) {
        data_->found_error.store(true, std::memory_order_relaxed);
        break;
      }
      // After validating one function, check if we should yield.
      if (delegate->ShouldYield()) break;
    }
  }

  size_t GetMaxConcurrency(size_t worker_count) const override {
    return worker_count + data_->NumOutstandingUnits();
  }

 private:
  const WasmModule* const module_;
  const WasmEnabledFeatures enabled_features_;
  ValidateFunctionsStreamingJobData* data_;
};

class AsyncStreamingProcessor final : public StreamingProcessor {
 public:
  explicit AsyncStreamingProcessor(AsyncCompileJob* job);

  bool ProcessModuleHeader(base::Vector<const uint8_t> bytes) override;

  bool ProcessSection(SectionCode section_code,
                      base::Vector<const uint8_t> bytes,
                      uint32_t offset) override;

  bool ProcessCodeSectionHeader(int num_functions,
                                uint32_t functions_mismatch_error_offset,
                                std::shared_ptr<WireBytesStorage>,
                                int code_section_start,
                                int code_section_length) override;

  bool ProcessFunctionBody(base::Vector<const uint8_t> bytes,
                           uint32_t offset) override;

  void OnFinishedChunk() override;

  void OnFinishedStream(base::OwnedVector<const uint8_t> bytes,
                        bool after_error) override;

  void OnAbort() override;

  bool Deserialize(base::Vector<const uint8_t> wire_bytes,
                   base::Vector<const uint8_t> module_bytes) override;

 private:
  void CommitCompilationUnits();

  ModuleDecoder decoder_;
  AsyncCompileJob* job_;
  std::unique_ptr<CompilationUnitBuilder> compilation_unit_builder_;
  int num_functions_ = 0;
  bool prefix_cache_hit_ = false;
  bool before_code_section_ = true;
  ValidateFunctionsStreamingJobData validate_functions_job_data_;
  std::unique_ptr<JobHandle> validate_functions_job_handle_;

  // Running hash of the wire bytes up to code section size, but excluding the
  // code section itself. Used by the {NativeModuleCache} to detect potential
  // duplicate modules.
  size_t prefix_hash_ = 0;
};

std::shared_ptr<StreamingDecoder> AsyncCompileJob::CreateStreamingDecoder() {
  DCHECK_NULL(stream_);
  stream_ = StreamingDecoder::CreateAsyncStreamingDecoder(
      std::make_unique<AsyncStreamingProcessor>(this));
  return stream_;
}

AsyncCompileJob::~AsyncCompileJob() {
  // Note: This destructor always runs on the foreground thread of the isolate.
  background_task_manager_.CancelAndWait();
  // If initial compilation did not finish yet we can abort it.
  if (native_module_) {
    Impl(native_module_->compilation_state())
        ->CancelCompilation(CompilationStateImpl::kCancelInitialCompilation);
  }
  // Tell the streaming decoder that the AsyncCompileJob is not available
  // anymore.
  if (stream_) stream_->NotifyCompilationDiscarded();
  CancelPendingForegroundTask();
  isolate_->global_handles()->Destroy(native_context_.location());
  isolate_->global_handles()->Destroy(incumbent_context_.location());
  if (!module_object_.is_null()) {
    isolate_->global_handles()->Destroy(module_object_.location());
  }
}

void AsyncCompileJob::CreateNativeModule(
    std::shared_ptr<const WasmModule> module, size_t code_size_estimate) {
  // Embedder usage count for declared shared memories.
  const bool has_shared_memory =
      std::any_of(module->memories.begin(), module->memories.end(),
                  [](auto& memory) { return memory.is_shared; });
  if (has_shared_memory) {
    isolate_->CountUsage(v8::Isolate::UseCounterFeature::kWasmSharedMemory);
  }

  // Create the module object and populate with compiled functions and
  // information needed at instantiation time.

  native_module_ = GetWasmEngine()->NewNativeModule(
      isolate_, enabled_features_, std::move(compile_imports_),
      std::move(module), code_size_estimate);
  native_module_->SetWireBytes(std::move(bytes_copy_));
  native_module_->compilation_state()->set_compilation_id(compilation_id_);
}

bool AsyncCompileJob::GetOrCreateNativeModule(
    std::shared_ptr<const WasmModule> module, size_t code_size_estimate) {
  native_module_ = GetWasmEngine()->MaybeGetNativeModule(
      module->origin, wire_bytes_.module_bytes(), compile_imports_, isolate_);
  if (native_module_ == nullptr) {
    CreateNativeModule(std::move(module), code_size_estimate);
    return false;
  }
  return true;
}

void AsyncCompileJob::PrepareRuntimeObjects() {
  // Create heap objects for script and module bytes to be stored in the
  // module object. Asm.js is not compiled asynchronously.
  DCHECK(module_object_.is_null());
  auto source_url =
      stream_ ? base::VectorOf(stream_->url()) : base::Vector<const char>();
  auto script =
      GetWasmEngine()->GetOrCreateScript(isolate_, native_module_, source_url);
  DirectHandle<WasmModuleObject> module_object =
      WasmModuleObject::New(isolate_, native_module_, script);

  module_object_ = isolate_->global_handles()->Create(*module_object);
}

// This function assumes that it is executed in a HandleScope, and that a
// context is set on the isolate.
void AsyncCompileJob::FinishCompile(bool is_after_cache_hit) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.FinishAsyncCompile");
  if (stream_) {
    stream_->NotifyNativeModuleCreated(native_module_);
  }
  const WasmModule* module = native_module_->module();
  auto compilation_state = Impl(native_module_->compilation_state());

  // If experimental PGO via files is enabled, load profile information now that
  // we have all wire bytes and know that the module is valid.
  if (V8_UNLIKELY(v8_flags.experimental_wasm_pgo_from_file)) {
    std::unique_ptr<ProfileInformation> pgo_info =
        LoadProfileFromFile(module, native_module_->wire_bytes());
    if (pgo_info) {
      compilation_state->ApplyPgoInfoLate(pgo_info.get());
    }
  }

  bool is_after_deserialization = !module_object_.is_null();
  if (!is_after_deserialization) {
    PrepareRuntimeObjects();
  }

  // Measure duration of baseline compilation or deserialization from cache.
  if (base::TimeTicks::IsHighResolution()) {
    base::TimeDelta duration = base::TimeTicks::Now() - start_time_;
    int duration_usecs = static_cast<int>(duration.InMicroseconds());
    isolate_->counters()->wasm_streaming_finish_wasm_module_time()->AddSample(
        duration_usecs);

    if (is_after_cache_hit || is_after_deserialization) {
      v8::metrics::WasmModuleCompiled event{
          true,                                     // async
          true,                                     // streamed
          is_after_cache_hit,                       // cached
          is_after_deserialization,                 // deserialized
          v8_flags.wasm_lazy_compilation,           // lazy
          !compilation_state->failed(),             // success
          native_module_->turbofan_code_size(),     // code_size_in_bytes
          native_module_->liftoff_bailout_count(),  // liftoff_bailout_count
          duration.InMicroseconds()};               // wall_clock_duration_in_us
      isolate_->metrics_recorder()->DelayMainThreadEvent(event, context_id_);
    }
  }

  DCHECK(!isolate_->context().is_null());
  // Finish the wasm script now and make it public to the debugger.
  DirectHandle<Script> script(module_object_->script(), isolate_);
  if (script->type() == Script::Type::kWasm &&
      module->debug_symbols.type == WasmDebugSymbols::Type::SourceMap &&
      !module->debug_symbols.external_url.is_empty()) {
    ModuleWireBytes wire_bytes(native_module_->wire_bytes());
    MaybeHandle<String> src_map_str = isolate_->factory()->NewStringFromUtf8(
        wire_bytes.GetNameOrNull(module->debug_symbols.external_url),
        AllocationType::kOld);
    script->set_source_mapping_url(*src_map_str.ToHandleChecked());
  }
  {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
                 "wasm.Debug.OnAfterCompile");
    isolate_->debug()->OnAfterCompile(script);
  }

  // TODO(bbudge) Allow deserialization without wrapper compilation, so we can
  // just compile wrappers here.
  if (!is_after_deserialization) {
    if (is_after_cache_hit) {
      // TODO(thibaudm): Look into sharing wrappers.
      CompileJsToWasmWrappers(isolate_, module);
    } else {
      compilation_state->FinalizeJSToWasmWrappers(isolate_, module);
    }
  }

  // We can only update the feature counts once the entire compile is done.
  compilation_state->PublishDetectedFeaturesAfterCompilation(isolate_);

  // We might need debug code for the module, if the debugger was enabled while
  // streaming compilation was running. Since handling this while compiling via
  // streaming is tricky, we just remove all code which may have been generated,
  // and compile debug code lazily.
  if (native_module_->IsInDebugState()) {
    WasmCodeRefScope ref_scope;
    native_module_->RemoveCompiledCode(
        NativeModule::RemoveFilter::kRemoveNonDebugCode);
  }

  // Finally, log all generated code (it does not matter if this happens
  // repeatedly in case the script is shared).
  native_module_->LogWasmCodes(isolate_, module_object_->script());

  FinishSuccessfully();
}

void AsyncCompileJob::Failed() {
  // {job} keeps the {this} pointer alive.
  std::unique_ptr<AsyncCompileJob> job =
      GetWasmEngine()->RemoveCompileJob(this);

  // Revalidate the whole module to produce a deterministic error message.
  constexpr bool kValidate = true;
  ModuleResult result = DecodeWasmModule(
      enabled_features_, wire_bytes_.module_bytes(), kValidate, kWasmOrigin);
  ErrorThrower thrower(isolate_, api_method_name_);
  if (result.failed()) {
    thrower.CompileFailed(std::move(result).error());
  } else {
    // The only possible reason why {result} might be okay is if the failure
    // was due to compile-time imports checking.
    CHECK(!job->compile_imports_.empty());
    WasmError error = ValidateAndSetBuiltinImports(result.value().get(),
                                                   wire_bytes_.module_bytes(),
                                                   job->compile_imports_);
    CHECK(error.has_error());
    thrower.LinkError("%s", error.message().c_str());
  }
  resolver_->OnCompilationFailed(thrower.Reify());
}

class AsyncCompileJob::CompilationStateCallback
    : public CompilationEventCallback {
 public:
  explicit CompilationStateCallback(AsyncCompileJob* job) : job_(job) {}

  void call(CompilationEvent event) override {
    // This callback is only being called from a foreground task.
    switch (event) {
      case CompilationEvent::kFinishedExportWrappers:
        // Even if baseline compilation units finish first, we trigger the
        // "kFinishedExportWrappers" event first.
        DCHECK(!last_event_.has_value());
        break;
      case CompilationEvent::kFinishedBaselineCompilation:
        DCHECK_EQ(CompilationEvent::kFinishedExportWrappers, last_event_);
        if (job_->DecrementAndCheckFinisherCount()) {
          // Install the native module in the cache, or reuse a conflicting one.
          // If we get a conflicting module, wait until we are back in the
          // main thread to update {job_->native_module_} to avoid a data race.
          std::shared_ptr<NativeModule> cached_native_module =
              GetWasmEngine()->UpdateNativeModuleCache(
                  false, job_->native_module_, job_->isolate_);
          if (cached_native_module == job_->native_module_) {
            // There was no cached module.
            cached_native_module = nullptr;
          }
          job_->DoSync<FinishCompilation>(std::move(cached_native_module));
        }
        break;
      case CompilationEvent::kFinishedCompilationChunk:
        DCHECK(CompilationEvent::kFinishedBaselineCompilation == last_event_ ||
               CompilationEvent::kFinishedCompilationChunk == last_event_);
        break;
      case CompilationEvent::kFailedCompilation:
        DCHECK(!last_event_.has_value() ||
               last_event_ == CompilationEvent::kFinishedExportWrappers);
        if (job_->DecrementAndCheckFinisherCount()) {
          // Don't update {job_->native_module_} to avoid data races with other
          // compilation threads. Use a copy of the shared pointer instead.
          GetWasmEngine()->UpdateNativeModuleCache(true, job_->native_module_,
                                                   job_->isolate_);
          job_->DoSync<Fail>();
        }
        break;
    }
#ifdef DEBUG
    last_event_ = event;
#endif
  }

 private:
  AsyncCompileJob* job_;
#ifdef DEBUG
  // This will be modified by different threads, but they externally
  // synchronize, so no explicit synchronization (currently) needed here.
  std::optional<CompilationEvent> last_event_;
#endif
};

// A closure to run a compilation step (either as foreground or background
// task) and schedule the next step(s), if any.
class AsyncCompileJob::CompileStep {
 public:
  virtual ~CompileStep() = default;

  void Run(AsyncCompileJob* job, bool on_foreground) {
    if (on_foreground) {
      HandleScope scope(job->isolate_);
      SaveAndSwitchContext saved_context(job->isolate_, *job->native_context_);
      RunInForeground(job);
    } else {
      RunInBackground(job);
    }
  }

  virtual void RunInForeground(AsyncCompileJob*) { UNREACHABLE(); }
  virtual void RunInBackground(AsyncCompileJob*) { UNREACHABLE(); }
};

class AsyncCompileJob::CompileTask : public CancelableTask {
 public:
  CompileTask(AsyncCompileJob* job, bool on_foreground)
      // We only manage the background tasks with the {CancelableTaskManager} of
      // the {AsyncCompileJob}. Foreground tasks are managed by the system's
      // {CancelableTaskManager}. Background tasks cannot spawn tasks managed by
      // their own task manager.
      : CancelableTask(on_foreground ? job->isolate_->cancelable_task_manager()
                                     : &job->background_task_manager_),
        job_(job),
        on_foreground_(on_foreground) {}

  ~CompileTask() override {
    if (job_ != nullptr && on_foreground_) ResetPendingForegroundTask();
  }

  void RunInternal() final {
    if (!job_) return;
    if (on_foreground_) ResetPendingForegroundTask();
    job_->step_->Run(job_, on_foreground_);
    // After execution, reset {job_} such that we don't try to reset the pending
    // foreground task when the task is deleted.
    job_ = nullptr;
  }

  void Cancel() {
    DCHECK_NOT_NULL(job_);
    job_ = nullptr;
  }

 private:
  // {job_} will be cleared to cancel a pending task.
  AsyncCompileJob* job_;
  bool on_foreground_;

  void ResetPendingForegroundTask() const {
    DCHECK_EQ(this, job_->pending_foreground_task_);
    job_->pending_foreground_task_ = nullptr;
  }
};

void AsyncCompileJob::StartForegroundTask() {
  DCHECK_NULL(pending_foreground_task_);

  auto new_task = std::make_unique<CompileTask>(this, true);
  pending_foreground_task_ = new_task.get();
  foreground_task_runner_->PostTask(std::move(new_task));
}

void AsyncCompileJob::ExecuteForegroundTaskImmediately() {
  DCHECK_NULL(pending_foreground_task_);

  auto new_task = std::make_unique<CompileTask>(this, true);
  pending_foreground_task_ = new_task.get();
  new_task->Run();
}

void AsyncCompileJob::CancelPendingForegroundTask() {
  if (!pending_foreground_task_) return;
  pending_foreground_task_->Cancel();
  pending_foreground_task_ = nullptr;
}

void AsyncCompileJob::StartBackgroundTask() {
  auto task = std::make_unique<CompileTask>(this, false);

  // If --wasm-num-compilation-tasks=0 is passed, do only spawn foreground
  // tasks. This is used to make timing deterministic.
  if (v8_flags.wasm_num_compilation_tasks > 0) {
    V8::GetCurrentPlatform()->CallBlockingTaskOnWorkerThread(std::move(task));
  } else {
    foreground_task_runner_->PostTask(std::move(task));
  }
}

template <typename Step,
          AsyncCompileJob::UseExistingForegroundTask use_existing_fg_task,
          typename... Args>
void AsyncCompileJob::DoSync(Args&&... args) {
  NextStep<Step>(std::forward<Args>(args)...);
  if (use_existing_fg_task && pending_foreground_task_ != nullptr) return;
  StartForegroundTask();
}

template <typename Step, typename... Args>
void AsyncCompileJob::DoImmediately(Args&&... args) {
  NextStep<Step>(std::forward<Args>(args)...);
  ExecuteForegroundTaskImmediately();
}

template <typename Step, typename... Args>
void AsyncCompileJob::DoAsync(Args&&... args) {
  NextStep<Step>(std::forward<Args>(args)...);
  StartBackgroundTask();
}

template <typename Step, typename... Args>
void AsyncCompileJob::NextStep(Args&&... args) {
  step_.reset(new Step(std::forward<Args>(args)...));
}

//==========================================================================
// Step 1: (async) Decode the module.
//==========================================================================
class AsyncCompileJob::DecodeModule : public AsyncCompileJob::CompileStep {
 public:
  explicit DecodeModule(Counters* counters,
                        std::shared_ptr<metrics::Recorder> metrics_recorder)
      : counters_(counters), metrics_recorder_(std::move(metrics_recorder)) {}

  void RunInBackground(AsyncCompileJob* job) override {
    ModuleResult result;
    {
      DisallowHandleAllocation no_handle;
      DisallowGarbageCollection no_gc;
      // Decode the module bytes.
      TRACE_COMPILE("(1) Decoding module...\n");
      TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
                   "wasm.DecodeModule");
      auto enabled_features = job->enabled_features_;
      result =
          DecodeWasmModule(enabled_features, job->wire_bytes_.module_bytes(),
                           false, kWasmOrigin, counters_, metrics_recorder_,
                           job->context_id(), DecodingMethod::kAsync);

      // Validate lazy functions here if requested.
      if (result.ok() && !v8_flags.wasm_lazy_validation) {
        const WasmModule* module = result.value().get();
        if (WasmError validation_error =
                ValidateFunctions(module, job->wire_bytes_.module_bytes(),
                                  job->enabled_features_, kOnlyLazyFunctions)) {
          result = ModuleResult{std::move(validation_error)};
        }
      }
      if (result.ok()) {
        const WasmModule* module = result.value().get();
        if (WasmError link_error = ValidateAndSetBuiltinImports(
                module, job->wire_bytes_.module_bytes(),
                job->compile_imports_)) {
          result = ModuleResult{std::move(link_error)};
        }
      }
    }
    if (result.failed()) {
      // Decoding failure; reject the promise and clean up.
      job->DoSync<Fail>();
    } else {
      // Decode passed.
      std::shared_ptr<WasmModule> module = std::move(result).value();
      const bool include_liftoff = v8_flags.liftoff;
      size_t code_size_estimate =
          wasm::WasmCodeManager::EstimateNativeModuleCodeSize(
              module.get(), include_liftoff, job->dynamic_tiering_);
      job->DoSync<PrepareAndStartCompile>(
          std::move(module), true /* start_compilation */,
          true /* lazy_functions_are_validated */, code_size_estimate);
    }
  }

 private:
  Counters* const counters_;
  std::shared_ptr<metrics::Recorder> metrics_recorder_;
};

//==========================================================================
// Step 2 (sync): Create heap-allocated data and start compilation.
//==========================================================================
class AsyncCompileJob::PrepareAndStartCompile : public CompileStep {
 public:
  PrepareAndStartCompile(std::shared_ptr<const WasmModule> module,
                         bool start_compilation,
                         bool lazy_functions_are_validated,
                         size_t code_size_estimate)
      : module_(std::move(module)),
        start_compilation_(start_compilation),
        lazy_functions_are_validated_(lazy_functions_are_validated),
        code_size_estimate_(code_size_estimate) {}

 private:
  void RunInForeground(AsyncCompileJob* job) override {
    TRACE_COMPILE("(2) Prepare and start compile...\n");

    const bool streaming = job->wire_bytes_.length() == 0;
    if (streaming) {
      // Streaming compilation already checked for cache hits.
      job->CreateNativeModule(module_, code_size_estimate_);
    } else if (job->GetOrCreateNativeModule(std::move(module_),
                                            code_size_estimate_)) {
      job->FinishCompile(true);
      return;
    } else if (!lazy_functions_are_validated_) {
      // If we are not streaming and did not get a cache hit, we might have hit
      // the path where the streaming decoder got a prefix cache hit, but the
      // module then turned out to be invalid, and we are running it through
      // non-streaming decoding again. In this case, function bodies have not
      // been validated yet (would have happened in the {DecodeModule} phase
      // if we would not come via the non-streaming path). Thus do this now.
      // Note that we only need to validate lazily compiled functions, others
      // will be validated during eager compilation.
      DCHECK(start_compilation_);
      if (!v8_flags.wasm_lazy_validation &&
          ValidateFunctions(*job->native_module_, kOnlyLazyFunctions)
              .has_error()) {
        job->Failed();
        return;
      }
    }

    // Make sure all compilation tasks stopped running. Decoding (async step)
    // is done.
    job->background_task_manager_.CancelAndWait();

    CompilationStateImpl* compilation_state =
        Impl(job->native_module_->compilation_state());
    compilation_state->AddCallback(
        std::make_unique<CompilationStateCallback>(job));
    if (base::TimeTicks::IsHighResolution()) {
      auto compile_mode = job->stream_ == nullptr
                              ? CompilationTimeCallback::kAsync
                              : CompilationTimeCallback::kStreaming;
      compilation_state->AddCallback(std::make_unique<CompilationTimeCallback>(
          job->isolate_->async_counters(), job->isolate_->metrics_recorder(),
          job->context_id_, job->native_module_, compile_mode));
    }

    if (start_compilation_) {
      // TODO(13209): Use PGO for async compilation, if available.
      constexpr ProfileInformation* kNoProfileInformation = nullptr;
      std::unique_ptr<CompilationUnitBuilder> builder = InitializeCompilation(
          job->isolate(), job->native_module_.get(), kNoProfileInformation);
      compilation_state->InitializeCompilationUnits(std::move(builder));
      // In single-threaded mode there are no worker tasks that will do the
      // compilation. We call {WaitForCompilationEvent} here so that the main
      // thread participates and finishes the compilation.
      if (v8_flags.wasm_num_compilation_tasks == 0 || v8_flags.wasm_jitless) {
        compilation_state->WaitForCompilationEvent(
            CompilationEvent::kFinishedBaselineCompilation);
      }
    }
  }

  const std::shared_ptr<const WasmModule> module_;
  const bool start_compilation_;
  const bool lazy_functions_are_validated_;
  const size_t code_size_estimate_;
};

//==========================================================================
// Step 3 (sync): Compilation finished.
//==========================================================================
class AsyncCompileJob::FinishCompilation : public CompileStep {
 public:
  explicit FinishCompilation(std::shared_ptr<NativeModule> cached_native_module)
      : cached_native_module_(std::move(cached_native_module)) {}

 private:
  void RunInForeground(AsyncCompileJob* job) override {
    TRACE_COMPILE("(3) Compilation finished\n");
    if (cached_native_module_) {
      job->native_module_ = cached_native_module_;
    }
    // Then finalize and publish the generated module.
    job->FinishCompile(cached_native_module_ != nullptr);
  }

  std::shared_ptr<NativeModule> cached_native_module_;
};

//==========================================================================
// Step 4 (sync): Decoding or compilation failed.
//==========================================================================
class AsyncCompileJob::Fail : public CompileStep {
 private:
  void RunInForeground(AsyncCompileJob* job) override {
    TRACE_COMPILE("(4) Async compilation failed.\n");
    // {job_} is deleted in {Failed}, therefore the {return}.
    return job->Failed();
  }
};

void AsyncCompileJob::FinishSuccessfully() {
  TRACE_COMPILE("(4) Finish module...\n");
  {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
                 "wasm.OnCompilationSucceeded");
    // We have to make sure that an "incumbent context" is available in case
    // the module's start function calls out to Blink.
    Local<v8::Context> backup_incumbent_context =
        Utils::ToLocal(incumbent_context_);
    v8::Context::BackupIncumbentScope incumbent(backup_incumbent_context);
    resolver_->OnCompilationSucceeded(module_object_);
  }
  GetWasmEngine()->RemoveCompileJob(this);
}

AsyncStreamingProcessor::AsyncStreamingProcessor(AsyncCompileJob* job)
    : decoder_(job->enabled_features_),
      job_(job),
      compilation_unit_builder_(nullptr) {}

// Process the module header.
bool AsyncStreamingProcessor::ProcessModuleHeader(
    base::Vector<const uint8_t> bytes) {
  TRACE_STREAMING("Process module header...\n");
  decoder_.DecodeModuleHeader(bytes);
  if (!decoder_.ok()) return false;
  prefix_hash_ = GetWireBytesHash(bytes);
  return true;
}

// Process all sections except for the code section.
bool AsyncStreamingProcessor::ProcessSection(SectionCode section_code,
                                             base::Vector<const uint8_t> bytes,
                                             uint32_t offset) {
  TRACE_STREAMING("Process section %d ...\n", section_code);
  if (compilation_unit_builder_) {
    // We reached a section after the code section, we do not need the
    // compilation_unit_builder_ anymore.
    CommitCompilationUnits();
    compilation_unit_builder_.reset();
  }
  if (before_code_section_) {
    // Combine section hashes until code section.
    prefix_hash_ = base::hash_combine(prefix_hash_, GetWireBytesHash(bytes));
  }
  if (section_code == SectionCode::kUnknownSectionCode) {
    size_t bytes_consumed = ModuleDecoder::IdentifyUnknownSection(
        &decoder_, bytes, offset, &section_code);
    if (!decoder_.ok()) return false;
    if (section_code == SectionCode::kUnknownSectionCode) {
      // Skip unknown sections that we do not know how to handle.
      return true;
    }
    // Remove the unknown section tag from the payload bytes.
    offset += bytes_consumed;
    bytes = bytes.SubVector(bytes_consumed, bytes.size());
  }
  decoder_.DecodeSection(section_code, bytes, offset);
  return decoder_.ok();
}

// Start the code section.
bool AsyncStreamingProcessor::ProcessCodeSectionHeader(
    int num_functions, uint32_t functions_mismatch_error_offset,
    std::shared_ptr<WireBytesStorage> wire_bytes_storage,
    int code_section_start, int code_section_length) {
  DCHECK_LE(0, code_section_length);
  before_code_section_ = false;
  TRACE_STREAMING("Start the code section with %d functions...\n",
                  num_functions);
  prefix_hash_ = base::hash_combine(prefix_hash_,
                                    static_cast<uint32_t>(code_section_length));
  if (!decoder_.CheckFunctionsCount(static_cast<uint32_t>(num_functions),
                                    functions_mismatch_error_offset)) {
    return false;
  }

  decoder_.StartCodeSection({static_cast<uint32_t>(code_section_start),
                             static_cast<uint32_t>(code_section_length)});

  if (!GetWasmEngine()->GetStreamingCompilationOwnership(
          prefix_hash_, job_->compile_imports_)) {
    // Known prefix, wait until the end of the stream and check the cache.
    prefix_cache_hit_ = true;
    return true;
  }

  // Execute the PrepareAndStartCompile step immediately and not in a separate
  // task.
  int num_imported_functions =
      static_cast<int>(decoder_.module()->num_imported_functions);
  DCHECK_EQ(kWasmOrigin, decoder_.module()->origin);
  const bool include_liftoff = v8_flags.liftoff;
  size_t code_size_estimate =
      wasm::WasmCodeManager::EstimateNativeModuleCodeSize(
          num_functions, num_imported_functions, code_section_length,
          include_liftoff, job_->dynamic_tiering_);
  job_->DoImmediately<AsyncCompileJob::PrepareAndStartCompile>(
      decoder_.shared_module(),
      // start_compilation: false; triggered when we receive the bodies.
      false,
      // lazy_functions_are_validated: false (bodies not received yet).
      false, code_size_estimate);

  auto* compilation_state = Impl(job_->native_module_->compilation_state());
  compilation_state->SetWireBytesStorage(std::move(wire_bytes_storage));
  DCHECK_EQ(job_->native_module_->module()->origin, kWasmOrigin);

  // Set outstanding_finishers_ to 2, because both the AsyncCompileJob and the
  // AsyncStreamingProcessor have to finish.
  job_->outstanding_finishers_.store(2);
  // TODO(13209): Use PGO for streaming compilation, if available.
  constexpr ProfileInformation* kNoProfileInformation = nullptr;
  compilation_unit_builder_ = InitializeCompilation(
      job_->isolate(), job_->native_module_.get(), kNoProfileInformation);
  return true;
}

// Process a function body.
bool AsyncStreamingProcessor::ProcessFunctionBody(
    base::Vector<const uint8_t> bytes, uint32_t offset) {
  TRACE_STREAMING("Process function body %d ...\n", num_functions_);
  uint32_t func_index =
      decoder_.module()->num_imported_functions + num_functions_;
  ++num_functions_;
  // In case of {prefix_cache_hit} we still need the function body to be
  // decoded. Otherwise a later cache miss cannot be handled.
  decoder_.DecodeFunctionBody(func_index, static_cast<uint32_t>(bytes.length()),
                              offset);

  if (prefix_cache_hit_) {
    // Don't compile yet if we might have a cache hit.
    return true;
  }

  const WasmModule* module = decoder_.module();
  auto enabled_features = job_->enabled_features_;
  DCHECK_EQ(module->origin, kWasmOrigin);
  const bool lazy_module = v8_flags.wasm_lazy_compilation;
  CompileStrategy strategy =
      GetCompileStrategy(module, enabled_features, func_index, lazy_module);
  CHECK_IMPLIES(v8_flags.wasm_jitless, !v8_flags.wasm_lazy_validation);
  bool validate_lazily_compiled_function =
      v8_flags.wasm_jitless ||
      (!v8_flags.wasm_lazy_validation &&
       (strategy == CompileStrategy::kLazy ||
        strategy == CompileStrategy::kLazyBaselineEagerTopTier));
  if (validate_lazily_compiled_function) {
    // {bytes} is part of a section buffer owned by the streaming decoder. The
    // streaming decoder is held alive by the {AsyncCompileJob}, so we can just
    // use the {bytes} vector as long as the {AsyncCompileJob} is still running.
    if (!validate_functions_job_handle_) {
      validate_functions_job_data_.Initialize(module->num_declared_functions);
      validate_functions_job_handle_ = V8::GetCurrentPlatform()->CreateJob(
          TaskPriority::kUserVisible,
          std::make_unique<ValidateFunctionsStreamingJob>(
              module, enabled_features, &validate_functions_job_data_));
    }
    validate_functions_job_data_.AddUnit(func_index, bytes,
                                         validate_functions_job_handle_.get());
  }

  auto* compilation_state = Impl(job_->native_module_->compilation_state());
  compilation_state->AddCompilationUnit(compilation_unit_builder_.get(),
                                        func_index);
  return true;
}

void AsyncStreamingProcessor::CommitCompilationUnits() {
  DCHECK(compilation_unit_builder_);
  compilation_unit_builder_->Commit();
}

void AsyncStreamingProcessor::OnFinishedChunk() {
  TRACE_STREAMING("FinishChunk...\n");
  if (compilation_unit_builder_) CommitCompilationUnits();
}

// Finish the processing of the stream.
void AsyncStreamingProcessor::OnFinishedStream(
    base::OwnedVector<const uint8_t> bytes, bool after_error) {
  TRACE_STREAMING("Finish stream...\n");
  ModuleResult module_result = decoder_.FinishDecoding();
  if (module_result.failed()) after_error = true;

  if (validate_functions_job_handle_) {
    // Wait for background validation to finish, then check if a validation
    // error was found.
    // TODO(13447): Do not block here; register validation as another finisher
    // instead.
    validate_functions_job_handle_->Join();
    validate_functions_job_handle_.reset();
    if (validate_functions_job_data_.found_error) after_error = true;
  }

  job_->wire_bytes_ = ModuleWireBytes(bytes.as_vector());
  job_->bytes_copy_ = std::move(bytes);

  if (!after_error) {
    if (WasmError error = ValidateAndSetBuiltinImports(
            module_result.value().get(), job_->wire_bytes_.module_bytes(),
            job_->compile_imports_)) {
      after_error = true;
    }
  }

  // Record event metrics.
  auto duration = base::TimeTicks::Now() - job_->start_time_;
  job_->metrics_event_.success = !after_error;
  job_->metrics_event_.streamed = true;
  job_->metrics_event_.module_size_in_bytes = job_->wire_bytes_.length();
  job_->metrics_event_.function_count = num_functions_;
  job_->metrics_event_.wall_clock_duration_in_us = duration.InMicroseconds();
  job_->isolate_->metrics_recorder()->DelayMainThreadEvent(job_->metrics_event_,
                                                           job_->context_id_);

  if (after_error) {
    if (job_->native_module_ && job_->native_module_->wire_bytes().empty()) {
      // Clean up the temporary cache entry.
      GetWasmEngine()->StreamingCompilationFailed(prefix_hash_,
                                                  job_->compile_imports_);
    }
    // Calling {Failed} will invalidate the {AsyncCompileJob} and delete {this}.
    job_->Failed();
    return;
  }

  std::shared_ptr<WasmModule> module = std::move(module_result).value();

  // At this point we identified the module as valid (except maybe for function
  // bodies, if lazy validation is enabled).
  // This DCHECK could be considered slow, but it only happens once per async
  // module compilation, and we only re-decode the module structure, without
  // validation function bodies. Overall this does not add a lot of overhead.
  DCHECK(DecodeWasmModule(job_->enabled_features_,
                          job_->bytes_copy_.as_vector(),
                          /* validate functions */ false, kWasmOrigin)
             .ok());

  DCHECK_EQ(NativeModuleCache::PrefixHash(job_->wire_bytes_.module_bytes()),
            prefix_hash_);
  if (prefix_cache_hit_) {
    // Restart as an asynchronous, non-streaming compilation. Most likely
    // {PrepareAndStartCompile} will get the native module from the cache.
    const bool include_liftoff = v8_flags.liftoff;
    size_t code_size_estimate =
        wasm::WasmCodeManager::EstimateNativeModuleCodeSize(
            module.get(), include_liftoff, job_->dynamic_tiering_);
    job_->DoSync<AsyncCompileJob::PrepareAndStartCompile>(
        std::move(module), true /* start_compilation */,
        false /* lazy_functions_are_validated_ */, code_size_estimate);
    return;
  }

  // We have to open a HandleScope and prepare the Context for
  // CreateNativeModule, PrepareRuntimeObjects and FinishCompile as this is a
  // callback from the embedder.
  HandleScope scope(job_->isolate_);
  SaveAndSwitchContext saved_context(job_->isolate_, *job_->native_context_);

  // Record the size of the wire bytes and the number of functions. In
  // synchronous and asynchronous (non-streaming) compilation, this happens in
  // {DecodeWasmModule}.
  auto* module_size_histogram =
      job_->isolate_->counters()->wasm_wasm_module_size_bytes();
  module_size_histogram->AddSample(job_->wire_bytes_.module_bytes().length());
  auto* num_functions_histogram =
      job_->isolate_->counters()->wasm_functions_per_wasm_module();
  num_functions_histogram->AddSample(static_cast<int>(num_functions_));

  const bool has_code_section = job_->native_module_ != nullptr;
  bool cache_hit = false;
  if (!has_code_section) {
    // We are processing a WebAssembly module without code section. Create the
    // native module now (would otherwise happen in {PrepareAndStartCompile} or
    // {ProcessCodeSectionHeader}).
    constexpr size_t kCodeSizeEstimate = 0;
    cache_hit =
        job_->GetOrCreateNativeModule(std::move(module), kCodeSizeEstimate);
  } else {
    job_->native_module_->SetWireBytes(std::move(job_->bytes_copy_));
  }
  const bool needs_finish = job_->DecrementAndCheckFinisherCount();
  DCHECK_IMPLIES(!has_code_section, needs_finish);
  if (needs_finish) {
    const bool failed = job_->native_module_->compilation_state()->failed();
    if (!cache_hit) {
      auto* prev_native_module = job_->native_module_.get();
      job_->native_module_ = GetWasmEngine()->UpdateNativeModuleCache(
          failed, std::move(job_->native_module_), job_->isolate_);
      cache_hit = prev_native_module != job_->native_module_.get();
    }
    // We finally call {Failed} or {FinishCompile}, which will invalidate the
    // {AsyncCompileJob} and delete {this}.
    if (failed) {
      job_->Failed();
    } else {
      job_->FinishCompile(cache_hit);
    }
  }
}

void AsyncStreamingProcessor::OnAbort() {
  TRACE_STREAMING("Abort stream...\n");
  if (validate_functions_job_handle_) {
    validate_functions_job_handle_->Cancel();
    validate_functions_job_handle_.reset();
  }
  if (job_->native_module_ && job_->native_module_->wire_bytes().empty()) {
    // Clean up the temporary cache entry.
    GetWasmEngine()->StreamingCompilationFailed(prefix_hash_,
                                                job_->compile_imports_);
  }
  // {Abort} invalidates the {AsyncCompileJob}, which in turn deletes {this}.
  job_->Abort();
}

bool AsyncStreamingProcessor::Deserialize(
    base::Vector<const uint8_t> module_bytes,
    base::Vector<const uint8_t> wire_bytes) {
  TRACE_EVENT0("v8.wasm", "wasm.Deserialize");
  std::optional<TimedHistogramScope> time_scope;
  if (base::TimeTicks::IsHighResolution()) {
    time_scope.emplace(job_->isolate()->counters()->wasm_deserialization_time(),
                       job_->isolate());
  }
  // DeserializeNativeModule and FinishCompile assume that they are executed in
  // a HandleScope, and that a context is set on the isolate.
  HandleScope scope(job_->isolate_);
  SaveAndSwitchContext saved_context(job_->isolate_, *job_->native_context_);

  MaybeHandle<WasmModuleObject> result = DeserializeNativeModule(
      job_->isolate_, module_bytes, wire_bytes, job_->compile_imports_,
      base::VectorOf(job_->stream_->url()));

  if (result.is_null()) return false;

  job_->module_object_ =
      job_->isolate_->global_handles()->Create(*result.ToHandleChecked());
  job_->native_module_ = job_->module_object_->shared_native_module();
  job_->wire_bytes_ = ModuleWireBytes(job_->native_module_->wire_bytes());
  // Calling {FinishCompile} deletes the {AsyncCompileJob} and {this}.
  job_->FinishCompile(false);
  return true;
}

CompilationStateImpl::CompilationStateImpl(
    const std::shared_ptr<NativeModule>& native_module,
    std::shared_ptr<Counters> async_counters, DynamicTiering dynamic_tiering)
    : native_module_(native_module.get()),
      native_module_weak_(std::move(native_module)),
      async_counters_(std::move(async_counters)),
      compilation_unit_queues_(native_module->num_functions()),
      dynamic_tiering_(dynamic_tiering) {
  if (native_module->module()->memories.size() > 1) {
    detected_features_.add_multi_memory();
  }
}

void CompilationStateImpl::InitCompileJob() {
  DCHECK_NULL(baseline_compile_job_);
  DCHECK_NULL(top_tier_compile_job_);
  // Create the job, but don't spawn workers yet. This will happen on
  // {NotifyConcurrencyIncrease}.
  baseline_compile_job_ = V8::GetCurrentPlatform()->CreateJob(
      TaskPriority::kUserVisible,
      std::make_unique<BackgroundCompileJob>(
          native_module_weak_, async_counters_, CompilationTier::kBaseline));
  top_tier_compile_job_ = V8::GetCurrentPlatform()->CreateJob(
      TaskPriority::kUserVisible,
      std::make_unique<BackgroundCompileJob>(
          native_module_weak_, async_counters_, CompilationTier::kTopTier));
}

void CompilationStateImpl::CancelCompilation(
    CompilationStateImpl::CancellationPolicy cancellation_policy) {
  base::MutexGuard callbacks_guard(&callbacks_mutex_);

  if (cancellation_policy == kCancelInitialCompilation &&
      finished_events_.contains(
          CompilationEvent::kFinishedBaselineCompilation)) {
    // Initial compilation already finished; cannot be cancelled.
    return;
  }

  // std::memory_order_relaxed is sufficient because no other state is
  // synchronized with |compile_cancelled_|.
  compile_cancelled_.store(true, std::memory_order_relaxed);

  // No more callbacks after abort.
  callbacks_.clear();
}

bool CompilationStateImpl::cancelled() const {
  return compile_cancelled_.load(std::memory_order_relaxed);
}

void CompilationStateImpl::ApplyCompilationHintToInitialProgress(
    const WasmCompilationHint& hint, size_t hint_idx) {
  // Get old information.
  uint8_t& progress = compilation_progress_[hint_idx];
  ExecutionTier old_baseline_tier = RequiredBaselineTierField::decode(progress);
  ExecutionTier old_top_tier = RequiredTopTierField::decode(progress);

  // Compute new information.
  ExecutionTier new_baseline_tier =
      ApplyHintToExecutionTier(hint.baseline_tier, old_baseline_tier);
  ExecutionTier new_top_tier =
      ApplyHintToExecutionTier(hint.top_tier, old_top_tier);
  switch (hint.strategy) {
    case WasmCompilationHintStrategy::kDefault:
      // Be careful not to switch from lazy to non-lazy.
      if (old_baseline_tier == ExecutionTier::kNone) {
        new_baseline_tier = ExecutionTier::kNone;
      }
      if (old_top_tier == ExecutionTier::kNone) {
        new_top_tier = ExecutionTier::kNone;
      }
      break;
    case WasmCompilationHintStrategy::kLazy:
      new_baseline_tier = ExecutionTier::kNone;
      new_top_tier = ExecutionTier::kNone;
      break;
    case WasmCompilationHintStrategy::kEager:
      // Nothing to do, use the encoded (new) tiers.
      break;
    case WasmCompilationHintStrategy::kLazyBaselineEagerTopTier:
      new_baseline_tier = ExecutionTier::kNone;
      break;
  }

  progress = RequiredBaselineTierField::update(progress, new_baseline_tier);
  progress = RequiredTopTierField::update(progress, new_top_tier);

  // Update counter for outstanding baseline units.
  outstanding_baseline_units_ += (new_baseline_tier != ExecutionTier::kNone) -
                                 (old_baseline_tier != ExecutionTier::kNone);
}

void CompilationStateImpl::ApplyPgoInfoToInitialProgress(
    ProfileInformation* pgo_info) {
  // Functions that were executed in the profiling run are eagerly compiled to
  // Liftoff.
  const WasmModule* module = native_module_->module();
  for (int func_index : pgo_info->executed_functions()) {
    uint8_t& progress =
        compilation_progress_[declared_function_index(module, func_index)];
    ExecutionTier old_baseline_tier =
        RequiredBaselineTierField::decode(progress);
    // If the function is already marked for eager compilation, we are good.
    if (old_baseline_tier != ExecutionTier::kNone) continue;

    // Set the baseline tier to Liftoff, so we eagerly compile to Liftoff.
    // TODO(13288): Compile Liftoff code in the background, if lazy compilation
    // is enabled.
    progress =
        RequiredBaselineTierField::update(progress, ExecutionTier::kLiftoff);
    ++outstanding_baseline_units_;
  }

  // Functions that were tiered up during PGO generation are eagerly compiled to
  // TurboFan (in the background, not blocking instantiation).
  for (int func_index : pgo_info->tiered_up_functions()) {
    uint8_t& progress =
        compilation_progress_[declared_function_index(module, func_index)];
    ExecutionTier old_baseline_tier =
        RequiredBaselineTierField::decode(progress);
    ExecutionTier old_top_tier = RequiredTopTierField::decode(progress);
    // If the function is already marked for eager or background compilation to
    // TurboFan, we are good.
    if (old_baseline_tier == ExecutionTier::kTurbofan) continue;
    if (old_top_tier == ExecutionTier::kTurbofan) continue;

    // Set top tier to TurboFan, so we eagerly trigger compilation in the
    // background.
    progress = RequiredTopTierField::update(progress, ExecutionTier::kTurbofan);
  }
}

void CompilationStateImpl::ApplyPgoInfoLate(ProfileInformation* pgo_info) {
  TRACE_EVENT0("v8.wasm", "wasm.ApplyPgoInfo");
  const WasmModule* module = native_module_->module();
  CompilationUnitBuilder builder{native_module_};

  base::MutexGuard guard(&callbacks_mutex_);
  // Functions that were executed in the profiling run are eagerly compiled to
  // Liftoff (in the background).
  for (int func_index : pgo_info->executed_functions()) {
    uint8_t& progress =
        compilation_progress_[declared_function_index(module, func_index)];
    ExecutionTier old_baseline_tier =
        RequiredBaselineTierField::decode(progress);
    // If the function is already marked for eager compilation, we are good.
    if (old_baseline_tier != ExecutionTier::kNone) continue;

    // If we already compiled Liftoff or TurboFan code, we are also good.
    ExecutionTier reached_tier = ReachedTierField::decode(progress);
    if (reached_tier >= ExecutionTier::kLiftoff) continue;

    // Set the baseline tier to Liftoff and schedule a compilation unit.
    progress =
        RequiredBaselineTierField::update(progress, ExecutionTier::kLiftoff);
    // Add this as a "top tier unit" since it does not contribute to initial
    // compilation ("baseline finished" might already be triggered).
    // TODO(clemensb): Rename "baseline finished" to "initial compile finished".
    // TODO(clemensb): Avoid scheduling both a Liftoff and a TurboFan unit, or
    // prioritize Liftoff when executing the units.
    builder.AddTopTierUnit(func_index, ExecutionTier::kLiftoff);
  }

  // Functions that were tiered up during PGO generation are eagerly compiled to
  // TurboFan in the background.
  for (int func_index : pgo_info->tiered_up_functions()) {
    uint8_t& progress =
        compilation_progress_[declared_function_index(module, func_index)];
    ExecutionTier old_baseline_tier =
        RequiredBaselineTierField::decode(progress);
    ExecutionTier old_top_tier = RequiredTopTierField::decode(progress);
    // If the function is already marked for eager or background compilation to
    // TurboFan, we are good.
    if (old_baseline_tier == ExecutionTier::kTurbofan) continue;
    if (old_top_tier == ExecutionTier::kTurbofan) continue;

    // If we already compiled TurboFan code, we are also good.
    ExecutionTier reached_tier = ReachedTierField::decode(progress);
    if (reached_tier == ExecutionTier::kTurbofan) continue;

    // Set top tier to TurboFan and schedule a compilation unit.
    progress = RequiredTopTierField::update(progress, ExecutionTier::kTurbofan);
    builder.AddTopTierUnit(func_index, ExecutionTier::kTurbofan);
  }
  builder.Commit();
}

void CompilationStateImpl::InitializeCompilationProgress(
    int num_import_wrappers, int num_export_wrappers,
    ProfileInformation* pgo_info) {
  DCHECK(!failed());

  base::MutexGuard guard(&callbacks_mutex_);

  if (!v8_flags.wasm_jitless) {
    auto* module = native_module_->module();

    DCHECK_EQ(0, outstanding_baseline_units_);
    DCHECK(!has_outstanding_export_wrappers_);

    // Compute the default compilation progress for all functions, and set it.
    const ExecutionTierPair default_tiers = GetDefaultTiersPerModule(
        native_module_, dynamic_tiering_, native_module_->IsInDebugState(),
        IsLazyModule(module));
    const uint8_t default_progress =
        RequiredBaselineTierField::encode(default_tiers.baseline_tier) |
        RequiredTopTierField::encode(default_tiers.top_tier) |
        ReachedTierField::encode(ExecutionTier::kNone);
    compilation_progress_.assign(module->num_declared_functions,
                                 default_progress);
    if (default_tiers.baseline_tier != ExecutionTier::kNone) {
      outstanding_baseline_units_ += module->num_declared_functions;
    }

    // Apply compilation hints, if enabled.
    if (native_module_->enabled_features().has_compilation_hints()) {
      size_t num_hints = std::min(module->compilation_hints.size(),
                                  size_t{module->num_declared_functions});
      for (size_t hint_idx = 0; hint_idx < num_hints; ++hint_idx) {
        const auto& hint = module->compilation_hints[hint_idx];
        ApplyCompilationHintToInitialProgress(hint, hint_idx);
      }
    }

    // Transform --wasm-eager-tier-up-function, if given, into a fake
    // compilation hint.
    if (V8_UNLIKELY(
            v8_flags.wasm_eager_tier_up_function >= 0 &&
            static_cast<uint32_t>(v8_flags.wasm_eager_tier_up_function) >=
                module->num_imported_functions &&
            static_cast<uint32_t>(v8_flags.wasm_eager_tier_up_function) <
                module->functions.size())) {
      uint32_t func_idx =
          v8_flags.wasm_eager_tier_up_function - module->num_imported_functions;
      WasmCompilationHint hint{WasmCompilationHintStrategy::kEager,
                               WasmCompilationHintTier::kOptimized,
                               WasmCompilationHintTier::kOptimized};
      ApplyCompilationHintToInitialProgress(hint, func_idx);
    }
  }

  // Apply PGO information, if available.
  if (pgo_info) ApplyPgoInfoToInitialProgress(pgo_info);

  // Account for outstanding wrapper compilation.
  outstanding_baseline_units_ += num_import_wrappers;
  has_outstanding_export_wrappers_ = (num_export_wrappers > 0);

  // Trigger callbacks if module needs no baseline or top tier compilation. This
  // can be the case for an empty or fully lazy module.
  TriggerOutstandingCallbacks();
}

void CompilationStateImpl::AddCompilationUnitInternal(
    CompilationUnitBuilder* builder, int function_index,
    uint8_t function_progress) {
  ExecutionTier required_baseline_tier =
      CompilationStateImpl::RequiredBaselineTierField::decode(
          function_progress);
  ExecutionTier required_top_tier =
      CompilationStateImpl::RequiredTopTierField::decode(function_progress);
  ExecutionTier reached_tier =
      CompilationStateImpl::ReachedTierField::decode(function_progress);

  if (reached_tier < required_baseline_tier) {
    builder->AddBaselineUnit(function_index, required_baseline_tier);
  }
  if (reached_tier < required_top_tier &&
      required_baseline_tier != required_top_tier) {
    builder->AddTopTierUnit(function_index, required_top_tier);
  }
}

void CompilationStateImpl::InitializeCompilationUnits(
    std::unique_ptr<CompilationUnitBuilder> builder) {
  if (!v8_flags.wasm_jitless) {
    int offset = native_module_->module()->num_imported_functions;
    {
      base::MutexGuard guard(&callbacks_mutex_);

      for (size_t i = 0, e = compilation_progress_.size(); i < e; ++i) {
        uint8_t function_progress = compilation_progress_[i];
        int func_index = offset + static_cast<int>(i);
        AddCompilationUnitInternal(builder.get(), func_index,
                                   function_progress);
      }
    }
  }
  builder->Commit();
}

void CompilationStateImpl::AddCompilationUnit(CompilationUnitBuilder* builder,
                                              int func_index) {
  int offset = native_module_->module()->num_imported_functions;
  int progress_index = func_index - offset;
  uint8_t function_progress = 0;
  if (!v8_flags.wasm_jitless) {
    // TODO(ahaas): This lock may cause overhead. If so, we could get rid of the
    // lock as follows:
    // 1) Make compilation_progress_ an array of atomic<uint8_t>, and access it
    // lock-free.
    // 2) Have a copy of compilation_progress_ that we use for initialization.
    // 3) Just re-calculate the content of compilation_progress_.
    base::MutexGuard guard(&callbacks_mutex_);
    function_progress = compilation_progress_[progress_index];
  }
  AddCompilationUnitInternal(builder, func_index, function_progress);
}

void CompilationStateImpl::InitializeCompilationProgressAfterDeserialization(
    base::Vector<const int> lazy_functions,
    base::Vector<const int> eager_functions) {
  TRACE_EVENT2("v8.wasm", "wasm.CompilationAfterDeserialization",
               "num_lazy_functions", lazy_functions.size(),
               "num_eager_functions", eager_functions.size());
  std::optional<TimedHistogramScope> lazy_compile_time_scope;
  if (base::TimeTicks::IsHighResolution()) {
    lazy_compile_time_scope.emplace(
        counters()->wasm_compile_after_deserialize());
  }

  auto* module = native_module_->module();
  {
    base::MutexGuard guard(&callbacks_mutex_);
    DCHECK(compilation_progress_.empty());

    // Initialize the compilation progress as if everything was
    // TurboFan-compiled.
    constexpr uint8_t kProgressAfterTurbofanDeserialization =
        RequiredBaselineTierField::encode(ExecutionTier::kTurbofan) |
        RequiredTopTierField::encode(ExecutionTier::kTurbofan) |
        ReachedTierField::encode(ExecutionTier::kTurbofan);
    compilation_progress_.assign(module->num_declared_functions,
                                 kProgressAfterTurbofanDeserialization);

    // Update compilation state for lazy functions.
    constexpr uint8_t kProgressForLazyFunctions =
        RequiredBaselineTierField::encode(ExecutionTier::kNone) |
        RequiredTopTierField::encode(ExecutionTier::kNone) |
        ReachedTierField::encode(ExecutionTier::kNone);
    for (auto func_index : lazy_functions) {
      compilation_progress_[declared_function_index(module, func_index)] =
          kProgressForLazyFunctions;
    }

    // Update compilation state for eagerly compiled functions.
    constexpr bool kNotLazy = false;
    ExecutionTierPair default_tiers =
        GetDefaultTiersPerModule(native_module_, dynamic_tiering_,
                                 native_module_->IsInDebugState(), kNotLazy);
    uint8_t progress_for_eager_functions =
        RequiredBaselineTierField::encode(default_tiers.baseline_tier) |
        RequiredTopTierField::encode(default_tiers.top_tier) |
        ReachedTierField::encode(ExecutionTier::kNone);
    for (auto func_index : eager_functions) {
      // Check that {func_index} is not contained in {lazy_functions}.
      DCHECK_EQ(
          compilation_progress_[declared_function_index(module, func_index)],
          kProgressAfterTurbofanDeserialization);
      compilation_progress_[declared_function_index(module, func_index)] =
          progress_for_eager_functions;
    }
    DCHECK_NE(ExecutionTier::kNone, default_tiers.baseline_tier);
    outstanding_baseline_units_ += eager_functions.size();

    // Export wrappers are compiled synchronously after deserialization, so set
    // that as finished already. Baseline compilation is done if we do not have
    // any Liftoff functions to compile.
    finished_events_.Add(CompilationEvent::kFinishedExportWrappers);
    if (eager_functions.empty() || v8_flags.wasm_lazy_compilation) {
      finished_events_.Add(CompilationEvent::kFinishedBaselineCompilation);
    }
  }
  auto builder = std::make_unique<CompilationUnitBuilder>(native_module_);
  InitializeCompilationUnits(std::move(builder));
  if (!v8_flags.wasm_lazy_compilation) {
    WaitForCompilationEvent(CompilationEvent::kFinishedBaselineCompilation);
  }
}

void CompilationStateImpl::AddCallback(
    std::unique_ptr<CompilationEventCallback> callback) {
  base::MutexGuard callbacks_guard(&callbacks_mutex_);
  // Immediately trigger events that already happened.
  for (auto event : {CompilationEvent::kFinishedExportWrappers,
                     CompilationEvent::kFinishedBaselineCompilation,
                     CompilationEvent::kFailedCompilation}) {
    if (finished_events_.contains(event)) {
      callback->call(event);
    }
  }
  constexpr base::EnumSet<CompilationEvent> kFinalEvents{
      CompilationEvent::kFailedCompilation};
  if (!finished_events_.contains_any(kFinalEvents)) {
    callbacks_.emplace_back(std::move(callback));
  }
}

void CompilationStateImpl::CommitCompilationUnits(
    base::Vector<WasmCompilationUnit> baseline_units,
    base::Vector<WasmCompilationUnit> top_tier_units,
    base::Vector<JSToWasmWrapperCompilationUnit> js_to_wasm_wrapper_units) {
  base::MutexGuard guard{&mutex_};
  if (!js_to_wasm_wrapper_units.empty()) {
    // |js_to_wasm_wrapper_units_| will only be initialized once. This ensures
    // that pointers handed out by GetJSToWasmWrapperCompilationUnit stay valid.
    DCHECK_NULL(js_to_wasm_wrapper_job_);
    js_to_wasm_wrapper_units_.insert(
        js_to_wasm_wrapper_units_.end(),
        std::make_move_iterator(js_to_wasm_wrapper_units.begin()),
        std::make_move_iterator(js_to_wasm_wrapper_units.end()));
    js_to_wasm_wrapper_job_ = V8::GetCurrentPlatform()->PostJob(
        TaskPriority::kUserBlocking,
        std::make_unique<AsyncCompileJSToWasmWrapperJob>(
            native_module_weak_, js_to_wasm_wrapper_units_.size()));
  }
  if (!baseline_units.empty() || !top_tier_units.empty()) {
    compilation_unit_queues_.AddUnits(baseline_units, top_tier_units,
                                      native_module_->module());
  }
  if (!baseline_units.empty()) {
    DCHECK(baseline_compile_job_->IsValid());
    baseline_compile_job_->NotifyConcurrencyIncrease();
  }
  if (!top_tier_units.empty()) {
    DCHECK(top_tier_compile_job_->IsValid());
    top_tier_compile_job_->NotifyConcurrencyIncrease();
  }
}

void CompilationStateImpl::CommitTopTierCompilationUnit(
    WasmCompilationUnit unit) {
  CommitCompilationUnits({}, {&unit, 1}, {});
}

void CompilationStateImpl::AddTopTierPriorityCompilationUnit(
    WasmCompilationUnit unit, size_t priority) {
  compilation_unit_queues_.AddTopTierPriorityUnit(unit, priority);
  // We should not have a {CodeSpaceWriteScope} open at this point, as
  // {NotifyConcurrencyIncrease} can spawn new threads which could inherit PKU
  // permissions (which would be a security issue).
  top_tier_compile_job_->NotifyConcurrencyIncrease();
}

JSToWasmWrapperCompilationUnit*
CompilationStateImpl::GetJSToWasmWrapperCompilationUnit(size_t index) {
  DCHECK_LT(index, js_to_wasm_wrapper_units_.size());
  return js_to_wasm_wrapper_units_.data() + index;
}

void CompilationStateImpl::FinalizeJSToWasmWrappers(Isolate* isolate,
                                                    const WasmModule* module) {
  TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.FinalizeJSToWasmWrappers", "wrappers",
               js_to_wasm_wrapper_units_.size());

  isolate->heap()->EnsureWasmCanonicalRttsSize(module->MaxCanonicalTypeIndex() +
                                               1);
  for (auto& unit : js_to_wasm_wrapper_units_) {
    DCHECK_EQ(isolate, unit.isolate());
    DirectHandle<Code> code = unit.Finalize();
    // Each JSToWasmWrapperCompilationUnit compiles an actual wrappers and never
    // returns the generic builtin.
    DCHECK(!code->is_builtin());
    uint32_t index = unit.canonical_sig_index();
    isolate->heap()->js_to_wasm_wrappers()->Set(index, code->wrapper());
    RecordStats(*code, isolate->counters());
    isolate->counters()->wasm_compiled_export_wrapper()->Increment(1);
  }
  // Clearing needs to hold the mutex to avoid racing with
  // {EstimateCurrentMemoryConsumption}.
  base::MutexGuard guard{&mutex_};
  js_to_wasm_wrapper_units_.clear();
}

CompilationUnitQueues::Queue* CompilationStateImpl::GetQueueForCompileTask(
    int task_id) {
  return compilation_unit_queues_.GetQueueForTask(task_id);
}

std::optional<WasmCompilationUnit> CompilationStateImpl::GetNextCompilationUnit(
    CompilationUnitQueues::Queue* queue, CompilationTier tier) {
  return compilation_unit_queues_.GetNextUnit(queue, tier);
}

void CompilationStateImpl::OnFinishedUnits(
    base::Vector<WasmCode*> code_vector) {
  TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("v8.wasm.detailed"),
               "wasm.OnFinishedUnits", "units", code_vector.size());

  base::MutexGuard guard(&callbacks_mutex_);

  // Assume an order of execution tiers that represents the quality of their
  // generated code.
  static_assert(ExecutionTier::kNone < ExecutionTier::kLiftoff &&
                    ExecutionTier::kLiftoff < ExecutionTier::kTurbofan,
                "Assume an order on execution tiers");

  if (!v8_flags.wasm_jitless) {
    DCHECK_EQ(compilation_progress_.size(),
              native_module_->module()->num_declared_functions);
  }

  bool has_top_tier_code = false;

  for (size_t i = 0; i < code_vector.size(); i++) {
    WasmCode* code = code_vector[i];
    DCHECK_NOT_NULL(code);
    DCHECK_LT(code->index(), native_module_->num_functions());

    has_top_tier_code |= code->tier() == ExecutionTier::kTurbofan;

    if (code->index() <
        static_cast<int>(native_module_->num_imported_functions())) {
      // Import wrapper.
      DCHECK_EQ(code->tier(), ExecutionTier::kTurbofan);
      outstanding_baseline_units_--;
    } else {
      // Function.
      DCHECK_NE(code->tier(), ExecutionTier::kNone);

      // Read function's compilation progress.
      // This view on the compilation progress may differ from the actually
      // compiled code. Any lazily compiled function does not contribute to the
      // compilation progress but may publish code to the code manager.
      int slot_index =
          declared_function_index(native_module_->module(), code->index());
      uint8_t function_progress = compilation_progress_[slot_index];
      ExecutionTier required_baseline_tier =
          RequiredBaselineTierField::decode(function_progress);
      ExecutionTier reached_tier = ReachedTierField::decode(function_progress);

      // Check whether required baseline or top tier are reached.
      if (reached_tier < required_baseline_tier &&
          required_baseline_tier <= code->tier()) {
        DCHECK_GT(outstanding_baseline_units_, 0);
        outstanding_baseline_units_--;
      }
      if (code->tier() == ExecutionTier::kTurbofan) {
        bytes_since_last_chunk_ += code->instructions().size();
      }

      // Update function's compilation progress.
      if (code->tier() > reached_tier) {
        compilation_progress_[slot_index] = ReachedTierField::update(
            compilation_progress_[slot_index], code->tier());
      }
      // Allow another top tier compilation if deopts are enabled and the
      // currently installed code object is a liftoff object.
      // Ideally, this would be done only if the code->tier() ==
      // ExeuctionTier::Liftoff as the code object for which we run this
      // function should be the same as the one installed on the native_module.
      // This is unfortunately not the case as installing a code object on the
      // native module and updating the compilation_progress_ and the
      // CompilationUnitQueues::top_tier_compiled_ are not synchronized.
      // Note: GetCode() acquires the NativeModule::allocation_mutex_, so this
      // could cause deadlocks if any other place acquires
      // NativeModule::allocation_mutex_ first and then
      // CompilationStateImpl::callbacks_mutex_!
      const bool is_liftoff = code->tier() == ExecutionTier::kLiftoff;
      auto published_code_is_liftoff = [this](int index) {
        WasmCode* code = native_module_->GetCode(index);
        if (code == nullptr) return false;
        return code->is_liftoff();
      };
      if (v8_flags.wasm_deopt &&
          (is_liftoff || published_code_is_liftoff(code->index()))) {
        compilation_progress_[slot_index] = ReachedTierField::update(
            compilation_progress_[slot_index], ExecutionTier::kLiftoff);
        compilation_unit_queues_.AllowAnotherTopTierJob(code->index());
      }
      DCHECK_LE(0, outstanding_baseline_units_);
    }
  }

  // Update the {last_top_tier_compilation_timestamp_} if it is set (i.e. a
  // delayed task has already been spawned).
  if (has_top_tier_code && !last_top_tier_compilation_timestamp_.IsNull()) {
    last_top_tier_compilation_timestamp_ = base::TimeTicks::Now();
  }

  TriggerOutstandingCallbacks();
}

void CompilationStateImpl::OnFinishedJSToWasmWrapperUnits() {
  base::MutexGuard guard(&callbacks_mutex_);
  has_outstanding_export_wrappers_ = false;
  TriggerOutstandingCallbacks();
}

namespace {
class TriggerCodeCachingAfterTimeoutTask : public v8::Task {
 public:
  explicit TriggerCodeCachingAfterTimeoutTask(
      std::weak_ptr<NativeModule> native_module)
      : native_module_(std::move(native_module)) {}

  void Run() override {
    if (std::shared_ptr<NativeModule> native_module = native_module_.lock()) {
      Impl(native_module->compilation_state())->TriggerCachingAfterTimeout();
    }
  }

 private:
  const std::weak_ptr<NativeModule> native_module_;
};
}  // namespace

void CompilationStateImpl::TriggerOutstandingCallbacks() {
  DCHECK(!callbacks_mutex_.TryLock());

  base::EnumSet<CompilationEvent> triggered_events;
  if (!has_outstanding_export_wrappers_) {
    triggered_events.Add(CompilationEvent::kFinishedExportWrappers);
    if (outstanding_baseline_units_ == 0) {
      triggered_events.Add(CompilationEvent::kFinishedBaselineCompilation);
    }
  }

  // For dynamic tiering, trigger "compilation chunk finished" after a new chunk
  // of size {v8_flags.wasm_caching_threshold}.
  if (dynamic_tiering_ &&
      static_cast<size_t>(v8_flags.wasm_caching_threshold) <=
          bytes_since_last_chunk_) {
    // Trigger caching immediately if there is no timeout or the hard threshold
    // was reached.
    if (v8_flags.wasm_caching_timeout_ms <= 0 ||
        static_cast<size_t>(v8_flags.wasm_caching_hard_threshold) <=
            bytes_since_last_chunk_) {
      triggered_events.Add(CompilationEvent::kFinishedCompilationChunk);
      bytes_since_last_chunk_ = 0;
    } else if (last_top_tier_compilation_timestamp_.IsNull()) {
      // Trigger a task after the given timeout; that task will only trigger
      // caching if no new code was added until then. Otherwise, it will
      // re-schedule itself.
      V8::GetCurrentPlatform()->CallDelayedOnWorkerThread(
          std::make_unique<TriggerCodeCachingAfterTimeoutTask>(
              native_module_weak_),
          1e-3 * v8_flags.wasm_caching_timeout_ms);

      // Set the timestamp (will be updated by {OnFinishedUnits} if more
      // top-tier compilation finished before the delayed task is being run).
      last_top_tier_compilation_timestamp_ = base::TimeTicks::Now();
    }
  }

  if (compile_failed_.load(std::memory_order_relaxed)) {
    // *Only* trigger the "failed" event.
    triggered_events =
        base::EnumSet<CompilationEvent>({CompilationEvent::kFailedCompilation});
  }

  TriggerCallbacks(triggered_events);
}

void CompilationStateImpl::TriggerCallbacks(
    base::EnumSet<CompilationEvent> events) {
  if (events.empty()) return;

  // Don't trigger past events again.
  events -= finished_events_;
  // There can be multiple compilation chunks, thus do not store this.
  finished_events_ |= events - CompilationEvent::kFinishedCompilationChunk;

  for (auto event :
       {std::make_pair(CompilationEvent::kFailedCompilation,
                       "wasm.CompilationFailed"),
        std::make_pair(CompilationEvent::kFinishedExportWrappers,
                       "wasm.ExportWrappersFinished"),
        std::make_pair(CompilationEvent::kFinishedBaselineCompilation,
                       "wasm.BaselineFinished"),
        std::make_pair(CompilationEvent::kFinishedCompilationChunk,
                       "wasm.CompilationChunkFinished")}) {
    if (!events.contains(event.first)) continue;
    DCHECK_NE(compilation_id_, kInvalidCompilationID);
    TRACE_EVENT1("v8.wasm", event.second, "id", compilation_id_);
    for (auto& callback : callbacks_) {
      callback->call(event.first);
    }
  }

  if (outstanding_baseline_units_ == 0 && !has_outstanding_export_wrappers_) {
    auto new_end = std::remove_if(
        callbacks_.begin(), callbacks_.end(), [](const auto& callback) {
          return callback->release_after_final_event();
        });
    callbacks_.erase(new_end, callbacks_.end());
  }
}

void CompilationStateImpl::TriggerCachingAfterTimeout() {
  base::MutexGuard guard{&callbacks_mutex_};

  // It can happen that we reached the hard threshold while waiting for the
  // timeout to expire. In that case, {bytes_since_last_chunk_} might be zero
  // and there is nothing new to cache.
  if (bytes_since_last_chunk_ == 0) return;

  DCHECK(!last_top_tier_compilation_timestamp_.IsNull());
  base::TimeTicks caching_time =
      last_top_tier_compilation_timestamp_ +
      base::TimeDelta::FromMilliseconds(v8_flags.wasm_caching_timeout_ms);
  base::TimeDelta time_until_caching = caching_time - base::TimeTicks::Now();
  // If we are still half a millisecond or more away from the timeout,
  // reschedule the task. Otherwise, call the caching callback.
  if (time_until_caching >= base::TimeDelta::FromMicroseconds(500)) {
    int ms_remaining =
        static_cast<int>(time_until_caching.InMillisecondsRoundedUp());
    DCHECK_LE(1, ms_remaining);
    V8::GetCurrentPlatform()->CallDelayedOnWorkerThread(
        std::make_unique<TriggerCodeCachingAfterTimeoutTask>(
            native_module_weak_),
        ms_remaining);
    return;
  }

  TriggerCallbacks({CompilationEvent::kFinishedCompilationChunk});
  last_top_tier_compilation_timestamp_ = {};
  bytes_since_last_chunk_ = 0;
}

void CompilationStateImpl::OnCompilationStopped(WasmDetectedFeatures detected) {
  base::MutexGuard guard(&mutex_);
  detected_features_.Add(detected);
}

void CompilationStateImpl::PublishDetectedFeaturesAfterCompilation(
    Isolate* isolate) {
  // Notifying the isolate of the feature counts must take place under
  // the mutex, because even if we have finished baseline compilation,
  // tiering compilations may still occur in the background.
  base::MutexGuard guard(&mutex_);

  using Feature = v8::Isolate::UseCounterFeature;
  static constexpr std::pair<WasmDetectedFeature, Feature> kUseCounters[] = {
      {WasmDetectedFeature::reftypes, Feature::kWasmRefTypes},
      {WasmDetectedFeature::simd, Feature::kWasmSimdOpcodes},
      {WasmDetectedFeature::threads, Feature::kWasmThreadOpcodes},
      {WasmDetectedFeature::legacy_eh, Feature::kWasmExceptionHandling},
      {WasmDetectedFeature::memory64, Feature::kWasmMemory64},
      {WasmDetectedFeature::multi_memory, Feature::kWasmMultiMemory},
      {WasmDetectedFeature::gc, Feature::kWasmGC},
      {WasmDetectedFeature::imported_strings, Feature::kWasmImportedStrings},
      {WasmDetectedFeature::return_call, Feature::kWasmReturnCall},
      {WasmDetectedFeature::extended_const, Feature::kWasmExtendedConst},
      {WasmDetectedFeature::relaxed_simd, Feature::kWasmRelaxedSimd},
      {WasmDetectedFeature::type_reflection, Feature::kWasmTypeReflection},
      {WasmDetectedFeature::exnref, Feature::kWasmExnRef},
      {WasmDetectedFeature::typed_funcref, Feature::kWasmTypedFuncRef},
      {WasmDetectedFeature::jspi, Feature::kWasmJavaScriptPromiseIntegration},
  };

  // Check that every staging or shipping feature has a use counter as that is
  // the main point of tracking used features.
  auto check_use_counter = [](WasmDetectedFeature feat) constexpr -> bool {
    // Some features intentionally do not have a use counter.
    constexpr WasmDetectedFeature kIntentionallyNoUseCounter[] = {
        WasmDetectedFeature::stringref,    // Deprecated / unlikely to ship.
        WasmDetectedFeature::inlining,     // Not a user-visible feature.
        WasmDetectedFeature::js_inlining,  // Not a user-visible feature.
    };
    for (auto no_use_counter_feature : kIntentionallyNoUseCounter) {
      if (feat == no_use_counter_feature) return true;
    }
    for (auto [feature, use_counter] : kUseCounters) {
      if (feat == feature) return true;
    }
    return false;
  };
#define CHECK_USE_COUNTER(feat, ...) \
  static_assert(check_use_counter(WasmDetectedFeature::feat));
  FOREACH_WASM_STAGING_FEATURE_FLAG(CHECK_USE_COUNTER)
  FOREACH_WASM_SHIPPED_FEATURE_FLAG(CHECK_USE_COUNTER)
  FOREACH_WASM_NON_FLAG_FEATURE(CHECK_USE_COUNTER)
#undef CHECK_USE_COUNTER

  static constexpr size_t kMaxFeatures = arraysize(kUseCounters) + 1;
  base::SmallVector<Feature, kMaxFeatures> use_counter_features;
  // Always set the WasmModuleCompilation feature as a baseline for the other
  // features. Note that we also track instantiation, but the number of
  // compilations and instantiations are pretty unrelated.
  use_counter_features.push_back(Feature::kWasmModuleCompilation);

  for (auto [wasm_feature, feature] : kUseCounters) {
    if (!detected_features_.contains(wasm_feature)) continue;
    use_counter_features.push_back(feature);
  }
  isolate->CountUsage(base::VectorOf(use_counter_features));
}

void CompilationStateImpl::PublishCompilationResults(
    std::vector<std::unique_ptr<WasmCode>> unpublished_code) {
  if (unpublished_code.empty()) return;

#if DEBUG
  // We don't compile import wrappers eagerly.
  for (const auto& code : unpublished_code) {
    int func_index = code->index();
    DCHECK_LE(native_module_->num_imported_functions(), func_index);
    DCHECK_LT(func_index, native_module_->num_functions());
  }
#endif
  PublishCode(base::VectorOf(unpublished_code));
}

std::vector<WasmCode*> CompilationStateImpl::PublishCode(
    base::Vector<std::unique_ptr<WasmCode>> code) {
  WasmCodeRefScope code_ref_scope;
  std::vector<WasmCode*> published_code =
      native_module_->PublishCode(std::move(code));
  // Defer logging code in case wire bytes were not fully received yet.
  if (native_module_->log_code() && native_module_->HasWireBytes()) {
    GetWasmEngine()->LogCode(base::VectorOf(published_code));
  }

  OnFinishedUnits(base::VectorOf(published_code));
  return published_code;
}

void CompilationStateImpl::SchedulePublishCompilationResults(
    std::vector<std::unique_ptr<WasmCode>> unpublished_code,
    CompilationTier tier) {
  PublishState& state = publish_state_[tier];
  {
    base::MutexGuard guard(&state.mutex_);
    if (state.publisher_running_) {
      // Add new code to the queue and return.
      state.publish_queue_.reserve(state.publish_queue_.size() +
                                   unpublished_code.size());
      for (auto& c : unpublished_code) {
        state.publish_queue_.emplace_back(std::move(c));
      }
      return;
    }
    state.publisher_running_ = true;
  }
  while (true) {
    PublishCompilationResults(std::move(unpublished_code));
    unpublished_code.clear();

    // Keep publishing new code that came in.
    base::MutexGuard guard(&state.mutex_);
    DCHECK(state.publisher_running_);
    if (state.publish_queue_.empty()) {
      state.publisher_running_ = false;
      return;
    }
    unpublished_code.swap(state.publish_queue_);
  }
}

size_t CompilationStateImpl::NumOutstandingCompilations(
    CompilationTier tier) const {
  return compilation_unit_queues_.GetSizeForTier(tier);
}

void CompilationStateImpl::SetError() {
  compile_cancelled_.store(true, std::memory_order_relaxed);
  if (compile_failed_.exchange(true, std::memory_order_relaxed)) {
    return;  // Already failed before.
  }

  base::MutexGuard callbacks_guard(&callbacks_mutex_);
  TriggerOutstandingCallbacks();
  callbacks_.clear();
}

void CompilationStateImpl::WaitForCompilationEvent(
    CompilationEvent expect_event) {
  switch (expect_event) {
    case CompilationEvent::kFinishedExportWrappers:
      break;
    case CompilationEvent::kFinishedBaselineCompilation:
      if (baseline_compile_job_->IsValid()) baseline_compile_job_->Join();
      break;
    default:
      // Waiting on other CompilationEvent doesn't make sense.
      UNREACHABLE();
  }
  if (js_to_wasm_wrapper_job_ && js_to_wasm_wrapper_job_->IsValid()) {
    js_to_wasm_wrapper_job_->Join();
  }
#ifdef DEBUG
  base::EnumSet<CompilationEvent> events{expect_event,
                                         CompilationEvent::kFailedCompilation};
  base::MutexGuard guard(&callbacks_mutex_);
  DCHECK(finished_events_.contains_any(events));
#endif
}

void CompilationStateImpl::TierUpAllFunctions() {
  const WasmModule* module = native_module_->module();
  uint32_t num_wasm_functions = module->num_declared_functions;
  WasmCodeRefScope code_ref_scope;
  CompilationUnitBuilder builder(native_module_);
  for (uint32_t i = 0; i < num_wasm_functions; ++i) {
    int func_index = module->num_imported_functions + i;
    WasmCode* code = native_module_->GetCode(func_index);
    if (!code || !code->is_turbofan()) {
      builder.AddTopTierUnit(func_index, ExecutionTier::kTurbofan);
    }
  }
  builder.Commit();

  // Join the compilation, until no compilation units are left anymore.
  class DummyDelegate final : public JobDelegate {
    bool ShouldYield() override { return false; }
    bool IsJoiningThread() const override { return true; }
    void NotifyConcurrencyIncrease() override { UNIMPLEMENTED(); }
    uint8_t GetTaskId() override { return kMainTaskId; }
  };

  DummyDelegate delegate;
  ExecuteCompilationUnits(native_module_weak_, async_counters_.get(), &delegate,
                          CompilationTier::kTopTier);

  // We cannot wait for other compilation threads to finish, so we explicitly
  // compile all functions which are not yet available as TurboFan code.
  for (uint32_t i = 0; i < num_wasm_functions; ++i) {
    uint32_t func_index = module->num_imported_functions + i;
    WasmCode* code = native_module_->GetCode(func_index);
    if (!code || !code->is_turbofan()) {
      wasm::GetWasmEngine()->CompileFunction(async_counters_.get(),
                                             native_module_, func_index,
                                             wasm::ExecutionTier::kTurbofan);
    }
  }
}

namespace {
using JSToWasmWrapperSet = std::unordered_set<uint32_t>;
using JSToWasmWrapperUnitVector = std::vector<
    std::pair<uint32_t, std::unique_ptr<JSToWasmWrapperCompilationUnit>>>;

class CompileJSToWasmWrapperJob final : public BaseCompileJSToWasmWrapperJob {
 public:
  explicit CompileJSToWasmWrapperJob(
      JSToWasmWrapperUnitVector* compilation_units)
      : BaseCompileJSToWasmWrapperJob(compilation_units->size()),
        compilation_units_(compilation_units) {}

  void Run(JobDelegate* delegate) override {
    size_t index;
    while (GetNextUnitIndex(&index)) {
      JSToWasmWrapperCompilationUnit* unit =
          (*compilation_units_)[index].second.get();
      unit->Execute();
      CompleteUnit();
      if (delegate && delegate->ShouldYield()) return;
    }
  }

 private:
  JSToWasmWrapperUnitVector* const compilation_units_;
};
}  // namespace

void CompileJsToWasmWrappers(Isolate* isolate, const WasmModule* module) {
  TRACE_EVENT0("v8.wasm", "wasm.CompileJsToWasmWrappers");

  isolate->heap()->EnsureWasmCanonicalRttsSize(module->MaxCanonicalTypeIndex() +
                                               1);

  JSToWasmWrapperSet set;
  JSToWasmWrapperUnitVector compilation_units;
  WasmEnabledFeatures enabled_features =
      WasmEnabledFeatures::FromIsolate(isolate);

  // Prepare compilation units in the main thread.
  for (auto exp : module->export_table) {
    if (exp.kind != kExternalFunction) continue;

    auto& function = module->functions[exp.index];
    bool use_generic_wrapper =
        !function.imported &&
        CanUseGenericJsToWasmWrapper(module, function.sig);
    if (use_generic_wrapper) continue;  // Nothing to compile.
    uint32_t canonical_type_index =
        module->isorecursive_canonical_type_ids[function.sig_index];
    int wrapper_index = canonical_type_index;
    Tagged<MaybeObject> existing_wrapper =
        isolate->heap()->js_to_wasm_wrappers()->Get(wrapper_index);
    if (existing_wrapper.IsStrongOrWeak() &&
        !IsUndefined(existing_wrapper.GetHeapObject())) {
      DCHECK(IsCodeWrapper(existing_wrapper.GetHeapObject()));
      continue;
    }

    const auto [it, inserted] = set.insert(canonical_type_index);
    if (!inserted) continue;  // Compilation already triggered.
    auto unit = std::make_unique<JSToWasmWrapperCompilationUnit>(
        isolate, function.sig, canonical_type_index, module, enabled_features);
    compilation_units.emplace_back(canonical_type_index, std::move(unit));
  }

  if (compilation_units.empty()) return;

  {
    // This is nested inside the event above, so the name can be less
    // descriptive. It's mainly to log the number of wrappers.
    TRACE_EVENT1("v8.wasm", "wasm.JsToWasmWrapperCompilation", "num_wrappers",
                 compilation_units.size());
    auto job = std::make_unique<CompileJSToWasmWrapperJob>(&compilation_units);
    if (V8_LIKELY(v8_flags.wasm_num_compilation_tasks > 0)) {
      auto job_handle = V8::GetCurrentPlatform()->CreateJob(
          TaskPriority::kUserVisible, std::move(job));

      // Wait for completion, while contributing to the work.
      job_handle->Join();
    } else {
      job->Run(nullptr);
    }
  }

  // Finalize compilation jobs on the main thread.
  for (auto& pair : compilation_units) {
    uint32_t key = pair.first;
    JSToWasmWrapperCompilationUnit* unit = pair.second.get();
    DCHECK_EQ(isolate, unit->isolate());
    DirectHandle<Code> code = unit->Finalize();
    DCHECK(!code->is_builtin() || v8_flags.wasm_jitless);
    if (v8_flags.wasm_jitless) continue;
    isolate->heap()->js_to_wasm_wrappers()->Set(key, code->wrapper());
    // Do not increase code stats for non-jitted wrappers.
    RecordStats(*code, isolate->counters());
    isolate->counters()->wasm_compiled_export_wrapper()->Increment(1);
  }
}

WasmCode* CompileImportWrapper(
    NativeModule* native_module, Counters* counters, ImportCallKind kind,
    const FunctionSig* sig, uint32_t canonical_type_index, int expected_arity,
    Suspend suspend, WasmImportWrapperCache::ModificationScope* cache_scope) {
  // Entry should exist, so that we don't insert a new one and invalidate
  // other threads' iterators/references, but it should not have been compiled
  // yet.
  WasmImportWrapperCache::CacheKey key(kind, canonical_type_index,
                                       expected_arity, suspend);
  DCHECK_NULL((*cache_scope)[key]);
  bool source_positions = is_asmjs_module(native_module->module());
  // Keep the {WasmCode} alive until we explicitly call {IncRef}.
  WasmCode* published_code = nullptr;
  if (v8_flags.wasm_jitless) {
    DCHECK_NULL((*cache_scope)[key]);
  } else {
    WasmCodeRefScope code_ref_scope;
    CompilationEnv env = CompilationEnv::ForModule(native_module);
    WasmCompilationResult result = compiler::CompileWasmImportCallWrapper(
        &env, kind, sig, source_positions, expected_arity, suspend);

    DCHECK(result.inlining_positions.empty());
    DCHECK(result.deopt_data.empty());

    std::unique_ptr<WasmCode> wasm_code = native_module->AddCode(
        result.func_index, result.code_desc, result.frame_slot_count,
        result.ool_spill_count, result.tagged_parameter_slots,
        result.protected_instructions_data.as_vector(),
        result.source_positions.as_vector(),
        result.inlining_positions.as_vector(), result.deopt_data.as_vector(),
        GetCodeKind(result), ExecutionTier::kNone, kNotForDebugging);
    published_code = native_module->PublishCode(std::move(wasm_code));
    (*cache_scope)[key] = published_code;
    published_code->IncRef();
    counters->wasm_generated_code_size()->Increment(
        published_code->instructions().length());
    counters->wasm_reloc_size()->Increment(
        published_code->reloc_info().length());
  }
  return published_code;
}

}  // namespace v8::internal::wasm

#undef TRACE_COMPILE
#undef TRACE_STREAMING
#undef TRACE_LAZY

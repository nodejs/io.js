// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-measurement.h"

#include "include/v8-local-handle.h"
#include "src/api/api-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/handles/global-handles-inl.h"
#include "src/heap/factory-inl.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/marking-worklist.h"
#include "src/logging/counters.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-promise-inl.h"
#include "src/objects/smi.h"
#include "src/tasks/task-utils.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"
#endif

namespace v8 {
namespace internal {

namespace {
class MemoryMeasurementResultBuilder {
 public:
  MemoryMeasurementResultBuilder(Isolate* isolate, Factory* factory)
      : isolate_(isolate), factory_(factory) {
    result_ = NewJSObject();
  }
  void AddTotal(size_t estimate, size_t lower_bound, size_t upper_bound) {
    AddProperty(result_, factory_->total_string(),
                NewResult(estimate, lower_bound, upper_bound));
  }
  void AddCurrent(size_t estimate, size_t lower_bound, size_t upper_bound) {
    detailed_ = true;
    AddProperty(result_, factory_->current_string(),
                NewResult(estimate, lower_bound, upper_bound));
  }
  void AddOther(size_t estimate, size_t lower_bound, size_t upper_bound) {
    detailed_ = true;
    other_.push_back(NewResult(estimate, lower_bound, upper_bound));
  }
  void AddWasm(size_t code, size_t metadata) {
    Handle<JSObject> wasm = NewJSObject();
    AddProperty(wasm, factory_->NewStringFromAsciiChecked("code"),
                NewNumber(code));
    AddProperty(wasm, factory_->NewStringFromAsciiChecked("metadata"),
                NewNumber(metadata));
    AddProperty(result_, factory_->NewStringFromAsciiChecked("WebAssembly"),
                wasm);
  }
  Handle<JSObject> Build() {
    if (detailed_) {
      int length = static_cast<int>(other_.size());
      Handle<FixedArray> other = factory_->NewFixedArray(length);
      for (int i = 0; i < length; i++) {
        other->set(i, *other_[i]);
      }
      AddProperty(result_, factory_->other_string(),
                  factory_->NewJSArrayWithElements(other));
    }
    return result_;
  }

 private:
  Handle<JSObject> NewResult(size_t estimate, size_t lower_bound,
                             size_t upper_bound) {
    Handle<JSObject> result = NewJSObject();
    Handle<Object> estimate_obj = NewNumber(estimate);
    AddProperty(result, factory_->jsMemoryEstimate_string(), estimate_obj);
    Handle<Object> range = NewRange(lower_bound, upper_bound);
    AddProperty(result, factory_->jsMemoryRange_string(), range);
    return result;
  }
  Handle<Object> NewNumber(size_t value) {
    return factory_->NewNumberFromSize(value);
  }
  Handle<JSObject> NewJSObject() {
    return factory_->NewJSObject(isolate_->object_function());
  }
  Handle<JSArray> NewRange(size_t lower_bound, size_t upper_bound) {
    Handle<Object> lower = NewNumber(lower_bound);
    Handle<Object> upper = NewNumber(upper_bound);
    Handle<FixedArray> elements = factory_->NewFixedArray(2);
    elements->set(0, *lower);
    elements->set(1, *upper);
    return factory_->NewJSArrayWithElements(elements);
  }
  void AddProperty(Handle<JSObject> object, Handle<String> name,
                   Handle<Object> value) {
    JSObject::AddProperty(isolate_, object, name, value, NONE);
  }
  Isolate* isolate_;
  Factory* factory_;
  Handle<JSObject> result_;
  std::vector<Handle<JSObject>> other_;
  bool detailed_ = false;
};
}  // anonymous namespace

class V8_EXPORT_PRIVATE MeasureMemoryDelegate
    : public v8::MeasureMemoryDelegate {
 public:
  MeasureMemoryDelegate(Isolate* isolate, Handle<NativeContext> context,
                        Handle<JSPromise> promise, v8::MeasureMemoryMode mode);
  ~MeasureMemoryDelegate() override;

  // v8::MeasureMemoryDelegate overrides:
  bool ShouldMeasure(v8::Local<v8::Context> context) override;
  void MeasurementComplete(Result result) override;

 private:
  Isolate* isolate_;
  Handle<JSPromise> promise_;
  Handle<NativeContext> context_;
  v8::MeasureMemoryMode mode_;
};

MeasureMemoryDelegate::MeasureMemoryDelegate(Isolate* isolate,
                                             Handle<NativeContext> context,
                                             Handle<JSPromise> promise,
                                             v8::MeasureMemoryMode mode)
    : isolate_(isolate), mode_(mode) {
  context_ = isolate->global_handles()->Create(*context);
  promise_ = isolate->global_handles()->Create(*promise);
}

MeasureMemoryDelegate::~MeasureMemoryDelegate() {
  isolate_->global_handles()->Destroy(promise_.location());
  isolate_->global_handles()->Destroy(context_.location());
}

bool MeasureMemoryDelegate::ShouldMeasure(v8::Local<v8::Context> context) {
  Handle<NativeContext> native_context =
      Handle<NativeContext>::cast(Utils::OpenHandle(*context));
  return context_->security_token() == native_context->security_token();
}

void MeasureMemoryDelegate::MeasurementComplete(Result result) {
  size_t shared_size = result.unattributed_size_in_bytes;
  size_t wasm_code = result.wasm_code_size_in_bytes;
  size_t wasm_metadata = result.wasm_metadata_size_in_bytes;
  v8::Local<v8::Context> v8_context =
      Utils::Convert<HeapObject, v8::Context>(context_);
  v8::Context::Scope scope(v8_context);
  size_t total_size = 0;
  size_t current_size = 0;
  DCHECK_EQ(result.contexts.size(), result.sizes_in_bytes.size());
  for (size_t i = 0; i < result.contexts.size(); ++i) {
    total_size += result.sizes_in_bytes[i];
    if (*Utils::OpenDirectHandle(*result.contexts[i]) == *context_) {
      current_size = result.sizes_in_bytes[i];
    }
  }
  MemoryMeasurementResultBuilder result_builder(isolate_, isolate_->factory());
  result_builder.AddTotal(total_size, total_size, total_size + shared_size);
  if (wasm_code > 0 || wasm_metadata > 0) {
    result_builder.AddWasm(wasm_code, wasm_metadata);
  }

  if (mode_ == v8::MeasureMemoryMode::kDetailed) {
    result_builder.AddCurrent(current_size, current_size,
                              current_size + shared_size);
    for (size_t i = 0; i < result.contexts.size(); ++i) {
      if (*Utils::OpenDirectHandle(*result.contexts[i]) != *context_) {
        size_t other_size = result.sizes_in_bytes[i];
        result_builder.AddOther(other_size, other_size,
                                other_size + shared_size);
      }
    }
  }

  Handle<JSObject> jsresult = result_builder.Build();
  if (JSPromise::Resolve(promise_, jsresult).is_null()) {
    CHECK(isolate_->is_execution_terminating());
  }
}

MemoryMeasurement::MemoryMeasurement(Isolate* isolate)
    : isolate_(isolate),
      task_runner_(isolate->heap()->GetForegroundTaskRunner()),
      random_number_generator_() {
  if (v8_flags.random_seed) {
    random_number_generator_.SetSeed(v8_flags.random_seed);
  }
}

bool MemoryMeasurement::EnqueueRequest(
    std::unique_ptr<v8::MeasureMemoryDelegate> delegate,
    v8::MeasureMemoryExecution execution,
    const std::vector<Handle<NativeContext>> contexts) {
  int length = static_cast<int>(contexts.size());
  Handle<WeakFixedArray> weak_contexts =
      isolate_->factory()->NewWeakFixedArray(length);
  for (int i = 0; i < length; ++i) {
    weak_contexts->set(i, MakeWeak(*contexts[i]));
  }
  Handle<WeakFixedArray> global_weak_contexts =
      isolate_->global_handles()->Create(*weak_contexts);
  Request request = {std::move(delegate),          // delegate
                     global_weak_contexts,         // contexts
                     std::vector<size_t>(length),  // sizes
                     0u,                           // shared
                     0u,                           // wasm_code
                     0u,                           // wasm_metadata
                     {}};                          // timer
  request.timer.Start();
  received_.push_back(std::move(request));
  ScheduleGCTask(execution);
  return true;
}

std::vector<Address> MemoryMeasurement::StartProcessing() {
  if (received_.empty()) return {};
  std::unordered_set<Address> unique_contexts;
  DCHECK(processing_.empty());
  processing_ = std::move(received_);
  for (const auto& request : processing_) {
    Handle<WeakFixedArray> contexts = request.contexts;
    for (int i = 0; i < contexts->length(); i++) {
      Tagged<HeapObject> context;
      if (contexts->get(i).GetHeapObject(&context)) {
        unique_contexts.insert(context.ptr());
      }
    }
  }
  return std::vector<Address>(unique_contexts.begin(), unique_contexts.end());
}

void MemoryMeasurement::FinishProcessing(const NativeContextStats& stats) {
  if (processing_.empty()) return;

  size_t shared = stats.Get(MarkingWorklists::kSharedContext);
#if V8_ENABLE_WEBASSEMBLY
  size_t wasm_code = wasm::GetWasmCodeManager()->committed_code_space();
  size_t wasm_metadata =
      wasm::GetWasmEngine()->EstimateCurrentMemoryConsumption();
#endif

  while (!processing_.empty()) {
    Request request = std::move(processing_.front());
    processing_.pop_front();
    for (int i = 0; i < static_cast<int>(request.sizes.size()); i++) {
      Tagged<HeapObject> context;
      if (!request.contexts->get(i).GetHeapObject(&context)) {
        continue;
      }
      request.sizes[i] = stats.Get(context.ptr());
    }
    request.shared = shared;
#if V8_ENABLE_WEBASSEMBLY
    request.wasm_code = wasm_code;
    request.wasm_metadata = wasm_metadata;
#endif
    done_.push_back(std::move(request));
  }
  ScheduleReportingTask();
}

void MemoryMeasurement::ScheduleReportingTask() {
  if (reporting_task_pending_) return;
  reporting_task_pending_ = true;
  task_runner_->PostTask(MakeCancelableTask(isolate_, [this] {
    reporting_task_pending_ = false;
    ReportResults();
  }));
}

bool MemoryMeasurement::IsGCTaskPending(v8::MeasureMemoryExecution execution) {
  DCHECK(execution == v8::MeasureMemoryExecution::kEager ||
         execution == v8::MeasureMemoryExecution::kDefault);
  return execution == v8::MeasureMemoryExecution::kEager
             ? eager_gc_task_pending_
             : delayed_gc_task_pending_;
}

void MemoryMeasurement::SetGCTaskPending(v8::MeasureMemoryExecution execution) {
  DCHECK(execution == v8::MeasureMemoryExecution::kEager ||
         execution == v8::MeasureMemoryExecution::kDefault);
  if (execution == v8::MeasureMemoryExecution::kEager) {
    eager_gc_task_pending_ = true;
  } else {
    delayed_gc_task_pending_ = true;
  }
}

void MemoryMeasurement::SetGCTaskDone(v8::MeasureMemoryExecution execution) {
  DCHECK(execution == v8::MeasureMemoryExecution::kEager ||
         execution == v8::MeasureMemoryExecution::kDefault);
  if (execution == v8::MeasureMemoryExecution::kEager) {
    eager_gc_task_pending_ = false;
  } else {
    delayed_gc_task_pending_ = false;
  }
}

void MemoryMeasurement::ScheduleGCTask(v8::MeasureMemoryExecution execution) {
  if (execution == v8::MeasureMemoryExecution::kLazy) return;
  if (IsGCTaskPending(execution)) return;
  SetGCTaskPending(execution);
  auto task = MakeCancelableTask(isolate_, [this, execution] {
    SetGCTaskDone(execution);
    if (received_.empty()) return;
    Heap* heap = isolate_->heap();
    if (v8_flags.incremental_marking) {
      if (heap->incremental_marking()->IsStopped()) {
        heap->StartIncrementalMarking(GCFlag::kNoFlags,
                                      GarbageCollectionReason::kMeasureMemory);
      } else {
        if (execution == v8::MeasureMemoryExecution::kEager) {
          heap->FinalizeIncrementalMarkingAtomically(
              GarbageCollectionReason::kMeasureMemory);
        }
        ScheduleGCTask(execution);
      }
    } else {
      heap->CollectGarbage(OLD_SPACE, GarbageCollectionReason::kMeasureMemory);
    }
  });
  if (execution == v8::MeasureMemoryExecution::kEager) {
    task_runner_->PostTask(std::move(task));
  } else {
    task_runner_->PostDelayedTask(std::move(task), NextGCTaskDelayInSeconds());
  }
}

int MemoryMeasurement::NextGCTaskDelayInSeconds() {
  return kGCTaskDelayInSeconds +
         random_number_generator_.NextInt(kGCTaskDelayInSeconds);
}

void MemoryMeasurement::ReportResults() {
  while (!done_.empty()) {
    Request request = std::move(done_.front());
    done_.pop_front();
    HandleScope handle_scope(isolate_);
    v8::LocalVector<v8::Context> contexts(
        reinterpret_cast<v8::Isolate*>(isolate_));
    std::vector<size_t> size_in_bytes;
    DCHECK_EQ(request.sizes.size(),
              static_cast<size_t>(request.contexts->length()));
    for (int i = 0; i < request.contexts->length(); i++) {
      Tagged<HeapObject> raw_context;
      if (!request.contexts->get(i).GetHeapObject(&raw_context)) {
        continue;
      }
      Local<v8::Context> context = Utils::Convert<HeapObject, v8::Context>(
          direct_handle(raw_context, isolate_), isolate_);
      contexts.push_back(context);
      size_in_bytes.push_back(request.sizes[i]);
    }
    request.delegate->MeasurementComplete(
        {{contexts.begin(), contexts.end()},
         {size_in_bytes.begin(), size_in_bytes.end()},
         request.shared,
         request.wasm_code,
         request.wasm_metadata});
    isolate_->counters()->measure_memory_delay_ms()->AddSample(
        static_cast<int>(request.timer.Elapsed().InMilliseconds()));
  }
}

std::unique_ptr<v8::MeasureMemoryDelegate> MemoryMeasurement::DefaultDelegate(
    Isolate* isolate, Handle<NativeContext> context, Handle<JSPromise> promise,
    v8::MeasureMemoryMode mode) {
  return std::make_unique<MeasureMemoryDelegate>(isolate, context, promise,
                                                 mode);
}

void NativeContextStats::Clear() { size_by_context_.clear(); }

void NativeContextStats::Merge(const NativeContextStats& other) {
  for (const auto& it : other.size_by_context_) {
    size_by_context_[it.first] += it.second;
  }
}

void NativeContextStats::IncrementExternalSize(Address context, Tagged<Map> map,
                                               Tagged<HeapObject> object) {
  InstanceType instance_type = map->instance_type();
  size_t external_size = 0;
  if (instance_type == JS_ARRAY_BUFFER_TYPE) {
    external_size = JSArrayBuffer::cast(object)->GetByteLength();
  } else {
    DCHECK(InstanceTypeChecker::IsExternalString(instance_type));
    external_size = ExternalString::cast(object)->ExternalPayloadSize();
  }
  size_by_context_[context] += external_size;
}

}  // namespace internal
}  // namespace v8

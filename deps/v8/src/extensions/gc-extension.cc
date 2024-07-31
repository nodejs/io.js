// Copyright 2010 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/extensions/gc-extension.h"

#include "include/v8-exception.h"
#include "include/v8-isolate.h"
#include "include/v8-maybe.h"
#include "include/v8-microtask-queue.h"
#include "include/v8-object.h"
#include "include/v8-persistent-handle.h"
#include "include/v8-platform.h"
#include "include/v8-primitive.h"
#include "include/v8-profiler.h"
#include "include/v8-template.h"
#include "src/api/api.h"
#include "src/execution/isolate.h"
#include "src/heap/heap.h"
#include "src/profiler/heap-profiler.h"
#include "src/tasks/cancelable-task.h"

namespace v8::internal {
namespace {

enum class GCType { kMinor, kMajor, kMajorWithSnapshot };
enum class ExecutionType { kAsync, kSync };
enum class Flavor { kRegular, kLastResort };

struct GCOptions {
  static GCOptions GetDefault() {
    return {GCType::kMajor, ExecutionType::kSync, Flavor::kRegular,
            "heap.heapsnapshot"};
  }
  static GCOptions GetDefaultForTruthyWithoutOptionsBag() {
    return {GCType::kMinor, ExecutionType::kSync, Flavor::kRegular,
            "heap.heapsnapshot"};
  }

  // Used with Nothing<GCOptions>.
  GCOptions() = default;

  GCType type;
  ExecutionType execution;
  Flavor flavor;
  std::string filename;

 private:
  GCOptions(GCType type, ExecutionType execution, Flavor flavor,
            std::string filename)
      : type(type), execution(execution), flavor(flavor), filename(filename) {}
};

MaybeLocal<v8::String> ReadProperty(v8::Isolate* isolate,
                                    v8::Local<v8::Context> ctx,
                                    v8::Local<v8::Object> object,
                                    const char* key) {
  auto k = v8::String::NewFromUtf8(isolate, key).ToLocalChecked();
  auto maybe_property = object->Get(ctx, k);
  v8::Local<v8::Value> property;
  if (!maybe_property.ToLocal(&property) || !property->IsString()) {
    return {};
  }
  return MaybeLocal<v8::String>(property.As<v8::String>());
}

void ParseType(v8::Isolate* isolate, MaybeLocal<v8::String> maybe_type,
               GCOptions* options, bool* found_options_object) {
  if (maybe_type.IsEmpty()) return;

  auto type = maybe_type.ToLocalChecked();
  if (type->StrictEquals(
          v8::String::NewFromUtf8(isolate, "minor").ToLocalChecked())) {
    *found_options_object = true;
    options->type = GCType::kMinor;
  } else if (type->StrictEquals(
                 v8::String::NewFromUtf8(isolate, "major").ToLocalChecked())) {
    *found_options_object = true;
    options->type = GCType::kMajor;
  } else if (type->StrictEquals(
                 v8::String::NewFromUtf8(isolate, "major-snapshot")
                     .ToLocalChecked())) {
    *found_options_object = true;
    options->type = GCType::kMajorWithSnapshot;
  }
}

void ParseExecution(v8::Isolate* isolate,
                    MaybeLocal<v8::String> maybe_execution, GCOptions* options,
                    bool* found_options_object) {
  if (maybe_execution.IsEmpty()) return;

  auto type = maybe_execution.ToLocalChecked();
  if (type->StrictEquals(
          v8::String::NewFromUtf8(isolate, "async").ToLocalChecked())) {
    *found_options_object = true;
    options->execution = ExecutionType::kAsync;
  } else if (type->StrictEquals(
                 v8::String::NewFromUtf8(isolate, "sync").ToLocalChecked())) {
    *found_options_object = true;
    options->execution = ExecutionType::kSync;
  }
}

void ParseFlavor(v8::Isolate* isolate, MaybeLocal<v8::String> maybe_execution,
                 GCOptions* options, bool* found_options_object) {
  if (maybe_execution.IsEmpty()) return;

  auto type = maybe_execution.ToLocalChecked();
  if (type->StrictEquals(
          v8::String::NewFromUtf8(isolate, "regular").ToLocalChecked())) {
    *found_options_object = true;
    options->flavor = Flavor::kRegular;
  } else if (type->StrictEquals(v8::String::NewFromUtf8(isolate, "last-resort")
                                    .ToLocalChecked())) {
    *found_options_object = true;
    options->flavor = Flavor::kLastResort;
  }
}

Maybe<GCOptions> Parse(v8::Isolate* isolate,
                       const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  DCHECK_LT(0, info.Length());

  // Default values.
  auto options = GCOptions::GetDefault();
  // This will only ever transition to true if one property is found. It will
  // never toggle.
  bool found_options_object = false;

  if (info[0]->IsObject()) {
    v8::HandleScope scope(isolate);
    auto ctx = isolate->GetCurrentContext();
    auto param = v8::Local<v8::Object>::Cast(info[0]);

    v8::TryCatch catch_block(isolate);
    ParseType(isolate, ReadProperty(isolate, ctx, param, "type"), &options,
              &found_options_object);
    if (catch_block.HasCaught()) {
      catch_block.ReThrow();
      return Nothing<GCOptions>();
    }
    ParseExecution(isolate, ReadProperty(isolate, ctx, param, "execution"),
                   &options, &found_options_object);
    if (catch_block.HasCaught()) {
      catch_block.ReThrow();
      return Nothing<GCOptions>();
    }
    ParseFlavor(isolate, ReadProperty(isolate, ctx, param, "flavor"), &options,
                &found_options_object);
    if (catch_block.HasCaught()) {
      catch_block.ReThrow();
      return Nothing<GCOptions>();
    }

    if (options.type == GCType::kMajorWithSnapshot) {
      auto maybe_filename = ReadProperty(isolate, ctx, param, "filename");
      if (catch_block.HasCaught()) {
        catch_block.ReThrow();
        return Nothing<GCOptions>();
      }
      Local<v8::String> filename;
      if (maybe_filename.ToLocal(&filename)) {
        std::unique_ptr<char[]> buffer(
            new char[filename->Utf8Length(isolate) + 1]);
        filename->WriteUtf8(isolate, buffer.get());
        options.filename = std::string(buffer.get());
        // Not setting found_options_object as the option only makes sense with
        // properly set type anyways.
        CHECK(found_options_object);
      }
    }
  }

  // If the parameter is not an object or if it does not define any relevant
  // options, default to legacy behavior.
  if (!found_options_object) {
    return Just<GCOptions>(GCOptions::GetDefaultForTruthyWithoutOptionsBag());
  }

  return Just<GCOptions>(options);
}

void InvokeGC(v8::Isolate* isolate, const GCOptions gc_options) {
  Heap* heap = reinterpret_cast<Isolate*>(isolate)->heap();
  EmbedderStackStateScope stack_scope(
      heap,
      gc_options.execution == ExecutionType::kAsync
          ? EmbedderStackStateOrigin::kImplicitThroughTask
          : EmbedderStackStateOrigin::kExplicitInvocation,
      gc_options.execution == ExecutionType::kAsync
          ? StackState::kNoHeapPointers
          : StackState::kMayContainHeapPointers);
  switch (gc_options.type) {
    case GCType::kMinor:
      heap->CollectGarbage(i::NEW_SPACE, i::GarbageCollectionReason::kTesting,
                           kGCCallbackFlagForced);
      break;
    case GCType::kMajor:
      switch (gc_options.flavor) {
        case Flavor::kRegular:
          heap->PreciseCollectAllGarbage(i::GCFlag::kNoFlags,
                                         i::GarbageCollectionReason::kTesting,
                                         kGCCallbackFlagForced);
          break;
        case Flavor::kLastResort:
          heap->CollectAllAvailableGarbage(
              i::GarbageCollectionReason::kTesting);

          break;
      }
      break;
    case GCType::kMajorWithSnapshot:
      heap->PreciseCollectAllGarbage(i::GCFlag::kNoFlags,
                                     i::GarbageCollectionReason::kTesting,
                                     kGCCallbackFlagForced);
      i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
      HeapProfiler* heap_profiler = i_isolate->heap_profiler();
      // Since this API is intended for V8 devs, we do not treat globals as
      // roots here on purpose.
      v8::HeapProfiler::HeapSnapshotOptions options;
      options.numerics_mode =
          v8::HeapProfiler::NumericsMode::kExposeNumericValues;
      options.snapshot_mode =
          v8::HeapProfiler::HeapSnapshotMode::kExposeInternals;
      heap_profiler->TakeSnapshotToFile(options, gc_options.filename);
      break;
  }
}

class AsyncGC final : public CancelableTask {
 public:
  ~AsyncGC() final = default;

  AsyncGC(v8::Isolate* isolate, v8::Local<v8::Promise::Resolver> resolver,
          GCOptions options)
      : CancelableTask(reinterpret_cast<Isolate*>(isolate)),
        isolate_(isolate),
        ctx_(isolate, isolate->GetCurrentContext()),
        resolver_(isolate, resolver),
        options_(options) {}
  AsyncGC(const AsyncGC&) = delete;
  AsyncGC& operator=(const AsyncGC&) = delete;

  void RunInternal() final {
    v8::HandleScope scope(isolate_);
    InvokeGC(isolate_, options_);
    auto resolver = v8::Local<v8::Promise::Resolver>::New(isolate_, resolver_);
    auto ctx = Local<v8::Context>::New(isolate_, ctx_);
    v8::MicrotasksScope microtasks_scope(
        ctx, v8::MicrotasksScope::kDoNotRunMicrotasks);
    resolver->Resolve(ctx, v8::Undefined(isolate_)).ToChecked();
  }

 private:
  v8::Isolate* isolate_;
  v8::Global<v8::Context> ctx_;
  v8::Global<v8::Promise::Resolver> resolver_;
  GCOptions options_;
};

}  // namespace

v8::Local<v8::FunctionTemplate> GCExtension::GetNativeFunctionTemplate(
    v8::Isolate* isolate, v8::Local<v8::String> str) {
  return v8::FunctionTemplate::New(isolate, GCExtension::GC);
}

void GCExtension::GC(const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();

  // Immediate bailout if no arguments are provided.
  if (info.Length() == 0) {
    InvokeGC(isolate, GCOptions::GetDefault());
    return;
  }

  GCOptions options;
  if (!Parse(isolate, info).To(&options)) {
    // Parsing ran into an exception. Just bail out without GC in this case.
    return;
  }
  switch (options.execution) {
    case ExecutionType::kSync:
      InvokeGC(isolate, options);
      break;
    case ExecutionType::kAsync: {
      v8::HandleScope scope(isolate);
      auto resolver = v8::Promise::Resolver::New(isolate->GetCurrentContext())
                          .ToLocalChecked();
      info.GetReturnValue().Set(resolver->GetPromise());
      auto task_runner =
          V8::GetCurrentPlatform()->GetForegroundTaskRunner(isolate);
      CHECK(task_runner->NonNestableTasksEnabled());
      task_runner->PostNonNestableTask(
          std::make_unique<AsyncGC>(isolate, resolver, options));
    } break;
  }
}

}  // namespace v8::internal

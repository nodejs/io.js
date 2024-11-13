// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The common functionality when building with or without snapshots.

#include "src/snapshot/snapshot.h"

#include "src/api/api-inl.h"  // For OpenHandle.
#include "src/baseline/baseline-batch-compiler.h"
#include "src/common/assert-scope.h"
#include "src/execution/local-isolate-inl.h"
#include "src/handles/global-handles-inl.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/read-only-promotion.h"
#include "src/heap/safepoint.h"
#include "src/init/bootstrapper.h"
#include "src/logging/counters-scopes.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/js-regexp-inl.h"
#include "src/snapshot/context-deserializer.h"
#include "src/snapshot/context-serializer.h"
#include "src/snapshot/read-only-serializer.h"
#include "src/snapshot/shared-heap-serializer.h"
#include "src/snapshot/snapshot-utils.h"
#include "src/snapshot/startup-serializer.h"
#include "src/utils/memcopy.h"
#include "src/utils/version.h"

#ifdef V8_SNAPSHOT_COMPRESSION
#include "src/snapshot/snapshot-compression.h"
#endif

namespace v8 {
namespace internal {

namespace {

class SnapshotImpl : public AllStatic {
 public:
  static v8::StartupData CreateSnapshotBlob(
      const SnapshotData* startup_snapshot_in,
      const SnapshotData* read_only_snapshot_in,
      const SnapshotData* shared_heap_snapshot_in,
      const std::vector<SnapshotData*>& context_snapshots_in,
      bool can_be_rehashed);

  static uint32_t ExtractNumContexts(const v8::StartupData* data);
  static uint32_t ExtractContextOffset(const v8::StartupData* data,
                                       uint32_t index);
  static base::Vector<const uint8_t> ExtractStartupData(
      const v8::StartupData* data);
  static base::Vector<const uint8_t> ExtractReadOnlyData(
      const v8::StartupData* data);
  static base::Vector<const uint8_t> ExtractSharedHeapData(
      const v8::StartupData* data);
  static base::Vector<const uint8_t> ExtractContextData(
      const v8::StartupData* data, uint32_t index);

  static uint32_t GetHeaderValue(const v8::StartupData* data, uint32_t offset) {
    DCHECK_NOT_NULL(data);
    DCHECK_LT(offset, static_cast<uint32_t>(data->raw_size));
    return base::ReadLittleEndianValue<uint32_t>(
        reinterpret_cast<Address>(data->data) + offset);
  }
  static void SetHeaderValue(char* data, uint32_t offset, uint32_t value) {
    base::WriteLittleEndianValue(reinterpret_cast<Address>(data) + offset,
                                 value);
  }

  static void CheckVersion(const v8::StartupData* data);

  // Snapshot blob layout:
  // [0] number of contexts N
  // [1] rehashability
  // [2] checksum
  // [3] read-only snapshot checksum
  // [4] (64 bytes) version string
  // [5] offset to readonly
  // [6] offset to shared heap
  // [7] offset to context 0
  // [8] offset to context 1
  // ...
  // ... offset to context N - 1
  // ... startup snapshot data
  // ... read-only snapshot data
  // ... shared heap snapshot data
  // ... context 0 snapshot data
  // ... context 1 snapshot data

  static const uint32_t kNumberOfContextsOffset = 0;
  // TODO(yangguo): generalize rehashing, and remove this flag.
  static const uint32_t kRehashabilityOffset =
      kNumberOfContextsOffset + kUInt32Size;
  static const uint32_t kChecksumOffset = kRehashabilityOffset + kUInt32Size;
  static const uint32_t kReadOnlySnapshotChecksumOffset =
      kChecksumOffset + kUInt32Size;
  static const uint32_t kVersionStringOffset =
      kReadOnlySnapshotChecksumOffset + kUInt32Size;
  static const uint32_t kVersionStringLength = 64;
  static const uint32_t kReadOnlyOffsetOffset =
      kVersionStringOffset + kVersionStringLength;
  static const uint32_t kSharedHeapOffsetOffset =
      kReadOnlyOffsetOffset + kUInt32Size;
  static const uint32_t kFirstContextOffsetOffset =
      kSharedHeapOffsetOffset + kUInt32Size;

  static base::Vector<const uint8_t> ChecksummedContent(
      const v8::StartupData* data) {
    // The hashed region is everything but the header slots up-to-and-including
    // the checksum slot itself.
    // TODO(jgruber): We currently exclude #contexts and rehashability. This
    // seems arbitrary and I think we could shuffle header slot order around to
    // include them, just for consistency.
    static_assert(kReadOnlySnapshotChecksumOffset ==
                  kChecksumOffset + kUInt32Size);
    const uint32_t kChecksumStart = kReadOnlySnapshotChecksumOffset;
    return base::Vector<const uint8_t>(
        reinterpret_cast<const uint8_t*>(data->data + kChecksumStart),
        data->raw_size - kChecksumStart);
  }

  static uint32_t StartupSnapshotOffset(int num_contexts) {
    return POINTER_SIZE_ALIGN(kFirstContextOffsetOffset +
                              num_contexts * kInt32Size);
  }

  static uint32_t ContextSnapshotOffsetOffset(int index) {
    return kFirstContextOffsetOffset + index * kInt32Size;
  }
};

}  // namespace

SnapshotData MaybeDecompress(Isolate* isolate,
                             base::Vector<const uint8_t> snapshot_data) {
#ifdef V8_SNAPSHOT_COMPRESSION
  TRACE_EVENT0("v8", "V8.SnapshotDecompress");
  RCS_SCOPE(isolate, RuntimeCallCounterId::kSnapshotDecompress);
  NestedTimedHistogramScope histogram_timer(
      isolate->counters()->snapshot_decompress());
  return SnapshotCompression::Decompress(snapshot_data);
#else
  return SnapshotData(snapshot_data);
#endif
}

#ifdef DEBUG
bool Snapshot::SnapshotIsValid(const v8::StartupData* snapshot_blob) {
  return SnapshotImpl::ExtractNumContexts(snapshot_blob) > 0;
}
#endif  // DEBUG

bool Snapshot::HasContextSnapshot(Isolate* isolate, size_t index) {
  // Do not use snapshots if the isolate is used to create snapshots.
  const v8::StartupData* blob = isolate->snapshot_blob();
  if (blob == nullptr) return false;
  if (blob->data == nullptr) return false;
  size_t num_contexts =
      static_cast<size_t>(SnapshotImpl::ExtractNumContexts(blob));
  return index < num_contexts;
}

bool Snapshot::VersionIsValid(const v8::StartupData* data) {
  char version[SnapshotImpl::kVersionStringLength];
  memset(version, 0, SnapshotImpl::kVersionStringLength);
  CHECK_LT(
      SnapshotImpl::kVersionStringOffset + SnapshotImpl::kVersionStringLength,
      static_cast<uint32_t>(data->raw_size));
  Version::GetString(
      base::Vector<char>(version, SnapshotImpl::kVersionStringLength));
  return strncmp(version, data->data + SnapshotImpl::kVersionStringOffset,
                 SnapshotImpl::kVersionStringLength) == 0;
}

bool Snapshot::Initialize(Isolate* isolate) {
  if (!isolate->snapshot_available()) return false;

  const v8::StartupData* blob = isolate->snapshot_blob();
  SnapshotImpl::CheckVersion(blob);
  if (Snapshot::ShouldVerifyChecksum(blob)) {
    CHECK(VerifyChecksum(blob));
  }

  base::Vector<const uint8_t> startup_data =
      SnapshotImpl::ExtractStartupData(blob);
  base::Vector<const uint8_t> read_only_data =
      SnapshotImpl::ExtractReadOnlyData(blob);
  base::Vector<const uint8_t> shared_heap_data =
      SnapshotImpl::ExtractSharedHeapData(blob);

  SnapshotData startup_snapshot_data(MaybeDecompress(isolate, startup_data));
  SnapshotData read_only_snapshot_data(
      MaybeDecompress(isolate, read_only_data));
  SnapshotData shared_heap_snapshot_data(
      MaybeDecompress(isolate, shared_heap_data));

  return isolate->InitWithSnapshot(
      &startup_snapshot_data, &read_only_snapshot_data,
      &shared_heap_snapshot_data, ExtractRehashability(blob));
}

MaybeHandle<Context> Snapshot::NewContextFromSnapshot(
    Isolate* isolate, Handle<JSGlobalProxy> global_proxy, size_t context_index,
    DeserializeEmbedderFieldsCallback embedder_fields_deserializer) {
  if (!isolate->snapshot_available()) return Handle<Context>();

  const v8::StartupData* blob = isolate->snapshot_blob();
  bool can_rehash = ExtractRehashability(blob);
  base::Vector<const uint8_t> context_data = SnapshotImpl::ExtractContextData(
      blob, static_cast<uint32_t>(context_index));
  SnapshotData snapshot_data(MaybeDecompress(isolate, context_data));

  return ContextDeserializer::DeserializeContext(
      isolate, &snapshot_data, context_index, can_rehash, global_proxy,
      embedder_fields_deserializer);
}

// static
void Snapshot::ClearReconstructableDataForSerialization(
    Isolate* isolate, bool clear_recompilable_data) {
  // Clear SFIs and JSRegExps.
  PtrComprCageBase cage_base(isolate);

  {
    HandleScope scope(isolate);
    std::vector<i::Handle<i::SharedFunctionInfo>> sfis_to_clear;
    {
      i::HeapObjectIterator it(isolate->heap());
      for (i::Tagged<i::HeapObject> o = it.Next(); !o.is_null();
           o = it.Next()) {
        if (clear_recompilable_data && IsSharedFunctionInfo(o, cage_base)) {
          i::Tagged<i::SharedFunctionInfo> shared =
              i::Cast<i::SharedFunctionInfo>(o);
          if (IsScript(shared->script(cage_base), cage_base) &&
              Cast<Script>(shared->script(cage_base))->type() ==
                  Script::Type::kExtension) {
            continue;  // Don't clear extensions, they cannot be recompiled.
          }
          if (shared->CanDiscardCompiled()) {
            sfis_to_clear.emplace_back(shared, isolate);
          }
        } else if (IsJSRegExp(o, cage_base)) {
          i::Tagged<i::JSRegExp> regexp = i::Cast<i::JSRegExp>(o);
          if (regexp->has_data()) {
            i::Tagged<i::RegExpData> data = regexp->data(isolate);
            if (data->HasCompiledCode()) {
              DCHECK(Is<IrRegExpData>(regexp->data(isolate)));
              Cast<IrRegExpData>(data)->DiscardCompiledCodeForSerialization();
            }
          }
        }
      }
    }

#if V8_ENABLE_WEBASSEMBLY
    // Clear the cached js-to-wasm wrappers.
    DirectHandle<WeakArrayList> wrappers(isolate->heap()->js_to_wasm_wrappers(),
                                         isolate);
    for (int i = 0; i < wrappers->length(); ++i) {
      wrappers->Set(i, Tagged<MaybeObject>{});
    }
#endif  // V8_ENABLE_WEBASSEMBLY

    // Must happen after heap iteration since SFI::DiscardCompiled may allocate.
    for (i::DirectHandle<i::SharedFunctionInfo> shared : sfis_to_clear) {
      if (shared->CanDiscardCompiled()) {
        i::SharedFunctionInfo::DiscardCompiled(isolate, shared);
      }
    }
  }

  // Clear JSFunctions.
  {
    i::HeapObjectIterator it(isolate->heap());
    for (i::Tagged<i::HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
      if (!IsJSFunction(o, cage_base)) continue;

      i::Tagged<i::JSFunction> fun = i::Cast<i::JSFunction>(o);
      fun->CompleteInobjectSlackTrackingIfActive();

      i::Tagged<i::SharedFunctionInfo> shared = fun->shared();
      if (IsScript(shared->script(cage_base), cage_base) &&
          Cast<Script>(shared->script(cage_base))->type() ==
              Script::Type::kExtension) {
        continue;  // Don't clear extensions, they cannot be recompiled.
      }

      // Also, clear out feedback vectors and recompilable code.
      if (fun->CanDiscardCompiled(isolate)) {
        fun->set_code(*BUILTIN_CODE(isolate, CompileLazy));
      }
      if (!IsUndefined(fun->raw_feedback_cell(cage_base)->value(cage_base))) {
        fun->raw_feedback_cell(cage_base)->set_value(
            i::ReadOnlyRoots(isolate).undefined_value());
      }
#ifdef DEBUG
      if (clear_recompilable_data) {
#if V8_ENABLE_WEBASSEMBLY
        DCHECK(fun->shared()->HasWasmExportedFunctionData() ||
               fun->shared()->HasBuiltinId() ||
               fun->shared()->IsApiFunction() ||
               fun->shared()->HasUncompiledDataWithoutPreparseData());
#else
        DCHECK(fun->shared()->HasBuiltinId() ||
               fun->shared()->IsApiFunction() ||
               fun->shared()->HasUncompiledDataWithoutPreparseData());
#endif  // V8_ENABLE_WEBASSEMBLY
      }
#endif  // DEBUG
    }
  }

  // PendingOptimizeTable also contains BytecodeArray, we need to clear the
  // recompilable code same as above.
  ReadOnlyRoots roots(isolate);
  isolate->heap()->SetFunctionsMarkedForManualOptimization(
      roots.undefined_value());

#if V8_ENABLE_WEBASSEMBLY
  {
    // Check if there are any asm.js / wasm functions on the heap.
    // These cannot be serialized due to restrictions with the js-to-wasm
    // wrapper. A tiered-up wrapper would have to be replaced with a generic
    // wrapper which isn't supported. For asm.js there also isn't any support
    // for the generic wrapper at all.
    i::HeapObjectIterator it(isolate->heap(),
                             HeapObjectIterator::kFilterUnreachable);
    for (i::Tagged<i::HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
      if (IsJSFunction(o)) {
        i::Tagged<i::JSFunction> fun = i::Cast<i::JSFunction>(o);
        if (fun->shared()->HasAsmWasmData()) {
          FATAL("asm.js functions are not supported in snapshots");
        }
        if (fun->shared()->HasWasmExportedFunctionData()) {
          FATAL(
              "Exported WebAssembly functions are not supported in snapshots");
        }
      }
    }
  }
#endif  // V8_ENABLE_WEBASSEMBLY
}

// static
void Snapshot::SerializeDeserializeAndVerifyForTesting(
    Isolate* isolate, DirectHandle<Context> default_context) {
  StartupData serialized_data;
  std::unique_ptr<const char[]> auto_delete_serialized_data;

  isolate->heap()->CollectAllAvailableGarbage(
      i::GarbageCollectionReason::kSnapshotCreator);

  // Test serialization.
  {
    SafepointKind safepoint_kind = isolate->has_shared_space()
                                       ? SafepointKind::kGlobal
                                       : SafepointKind::kIsolate;
    SafepointScope safepoint_scope(isolate, safepoint_kind);
    DisallowGarbageCollection no_gc;

    Snapshot::SerializerFlags flags(
        Snapshot::kAllowUnknownExternalReferencesForTesting |
        Snapshot::kAllowActiveIsolateForTesting |
        ((isolate->has_shared_space() || ReadOnlyHeap::IsReadOnlySpaceShared())
             ? Snapshot::kReconstructReadOnlyAndSharedObjectCachesForTesting
             : 0));
    std::vector<Tagged<Context>> contexts{*default_context};
    std::vector<SerializeEmbedderFieldsCallback> callbacks{
        SerializeEmbedderFieldsCallback()};
    serialized_data = Snapshot::Create(isolate, &contexts, callbacks,
                                       safepoint_scope, no_gc, flags);
    auto_delete_serialized_data.reset(serialized_data.data);
  }

  // The shared heap is verified on Heap teardown, which performs a global
  // safepoint. Both isolate and new_isolate are running in the same thread, so
  // park isolate before running new_isolate to avoid deadlock.
  isolate->main_thread_local_isolate()->ExecuteMainThreadWhileParked(
      [&serialized_data]() {
        // Test deserialization.
        Isolate* new_isolate = Isolate::New();
        std::unique_ptr<v8::ArrayBuffer::Allocator> array_buffer_allocator(
            v8::ArrayBuffer::Allocator::NewDefaultAllocator());
        {
          // Set serializer_enabled() to not install extensions and experimental
          // natives on the new isolate.
          // TODO(v8:10416): This should be a separate setting on the isolate.
          new_isolate->enable_serializer();
          new_isolate->Enter();
          new_isolate->set_snapshot_blob(&serialized_data);
          new_isolate->set_array_buffer_allocator(array_buffer_allocator.get());
          CHECK(Snapshot::Initialize(new_isolate));

          HandleScope scope(new_isolate);
          DirectHandle<Context> new_native_context =
              new_isolate->bootstrapper()->CreateEnvironmentForTesting();
          CHECK(IsNativeContext(*new_native_context));

#ifdef VERIFY_HEAP
          if (v8_flags.verify_heap)
            HeapVerifier::VerifyHeap(new_isolate->heap());
#endif  // VERIFY_HEAP
        }
        new_isolate->Exit();
        Isolate::Delete(new_isolate);
      });
}

// static
v8::StartupData Snapshot::Create(
    Isolate* isolate, std::vector<Tagged<Context>>* contexts,
    const std::vector<SerializeEmbedderFieldsCallback>&
        embedder_fields_serializers,
    const SafepointScope& safepoint_scope,
    const DisallowGarbageCollection& no_gc, SerializerFlags flags) {
  TRACE_EVENT0("v8", "V8.SnapshotCreate");
  DCHECK_EQ(contexts->size(), embedder_fields_serializers.size());
  DCHECK_GT(contexts->size(), 0);
  HandleScope scope(isolate);

  ReadOnlySerializer read_only_serializer(isolate, flags);
  read_only_serializer.Serialize();

  // TODO(v8:6593): generalize rehashing, and remove this flag.
  bool can_be_rehashed = read_only_serializer.can_be_rehashed();

  SharedHeapSerializer shared_heap_serializer(isolate, flags);
  StartupSerializer startup_serializer(isolate, flags, &shared_heap_serializer);
  startup_serializer.SerializeStrongReferences(no_gc);

  // Serialize each context with a new serializer.
  const int num_contexts = static_cast<int>(contexts->size());
  std::vector<SnapshotData*> context_snapshots;
  context_snapshots.reserve(num_contexts);

  std::vector<int> context_allocation_sizes;
  for (int i = 0; i < num_contexts; i++) {
    ContextSerializer context_serializer(isolate, flags, &startup_serializer,
                                         embedder_fields_serializers[i]);
    context_serializer.Serialize(&contexts->at(i), no_gc);
    can_be_rehashed = can_be_rehashed && context_serializer.can_be_rehashed();
    context_snapshots.push_back(new SnapshotData(&context_serializer));
    if (v8_flags.serialization_statistics) {
      context_allocation_sizes.push_back(
          context_serializer.TotalAllocationSize());
    }
  }

  startup_serializer.SerializeWeakReferencesAndDeferred();
  can_be_rehashed = can_be_rehashed && startup_serializer.can_be_rehashed();

  startup_serializer.CheckNoDirtyFinalizationRegistries();

  shared_heap_serializer.FinalizeSerialization();
  can_be_rehashed = can_be_rehashed && shared_heap_serializer.can_be_rehashed();

  if (v8_flags.serialization_statistics) {
    DCHECK_NE(read_only_serializer.TotalAllocationSize(), 0);
    DCHECK_NE(startup_serializer.TotalAllocationSize(), 0);
    // The shared heap snapshot can be empty, no problem.
    // DCHECK_NE(shared_heap_serializer.TotalAllocationSize(), 0);
    int per_isolate_allocation_size = startup_serializer.TotalAllocationSize();
    int per_process_allocation_size = 0;
    if (ReadOnlyHeap::IsReadOnlySpaceShared()) {
      per_process_allocation_size += read_only_serializer.TotalAllocationSize();
    } else {
      per_isolate_allocation_size += read_only_serializer.TotalAllocationSize();
    }
    // TODO(jgruber): At snapshot-generation time we don't know whether the
    // shared heap snapshot will actually be shared at runtime, or if it will
    // be deserialized into each isolate. Conservatively account to per-isolate
    // memory here.
    per_isolate_allocation_size += shared_heap_serializer.TotalAllocationSize();
    // These prints must match the regexp in test/memory/Memory.json
    PrintF("Deserialization will allocate:\n");
    PrintF("%10d bytes per process\n", per_process_allocation_size);
    PrintF("%10d bytes per isolate\n", per_isolate_allocation_size);
    for (int i = 0; i < num_contexts; i++) {
      DCHECK_NE(context_allocation_sizes[i], 0);
      PrintF("%10d bytes per context #%d\n", context_allocation_sizes[i], i);
    }
    PrintF("\n");
  }

  SnapshotData read_only_snapshot(&read_only_serializer);
  SnapshotData shared_heap_snapshot(&shared_heap_serializer);
  SnapshotData startup_snapshot(&startup_serializer);
  v8::StartupData result = SnapshotImpl::CreateSnapshotBlob(
      &startup_snapshot, &read_only_snapshot, &shared_heap_snapshot,
      context_snapshots, can_be_rehashed);

  for (const SnapshotData* ptr : context_snapshots) delete ptr;

  CHECK(Snapshot::VerifyChecksum(&result));
  return result;
}

v8::StartupData SnapshotImpl::CreateSnapshotBlob(
    const SnapshotData* startup_snapshot_in,
    const SnapshotData* read_only_snapshot_in,
    const SnapshotData* shared_heap_snapshot_in,
    const std::vector<SnapshotData*>& context_snapshots_in,
    bool can_be_rehashed) {
  TRACE_EVENT0("v8", "V8.SnapshotCompress");
  // Have these separate from snapshot_in for compression, since we need to
  // access the compressed data as well as the uncompressed reservations.
  const SnapshotData* startup_snapshot;
  const SnapshotData* read_only_snapshot;
  const SnapshotData* shared_heap_snapshot;
  const std::vector<SnapshotData*>* context_snapshots;
#ifdef V8_SNAPSHOT_COMPRESSION
  SnapshotData startup_compressed(
      SnapshotCompression::Compress(startup_snapshot_in));
  SnapshotData read_only_compressed(
      SnapshotCompression::Compress(read_only_snapshot_in));
  SnapshotData shared_heap_compressed(
      SnapshotCompression::Compress(shared_heap_snapshot_in));
  startup_snapshot = &startup_compressed;
  read_only_snapshot = &read_only_compressed;
  shared_heap_snapshot = &shared_heap_compressed;
  std::vector<SnapshotData> context_snapshots_compressed;
  context_snapshots_compressed.reserve(context_snapshots_in.size());
  std::vector<SnapshotData*> context_snapshots_compressed_ptrs;
  for (unsigned int i = 0; i < context_snapshots_in.size(); ++i) {
    context_snapshots_compressed.push_back(
        SnapshotCompression::Compress(context_snapshots_in[i]));
    context_snapshots_compressed_ptrs.push_back(
        &context_snapshots_compressed[i]);
  }
  context_snapshots = &context_snapshots_compressed_ptrs;
#else
  startup_snapshot = startup_snapshot_in;
  read_only_snapshot = read_only_snapshot_in;
  shared_heap_snapshot = shared_heap_snapshot_in;
  context_snapshots = &context_snapshots_in;
#endif

  uint32_t num_contexts = static_cast<uint32_t>(context_snapshots->size());
  uint32_t startup_snapshot_offset =
      SnapshotImpl::StartupSnapshotOffset(num_contexts);
  uint32_t total_length = startup_snapshot_offset;
  total_length += static_cast<uint32_t>(startup_snapshot->RawData().length());
  total_length += static_cast<uint32_t>(read_only_snapshot->RawData().length());
  total_length +=
      static_cast<uint32_t>(shared_heap_snapshot->RawData().length());
  for (const auto context_snapshot : *context_snapshots) {
    total_length += static_cast<uint32_t>(context_snapshot->RawData().length());
  }

  char* data = new char[total_length];
  // Zero out pre-payload data. Part of that is only used for padding.
  memset(data, 0, SnapshotImpl::StartupSnapshotOffset(num_contexts));

  SnapshotImpl::SetHeaderValue(data, SnapshotImpl::kNumberOfContextsOffset,
                               num_contexts);
  SnapshotImpl::SetHeaderValue(data, SnapshotImpl::kRehashabilityOffset,
                               can_be_rehashed ? 1 : 0);

  // Write version string into snapshot data.
  memset(data + SnapshotImpl::kVersionStringOffset, 0,
         SnapshotImpl::kVersionStringLength);
  Version::GetString(
      base::Vector<char>(data + SnapshotImpl::kVersionStringOffset,
                         SnapshotImpl::kVersionStringLength));

  // Startup snapshot (isolate-specific data).
  uint32_t payload_offset = startup_snapshot_offset;
  uint32_t payload_length =
      static_cast<uint32_t>(startup_snapshot->RawData().length());
  CopyBytes(data + payload_offset,
            reinterpret_cast<const char*>(startup_snapshot->RawData().begin()),
            payload_length);
  if (v8_flags.serialization_statistics) {
    // These prints must match the regexp in test/memory/Memory.json
    PrintF("Snapshot blob consists of:\n");
    PrintF("%10d bytes for startup\n", payload_length);
  }
  payload_offset += payload_length;

  // Read-only.
  SnapshotImpl::SetHeaderValue(data, SnapshotImpl::kReadOnlyOffsetOffset,
                               payload_offset);
  payload_length = read_only_snapshot->RawData().length();
  CopyBytes(
      data + payload_offset,
      reinterpret_cast<const char*>(read_only_snapshot->RawData().begin()),
      payload_length);
  SnapshotImpl::SetHeaderValue(
      data, SnapshotImpl::kReadOnlySnapshotChecksumOffset,
      Checksum(base::VectorOf(
          reinterpret_cast<const uint8_t*>(data + payload_offset),
          payload_length)));
  if (v8_flags.serialization_statistics) {
    // These prints must match the regexp in test/memory/Memory.json
    PrintF("%10d bytes for read-only\n", payload_length);
  }
  payload_offset += payload_length;

  // Shared heap.
  SnapshotImpl::SetHeaderValue(data, SnapshotImpl::kSharedHeapOffsetOffset,
                               payload_offset);
  payload_length = shared_heap_snapshot->RawData().length();
  CopyBytes(
      data + payload_offset,
      reinterpret_cast<const char*>(shared_heap_snapshot->RawData().begin()),
      payload_length);
  if (v8_flags.serialization_statistics) {
    // These prints must match the regexp in test/memory/Memory.json
    PrintF("%10d bytes for shared heap\n", payload_length);
  }
  payload_offset += payload_length;

  // Context snapshots (context-specific data).
  for (uint32_t i = 0; i < num_contexts; i++) {
    SnapshotImpl::SetHeaderValue(
        data, SnapshotImpl::ContextSnapshotOffsetOffset(i), payload_offset);
    SnapshotData* context_snapshot = (*context_snapshots)[i];
    payload_length = context_snapshot->RawData().length();
    CopyBytes(
        data + payload_offset,
        reinterpret_cast<const char*>(context_snapshot->RawData().begin()),
        payload_length);
    if (v8_flags.serialization_statistics) {
      // These prints must match the regexp in test/memory/Memory.json
      PrintF("%10d bytes for context #%d\n", payload_length, i);
    }
    payload_offset += payload_length;
  }
  if (v8_flags.serialization_statistics) PrintF("\n");

  DCHECK_EQ(total_length, payload_offset);
  v8::StartupData result = {data, static_cast<int>(total_length)};

  SnapshotImpl::SetHeaderValue(
      data, SnapshotImpl::kChecksumOffset,
      Checksum(SnapshotImpl::ChecksummedContent(&result)));

  return result;
}

uint32_t SnapshotImpl::ExtractNumContexts(const v8::StartupData* data) {
  return GetHeaderValue(data, kNumberOfContextsOffset);
}

uint32_t Snapshot::GetExpectedChecksum(const v8::StartupData* data) {
  return SnapshotImpl::GetHeaderValue(data, SnapshotImpl::kChecksumOffset);
}
uint32_t Snapshot::CalculateChecksum(const v8::StartupData* data) {
  return Checksum(SnapshotImpl::ChecksummedContent(data));
}

bool Snapshot::VerifyChecksum(const v8::StartupData* data) {
  base::ElapsedTimer timer;
  if (v8_flags.profile_deserialization) timer.Start();
  uint32_t expected = GetExpectedChecksum(data);
  uint32_t result = CalculateChecksum(data);
  if (v8_flags.profile_deserialization) {
    double ms = timer.Elapsed().InMillisecondsF();
    PrintF("[Verifying snapshot checksum took %0.3f ms]\n", ms);
  }
  return result == expected;
}

uint32_t SnapshotImpl::ExtractContextOffset(const v8::StartupData* data,
                                            uint32_t index) {
  // Extract the offset of the context at a given index from the StartupData,
  // and check that it is within bounds.
  uint32_t context_offset =
      GetHeaderValue(data, ContextSnapshotOffsetOffset(index));
  CHECK_LT(context_offset, static_cast<uint32_t>(data->raw_size));
  return context_offset;
}

bool Snapshot::ExtractRehashability(const v8::StartupData* data) {
  uint32_t rehashability =
      SnapshotImpl::GetHeaderValue(data, SnapshotImpl::kRehashabilityOffset);
  CHECK_IMPLIES(rehashability != 0, rehashability == 1);
  return rehashability != 0;
}

// static
uint32_t Snapshot::ExtractReadOnlySnapshotChecksum(
    const v8::StartupData* data) {
  return SnapshotImpl::GetHeaderValue(
      data, SnapshotImpl::kReadOnlySnapshotChecksumOffset);
}

namespace {
base::Vector<const uint8_t> ExtractData(const v8::StartupData* snapshot,
                                        uint32_t start_offset,
                                        uint32_t end_offset) {
  CHECK_LT(start_offset, end_offset);
  CHECK_LT(end_offset, snapshot->raw_size);
  uint32_t length = end_offset - start_offset;
  const uint8_t* data =
      reinterpret_cast<const uint8_t*>(snapshot->data + start_offset);
  return base::Vector<const uint8_t>(data, length);
}
}  // namespace

base::Vector<const uint8_t> SnapshotImpl::ExtractStartupData(
    const v8::StartupData* data) {
  DCHECK(Snapshot::SnapshotIsValid(data));

  uint32_t num_contexts = ExtractNumContexts(data);
  return ExtractData(data, StartupSnapshotOffset(num_contexts),
                     GetHeaderValue(data, kReadOnlyOffsetOffset));
}

base::Vector<const uint8_t> SnapshotImpl::ExtractReadOnlyData(
    const v8::StartupData* data) {
  DCHECK(Snapshot::SnapshotIsValid(data));

  return ExtractData(data, GetHeaderValue(data, kReadOnlyOffsetOffset),
                     GetHeaderValue(data, kSharedHeapOffsetOffset));
}

base::Vector<const uint8_t> SnapshotImpl::ExtractSharedHeapData(
    const v8::StartupData* data) {
  DCHECK(Snapshot::SnapshotIsValid(data));

  return ExtractData(data, GetHeaderValue(data, kSharedHeapOffsetOffset),
                     GetHeaderValue(data, ContextSnapshotOffsetOffset(0)));
}

base::Vector<const uint8_t> SnapshotImpl::ExtractContextData(
    const v8::StartupData* data, uint32_t index) {
  uint32_t num_contexts = ExtractNumContexts(data);
  CHECK_LT(index, num_contexts);

  uint32_t context_offset = ExtractContextOffset(data, index);
  uint32_t next_context_offset;
  if (index == num_contexts - 1) {
    next_context_offset = data->raw_size;
  } else {
    next_context_offset = ExtractContextOffset(data, index + 1);
    CHECK_LT(next_context_offset, data->raw_size);
  }

  const uint8_t* context_data =
      reinterpret_cast<const uint8_t*>(data->data + context_offset);
  uint32_t context_length = next_context_offset - context_offset;
  return base::Vector<const uint8_t>(context_data, context_length);
}

void SnapshotImpl::CheckVersion(const v8::StartupData* data) {
  if (!Snapshot::VersionIsValid(data)) {
    char version[kVersionStringLength];
    memset(version, 0, kVersionStringLength);
    CHECK_LT(kVersionStringOffset + kVersionStringLength,
             static_cast<uint32_t>(data->raw_size));
    Version::GetString(base::Vector<char>(version, kVersionStringLength));
    FATAL(
        "Version mismatch between V8 binary and snapshot.\n"
        "#   V8 binary version: %.*s\n"
        "#    Snapshot version: %.*s\n"
        "# The snapshot consists of %d bytes and contains %d context(s).",
        kVersionStringLength, version, kVersionStringLength,
        data->data + kVersionStringOffset, data->raw_size,
        ExtractNumContexts(data));
  }
}

namespace {

bool RunExtraCode(v8::Isolate* isolate, v8::Local<v8::Context> context,
                  const char* utf8_source, const char* name) {
  v8::Context::Scope context_scope(context);
  v8::TryCatch try_catch(isolate);
  v8::Local<v8::String> source_string;
  if (!v8::String::NewFromUtf8(isolate, utf8_source).ToLocal(&source_string)) {
    return false;
  }
  v8::Local<v8::String> resource_name =
      v8::String::NewFromUtf8(isolate, name).ToLocalChecked();
  v8::ScriptOrigin origin(resource_name);
  v8::ScriptCompiler::Source source(source_string, origin);
  v8::Local<v8::Script> script;
  if (!v8::ScriptCompiler::Compile(context, &source).ToLocal(&script)) {
    return false;
  }
  if (script->Run(context).IsEmpty()) return false;
  CHECK(!try_catch.HasCaught());
  return true;
}

}  // namespace

v8::StartupData CreateSnapshotDataBlobInternal(
    v8::SnapshotCreator::FunctionCodeHandling function_code_handling,
    const char* embedded_source, SnapshotCreator& snapshot_creator,
    Snapshot::SerializerFlags serializer_flags) {
  SnapshotCreatorImpl* creator =
      SnapshotCreatorImpl::FromSnapshotCreator(&snapshot_creator);
  {
    auto v8_isolate = reinterpret_cast<v8::Isolate*>(creator->isolate());
    v8::HandleScope scope(v8_isolate);
    v8::Local<v8::Context> context = v8::Context::New(v8_isolate);
    if (embedded_source != nullptr &&
        !RunExtraCode(v8_isolate, context, embedded_source, "<embedded>")) {
      return {};
    }
    creator->SetDefaultContext(Utils::OpenHandle(*context),
                               SerializeEmbedderFieldsCallback());
  }
  return creator->CreateBlob(function_code_handling, serializer_flags);
}

v8::StartupData CreateSnapshotDataBlobInternal(
    v8::SnapshotCreator::FunctionCodeHandling function_code_handling,
    const char* embedded_source, Snapshot::SerializerFlags serializer_flags) {
  std::unique_ptr<v8::ArrayBuffer::Allocator> array_buffer_allocator(
      v8::ArrayBuffer::Allocator::NewDefaultAllocator());
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = array_buffer_allocator.get();
  v8::SnapshotCreator creator(create_params);
  return CreateSnapshotDataBlobInternal(function_code_handling, embedded_source,
                                        creator, serializer_flags);
}

v8::StartupData CreateSnapshotDataBlobInternalForInspectorTest(
    v8::SnapshotCreator::FunctionCodeHandling function_code_handling,
    const char* embedded_source) {
  return CreateSnapshotDataBlobInternal(function_code_handling,
                                        embedded_source);
}

v8::StartupData WarmUpSnapshotDataBlobInternal(
    v8::StartupData cold_snapshot_blob, const char* warmup_source) {
  CHECK(cold_snapshot_blob.raw_size > 0 && cold_snapshot_blob.data != nullptr);
  CHECK_NOT_NULL(warmup_source);

  // Use following steps to create a warmed up snapshot blob from a cold one:
  //  - Create a new isolate from the cold snapshot.
  //  - Create a new context to run the warmup script. This will trigger
  //    compilation of executed functions.
  //  - Create a new context. This context will be unpolluted.
  //  - Serialize the isolate and the second context into a new snapshot blob.

  std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(
      ArrayBuffer::Allocator::NewDefaultAllocator());
  v8::Isolate::CreateParams params;
  params.snapshot_blob = &cold_snapshot_blob;
  params.array_buffer_allocator = allocator.get();
  v8::SnapshotCreator snapshot_creator(params);
  v8::Isolate* isolate = snapshot_creator.GetIsolate();
  {
    v8::HandleScope scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    if (!RunExtraCode(isolate, context, warmup_source, "<warm-up>")) {
      return {};
    }
  }
  {
    v8::HandleScope handle_scope(isolate);
    isolate->ContextDisposedNotification(false);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    snapshot_creator.SetDefaultContext(context);
  }

  return snapshot_creator.CreateBlob(
      v8::SnapshotCreator::FunctionCodeHandling::kKeep);
}

void SnapshotCreatorImpl::InitInternal(const StartupData* blob) {
  isolate_->enable_serializer();
  isolate_->Enter();

  if (blob != nullptr && blob->raw_size > 0) {
    isolate_->set_snapshot_blob(blob);
    Snapshot::Initialize(isolate_);
  } else {
    isolate_->InitWithoutSnapshot();
  }

#ifdef V8_ENABLE_SPARKPLUG
  isolate_->baseline_batch_compiler()->set_enabled(false);
#endif  // V8_ENABLE_SPARKPLUG

  // Reserve a spot for the default context s.t. the call sequence of
  // SetDefaultContext / AddContext remains independent.
  contexts_.push_back(SerializableContext{});
  DCHECK_EQ(contexts_.size(), kDefaultContextIndex + 1);
}

SnapshotCreatorImpl::SnapshotCreatorImpl(
    Isolate* isolate, const intptr_t* api_external_references,
    const StartupData* existing_blob, bool owns_isolate)
    : owns_isolate_(owns_isolate),
      isolate_(isolate == nullptr ? Isolate::New() : isolate),
      array_buffer_allocator_(ArrayBuffer::Allocator::NewDefaultAllocator()) {
  DCHECK_NOT_NULL(isolate_);

  isolate_->set_array_buffer_allocator(array_buffer_allocator_.get());
  isolate_->set_api_external_references(api_external_references);

  InitInternal(existing_blob ? existing_blob : Snapshot::DefaultSnapshotBlob());
}

SnapshotCreatorImpl::SnapshotCreatorImpl(
    const v8::Isolate::CreateParams& params)
    : owns_isolate_(true), isolate_(Isolate::New()) {
  if (auto allocator = params.array_buffer_allocator_shared) {
    CHECK(params.array_buffer_allocator == nullptr ||
          params.array_buffer_allocator == allocator.get());
    isolate_->set_array_buffer_allocator(allocator.get());
    isolate_->set_array_buffer_allocator_shared(std::move(allocator));
  } else {
    CHECK_NOT_NULL(params.array_buffer_allocator);
    isolate_->set_array_buffer_allocator(params.array_buffer_allocator);
  }
  isolate_->set_api_external_references(params.external_references);
  isolate_->heap()->ConfigureHeap(params.constraints, params.cpp_heap);

  InitInternal(params.snapshot_blob ? params.snapshot_blob
                                    : Snapshot::DefaultSnapshotBlob());
}

SnapshotCreatorImpl::SnapshotCreatorImpl(
    Isolate* isolate, const v8::Isolate::CreateParams& params)
    : owns_isolate_(false), isolate_(isolate) {
  if (auto allocator = params.array_buffer_allocator_shared) {
    CHECK(params.array_buffer_allocator == nullptr ||
          params.array_buffer_allocator == allocator.get());
    isolate_->set_array_buffer_allocator(allocator.get());
    isolate_->set_array_buffer_allocator_shared(std::move(allocator));
  } else {
    CHECK_NOT_NULL(params.array_buffer_allocator);
    isolate_->set_array_buffer_allocator(params.array_buffer_allocator);
  }
  isolate_->set_api_external_references(params.external_references);
  isolate_->heap()->ConfigureHeap(params.constraints, params.cpp_heap);

  InitInternal(params.snapshot_blob ? params.snapshot_blob
                                    : Snapshot::DefaultSnapshotBlob());
}

SnapshotCreatorImpl::~SnapshotCreatorImpl() {
  if (isolate_->heap()->read_only_space()->writable()) {
    // Finalize the RO heap in order to leave the Isolate in a consistent state.
    isolate_->read_only_heap()->OnCreateHeapObjectsComplete(isolate_);
  }
  // Destroy leftover global handles (i.e. if CreateBlob was never called).
  for (size_t i = 0; i < contexts_.size(); i++) {
    DCHECK(!created());
    GlobalHandles::Destroy(contexts_[i].handle_location);
    contexts_[i].handle_location = nullptr;
  }
  isolate_->Exit();
  if (owns_isolate_) Isolate::Delete(isolate_);
}

void SnapshotCreatorImpl::SetDefaultContext(
    Handle<NativeContext> context, SerializeEmbedderFieldsCallback callback) {
  DCHECK(contexts_[kDefaultContextIndex].handle_location == nullptr);
  DCHECK(!context.is_null());
  DCHECK(!created());
  CHECK_EQ(isolate_, context->GetIsolate());
  contexts_[kDefaultContextIndex].handle_location =
      isolate_->global_handles()->Create(*context).location();
  contexts_[kDefaultContextIndex].callback = callback;
}

size_t SnapshotCreatorImpl::AddContext(
    Handle<NativeContext> context, SerializeEmbedderFieldsCallback callback) {
  DCHECK(!context.is_null());
  DCHECK(!created());
  CHECK_EQ(isolate_, context->GetIsolate());
  size_t index = contexts_.size() - kFirstAddtlContextIndex;
  contexts_.emplace_back(
      isolate_->global_handles()->Create(*context).location(), callback);
  return index;
}

size_t SnapshotCreatorImpl::AddData(DirectHandle<NativeContext> context,
                                    Address object) {
  CHECK_EQ(isolate_, context->GetIsolate());
  DCHECK_NE(object, kNullAddress);
  DCHECK(!created());
  HandleScope scope(isolate_);
  DirectHandle<Object> obj(Tagged<Object>(object), isolate_);
  Handle<ArrayList> list;
  if (!IsArrayList(context->serialized_objects())) {
    list = ArrayList::New(isolate_, 1);
  } else {
    list = Handle<ArrayList>(Cast<ArrayList>(context->serialized_objects()),
                             isolate_);
  }
  size_t index = static_cast<size_t>(list->length());
  list = ArrayList::Add(isolate_, list, obj);
  context->set_serialized_objects(*list);
  return index;
}

size_t SnapshotCreatorImpl::AddData(Address object) {
  DCHECK_NE(object, kNullAddress);
  DCHECK(!created());
  HandleScope scope(isolate_);
  DirectHandle<Object> obj(Tagged<Object>(object), isolate_);
  Handle<ArrayList> list;
  if (!IsArrayList(isolate_->heap()->serialized_objects())) {
    list = ArrayList::New(isolate_, 1);
  } else {
    list = Handle<ArrayList>(
        Cast<ArrayList>(isolate_->heap()->serialized_objects()), isolate_);
  }
  size_t index = static_cast<size_t>(list->length());
  list = ArrayList::Add(isolate_, list, obj);
  isolate_->heap()->SetSerializedObjects(*list);
  return index;
}

Handle<NativeContext> SnapshotCreatorImpl::context_at(size_t i) const {
  return Handle<NativeContext>(contexts_[i].handle_location);
}

namespace {

void ConvertSerializedObjectsToFixedArray(Isolate* isolate) {
  if (!IsArrayList(isolate->heap()->serialized_objects())) {
    isolate->heap()->SetSerializedObjects(
        ReadOnlyRoots(isolate).empty_fixed_array());
  } else {
    DirectHandle<ArrayList> list(
        Cast<ArrayList>(isolate->heap()->serialized_objects()), isolate);
    DirectHandle<FixedArray> elements = ArrayList::ToFixedArray(isolate, list);
    isolate->heap()->SetSerializedObjects(*elements);
  }
}

void ConvertSerializedObjectsToFixedArray(Isolate* isolate,
                                          DirectHandle<NativeContext> context) {
  if (!IsArrayList(context->serialized_objects())) {
    context->set_serialized_objects(ReadOnlyRoots(isolate).empty_fixed_array());
  } else {
    DirectHandle<ArrayList> list(Cast<ArrayList>(context->serialized_objects()),
                                 isolate);
    DirectHandle<FixedArray> elements = ArrayList::ToFixedArray(isolate, list);
    context->set_serialized_objects(*elements);
  }
}

}  // anonymous namespace

// static
StartupData SnapshotCreatorImpl::CreateBlob(
    SnapshotCreator::FunctionCodeHandling function_code_handling,
    Snapshot::SerializerFlags serializer_flags) {
  CHECK(!created());
  CHECK(contexts_[kDefaultContextIndex].handle_location != nullptr);

  const size_t num_contexts = contexts_.size();
  const size_t num_additional_contexts = num_contexts - 1;

  // Create and store lists of embedder-provided data needed during
  // serialization.
  {
    HandleScope scope(isolate_);

    // Convert list of context-independent data to FixedArray.
    ConvertSerializedObjectsToFixedArray(isolate_);

    // Convert lists of context-dependent data to FixedArray.
    for (size_t i = 0; i < num_contexts; i++) {
      ConvertSerializedObjectsToFixedArray(isolate_, context_at(i));
    }

    // We need to store the global proxy size upfront in case we need the
    // bootstrapper to create a global proxy before we deserialize the context.
    DirectHandle<FixedArray> global_proxy_sizes =
        isolate_->factory()->NewFixedArray(
            static_cast<int>(num_additional_contexts), AllocationType::kOld);
    for (size_t i = kFirstAddtlContextIndex; i < num_contexts; i++) {
      global_proxy_sizes->set(
          static_cast<int>(i - kFirstAddtlContextIndex),
          Smi::FromInt(context_at(i)->global_proxy()->Size()));
    }
    isolate_->heap()->SetSerializedGlobalProxySizes(*global_proxy_sizes);
  }

  // We might rehash strings and re-sort descriptors. Clear the lookup cache.
  isolate_->descriptor_lookup_cache()->Clear();

  // If we don't do this then we end up with a stray root pointing at the
  // context even after we have disposed of the context.
  {
    // Note that we need to run a garbage collection without stack at this
    // point, so that all dead objects are reclaimed. This is required to avoid
    // conservative stack scanning and guarantee deterministic behaviour.
    EmbedderStackStateScope stack_scope(
        isolate_->heap(), EmbedderStackStateOrigin::kExplicitInvocation,
        StackState::kNoHeapPointers);
    isolate_->heap()->CollectAllAvailableGarbage(
        GarbageCollectionReason::kSnapshotCreator);
  }
  {
    HandleScope scope(isolate_);
    isolate_->heap()->CompactWeakArrayLists();
  }

  Snapshot::ClearReconstructableDataForSerialization(
      isolate_,
      function_code_handling == SnapshotCreator::FunctionCodeHandling::kClear);

  SafepointKind safepoint_kind = isolate_->has_shared_space()
                                     ? SafepointKind::kGlobal
                                     : SafepointKind::kIsolate;
  SafepointScope safepoint_scope(isolate_, safepoint_kind);
  DisallowGarbageCollection no_gc_from_here_on;

  // RO space is usually writable when serializing a snapshot s.t. preceding
  // heap initialization can also extend RO space. There are notable exceptions
  // though, including --stress-snapshot and serializer cctests.
  if (isolate_->heap()->read_only_space()->writable()) {
    // Promote objects from mutable heap spaces to read-only space prior to
    // serialization. Objects can be promoted if a) they are themselves
    // immutable-after-deserialization and b) all objects in the transitive
    // object graph also satisfy condition a).
    ReadOnlyPromotion::Promote(isolate_, safepoint_scope, no_gc_from_here_on);
    // When creating the snapshot from scratch, we are responsible for sealing
    // the RO heap here. Note we cannot delegate the responsibility e.g. to
    // Isolate::Init since it should still be possible to allocate into RO
    // space after the Isolate has been initialized, for example as part of
    // Context creation.
    isolate_->read_only_heap()->OnCreateHeapObjectsComplete(isolate_);
  }

  // Create a vector with all contexts and destroy associated global handles.
  // This is important because serialization visits active global handles as
  // roots, which we don't want for our internal SnapshotCreatorImpl-related
  // data.
  // Note these contexts may be dead after calling Clear(), but will not be
  // collected until serialization completes and the DisallowGarbageCollection
  // scope above goes out of scope.
  std::vector<Tagged<Context>> raw_contexts;
  raw_contexts.reserve(num_contexts);
  {
    HandleScope scope(isolate_);
    for (size_t i = 0; i < num_contexts; i++) {
      raw_contexts.push_back(*context_at(i));
      GlobalHandles::Destroy(contexts_[i].handle_location);
      contexts_[i].handle_location = nullptr;
    }
  }

  // Check that values referenced by global/eternal handles are accounted for.
  SerializedHandleChecker handle_checker(isolate_, &raw_contexts);
  if (!handle_checker.CheckGlobalAndEternalHandles()) {
    GRACEFUL_FATAL("CheckGlobalAndEternalHandles failed");
  }

  // Create a vector with all embedder fields serializers.
  std::vector<SerializeEmbedderFieldsCallback> raw_callbacks;
  raw_callbacks.reserve(num_contexts);
  for (size_t i = 0; i < num_contexts; i++) {
    raw_callbacks.push_back(contexts_[i].callback);
  }

  contexts_.clear();
  return Snapshot::Create(isolate_, &raw_contexts, raw_callbacks,
                          safepoint_scope, no_gc_from_here_on,
                          serializer_flags);
}

SnapshotCreatorImpl* SnapshotCreatorImpl::FromSnapshotCreator(
    v8::SnapshotCreator* snapshot_creator) {
  return snapshot_creator->impl_;
}

}  // namespace internal
}  // namespace v8

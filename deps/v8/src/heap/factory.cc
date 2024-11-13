// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/factory.h"

#include <algorithm>  // For copy
#include <memory>     // For shared_ptr<>
#include <optional>
#include <string>
#include <utility>  // For move

#include "src/ast/ast-source-ranges.h"
#include "src/base/bits.h"
#include "src/builtins/accessors.h"
#include "src/builtins/constants-table-builder.h"
#include "src/codegen/compilation-cache.h"
#include "src/codegen/compiler.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/diagnostics/basic-block-profiler.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/protectors-inl.h"
#include "src/flags/flags.h"
#include "src/heap/heap-allocator-inl.h"
#include "src/heap/heap-inl.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/large-page-metadata-inl.h"
#include "src/heap/mark-compact-inl.h"
#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/read-only-heap.h"
#include "src/ic/handler-configuration-inl.h"
#include "src/init/bootstrapper.h"
#include "src/interpreter/interpreter.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/numbers/conversions.h"
#include "src/numbers/hash-seed-inl.h"
#include "src/objects/allocation-site-inl.h"
#include "src/objects/allocation-site-scopes.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/arguments-inl.h"
#include "src/objects/bigint.h"
#include "src/objects/call-site-info-inl.h"
#include "src/objects/cell-inl.h"
#include "src/objects/debug-objects-inl.h"
#include "src/objects/embedder-data-array-inl.h"
#include "src/objects/feedback-cell-inl.h"
#include "src/objects/feedback-cell.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/foreign-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-array-buffer.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-atomics-synchronization-inl.h"
#include "src/objects/js-collection-inl.h"
#include "src/objects/js-disposable-stack-inl.h"
#include "src/objects/js-generator-inl.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-regexp-inl.h"
#include "src/objects/js-shared-array-inl.h"
#include "src/objects/js-struct-inl.h"
#include "src/objects/js-weak-refs-inl.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/megadom-handler-inl.h"
#include "src/objects/microtask-inl.h"
#include "src/objects/module-inl.h"
#include "src/objects/objects.h"
#include "src/objects/promise-inl.h"
#include "src/objects/property-descriptor-object-inl.h"
#include "src/objects/scope-info.h"
#include "src/objects/string-set-inl.h"
#include "src/objects/struct-inl.h"
#include "src/objects/synthetic-module-inl.h"
#include "src/objects/template-objects-inl.h"
#include "src/objects/templates.h"
#include "src/objects/transitions-inl.h"
#include "src/roots/roots-inl.h"
#include "src/roots/roots.h"
#include "src/strings/unicode-inl.h"
#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/module-decoder-impl.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/wasm-opcodes-inl.h"
#include "src/wasm/wasm-result.h"
#include "src/wasm/wasm-value.h"
#endif

#include "src/heap/local-factory-inl.h"
#include "src/heap/local-heap-inl.h"

namespace v8 {
namespace internal {

Factory::CodeBuilder::CodeBuilder(Isolate* isolate, const CodeDesc& desc,
                                  CodeKind kind)
    : isolate_(isolate),
      local_isolate_(isolate_->main_thread_local_isolate()),
      code_desc_(desc),
      kind_(kind) {}

Factory::CodeBuilder::CodeBuilder(LocalIsolate* local_isolate,
                                  const CodeDesc& desc, CodeKind kind)
    : isolate_(local_isolate->GetMainThreadIsolateUnsafe()),
      local_isolate_(local_isolate),
      code_desc_(desc),
      kind_(kind) {}

Handle<TrustedByteArray> Factory::CodeBuilder::NewTrustedByteArray(int length) {
  return local_isolate_->factory()->NewTrustedByteArray(length);
}

Handle<Code> Factory::CodeBuilder::NewCode(const NewCodeOptions& options) {
  return local_isolate_->factory()->NewCode(options);
}

MaybeHandle<Code> Factory::CodeBuilder::BuildInternal(
    bool retry_allocation_or_fail) {
  DirectHandle<TrustedByteArray> reloc_info =
      NewTrustedByteArray(code_desc_.reloc_size);

  // Basic block profiling data for builtins is stored in the JS heap rather
  // than in separately-allocated C++ objects. Allocate that data now if
  // appropriate.
  Handle<OnHeapBasicBlockProfilerData> on_heap_profiler_data;
  if (V8_UNLIKELY(profiler_data_ && isolate_->IsGeneratingEmbeddedBuiltins())) {
    on_heap_profiler_data = profiler_data_->CopyToJSHeap(isolate_);

    // Add the on-heap data to a global list, which keeps it alive and allows
    // iteration.
    Handle<ArrayList> list(isolate_->heap()->basic_block_profiling_data(),
                           isolate_);
    DirectHandle<ArrayList> new_list = ArrayList::Add(
        isolate_, list, on_heap_profiler_data, AllocationType::kOld);
    isolate_->heap()->SetBasicBlockProfilingData(new_list);
  }

  Tagged<HeapObject> istream_allocation =
      AllocateUninitializedInstructionStream(retry_allocation_or_fail);
  if (istream_allocation.is_null()) {
    return {};
  }

  Handle<InstructionStream> istream;
  {
    // The InstructionStream object has not been fully initialized yet. We
    // rely on the fact that no allocation will happen from this point on.
    DisallowGarbageCollection no_gc;
    Tagged<InstructionStream> raw_istream = InstructionStream::Initialize(
        istream_allocation,
        ReadOnlyRoots(local_isolate_).instruction_stream_map(),
        code_desc_.body_size(), code_desc_.constant_pool_offset, *reloc_info);
    istream = handle(raw_istream, local_isolate_);
    DCHECK(IsAligned(istream->instruction_start(), kCodeAlignment));
    DCHECK_IMPLIES(
        !V8_ENABLE_THIRD_PARTY_HEAP_BOOL &&
            !local_isolate_->heap()->heap()->code_region().is_empty(),
        local_isolate_->heap()->heap()->code_region().contains(
            istream->address()));
  }

  Handle<Code> code;
  {
    static_assert(InstructionStream::kOnHeapBodyIsContiguous);

    NewCodeOptions new_code_options = {
        kind_,
        builtin_,
        is_turbofanned_,
        stack_slots_,
        parameter_count_,
        code_desc_.instruction_size(),
        code_desc_.metadata_size(),
        inlined_bytecode_size_,
        osr_offset_,
        code_desc_.handler_table_offset_relative(),
        code_desc_.constant_pool_offset_relative(),
        code_desc_.code_comments_offset_relative(),
        code_desc_.unwinding_info_offset_relative(),
        interpreter_data_,
        deoptimization_data_,
        bytecode_offset_table_,
        source_position_table_,
        istream,
        /*instruction_start=*/kNullAddress,
    };
    code = NewCode(new_code_options);
    DCHECK_EQ(istream->body_size(), code->body_size());

    {
      DisallowGarbageCollection no_gc;
      Tagged<InstructionStream> raw_istream = *istream;

      // Allow self references to created code object by patching the handle to
      // point to the newly allocated InstructionStream object.
      Handle<Object> self_reference;
      if (self_reference_.ToHandle(&self_reference)) {
        DCHECK_EQ(*self_reference,
                  ReadOnlyRoots(isolate_).self_reference_marker());
        DCHECK_NE(kind_, CodeKind::BASELINE);
        if (isolate_->IsGeneratingEmbeddedBuiltins()) {
          isolate_->builtins_constants_table_builder()->PatchSelfReference(
              self_reference, istream);
        }
        self_reference.PatchValue(raw_istream);
      }

      // Likewise, any references to the basic block counters marker need to be
      // updated to point to the newly-allocated counters array.
      if (V8_UNLIKELY(!on_heap_profiler_data.is_null())) {
        isolate_->builtins_constants_table_builder()
            ->PatchBasicBlockCountersReference(
                handle(on_heap_profiler_data->counts(), isolate_));
      }

      // Migrate generated code.
      // The generated code can contain embedded objects (typically from
      // handles) in a pointer-to-tagged-value format (i.e. with indirection
      // like a handle) that are dereferenced during the copy to point
      // directly to the actual heap objects. These pointers can include
      // references to the code object itself, through the self_reference
      // parameter.
      istream->Finalize(*code, *reloc_info, code_desc_, isolate_->heap());

#ifdef VERIFY_HEAP
      if (v8_flags.verify_heap) {
        HeapObject::VerifyCodePointer(isolate_, raw_istream);
      }
#endif
    }
  }

#ifdef ENABLE_DISASSEMBLER
  if (V8_UNLIKELY(profiler_data_ && v8_flags.turbo_profiling_verbose)) {
    std::ostringstream os;
    code->Disassemble(nullptr, os, isolate_);
    if (!on_heap_profiler_data.is_null()) {
      DirectHandle<String> disassembly =
          local_isolate_->factory()->NewStringFromAsciiChecked(
              os.str().c_str(), AllocationType::kOld);
      on_heap_profiler_data->set_code(*disassembly);
    } else {
      profiler_data_->SetCode(os);
    }
  }
#endif  // ENABLE_DISASSEMBLER

  return code;
}

Tagged<HeapObject> Factory::CodeBuilder::AllocateUninitializedInstructionStream(
    bool retry_allocation_or_fail) {
  LocalHeap* heap = local_isolate_->heap();
  Tagged<HeapObject> result;
  const int object_size = InstructionStream::SizeFor(code_desc_.body_size());
  if (retry_allocation_or_fail) {
    // Only allowed to do `retry_allocation_or_fail` from the main thread.
    // TODO(leszeks): Remove the retrying allocation, always use TryBuild in
    // the code builder.
    DCHECK(local_isolate_->is_main_thread());
    result =
        heap->heap()->allocator()->AllocateRawWith<HeapAllocator::kRetryOrFail>(
            object_size, AllocationType::kCode, AllocationOrigin::kRuntime);
    CHECK(!result.is_null());
    return result;
  } else {
    // Return null if we cannot allocate the code object.
    return heap->AllocateRawWith<HeapAllocator::kLightRetry>(
        object_size, AllocationType::kCode);
  }
}

MaybeHandle<Code> Factory::CodeBuilder::TryBuild() {
  return BuildInternal(false);
}

Handle<Code> Factory::CodeBuilder::Build() {
  return BuildInternal(true).ToHandleChecked();
}

Tagged<HeapObject> Factory::AllocateRaw(int size, AllocationType allocation,
                                        AllocationAlignment alignment) {
  return allocator()->AllocateRawWith<HeapAllocator::kRetryOrFail>(
      size, allocation, AllocationOrigin::kRuntime, alignment);
}

Tagged<HeapObject> Factory::AllocateRawWithAllocationSite(
    DirectHandle<Map> map, AllocationType allocation,
    DirectHandle<AllocationSite> allocation_site) {
  DCHECK(map->instance_type() != MAP_TYPE);
  int size = map->instance_size();
  if (!allocation_site.is_null()) {
    DCHECK(V8_ALLOCATION_SITE_TRACKING_BOOL);
    size += ALIGN_TO_ALLOCATION_ALIGNMENT(AllocationMemento::kSize);
  }
  Tagged<HeapObject> result =
      allocator()->AllocateRawWith<HeapAllocator::kRetryOrFail>(size,
                                                                allocation);
  WriteBarrierMode write_barrier_mode = allocation == AllocationType::kYoung
                                            ? SKIP_WRITE_BARRIER
                                            : UPDATE_WRITE_BARRIER;
  result->set_map_after_allocation(*map, write_barrier_mode);
  if (!allocation_site.is_null()) {
    int aligned_size = ALIGN_TO_ALLOCATION_ALIGNMENT(map->instance_size());
    Tagged<AllocationMemento> alloc_memento = UncheckedCast<AllocationMemento>(
        Tagged<Object>(result.ptr() + aligned_size));
    InitializeAllocationMemento(alloc_memento, *allocation_site);
  }
  return result;
}

void Factory::InitializeAllocationMemento(
    Tagged<AllocationMemento> memento, Tagged<AllocationSite> allocation_site) {
  DCHECK(V8_ALLOCATION_SITE_TRACKING_BOOL);
  memento->set_map_after_allocation(*allocation_memento_map(),
                                    SKIP_WRITE_BARRIER);
  memento->set_allocation_site(allocation_site, SKIP_WRITE_BARRIER);
  if (v8_flags.allocation_site_pretenuring) {
    allocation_site->IncrementMementoCreateCount();
  }
}

Tagged<HeapObject> Factory::New(DirectHandle<Map> map,
                                AllocationType allocation) {
  DCHECK(map->instance_type() != MAP_TYPE);
  int size = map->instance_size();
  Tagged<HeapObject> result =
      allocator()->AllocateRawWith<HeapAllocator::kRetryOrFail>(size,
                                                                allocation);
  // New space objects are allocated white.
  WriteBarrierMode write_barrier_mode = allocation == AllocationType::kYoung
                                            ? SKIP_WRITE_BARRIER
                                            : UPDATE_WRITE_BARRIER;
  result->set_map_after_allocation(*map, write_barrier_mode);
  return result;
}

Handle<HeapObject> Factory::NewFillerObject(int size,
                                            AllocationAlignment alignment,
                                            AllocationType allocation,
                                            AllocationOrigin origin) {
  Heap* heap = isolate()->heap();
  Tagged<HeapObject> result =
      allocator()->AllocateRawWith<HeapAllocator::kRetryOrFail>(
          size, allocation, origin, alignment);
  heap->CreateFillerObjectAt(result.address(), size);
  return Handle<HeapObject>(result, isolate());
}

Handle<PrototypeInfo> Factory::NewPrototypeInfo() {
  auto result = NewStructInternal<PrototypeInfo>(PROTOTYPE_INFO_TYPE,
                                                 AllocationType::kOld);
  DisallowGarbageCollection no_gc;
  result->set_prototype_users(Smi::zero());
  result->set_registry_slot(MemoryChunk::UNREGISTERED);
  result->set_bit_field(0);
  result->set_module_namespace(*undefined_value(), SKIP_WRITE_BARRIER);
  return handle(result, isolate());
}

Handle<EnumCache> Factory::NewEnumCache(DirectHandle<FixedArray> keys,
                                        DirectHandle<FixedArray> indices,
                                        AllocationType allocation) {
  DCHECK(allocation == AllocationType::kOld ||
         allocation == AllocationType::kSharedOld);
  DCHECK_EQ(allocation == AllocationType::kSharedOld,
            InAnySharedSpace(*keys) && InAnySharedSpace(*indices));
  auto result = NewStructInternal<EnumCache>(ENUM_CACHE_TYPE, allocation);
  DisallowGarbageCollection no_gc;
  result->set_keys(*keys);
  result->set_indices(*indices);
  return handle(result, isolate());
}

Handle<Tuple2> Factory::NewTuple2Uninitialized(AllocationType allocation) {
  auto result = NewStructInternal<Tuple2>(TUPLE2_TYPE, allocation);
  return handle(result, isolate());
}

Handle<Tuple2> Factory::NewTuple2(DirectHandle<Object> value1,
                                  DirectHandle<Object> value2,
                                  AllocationType allocation) {
  auto result = NewStructInternal<Tuple2>(TUPLE2_TYPE, allocation);
  DisallowGarbageCollection no_gc;
  result->set_value1(*value1);
  result->set_value2(*value2);
  return handle(result, isolate());
}

Handle<Hole> Factory::NewHole() {
  Handle<Hole> hole(Cast<Hole>(New(hole_map(), AllocationType::kReadOnly)),
                    isolate());
  Hole::Initialize(isolate(), hole, hole_nan_value());
  return hole;
}

Handle<PropertyArray> Factory::NewPropertyArray(int length,
                                                AllocationType allocation) {
  DCHECK_LE(0, length);
  if (length == 0) return empty_property_array();
  Tagged<HeapObject> result = AllocateRawFixedArray(length, allocation);
  DisallowGarbageCollection no_gc;
  result->set_map_after_allocation(*property_array_map(), SKIP_WRITE_BARRIER);
  Tagged<PropertyArray> array = Cast<PropertyArray>(result);
  array->initialize_length(length);
  MemsetTagged(array->data_start(), read_only_roots().undefined_value(),
               length);
  return handle(array, isolate());
}

MaybeHandle<FixedArray> Factory::TryNewFixedArray(
    int length, AllocationType allocation_type) {
  DCHECK_LE(0, length);
  if (length == 0) return empty_fixed_array();

  int size = FixedArray::SizeFor(length);
  Heap* heap = isolate()->heap();
  AllocationResult allocation = heap->AllocateRaw(size, allocation_type);
  Tagged<HeapObject> result;
  if (!allocation.To(&result)) return MaybeHandle<FixedArray>();
  if ((size > heap->MaxRegularHeapObjectSize(allocation_type)) &&
      v8_flags.use_marking_progress_bar) {
    LargePageMetadata::FromHeapObject(result)->ProgressBar().Enable();
  }
  DisallowGarbageCollection no_gc;
  result->set_map_after_allocation(*fixed_array_map(), SKIP_WRITE_BARRIER);
  Tagged<FixedArray> array = Cast<FixedArray>(result);
  array->set_length(length);
  MemsetTagged(array->RawFieldOfFirstElement(), *undefined_value(), length);
  return handle(array, isolate());
}

Handle<FeedbackVector> Factory::NewFeedbackVector(
    DirectHandle<SharedFunctionInfo> shared,
    DirectHandle<ClosureFeedbackCellArray> closure_feedback_cell_array,
    DirectHandle<FeedbackCell> parent_feedback_cell) {
  int length = shared->feedback_metadata()->slot_count();
  DCHECK_LE(0, length);
  int size = FeedbackVector::SizeFor(length);

  Tagged<FeedbackVector> vector =
      Cast<FeedbackVector>(AllocateRawWithImmortalMap(
          size, AllocationType::kOld, *feedback_vector_map()));
  DisallowGarbageCollection no_gc;
  vector->set_shared_function_info(*shared);
  vector->set_maybe_optimized_code(ClearedValue(isolate()));
  vector->set_length(length);
  vector->set_invocation_count(0);
  vector->set_invocation_count_before_stable(0);
  vector->reset_osr_state();
  vector->reset_flags();
  vector->set_log_next_execution(v8_flags.log_function_events);
  vector->set_closure_feedback_cell_array(*closure_feedback_cell_array);
  vector->set_parent_feedback_cell(*parent_feedback_cell);

  // TODO(leszeks): Initialize based on the feedback metadata.
  MemsetTagged(ObjectSlot(vector->slots_start()), *undefined_value(), length);
  return handle(vector, isolate());
}

Handle<EmbedderDataArray> Factory::NewEmbedderDataArray(int length) {
  DCHECK_LE(0, length);
  int size = EmbedderDataArray::SizeFor(length);
  Tagged<EmbedderDataArray> array =
      Cast<EmbedderDataArray>(AllocateRawWithImmortalMap(
          size, AllocationType::kYoung, *embedder_data_array_map()));
  DisallowGarbageCollection no_gc;
  array->set_length(length);

  if (length > 0) {
    for (int i = 0; i < length; i++) {
      // TODO(v8): consider initializing embedded data array with Smi::zero().
      EmbedderDataSlot(array, i).Initialize(*undefined_value());
    }
  }
  return handle(array, isolate());
}

Handle<FixedArrayBase> Factory::NewFixedDoubleArrayWithHoles(int length) {
  DCHECK_LE(0, length);
  Handle<FixedArrayBase> array = NewFixedDoubleArray(length);
  if (length > 0) {
    Cast<FixedDoubleArray>(array)->FillWithHoles(0, length);
  }
  return array;
}

template <typename T>
Handle<T> Factory::AllocateSmallOrderedHashTable(DirectHandle<Map> map,
                                                 int capacity,
                                                 AllocationType allocation) {
  // Capacity must be a power of two, since we depend on being able
  // to divide and multiple by 2 (kLoadFactor) to derive capacity
  // from number of buckets. If we decide to change kLoadFactor
  // to something other than 2, capacity should be stored as another
  // field of this object.
  DCHECK_EQ(T::kLoadFactor, 2);
  capacity =
      base::bits::RoundUpToPowerOfTwo32(std::max({T::kMinCapacity, capacity}));
  capacity = std::min({capacity, T::kMaxCapacity});

  DCHECK_LT(0, capacity);
  DCHECK_EQ(0, capacity % T::kLoadFactor);

  int size = T::SizeFor(capacity);
  Tagged<HeapObject> result =
      AllocateRawWithImmortalMap(size, allocation, *map);
  Handle<T> table(Cast<T>(result), isolate());
  table->Initialize(isolate(), capacity);
  return table;
}

Handle<SmallOrderedHashSet> Factory::NewSmallOrderedHashSet(
    int capacity, AllocationType allocation) {
  return AllocateSmallOrderedHashTable<SmallOrderedHashSet>(
      small_ordered_hash_set_map(), capacity, allocation);
}

Handle<SmallOrderedHashMap> Factory::NewSmallOrderedHashMap(
    int capacity, AllocationType allocation) {
  return AllocateSmallOrderedHashTable<SmallOrderedHashMap>(
      small_ordered_hash_map_map(), capacity, allocation);
}

Handle<SmallOrderedNameDictionary> Factory::NewSmallOrderedNameDictionary(
    int capacity, AllocationType allocation) {
  Handle<SmallOrderedNameDictionary> dict =
      AllocateSmallOrderedHashTable<SmallOrderedNameDictionary>(
          small_ordered_name_dictionary_map(), capacity, allocation);
  dict->SetHash(PropertyArray::kNoHashSentinel);
  return dict;
}

Handle<OrderedHashSet> Factory::NewOrderedHashSet() {
  return OrderedHashSet::Allocate(isolate(), OrderedHashSet::kInitialCapacity,
                                  AllocationType::kYoung)
      .ToHandleChecked();
}

Handle<OrderedHashMap> Factory::NewOrderedHashMap() {
  return OrderedHashMap::Allocate(isolate(), OrderedHashMap::kInitialCapacity,
                                  AllocationType::kYoung)
      .ToHandleChecked();
}

Handle<NameDictionary> Factory::NewNameDictionary(int at_least_space_for) {
  return NameDictionary::New(isolate(), at_least_space_for);
}

Handle<PropertyDescriptorObject> Factory::NewPropertyDescriptorObject() {
  auto object = NewStructInternal<PropertyDescriptorObject>(
      PROPERTY_DESCRIPTOR_OBJECT_TYPE, AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  object->set_flags(0);
  Tagged<Hole> the_hole = read_only_roots().the_hole_value();
  object->set_value(the_hole, SKIP_WRITE_BARRIER);
  object->set_get(the_hole, SKIP_WRITE_BARRIER);
  object->set_set(the_hole, SKIP_WRITE_BARRIER);
  return handle(object, isolate());
}

Handle<SwissNameDictionary> Factory::CreateCanonicalEmptySwissNameDictionary() {
  // This function is only supposed to be used to create the canonical empty
  // version and should not be used afterwards.
  DCHECK(!ReadOnlyRoots(isolate()).is_initialized(
      RootIndex::kEmptySwissPropertyDictionary));

  ReadOnlyRoots roots(isolate());

  DirectHandle<ByteArray> empty_meta_table =
      NewByteArray(SwissNameDictionary::kMetaTableEnumerationDataStartIndex,
                   AllocationType::kReadOnly);

  Tagged<Map> map = roots.swiss_name_dictionary_map();
  int size = SwissNameDictionary::SizeFor(0);
  Tagged<HeapObject> obj =
      AllocateRawWithImmortalMap(size, AllocationType::kReadOnly, map);
  Tagged<SwissNameDictionary> result = Cast<SwissNameDictionary>(obj);
  result->Initialize(isolate(), *empty_meta_table, 0);
  return handle(result, isolate());
}

// Internalized strings are created in the old generation (data space).
Handle<String> Factory::InternalizeUtf8String(base::Vector<const char> string) {
  base::Vector<const uint8_t> utf8_data =
      base::Vector<const uint8_t>::cast(string);
  Utf8Decoder decoder(utf8_data);
  if (decoder.is_ascii()) return InternalizeString(utf8_data);
  if (decoder.is_one_byte()) {
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[decoder.utf16_length()]);
    decoder.Decode(buffer.get(), utf8_data);
    return InternalizeString(
        base::Vector<const uint8_t>(buffer.get(), decoder.utf16_length()));
  }
  std::unique_ptr<uint16_t[]> buffer(new uint16_t[decoder.utf16_length()]);
  decoder.Decode(buffer.get(), utf8_data);
  return InternalizeString(
      base::Vector<const base::uc16>(buffer.get(), decoder.utf16_length()));
}

template <typename SeqString>
Handle<String> Factory::InternalizeString(Handle<SeqString> string, int from,
                                          int length, bool convert_encoding) {
  SeqSubStringKey<SeqString> key(isolate(), string, from, length,
                                 convert_encoding);
  return InternalizeStringWithKey(&key);
}

template Handle<String> Factory::InternalizeString(
    Handle<SeqOneByteString> string, int from, int length,
    bool convert_encoding);
template Handle<String> Factory::InternalizeString(
    Handle<SeqTwoByteString> string, int from, int length,
    bool convert_encoding);

namespace {
void ThrowInvalidEncodedStringBytes(Isolate* isolate, MessageTemplate message) {
#if V8_ENABLE_WEBASSEMBLY
  DCHECK(message == MessageTemplate::kWasmTrapStringInvalidWtf8 ||
         message == MessageTemplate::kWasmTrapStringInvalidUtf8);
  Handle<JSObject> error_obj = isolate->factory()->NewWasmRuntimeError(message);
  JSObject::AddProperty(isolate, error_obj,
                        isolate->factory()->wasm_uncatchable_symbol(),
                        isolate->factory()->true_value(), NONE);
  isolate->Throw(*error_obj);
#else
  // The default in JS-land is to use Utf8Variant::kLossyUtf8, which never
  // throws an error, so if there is no WebAssembly compiled in we'll never get
  // here.
  UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
}

template <typename Decoder, typename PeekBytes>
MaybeHandle<String> NewStringFromBytes(Isolate* isolate, PeekBytes peek_bytes,
                                       AllocationType allocation,
                                       MessageTemplate message) {
  Decoder decoder(peek_bytes());
  if (decoder.is_invalid()) {
    if (message != MessageTemplate::kNone) {
      ThrowInvalidEncodedStringBytes(isolate, message);
    }
    return MaybeHandle<String>();
  }

  if (decoder.utf16_length() == 0) return isolate->factory()->empty_string();

  if (decoder.is_one_byte()) {
    if (decoder.utf16_length() == 1) {
      uint8_t codepoint;
      decoder.Decode(&codepoint, peek_bytes());
      return isolate->factory()->LookupSingleCharacterStringFromCode(codepoint);
    }
    // Allocate string.
    Handle<SeqOneByteString> result;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                               isolate->factory()->NewRawOneByteString(
                                   decoder.utf16_length(), allocation));

    DisallowGarbageCollection no_gc;
    decoder.Decode(result->GetChars(no_gc), peek_bytes());
    return result;
  }

  // Allocate string.
  Handle<SeqTwoByteString> result;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, result,
                             isolate->factory()->NewRawTwoByteString(
                                 decoder.utf16_length(), allocation));

  DisallowGarbageCollection no_gc;
  decoder.Decode(result->GetChars(no_gc), peek_bytes());
  return result;
}

template <typename PeekBytes>
MaybeHandle<String> NewStringFromUtf8Variant(Isolate* isolate,
                                             PeekBytes peek_bytes,
                                             unibrow::Utf8Variant utf8_variant,
                                             AllocationType allocation) {
  switch (utf8_variant) {
    case unibrow::Utf8Variant::kLossyUtf8:
      return NewStringFromBytes<Utf8Decoder>(isolate, peek_bytes, allocation,
                                             MessageTemplate::kNone);
#if V8_ENABLE_WEBASSEMBLY
    case unibrow::Utf8Variant::kUtf8:
      return NewStringFromBytes<StrictUtf8Decoder>(
          isolate, peek_bytes, allocation,
          MessageTemplate::kWasmTrapStringInvalidUtf8);
    case unibrow::Utf8Variant::kUtf8NoTrap:
      return NewStringFromBytes<StrictUtf8Decoder>(
          isolate, peek_bytes, allocation, MessageTemplate::kNone);
    case unibrow::Utf8Variant::kWtf8:
      return NewStringFromBytes<Wtf8Decoder>(
          isolate, peek_bytes, allocation,
          MessageTemplate::kWasmTrapStringInvalidWtf8);
#endif
  }
}

}  // namespace

MaybeHandle<String> Factory::NewStringFromUtf8(
    base::Vector<const uint8_t> string, unibrow::Utf8Variant utf8_variant,
    AllocationType allocation) {
  if (string.size() > kMaxInt) {
    // The Utf8Decode can't handle longer inputs, and we couldn't create
    // strings from them anyway.
    THROW_NEW_ERROR(isolate(), NewInvalidStringLengthError());
  }
  auto peek_bytes = [&]() -> base::Vector<const uint8_t> { return string; };
  return NewStringFromUtf8Variant(isolate(), peek_bytes, utf8_variant,
                                  allocation);
}

MaybeHandle<String> Factory::NewStringFromUtf8(base::Vector<const char> string,
                                               AllocationType allocation) {
  return NewStringFromUtf8(base::Vector<const uint8_t>::cast(string),
                           unibrow::Utf8Variant::kLossyUtf8, allocation);
}

#if V8_ENABLE_WEBASSEMBLY
MaybeHandle<String> Factory::NewStringFromUtf8(
    DirectHandle<WasmArray> array, uint32_t start, uint32_t end,
    unibrow::Utf8Variant utf8_variant, AllocationType allocation) {
  DCHECK_EQ(sizeof(uint8_t), array->type()->element_type().value_kind_size());
  DCHECK_LE(start, end);
  DCHECK_LE(end, array->length());
  // {end - start} can never be more than what the Utf8Decoder can handle.
  static_assert(WasmArray::MaxLength(sizeof(uint8_t)) <= kMaxInt);
  auto peek_bytes = [&]() -> base::Vector<const uint8_t> {
    const uint8_t* contents =
        reinterpret_cast<const uint8_t*>(array->ElementAddress(0));
    return {contents + start, end - start};
  };
  return NewStringFromUtf8Variant(isolate(), peek_bytes, utf8_variant,
                                  allocation);
}

MaybeHandle<String> Factory::NewStringFromUtf8(
    DirectHandle<ByteArray> array, uint32_t start, uint32_t end,
    unibrow::Utf8Variant utf8_variant, AllocationType allocation) {
  DCHECK_LE(start, end);
  DCHECK_LE(end, array->length());
  // {end - start} can never be more than what the Utf8Decoder can handle.
  static_assert(ByteArray::kMaxLength <= kMaxInt);
  auto peek_bytes = [&]() -> base::Vector<const uint8_t> {
    const uint8_t* contents = reinterpret_cast<const uint8_t*>(array->begin());
    return {contents + start, end - start};
  };
  return NewStringFromUtf8Variant(isolate(), peek_bytes, utf8_variant,
                                  allocation);
}

namespace {
struct Wtf16Decoder {
  int length_;
  bool is_one_byte_;
  explicit Wtf16Decoder(base::Vector<const uint16_t> data)
      : length_(data.length()),
        is_one_byte_(String::IsOneByte(data.begin(), length_)) {}
  bool is_invalid() const { return false; }
  bool is_one_byte() const { return is_one_byte_; }
  int utf16_length() const { return length_; }
  template <typename Char>
  void Decode(Char* out, base::Vector<const uint16_t> data) {
    CopyChars(out, data.begin(), length_);
  }
};
}  // namespace

MaybeHandle<String> Factory::NewStringFromUtf16(DirectHandle<WasmArray> array,
                                                uint32_t start, uint32_t end,
                                                AllocationType allocation) {
  DCHECK_EQ(sizeof(uint16_t), array->type()->element_type().value_kind_size());
  DCHECK_LE(start, end);
  DCHECK_LE(end, array->length());
  // {end - start} can never be more than what the Utf8Decoder can handle.
  static_assert(WasmArray::MaxLength(sizeof(uint16_t)) <= kMaxInt);
  auto peek_bytes = [&]() -> base::Vector<const uint16_t> {
    const uint16_t* contents =
        reinterpret_cast<const uint16_t*>(array->ElementAddress(0));
    return {contents + start, end - start};
  };
  return NewStringFromBytes<Wtf16Decoder>(isolate(), peek_bytes, allocation,
                                          MessageTemplate::kNone);
}
#endif  // V8_ENABLE_WEBASSEMBLY

MaybeHandle<String> Factory::NewStringFromUtf8SubString(
    Handle<SeqOneByteString> str, int begin, int length,
    AllocationType allocation) {
  base::Vector<const uint8_t> utf8_data;
  {
    DisallowGarbageCollection no_gc;
    utf8_data =
        base::Vector<const uint8_t>(str->GetChars(no_gc) + begin, length);
  }
  Utf8Decoder decoder(utf8_data);

  if (length == 1) {
    uint16_t t;
    // Decode even in the case of length 1 since it can be a bad character.
    decoder.Decode(&t, utf8_data);
    return LookupSingleCharacterStringFromCode(t);
  }

  if (decoder.is_ascii()) {
    // If the string is ASCII, we can just make a substring.
    // TODO(v8): the allocation flag is ignored in this case.
    return NewSubString(str, begin, begin + length);
  }

  DCHECK_GT(decoder.utf16_length(), 0);

  if (decoder.is_one_byte()) {
    // Allocate string.
    Handle<SeqOneByteString> result;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate(), result,
        NewRawOneByteString(decoder.utf16_length(), allocation));
    DisallowGarbageCollection no_gc;
    // Update pointer references, since the original string may have moved after
    // allocation.
    utf8_data =
        base::Vector<const uint8_t>(str->GetChars(no_gc) + begin, length);
    decoder.Decode(result->GetChars(no_gc), utf8_data);
    return result;
  }

  // Allocate string.
  Handle<SeqTwoByteString> result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate(), result,
      NewRawTwoByteString(decoder.utf16_length(), allocation));

  DisallowGarbageCollection no_gc;
  // Update pointer references, since the original string may have moved after
  // allocation.
  utf8_data = base::Vector<const uint8_t>(str->GetChars(no_gc) + begin, length);
  decoder.Decode(result->GetChars(no_gc), utf8_data);
  return result;
}

MaybeHandle<String> Factory::NewStringFromTwoByte(const base::uc16* string,
                                                  int length,
                                                  AllocationType allocation) {
  DCHECK_NE(allocation, AllocationType::kReadOnly);
  if (length == 0) return empty_string();
  if (String::IsOneByte(string, length)) {
    if (length == 1) return LookupSingleCharacterStringFromCode(string[0]);
    Handle<SeqOneByteString> result;
    ASSIGN_RETURN_ON_EXCEPTION(isolate(), result,
                               NewRawOneByteString(length, allocation));
    DisallowGarbageCollection no_gc;
    CopyChars(result->GetChars(no_gc), string, length);
    return result;
  } else {
    Handle<SeqTwoByteString> result;
    ASSIGN_RETURN_ON_EXCEPTION(isolate(), result,
                               NewRawTwoByteString(length, allocation));
    DisallowGarbageCollection no_gc;
    CopyChars(result->GetChars(no_gc), string, length);
    return result;
  }
}

MaybeHandle<String> Factory::NewStringFromTwoByte(
    base::Vector<const base::uc16> string, AllocationType allocation) {
  return NewStringFromTwoByte(string.begin(), string.length(), allocation);
}

MaybeHandle<String> Factory::NewStringFromTwoByte(
    const ZoneVector<base::uc16>* string, AllocationType allocation) {
  return NewStringFromTwoByte(string->data(), static_cast<int>(string->size()),
                              allocation);
}

#if V8_ENABLE_WEBASSEMBLY
MaybeHandle<String> Factory::NewStringFromTwoByteLittleEndian(
    base::Vector<const base::uc16> str, AllocationType allocation) {
#if defined(V8_TARGET_LITTLE_ENDIAN)
  return NewStringFromTwoByte(str, allocation);
#elif defined(V8_TARGET_BIG_ENDIAN)
  // TODO(12868): Duplicate the guts of NewStringFromTwoByte, so that
  // copying and transcoding the data can be done in a single pass.
  UNIMPLEMENTED();
#else
#error Unknown endianness
#endif
}
#endif  // V8_ENABLE_WEBASSEMBLY

Handle<String> Factory::NewInternalizedStringImpl(DirectHandle<String> string,
                                                  int len,
                                                  uint32_t hash_field) {
  if (string->IsOneByteRepresentation()) {
    Handle<SeqOneByteString> result =
        AllocateRawOneByteInternalizedString(len, hash_field);
    DisallowGarbageCollection no_gc;
    String::WriteToFlat(*string, result->GetChars(no_gc), 0, len);
    return result;
  }

  Handle<SeqTwoByteString> result =
      AllocateRawTwoByteInternalizedString(len, hash_field);
  DisallowGarbageCollection no_gc;
  String::WriteToFlat(*string, result->GetChars(no_gc), 0, len);
  return result;
}

StringTransitionStrategy Factory::ComputeInternalizationStrategyForString(
    DirectHandle<String> string, MaybeDirectHandle<Map>* internalized_map) {
  // The serializer requires internalized strings to be in ReadOnlySpace s.t.
  // other objects referencing the string can be allocated in RO space
  // themselves.
  if (isolate()->enable_ro_allocation_for_snapshot() &&
      isolate()->serializer_enabled()) {
    return StringTransitionStrategy::kCopy;
  }
  // Do not internalize young strings in-place: This allows us to ignore both
  // string table and stub cache on scavenges.
  if (Heap::InYoungGeneration(*string)) {
    return StringTransitionStrategy::kCopy;
  }
  // If the string table is shared, we need to copy if the string is not already
  // in the shared heap.
  if (v8_flags.shared_string_table && !InAnySharedSpace(*string)) {
    return StringTransitionStrategy::kCopy;
  }
  DCHECK_NOT_NULL(internalized_map);
  DisallowGarbageCollection no_gc;
  // This method may be called concurrently, so snapshot the map from the input
  // string instead of the calling IsType methods on HeapObject, which would
  // reload the map each time.
  Tagged<Map> map = string->map();
  *internalized_map = GetInPlaceInternalizedStringMap(map);
  if (!internalized_map->is_null()) {
    return StringTransitionStrategy::kInPlace;
  }
  if (InstanceTypeChecker::IsInternalizedString(map)) {
    return StringTransitionStrategy::kAlreadyTransitioned;
  }
  return StringTransitionStrategy::kCopy;
}

template <class StringClass>
Handle<StringClass> Factory::InternalizeExternalString(
    DirectHandle<String> string) {
  DirectHandle<Map> map =
      GetInPlaceInternalizedStringMap(string->map()).ToHandleChecked();
  Tagged<StringClass> external_string =
      Cast<StringClass>(New(map, AllocationType::kOld));
  DisallowGarbageCollection no_gc;
  external_string->InitExternalPointerFields(isolate());
  Tagged<StringClass> cast_string = Cast<StringClass>(*string);
  external_string->set_length(cast_string->length());
  external_string->set_raw_hash_field(cast_string->raw_hash_field());
  external_string->SetResource(isolate(), nullptr);
  isolate()->heap()->RegisterExternalString(external_string);
  return handle(external_string, isolate());
}

template Handle<ExternalOneByteString> Factory::InternalizeExternalString<
    ExternalOneByteString>(DirectHandle<String>);
template Handle<ExternalTwoByteString> Factory::InternalizeExternalString<
    ExternalTwoByteString>(DirectHandle<String>);

StringTransitionStrategy Factory::ComputeSharingStrategyForString(
    DirectHandle<String> string, MaybeDirectHandle<Map>* shared_map) {
  DCHECK(v8_flags.shared_string_table);
  // TODO(pthier): Avoid copying LO-space strings. Update page flags instead.
  if (!InAnySharedSpace(*string)) {
    return StringTransitionStrategy::kCopy;
  }
  DCHECK_NOT_NULL(shared_map);
  DisallowGarbageCollection no_gc;
  InstanceType instance_type = string->map()->instance_type();
  if (StringShape(instance_type).IsShared()) {
    return StringTransitionStrategy::kAlreadyTransitioned;
  }
  switch (instance_type) {
    case SEQ_TWO_BYTE_STRING_TYPE:
      *shared_map = read_only_roots().shared_seq_two_byte_string_map_handle();
      return StringTransitionStrategy::kInPlace;
    case SEQ_ONE_BYTE_STRING_TYPE:
      *shared_map = read_only_roots().shared_seq_one_byte_string_map_handle();
      return StringTransitionStrategy::kInPlace;
    case EXTERNAL_TWO_BYTE_STRING_TYPE:
      *shared_map =
          read_only_roots().shared_external_two_byte_string_map_handle();
      return StringTransitionStrategy::kInPlace;
    case EXTERNAL_ONE_BYTE_STRING_TYPE:
      *shared_map =
          read_only_roots().shared_external_one_byte_string_map_handle();
      return StringTransitionStrategy::kInPlace;
    case UNCACHED_EXTERNAL_TWO_BYTE_STRING_TYPE:
      *shared_map = read_only_roots()
                        .shared_uncached_external_two_byte_string_map_handle();
      return StringTransitionStrategy::kInPlace;
    case UNCACHED_EXTERNAL_ONE_BYTE_STRING_TYPE:
      *shared_map = read_only_roots()
                        .shared_uncached_external_one_byte_string_map_handle();
      return StringTransitionStrategy::kInPlace;
    default:
      return StringTransitionStrategy::kCopy;
  }
}

Handle<String> Factory::NewSurrogatePairString(uint16_t lead, uint16_t trail) {
  DCHECK_GE(lead, 0xD800);
  DCHECK_LE(lead, 0xDBFF);
  DCHECK_GE(trail, 0xDC00);
  DCHECK_LE(trail, 0xDFFF);

  Handle<SeqTwoByteString> str =
      isolate()->factory()->NewRawTwoByteString(2).ToHandleChecked();
  DisallowGarbageCollection no_gc;
  base::uc16* dest = str->GetChars(no_gc);
  dest[0] = lead;
  dest[1] = trail;
  return str;
}

Handle<String> Factory::NewCopiedSubstring(DirectHandle<String> str, int begin,
                                           int length) {
  DCHECK(str->IsFlat());  // Callers must flatten.
  DCHECK_GT(length, 0);   // Callers must handle empty string.
  bool one_byte;
  {
    DisallowGarbageCollection no_gc;
    String::FlatContent flat = str->GetFlatContent(no_gc);
    if (flat.IsOneByte()) {
      one_byte = true;
    } else {
      one_byte = String::IsOneByte(flat.ToUC16Vector().data() + begin, length);
    }
  }
  if (one_byte) {
    Handle<SeqOneByteString> result =
        NewRawOneByteString(length).ToHandleChecked();
    DisallowGarbageCollection no_gc;
    uint8_t* dest = result->GetChars(no_gc);
    String::WriteToFlat(*str, dest, begin, length);
    return result;
  } else {
    Handle<SeqTwoByteString> result =
        NewRawTwoByteString(length).ToHandleChecked();
    DisallowGarbageCollection no_gc;
    base::uc16* dest = result->GetChars(no_gc);
    String::WriteToFlat(*str, dest, begin, length);
    return result;
  }
}

Handle<String> Factory::NewProperSubString(Handle<String> str, int begin,
                                           int end) {
#if VERIFY_HEAP
  if (v8_flags.verify_heap) str->StringVerify(isolate());
#endif
  DCHECK(begin > 0 || end < str->length());

  str = String::Flatten(isolate(), str);

  int length = end - begin;
  if (length <= 0) return empty_string();
  if (length == 1) {
    return LookupSingleCharacterStringFromCode(str->Get(begin));
  }
  if (length == 2) {
    // Optimization for 2-byte strings often used as keys in a decompression
    // dictionary.  Check whether we already have the string in the string
    // table to prevent creation of many unnecessary strings.
    uint16_t c1 = str->Get(begin);
    uint16_t c2 = str->Get(begin + 1);
    return MakeOrFindTwoCharacterString(c1, c2);
  }

  if (!v8_flags.string_slices || length < SlicedString::kMinLength) {
    return NewCopiedSubstring(str, begin, length);
  }

  int offset = begin;

  if (IsSlicedString(*str)) {
    auto slice = Cast<SlicedString>(str);
    str = Handle<String>(slice->parent(), isolate());
    offset += slice->offset();
  }
  if (IsThinString(*str)) {
    auto thin = Cast<ThinString>(str);
    str = handle(thin->actual(), isolate());
  }

  DCHECK(IsSeqString(*str) || IsExternalString(*str));
  DirectHandle<Map> map = str->IsOneByteRepresentation()
                              ? sliced_one_byte_string_map()
                              : sliced_two_byte_string_map();
  Tagged<SlicedString> slice =
      Cast<SlicedString>(New(map, AllocationType::kYoung));
  DisallowGarbageCollection no_gc;
  slice->set_raw_hash_field(String::kEmptyHashField);
  slice->set_length(length);
  slice->set_parent(*str);
  slice->set_offset(offset);
  return handle(slice, isolate());
}

MaybeHandle<String> Factory::NewExternalStringFromOneByte(
    const ExternalOneByteString::Resource* resource) {
  size_t length = resource->length();
  if (length > static_cast<size_t>(String::kMaxLength)) {
    THROW_NEW_ERROR(isolate(), NewInvalidStringLengthError());
  }
  if (length == 0) return empty_string();

  DirectHandle<Map> map = resource->IsCacheable()
                              ? external_one_byte_string_map()
                              : uncached_external_one_byte_string_map();
  Tagged<ExternalOneByteString> external_string =
      Cast<ExternalOneByteString>(New(map, AllocationType::kOld));
  DisallowGarbageCollection no_gc;
  external_string->InitExternalPointerFields(isolate());
  external_string->set_length(static_cast<int>(length));
  external_string->set_raw_hash_field(String::kEmptyHashField);
  external_string->SetResource(isolate(), resource);

  isolate()->heap()->RegisterExternalString(external_string);

  return Handle<String>(external_string, isolate());
}

MaybeHandle<String> Factory::NewExternalStringFromTwoByte(
    const ExternalTwoByteString::Resource* resource) {
  size_t length = resource->length();
  if (length > static_cast<size_t>(String::kMaxLength)) {
    THROW_NEW_ERROR(isolate(), NewInvalidStringLengthError());
  }
  if (length == 0) return empty_string();

  DirectHandle<Map> map = resource->IsCacheable()
                              ? external_two_byte_string_map()
                              : uncached_external_two_byte_string_map();
  Tagged<ExternalTwoByteString> string =
      Cast<ExternalTwoByteString>(New(map, AllocationType::kOld));
  DisallowGarbageCollection no_gc;
  string->InitExternalPointerFields(isolate());
  string->set_length(static_cast<int>(length));
  string->set_raw_hash_field(String::kEmptyHashField);
  string->SetResource(isolate(), resource);

  isolate()->heap()->RegisterExternalString(string);

  return Handle<ExternalTwoByteString>(string, isolate());
}

Handle<JSStringIterator> Factory::NewJSStringIterator(Handle<String> string) {
  DirectHandle<Map> map(
      isolate()->native_context()->initial_string_iterator_map(), isolate());
  DirectHandle<String> flat_string = String::Flatten(isolate(), string);
  Handle<JSStringIterator> iterator =
      Cast<JSStringIterator>(NewJSObjectFromMap(map));

  DisallowGarbageCollection no_gc;
  Tagged<JSStringIterator> raw = *iterator;
  raw->set_string(*flat_string);
  raw->set_index(0);
  return iterator;
}

Tagged<Symbol> Factory::NewSymbolInternal(AllocationType allocation) {
  DCHECK(allocation != AllocationType::kYoung);
  // Statically ensure that it is safe to allocate symbols in paged spaces.
  static_assert(sizeof(Symbol) <= kMaxRegularHeapObjectSize);

  Tagged<Symbol> symbol = Cast<Symbol>(AllocateRawWithImmortalMap(
      sizeof(Symbol), allocation, read_only_roots().symbol_map()));
  DisallowGarbageCollection no_gc;
  // Generate a random hash value.
  int hash = isolate()->GenerateIdentityHash(Name::HashBits::kMax);
  symbol->set_raw_hash_field(
      Name::CreateHashFieldValue(hash, Name::HashFieldType::kHash));
  if (isolate()->read_only_heap()->roots_init_complete()) {
    symbol->set_description(read_only_roots().undefined_value(),
                            SKIP_WRITE_BARRIER);
  } else {
    // Can't use setter during bootstrapping as its typecheck tries to access
    // the roots table before it is initialized.
    symbol->description_.store(&*symbol, read_only_roots().undefined_value(),
                               SKIP_WRITE_BARRIER);
  }
  symbol->set_flags(0);
  DCHECK(!symbol->is_private());
  return symbol;
}

Handle<Symbol> Factory::NewSymbol(AllocationType allocation) {
  return handle(NewSymbolInternal(allocation), isolate());
}

Handle<Symbol> Factory::NewPrivateSymbol(AllocationType allocation) {
  DCHECK(allocation != AllocationType::kYoung);
  Tagged<Symbol> symbol = NewSymbolInternal(allocation);
  DisallowGarbageCollection no_gc;
  symbol->set_is_private(true);
  return handle(symbol, isolate());
}

Handle<Symbol> Factory::NewPrivateNameSymbol(DirectHandle<String> name) {
  Tagged<Symbol> symbol = NewSymbolInternal();
  DisallowGarbageCollection no_gc;
  symbol->set_is_private_name();
  symbol->set_description(*name);
  return handle(symbol, isolate());
}

Tagged<Context> Factory::NewContextInternal(DirectHandle<Map> map, int size,
                                            int variadic_part_length,
                                            AllocationType allocation) {
  DCHECK_LE(Context::kTodoHeaderSize, size);
  DCHECK(IsAligned(size, kTaggedSize));
  DCHECK_LE(Context::MIN_CONTEXT_SLOTS, variadic_part_length);
  DCHECK_LE(Context::SizeFor(variadic_part_length), size);

  Tagged<HeapObject> result =
      allocator()->AllocateRawWith<HeapAllocator::kRetryOrFail>(size,
                                                                allocation);
  result->set_map_after_allocation(*map);
  DisallowGarbageCollection no_gc;
  Tagged<Context> context = Cast<Context>(result);
  context->set_length(variadic_part_length);
  DCHECK_EQ(context->SizeFromMap(*map), size);
  if (size > Context::kTodoHeaderSize) {
    ObjectSlot start = context->RawField(Context::kTodoHeaderSize);
    ObjectSlot end = context->RawField(size);
    size_t slot_count = end - start;
    MemsetTagged(start, *undefined_value(), slot_count);
  }
  return context;
}

// Creates new maps and new native context and wires them up.
//
// +-+------------->|NativeContext|
// | |                    |
// | |                   map
// | |                    v
// | |              |context_map| <Map(NATIVE_CONTEXT_TYPE)>
// | |                  |   |
// | +--native_context--+  map
// |                        v
// |   +------->|contextful_meta_map| <Map(MAP_TYPE)>
// |   |             |      |
// |   +-----map-----+      |
// |                        |
// +-----native_context-----+
//
Handle<NativeContext> Factory::NewNativeContext() {
  // All maps that belong to this new native context will have this meta map.
  // The native context does not exist yet, so create the map as contextless
  // for now.
  Handle<Map> contextful_meta_map = NewContextlessMap(MAP_TYPE, Map::kSize);
  contextful_meta_map->set_map(*contextful_meta_map);

  Handle<Map> context_map = NewMapWithMetaMap(
      contextful_meta_map, NATIVE_CONTEXT_TYPE, kVariableSizeSentinel);

  if (v8_flags.log_maps) {
    LOG(isolate(),
        MapEvent("NewNativeContext", isolate()->factory()->meta_map(),
                 contextful_meta_map, "contextful meta map"));
    LOG(isolate(),
        MapEvent("NewNativeContext", isolate()->factory()->meta_map(),
                 context_map, "native context map"));
  }

  Tagged<NativeContext> context = Cast<NativeContext>(NewContextInternal(
      context_map, NativeContext::kSize, NativeContext::NATIVE_CONTEXT_SLOTS,
      AllocationType::kOld));
  DisallowGarbageCollection no_gc;
  contextful_meta_map->set_native_context(context);
  context_map->set_native_context(context);
  context->set_meta_map(*contextful_meta_map);
  context->set_scope_info(*native_scope_info());
  context->set_previous(Context());
  context->set_extension(*undefined_value());
  context->set_errors_thrown(Smi::zero());
  context->set_is_wasm_js_installed(Smi::zero());
  context->set_is_wasm_jspi_installed(Smi::zero());
  context->set_math_random_index(Smi::zero());
  context->set_serialized_objects(*empty_fixed_array());
  context->init_microtask_queue(isolate(), nullptr);
  context->set_retained_maps(*empty_weak_array_list());
  return handle(context, isolate());
}

Handle<Context> Factory::NewScriptContext(DirectHandle<NativeContext> outer,
                                          DirectHandle<ScopeInfo> scope_info) {
  DCHECK(scope_info->is_script_scope());
  int variadic_part_length = scope_info->ContextLength();

  DirectHandle<FixedArray> side_data;
  if (v8_flags.const_tracking_let) {
    side_data = NewFixedArray(scope_info->ContextLocalCount());
  } else {
    side_data = empty_fixed_array();
  }
  Tagged<Context> context =
      NewContextInternal(handle(outer->script_context_map(), isolate()),
                         Context::SizeFor(variadic_part_length),
                         variadic_part_length, AllocationType::kOld);
  DisallowGarbageCollection no_gc;
  context->set_scope_info(*scope_info);
  context->set_previous(*outer);
  context->set(Context::CONST_TRACKING_LET_SIDE_DATA_INDEX, *side_data);
  DCHECK(context->IsScriptContext());
  return handle(context, isolate());
}

Handle<ScriptContextTable> Factory::NewScriptContextTable() {
  static constexpr int kInitialCapacity = 0;
  return ScriptContextTable::New(isolate(), kInitialCapacity);
}

Handle<Context> Factory::NewModuleContext(DirectHandle<SourceTextModule> module,
                                          DirectHandle<NativeContext> outer,
                                          DirectHandle<ScopeInfo> scope_info) {
  // TODO(v8:13567): Const tracking let in module contexts.
  DCHECK_EQ(scope_info->scope_type(), MODULE_SCOPE);
  int variadic_part_length = scope_info->ContextLength();
  Tagged<Context> context = NewContextInternal(
      isolate()->module_context_map(), Context::SizeFor(variadic_part_length),
      variadic_part_length, AllocationType::kOld);
  DisallowGarbageCollection no_gc;
  context->set_scope_info(*scope_info);
  context->set_previous(*outer);
  context->set_extension(*module);
  DCHECK(context->IsModuleContext());
  return handle(context, isolate());
}

Handle<Context> Factory::NewFunctionContext(
    DirectHandle<Context> outer, DirectHandle<ScopeInfo> scope_info) {
  DirectHandle<Map> map;
  switch (scope_info->scope_type()) {
    case EVAL_SCOPE:
      map = isolate()->eval_context_map();
      break;
    case FUNCTION_SCOPE:
      map = isolate()->function_context_map();
      break;
    default:
      UNREACHABLE();
  }
  int variadic_part_length = scope_info->ContextLength();
  Tagged<Context> context =
      NewContextInternal(map, Context::SizeFor(variadic_part_length),
                         variadic_part_length, AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  context->set_scope_info(*scope_info);
  context->set_previous(*outer);
  return handle(context, isolate());
}

#if V8_SINGLE_GENERATION_BOOL
#define DCHECK_NEWLY_ALLOCATED_OBJECT_IS_YOUNG(isolate, object)
#elif V8_ENABLE_STICKY_MARK_BITS_BOOL
#define DCHECK_NEWLY_ALLOCATED_OBJECT_IS_YOUNG(isolate, object)             \
  DCHECK_IMPLIES(!isolate->heap()->incremental_marking()->IsMajorMarking(), \
                 Heap::InYoungGeneration(object))
#else
#define DCHECK_NEWLY_ALLOCATED_OBJECT_IS_YOUNG(isolate, object) \
  DCHECK(Heap::InYoungGeneration(object))
#endif

Handle<Context> Factory::NewCatchContext(DirectHandle<Context> previous,
                                         DirectHandle<ScopeInfo> scope_info,
                                         DirectHandle<Object> thrown_object) {
  DCHECK_EQ(scope_info->scope_type(), CATCH_SCOPE);
  static_assert(Context::MIN_CONTEXT_SLOTS == Context::THROWN_OBJECT_INDEX);
  // TODO(ishell): Take the details from CatchContext class.
  int variadic_part_length = Context::MIN_CONTEXT_SLOTS + 1;
  Tagged<Context> context = NewContextInternal(
      isolate()->catch_context_map(), Context::SizeFor(variadic_part_length),
      variadic_part_length, AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  DCHECK_NEWLY_ALLOCATED_OBJECT_IS_YOUNG(isolate(), context);
  context->set_scope_info(*scope_info, SKIP_WRITE_BARRIER);
  context->set_previous(*previous, SKIP_WRITE_BARRIER);
  context->set(Context::THROWN_OBJECT_INDEX, *thrown_object,
               SKIP_WRITE_BARRIER);
  return handle(context, isolate());
}

Handle<Context> Factory::NewDebugEvaluateContext(
    DirectHandle<Context> previous, DirectHandle<ScopeInfo> scope_info,
    DirectHandle<JSReceiver> extension, DirectHandle<Context> wrapped) {
  DCHECK(scope_info->IsDebugEvaluateScope());
  DirectHandle<HeapObject> ext = extension.is_null()
                                     ? Cast<HeapObject>(undefined_value())
                                     : Cast<HeapObject>(extension);
  // TODO(ishell): Take the details from DebugEvaluateContextContext class.
  int variadic_part_length = Context::MIN_CONTEXT_EXTENDED_SLOTS + 1;
  Tagged<Context> context =
      NewContextInternal(isolate()->debug_evaluate_context_map(),
                         Context::SizeFor(variadic_part_length),
                         variadic_part_length, AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  DCHECK_NEWLY_ALLOCATED_OBJECT_IS_YOUNG(isolate(), context);
  context->set_scope_info(*scope_info, SKIP_WRITE_BARRIER);
  context->set_previous(*previous, SKIP_WRITE_BARRIER);
  context->set_extension(*ext, SKIP_WRITE_BARRIER);
  if (!wrapped.is_null()) {
    context->set(Context::WRAPPED_CONTEXT_INDEX, *wrapped, SKIP_WRITE_BARRIER);
  }
  return handle(context, isolate());
}

Handle<Context> Factory::NewWithContext(DirectHandle<Context> previous,
                                        DirectHandle<ScopeInfo> scope_info,
                                        DirectHandle<JSReceiver> extension) {
  DCHECK_EQ(scope_info->scope_type(), WITH_SCOPE);
  // TODO(ishell): Take the details from WithContext class.
  int variadic_part_length = Context::MIN_CONTEXT_EXTENDED_SLOTS;
  Tagged<Context> context = NewContextInternal(
      isolate()->with_context_map(), Context::SizeFor(variadic_part_length),
      variadic_part_length, AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  DCHECK_NEWLY_ALLOCATED_OBJECT_IS_YOUNG(isolate(), context);
  context->set_scope_info(*scope_info, SKIP_WRITE_BARRIER);
  context->set_previous(*previous, SKIP_WRITE_BARRIER);
  context->set_extension(*extension, SKIP_WRITE_BARRIER);
  return handle(context, isolate());
}

Handle<Context> Factory::NewBlockContext(DirectHandle<Context> previous,
                                         DirectHandle<ScopeInfo> scope_info) {
  DCHECK_IMPLIES(scope_info->scope_type() != BLOCK_SCOPE,
                 scope_info->scope_type() == CLASS_SCOPE);
  int variadic_part_length = scope_info->ContextLength();
  Tagged<Context> context = NewContextInternal(
      isolate()->block_context_map(), Context::SizeFor(variadic_part_length),
      variadic_part_length, AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  DCHECK_NEWLY_ALLOCATED_OBJECT_IS_YOUNG(isolate(), context);
  context->set_scope_info(*scope_info, SKIP_WRITE_BARRIER);
  context->set_previous(*previous, SKIP_WRITE_BARRIER);
  return handle(context, isolate());
}

Handle<Context> Factory::NewBuiltinContext(
    DirectHandle<NativeContext> native_context, int variadic_part_length) {
  DCHECK_LE(Context::MIN_CONTEXT_SLOTS, variadic_part_length);
  Tagged<Context> context = NewContextInternal(
      isolate()->function_context_map(), Context::SizeFor(variadic_part_length),
      variadic_part_length, AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  DCHECK_NEWLY_ALLOCATED_OBJECT_IS_YOUNG(isolate(), context);
  context->set_scope_info(read_only_roots().empty_scope_info(),
                          SKIP_WRITE_BARRIER);
  context->set_previous(*native_context, SKIP_WRITE_BARRIER);
  return handle(context, isolate());
}

Handle<AliasedArgumentsEntry> Factory::NewAliasedArgumentsEntry(
    int aliased_context_slot) {
  auto entry = NewStructInternal<AliasedArgumentsEntry>(
      ALIASED_ARGUMENTS_ENTRY_TYPE, AllocationType::kYoung);
  entry->set_aliased_context_slot(aliased_context_slot);
  return handle(entry, isolate());
}

Handle<AccessorInfo> Factory::NewAccessorInfo() {
  Tagged<AccessorInfo> info =
      Cast<AccessorInfo>(New(accessor_info_map(), AllocationType::kOld));
  DisallowGarbageCollection no_gc;
  info->set_name(*empty_string(), SKIP_WRITE_BARRIER);
  info->set_data(*undefined_value(), SKIP_WRITE_BARRIER);
  info->set_flags(0);  // Must clear the flags, it was initialized as undefined.
  info->set_is_sloppy(true);
  info->set_initial_property_attributes(NONE);

  info->init_getter(isolate(), kNullAddress);
  info->init_setter(isolate(), kNullAddress);

  info->clear_padding();

  return handle(info, isolate());
}

Handle<ErrorStackData> Factory::NewErrorStackData(
    DirectHandle<UnionOf<JSAny, FixedArray>> call_site_infos_or_formatted_stack,
    DirectHandle<UnionOf<Smi, FixedArray>> limit_or_stack_frame_infos) {
  Tagged<ErrorStackData> error_stack_data = NewStructInternal<ErrorStackData>(
      ERROR_STACK_DATA_TYPE, AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  error_stack_data->set_call_site_infos_or_formatted_stack(
      *call_site_infos_or_formatted_stack, SKIP_WRITE_BARRIER);
  error_stack_data->set_limit_or_stack_frame_infos(*limit_or_stack_frame_infos,
                                                   SKIP_WRITE_BARRIER);
  return handle(error_stack_data, isolate());
}

void Factory::ProcessNewScript(Handle<Script> script,
                               ScriptEventType script_event_type) {
  int script_id = script->id();
  if (script_id != Script::kTemporaryScriptId) {
    Handle<WeakArrayList> scripts = script_list();
    scripts = WeakArrayList::Append(isolate(), scripts,
                                    MaybeObjectHandle::Weak(script),
                                    AllocationType::kOld);
    isolate()->heap()->set_script_list(*scripts);
  }
  if (IsString(script->source()) && isolate()->NeedsSourcePositions()) {
    Script::InitLineEnds(isolate(), script);
  }
  LOG(isolate(), ScriptEvent(script_event_type, script_id));
}

Handle<Script> Factory::CloneScript(DirectHandle<Script> script,
                                    DirectHandle<String> source) {
  int script_id = isolate()->GetNextScriptId();
#ifdef V8_SCRIPTORMODULE_LEGACY_LIFETIME
  Handle<ArrayList> list = ArrayList::New(isolate(), 0);
#endif
  Handle<Script> new_script_handle =
      Cast<Script>(NewStruct(SCRIPT_TYPE, AllocationType::kOld));
  {
    DisallowGarbageCollection no_gc;
    Tagged<Script> new_script = *new_script_handle;
    const Tagged<Script> old_script = *script;
    new_script->set_source(*source);
    new_script->set_name(old_script->name());
    new_script->set_id(script_id);
    new_script->set_line_offset(old_script->line_offset());
    new_script->set_column_offset(old_script->column_offset());
    new_script->set_context_data(old_script->context_data());
    new_script->set_type(old_script->type());
    new_script->set_line_ends(Smi::zero());
    new_script->set_eval_from_shared_or_wrapped_arguments(
        script->eval_from_shared_or_wrapped_arguments());
    new_script->set_infos(*empty_weak_fixed_array(), SKIP_WRITE_BARRIER);
    new_script->set_eval_from_position(old_script->eval_from_position());
    new_script->set_flags(old_script->flags());
    new_script->set_host_defined_options(old_script->host_defined_options());
    new_script->set_source_hash(*undefined_value(), SKIP_WRITE_BARRIER);
    new_script->set_compiled_lazy_function_positions(*undefined_value(),
                                                     SKIP_WRITE_BARRIER);
#ifdef V8_SCRIPTORMODULE_LEGACY_LIFETIME
    new_script->set_script_or_modules(*list);
#endif
  }
  ProcessNewScript(new_script_handle, ScriptEventType::kCreate);
  return new_script_handle;
}

Handle<CallableTask> Factory::NewCallableTask(DirectHandle<JSReceiver> callable,
                                              DirectHandle<Context> context) {
  DCHECK(IsCallable(*callable));
  auto microtask = NewStructInternal<CallableTask>(CALLABLE_TASK_TYPE,
                                                   AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  microtask->set_callable(*callable, SKIP_WRITE_BARRIER);
  microtask->set_context(*context, SKIP_WRITE_BARRIER);
#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  microtask->set_continuation_preserved_embedder_data(
      isolate()->isolate_data()->continuation_preserved_embedder_data(),
      SKIP_WRITE_BARRIER);
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  return handle(microtask, isolate());
}

Handle<CallbackTask> Factory::NewCallbackTask(DirectHandle<Foreign> callback,
                                              DirectHandle<Foreign> data) {
  auto microtask = NewStructInternal<CallbackTask>(CALLBACK_TASK_TYPE,
                                                   AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  microtask->set_callback(*callback, SKIP_WRITE_BARRIER);
  microtask->set_data(*data, SKIP_WRITE_BARRIER);
#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  microtask->set_continuation_preserved_embedder_data(
      isolate()->isolate_data()->continuation_preserved_embedder_data(),
      SKIP_WRITE_BARRIER);
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  return handle(microtask, isolate());
}

Handle<PromiseResolveThenableJobTask> Factory::NewPromiseResolveThenableJobTask(
    DirectHandle<JSPromise> promise_to_resolve,
    DirectHandle<JSReceiver> thenable, DirectHandle<JSReceiver> then,
    DirectHandle<Context> context) {
  DCHECK(IsCallable(*then));
  auto microtask = NewStructInternal<PromiseResolveThenableJobTask>(
      PROMISE_RESOLVE_THENABLE_JOB_TASK_TYPE, AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  microtask->set_promise_to_resolve(*promise_to_resolve, SKIP_WRITE_BARRIER);
  microtask->set_thenable(*thenable, SKIP_WRITE_BARRIER);
  microtask->set_then(*then, SKIP_WRITE_BARRIER);
  microtask->set_context(*context, SKIP_WRITE_BARRIER);
#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  microtask->set_continuation_preserved_embedder_data(
      isolate()->isolate_data()->continuation_preserved_embedder_data(),
      SKIP_WRITE_BARRIER);
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  return handle(microtask, isolate());
}

#if V8_ENABLE_WEBASSEMBLY

Handle<WasmTrustedInstanceData> Factory::NewWasmTrustedInstanceData() {
  Tagged<WasmTrustedInstanceData> result =
      Cast<WasmTrustedInstanceData>(AllocateRawWithImmortalMap(
          WasmTrustedInstanceData::kSize, AllocationType::kTrusted,
          read_only_roots().wasm_trusted_instance_data_map()));
  DisallowGarbageCollection no_gc;
  result->init_self_indirect_pointer(isolate());
  result->clear_padding();
  for (int offset : WasmTrustedInstanceData::kTaggedFieldOffsets) {
    result->RawField(offset).store(read_only_roots().undefined_value());
  }
  return handle(result, isolate());
}

Handle<WasmDispatchTable> Factory::NewWasmDispatchTable(int length) {
  CHECK_LE(length, WasmDispatchTable::kMaxLength);
  int bytes = WasmDispatchTable::SizeFor(length);
  Tagged<WasmDispatchTable> result = UncheckedCast<WasmDispatchTable>(
      AllocateRawWithImmortalMap(bytes, AllocationType::kTrusted,
                                 read_only_roots().wasm_dispatch_table_map()));
  result->WriteField<int>(WasmDispatchTable::kLengthOffset, length);
  result->WriteField<int>(WasmDispatchTable::kCapacityOffset, length);
  for (int i = 0; i < length; ++i) {
    result->Clear(i);
    result->clear_entry_padding(i);
  }
  return handle(result, isolate());
}

Handle<WasmTypeInfo> Factory::NewWasmTypeInfo(
    Address type_address, Handle<Map> opt_parent,
    DirectHandle<WasmTrustedInstanceData> opt_trusted_data,
    uint32_t type_index) {
  // We pretenure WasmTypeInfo objects for two reasons:
  // (1) They are referenced by Maps, which are assumed to be long-lived,
  //     so pretenuring the WTI is a bit more efficient.
  // (2) The object visitors need to read the WasmTypeInfo to find tagged
  //     fields in Wasm structs; in the middle of a GC cycle that's only
  //     safe to do if the WTI is in old space.
  std::vector<Handle<Object>> supertypes;
  if (opt_parent.is_null()) {
    supertypes.resize(wasm::kMinimumSupertypeArraySize, undefined_value());
  } else {
    DirectHandle<WasmTypeInfo> parent_type_info(opt_parent->wasm_type_info(),
                                                isolate());
    int first_undefined_index = -1;
    for (int i = 0; i < parent_type_info->supertypes_length(); i++) {
      Handle<Object> supertype =
          handle(parent_type_info->supertypes(i), isolate());
      if (IsUndefined(*supertype) && first_undefined_index == -1) {
        first_undefined_index = i;
      }
      supertypes.emplace_back(supertype);
    }
    if (first_undefined_index >= 0) {
      supertypes[first_undefined_index] = opt_parent;
    } else {
      supertypes.emplace_back(opt_parent);
    }
  }
  Tagged<Map> map = *wasm_type_info_map();
  Tagged<WasmTypeInfo> result = Cast<WasmTypeInfo>(AllocateRawWithImmortalMap(
      WasmTypeInfo::SizeFor(static_cast<int>(supertypes.size())),
      AllocationType::kOld, map));
  DisallowGarbageCollection no_gc;
  result->set_supertypes_length(static_cast<int>(supertypes.size()));
  for (size_t i = 0; i < supertypes.size(); i++) {
    result->set_supertypes(static_cast<int>(i), *supertypes[i]);
  }
  result->init_native_type(isolate(), type_address);
  if (opt_trusted_data.is_null()) {
    result->clear_trusted_data();
  } else {
    result->set_trusted_data(*opt_trusted_data);
  }
  result->set_type_index(type_index);
  return handle(result, isolate());
}

Handle<WasmImportData> Factory::NewWasmImportData(
    DirectHandle<HeapObject> callable, wasm::Suspend suspend,
    MaybeDirectHandle<WasmTrustedInstanceData> instance_data,
    DirectHandle<PodArray<wasm::ValueType>> serialized_sig) {
  Tagged<Map> map = *wasm_import_data_map();
  auto result = Cast<WasmImportData>(AllocateRawWithImmortalMap(
      map->instance_size(), AllocationType::kTrusted, map));
  DisallowGarbageCollection no_gc;
  result->set_native_context(*isolate()->native_context());
  result->set_callable(Cast<UnionOf<Undefined, JSReceiver>>(*callable));
  result->set_suspend(suspend);
  if (instance_data.is_null()) {
    result->clear_instance_data();
  } else {
    result->set_instance_data(*instance_data.ToHandleChecked());
  }
  result->set_wrapper_budget(v8_flags.wasm_wrapper_tiering_budget);
  result->set_call_origin(Smi::FromInt(WasmImportData::kInvalidCallOrigin));
  result->set_sig(*serialized_sig);
  result->set_code(*BUILTIN_CODE(isolate(), Abort));
  return handle(result, isolate());
}

Handle<WasmImportData> Factory::NewWasmImportData(
    DirectHandle<WasmImportData> ref) {
  return NewWasmImportData(handle(ref->callable(), isolate()),
                           static_cast<wasm::Suspend>(ref->suspend()),
                           handle(ref->instance_data(), isolate()),
                           handle(ref->sig(), isolate()));
}

Handle<WasmFastApiCallData> Factory::NewWasmFastApiCallData(
    DirectHandle<HeapObject> signature, DirectHandle<Object> callback_data) {
  Tagged<Map> map = *wasm_fast_api_call_data_map();
  auto result = Cast<WasmFastApiCallData>(AllocateRawWithImmortalMap(
      map->instance_size(), AllocationType::kOld, map));
  result->set_signature(*signature);
  result->set_callback_data(*callback_data);
  result->set_cached_map(read_only_roots().null_value());
  return handle(result, isolate());
}

Handle<WasmInternalFunction> Factory::NewWasmInternalFunction(
    DirectHandle<TrustedObject> implicit_arg, int function_index,
    uintptr_t signature_hash) {
  Tagged<WasmInternalFunction> internal =
      Cast<WasmInternalFunction>(AllocateRawWithImmortalMap(
          WasmInternalFunction::kSize, AllocationType::kTrusted,
          *wasm_internal_function_map()));
  internal->init_self_indirect_pointer(isolate());
  {
    DisallowGarbageCollection no_gc;
    internal->set_call_target(kNullAddress);
    DCHECK(IsWasmTrustedInstanceData(*implicit_arg) ||
           IsWasmImportData(*implicit_arg));
    internal->set_implicit_arg(*implicit_arg);
#if V8_ENABLE_SANDBOX
    internal->set_signature_hash(signature_hash);
#endif  // V8_SANDBOX
    // Default values, will be overwritten by the caller.
    internal->set_function_index(function_index);
    internal->set_external(*undefined_value());
  }

  return handle(internal, isolate());
}

Handle<WasmFuncRef> Factory::NewWasmFuncRef(
    DirectHandle<WasmInternalFunction> internal_function,
    DirectHandle<Map> rtt) {
  Tagged<HeapObject> raw =
      AllocateRaw(WasmFuncRef::kSize, AllocationType::kOld);
  DisallowGarbageCollection no_gc;
  DCHECK_EQ(WASM_FUNC_REF_TYPE, rtt->instance_type());
  DCHECK_EQ(WasmFuncRef::kSize, rtt->instance_size());
  raw->set_map_after_allocation(*rtt);
  Tagged<WasmFuncRef> func_ref = Cast<WasmFuncRef>(raw);
  func_ref->set_internal(*internal_function);
  return handle(func_ref, isolate());
}

Handle<WasmJSFunctionData> Factory::NewWasmJSFunctionData(
    uint32_t canonical_sig_index, DirectHandle<JSReceiver> callable,
    DirectHandle<PodArray<wasm::ValueType>> serialized_sig,
    DirectHandle<Code> wrapper_code, DirectHandle<Map> rtt,
    wasm::Suspend suspend, wasm::Promise promise, uintptr_t signature_hash) {
  DirectHandle<WasmImportData> ref = NewWasmImportData(
      callable, suspend, DirectHandle<WasmTrustedInstanceData>(),
      serialized_sig);

  DirectHandle<WasmInternalFunction> internal =
      NewWasmInternalFunction(ref, -1, signature_hash);
  DirectHandle<WasmFuncRef> func_ref = NewWasmFuncRef(internal, rtt);
  WasmImportData::SetFuncRefAsCallOrigin(ref, func_ref);
  Tagged<Map> map = *wasm_js_function_data_map();
  Tagged<WasmJSFunctionData> result =
      Cast<WasmJSFunctionData>(AllocateRawWithImmortalMap(
          map->instance_size(), AllocationType::kTrusted, map));
  result->init_self_indirect_pointer(isolate());
  DisallowGarbageCollection no_gc;
  result->set_func_ref(*func_ref);
  result->set_internal(*internal);
  result->set_wrapper_code(*wrapper_code);
  result->set_canonical_sig_index(static_cast<int>(canonical_sig_index));
  result->set_js_promise_flags(WasmFunctionData::SuspendField::encode(suspend) |
                               WasmFunctionData::PromiseField::encode(promise));
  return handle(result, isolate());
}

Handle<WasmResumeData> Factory::NewWasmResumeData(
    DirectHandle<WasmSuspenderObject> suspender, wasm::OnResume on_resume) {
  Tagged<Map> map = *wasm_resume_data_map();
  Tagged<WasmResumeData> result =
      Cast<WasmResumeData>(AllocateRawWithImmortalMap(
          map->instance_size(), AllocationType::kOld, map));
  DisallowGarbageCollection no_gc;
  result->set_suspender(*suspender);
  result->set_on_resume(static_cast<int>(on_resume));
  return handle(result, isolate());
}

Handle<WasmSuspenderObject> Factory::NewWasmSuspenderObject() {
  DirectHandle<JSPromise> promise = NewJSPromise();
  Tagged<Map> map = *wasm_suspender_object_map();
  Tagged<WasmSuspenderObject> obj =
      Cast<WasmSuspenderObject>(AllocateRawWithImmortalMap(
          map->instance_size(), AllocationType::kOld, map));
  auto suspender = handle(obj, isolate());
  // Ensure that all properties are initialized before the allocation below.
  suspender->set_continuation(*undefined_value());
  suspender->set_parent(*undefined_value());
  suspender->set_promise(*promise);
  suspender->set_resume(*undefined_value());
  suspender->set_reject(*undefined_value());
  suspender->set_state(WasmSuspenderObject::kInactive);
  suspender->set_has_js_frames(0);
  // Instantiate the callable object which resumes this Suspender. This will be
  // used implicitly as the onFulfilled callback of the returned JS promise.
  DirectHandle<WasmResumeData> resume_data =
      NewWasmResumeData(suspender, wasm::OnResume::kContinue);
  Handle<SharedFunctionInfo> resume_sfi =
      NewSharedFunctionInfoForWasmResume(resume_data);
  Handle<Context> context(isolate()->native_context());
  DirectHandle<JSObject> resume =
      Factory::JSFunctionBuilder{isolate(), resume_sfi, context}.Build();

  DirectHandle<WasmResumeData> reject_data =
      isolate()->factory()->NewWasmResumeData(suspender,
                                              wasm::OnResume::kThrow);
  Handle<SharedFunctionInfo> reject_sfi =
      isolate()->factory()->NewSharedFunctionInfoForWasmResume(reject_data);
  DirectHandle<JSObject> reject =
      Factory::JSFunctionBuilder{isolate(), reject_sfi, context}.Build();
  suspender->set_resume(*resume);
  suspender->set_reject(*reject);
  return suspender;
}

Handle<WasmExportedFunctionData> Factory::NewWasmExportedFunctionData(
    DirectHandle<Code> export_wrapper,
    DirectHandle<WasmTrustedInstanceData> instance_data,
    DirectHandle<WasmFuncRef> func_ref,
    DirectHandle<WasmInternalFunction> internal_function,
    const wasm::FunctionSig* sig, uint32_t canonical_type_index,
    int wrapper_budget, wasm::Promise promise) {
  int func_index = internal_function->function_index();
  DirectHandle<Cell> wrapper_budget_cell =
      NewCell(Smi::FromInt(wrapper_budget));
  Tagged<Map> map = *wasm_exported_function_data_map();
  Tagged<WasmExportedFunctionData> result =
      Cast<WasmExportedFunctionData>(AllocateRawWithImmortalMap(
          map->instance_size(), AllocationType::kTrusted, map));
  result->init_self_indirect_pointer(isolate());
  DisallowGarbageCollection no_gc;
  result->set_func_ref(*func_ref);
  result->set_internal(*internal_function);
  result->set_wrapper_code(*export_wrapper);
  result->set_instance_data(*instance_data);
  result->set_function_index(func_index);
  result->set_sig(sig);
  result->set_canonical_type_index(canonical_type_index);
  result->set_wrapper_budget(*wrapper_budget_cell);
  // We can't skip the write barrier because Code objects are not immovable.
  result->set_c_wrapper_code(*BUILTIN_CODE(isolate(), Illegal),
                             UPDATE_WRITE_BARRIER);
  result->set_packed_args_size(0);
  result->set_js_promise_flags(
      WasmFunctionData::SuspendField::encode(wasm::kNoSuspend) |
      WasmFunctionData::PromiseField::encode(promise));
  return handle(result, isolate());
}

Handle<WasmCapiFunctionData> Factory::NewWasmCapiFunctionData(
    Address call_target, DirectHandle<Foreign> embedder_data,
    DirectHandle<Code> wrapper_code, DirectHandle<Map> rtt,
    DirectHandle<PodArray<wasm::ValueType>> serialized_sig,
    uintptr_t signature_hash) {
  DirectHandle<WasmImportData> ref = NewWasmImportData(
      undefined_value(), wasm::kNoSuspend,
      DirectHandle<WasmTrustedInstanceData>(), serialized_sig);
  DirectHandle<WasmInternalFunction> internal =
      NewWasmInternalFunction(ref, -1, signature_hash);
  DirectHandle<WasmFuncRef> func_ref = NewWasmFuncRef(internal, rtt);
  WasmImportData::SetFuncRefAsCallOrigin(ref, func_ref);
  internal->set_call_target(call_target);
  Tagged<Map> map = *wasm_capi_function_data_map();
  Tagged<WasmCapiFunctionData> result =
      Cast<WasmCapiFunctionData>(AllocateRawWithImmortalMap(
          map->instance_size(), AllocationType::kTrusted, map));
  result->init_self_indirect_pointer(isolate());
  DisallowGarbageCollection no_gc;
  result->set_func_ref(*func_ref);
  result->set_internal(*internal);
  result->set_wrapper_code(*wrapper_code);
  result->set_embedder_data(*embedder_data);
  result->set_serialized_signature(*serialized_sig);
  result->set_js_promise_flags(
      WasmFunctionData::SuspendField::encode(wasm::kNoSuspend) |
      WasmFunctionData::PromiseField::encode(wasm::kNoPromise));
  return handle(result, isolate());
}

Tagged<WasmArray> Factory::NewWasmArrayUninitialized(uint32_t length,
                                                     DirectHandle<Map> map) {
  Tagged<HeapObject> raw =
      AllocateRaw(WasmArray::SizeFor(*map, length), AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  raw->set_map_after_allocation(*map);
  Tagged<WasmArray> result = Cast<WasmArray>(raw);
  result->set_raw_properties_or_hash(*empty_fixed_array(), kRelaxedStore);
  result->set_length(length);
  return result;
}

Handle<WasmArray> Factory::NewWasmArray(wasm::ValueType element_type,
                                        uint32_t length,
                                        wasm::WasmValue initial_value,
                                        DirectHandle<Map> map) {
  Tagged<WasmArray> result = NewWasmArrayUninitialized(length, map);
  DisallowGarbageCollection no_gc;
  if (element_type.is_numeric()) {
    if (initial_value.zero_byte_representation()) {
      memset(reinterpret_cast<void*>(result->ElementAddress(0)), 0,
             length * element_type.value_kind_size());
    } else {
      wasm::WasmValue packed = initial_value.Packed(element_type);
      for (uint32_t i = 0; i < length; i++) {
        Address address = result->ElementAddress(i);
        packed.CopyTo(reinterpret_cast<uint8_t*>(address));
      }
    }
  } else {
    for (uint32_t i = 0; i < length; i++) {
      result->SetTaggedElement(i, initial_value.to_ref());
    }
  }
  return handle(result, isolate());
}

Handle<WasmArray> Factory::NewWasmArrayFromElements(
    const wasm::ArrayType* type, base::Vector<wasm::WasmValue> elements,
    DirectHandle<Map> map) {
  uint32_t length = static_cast<uint32_t>(elements.size());
  Tagged<WasmArray> result = NewWasmArrayUninitialized(length, map);
  DisallowGarbageCollection no_gc;
  if (type->element_type().is_numeric()) {
    for (uint32_t i = 0; i < length; i++) {
      Address address = result->ElementAddress(i);
      elements[i]
          .Packed(type->element_type())
          .CopyTo(reinterpret_cast<uint8_t*>(address));
    }
  } else {
    for (uint32_t i = 0; i < length; i++) {
      result->SetTaggedElement(i, elements[i].to_ref());
    }
  }
  return handle(result, isolate());
}

Handle<WasmArray> Factory::NewWasmArrayFromMemory(uint32_t length,
                                                  DirectHandle<Map> map,
                                                  Address source) {
  wasm::ValueType element_type =
      reinterpret_cast<wasm::ArrayType*>(map->wasm_type_info()->native_type())
          ->element_type();
  DCHECK(element_type.is_numeric());
  Tagged<WasmArray> result = NewWasmArrayUninitialized(length, map);
  DisallowGarbageCollection no_gc;
#if V8_TARGET_BIG_ENDIAN
  MemCopyAndSwitchEndianness(reinterpret_cast<void*>(result->ElementAddress(0)),
                             reinterpret_cast<void*>(source), length,
                             element_type.value_kind_size());
#else
  MemCopy(reinterpret_cast<void*>(result->ElementAddress(0)),
          reinterpret_cast<void*>(source),
          length * element_type.value_kind_size());
#endif

  return handle(result, isolate());
}

Handle<Object> Factory::NewWasmArrayFromElementSegment(
    Handle<WasmTrustedInstanceData> trusted_instance_data,
    Handle<WasmTrustedInstanceData> shared_trusted_instance_data,
    uint32_t segment_index, uint32_t start_offset, uint32_t length,
    DirectHandle<Map> map) {
  DCHECK(WasmArray::type(*map)->element_type().is_reference());

  // If the element segment has not been initialized yet, lazily initialize it
  // now.
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);
  std::optional<MessageTemplate> opt_error = wasm::InitializeElementSegment(
      &zone, isolate(), trusted_instance_data, shared_trusted_instance_data,
      segment_index);
  if (opt_error.has_value()) {
    return handle(Smi::FromEnum(opt_error.value()), isolate());
  }

  DirectHandle<FixedArray> elements =
      handle(Cast<FixedArray>(
                 trusted_instance_data->element_segments()->get(segment_index)),
             isolate());

  Tagged<WasmArray> result = NewWasmArrayUninitialized(length, map);
  DisallowGarbageCollection no_gc;
  if (length > 0) {
    isolate()->heap()->CopyRange(result, result->ElementSlot(0),
                                 elements->RawFieldOfElementAt(start_offset),
                                 length, SKIP_WRITE_BARRIER);
  }
  return handle(result, isolate());
}

#if V8_ENABLE_DRUMBRAKE
Handle<WasmStruct> Factory::NewWasmStructUninitialized(
    const wasm::StructType* type, Handle<Map> map) {
  Tagged<HeapObject> raw =
      AllocateRaw(WasmStruct::Size(type), AllocationType::kYoung);
  raw->set_map_after_allocation(*map);
  Tagged<WasmStruct> result = Cast<WasmStruct>(raw);
  result->set_raw_properties_or_hash(*empty_fixed_array(), kRelaxedStore);
  return handle(result, isolate());
}
#endif  // V8_ENABLE_DRUMBRAKE

Handle<WasmStruct> Factory::NewWasmStruct(const wasm::StructType* type,
                                          wasm::WasmValue* args,
                                          DirectHandle<Map> map) {
  Tagged<HeapObject> raw =
      AllocateRaw(WasmStruct::Size(type), AllocationType::kYoung);
  raw->set_map_after_allocation(*map);
  Tagged<WasmStruct> result = Cast<WasmStruct>(raw);
  result->set_raw_properties_or_hash(*empty_fixed_array(), kRelaxedStore);
  for (uint32_t i = 0; i < type->field_count(); i++) {
    int offset = type->field_offset(i);
    if (type->field(i).is_numeric()) {
      Address address = result->RawFieldAddress(offset);
      args[i]
          .Packed(type->field(i))
          .CopyTo(reinterpret_cast<uint8_t*>(address));
    } else {
      offset += WasmStruct::kHeaderSize;
      TaggedField<Object>::store(result, offset, *args[i].to_ref());
    }
  }
  return handle(result, isolate());
}

Handle<WasmContinuationObject> Factory::NewWasmContinuationObject(
    Address jmpbuf, wasm::StackMemory* stack, DirectHandle<HeapObject> parent,
    AllocationType allocation) {
  Tagged<Map> map = *wasm_continuation_object_map();
  auto result = Cast<WasmContinuationObject>(
      AllocateRawWithImmortalMap(map->instance_size(), allocation, map));
  result->init_jmpbuf(isolate(), jmpbuf);
  result->init_stack(isolate(), reinterpret_cast<Address>(stack));
  result->set_parent(Cast<UnionOf<Undefined, WasmContinuationObject>>(*parent));
  return handle(result, isolate());
}

Handle<SharedFunctionInfo>
Factory::NewSharedFunctionInfoForWasmExportedFunction(
    DirectHandle<String> name, DirectHandle<WasmExportedFunctionData> data) {
  return NewSharedFunctionInfo(name, data, Builtin::kNoBuiltinId);
}

Handle<SharedFunctionInfo> Factory::NewSharedFunctionInfoForWasmJSFunction(
    DirectHandle<String> name, DirectHandle<WasmJSFunctionData> data) {
  return NewSharedFunctionInfo(name, data, Builtin::kNoBuiltinId);
}

Handle<SharedFunctionInfo> Factory::NewSharedFunctionInfoForWasmResume(
    DirectHandle<WasmResumeData> data) {
  return NewSharedFunctionInfo({}, data, Builtin::kNoBuiltinId);
}

Handle<SharedFunctionInfo> Factory::NewSharedFunctionInfoForWasmCapiFunction(
    DirectHandle<WasmCapiFunctionData> data) {
  return NewSharedFunctionInfo(MaybeHandle<String>(), data,
                               Builtin::kNoBuiltinId,
                               FunctionKind::kConciseMethod);
}
#endif  // V8_ENABLE_WEBASSEMBLY

Handle<Cell> Factory::NewCell(Tagged<Smi> value) {
  static_assert(Cell::kSize <= kMaxRegularHeapObjectSize);
  Tagged<Cell> result = Cast<Cell>(AllocateRawWithImmortalMap(
      Cell::kSize, AllocationType::kOld, *cell_map()));
  DisallowGarbageCollection no_gc;
  result->set_value(value, WriteBarrierMode::SKIP_WRITE_BARRIER);
  return handle(result, isolate());
}

Handle<Cell> Factory::NewCell() {
  static_assert(Cell::kSize <= kMaxRegularHeapObjectSize);
  Tagged<Cell> result = Cast<Cell>(AllocateRawWithImmortalMap(
      Cell::kSize, AllocationType::kOld, *cell_map()));
  result->set_value(read_only_roots().undefined_value(),
                    WriteBarrierMode::SKIP_WRITE_BARRIER);
  return handle(result, isolate());
}

Handle<FeedbackCell> Factory::NewNoClosuresCell() {
  Tagged<FeedbackCell> result = Cast<FeedbackCell>(AllocateRawWithImmortalMap(
      FeedbackCell::kAlignedSize, AllocationType::kOld,
      *no_closures_cell_map()));
  DisallowGarbageCollection no_gc;
  result->set_value(read_only_roots().undefined_value());
  result->clear_interrupt_budget();
#ifdef V8_ENABLE_LEAPTIERING
  result->clear_dispatch_handle();
#endif  // V8_ENABLE_LEAPTIERING
  result->clear_padding();
  return handle(result, isolate());
}

Handle<FeedbackCell> Factory::NewOneClosureCell(
    DirectHandle<ClosureFeedbackCellArray> value) {
  Tagged<FeedbackCell> result = Cast<FeedbackCell>(AllocateRawWithImmortalMap(
      FeedbackCell::kAlignedSize, AllocationType::kOld,
      *one_closure_cell_map()));
  DisallowGarbageCollection no_gc;
  result->set_value(*value);
  result->clear_interrupt_budget();
#ifdef V8_ENABLE_LEAPTIERING
  result->clear_dispatch_handle();
#endif  // V8_ENABLE_LEAPTIERING
  result->clear_padding();
  return handle(result, isolate());
}

Handle<FeedbackCell> Factory::NewManyClosuresCell() {
  Tagged<FeedbackCell> result = Cast<FeedbackCell>(AllocateRawWithImmortalMap(
      FeedbackCell::kAlignedSize, AllocationType::kOld,
      *many_closures_cell_map()));
  DisallowGarbageCollection no_gc;
  result->set_value(read_only_roots().undefined_value());
  result->clear_interrupt_budget();
#ifdef V8_ENABLE_LEAPTIERING
  result->clear_dispatch_handle();
#endif  // V8_ENABLE_LEAPTIERING
  result->clear_padding();
  return handle(result, isolate());
}

Handle<PropertyCell> Factory::NewPropertyCell(DirectHandle<Name> name,
                                              PropertyDetails details,
                                              DirectHandle<Object> value,
                                              AllocationType allocation) {
  DCHECK(IsUniqueName(*name));
  static_assert(PropertyCell::kSize <= kMaxRegularHeapObjectSize);
  Tagged<PropertyCell> cell = Cast<PropertyCell>(AllocateRawWithImmortalMap(
      PropertyCell::kSize, allocation, *global_property_cell_map()));
  DisallowGarbageCollection no_gc;
  cell->set_dependent_code(
      DependentCode::empty_dependent_code(ReadOnlyRoots(isolate())),
      SKIP_WRITE_BARRIER);
  WriteBarrierMode mode = allocation == AllocationType::kYoung
                              ? SKIP_WRITE_BARRIER
                              : UPDATE_WRITE_BARRIER;
  cell->set_name(*name, mode);
  cell->set_value(*value, mode);
  cell->set_property_details_raw(details.AsSmi(), SKIP_WRITE_BARRIER);
  return handle(cell, isolate());
}

Handle<ConstTrackingLetCell> Factory::NewConstTrackingLetCell(
    AllocationType allocation) {
  static_assert(ConstTrackingLetCell::kSize <= kMaxRegularHeapObjectSize);
  Tagged<ConstTrackingLetCell> cell = Cast<ConstTrackingLetCell>(
      AllocateRawWithImmortalMap(ConstTrackingLetCell::kSize, allocation,
                                 *global_const_tracking_let_cell_map()));
  DisallowGarbageCollection no_gc;
  cell->set_dependent_code(
      DependentCode::empty_dependent_code(ReadOnlyRoots(isolate())),
      SKIP_WRITE_BARRIER);
  return handle(cell, isolate());
}

Handle<PropertyCell> Factory::NewProtector() {
  return NewPropertyCell(
      empty_string(), PropertyDetails::Empty(PropertyCellType::kConstantType),
      handle(Smi::FromInt(Protectors::kProtectorValid), isolate()));
}

Handle<TransitionArray> Factory::NewTransitionArray(int number_of_transitions,
                                                    int slack) {
  int capacity = TransitionArray::LengthFor(number_of_transitions + slack);
  Handle<TransitionArray> array = Cast<TransitionArray>(
      NewWeakFixedArrayWithMap(read_only_roots().transition_array_map(),
                               capacity, AllocationType::kOld));
  // Transition arrays are AllocationType::kOld. When black allocation is on we
  // have to add the transition array to the list of
  // encountered_transition_arrays.
  Heap* heap = isolate()->heap();
  if (heap->incremental_marking()->black_allocation()) {
    heap->mark_compact_collector()->AddTransitionArray(*array);
  }
  array->WeakFixedArray::set(TransitionArray::kPrototypeTransitionsIndex,
                             Smi::zero());
  array->WeakFixedArray::set(TransitionArray::kSideStepTransitionsIndex,
                             Smi::zero());
  array->WeakFixedArray::set(TransitionArray::kTransitionLengthIndex,
                             Smi::FromInt(number_of_transitions));
  return array;
}

Handle<AllocationSite> Factory::NewAllocationSite(bool with_weak_next) {
  DirectHandle<Map> map = with_weak_next
                              ? allocation_site_map()
                              : allocation_site_without_weaknext_map();
  Handle<AllocationSite> site(
      Cast<AllocationSite>(New(map, AllocationType::kOld)), isolate());
  site->Initialize();

  if (with_weak_next) {
    // Link the site
    site->set_weak_next(isolate()->heap()->allocation_sites_list());
    isolate()->heap()->set_allocation_sites_list(*site);
  }
  return site;
}

template <typename MetaMapProviderFunc>
Handle<Map> Factory::NewMapImpl(MetaMapProviderFunc&& meta_map_provider,
                                InstanceType type, int instance_size,
                                ElementsKind elements_kind,
                                int inobject_properties,
                                AllocationType allocation_type) {
  static_assert(LAST_JS_OBJECT_TYPE == LAST_TYPE);
  DCHECK(!InstanceTypeChecker::MayHaveMapCheckFastCase(type));
  DCHECK_IMPLIES(InstanceTypeChecker::IsJSObject(type) &&
                     !Map::CanHaveFastTransitionableElementsKind(type),
                 IsDictionaryElementsKind(elements_kind) ||
                     IsTerminalElementsKind(elements_kind) ||
                     IsSharedArrayElementsKind(elements_kind));
  DCHECK(allocation_type == AllocationType::kMap ||
         allocation_type == AllocationType::kSharedMap);
  Tagged<HeapObject> result =
      allocator()->AllocateRawWith<HeapAllocator::kRetryOrFail>(
          Map::kSize, allocation_type);
  DisallowGarbageCollection no_gc;
  ReadOnlyRoots roots(isolate());
  result->set_map_after_allocation(meta_map_provider());

#if V8_STATIC_ROOTS_BOOL
  CHECK_IMPLIES(InstanceTypeChecker::IsJSReceiver(type),
                V8HeapCompressionScheme::CompressObject(result.ptr()) >
                    InstanceTypeChecker::kNonJsReceiverMapLimit);
#endif
  isolate()->counters()->maps_created()->Increment();
  return handle(InitializeMap(Cast<Map>(result), type, instance_size,
                              elements_kind, inobject_properties, roots),
                isolate());
}

Tagged<Map> Factory::InitializeMap(Tagged<Map> map, InstanceType type,
                                   int instance_size,
                                   ElementsKind elements_kind,
                                   int inobject_properties,
                                   ReadOnlyRoots roots) {
  DisallowGarbageCollection no_gc;
  map->set_bit_field(0);
  map->set_bit_field2(Map::Bits2::NewTargetIsBaseBit::encode(true));
  int bit_field3 =
      Map::Bits3::EnumLengthBits::encode(kInvalidEnumCacheSentinel) |
      Map::Bits3::OwnsDescriptorsBit::encode(true) |
      Map::Bits3::ConstructionCounterBits::encode(Map::kNoSlackTracking) |
      Map::Bits3::IsExtensibleBit::encode(true);
  map->set_bit_field3(bit_field3);
  map->set_instance_type(type);
  map->init_prototype_and_constructor_or_back_pointer(roots);
  map->set_instance_size(instance_size);
  if (InstanceTypeChecker::IsJSObject(type)) {
    // Shared space JS objects have fixed layout and can have RO maps. No other
    // JS objects have RO maps.
    DCHECK_IMPLIES(!IsAlwaysSharedSpaceJSObjectMap(*map),
                   !ReadOnlyHeap::Contains(map));
    map->SetInObjectPropertiesStartInWords(instance_size / kTaggedSize -
                                           inobject_properties);
    DCHECK_EQ(map->GetInObjectProperties(), inobject_properties);
    map->set_prototype_validity_cell(roots.invalid_prototype_validity_cell(),
                                     kRelaxedStore);
  } else {
    DCHECK_EQ(inobject_properties, 0);
    map->set_inobject_properties_start_or_constructor_function_index(0);
    map->set_prototype_validity_cell(Smi::FromInt(Map::kPrototypeChainValid),
                                     kRelaxedStore, SKIP_WRITE_BARRIER);
  }
  map->set_dependent_code(DependentCode::empty_dependent_code(roots),
                          SKIP_WRITE_BARRIER);
  map->set_raw_transitions(Smi::zero(), SKIP_WRITE_BARRIER);
  map->SetInObjectUnusedPropertyFields(inobject_properties);
  map->SetInstanceDescriptors(isolate(), roots.empty_descriptor_array(), 0);
  // Must be called only after |instance_type| and |instance_size| are set.
  map->set_visitor_id(Map::GetVisitorId(map));
  DCHECK(!map->is_in_retained_map_list());
  map->clear_padding();
  map->set_elements_kind(elements_kind);
  if (v8_flags.log_maps) LOG(isolate(), MapCreate(map));
  return map;
}

Handle<Map> Factory::NewMap(Handle<HeapObject> meta_map_holder,
                            InstanceType type, int instance_size,
                            ElementsKind elements_kind, int inobject_properties,
                            AllocationType allocation_type) {
  auto meta_map_provider = [=] {
    // Tie new map to the same native context as given |meta_map_holder| object.
    Tagged<Map> meta_map = meta_map_holder->map();
    DCHECK(IsMapMap(meta_map));
    return meta_map;
  };
  Handle<Map> map =
      NewMapImpl(meta_map_provider, type, instance_size, elements_kind,
                 inobject_properties, allocation_type);
  return map;
}

Handle<Map> Factory::NewMapWithMetaMap(Handle<Map> meta_map, InstanceType type,
                                       int instance_size,
                                       ElementsKind elements_kind,
                                       int inobject_properties,
                                       AllocationType allocation_type) {
  DCHECK_EQ(*meta_map, meta_map->map());
  auto meta_map_provider = [=] {
    // Use given meta map.
    return *meta_map;
  };
  Handle<Map> map =
      NewMapImpl(meta_map_provider, type, instance_size, elements_kind,
                 inobject_properties, allocation_type);
  return map;
}

Handle<Map> Factory::NewContextfulMap(
    Handle<JSReceiver> creation_context_holder, InstanceType type,
    int instance_size, ElementsKind elements_kind, int inobject_properties,
    AllocationType allocation_type) {
  auto meta_map_provider = [=] {
    // Tie new map to the creation context of given |creation_context_holder|
    // object.
    Tagged<Map> meta_map = creation_context_holder->map()->map();
    DCHECK(IsMapMap(meta_map));
    return meta_map;
  };
  Handle<Map> map =
      NewMapImpl(meta_map_provider, type, instance_size, elements_kind,
                 inobject_properties, allocation_type);
  return map;
}

Handle<Map> Factory::NewContextfulMap(Handle<NativeContext> native_context,
                                      InstanceType type, int instance_size,
                                      ElementsKind elements_kind,
                                      int inobject_properties,
                                      AllocationType allocation_type) {
  DCHECK(InstanceTypeChecker::IsNativeContextSpecific(type) ||
         InstanceTypeChecker::IsMap(type));
  auto meta_map_provider = [=] {
    // Tie new map to given native context.
    return native_context->meta_map();
  };
  Handle<Map> map =
      NewMapImpl(meta_map_provider, type, instance_size, elements_kind,
                 inobject_properties, allocation_type);
  return map;
}

Handle<Map> Factory::NewContextfulMapForCurrentContext(
    InstanceType type, int instance_size, ElementsKind elements_kind,
    int inobject_properties, AllocationType allocation_type) {
  DCHECK(InstanceTypeChecker::IsNativeContextSpecific(type) ||
         InstanceTypeChecker::IsMap(type));
  auto meta_map_provider = [=, this] {
    // Tie new map to current native context.
    return isolate()->raw_native_context()->meta_map();
  };
  Handle<Map> map =
      NewMapImpl(meta_map_provider, type, instance_size, elements_kind,
                 inobject_properties, allocation_type);
  return map;
}

Handle<Map> Factory::NewContextlessMap(InstanceType type, int instance_size,
                                       ElementsKind elements_kind,
                                       int inobject_properties,
                                       AllocationType allocation_type) {
  DCHECK(!InstanceTypeChecker::IsNativeContextSpecific(type) ||
         type == NATIVE_CONTEXT_TYPE ||   // just during NativeContext creation.
         type == JS_GLOBAL_PROXY_TYPE ||  // might be a placeholder object.
         type == JS_SPECIAL_API_OBJECT_TYPE ||  // might be a remote Api object.
         InstanceTypeChecker::IsMap(type));
  auto meta_map_provider = [=, this] {
    // The new map is not tied to any context.
    return ReadOnlyRoots(isolate()).meta_map();
  };
  Handle<Map> map =
      NewMapImpl(meta_map_provider, type, instance_size, elements_kind,
                 inobject_properties, allocation_type);
  return map;
}

Handle<JSObject> Factory::CopyJSObject(DirectHandle<JSObject> source) {
  return CopyJSObjectWithAllocationSite(source, Handle<AllocationSite>());
}

Handle<JSObject> Factory::CopyJSObjectWithAllocationSite(
    DirectHandle<JSObject> source, DirectHandle<AllocationSite> site) {
  DirectHandle<Map> map(source->map(), isolate());

  // We can only clone regexps, normal objects, api objects, errors or arrays.
  // Copying anything else will break invariants.
  InstanceType instance_type = map->instance_type();
  bool is_clonable_js_type =
      instance_type == JS_REG_EXP_TYPE || instance_type == JS_OBJECT_TYPE ||
      instance_type == JS_ERROR_TYPE || instance_type == JS_ARRAY_TYPE ||
      instance_type == JS_SPECIAL_API_OBJECT_TYPE ||
      InstanceTypeChecker::IsJSApiObject(instance_type);
  bool is_clonable_wasm_type = false;
#if V8_ENABLE_WEBASSEMBLY
  is_clonable_wasm_type = instance_type == WASM_GLOBAL_OBJECT_TYPE ||
                          instance_type == WASM_INSTANCE_OBJECT_TYPE ||
                          instance_type == WASM_MEMORY_OBJECT_TYPE ||
                          instance_type == WASM_MODULE_OBJECT_TYPE ||
                          instance_type == WASM_TABLE_OBJECT_TYPE;
#endif  // V8_ENABLE_WEBASSEMBLY
  CHECK(is_clonable_js_type || is_clonable_wasm_type);

  DCHECK(site.is_null() || AllocationSite::CanTrack(instance_type));

  int object_size = map->instance_size();
  int aligned_object_size = ALIGN_TO_ALLOCATION_ALIGNMENT(object_size);
  int adjusted_object_size = aligned_object_size;
  if (!site.is_null()) {
    DCHECK(V8_ALLOCATION_SITE_TRACKING_BOOL);
    adjusted_object_size +=
        ALIGN_TO_ALLOCATION_ALIGNMENT(AllocationMemento::kSize);
  }
  Tagged<HeapObject> raw_clone =
      allocator()->AllocateRawWith<HeapAllocator::kRetryOrFail>(
          adjusted_object_size, AllocationType::kYoung);

  DCHECK_NEWLY_ALLOCATED_OBJECT_IS_YOUNG(isolate(), raw_clone);

  Heap::CopyBlock(raw_clone.address(), source->address(), object_size);
  Handle<JSObject> clone(Cast<JSObject>(raw_clone), isolate());

  if (v8_flags.enable_unconditional_write_barriers) {
    // By default, we shouldn't need to update the write barrier here, as the
    // clone will be allocated in new space.
    const ObjectSlot start(raw_clone.address());
    const ObjectSlot end(raw_clone.address() + object_size);
    isolate()->heap()->WriteBarrierForRange(raw_clone, start, end);
  }
  if (!site.is_null()) {
    Tagged<AllocationMemento> alloc_memento = UncheckedCast<AllocationMemento>(
        Tagged<Object>(raw_clone.ptr() + aligned_object_size));
    InitializeAllocationMemento(alloc_memento, *site);
  }

  SLOW_DCHECK(clone->GetElementsKind() == source->GetElementsKind());
  Tagged<FixedArrayBase> elements = source->elements();
  // Update elements if necessary.
  if (elements->length() > 0) {
    Tagged<FixedArrayBase> elem;
    if (elements->map() == *fixed_cow_array_map()) {
      elem = elements;
    } else if (source->HasDoubleElements()) {
      elem = *CopyFixedDoubleArray(
          handle(Cast<FixedDoubleArray>(elements), isolate()));
    } else {
      elem = *CopyFixedArray(handle(Cast<FixedArray>(elements), isolate()));
    }
    clone->set_elements(elem);
  }

  // Update properties if necessary.
  if (source->HasFastProperties()) {
    Tagged<PropertyArray> properties = source->property_array();
    if (properties->length() > 0) {
      // TODO(gsathya): Do not copy hash code.
      DirectHandle<PropertyArray> prop =
          CopyArrayWithMap(direct_handle(properties, isolate()),
                           direct_handle(properties->map(), isolate()));
      clone->set_raw_properties_or_hash(*prop, kRelaxedStore);
    }
  } else {
    DirectHandle<Object> copied_properties;
    if (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
      copied_properties = SwissNameDictionary::ShallowCopy(
          isolate(), handle(source->property_dictionary_swiss(), isolate()));
    } else {
      copied_properties =
          CopyFixedArray(handle(source->property_dictionary(), isolate()));
    }
    clone->set_raw_properties_or_hash(*copied_properties, kRelaxedStore);
  }
  return clone;
}

namespace {
template <typename T>
void initialize_length(Tagged<T> array, int length) {
  array->set_length(length);
}

template <>
void initialize_length<PropertyArray>(Tagged<PropertyArray> array, int length) {
  array->initialize_length(length);
}

inline void InitEmbedderFields(Tagged<JSObject> obj,
                               Tagged<Object> initial_value) {
  for (int i = 0; i < obj->GetEmbedderFieldCount(); i++) {
    EmbedderDataSlot(obj, i).Initialize(initial_value);
  }
}

}  // namespace

template <typename T>
Handle<T> Factory::CopyArrayWithMap(DirectHandle<T> src, DirectHandle<Map> map,
                                    AllocationType allocation) {
  int len = src->length();
  Tagged<HeapObject> new_object = AllocateRawFixedArray(len, allocation);
  DisallowGarbageCollection no_gc;
  new_object->set_map_after_allocation(*map, SKIP_WRITE_BARRIER);
  Tagged<T> result = Cast<T>(new_object);
  initialize_length(result, len);
  // Copy the content.
  WriteBarrierMode mode = result->GetWriteBarrierMode(no_gc);
  T::CopyElements(isolate(), result, 0, *src, 0, len, mode);
  return handle(result, isolate());
}

template <typename T>
Handle<T> Factory::CopyArrayAndGrow(DirectHandle<T> src, int grow_by,
                                    AllocationType allocation) {
  DCHECK_LT(0, grow_by);
  DCHECK_LE(grow_by, kMaxInt - src->length());
  int old_len = src->length();
  int new_len = old_len + grow_by;
  // TODO(jgruber,v8:14345): Use T::Allocate instead.
  Tagged<HeapObject> new_object = AllocateRawFixedArray(new_len, allocation);
  DisallowGarbageCollection no_gc;
  new_object->set_map_after_allocation(src->map(), SKIP_WRITE_BARRIER);
  Tagged<T> result = Cast<T>(new_object);
  initialize_length(result, new_len);
  // Copy the content.
  WriteBarrierMode mode = result->GetWriteBarrierMode(no_gc);
  T::CopyElements(isolate(), *result, 0, *src, 0, old_len, mode);
  // TODO(jgruber,v8:14345): Enable the static assert once all T's support it:
  // static_assert(T::Shape::kElementSize == kTaggedSize);
  MemsetTagged(ObjectSlot(result->RawFieldOfElementAt(old_len)),
               read_only_roots().undefined_value(), grow_by);
  return handle(result, isolate());
}

Handle<FixedArray> Factory::CopyFixedArrayWithMap(
    DirectHandle<FixedArray> array, DirectHandle<Map> map,
    AllocationType allocation) {
  return CopyArrayWithMap(array, map, allocation);
}

Handle<WeakArrayList> Factory::NewUninitializedWeakArrayList(
    int capacity, AllocationType allocation) {
  DCHECK_LE(0, capacity);
  if (capacity == 0) return empty_weak_array_list();

  Tagged<HeapObject> heap_object =
      AllocateRawWeakArrayList(capacity, allocation);
  DisallowGarbageCollection no_gc;
  heap_object->set_map_after_allocation(*weak_array_list_map(),
                                        SKIP_WRITE_BARRIER);
  Tagged<WeakArrayList> result = Cast<WeakArrayList>(heap_object);
  result->set_length(0);
  result->set_capacity(capacity);
  return handle(result, isolate());
}

Handle<WeakArrayList> Factory::NewWeakArrayList(int capacity,
                                                AllocationType allocation) {
  Handle<WeakArrayList> result =
      NewUninitializedWeakArrayList(capacity, allocation);
  MemsetTagged(ObjectSlot(result->data_start()),
               read_only_roots().undefined_value(), capacity);
  return result;
}

Handle<FixedArray> Factory::CopyFixedArrayAndGrow(
    DirectHandle<FixedArray> array, int grow_by, AllocationType allocation) {
  return CopyArrayAndGrow(array, grow_by, allocation);
}

Handle<WeakFixedArray> Factory::CopyWeakFixedArray(
    DirectHandle<WeakFixedArray> src) {
  DCHECK(!IsTransitionArray(*src));  // Compacted by GC, this code doesn't work
  return CopyArrayWithMap(src, weak_fixed_array_map(), AllocationType::kOld);
}

Handle<WeakFixedArray> Factory::CopyWeakFixedArrayAndGrow(
    DirectHandle<WeakFixedArray> src, int grow_by) {
  DCHECK(!IsTransitionArray(*src));  // Compacted by GC, this code doesn't work
  return CopyArrayAndGrow(src, grow_by, AllocationType::kOld);
}

Handle<WeakArrayList> Factory::CopyWeakArrayListAndGrow(
    DirectHandle<WeakArrayList> src, int grow_by, AllocationType allocation) {
  int old_capacity = src->capacity();
  int new_capacity = old_capacity + grow_by;
  DCHECK_GE(new_capacity, old_capacity);
  Handle<WeakArrayList> result =
      NewUninitializedWeakArrayList(new_capacity, allocation);
  DisallowGarbageCollection no_gc;
  Tagged<WeakArrayList> raw = *result;
  int old_len = src->length();
  raw->set_length(old_len);
  // Copy the content.
  WriteBarrierMode mode = raw->GetWriteBarrierMode(no_gc);
  raw->CopyElements(isolate(), 0, *src, 0, old_len, mode);
  MemsetTagged(ObjectSlot(raw->data_start() + old_len),
               read_only_roots().undefined_value(), new_capacity - old_len);
  return result;
}

Handle<WeakArrayList> Factory::CompactWeakArrayList(
    DirectHandle<WeakArrayList> src, int new_capacity,
    AllocationType allocation) {
  Handle<WeakArrayList> result =
      NewUninitializedWeakArrayList(new_capacity, allocation);

  // Copy the content.
  DisallowGarbageCollection no_gc;
  Tagged<WeakArrayList> raw_src = *src;
  Tagged<WeakArrayList> raw_result = *result;
  WriteBarrierMode mode = raw_result->GetWriteBarrierMode(no_gc);
  int copy_to = 0, length = raw_src->length();
  for (int i = 0; i < length; i++) {
    Tagged<MaybeObject> element = raw_src->Get(i);
    if (element.IsCleared()) continue;
    raw_result->Set(copy_to++, element, mode);
  }
  raw_result->set_length(copy_to);

  MemsetTagged(ObjectSlot(raw_result->data_start() + copy_to),
               read_only_roots().undefined_value(), new_capacity - copy_to);
  return result;
}

Handle<PropertyArray> Factory::CopyPropertyArrayAndGrow(
    DirectHandle<PropertyArray> array, int grow_by) {
  return CopyArrayAndGrow(array, grow_by, AllocationType::kYoung);
}

Handle<FixedArray> Factory::CopyFixedArrayUpTo(DirectHandle<FixedArray> array,
                                               int new_len,
                                               AllocationType allocation) {
  DCHECK_LE(0, new_len);
  DCHECK_LE(new_len, array->length());
  if (new_len == 0) return empty_fixed_array();
  Tagged<HeapObject> heap_object = AllocateRawFixedArray(new_len, allocation);
  DisallowGarbageCollection no_gc;
  heap_object->set_map_after_allocation(*fixed_array_map(), SKIP_WRITE_BARRIER);
  Tagged<FixedArray> result = Cast<FixedArray>(heap_object);
  result->set_length(new_len);
  // Copy the content.
  WriteBarrierMode mode = result->GetWriteBarrierMode(no_gc);
  result->CopyElements(isolate(), 0, *array, 0, new_len, mode);
  return handle(result, isolate());
}

Handle<FixedArray> Factory::CopyFixedArray(Handle<FixedArray> array) {
  if (array->length() == 0) return array;
  return CopyArrayWithMap(DirectHandle<FixedArray>(array),
                          direct_handle(array->map(), isolate()));
}

Handle<FixedDoubleArray> Factory::CopyFixedDoubleArray(
    Handle<FixedDoubleArray> array) {
  int len = array->length();
  if (len == 0) return array;
  Handle<FixedDoubleArray> result =
      Cast<FixedDoubleArray>(NewFixedDoubleArray(len));
  Heap::CopyBlock(
      result->address() + FixedDoubleArray::kLengthOffset,
      array->address() + FixedDoubleArray::kLengthOffset,
      FixedDoubleArray::SizeFor(len) - FixedDoubleArray::kLengthOffset);
  return result;
}

Handle<HeapNumber> Factory::NewHeapNumberForCodeAssembler(double value) {
  ReadOnlyRoots roots(isolate());
  auto num = roots.FindHeapNumber(value);
  if (!num.is_null()) return num;
  // Add known HeapNumber constants to the read only roots. This ensures
  // r/o snapshots to be deterministic.
  DCHECK(!CanAllocateInReadOnlySpace());
  return NewHeapNumber<AllocationType::kOld>(value);
}

Handle<JSObject> Factory::NewError(
    Handle<JSFunction> constructor, MessageTemplate template_index,
    base::Vector<const DirectHandle<Object>> args) {
  HandleScope scope(isolate());

  return scope.CloseAndEscape(ErrorUtils::MakeGenericError(
      isolate(), constructor, template_index, args, SKIP_NONE));
}

Handle<JSObject> Factory::NewError(Handle<JSFunction> constructor,
                                   DirectHandle<String> message,
                                   Handle<Object> options) {
  // Construct a new error object. If an exception is thrown, use the exception
  // as the result.

  Handle<Object> no_caller;
  if (options.is_null()) options = undefined_value();
  return ErrorUtils::Construct(isolate(), constructor, constructor, message,
                               options, SKIP_NONE, no_caller,
                               ErrorUtils::StackTraceCollection::kEnabled)
      .ToHandleChecked();
}

Handle<JSObject> Factory::ShadowRealmNewTypeErrorCopy(
    Handle<Object> original, MessageTemplate template_index,
    base::Vector<const DirectHandle<Object>> args) {
  return ErrorUtils::ShadowRealmConstructTypeErrorCopy(isolate(), original,
                                                       template_index, args);
}

Handle<Object> Factory::NewInvalidStringLengthError() {
  if (v8_flags.correctness_fuzzer_suppressions) {
    FATAL("Aborting on invalid string length");
  }
  // Invalidate the "string length" protector.
  if (Protectors::IsStringLengthOverflowLookupChainIntact(isolate())) {
    Protectors::InvalidateStringLengthOverflowLookupChain(isolate());
  }
  return NewRangeError(MessageTemplate::kInvalidStringLength);
}

Handle<JSObject> Factory::NewSuppressedErrorAtDisposal(
    Isolate* isolate, Handle<Object> error, Handle<Object> suppressed_error) {
  Handle<JSObject> err =
      NewSuppressedError(MessageTemplate::kSuppressedErrorDuringDisposal);

  JSObject::SetOwnPropertyIgnoreAttributes(
      err, isolate->factory()->error_string(), error, DONT_ENUM)
      .Assert();

  JSObject::SetOwnPropertyIgnoreAttributes(
      err, isolate->factory()->suppressed_string(), suppressed_error, DONT_ENUM)
      .Assert();

  return err;
}

#define DEFINE_ERROR(NAME, name)                                         \
  Handle<JSObject> Factory::New##NAME(                                   \
      MessageTemplate template_index,                                    \
      base::Vector<const DirectHandle<Object>> args) {                   \
    return NewError(isolate()->name##_function(), template_index, args); \
  }
DEFINE_ERROR(Error, error)
DEFINE_ERROR(EvalError, eval_error)
DEFINE_ERROR(RangeError, range_error)
DEFINE_ERROR(ReferenceError, reference_error)
DEFINE_ERROR(SyntaxError, syntax_error)
DEFINE_ERROR(SuppressedError, suppressed_error)
DEFINE_ERROR(TypeError, type_error)
DEFINE_ERROR(WasmCompileError, wasm_compile_error)
DEFINE_ERROR(WasmLinkError, wasm_link_error)
DEFINE_ERROR(WasmRuntimeError, wasm_runtime_error)
#undef DEFINE_ERROR

Handle<JSObject> Factory::NewFunctionPrototype(
    DirectHandle<JSFunction> function) {
  // Make sure to use globals from the function's context, since the function
  // can be from a different context.
  DirectHandle<NativeContext> native_context(function->native_context(),
                                             isolate());
  DirectHandle<Map> new_map;
  if (V8_UNLIKELY(IsAsyncGeneratorFunction(function->shared()->kind()))) {
    new_map = direct_handle(
        native_context->async_generator_object_prototype_map(), isolate());
  } else if (IsResumableFunction(function->shared()->kind())) {
    // Generator and async function prototypes can share maps since they
    // don't have "constructor" properties.
    new_map = direct_handle(native_context->generator_object_prototype_map(),
                            isolate());
  } else {
    // Each function prototype gets a fresh map to avoid unwanted sharing of
    // maps between prototypes of different constructors.
    DirectHandle<JSFunction> object_function(native_context->object_function(),
                                             isolate());
    DCHECK(object_function->has_initial_map());
    new_map = direct_handle(object_function->initial_map(), isolate());
  }

  DCHECK(!new_map->is_prototype_map());
  Handle<JSObject> prototype = NewJSObjectFromMap(new_map);

  if (!IsResumableFunction(function->shared()->kind())) {
    JSObject::AddProperty(isolate(), prototype, constructor_string(), function,
                          DONT_ENUM);
  }

  return prototype;
}

Handle<JSObject> Factory::NewExternal(void* value) {
  auto external = Cast<JSExternalObject>(NewJSObjectFromMap(external_map()));
  external->init_value(isolate(), value);
  return external;
}

Handle<Code> Factory::NewCodeObjectForEmbeddedBuiltin(DirectHandle<Code> code,
                                                      Address off_heap_entry) {
  CHECK_NOT_NULL(isolate()->embedded_blob_code());
  CHECK_NE(0, isolate()->embedded_blob_code_size());
  CHECK(Builtins::IsIsolateIndependentBuiltin(*code));

  DCHECK(code->has_instruction_stream());  // Just generated as on-heap code.
  DCHECK(Builtins::IsBuiltinId(code->builtin_id()));
  DCHECK_EQ(code->inlined_bytecode_size(), 0);
  DCHECK_EQ(code->osr_offset(), BytecodeOffset::None());
  DCHECK_EQ(code->raw_deoptimization_data_or_interpreter_data(), Smi::zero());
  // .. because we don't explicitly initialize these flags:
  DCHECK(!code->marked_for_deoptimization());
  DCHECK(!code->can_have_weak_objects());
  DCHECK(!code->embedded_objects_cleared());
  // This check would fail. We explicitly clear any existing position tables
  // below. Note this isn't strictly necessary - we could keep the position
  // tables if we'd properly allocate them into RO space when needed.
  // DCHECK_EQ(code->raw_position_table(), *empty_byte_array());

  NewCodeOptions new_code_options = {
      code->kind(),
      code->builtin_id(),
      code->is_turbofanned(),
      code->stack_slots(),
      code->parameter_count(),
      code->instruction_size(),
      code->metadata_size(),
      code->inlined_bytecode_size(),
      code->osr_offset(),
      code->handler_table_offset(),
      code->constant_pool_offset(),
      code->code_comments_offset(),
      code->unwinding_info_offset(),
      MaybeHandle<TrustedObject>{},
      MaybeHandle<DeoptimizationData>{},
      /*bytecode_offset_table=*/MaybeHandle<TrustedByteArray>{},
      /*source_position_table=*/MaybeHandle<TrustedByteArray>{},
      MaybeHandle<InstructionStream>{},
      off_heap_entry,
  };

  return NewCode(new_code_options);
}

Handle<BytecodeArray> Factory::CopyBytecodeArray(
    DirectHandle<BytecodeArray> source) {
  DirectHandle<BytecodeWrapper> wrapper = NewBytecodeWrapper();
  int size = BytecodeArray::SizeFor(source->length());
  Tagged<BytecodeArray> copy = Cast<BytecodeArray>(AllocateRawWithImmortalMap(
      size, AllocationType::kTrusted, *bytecode_array_map()));
  DisallowGarbageCollection no_gc;
  Tagged<BytecodeArray> raw_source = *source;
  copy->init_self_indirect_pointer(isolate());
  copy->set_length(raw_source->length());
  copy->set_frame_size(raw_source->frame_size());
  copy->set_parameter_count(raw_source->parameter_count());
  copy->set_max_arguments(raw_source->max_arguments());
  copy->set_incoming_new_target_or_generator_register(
      raw_source->incoming_new_target_or_generator_register());
  copy->set_constant_pool(raw_source->constant_pool());
  copy->set_handler_table(raw_source->handler_table());
  copy->set_wrapper(*wrapper);
  if (raw_source->has_source_position_table(kAcquireLoad)) {
    copy->set_source_position_table(
        raw_source->source_position_table(kAcquireLoad), kReleaseStore);
  } else {
    copy->clear_source_position_table(kReleaseStore);
  }
  raw_source->CopyBytecodesTo(copy);
  wrapper->set_bytecode(copy);
  return handle(copy, isolate());
}

Handle<JSObject> Factory::NewJSObject(Handle<JSFunction> constructor,
                                      AllocationType allocation,
                                      NewJSObjectType new_js_object_type) {
  JSFunction::EnsureHasInitialMap(constructor);
  DirectHandle<Map> map(constructor->initial_map(), isolate());
  // NewJSObjectFromMap does not support creating dictionary mode objects. Need
  // to use NewSlowJSObjectFromMap instead.
  DCHECK(!map->is_dictionary_map());
  return NewJSObjectFromMap(map, allocation,
                            DirectHandle<AllocationSite>::null(),
                            new_js_object_type);
}

Handle<JSObject> Factory::NewSlowJSObjectWithNullProto() {
  Handle<JSObject> result =
      NewSlowJSObjectFromMap(isolate()->slow_object_with_null_prototype_map());
  return result;
}

Handle<JSObject> Factory::NewJSObjectWithNullProto() {
  Handle<Map> map(isolate()->object_function()->initial_map(), isolate());
  DirectHandle<Map> map_with_null_proto =
      Map::TransitionRootMapToPrototypeForNewObject(isolate(), map,
                                                    null_value());
  return NewJSObjectFromMap(map_with_null_proto);
}

Handle<JSGlobalObject> Factory::NewJSGlobalObject(
    DirectHandle<JSFunction> constructor) {
  DCHECK(constructor->has_initial_map());
  Handle<Map> map(constructor->initial_map(), isolate());
  DCHECK(map->is_dictionary_map());

  // Make sure no field properties are described in the initial map.
  // This guarantees us that normalizing the properties does not
  // require us to change property values to PropertyCells.
  DCHECK_EQ(map->NextFreePropertyIndex(), 0);

  // Make sure we don't have a ton of pre-allocated slots in the
  // global objects. They will be unused once we normalize the object.
  DCHECK_EQ(map->UnusedPropertyFields(), 0);
  DCHECK_EQ(map->GetInObjectProperties(), 0);

  // Initial size of the backing store to avoid resize of the storage during
  // bootstrapping. The size differs between the JS global object ad the
  // builtins object.
  int initial_size = 64;

  // Allocate a dictionary object for backing storage.
  int at_least_space_for = map->NumberOfOwnDescriptors() * 2 + initial_size;
  Handle<GlobalDictionary> dictionary =
      GlobalDictionary::New(isolate(), at_least_space_for);

  // The global object might be created from an object template with accessors.
  // Fill these accessors into the dictionary.
  DirectHandle<DescriptorArray> descs(map->instance_descriptors(isolate()),
                                      isolate());
  for (InternalIndex i : map->IterateOwnDescriptors()) {
    PropertyDetails details = descs->GetDetails(i);
    // Only accessors are expected.
    DCHECK_EQ(PropertyKind::kAccessor, details.kind());
    PropertyDetails d(PropertyKind::kAccessor, details.attributes(),
                      PropertyCellType::kMutable);
    Handle<Name> name(descs->GetKey(i), isolate());
    DirectHandle<Object> value(descs->GetStrongValue(i), isolate());
    Handle<PropertyCell> cell = NewPropertyCell(name, d, value);
    // |dictionary| already contains enough space for all properties.
    USE(GlobalDictionary::Add(isolate(), dictionary, name, cell, d));
  }

  // Allocate the global object and initialize it with the backing store.
  Handle<JSGlobalObject> global(
      Cast<JSGlobalObject>(New(map, AllocationType::kOld)), isolate());
  InitializeJSObjectFromMap(*global, *dictionary, *map,
                            NewJSObjectType::kAPIWrapper);

  // Create a new map for the global object.
  DirectHandle<Map> new_map = Map::CopyDropDescriptors(isolate(), map);
  Tagged<Map> raw_map = *new_map;
  raw_map->set_may_have_interesting_properties(true);
  raw_map->set_is_dictionary_map(true);
  LOG(isolate(), MapDetails(raw_map));

  // Set up the global object as a normalized object.
  global->set_global_dictionary(*dictionary, kReleaseStore);
  global->set_map(raw_map, kReleaseStore);

  // Make sure result is a global object with properties in dictionary.
  DCHECK(IsJSGlobalObject(*global) && !global->HasFastProperties());
  return global;
}

void Factory::InitializeCppHeapWrapper(Tagged<JSObject> obj) {
  DCHECK(IsJSApiWrapperObject(obj));
  DCHECK(IsJSAPIObjectWithEmbedderSlots(obj) || IsJSSpecialObject(obj));
  static_assert(JSSpecialObject::kCppHeapWrappableOffset ==
                JSAPIObjectWithEmbedderSlots::kCppHeapWrappableOffset);
  obj->SetupLazilyInitializedCppHeapPointerField(
      JSAPIObjectWithEmbedderSlots::kCppHeapWrappableOffset);
}

void Factory::InitializeJSObjectFromMap(Tagged<JSObject> obj,
                                        Tagged<Object> properties,
                                        Tagged<Map> map,
                                        NewJSObjectType new_js_object_type) {
  DisallowGarbageCollection no_gc;
  obj->set_raw_properties_or_hash(properties, kRelaxedStore);
  obj->initialize_elements();
  // TODO(1240798): Initialize the object's body using valid initial values
  // according to the object's initial map.  For example, if the map's
  // instance type is JS_ARRAY_TYPE, the length field should be initialized
  // to a number (e.g. Smi::zero()) and the elements initialized to a
  // fixed array (e.g. Heap::empty_fixed_array()).  Currently, the object
  // verification code has to cope with (temporarily) invalid objects.  See
  // for example, JSArray::JSArrayVerify).
  DCHECK_EQ(IsJSApiWrapperObject(map),
            new_js_object_type == NewJSObjectType::kAPIWrapper);
  InitializeJSObjectBody(obj, map,
                         new_js_object_type == NewJSObjectType::kNoAPIWrapper
                             ? JSObject::kHeaderSize
                             : JSAPIObjectWithEmbedderSlots::kHeaderSize);
  if (new_js_object_type == NewJSObjectType::kAPIWrapper) {
    InitializeCppHeapWrapper(obj);
  }
}

void Factory::InitializeJSObjectBody(Tagged<JSObject> obj, Tagged<Map> map,
                                     int start_offset) {
  DisallowGarbageCollection no_gc;
  if (start_offset == map->instance_size()) return;
  DCHECK_LT(start_offset, map->instance_size());

  // We cannot always fill with one_pointer_filler_map because objects
  // created from API functions expect their embedder fields to be initialized
  // with undefined_value.
  // Pre-allocated fields need to be initialized with undefined_value as well
  // so that object accesses before the constructor completes (e.g. in the
  // debugger) will not cause a crash.

  // In case of Array subclassing the |map| could already be transitioned
  // to different elements kind from the initial map on which we track slack.
  bool in_progress = map->IsInobjectSlackTrackingInProgress();
  obj->InitializeBody(map, start_offset, in_progress,
                      ReadOnlyRoots(isolate()).one_pointer_filler_map_word(),
                      *undefined_value());
  if (in_progress) {
    map->FindRootMap(isolate())->InobjectSlackTrackingStep(isolate());
  }
}

Handle<JSObject> Factory::NewJSObjectFromMap(
    DirectHandle<Map> map, AllocationType allocation,
    DirectHandle<AllocationSite> allocation_site,
    NewJSObjectType new_js_object_type) {
  // JSFunctions should be allocated using AllocateFunction to be
  // properly initialized.
  DCHECK(!InstanceTypeChecker::IsJSFunction(*map));

  // Both types of global objects should be allocated using
  // AllocateGlobalObject to be properly initialized.
  DCHECK_NE(map->instance_type(), JS_GLOBAL_OBJECT_TYPE);

  Tagged<JSObject> js_obj = Cast<JSObject>(
      AllocateRawWithAllocationSite(map, allocation, allocation_site));

  InitializeJSObjectFromMap(js_obj, *empty_fixed_array(), *map,
                            new_js_object_type);

  DCHECK(js_obj->HasFastElements() ||
         (isolate()->bootstrapper()->IsActive() ||
          *map == isolate()
                      ->raw_native_context()
                      ->js_array_template_literal_object_map()) ||
         js_obj->HasTypedArrayOrRabGsabTypedArrayElements() ||
         js_obj->HasFastStringWrapperElements() ||
         js_obj->HasFastArgumentsElements() ||
         js_obj->HasDictionaryElements() || js_obj->HasSharedArrayElements());
  return handle(js_obj, isolate());
}

Handle<JSObject> Factory::NewSlowJSObjectFromMap(
    DirectHandle<Map> map, int capacity, AllocationType allocation,
    DirectHandle<AllocationSite> allocation_site,
    NewJSObjectType new_js_object_type) {
  DCHECK(map->is_dictionary_map());
  DirectHandle<HeapObject> object_properties;
  if (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
    object_properties = NewSwissNameDictionary(capacity, allocation);
  } else {
    object_properties = NameDictionary::New(isolate(), capacity);
  }
  Handle<JSObject> js_object =
      NewJSObjectFromMap(map, allocation, allocation_site, new_js_object_type);
  js_object->set_raw_properties_or_hash(*object_properties, kRelaxedStore);
  return js_object;
}

Handle<JSObject> Factory::NewSlowJSObjectFromMap(DirectHandle<Map> map) {
  return NewSlowJSObjectFromMap(map, PropertyDictionary::kInitialCapacity);
}

Handle<JSObject> Factory::NewSlowJSObjectWithPropertiesAndElements(
    Handle<HeapObject> prototype, DirectHandle<HeapObject> properties,
    DirectHandle<FixedArrayBase> elements) {
  DCHECK(IsPropertyDictionary(*properties));

  Handle<Map> object_map = isolate()->slow_object_with_object_prototype_map();
  if (object_map->prototype() != *prototype) {
    object_map = Map::TransitionRootMapToPrototypeForNewObject(
        isolate(), object_map, prototype);
  }
  DCHECK(object_map->is_dictionary_map());
  Handle<JSObject> object =
      NewJSObjectFromMap(object_map, AllocationType::kYoung);
  object->set_raw_properties_or_hash(*properties);
  if (*elements != read_only_roots().empty_fixed_array()) {
    DCHECK(IsNumberDictionary(*elements));
    object_map =
        JSObject::GetElementsTransitionMap(object, DICTIONARY_ELEMENTS);
    JSObject::MigrateToMap(isolate(), object, object_map);
    object->set_elements(*elements);
  }
  return object;
}

Handle<JSArray> Factory::NewJSArray(ElementsKind elements_kind, int length,
                                    int capacity,
                                    ArrayStorageAllocationMode mode,
                                    AllocationType allocation) {
  DCHECK(capacity >= length);
  if (capacity == 0) {
    return NewJSArrayWithElements(empty_fixed_array(), elements_kind, length,
                                  allocation);
  }

  HandleScope inner_scope(isolate());
  DirectHandle<FixedArrayBase> elms =
      NewJSArrayStorage(elements_kind, capacity, mode);
  return inner_scope.CloseAndEscape(NewJSArrayWithUnverifiedElements(
      elms, elements_kind, length, allocation));
}

Handle<JSArray> Factory::NewJSArrayWithElements(
    DirectHandle<FixedArrayBase> elements, ElementsKind elements_kind,
    int length, AllocationType allocation) {
  Handle<JSArray> array = NewJSArrayWithUnverifiedElements(
      elements, elements_kind, length, allocation);
#ifdef ENABLE_SLOW_DCHECKS
  JSObject::ValidateElements(*array);
#endif
  return array;
}

Handle<JSArray> Factory::NewJSArrayWithUnverifiedElements(
    DirectHandle<FixedArrayBase> elements, ElementsKind elements_kind,
    int length, AllocationType allocation) {
  DCHECK(length <= elements->length());
  Tagged<NativeContext> native_context = isolate()->raw_native_context();
  Tagged<Map> map = native_context->GetInitialJSArrayMap(elements_kind);
  if (map.is_null()) {
    Tagged<JSFunction> array_function = native_context->array_function();
    map = array_function->initial_map();
  }
  return NewJSArrayWithUnverifiedElements(handle(map, isolate()), elements,
                                          length, allocation);
}

Handle<JSArray> Factory::NewJSArrayWithUnverifiedElements(
    DirectHandle<Map> map, DirectHandle<FixedArrayBase> elements, int length,
    AllocationType allocation) {
  auto array = Cast<JSArray>(NewJSObjectFromMap(map, allocation));
  DisallowGarbageCollection no_gc;
  Tagged<JSArray> raw = *array;
  raw->set_elements(*elements);
  raw->set_length(Smi::FromInt(length));
  return array;
}

Handle<JSArray> Factory::NewJSArrayForTemplateLiteralArray(
    DirectHandle<FixedArray> cooked_strings,
    DirectHandle<FixedArray> raw_strings, int function_literal_id,
    int slot_id) {
  Handle<JSArray> raw_object =
      NewJSArrayWithElements(raw_strings, PACKED_ELEMENTS,
                             raw_strings->length(), AllocationType::kOld);
  JSObject::SetIntegrityLevel(isolate(), raw_object, FROZEN, kThrowOnError)
      .ToChecked();

  DirectHandle<NativeContext> native_context = isolate()->native_context();
  auto template_object =
      Cast<TemplateLiteralObject>(NewJSArrayWithUnverifiedElements(
          handle(native_context->js_array_template_literal_object_map(),
                 isolate()),
          cooked_strings, cooked_strings->length(), AllocationType::kOld));
  DisallowGarbageCollection no_gc;
  Tagged<TemplateLiteralObject> raw_template_object = *template_object;
  DCHECK_EQ(raw_template_object->map(),
            native_context->js_array_template_literal_object_map());
  raw_template_object->set_raw(*raw_object);
  raw_template_object->set_function_literal_id(function_literal_id);
  raw_template_object->set_slot_id(slot_id);
  return template_object;
}

void Factory::NewJSArrayStorage(DirectHandle<JSArray> array, int length,
                                int capacity, ArrayStorageAllocationMode mode) {
  DCHECK(capacity >= length);

  if (capacity == 0) {
    Tagged<JSArray> raw = *array;
    DisallowGarbageCollection no_gc;
    raw->set_length(Smi::zero());
    raw->set_elements(*empty_fixed_array());
    return;
  }

  HandleScope inner_scope(isolate());
  DirectHandle<FixedArrayBase> elms =
      NewJSArrayStorage(array->GetElementsKind(), capacity, mode);
  DisallowGarbageCollection no_gc;
  Tagged<JSArray> raw = *array;
  raw->set_elements(*elms);
  raw->set_length(Smi::FromInt(length));
}

Handle<FixedArrayBase> Factory::NewJSArrayStorage(
    ElementsKind elements_kind, int capacity, ArrayStorageAllocationMode mode) {
  DCHECK_GT(capacity, 0);
  Handle<FixedArrayBase> elms;
  if (IsDoubleElementsKind(elements_kind)) {
    if (mode == ArrayStorageAllocationMode::DONT_INITIALIZE_ARRAY_ELEMENTS) {
      elms = NewFixedDoubleArray(capacity);
    } else {
      DCHECK_EQ(
          mode,
          ArrayStorageAllocationMode::INITIALIZE_ARRAY_ELEMENTS_WITH_HOLE);
      elms = NewFixedDoubleArrayWithHoles(capacity);
    }
  } else {
    DCHECK(IsSmiOrObjectElementsKind(elements_kind));
    if (mode == ArrayStorageAllocationMode::DONT_INITIALIZE_ARRAY_ELEMENTS) {
      elms = NewFixedArray(capacity);
    } else {
      DCHECK_EQ(
          mode,
          ArrayStorageAllocationMode::INITIALIZE_ARRAY_ELEMENTS_WITH_HOLE);
      elms = NewFixedArrayWithHoles(capacity);
    }
  }
  return elms;
}

Handle<JSWeakMap> Factory::NewJSWeakMap() {
  Tagged<NativeContext> native_context = isolate()->raw_native_context();
  DirectHandle<Map> map(native_context->js_weak_map_fun()->initial_map(),
                        isolate());
  Handle<JSWeakMap> weakmap(Cast<JSWeakMap>(*NewJSObjectFromMap(map)),
                            isolate());
  {
    // Do not leak handles for the hash table, it would make entries strong.
    HandleScope scope(isolate());
    JSWeakCollection::Initialize(weakmap, isolate());
  }
  return weakmap;
}

Handle<JSModuleNamespace> Factory::NewJSModuleNamespace() {
  DirectHandle<Map> map = isolate()->js_module_namespace_map();
  Handle<JSModuleNamespace> module_namespace(
      Cast<JSModuleNamespace>(NewJSObjectFromMap(
          map, AllocationType::kYoung, DirectHandle<AllocationSite>::null(),
          NewJSObjectType::kAPIWrapper)));
  FieldIndex index = FieldIndex::ForDescriptor(
      *map, InternalIndex(JSModuleNamespace::kToStringTagFieldIndex));
  module_namespace->FastPropertyAtPut(index, read_only_roots().Module_string(),
                                      SKIP_WRITE_BARRIER);
  return module_namespace;
}

Handle<JSWrappedFunction> Factory::NewJSWrappedFunction(
    DirectHandle<NativeContext> creation_context, DirectHandle<Object> target) {
  DCHECK(IsCallable(*target));
  DirectHandle<Map> map(
      Cast<Map>(creation_context->get(Context::WRAPPED_FUNCTION_MAP_INDEX)),
      isolate());
  // 2. Let wrapped be ! MakeBasicObject(internalSlotsList).
  // 3. Set wrapped.[[Prototype]] to
  // callerRealm.[[Intrinsics]].[[%Function->prototype%]].
  // 4. Set wrapped.[[Call]] as described in 2.1.
  Handle<JSWrappedFunction> wrapped =
      Cast<JSWrappedFunction>(isolate()->factory()->NewJSObjectFromMap(map));
  // 5. Set wrapped.[[WrappedTargetFunction]] to Target.
  wrapped->set_wrapped_target_function(Cast<JSCallable>(*target));
  // 6. Set wrapped.[[Realm]] to callerRealm.
  wrapped->set_context(*creation_context);
  // TODO(v8:11989): https://github.com/tc39/proposal-shadowrealm/pull/348

  return wrapped;
}

Handle<JSGeneratorObject> Factory::NewJSGeneratorObject(
    Handle<JSFunction> function) {
  DCHECK(IsResumableFunction(function->shared()->kind()));
  JSFunction::EnsureHasInitialMap(function);
  DirectHandle<Map> map(function->initial_map(), isolate());

  DCHECK(map->instance_type() == JS_GENERATOR_OBJECT_TYPE ||
         map->instance_type() == JS_ASYNC_GENERATOR_OBJECT_TYPE);

  return Cast<JSGeneratorObject>(NewJSObjectFromMap(map));
}

Handle<JSDisposableStackBase> Factory::NewJSDisposableStackBase() {
  DirectHandle<NativeContext> native_context = isolate()->native_context();
  DirectHandle<Map> map(native_context->js_disposable_stack_map(), isolate());
  Handle<JSDisposableStackBase> disposable_stack(
      Cast<JSDisposableStackBase>(NewJSObjectFromMap(map)));
  disposable_stack->set_status(0);
  return disposable_stack;
}

Handle<JSSyncDisposableStack> Factory::NewJSSyncDisposableStack(
    DirectHandle<Map> map) {
  Handle<JSSyncDisposableStack> disposable_stack(
      Cast<JSSyncDisposableStack>(NewJSObjectFromMap(map)));
  disposable_stack->set_status(0);
  return disposable_stack;
}

Handle<JSAsyncDisposableStack> Factory::NewJSAsyncDisposableStack(
    DirectHandle<Map> map) {
  Handle<JSAsyncDisposableStack> disposable_stack(
      Cast<JSAsyncDisposableStack>(NewJSObjectFromMap(map)));
  disposable_stack->set_status(0);
  return disposable_stack;
}

Handle<SourceTextModule> Factory::NewSourceTextModule(
    DirectHandle<SharedFunctionInfo> sfi) {
  DirectHandle<SourceTextModuleInfo> module_info(
      sfi->scope_info()->ModuleDescriptorInfo(), isolate());
  DirectHandle<ObjectHashTable> exports =
      ObjectHashTable::New(isolate(), module_info->RegularExportCount());
  DirectHandle<FixedArray> regular_exports =
      NewFixedArray(module_info->RegularExportCount());
  DirectHandle<FixedArray> regular_imports =
      NewFixedArray(module_info->regular_imports()->length());
  int requested_modules_length = module_info->module_requests()->length();
  DirectHandle<FixedArray> requested_modules =
      requested_modules_length > 0 ? NewFixedArray(requested_modules_length)
                                   : empty_fixed_array();

  ReadOnlyRoots roots(isolate());
  Tagged<SourceTextModule> module = Cast<SourceTextModule>(
      New(source_text_module_map(), AllocationType::kOld));
  DisallowGarbageCollection no_gc;
  module->set_code(*sfi);
  module->set_exports(*exports);
  module->set_regular_exports(*regular_exports);
  module->set_regular_imports(*regular_imports);
  module->set_hash(isolate()->GenerateIdentityHash(Smi::kMaxValue));
  module->set_module_namespace(roots.undefined_value(), SKIP_WRITE_BARRIER);
  module->set_requested_modules(*requested_modules);
  module->set_status(Module::kUnlinked);
  module->set_exception(roots.the_hole_value(), SKIP_WRITE_BARRIER);
  module->set_top_level_capability(roots.undefined_value(), SKIP_WRITE_BARRIER);
  module->set_import_meta(roots.the_hole_value(), kReleaseStore,
                          SKIP_WRITE_BARRIER);
  module->set_dfs_index(-1);
  module->set_dfs_ancestor_index(-1);
  module->set_flags(0);
  module->set_has_toplevel_await(IsModuleWithTopLevelAwait(sfi->kind()));
  module->set_async_evaluation_ordinal(SourceTextModule::kNotAsyncEvaluated);
  module->set_cycle_root(roots.the_hole_value(), SKIP_WRITE_BARRIER);
  module->set_async_parent_modules(roots.empty_array_list());
  module->set_pending_async_dependencies(0);
  return handle(module, isolate());
}

Handle<SyntheticModule> Factory::NewSyntheticModule(
    DirectHandle<String> module_name, DirectHandle<FixedArray> export_names,
    v8::Module::SyntheticModuleEvaluationSteps evaluation_steps) {
  ReadOnlyRoots roots(isolate());

  DirectHandle<ObjectHashTable> exports =
      ObjectHashTable::New(isolate(), static_cast<int>(export_names->length()));
  DirectHandle<Foreign> evaluation_steps_foreign =
      NewForeign<kSyntheticModuleTag>(
          reinterpret_cast<Address>(evaluation_steps));

  Tagged<SyntheticModule> module =
      Cast<SyntheticModule>(New(synthetic_module_map(), AllocationType::kOld));
  DisallowGarbageCollection no_gc;
  module->set_hash(isolate()->GenerateIdentityHash(Smi::kMaxValue));
  module->set_module_namespace(roots.undefined_value(), SKIP_WRITE_BARRIER);
  module->set_status(Module::kUnlinked);
  module->set_exception(roots.the_hole_value(), SKIP_WRITE_BARRIER);
  module->set_top_level_capability(roots.undefined_value(), SKIP_WRITE_BARRIER);
  module->set_name(*module_name);
  module->set_export_names(*export_names);
  module->set_exports(*exports);
  module->set_evaluation_steps(*evaluation_steps_foreign);
  return handle(module, isolate());
}

Handle<JSArrayBuffer> Factory::NewJSArrayBuffer(
    std::shared_ptr<BackingStore> backing_store, AllocationType allocation) {
  DirectHandle<Map> map(
      isolate()->native_context()->array_buffer_fun()->initial_map(),
      isolate());
  ResizableFlag resizable_by_js = ResizableFlag::kNotResizable;
  if (backing_store->is_resizable_by_js()) {
    resizable_by_js = ResizableFlag::kResizable;
  }
  auto result = Cast<JSArrayBuffer>(
      NewJSObjectFromMap(map, allocation, DirectHandle<AllocationSite>::null(),
                         NewJSObjectType::kAPIWrapper));
  result->Setup(SharedFlag::kNotShared, resizable_by_js,
                std::move(backing_store), isolate());
  return result;
}

MaybeHandle<JSArrayBuffer> Factory::NewJSArrayBufferAndBackingStore(
    size_t byte_length, InitializedFlag initialized,
    AllocationType allocation) {
  return NewJSArrayBufferAndBackingStore(byte_length, byte_length, initialized,
                                         ResizableFlag::kNotResizable,
                                         allocation);
}

MaybeHandle<JSArrayBuffer> Factory::NewJSArrayBufferAndBackingStore(
    size_t byte_length, size_t max_byte_length, InitializedFlag initialized,
    ResizableFlag resizable, AllocationType allocation) {
  DCHECK_LE(byte_length, max_byte_length);
  std::unique_ptr<BackingStore> backing_store = nullptr;

  if (resizable == ResizableFlag::kResizable) {
    size_t page_size, initial_pages, max_pages;
    if (JSArrayBuffer::GetResizableBackingStorePageConfiguration(
            isolate(), byte_length, max_byte_length, kDontThrow, &page_size,
            &initial_pages, &max_pages)
            .IsNothing()) {
      return MaybeHandle<JSArrayBuffer>();
    }

    backing_store = BackingStore::TryAllocateAndPartiallyCommitMemory(
        isolate(), byte_length, max_byte_length, page_size, initial_pages,
        max_pages, WasmMemoryFlag::kNotWasm, SharedFlag::kNotShared);
    if (!backing_store) return MaybeHandle<JSArrayBuffer>();
  } else {
    if (byte_length > 0) {
      backing_store = BackingStore::Allocate(
          isolate(), byte_length, SharedFlag::kNotShared, initialized);
      if (!backing_store) return MaybeHandle<JSArrayBuffer>();
    }
  }
  DirectHandle<Map> map(
      isolate()->native_context()->array_buffer_fun()->initial_map(),
      isolate());
  auto array_buffer = Cast<JSArrayBuffer>(
      NewJSObjectFromMap(map, allocation, DirectHandle<AllocationSite>::null(),
                         NewJSObjectType::kAPIWrapper));
  array_buffer->Setup(SharedFlag::kNotShared, resizable,
                      std::move(backing_store), isolate());
  return array_buffer;
}

Handle<JSArrayBuffer> Factory::NewJSSharedArrayBuffer(
    std::shared_ptr<BackingStore> backing_store) {
  DirectHandle<Map> map(
      isolate()->native_context()->shared_array_buffer_fun()->initial_map(),
      isolate());
  auto result = Cast<JSArrayBuffer>(NewJSObjectFromMap(
      map, AllocationType::kYoung, DirectHandle<AllocationSite>::null(),
      NewJSObjectType::kAPIWrapper));
  ResizableFlag resizable = backing_store->is_resizable_by_js()
                                ? ResizableFlag::kResizable
                                : ResizableFlag::kNotResizable;
  result->Setup(SharedFlag::kShared, resizable, std::move(backing_store),
                isolate());
  return result;
}

Handle<JSIteratorResult> Factory::NewJSIteratorResult(
    DirectHandle<Object> value, bool done) {
  DirectHandle<Map> map(isolate()->native_context()->iterator_result_map(),
                        isolate());
  Handle<JSIteratorResult> js_iter_result =
      Cast<JSIteratorResult>(NewJSObjectFromMap(map, AllocationType::kYoung));
  DisallowGarbageCollection no_gc;
  Tagged<JSIteratorResult> raw = *js_iter_result;
  raw->set_value(*value, SKIP_WRITE_BARRIER);
  raw->set_done(*ToBoolean(done), SKIP_WRITE_BARRIER);
  return js_iter_result;
}

Handle<JSAsyncFromSyncIterator> Factory::NewJSAsyncFromSyncIterator(
    DirectHandle<JSReceiver> sync_iterator, DirectHandle<Object> next) {
  DirectHandle<Map> map(
      isolate()->native_context()->async_from_sync_iterator_map(), isolate());
  Handle<JSAsyncFromSyncIterator> iterator = Cast<JSAsyncFromSyncIterator>(
      NewJSObjectFromMap(map, AllocationType::kYoung));
  DisallowGarbageCollection no_gc;
  Tagged<JSAsyncFromSyncIterator> raw = *iterator;
  raw->set_sync_iterator(*sync_iterator, SKIP_WRITE_BARRIER);
  raw->set_next(*next, SKIP_WRITE_BARRIER);
  return iterator;
}

Handle<JSMap> Factory::NewJSMap() {
  DirectHandle<Map> map(isolate()->native_context()->js_map_map(), isolate());
  Handle<JSMap> js_map = Cast<JSMap>(NewJSObjectFromMap(map));
  JSMap::Initialize(js_map, isolate());
  return js_map;
}

Handle<JSSet> Factory::NewJSSet() {
  DirectHandle<Map> map(isolate()->native_context()->js_set_map(), isolate());
  Handle<JSSet> js_set = Cast<JSSet>(NewJSObjectFromMap(map));
  JSSet::Initialize(js_set, isolate());
  return js_set;
}

void Factory::TypeAndSizeForElementsKind(ElementsKind kind,
                                         ExternalArrayType* array_type,
                                         size_t* element_size) {
  switch (kind) {
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) \
  case TYPE##_ELEMENTS:                           \
    *array_type = kExternal##Type##Array;         \
    *element_size = sizeof(ctype);                \
    break;
    TYPED_ARRAYS(TYPED_ARRAY_CASE)
    RAB_GSAB_TYPED_ARRAYS_WITH_TYPED_ARRAY_TYPE(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE

    default:
      UNREACHABLE();
  }
}

Handle<JSArrayBufferView> Factory::NewJSArrayBufferView(
    DirectHandle<Map> map, DirectHandle<FixedArrayBase> elements,
    DirectHandle<JSArrayBuffer> buffer, size_t byte_offset,
    size_t byte_length) {
  if (!IsRabGsabTypedArrayElementsKind(map->elements_kind())) {
    CHECK_LE(byte_length, buffer->GetByteLength());
    CHECK_LE(byte_offset, buffer->GetByteLength());
    CHECK_LE(byte_offset + byte_length, buffer->GetByteLength());
  }

  Handle<JSArrayBufferView> array_buffer_view =
      Cast<JSArrayBufferView>(NewJSObjectFromMap(
          map, AllocationType::kYoung, DirectHandle<AllocationSite>::null(),
          NewJSObjectType::kAPIWrapper));
  DisallowGarbageCollection no_gc;
  Tagged<JSArrayBufferView> raw = *array_buffer_view;
  raw->set_elements(*elements, SKIP_WRITE_BARRIER);
  raw->set_buffer(*buffer, SKIP_WRITE_BARRIER);
  raw->set_byte_offset(byte_offset);
  raw->set_byte_length(byte_length);
  raw->set_bit_field(0);
  // TODO(v8) remove once embedder data slots are always zero-initialized.
  InitEmbedderFields(raw, Smi::zero());
  DCHECK_EQ(raw->GetEmbedderFieldCount(),
            v8::ArrayBufferView::kEmbedderFieldCount);
  return array_buffer_view;
}

Handle<JSTypedArray> Factory::NewJSTypedArray(
    ExternalArrayType type, DirectHandle<JSArrayBuffer> buffer,
    size_t byte_offset, size_t length, bool is_length_tracking) {
  size_t element_size;
  ElementsKind elements_kind;
  JSTypedArray::ForFixedTypedArray(type, &element_size, &elements_kind);

  const bool is_backed_by_rab =
      buffer->is_resizable_by_js() && !buffer->is_shared();

  DirectHandle<Map> map;
  if (is_backed_by_rab || is_length_tracking) {
    map = direct_handle(
        isolate()->raw_native_context()->TypedArrayElementsKindToRabGsabCtorMap(
            elements_kind),
        isolate());
  } else {
    map = direct_handle(
        isolate()->raw_native_context()->TypedArrayElementsKindToCtorMap(
            elements_kind),
        isolate());
  }

  if (is_length_tracking) {
    // Security: enforce the invariant that length-tracking TypedArrays have
    // their length and byte_length set to 0.
    length = 0;
  }

  CHECK_LE(length, JSTypedArray::kMaxByteLength / element_size);
  CHECK_EQ(0, byte_offset % element_size);
  size_t byte_length = length * element_size;

  Handle<JSTypedArray> typed_array = Cast<JSTypedArray>(NewJSArrayBufferView(
      map, empty_byte_array(), buffer, byte_offset, byte_length));
  Tagged<JSTypedArray> raw = *typed_array;
  DisallowGarbageCollection no_gc;
  raw->set_length(length);
  raw->SetOffHeapDataPtr(isolate(), buffer->backing_store(), byte_offset);
  raw->set_is_length_tracking(is_length_tracking);
  raw->set_is_backed_by_rab(is_backed_by_rab);
  return typed_array;
}

Handle<JSDataViewOrRabGsabDataView> Factory::NewJSDataViewOrRabGsabDataView(
    DirectHandle<JSArrayBuffer> buffer, size_t byte_offset, size_t byte_length,
    bool is_length_tracking) {
  if (is_length_tracking) {
    // Security: enforce the invariant that length-tracking DataViews have their
    // byte_length set to 0.
    byte_length = 0;
  }
  bool is_backed_by_rab = !buffer->is_shared() && buffer->is_resizable_by_js();
  DirectHandle<Map> map;
  if (is_backed_by_rab || is_length_tracking) {
    map = direct_handle(
        isolate()->native_context()->js_rab_gsab_data_view_map(), isolate());
  } else {
    map = direct_handle(
        isolate()->native_context()->data_view_fun()->initial_map(), isolate());
  }
  Handle<JSDataViewOrRabGsabDataView> obj =
      Cast<JSDataViewOrRabGsabDataView>(NewJSArrayBufferView(
          map, empty_fixed_array(), buffer, byte_offset, byte_length));
  obj->set_data_pointer(
      isolate(), static_cast<uint8_t*>(buffer->backing_store()) + byte_offset);
  obj->set_is_length_tracking(is_length_tracking);
  obj->set_is_backed_by_rab(is_backed_by_rab);
  return obj;
}

MaybeHandle<JSBoundFunction> Factory::NewJSBoundFunction(
    DirectHandle<JSReceiver> target_function, DirectHandle<JSAny> bound_this,
    base::Vector<Handle<Object>> bound_args, Handle<HeapObject> prototype) {
  DCHECK(IsCallable(*target_function));
  static_assert(Code::kMaxArguments <= FixedArray::kMaxLength);
  if (bound_args.length() >= Code::kMaxArguments) {
    THROW_NEW_ERROR(isolate(),
                    NewRangeError(MessageTemplate::kTooManyArguments));
  }

  SaveAndSwitchContext save(isolate(),
                            target_function->GetCreationContext().value());

  // Create the [[BoundArguments]] for the result.
  DirectHandle<FixedArray> bound_arguments;
  if (bound_args.empty()) {
    bound_arguments = empty_fixed_array();
  } else {
    bound_arguments = NewFixedArray(bound_args.length());
    for (int i = 0; i < bound_args.length(); ++i) {
      bound_arguments->set(i, *bound_args[i]);
    }
  }

  // Setup the map for the JSBoundFunction instance.
  Handle<Map> map = IsConstructor(*target_function)
                        ? isolate()->bound_function_with_constructor_map()
                        : isolate()->bound_function_without_constructor_map();
  if (map->prototype() != *prototype) {
    map = Map::TransitionRootMapToPrototypeForNewObject(isolate(), map,
                                                        prototype);
  }
  DCHECK_EQ(IsConstructor(*target_function), map->is_constructor());

  // Setup the JSBoundFunction instance.
  Handle<JSBoundFunction> result =
      Cast<JSBoundFunction>(NewJSObjectFromMap(map, AllocationType::kYoung));
  DisallowGarbageCollection no_gc;
  Tagged<JSBoundFunction> raw = *result;
  raw->set_bound_target_function(Cast<JSCallable>(*target_function),
                                 SKIP_WRITE_BARRIER);
  raw->set_bound_this(*bound_this, SKIP_WRITE_BARRIER);
  raw->set_bound_arguments(*bound_arguments, SKIP_WRITE_BARRIER);
  return result;
}

// ES6 section 9.5.15 ProxyCreate (target, handler)
Handle<JSProxy> Factory::NewJSProxy(DirectHandle<JSReceiver> target,
                                    DirectHandle<JSReceiver> handler) {
  // Allocate the proxy object.
  DirectHandle<Map> map = IsCallable(*target)
                              ? IsConstructor(*target)
                                    ? isolate()->proxy_constructor_map()
                                    : isolate()->proxy_callable_map()
                              : isolate()->proxy_map();
  DCHECK(IsNull(map->prototype(), isolate()));
  Tagged<JSProxy> result = Cast<JSProxy>(New(map, AllocationType::kYoung));
  DisallowGarbageCollection no_gc;
  result->initialize_properties(isolate());
  result->set_target(*target, SKIP_WRITE_BARRIER);
  result->set_handler(*handler, SKIP_WRITE_BARRIER);
  return handle(result, isolate());
}

Handle<JSGlobalProxy> Factory::NewUninitializedJSGlobalProxy(int size) {
  // Create an empty shell of a JSGlobalProxy that needs to be reinitialized
  // via ReinitializeJSGlobalProxy later.
  DirectHandle<Map> map = NewContextlessMap(JS_GLOBAL_PROXY_TYPE, size);
  // Maintain invariant expected from any JSGlobalProxy.
  {
    DisallowGarbageCollection no_gc;
    Tagged<Map> raw = *map;
    raw->set_is_access_check_needed(true);
    raw->set_may_have_interesting_properties(true);
    LOG(isolate(), MapDetails(raw));
  }
  Handle<JSGlobalProxy> proxy = Cast<JSGlobalProxy>(NewJSObjectFromMap(
      map, AllocationType::kOld, DirectHandle<AllocationSite>::null(),
      NewJSObjectType::kAPIWrapper));
  // Create identity hash early in case there is any JS collection containing
  // a global proxy key and needs to be rehashed after deserialization.
  proxy->GetOrCreateIdentityHash(isolate());
  return proxy;
}

void Factory::ReinitializeJSGlobalProxy(DirectHandle<JSGlobalProxy> object,
                                        DirectHandle<JSFunction> constructor) {
  DCHECK(constructor->has_initial_map());
  Handle<Map> map(constructor->initial_map(), isolate());
  DirectHandle<Map> old_map(object->map(), isolate());

  // The proxy's hash should be retained across reinitialization.
  DirectHandle<Object> raw_properties_or_hash(object->raw_properties_or_hash(),
                                              isolate());

  if (old_map->is_prototype_map()) {
    map = Map::Copy(isolate(), map, "CopyAsPrototypeForJSGlobalProxy");
    map->set_is_prototype_map(true);
  }
  JSObject::NotifyMapChange(old_map, map, isolate());
  old_map->NotifyLeafMapLayoutChange(isolate());

  // Check that the already allocated object has the same size and type as
  // objects allocated using the constructor.
  DCHECK(map->instance_size() == old_map->instance_size());
  DCHECK(map->instance_type() == old_map->instance_type());

  // In order to keep heap in consistent state there must be no allocations
  // before object re-initialization is finished.
  DisallowGarbageCollection no_gc;

  // Reset the map for the object.
  Tagged<JSGlobalProxy> raw = *object;
  raw->set_map(*map, kReleaseStore);

  // Reinitialize the object from the constructor map.
  InitializeJSObjectFromMap(raw, *raw_properties_or_hash, *map,
                            NewJSObjectType::kAPIWrapper);
  // Ensure that the object and constructor belongs to the same native context.
  DCHECK_EQ(object->map()->map(), constructor->map()->map());
}

Handle<JSMessageObject> Factory::NewJSMessageObject(
    MessageTemplate message, DirectHandle<Object> argument, int start_position,
    int end_position, DirectHandle<SharedFunctionInfo> shared_info,
    int bytecode_offset, DirectHandle<Script> script,
    DirectHandle<Object> stack_frames) {
  DirectHandle<Map> map = message_object_map();
  Tagged<JSMessageObject> message_obj =
      Cast<JSMessageObject>(New(map, AllocationType::kYoung));
  DisallowGarbageCollection no_gc;
  message_obj->set_raw_properties_or_hash(*empty_fixed_array(),
                                          SKIP_WRITE_BARRIER);
  message_obj->initialize_elements();
  message_obj->set_elements(*empty_fixed_array(), SKIP_WRITE_BARRIER);
  message_obj->set_type(message);
  message_obj->set_argument(*argument, SKIP_WRITE_BARRIER);
  message_obj->set_start_position(start_position);
  message_obj->set_end_position(end_position);
  message_obj->set_script(*script, SKIP_WRITE_BARRIER);
  if (start_position >= 0) {
    // If there's a start_position, then there's no need to store the
    // SharedFunctionInfo as it will never be necessary to regenerate the
    // position.
    message_obj->set_shared_info(Smi::FromInt(-1));
    message_obj->set_bytecode_offset(Smi::FromInt(0));
  } else {
    message_obj->set_bytecode_offset(Smi::FromInt(bytecode_offset));
    if (shared_info.is_null()) {
      message_obj->set_shared_info(Smi::FromInt(-1));
      DCHECK_EQ(bytecode_offset, -1);
    } else {
      message_obj->set_shared_info(*shared_info, SKIP_WRITE_BARRIER);
      DCHECK_GE(bytecode_offset, kFunctionEntryBytecodeOffset);
    }
  }

  message_obj->set_stack_frames(*stack_frames, SKIP_WRITE_BARRIER);
  message_obj->set_error_level(v8::Isolate::kMessageError);
  return handle(message_obj, isolate());
}

Handle<SharedFunctionInfo> Factory::NewSharedFunctionInfoForApiFunction(
    MaybeDirectHandle<String> maybe_name,
    DirectHandle<FunctionTemplateInfo> function_template_info,
    FunctionKind kind) {
  return NewSharedFunctionInfo(maybe_name, function_template_info,
                               Builtin::kNoBuiltinId, kind);
}

Handle<SharedFunctionInfo> Factory::NewSharedFunctionInfoForBuiltin(
    MaybeDirectHandle<String> maybe_name, Builtin builtin, FunctionKind kind) {
  return NewSharedFunctionInfo(maybe_name, MaybeHandle<HeapObject>(), builtin,
                               kind);
}

Handle<InterpreterData> Factory::NewInterpreterData(
    DirectHandle<BytecodeArray> bytecode_array, DirectHandle<Code> code) {
  Tagged<Map> map = *interpreter_data_map();
  Tagged<InterpreterData> interpreter_data = Cast<InterpreterData>(
      AllocateRawWithImmortalMap(map->instance_size(), AllocationType::kTrusted,
                                 *interpreter_data_map()));
  DisallowGarbageCollection no_gc;
  interpreter_data->init_self_indirect_pointer(isolate());
  interpreter_data->set_bytecode_array(*bytecode_array);
  interpreter_data->set_interpreter_trampoline(*code);
  return handle(interpreter_data, isolate());
}

int Factory::NumberToStringCacheHash(Tagged<Smi> number) {
  int mask = (number_string_cache()->length() >> 1) - 1;
  return number.value() & mask;
}

int Factory::NumberToStringCacheHash(double number) {
  int mask = (number_string_cache()->length() >> 1) - 1;
  int64_t bits = base::bit_cast<int64_t>(number);
  return (static_cast<int>(bits) ^ static_cast<int>(bits >> 32)) & mask;
}

Handle<String> Factory::SizeToString(size_t value, bool check_cache) {
  Handle<String> result;
  NumberCacheMode cache_mode =
      check_cache ? NumberCacheMode::kBoth : NumberCacheMode::kIgnore;
  if (value <= Smi::kMaxValue) {
    int32_t int32v = static_cast<int32_t>(static_cast<uint32_t>(value));
    // SmiToString sets the hash when needed, we can return immediately.
    return SmiToString(Smi::FromInt(int32v), cache_mode);
  } else if (value <= kMaxSafeInteger) {
    // TODO(jkummerow): Refactor the cache to not require Objects as keys.
    double double_value = static_cast<double>(value);
    result = HeapNumberToString(NewHeapNumber(double_value), value, cache_mode);
  } else {
    char arr[kNumberToStringBufferSize];
    base::Vector<char> buffer(arr, arraysize(arr));
    // Build the string backwards from the least significant digit.
    int i = buffer.length();
    size_t value_copy = value;
    buffer[--i] = '\0';
    do {
      buffer[--i] = '0' + (value_copy % 10);
      value_copy /= 10;
    } while (value_copy > 0);
    char* string = buffer.begin() + i;
    // No way to cache this; we'd need an {Object} to use as key.
    result = NewStringFromAsciiChecked(string);
  }
  {
    DisallowGarbageCollection no_gc;
    Tagged<String> raw = *result;
    if (value <= JSArray::kMaxArrayIndex &&
        raw->raw_hash_field() == String::kEmptyHashField) {
      uint32_t raw_hash_field = StringHasher::MakeArrayIndexHash(
          static_cast<uint32_t>(value), raw->length());
      raw->set_raw_hash_field(raw_hash_field);
    }
  }
  return result;
}

Handle<DebugInfo> Factory::NewDebugInfo(
    DirectHandle<SharedFunctionInfo> shared) {
  DCHECK(!shared->HasDebugInfo(isolate()));

  auto debug_info =
      NewStructInternal<DebugInfo>(DEBUG_INFO_TYPE, AllocationType::kOld);
  DisallowGarbageCollection no_gc;
  Tagged<SharedFunctionInfo> raw_shared = *shared;
  debug_info->set_flags(DebugInfo::kNone, kRelaxedStore);
  debug_info->set_shared(raw_shared);
  debug_info->set_debugger_hints(0);
  DCHECK_EQ(DebugInfo::kNoDebuggingId, debug_info->debugging_id());
  debug_info->set_break_points(*empty_fixed_array(), SKIP_WRITE_BARRIER);
  debug_info->clear_original_bytecode_array();
  debug_info->clear_debug_bytecode_array();

  return handle(debug_info, isolate());
}

Handle<BreakPointInfo> Factory::NewBreakPointInfo(int source_position) {
  auto new_break_point_info = NewStructInternal<BreakPointInfo>(
      BREAK_POINT_INFO_TYPE, AllocationType::kOld);
  DisallowGarbageCollection no_gc;
  new_break_point_info->set_source_position(source_position);
  new_break_point_info->set_break_points(*undefined_value(),
                                         SKIP_WRITE_BARRIER);
  return handle(new_break_point_info, isolate());
}

Handle<BreakPoint> Factory::NewBreakPoint(int id,
                                          DirectHandle<String> condition) {
  auto new_break_point =
      NewStructInternal<BreakPoint>(BREAK_POINT_TYPE, AllocationType::kOld);
  DisallowGarbageCollection no_gc;
  new_break_point->set_id(id);
  new_break_point->set_condition(*condition);
  return handle(new_break_point, isolate());
}

Handle<CallSiteInfo> Factory::NewCallSiteInfo(
    DirectHandle<JSAny> receiver_or_instance,
    DirectHandle<UnionOf<Smi, JSFunction>> function,
    DirectHandle<HeapObject> code_object, int code_offset_or_source_position,
    int flags, DirectHandle<FixedArray> parameters) {
  auto info = NewStructInternal<CallSiteInfo>(CALL_SITE_INFO_TYPE,
                                              AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  info->set_receiver_or_instance(*receiver_or_instance, SKIP_WRITE_BARRIER);
  info->set_function(*function, SKIP_WRITE_BARRIER);
  info->set_code_object(*code_object, SKIP_WRITE_BARRIER);
  info->set_code_offset_or_source_position(code_offset_or_source_position);
  info->set_flags(flags);
  info->set_parameters(*parameters, SKIP_WRITE_BARRIER);
  return handle(info, isolate());
}

Handle<StackFrameInfo> Factory::NewStackFrameInfo(
    DirectHandle<UnionOf<SharedFunctionInfo, Script>> shared_or_script,
    int bytecode_offset_or_source_position, DirectHandle<String> function_name,
    bool is_constructor) {
  DCHECK_GE(bytecode_offset_or_source_position, 0);
  Tagged<StackFrameInfo> info = NewStructInternal<StackFrameInfo>(
      STACK_FRAME_INFO_TYPE, AllocationType::kYoung);
  DisallowGarbageCollection no_gc;
  info->set_flags(0);
  info->set_shared_or_script(*shared_or_script, SKIP_WRITE_BARRIER);
  info->set_bytecode_offset_or_source_position(
      bytecode_offset_or_source_position);
  info->set_function_name(*function_name, SKIP_WRITE_BARRIER);
  info->set_is_constructor(is_constructor);
  return handle(info, isolate());
}

Handle<JSObject> Factory::NewArgumentsObject(Handle<JSFunction> callee,
                                             int length) {
  bool strict_mode_callee = is_strict(callee->shared()->language_mode()) ||
                            !callee->shared()->has_simple_parameters();
  DirectHandle<Map> map = strict_mode_callee
                              ? isolate()->strict_arguments_map()
                              : isolate()->sloppy_arguments_map();
  AllocationSiteUsageContext context(isolate(), Handle<AllocationSite>(),
                                     false);
  DCHECK(!isolate()->has_exception());
  Handle<JSObject> result = NewJSObjectFromMap(map);
  Handle<Smi> value(Smi::FromInt(length), isolate());
  Object::SetProperty(isolate(), result, length_string(), value,
                      StoreOrigin::kMaybeKeyed,
                      Just(ShouldThrow::kThrowOnError))
      .Assert();
  if (!strict_mode_callee) {
    Object::SetProperty(isolate(), result, callee_string(), callee,
                        StoreOrigin::kMaybeKeyed,
                        Just(ShouldThrow::kThrowOnError))
        .Assert();
  }
  return result;
}

Handle<Map> Factory::ObjectLiteralMapFromCache(
    DirectHandle<NativeContext> context, int number_of_properties) {
  // Use initial slow object proto map for too many properties.
  if (number_of_properties >= JSObject::kMapCacheSize) {
    return handle(context->slow_object_with_object_prototype_map(), isolate());
  }
  // TODO(chromium:1503456): remove once fixed.
  CHECK_LE(0, number_of_properties);

  DirectHandle<WeakFixedArray> cache(Cast<WeakFixedArray>(context->map_cache()),
                                     isolate());

  // Check to see whether there is a matching element in the cache.
  Tagged<MaybeObject> result = cache->get(number_of_properties);
  Tagged<HeapObject> heap_object;
  if (result.GetHeapObjectIfWeak(&heap_object)) {
    Tagged<Map> map = Cast<Map>(heap_object);
    DCHECK(!map->is_dictionary_map());
    return handle(map, isolate());
  }

  // Create a new map and add it to the cache.
  Handle<Map> map = Map::Create(isolate(), number_of_properties);
  DCHECK(!map->is_dictionary_map());
  cache->set(number_of_properties, MakeWeak(*map));
  return map;
}

Handle<MegaDomHandler> Factory::NewMegaDomHandler(MaybeObjectHandle accessor,
                                                  MaybeObjectHandle context) {
  DirectHandle<Map> map = read_only_roots().mega_dom_handler_map_handle();
  Tagged<MegaDomHandler> handler =
      Cast<MegaDomHandler>(New(map, AllocationType::kOld));
  DisallowGarbageCollection no_gc;
  handler->set_accessor(*accessor, kReleaseStore);
  handler->set_context(*context);
  return handle(handler, isolate());
}

Handle<LoadHandler> Factory::NewLoadHandler(int data_count,
                                            AllocationType allocation) {
  DirectHandle<Map> map;
  switch (data_count) {
    case 1:
      map = load_handler1_map();
      break;
    case 2:
      map = load_handler2_map();
      break;
    case 3:
      map = load_handler3_map();
      break;
    default:
      UNREACHABLE();
  }
  return handle(Cast<LoadHandler>(New(map, allocation)), isolate());
}

Handle<StoreHandler> Factory::NewStoreHandler(int data_count) {
  DirectHandle<Map> map;
  switch (data_count) {
    case 0:
      map = store_handler0_map();
      break;
    case 1:
      map = store_handler1_map();
      break;
    case 2:
      map = store_handler2_map();
      break;
    case 3:
      map = store_handler3_map();
      break;
    default:
      UNREACHABLE();
  }
  return handle(Cast<StoreHandler>(New(map, AllocationType::kOld)), isolate());
}

void Factory::SetRegExpAtomData(DirectHandle<JSRegExp> regexp,
                                DirectHandle<String> source,
                                JSRegExp::Flags flags,
                                DirectHandle<String> pattern) {
  DirectHandle<RegExpData> regexp_data =
      NewAtomRegExpData(source, flags, pattern);
  regexp->set_data(*regexp_data);
}

void Factory::SetRegExpIrregexpData(DirectHandle<JSRegExp> regexp,
                                    DirectHandle<String> source,
                                    JSRegExp::Flags flags, int capture_count,
                                    uint32_t backtrack_limit) {
  DirectHandle<RegExpData> regexp_data =
      NewIrRegExpData(source, flags, capture_count, backtrack_limit);
  regexp->set_data(*regexp_data);
}

void Factory::SetRegExpExperimentalData(DirectHandle<JSRegExp> regexp,
                                        DirectHandle<String> source,
                                        JSRegExp::Flags flags,
                                        int capture_count) {
  DirectHandle<RegExpData> regexp_data =
      NewExperimentalRegExpData(source, flags, capture_count);
  regexp->set_data(*regexp_data);
}

Handle<RegExpData> Factory::NewAtomRegExpData(DirectHandle<String> source,
                                              JSRegExp::Flags flags,
                                              DirectHandle<String> pattern) {
  DirectHandle<RegExpDataWrapper> wrapper = NewRegExpDataWrapper();
  int size = AtomRegExpData::kSize;
  Tagged<HeapObject> result = AllocateRawWithImmortalMap(
      size, AllocationType::kTrusted, read_only_roots().atom_regexp_data_map());
  DisallowGarbageCollection no_gc;
  Tagged<AtomRegExpData> instance = Cast<AtomRegExpData>(result);
  instance->init_self_indirect_pointer(isolate());
  instance->set_type_tag(RegExpData::Type::ATOM);
  instance->set_source(*source);
  instance->set_flags(flags);
  instance->set_pattern(*pattern);
  Tagged<RegExpDataWrapper> raw_wrapper = *wrapper;
  instance->set_wrapper(raw_wrapper);
  raw_wrapper->set_data(instance);
  return handle(instance, isolate());
}

Handle<RegExpData> Factory::NewIrRegExpData(DirectHandle<String> source,
                                            JSRegExp::Flags flags,
                                            int capture_count,
                                            uint32_t backtrack_limit) {
  DirectHandle<RegExpDataWrapper> wrapper = NewRegExpDataWrapper();
  int size = IrRegExpData::kSize;
  Tagged<HeapObject> result = AllocateRawWithImmortalMap(
      size, AllocationType::kTrusted, read_only_roots().ir_regexp_data_map());
  DisallowGarbageCollection no_gc;
  Tagged<IrRegExpData> instance = Cast<IrRegExpData>(result);
  instance->init_self_indirect_pointer(isolate());
  instance->set_type_tag(RegExpData::Type::IRREGEXP);
  instance->set_source(*source);
  instance->set_flags(flags);
  instance->clear_latin1_code();
  instance->clear_uc16_code();
  instance->clear_latin1_bytecode();
  instance->clear_uc16_bytecode();
  instance->set_capture_name_map(Smi::FromInt(JSRegExp::kUninitializedValue));
  instance->set_max_register_count(JSRegExp::kUninitializedValue);
  instance->set_capture_count(capture_count);
  int ticks_until_tier_up = v8_flags.regexp_tier_up
                                ? v8_flags.regexp_tier_up_ticks
                                : JSRegExp::kUninitializedValue;
  instance->set_ticks_until_tier_up(ticks_until_tier_up);
  instance->set_backtrack_limit(backtrack_limit);
  Tagged<RegExpDataWrapper> raw_wrapper = *wrapper;
  instance->set_wrapper(raw_wrapper);
  raw_wrapper->set_data(instance);
  return handle(instance, isolate());
}

Handle<RegExpData> Factory::NewExperimentalRegExpData(
    DirectHandle<String> source, JSRegExp::Flags flags, int capture_count) {
  DirectHandle<RegExpDataWrapper> wrapper = NewRegExpDataWrapper();
  int size = IrRegExpData::kSize;
  Tagged<HeapObject> result = AllocateRawWithImmortalMap(
      size, AllocationType::kTrusted, read_only_roots().ir_regexp_data_map());
  DisallowGarbageCollection no_gc;
  Tagged<IrRegExpData> instance = Cast<IrRegExpData>(result);
  // TODO(mbid,v8:10765): At the moment the ExperimentalRegExpData is just an
  // alias of IrRegExpData, with most fields set to some default/uninitialized
  // value. This is because EXPERIMENTAL and IRREGEXP regexps take the same code
  // path in `RegExpExecInternal`, which reads off various fields from this
  // struct. `RegExpExecInternal` should probably distinguish between
  // EXPERIMENTAL and IRREGEXP, and then we can get rid of all the IRREGEXP only
  // fields.
  instance->init_self_indirect_pointer(isolate());
  instance->set_type_tag(RegExpData::Type::EXPERIMENTAL);
  instance->set_source(*source);
  instance->set_flags(flags);
  instance->clear_latin1_code();
  instance->clear_uc16_code();
  instance->clear_latin1_bytecode();
  instance->clear_uc16_bytecode();
  instance->set_capture_name_map(Smi::FromInt(JSRegExp::kUninitializedValue));
  instance->set_max_register_count(JSRegExp::kUninitializedValue);
  instance->set_capture_count(capture_count);
  instance->set_ticks_until_tier_up(JSRegExp::kUninitializedValue);
  instance->set_backtrack_limit(JSRegExp::kUninitializedValue);
  Tagged<RegExpDataWrapper> raw_wrapper = *wrapper;
  instance->set_wrapper(raw_wrapper);
  raw_wrapper->set_data(instance);
  return handle(instance, isolate());
}

Handle<Object> Factory::GlobalConstantFor(Handle<Name> name) {
  if (Name::Equals(isolate(), name, undefined_string())) {
    return undefined_value();
  }
  if (Name::Equals(isolate(), name, NaN_string())) return nan_value();
  if (Name::Equals(isolate(), name, Infinity_string())) return infinity_value();
  return Handle<Object>::null();
}

Handle<String> Factory::ToPrimitiveHintString(ToPrimitiveHint hint) {
  switch (hint) {
    case ToPrimitiveHint::kDefault:
      return default_string();
    case ToPrimitiveHint::kNumber:
      return number_string();
    case ToPrimitiveHint::kString:
      return string_string();
  }
  UNREACHABLE();
}

Handle<Map> Factory::CreateSloppyFunctionMap(
    FunctionMode function_mode, MaybeHandle<JSFunction> maybe_empty_function) {
  bool has_prototype = IsFunctionModeWithPrototype(function_mode);
  int header_size = has_prototype ? JSFunction::kSizeWithPrototype
                                  : JSFunction::kSizeWithoutPrototype;
  int descriptors_count = has_prototype ? 5 : 4;
  int inobject_properties_count = 0;
  if (IsFunctionModeWithName(function_mode)) ++inobject_properties_count;

  Handle<Map> map = NewContextfulMapForCurrentContext(
      JS_FUNCTION_TYPE, header_size + inobject_properties_count * kTaggedSize,
      TERMINAL_FAST_ELEMENTS_KIND, inobject_properties_count);
  {
    DisallowGarbageCollection no_gc;
    Tagged<Map> raw_map = *map;
    raw_map->set_has_prototype_slot(has_prototype);
    raw_map->set_is_constructor(has_prototype);
    raw_map->set_is_callable(true);
  }
  Handle<JSFunction> empty_function;
  if (maybe_empty_function.ToHandle(&empty_function)) {
    // Temporarily set constructor to empty function to calm down map verifier.
    map->SetConstructor(*empty_function);
    Map::SetPrototype(isolate(), map, empty_function);
  } else {
    // |maybe_empty_function| is allowed to be empty only during empty function
    // creation.
    DCHECK(IsUndefined(
        isolate()->raw_native_context()->get(Context::EMPTY_FUNCTION_INDEX)));
  }

  //
  // Setup descriptors array.
  //
  Map::EnsureDescriptorSlack(isolate(), map, descriptors_count);

  PropertyAttributes ro_attribs =
      static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE | READ_ONLY);
  PropertyAttributes rw_attribs =
      static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE);
  PropertyAttributes roc_attribs =
      static_cast<PropertyAttributes>(DONT_ENUM | READ_ONLY);

  int field_index = 0;
  static_assert(
      JSFunctionOrBoundFunctionOrWrappedFunction::kLengthDescriptorIndex == 0);
  {  // Add length accessor.
    Descriptor d = Descriptor::AccessorConstant(
        length_string(), function_length_accessor(), roc_attribs);
    map->AppendDescriptor(isolate(), &d);
  }

  static_assert(
      JSFunctionOrBoundFunctionOrWrappedFunction::kNameDescriptorIndex == 1);
  if (IsFunctionModeWithName(function_mode)) {
    // Add name field.
    Handle<Name> name = isolate()->factory()->name_string();
    Descriptor d = Descriptor::DataField(isolate(), name, field_index++,
                                         roc_attribs, Representation::Tagged());
    map->AppendDescriptor(isolate(), &d);

  } else {
    // Add name accessor.
    Descriptor d = Descriptor::AccessorConstant(
        name_string(), function_name_accessor(), roc_attribs);
    map->AppendDescriptor(isolate(), &d);
  }
  {  // Add arguments accessor.
    Descriptor d = Descriptor::AccessorConstant(
        arguments_string(), function_arguments_accessor(), ro_attribs);
    map->AppendDescriptor(isolate(), &d);
  }
  {  // Add caller accessor.
    Descriptor d = Descriptor::AccessorConstant(
        caller_string(), function_caller_accessor(), ro_attribs);
    map->AppendDescriptor(isolate(), &d);
  }
  if (IsFunctionModeWithPrototype(function_mode)) {
    // Add prototype accessor.
    PropertyAttributes attribs =
        IsFunctionModeWithWritablePrototype(function_mode) ? rw_attribs
                                                           : ro_attribs;
    Descriptor d = Descriptor::AccessorConstant(
        prototype_string(), function_prototype_accessor(), attribs);
    map->AppendDescriptor(isolate(), &d);
  }
  DCHECK_EQ(inobject_properties_count, field_index);
  DCHECK_EQ(
      0, map->instance_descriptors(isolate())->number_of_slack_descriptors());
  LOG(isolate(), MapDetails(*map));
  return map;
}

Handle<Map> Factory::CreateStrictFunctionMap(
    FunctionMode function_mode, Handle<JSFunction> empty_function) {
  bool has_prototype = IsFunctionModeWithPrototype(function_mode);
  int header_size = has_prototype ? JSFunction::kSizeWithPrototype
                                  : JSFunction::kSizeWithoutPrototype;
  int inobject_properties_count = 0;
  // length and prototype accessors or just length accessor.
  int descriptors_count = IsFunctionModeWithPrototype(function_mode) ? 2 : 1;
  if (IsFunctionModeWithName(function_mode)) {
    ++inobject_properties_count;  // name property.
  } else {
    ++descriptors_count;  // name accessor.
  }
  descriptors_count += inobject_properties_count;

  Handle<Map> map = NewContextfulMapForCurrentContext(
      JS_FUNCTION_TYPE, header_size + inobject_properties_count * kTaggedSize,
      TERMINAL_FAST_ELEMENTS_KIND, inobject_properties_count);
  {
    DisallowGarbageCollection no_gc;
    Tagged<Map> raw_map = *map;
    raw_map->set_has_prototype_slot(has_prototype);
    raw_map->set_is_constructor(has_prototype);
    raw_map->set_is_callable(true);
    // Temporarily set constructor to empty function to calm down map verifier.
    raw_map->SetConstructor(*empty_function);
  }
  Map::SetPrototype(isolate(), map, empty_function);

  //
  // Setup descriptors array.
  //
  Map::EnsureDescriptorSlack(isolate(), map, descriptors_count);

  PropertyAttributes rw_attribs =
      static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE);
  PropertyAttributes ro_attribs =
      static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE | READ_ONLY);
  PropertyAttributes roc_attribs =
      static_cast<PropertyAttributes>(DONT_ENUM | READ_ONLY);

  int field_index = 0;
  static_assert(JSFunction::kLengthDescriptorIndex == 0);
  {  // Add length accessor.
    Descriptor d = Descriptor::AccessorConstant(
        length_string(), function_length_accessor(), roc_attribs);
    map->AppendDescriptor(isolate(), &d);
  }

  static_assert(JSFunction::kNameDescriptorIndex == 1);
  if (IsFunctionModeWithName(function_mode)) {
    // Add name field.
    Handle<Name> name = isolate()->factory()->name_string();
    Descriptor d = Descriptor::DataField(isolate(), name, field_index++,
                                         roc_attribs, Representation::Tagged());
    map->AppendDescriptor(isolate(), &d);

  } else {
    // Add name accessor.
    Descriptor d = Descriptor::AccessorConstant(
        name_string(), function_name_accessor(), roc_attribs);
    map->AppendDescriptor(isolate(), &d);
  }

  if (IsFunctionModeWithPrototype(function_mode)) {
    // Add prototype accessor.
    PropertyAttributes attribs =
        IsFunctionModeWithWritablePrototype(function_mode) ? rw_attribs
                                                           : ro_attribs;
    Descriptor d = Descriptor::AccessorConstant(
        prototype_string(), function_prototype_accessor(), attribs);
    map->AppendDescriptor(isolate(), &d);
  }
  DCHECK_EQ(inobject_properties_count, field_index);
  DCHECK_EQ(
      0, map->instance_descriptors(isolate())->number_of_slack_descriptors());
  LOG(isolate(), MapDetails(*map));
  return map;
}

Handle<Map> Factory::CreateClassFunctionMap(Handle<JSFunction> empty_function) {
  Handle<Map> map = NewContextfulMapForCurrentContext(
      JS_CLASS_CONSTRUCTOR_TYPE, JSFunction::kSizeWithPrototype);
  {
    DisallowGarbageCollection no_gc;
    Tagged<Map> raw_map = *map;
    raw_map->set_has_prototype_slot(true);
    raw_map->set_is_constructor(true);
    raw_map->set_is_prototype_map(true);
    raw_map->set_is_callable(true);
    // Temporarily set constructor to empty function to calm down map verifier.
    raw_map->SetConstructor(*empty_function);
  }
  Map::SetPrototype(isolate(), map, empty_function);

  //
  // Setup descriptors array.
  //
  Map::EnsureDescriptorSlack(isolate(), map, 2);

  PropertyAttributes ro_attribs =
      static_cast<PropertyAttributes>(DONT_ENUM | DONT_DELETE | READ_ONLY);
  PropertyAttributes roc_attribs =
      static_cast<PropertyAttributes>(DONT_ENUM | READ_ONLY);

  static_assert(JSFunction::kLengthDescriptorIndex == 0);
  {  // Add length accessor.
    Descriptor d = Descriptor::AccessorConstant(
        length_string(), function_length_accessor(), roc_attribs);
    map->AppendDescriptor(isolate(), &d);
  }

  {
    // Add prototype accessor.
    Descriptor d = Descriptor::AccessorConstant(
        prototype_string(), function_prototype_accessor(), ro_attribs);
    map->AppendDescriptor(isolate(), &d);
  }
  LOG(isolate(), MapDetails(*map));
  return map;
}

Handle<JSPromise> Factory::NewJSPromiseWithoutHook() {
  Handle<JSPromise> promise =
      Cast<JSPromise>(NewJSObject(isolate()->promise_function()));
  DisallowGarbageCollection no_gc;
  Tagged<JSPromise> raw = *promise;
  raw->set_reactions_or_result(Smi::zero(), SKIP_WRITE_BARRIER);
  raw->set_flags(0);
  // TODO(v8) remove once embedder data slots are always zero-initialized.
  InitEmbedderFields(*promise, Smi::zero());
  DCHECK_EQ(raw->GetEmbedderFieldCount(), v8::Promise::kEmbedderFieldCount);
  return promise;
}

Handle<JSPromise> Factory::NewJSPromise() {
  Handle<JSPromise> promise = NewJSPromiseWithoutHook();
  isolate()->RunAllPromiseHooks(PromiseHookType::kInit, promise,
                                undefined_value());
  return promise;
}

bool Factory::CanAllocateInReadOnlySpace() {
  return allocator()->CanAllocateInReadOnlySpace();
}

bool Factory::EmptyStringRootIsInitialized() {
  return isolate()->roots_table()[RootIndex::kempty_string] != kNullAddress;
}

AllocationType Factory::AllocationTypeForInPlaceInternalizableString() {
  return isolate()
      ->heap()
      ->allocation_type_for_in_place_internalizable_strings();
}

Handle<JSFunction> Factory::NewFunctionForTesting(DirectHandle<String> name) {
  Handle<SharedFunctionInfo> info =
      NewSharedFunctionInfoForBuiltin(name, Builtin::kIllegal);
  info->set_language_mode(LanguageMode::kSloppy);
  return JSFunctionBuilder{isolate(), info, isolate()->native_context()}
      .Build();
}

Handle<JSSharedStruct> Factory::NewJSSharedStruct(
    Handle<JSFunction> constructor,
    MaybeHandle<NumberDictionary> maybe_elements_template) {
  SharedObjectSafePublishGuard publish_guard;

  DirectHandle<Map> instance_map(constructor->initial_map(), isolate());
  DirectHandle<PropertyArray> property_array;
  const int num_oob_fields =
      instance_map->NumberOfFields(ConcurrencyMode::kSynchronous) -
      instance_map->GetInObjectProperties();
  if (num_oob_fields > 0) {
    property_array =
        NewPropertyArray(num_oob_fields, AllocationType::kSharedOld);
  }

  Handle<NumberDictionary> elements_dictionary;
  bool has_elements_dictionary;
  if ((has_elements_dictionary =
           maybe_elements_template.ToHandle(&elements_dictionary))) {
    elements_dictionary = NumberDictionary::ShallowCopy(
        isolate(), elements_dictionary, AllocationType::kSharedOld);
  }

  Handle<JSSharedStruct> instance = Cast<JSSharedStruct>(
      NewJSObject(constructor, AllocationType::kSharedOld));

  // The struct object has not been fully initialized yet. Disallow allocation
  // from this point on.
  DisallowGarbageCollection no_gc;
  if (!property_array.is_null()) instance->SetProperties(*property_array);
  if (has_elements_dictionary) instance->set_elements(*elements_dictionary);

  return instance;
}

Handle<JSSharedArray> Factory::NewJSSharedArray(Handle<JSFunction> constructor,
                                                int length) {
  SharedObjectSafePublishGuard publish_guard;
  DirectHandle<FixedArrayBase> storage =
      NewFixedArray(length, AllocationType::kSharedOld);
  auto instance =
      Cast<JSSharedArray>(NewJSObject(constructor, AllocationType::kSharedOld));
  instance->set_elements(*storage);
  FieldIndex index = FieldIndex::ForDescriptor(
      constructor->initial_map(),
      InternalIndex(JSSharedArray::kLengthFieldIndex));
  instance->FastPropertyAtPut(index, Smi::FromInt(length), SKIP_WRITE_BARRIER);
  return instance;
}

Handle<JSAtomicsMutex> Factory::NewJSAtomicsMutex() {
  SharedObjectSafePublishGuard publish_guard;
  DirectHandle<Map> map = read_only_roots().js_atomics_mutex_map_handle();
  auto mutex =
      Cast<JSAtomicsMutex>(NewJSObjectFromMap(map, AllocationType::kSharedOld));
  mutex->set_state(JSAtomicsMutex::kUnlockedUncontended);
  mutex->set_owner_thread_id(ThreadId::Invalid().ToInteger());
  mutex->SetNullWaiterQueueHead();
  return mutex;
}

Handle<JSAtomicsCondition> Factory::NewJSAtomicsCondition() {
  SharedObjectSafePublishGuard publish_guard;
  DirectHandle<Map> map = read_only_roots().js_atomics_condition_map_handle();
  Handle<JSAtomicsCondition> cond = Cast<JSAtomicsCondition>(
      NewJSObjectFromMap(map, AllocationType::kSharedOld));
  cond->set_state(JSAtomicsCondition::kEmptyState);
  cond->SetNullWaiterQueueHead();
  return cond;
}

namespace {

inline void InitializeTemplate(Tagged<TemplateInfo> that, ReadOnlyRoots roots,
                               bool do_not_cache) {
  that->set_number_of_properties(0);
  int serial_number =
      do_not_cache ? TemplateInfo::kDoNotCache : TemplateInfo::kUncached;
  that->set_serial_number(serial_number);
  that->set_property_list(roots.undefined_value(), SKIP_WRITE_BARRIER);
  that->set_property_accessors(roots.undefined_value(), SKIP_WRITE_BARRIER);
}

}  // namespace

Handle<FunctionTemplateInfo> Factory::NewFunctionTemplateInfo(
    int length, bool do_not_cache) {
  const int size = FunctionTemplateInfo::SizeFor();
  Tagged<FunctionTemplateInfo> obj =
      Cast<FunctionTemplateInfo>(AllocateRawWithImmortalMap(
          size, AllocationType::kOld,
          read_only_roots().function_template_info_map()));
  {
    // Disallow GC until all fields of obj have acceptable types.
    DisallowGarbageCollection no_gc;
    Tagged<FunctionTemplateInfo> raw = *obj;
    ReadOnlyRoots roots(isolate());
    InitializeTemplate(raw, roots, do_not_cache);
    raw->set_class_name(roots.undefined_value(), SKIP_WRITE_BARRIER);
    raw->set_interface_name(roots.undefined_value(), SKIP_WRITE_BARRIER);
    raw->set_signature(roots.undefined_value(), SKIP_WRITE_BARRIER);
    raw->set_rare_data(roots.undefined_value(), kReleaseStore,
                       SKIP_WRITE_BARRIER);
    raw->set_shared_function_info(roots.undefined_value(), SKIP_WRITE_BARRIER);
    raw->set_cached_property_name(roots.the_hole_value(), SKIP_WRITE_BARRIER);

    raw->set_flag(0, kRelaxedStore);
    raw->set_undetectable(false);
    raw->set_needs_access_check(false);
    raw->set_accept_any_receiver(true);
    raw->set_exception_context(
        static_cast<uint32_t>(ExceptionContext::kUnknown));

    raw->set_length(length);
    raw->SetInstanceType(0);
    raw->init_callback(isolate(), kNullAddress);
    raw->set_callback_data(roots.the_hole_value(), kReleaseStore,
                           SKIP_WRITE_BARRIER);
  }
  return handle(obj, isolate());
}

Handle<ObjectTemplateInfo> Factory::NewObjectTemplateInfo(
    DirectHandle<FunctionTemplateInfo> constructor, bool do_not_cache) {
  const int size = ObjectTemplateInfo::SizeFor();
  Tagged<ObjectTemplateInfo> obj = Cast<ObjectTemplateInfo>(
      AllocateRawWithImmortalMap(size, AllocationType::kOld,
                                 read_only_roots().object_template_info_map()));
  {
    // Disallow GC until all fields of obj have acceptable types.
    DisallowGarbageCollection no_gc;
    Tagged<ObjectTemplateInfo> raw = *obj;
    ReadOnlyRoots roots(isolate());
    InitializeTemplate(raw, roots, do_not_cache);
    if (constructor.is_null()) {
      raw->set_constructor(roots.undefined_value(), SKIP_WRITE_BARRIER);
    } else {
      raw->set_constructor(*constructor);
    }
    raw->set_data(0);
  }
  return handle(obj, isolate());
}

Handle<DictionaryTemplateInfo> Factory::NewDictionaryTemplateInfo(
    DirectHandle<FixedArray> property_names) {
  const int size = DictionaryTemplateInfo::SizeFor();
  DirectHandle<Map> map =
      read_only_roots().dictionary_template_info_map_handle();
  Tagged<DictionaryTemplateInfo> obj = Cast<DictionaryTemplateInfo>(
      AllocateRawWithImmortalMap(size, AllocationType::kOld, *map));
  obj->set_property_names(*property_names);
  obj->set_serial_number(TemplateInfo::kUncached);
  return handle(obj, isolate());
}

Handle<TrustedForeign> Factory::NewTrustedForeign(Address addr) {
  // Statically ensure that it is safe to allocate foreigns in paged spaces.
  static_assert(TrustedForeign::kSize <= kMaxRegularHeapObjectSize);
  Tagged<Map> map = *trusted_foreign_map();
  Tagged<TrustedForeign> foreign =
      Cast<TrustedForeign>(AllocateRawWithImmortalMap(
          map->instance_size(), AllocationType::kTrusted, map));
  DisallowGarbageCollection no_gc;
  foreign->set_foreign_address(addr);
  return handle(foreign, isolate());
}

Factory::JSFunctionBuilder::JSFunctionBuilder(Isolate* isolate,
                                              Handle<SharedFunctionInfo> sfi,
                                              Handle<Context> context)
    : isolate_(isolate), sfi_(sfi), context_(context) {}

Handle<JSFunction> Factory::JSFunctionBuilder::Build() {
  PrepareMap();
  PrepareFeedbackCell();

  DirectHandle<Code> code(sfi_->GetCode(isolate_), isolate_);
  // Retain the code across the call to BuildRaw, because it allocates and can
  // trigger code to be flushed. Otherwise the SFI's compiled state and the
  // function's compiled state can diverge, and the call to PostInstantiation
  // below can fail to initialize the feedback vector.
  IsCompiledScope is_compiled_scope(sfi_->is_compiled_scope(isolate_));
  Handle<JSFunction> result = BuildRaw(code);

  if (code->kind() == CodeKind::BASELINE) {
    JSFunction::EnsureFeedbackVector(isolate_, result, &is_compiled_scope);
  }

  Compiler::PostInstantiation(isolate_, result, &is_compiled_scope);
  return result;
}

Handle<JSFunction> Factory::JSFunctionBuilder::BuildRaw(
    DirectHandle<Code> code) {
  Isolate* isolate = isolate_;
  Factory* factory = isolate_->factory();

  DirectHandle<Map> map = maybe_map_.ToHandleChecked();
  DirectHandle<FeedbackCell> feedback_cell =
      maybe_feedback_cell_.ToHandleChecked();

  DCHECK(InstanceTypeChecker::IsJSFunction(*map));

  // Allocation.
  Tagged<JSFunction> function =
      Cast<JSFunction>(factory->New(map, allocation_type_));
  DisallowGarbageCollection no_gc;

  WriteBarrierMode mode = allocation_type_ == AllocationType::kYoung
                              ? SKIP_WRITE_BARRIER
                              : UPDATE_WRITE_BARRIER;
  // Header initialization.
  function->initialize_properties(isolate);
  function->initialize_elements();
  function->set_shared(*sfi_, mode);
  function->set_context(*context_, kReleaseStore, mode);
  function->set_raw_feedback_cell(*feedback_cell, mode);
#ifdef V8_ENABLE_LEAPTIERING
  // If the FeedbackCell doesn't have a dispatch handle, we need to allocate a
  // dispatch entry now. This should only be the case for functions using the
  // generic many_closures_cell (for example builtin functions), and only for
  // functions using certain kinds of code.
  if (!feedback_cell->dispatch_handle()) {
    DCHECK_EQ(*feedback_cell, *factory->many_closures_cell());
    // We currently only expect to see these kinds of Code here. For BASELINE
    // code, we will allocate a FeedbackCell after building the JSFunction. See
    // JSFunctionBuilder::Build.
    DCHECK(code->kind() == CodeKind::BUILTIN ||
           code->kind() == CodeKind::JS_TO_WASM_FUNCTION ||
           code->kind() == CodeKind::BASELINE);
    // TODO(saelo): in the future, we probably want to use
    // code->parameter_count() here instead, but not all Code objects know
    // their parameter count yet.
    function->initialize_dispatch_handle(
        isolate, sfi_->internal_formal_parameter_count_with_receiver());
  } else {
    function->set_dispatch_handle(feedback_cell->dispatch_handle());
  }
#endif  // V8_ENABLE_LEAPTIERING
  function->set_code(*code, kReleaseStore, mode);
  if (function->has_prototype_slot()) {
    function->set_prototype_or_initial_map(
        ReadOnlyRoots(isolate).the_hole_value(), kReleaseStore,
        SKIP_WRITE_BARRIER);
  }

  // Potentially body initialization.
  factory->InitializeJSObjectBody(
      function, *map, JSFunction::GetHeaderSize(map->has_prototype_slot()));

  return handle(function, isolate_);
}

void Factory::JSFunctionBuilder::PrepareMap() {
  if (maybe_map_.is_null()) {
    // No specific map requested, use the default.
    maybe_map_ = handle(
        Cast<Map>(context_->native_context()->get(sfi_->function_map_index())),
        isolate_);
  }
}

void Factory::JSFunctionBuilder::PrepareFeedbackCell() {
  Handle<FeedbackCell> feedback_cell;
  if (maybe_feedback_cell_.ToHandle(&feedback_cell)) {
    // Track the newly-created closure.
    feedback_cell->IncrementClosureCount(isolate_);
  } else {
    // Fall back to the many_closures_cell.
    maybe_feedback_cell_ = isolate_->factory()->many_closures_cell();
  }
}

}  // namespace internal
}  // namespace v8

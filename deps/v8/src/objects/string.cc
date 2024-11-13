// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/string.h"

#include "src/base/small-vector.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/execution/isolate-utils.h"
#include "src/execution/thread-id.h"
#include "src/handles/handles-inl.h"
#include "src/heap/heap-inl.h"
#include "src/heap/local-factory-inl.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/read-only-heap.h"
#include "src/numbers/conversions.h"
#include "src/objects/instance-type.h"
#include "src/objects/map.h"
#include "src/objects/oddball.h"
#include "src/objects/string-comparator.h"
#include "src/objects/string-inl.h"
#include "src/strings/char-predicates.h"
#include "src/strings/string-builder-inl.h"
#include "src/strings/string-hasher.h"
#include "src/strings/string-search.h"
#include "src/strings/string-stream.h"
#include "src/strings/unicode-inl.h"
#include "src/utils/ostreams.h"

namespace v8 {
namespace internal {

Handle<String> String::SlowFlatten(Isolate* isolate, Handle<ConsString> cons,
                                   AllocationType allocation) {
  DCHECK_NE(cons->second()->length(), 0);
  DCHECK(!InAnySharedSpace(*cons));

  // TurboFan can create cons strings with empty first parts.
  while (cons->first()->length() == 0) {
    // We do not want to call this function recursively. Therefore we call
    // String::Flatten only in those cases where String::SlowFlatten is not
    // called again.
    if (IsConsString(cons->second()) && !cons->second()->IsFlat()) {
      cons = handle(Cast<ConsString>(cons->second()), isolate);
    } else {
      return String::Flatten(isolate, handle(cons->second(), isolate),
                             allocation);
    }
  }

  DCHECK(AllowGarbageCollection::IsAllowed());
  int length = cons->length();
  if (allocation != AllocationType::kSharedOld) {
    allocation =
        ObjectInYoungGeneration(*cons) ? allocation : AllocationType::kOld;
  }
  Handle<SeqString> result;
  if (cons->IsOneByteRepresentation()) {
    Handle<SeqOneByteString> flat =
        isolate->factory()
            ->NewRawOneByteString(length, allocation)
            .ToHandleChecked();
    // When the ConsString had a forwarding index, it is possible that it was
    // transitioned to a ThinString (and eventually shortcutted to
    // InternalizedString) during GC.
    if (V8_UNLIKELY(v8_flags.always_use_string_forwarding_table &&
                    !IsConsString(*cons))) {
      DCHECK(IsInternalizedString(*cons) || IsThinString(*cons));
      return String::Flatten(isolate, cons, allocation);
    }
    DisallowGarbageCollection no_gc;
    WriteToFlat(*cons, flat->GetChars(no_gc), 0, length);
    result = flat;
  } else {
    Handle<SeqTwoByteString> flat =
        isolate->factory()
            ->NewRawTwoByteString(length, allocation)
            .ToHandleChecked();
    // When the ConsString had a forwarding index, it is possible that it was
    // transitioned to a ThinString (and eventually shortcutted to
    // InternalizedString) during GC.
    if (V8_UNLIKELY(v8_flags.always_use_string_forwarding_table &&
                    !IsConsString(*cons))) {
      DCHECK(IsInternalizedString(*cons) || IsThinString(*cons));
      return String::Flatten(isolate, cons, allocation);
    }
    DisallowGarbageCollection no_gc;
    WriteToFlat(*cons, flat->GetChars(no_gc), 0, length);
    result = flat;
  }
  {
    DisallowGarbageCollection no_gc;
    Tagged<ConsString> raw_cons = *cons;
    raw_cons->set_first(*result);
    raw_cons->set_second(ReadOnlyRoots(isolate).empty_string());
  }
  DCHECK(result->IsFlat());
  return result;
}

Handle<String> String::SlowShare(Isolate* isolate, Handle<String> source) {
  DCHECK(v8_flags.shared_string_table);
  Handle<String> flat = Flatten(isolate, source, AllocationType::kSharedOld);

  // Do not recursively call Share, so directly compute the sharing strategy for
  // the flat string, which could already be a copy or an existing string from
  // e.g. a shortcut ConsString.
  MaybeDirectHandle<Map> new_map;
  switch (isolate->factory()->ComputeSharingStrategyForString(flat, &new_map)) {
    case StringTransitionStrategy::kCopy:
      break;
    case StringTransitionStrategy::kInPlace:
      // A relaxed write is sufficient here, because at this point the string
      // has not yet escaped the current thread.
      DCHECK(InAnySharedSpace(*flat));
      flat->set_map_no_write_barrier(*new_map.ToHandleChecked());
      return flat;
    case StringTransitionStrategy::kAlreadyTransitioned:
      return flat;
  }

  int length = flat->length();
  if (flat->IsOneByteRepresentation()) {
    Handle<SeqOneByteString> copy =
        isolate->factory()->NewRawSharedOneByteString(length).ToHandleChecked();
    DisallowGarbageCollection no_gc;
    WriteToFlat(*flat, copy->GetChars(no_gc), 0, length);
    return copy;
  }
  Handle<SeqTwoByteString> copy =
      isolate->factory()->NewRawSharedTwoByteString(length).ToHandleChecked();
  DisallowGarbageCollection no_gc;
  WriteToFlat(*flat, copy->GetChars(no_gc), 0, length);
  return copy;
}

namespace {

template <class StringClass>
void MigrateExternalStringResource(Isolate* isolate,
                                   Tagged<ExternalString> from,
                                   Tagged<StringClass> to) {
  Address to_resource_address = to->resource_as_address();
  if (to_resource_address == kNullAddress) {
    Tagged<StringClass> cast_from = Cast<StringClass>(from);
    // |to| is a just-created internalized copy of |from|. Migrate the resource.
    to->SetResource(isolate, cast_from->resource());
    // Zap |from|'s resource pointer to reflect the fact that |from| has
    // relinquished ownership of its resource.
    isolate->heap()->UpdateExternalString(
        from, Cast<ExternalString>(from)->ExternalPayloadSize(), 0);
    cast_from->SetResource(isolate, nullptr);
  } else if (to_resource_address != from->resource_as_address()) {
    // |to| already existed and has its own resource. Finalize |from|.
    isolate->heap()->FinalizeExternalString(from);
  }
}

void MigrateExternalString(Isolate* isolate, Tagged<String> string,
                           Tagged<String> internalized) {
  if (IsExternalOneByteString(internalized)) {
    MigrateExternalStringResource(isolate, Cast<ExternalString>(string),
                                  Cast<ExternalOneByteString>(internalized));
  } else if (IsExternalTwoByteString(internalized)) {
    MigrateExternalStringResource(isolate, Cast<ExternalString>(string),
                                  Cast<ExternalTwoByteString>(internalized));
  } else {
    // If the external string is duped into an existing non-external
    // internalized string, free its resource (it's about to be rewritten
    // into a ThinString below).
    isolate->heap()->FinalizeExternalString(string);
  }
}

}  // namespace

void ExternalString::InitExternalPointerFieldsDuringExternalization(
    Tagged<Map> new_map, Isolate* isolate) {
  resource_.Init(address(), isolate, kNullAddress);
  bool is_uncached = (new_map->instance_type() & kUncachedExternalStringMask) ==
                     kUncachedExternalStringTag;
  if (!is_uncached) {
    resource_data_.Init(address(), isolate, kNullAddress);
  }
}

template <typename IsolateT>
void String::MakeThin(IsolateT* isolate, Tagged<String> internalized) {
  DisallowGarbageCollection no_gc;
  DCHECK_NE(this, internalized);
  DCHECK(IsInternalizedString(internalized));

  Tagged<Map> initial_map = map(kAcquireLoad);
  StringShape initial_shape(initial_map);

  DCHECK(!initial_shape.IsThin());

#ifdef DEBUG
  // Check that shared strings can only transition to ThinStrings on the main
  // thread when no other thread is active.
  // The exception is during serialization, as no strings have escaped the
  // thread yet.
  if (initial_shape.IsShared() && !isolate->has_active_deserializer()) {
    isolate->AsIsolate()->global_safepoint()->AssertActive();
  }
#endif

  bool may_contain_recorded_slots = initial_shape.IsIndirect();
  int old_size = SizeFromMap(initial_map);
  ReadOnlyRoots roots(isolate);
  Tagged<Map> target_map = internalized->IsOneByteRepresentation()
                               ? roots.thin_one_byte_string_map()
                               : roots.thin_two_byte_string_map();
  if (initial_shape.IsExternal()) {
    // Notify GC about the layout change before the transition to avoid
    // concurrent marking from observing any in-between state (e.g.
    // ExternalString map where the resource external pointer is overwritten
    // with a tagged pointer).
    // ExternalString -> ThinString transitions can only happen on the
    // main-thread.
    isolate->AsIsolate()->heap()->NotifyObjectLayoutChange(
        Tagged(this), no_gc, InvalidateRecordedSlots::kYes,
        InvalidateExternalPointerSlots::kYes, sizeof(ThinString));
    MigrateExternalString(isolate->AsIsolate(), this, internalized);
  }

  // Update actual first and then do release store on the map word. This ensures
  // that the concurrent marker will read the pointer when visiting a
  // ThinString.
  Tagged<ThinString> thin = UncheckedCast<ThinString>(Tagged(this));
  thin->set_actual(internalized);

  DCHECK_GE(old_size, sizeof(ThinString));
  int size_delta = old_size - sizeof(ThinString);
  if (size_delta != 0) {
    if (!Heap::IsLargeObject(thin)) {
      isolate->heap()->NotifyObjectSizeChange(
          thin, old_size, sizeof(ThinString),
          may_contain_recorded_slots ? ClearRecordedSlots::kYes
                                     : ClearRecordedSlots::kNo);
    } else {
      // We don't need special handling for the combination IsLargeObject &&
      // may_contain_recorded_slots, because indirect strings never get that
      // large.
      DCHECK(!may_contain_recorded_slots);
    }
  }

  if (initial_shape.IsExternal()) {
    set_map(target_map, kReleaseStore);
  } else {
    set_map_safe_transition(target_map, kReleaseStore);
  }
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void String::MakeThin(
    Isolate* isolate, Tagged<String> internalized);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void String::MakeThin(
    LocalIsolate* isolate, Tagged<String> internalized);

template <typename T>
bool String::MarkForExternalizationDuringGC(Isolate* isolate, T* resource) {
  uint32_t raw_hash = raw_hash_field(kAcquireLoad);
  if (IsExternalForwardingIndex(raw_hash)) return false;
  if (IsInternalizedForwardingIndex(raw_hash)) {
    const int forwarding_index = ForwardingIndexValueBits::decode(raw_hash);
    if (!isolate->string_forwarding_table()->TryUpdateExternalResource(
            forwarding_index, resource)) {
      // The external resource was concurrently updated by another thread.
      return false;
    }
    raw_hash = Name::IsExternalForwardingIndexBit::update(raw_hash, true);
    set_raw_hash_field(raw_hash, kReleaseStore);
    return true;
  }
  // We need to store the hash in the forwarding table, as all non-external
  // shared strings are in-place internalizable. In case the string gets
  // internalized, we have to ensure that we can get the hash from the
  // forwarding table to satisfy the invariant that all internalized strings
  // have a computed hash value.
  if (!IsHashFieldComputed(raw_hash)) {
    raw_hash = EnsureRawHash();
  }
  DCHECK(IsHashFieldComputed(raw_hash));
  int forwarding_index =
      isolate->string_forwarding_table()->AddExternalResourceAndHash(
          this, resource, raw_hash);
  set_raw_hash_field(String::CreateExternalForwardingIndex(forwarding_index),
                     kReleaseStore);

  return true;
}

namespace {

template <bool is_one_byte>
Tagged<Map> ComputeExternalStringMap(Isolate* isolate, Tagged<String> string,
                                     int size) {
  ReadOnlyRoots roots(isolate);
  StringShape shape(string, isolate);
  const bool is_internalized = shape.IsInternalized();
  const bool is_shared = shape.IsShared();
  if constexpr (is_one_byte) {
    if (size < static_cast<int>(sizeof(ExternalString))) {
      if (is_internalized) {
        return roots.uncached_external_internalized_one_byte_string_map();
      } else {
        return is_shared ? roots.shared_uncached_external_one_byte_string_map()
                         : roots.uncached_external_one_byte_string_map();
      }
    } else {
      if (is_internalized) {
        return roots.external_internalized_one_byte_string_map();
      } else {
        return is_shared ? roots.shared_external_one_byte_string_map()
                         : roots.external_one_byte_string_map();
      }
    }
  } else {
    if (size < static_cast<int>(sizeof(ExternalString))) {
      if (is_internalized) {
        return roots.uncached_external_internalized_two_byte_string_map();
      } else {
        return is_shared ? roots.shared_uncached_external_two_byte_string_map()
                         : roots.uncached_external_two_byte_string_map();
      }
    } else {
      if (is_internalized) {
        return roots.external_internalized_two_byte_string_map();
      } else {
        return is_shared ? roots.shared_external_two_byte_string_map()
                         : roots.external_two_byte_string_map();
      }
    }
  }
}

}  // namespace

template <typename T>
void String::MakeExternalDuringGC(Isolate* isolate, T* resource) {
  isolate->heap()->safepoint()->AssertActive();
  DCHECK_NE(isolate->heap()->gc_state(), Heap::NOT_IN_GC);

  constexpr bool is_one_byte =
      std::is_base_of_v<v8::String::ExternalOneByteStringResource, T>;
  int size = this->Size();  // Byte size of the original string.
  DCHECK_GE(size, sizeof(UncachedExternalString));

  // Morph the string to an external string by replacing the map and
  // reinitializing the fields.  This won't work if the space the existing
  // string occupies is too small for a regular external string.  Instead, we
  // resort to an uncached external string instead, omitting the field caching
  // the address of the backing store.  When we encounter uncached external
  // strings in generated code, we need to bailout to runtime.
  Tagged<Map> new_map =
      ComputeExternalStringMap<is_one_byte>(isolate, this, size);

  // Byte size of the external String object.
  int new_size = this->SizeFromMap(new_map);

  // Shared strings are never indirect.
  DCHECK(!StringShape(this).IsIndirect());

  if (!isolate->heap()->IsLargeObject(this)) {
    isolate->heap()->NotifyObjectSizeChange(this, size, new_size,
                                            ClearRecordedSlots::kNo);
  }

  // The external pointer slots must be initialized before the new map is
  // installed. Otherwise, a GC marking thread may see the new map before the
  // slots are initialized and attempt to mark the (invalid) external pointers
  // table entries as alive.
  static_cast<ExternalString*>(this)
      ->InitExternalPointerFieldsDuringExternalization(new_map, isolate);

  // We are storing the new map using release store after creating a filler in
  // the NotifyObjectSizeChange call for the left-over space to avoid races with
  // the sweeper thread.
  this->set_map(new_map, kReleaseStore);

  if constexpr (is_one_byte) {
    Tagged<ExternalOneByteString> self = Cast<ExternalOneByteString>(this);
    self->SetResource(isolate, resource);
  } else {
    Tagged<ExternalTwoByteString> self = Cast<ExternalTwoByteString>(this);
    self->SetResource(isolate, resource);
  }
  isolate->heap()->RegisterExternalString(this);
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void String::
    MakeExternalDuringGC(Isolate* isolate,
                         v8::String::ExternalOneByteStringResource*);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void String::
    MakeExternalDuringGC(Isolate* isolate, v8::String::ExternalStringResource*);

bool String::MakeExternal(v8::String::ExternalStringResource* resource) {
  // Disallow garbage collection to avoid possible GC vs string access deadlock.
  DisallowGarbageCollection no_gc;

  // Externalizing twice leaks the external resource, so it's
  // prohibited by the API.
  DCHECK(
      this->SupportsExternalization(v8::String::Encoding::TWO_BYTE_ENCODING));
  DCHECK(resource->IsCacheable());
#ifdef ENABLE_SLOW_DCHECKS
  if (v8_flags.enable_slow_asserts) {
    // Assert that the resource and the string are equivalent.
    DCHECK(static_cast<size_t>(this->length()) == resource->length());
    base::ScopedVector<base::uc16> smart_chars(this->length());
    String::WriteToFlat(this, smart_chars.begin(), 0, this->length());
    DCHECK_EQ(0, memcmp(smart_chars.begin(), resource->data(),
                        resource->length() * sizeof(smart_chars[0])));
  }
#endif                      // DEBUG
  int size = this->Size();  // Byte size of the original string.
  // Abort if size does not allow in-place conversion.
  if (size < static_cast<int>(sizeof(UncachedExternalString))) return false;
  // Read-only strings cannot be made external, since that would mutate the
  // string.
  if (IsReadOnlyHeapObject(this)) return false;
  Isolate* isolate = GetIsolateFromWritableObject(this);
  if (IsShared()) {
    DCHECK(isolate->is_shared_space_isolate());
    return MarkForExternalizationDuringGC(isolate, resource);
  }
  DCHECK_IMPLIES(InWritableSharedSpace(this),
                 isolate->is_shared_space_isolate());
  bool is_internalized = IsInternalizedString(this);
  bool has_pointers = StringShape(this).IsIndirect();

  base::SharedMutexGuardIf<base::kExclusive> shared_mutex_guard(
      isolate->internalized_string_access(), is_internalized);
  // Morph the string to an external string by replacing the map and
  // reinitializing the fields.  This won't work if the space the existing
  // string occupies is too small for a regular external string.  Instead, we
  // resort to an uncached external string instead, omitting the field caching
  // the address of the backing store.  When we encounter uncached external
  // strings in generated code, we need to bailout to runtime.
  constexpr bool is_one_byte = false;
  Tagged<Map> new_map =
      ComputeExternalStringMap<is_one_byte>(isolate, this, size);

  // Byte size of the external String object.
  int new_size = this->SizeFromMap(new_map);

  if (has_pointers) {
    isolate->heap()->NotifyObjectLayoutChange(
        this, no_gc, InvalidateRecordedSlots::kYes,
        InvalidateExternalPointerSlots::kNo, new_size);
  }

  if (!isolate->heap()->IsLargeObject(this)) {
    isolate->heap()->NotifyObjectSizeChange(
        this, size, new_size,
        has_pointers ? ClearRecordedSlots::kYes : ClearRecordedSlots::kNo);
  } else {
    // We don't need special handling for the combination IsLargeObject &&
    // has_pointers, because indirect strings never get that large.
    DCHECK(!has_pointers);
  }

  // The external pointer slots must be initialized before the new map is
  // installed. Otherwise, a GC marking thread may see the new map before the
  // slots are initialized and attempt to mark the (invalid) external pointers
  // table entries as alive.
  static_cast<ExternalString*>(this)
      ->InitExternalPointerFieldsDuringExternalization(new_map, isolate);

  // We are storing the new map using release store after creating a filler in
  // the NotifyObjectSizeChange call for the left-over space to avoid races with
  // the sweeper thread.
  this->set_map(new_map, kReleaseStore);

  Tagged<ExternalTwoByteString> self = Cast<ExternalTwoByteString>(this);
  self->SetResource(isolate, resource);
  isolate->heap()->RegisterExternalString(this);
  // Force regeneration of the hash value.
  if (is_internalized) self->EnsureHash();
  return true;
}

bool String::MakeExternal(v8::String::ExternalOneByteStringResource* resource) {
  // Disallow garbage collection to avoid possible GC vs string access deadlock.
  DisallowGarbageCollection no_gc;

  // Externalizing twice leaks the external resource, so it's
  // prohibited by the API.
  DCHECK(
      this->SupportsExternalization(v8::String::Encoding::ONE_BYTE_ENCODING));
  DCHECK(resource->IsCacheable());
#ifdef ENABLE_SLOW_DCHECKS
  if (v8_flags.enable_slow_asserts) {
    // Assert that the resource and the string are equivalent.
    DCHECK(static_cast<size_t>(this->length()) == resource->length());
    if (this->IsTwoByteRepresentation()) {
      base::ScopedVector<uint16_t> smart_chars(this->length());
      String::WriteToFlat(this, smart_chars.begin(), 0, this->length());
      DCHECK(String::IsOneByte(smart_chars.begin(), this->length()));
    }
    base::ScopedVector<char> smart_chars(this->length());
    String::WriteToFlat(this, smart_chars.begin(), 0, this->length());
    DCHECK_EQ(0, memcmp(smart_chars.begin(), resource->data(),
                        resource->length() * sizeof(smart_chars[0])));
  }
#endif                      // DEBUG
  int size = this->Size();  // Byte size of the original string.
  // Abort if size does not allow in-place conversion.
  if (size < static_cast<int>(sizeof(UncachedExternalString))) return false;
  // Read-only strings cannot be made external, since that would mutate the
  // string.
  if (IsReadOnlyHeapObject(this)) return false;
  Isolate* isolate = GetIsolateFromWritableObject(this);
  if (IsShared()) {
    DCHECK(isolate->is_shared_space_isolate());
    return MarkForExternalizationDuringGC(isolate, resource);
  }
  DCHECK_IMPLIES(InWritableSharedSpace(this),
                 isolate->is_shared_space_isolate());
  bool is_internalized = IsInternalizedString(this);
  bool has_pointers = StringShape(this).IsIndirect();

  base::SharedMutexGuardIf<base::kExclusive> shared_mutex_guard(
      isolate->internalized_string_access(), is_internalized);
  // Morph the string to an external string by replacing the map and
  // reinitializing the fields.  This won't work if the space the existing
  // string occupies is too small for a regular external string.  Instead, we
  // resort to an uncached external string instead, omitting the field caching
  // the address of the backing store.  When we encounter uncached external
  // strings in generated code, we need to bailout to runtime.
  constexpr bool is_one_byte = true;
  Tagged<Map> new_map =
      ComputeExternalStringMap<is_one_byte>(isolate, this, size);

  if (!isolate->heap()->IsLargeObject(this)) {
    // Byte size of the external String object.
    int new_size = this->SizeFromMap(new_map);

    if (has_pointers) {
      DCHECK(!InWritableSharedSpace(this));
      isolate->heap()->NotifyObjectLayoutChange(
          this, no_gc, InvalidateRecordedSlots::kYes,
          InvalidateExternalPointerSlots::kNo, new_size);
    }
    isolate->heap()->NotifyObjectSizeChange(
        this, size, new_size,
        has_pointers ? ClearRecordedSlots::kYes : ClearRecordedSlots::kNo);
  } else {
    // We don't need special handling for the combination IsLargeObject &&
    // has_pointers, because indirect strings never get that large.
    DCHECK(!has_pointers);
  }

  // The external pointer slots must be initialized before the new map is
  // installed. Otherwise, a GC marking thread may see the new map before the
  // slots are initialized and attempt to mark the (invalid) external pointers
  // table entries as alive.
  static_cast<ExternalString*>(this)
      ->InitExternalPointerFieldsDuringExternalization(new_map, isolate);

  // We are storing the new map using release store after creating a filler in
  // the NotifyObjectSizeChange call for the left-over space to avoid races with
  // the sweeper thread.
  this->set_map(new_map, kReleaseStore);

  Tagged<ExternalOneByteString> self = Cast<ExternalOneByteString>(this);
  self->SetResource(isolate, resource);
  isolate->heap()->RegisterExternalString(this);
  // Force regeneration of the hash value.
  if (is_internalized) self->EnsureHash();
  return true;
}

bool String::SupportsExternalization(v8::String::Encoding encoding) {
  if (IsThinString(this)) {
    return i::Cast<i::ThinString>(this)->actual()->SupportsExternalization(
        encoding);
  }

  // RO_SPACE strings cannot be externalized.
  if (IsReadOnlyHeapObject(this)) {
    return false;
  }

#ifdef V8_COMPRESS_POINTERS
  // Small strings may not be in-place externalizable.
  if (this->Size() < static_cast<int>(sizeof(UncachedExternalString))) {
    return false;
  }
#else
  DCHECK_LE(sizeof(UncachedExternalString), this->Size());
#endif

  StringShape shape(this);

  // Already an external string.
  if (shape.IsExternal()) {
    return false;
  }

  // Only strings in old space can be externalized.
  if (Heap::InYoungGeneration(Tagged(this))) {
    return false;
  }

  // Encoding changes are not supported.
  static_assert(kStringEncodingMask == 1 << 3);
  static_assert(v8::String::Encoding::ONE_BYTE_ENCODING == 1 << 3);
  static_assert(v8::String::Encoding::TWO_BYTE_ENCODING == 0);
  return shape.encoding_tag() == static_cast<uint32_t>(encoding);
}

const char* String::PrefixForDebugPrint() const {
  StringShape shape(this);
  if (IsTwoByteRepresentation()) {
    if (shape.IsInternalized()) {
      return "u#";
    } else if (shape.IsCons()) {
      return "uc\"";
    } else if (shape.IsThin()) {
      return "u>\"";
    } else if (shape.IsExternal()) {
      return "ue\"";
    } else {
      return "u\"";
    }
  } else {
    if (shape.IsInternalized()) {
      return "#";
    } else if (shape.IsCons()) {
      return "c\"";
    } else if (shape.IsThin()) {
      return ">\"";
    } else if (shape.IsExternal()) {
      return "e\"";
    } else {
      return "\"";
    }
  }
  UNREACHABLE();
}

const char* String::SuffixForDebugPrint() const {
  StringShape shape(this);
  if (shape.IsInternalized()) return "";
  return "\"";
}

void String::StringShortPrint(StringStream* accumulator) {
  if (!LooksValid()) {
    accumulator->Add("<Invalid String>");
    return;
  }

  const int len = length();
  accumulator->Add("<String[%u]: ", len);
  accumulator->Add(PrefixForDebugPrint());

  if (len > kMaxShortPrintLength) {
    accumulator->Add("...<truncated>>");
    accumulator->Add(SuffixForDebugPrint());
    accumulator->Put('>');
    return;
  }

  PrintUC16(accumulator, 0, len);
  accumulator->Add(SuffixForDebugPrint());
  accumulator->Put('>');
}

void String::PrintUC16(std::ostream& os, int start, int end) {
  if (end < 0) end = length();
  StringCharacterStream stream(this, start);
  for (int i = start; i < end && stream.HasMore(); i++) {
    os << AsUC16(stream.GetNext());
  }
}

void String::PrintUC16(StringStream* accumulator, int start, int end) {
  if (end < 0) end = length();
  StringCharacterStream stream(this, start);
  for (int i = start; i < end && stream.HasMore(); i++) {
    uint16_t c = stream.GetNext();
    if (c == '\n') {
      accumulator->Add("\\n");
    } else if (c == '\r') {
      accumulator->Add("\\r");
    } else if (c == '\\') {
      accumulator->Add("\\\\");
    } else if (!std::isprint(c)) {
      accumulator->Add("\\x%02x", c);
    } else {
      accumulator->Put(static_cast<char>(c));
    }
  }
}

int32_t String::ToArrayIndex(Address addr) {
  DisallowGarbageCollection no_gc;
  Tagged<String> key(addr);

  uint32_t index;
  if (!key->AsArrayIndex(&index)) return -1;
  if (index <= INT_MAX) return index;
  return -1;
}

bool String::LooksValid() {
  // TODO(leszeks): Maybe remove this check entirely, Heap::Contains uses
  // basically the same logic as the way we access the heap in the first place.
  // RO_SPACE objects should always be valid.
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) return true;
  if (ReadOnlyHeap::Contains(this)) return true;
  MemoryChunkMetadata* chunk = MemoryChunkMetadata::FromHeapObject(this);
  if (chunk->heap() == nullptr) return false;
  return chunk->heap()->Contains(this);
}

// static
Handle<Number> String::ToNumber(Isolate* isolate, Handle<String> subject) {
  return isolate->factory()->NewNumber(
      StringToDouble(isolate, subject, ALLOW_NON_DECIMAL_PREFIX));
}

String::FlatContent String::SlowGetFlatContent(
    const DisallowGarbageCollection& no_gc,
    const SharedStringAccessGuardIfNeeded& access_guard) {
  USE(no_gc);
  Tagged<String> string = this;
  StringShape shape(string);
  int offset = 0;

  // Extract cons- and sliced strings.
  if (shape.IsCons()) {
    Tagged<ConsString> cons = Cast<ConsString>(string);
    if (!cons->IsFlat()) return FlatContent(no_gc);
    string = cons->first();
    shape = StringShape(string);
  } else if (shape.IsSliced()) {
    Tagged<SlicedString> slice = Cast<SlicedString>(string);
    offset = slice->offset();
    string = slice->parent();
    shape = StringShape(string);
  }

  DCHECK(!shape.IsCons());
  DCHECK(!shape.IsSliced());

  // Extract thin strings.
  if (shape.IsThin()) {
    Tagged<ThinString> thin = Cast<ThinString>(string);
    string = thin->actual();
    shape = StringShape(string);
  }

  DCHECK(shape.IsDirect());
  return TryGetFlatContentFromDirectString(no_gc, string, offset, length(),
                                           access_guard)
      .value();
}

std::unique_ptr<char[]> String::ToCString(AllowNullsFlag allow_nulls,
                                          RobustnessFlag robust_flag,
                                          int offset, int length,
                                          int* length_return) {
  if (robust_flag == ROBUST_STRING_TRAVERSAL && !LooksValid()) {
    return std::unique_ptr<char[]>();
  }
  // Negative length means the to the end of the string.
  if (length < 0) length = kMaxInt - offset;

  // Compute the size of the UTF-8 string. Start at the specified offset.
  StringCharacterStream stream(this, offset);
  int character_position = offset;
  int utf8_bytes = 0;
  int last = unibrow::Utf16::kNoPreviousCharacter;
  while (stream.HasMore() && character_position++ < offset + length) {
    uint16_t character = stream.GetNext();
    utf8_bytes += unibrow::Utf8::Length(character, last);
    last = character;
  }

  if (length_return) {
    *length_return = utf8_bytes;
  }

  char* result = NewArray<char>(utf8_bytes + 1);

  // Convert the UTF-16 string to a UTF-8 buffer. Start at the specified offset.
  stream.Reset(this, offset);
  character_position = offset;
  int utf8_byte_position = 0;
  last = unibrow::Utf16::kNoPreviousCharacter;
  while (stream.HasMore() && character_position++ < offset + length) {
    uint16_t character = stream.GetNext();
    if (allow_nulls == DISALLOW_NULLS && character == 0) {
      character = ' ';
    }
    utf8_byte_position +=
        unibrow::Utf8::Encode(result + utf8_byte_position, character, last);
    last = character;
  }
  result[utf8_byte_position] = 0;
  return std::unique_ptr<char[]>(result);
}

std::unique_ptr<char[]> String::ToCString(AllowNullsFlag allow_nulls,
                                          RobustnessFlag robust_flag,
                                          int* length_return) {
  return ToCString(allow_nulls, robust_flag, 0, -1, length_return);
}

// static
template <typename sinkchar>
void String::WriteToFlat(Tagged<String> source, sinkchar* sink, int start,
                         int length) {
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(source));
  return WriteToFlat(source, sink, start, length,
                     SharedStringAccessGuardIfNeeded::NotNeeded());
}

// static
template <typename sinkchar>
void String::WriteToFlat(Tagged<String> source, sinkchar* sink, int start,
                         int length,
                         const SharedStringAccessGuardIfNeeded& access_guard) {
  DisallowGarbageCollection no_gc;
  if (length == 0) return;
  while (true) {
    DCHECK_LT(0, length);
    DCHECK_LE(0, start);
    DCHECK_LE(length, source->length());
    switch (StringShape(source).representation_and_encoding_tag()) {
      case kOneByteStringTag | kExternalStringTag:
        CopyChars(sink, Cast<ExternalOneByteString>(source)->GetChars() + start,
                  length);
        return;
      case kTwoByteStringTag | kExternalStringTag:
        CopyChars(sink, Cast<ExternalTwoByteString>(source)->GetChars() + start,
                  length);
        return;
      case kOneByteStringTag | kSeqStringTag:
        CopyChars(
            sink,
            Cast<SeqOneByteString>(source)->GetChars(no_gc, access_guard) +
                start,
            length);
        return;
      case kTwoByteStringTag | kSeqStringTag:
        CopyChars(
            sink,
            Cast<SeqTwoByteString>(source)->GetChars(no_gc, access_guard) +
                start,
            length);
        return;
      case kOneByteStringTag | kConsStringTag:
      case kTwoByteStringTag | kConsStringTag: {
        Tagged<ConsString> cons_string = Cast<ConsString>(source);
        Tagged<String> first = cons_string->first();
        int boundary = first->length();
        int first_length = boundary - start;
        int second_length = start + length - boundary;
        if (second_length >= first_length) {
          // Right hand side is longer.  Recurse over left.
          if (first_length > 0) {
            WriteToFlat(first, sink, start, first_length, access_guard);
            if (start == 0 && cons_string->second() == first) {
              CopyChars(sink + boundary, sink, boundary);
              return;
            }
            sink += boundary - start;
            start = 0;
            length -= first_length;
          } else {
            start -= boundary;
          }
          source = cons_string->second();
        } else {
          // Left hand side is longer.  Recurse over right.
          if (second_length > 0) {
            Tagged<String> second = cons_string->second();
            // When repeatedly appending to a string, we get a cons string that
            // is unbalanced to the left, a list, essentially.  We inline the
            // common case of sequential one-byte right child.
            if (second_length == 1) {
              sink[boundary - start] =
                  static_cast<sinkchar>(second->Get(0, access_guard));
            } else if (IsSeqOneByteString(second)) {
              CopyChars(
                  sink + boundary - start,
                  Cast<SeqOneByteString>(second)->GetChars(no_gc, access_guard),
                  second_length);
            } else {
              WriteToFlat(second, sink + boundary - start, 0, second_length,
                          access_guard);
            }
            length -= second_length;
          }
          source = first;
        }
        if (length == 0) return;
        continue;
      }
      case kOneByteStringTag | kSlicedStringTag:
      case kTwoByteStringTag | kSlicedStringTag: {
        Tagged<SlicedString> slice = Cast<SlicedString>(source);
        unsigned offset = slice->offset();
        source = slice->parent();
        start += offset;
        continue;
      }
      case kOneByteStringTag | kThinStringTag:
      case kTwoByteStringTag | kThinStringTag:
        source = Cast<ThinString>(source)->actual();
        continue;
    }
    UNREACHABLE();
  }
  UNREACHABLE();
}

template <typename SourceChar>
static void CalculateLineEndsImpl(String::LineEndsVector* line_ends,
                                  base::Vector<const SourceChar> src,
                                  bool include_ending_line) {
  const int src_len = src.length();
  for (int i = 0; i < src_len - 1; i++) {
    SourceChar current = src[i];
    SourceChar next = src[i + 1];
    if (IsLineTerminatorSequence(current, next)) line_ends->push_back(i);
  }

  if (src_len > 0 && IsLineTerminatorSequence(src[src_len - 1], 0)) {
    line_ends->push_back(src_len - 1);
  }
  if (include_ending_line) {
    // Include one character beyond the end of script. The rewriter uses that
    // position for the implicit return statement.
    line_ends->push_back(src_len);
  }
}

template <typename IsolateT>
String::LineEndsVector String::CalculateLineEndsVector(
    IsolateT* isolate, Handle<String> src, bool include_ending_line) {
  src = Flatten(isolate, src);
  // Rough estimate of line count based on a roughly estimated average
  // length of packed code. Most scripts have < 32 lines.
  int line_count_estimate = (src->length() >> 6) + 16;
  LineEndsVector line_ends;
  line_ends.reserve(line_count_estimate);
  {
    DisallowGarbageCollection no_gc;
    // Dispatch on type of strings.
    String::FlatContent content = src->GetFlatContent(no_gc);
    DCHECK(content.IsFlat());
    if (content.IsOneByte()) {
      CalculateLineEndsImpl(&line_ends, content.ToOneByteVector(),
                            include_ending_line);
    } else {
      CalculateLineEndsImpl(&line_ends, content.ToUC16Vector(),
                            include_ending_line);
    }
  }
  return line_ends;
}

template String::LineEndsVector String::CalculateLineEndsVector(
    Isolate* isolate, Handle<String> src, bool include_ending_line);
template String::LineEndsVector String::CalculateLineEndsVector(
    LocalIsolate* isolate, Handle<String> src, bool include_ending_line);

template <typename IsolateT>
Handle<FixedArray> String::CalculateLineEnds(IsolateT* isolate,
                                             Handle<String> src,
                                             bool include_ending_line) {
  LineEndsVector line_ends =
      CalculateLineEndsVector(isolate, src, include_ending_line);
  int line_count = static_cast<int>(line_ends.size());
  Handle<FixedArray> array =
      isolate->factory()->NewFixedArray(line_count, AllocationType::kOld);
  {
    DisallowGarbageCollection no_gc;
    Tagged<FixedArray> raw_array = *array;
    for (int i = 0; i < line_count; i++) {
      raw_array->set(i, Smi::FromInt(line_ends[i]));
    }
  }
  return array;
}

template Handle<FixedArray> String::CalculateLineEnds(Isolate* isolate,
                                                      Handle<String> src,
                                                      bool include_ending_line);
template Handle<FixedArray> String::CalculateLineEnds(LocalIsolate* isolate,
                                                      Handle<String> src,
                                                      bool include_ending_line);

bool String::SlowEquals(Tagged<String> other) const {
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(this));
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(other));
  return SlowEquals(other, SharedStringAccessGuardIfNeeded::NotNeeded());
}

bool String::SlowEquals(
    Tagged<String> other,
    const SharedStringAccessGuardIfNeeded& access_guard) const {
  DisallowGarbageCollection no_gc;
  // Fast check: negative check with lengths.
  int len = length();
  if (len != other->length()) return false;
  if (len == 0) return true;

  // Fast check: if at least one ThinString is involved, dereference it/them
  // and restart.
  if (IsThinString(this) || IsThinString(other)) {
    if (IsThinString(other)) other = Cast<ThinString>(other)->actual();
    if (IsThinString(this)) {
      return Cast<ThinString>(this)->actual()->Equals(other);
    } else {
      return this->Equals(other);
    }
  }

  // Fast check: if hash code is computed for both strings
  // a fast negative check can be performed.
  uint32_t this_hash;
  uint32_t other_hash;
  if (TryGetHash(&this_hash) && other->TryGetHash(&other_hash)) {
#ifdef ENABLE_SLOW_DCHECKS
    if (v8_flags.enable_slow_asserts) {
      if (this_hash != other_hash) {
        bool found_difference = false;
        for (int i = 0; i < len; i++) {
          if (Get(i) != other->Get(i)) {
            found_difference = true;
            break;
          }
        }
        DCHECK(found_difference);
      }
    }
#endif
    if (this_hash != other_hash) return false;
  }

  // We know the strings are both non-empty. Compare the first chars
  // before we try to flatten the strings.
  if (this->Get(0, access_guard) != other->Get(0, access_guard)) return false;

  if (IsSeqOneByteString(this) && IsSeqOneByteString(other)) {
    const uint8_t* str1 =
        Cast<SeqOneByteString>(this)->GetChars(no_gc, access_guard);
    const uint8_t* str2 =
        Cast<SeqOneByteString>(other)->GetChars(no_gc, access_guard);
    return CompareCharsEqual(str1, str2, len);
  }

  StringComparator comparator;
  return comparator.Equals(this, other, access_guard);
}

// static
bool String::SlowEquals(Isolate* isolate, Handle<String> one,
                        Handle<String> two) {
  // Fast check: negative check with lengths.
  const int one_length = one->length();
  if (one_length != two->length()) return false;
  if (one_length == 0) return true;

  // Fast check: if at least one ThinString is involved, dereference it/them
  // and restart.
  if (IsThinString(*one) || IsThinString(*two)) {
    if (IsThinString(*one)) {
      one = handle(Cast<ThinString>(*one)->actual(), isolate);
    }
    if (IsThinString(*two)) {
      two = handle(Cast<ThinString>(*two)->actual(), isolate);
    }
    return String::Equals(isolate, one, two);
  }

  // Fast check: if hash code is computed for both strings
  // a fast negative check can be performed.
  uint32_t one_hash;
  uint32_t two_hash;
  if (one->TryGetHash(&one_hash) && two->TryGetHash(&two_hash)) {
#ifdef ENABLE_SLOW_DCHECKS
    if (v8_flags.enable_slow_asserts) {
      if (one_hash != two_hash) {
        bool found_difference = false;
        for (int i = 0; i < one_length; i++) {
          if (one->Get(i) != two->Get(i)) {
            found_difference = true;
            break;
          }
        }
        DCHECK(found_difference);
      }
    }
#endif
    if (one_hash != two_hash) return false;
  }

  // We know the strings are both non-empty. Compare the first chars
  // before we try to flatten the strings.
  if (one->Get(0) != two->Get(0)) return false;

  one = String::Flatten(isolate, one);
  two = String::Flatten(isolate, two);

  DisallowGarbageCollection no_gc;
  String::FlatContent flat1 = one->GetFlatContent(no_gc);
  String::FlatContent flat2 = two->GetFlatContent(no_gc);

  if (flat1.IsOneByte() && flat2.IsOneByte()) {
    return CompareCharsEqual(flat1.ToOneByteVector().begin(),
                             flat2.ToOneByteVector().begin(), one_length);
  } else if (flat1.IsTwoByte() && flat2.IsTwoByte()) {
    return CompareCharsEqual(flat1.ToUC16Vector().begin(),
                             flat2.ToUC16Vector().begin(), one_length);
  } else if (flat1.IsOneByte() && flat2.IsTwoByte()) {
    return CompareCharsEqual(flat1.ToOneByteVector().begin(),
                             flat2.ToUC16Vector().begin(), one_length);
  } else if (flat1.IsTwoByte() && flat2.IsOneByte()) {
    return CompareCharsEqual(flat1.ToUC16Vector().begin(),
                             flat2.ToOneByteVector().begin(), one_length);
  }
  UNREACHABLE();
}

// static
ComparisonResult String::Compare(Isolate* isolate, Handle<String> x,
                                 Handle<String> y) {
  // A few fast case tests before we flatten.
  if (x.is_identical_to(y)) {
    return ComparisonResult::kEqual;
  } else if (y->length() == 0) {
    return x->length() == 0 ? ComparisonResult::kEqual
                            : ComparisonResult::kGreaterThan;
  } else if (x->length() == 0) {
    return ComparisonResult::kLessThan;
  }

  int const d = x->Get(0) - y->Get(0);
  if (d < 0) {
    return ComparisonResult::kLessThan;
  } else if (d > 0) {
    return ComparisonResult::kGreaterThan;
  }

  // Slow case.
  x = String::Flatten(isolate, x);
  y = String::Flatten(isolate, y);

  DisallowGarbageCollection no_gc;
  ComparisonResult result = ComparisonResult::kEqual;
  int prefix_length = x->length();
  if (y->length() < prefix_length) {
    prefix_length = y->length();
    result = ComparisonResult::kGreaterThan;
  } else if (y->length() > prefix_length) {
    result = ComparisonResult::kLessThan;
  }
  int r;
  String::FlatContent x_content = x->GetFlatContent(no_gc);
  String::FlatContent y_content = y->GetFlatContent(no_gc);
  if (x_content.IsOneByte()) {
    base::Vector<const uint8_t> x_chars = x_content.ToOneByteVector();
    if (y_content.IsOneByte()) {
      base::Vector<const uint8_t> y_chars = y_content.ToOneByteVector();
      r = CompareChars(x_chars.begin(), y_chars.begin(), prefix_length);
    } else {
      base::Vector<const base::uc16> y_chars = y_content.ToUC16Vector();
      r = CompareChars(x_chars.begin(), y_chars.begin(), prefix_length);
    }
  } else {
    base::Vector<const base::uc16> x_chars = x_content.ToUC16Vector();
    if (y_content.IsOneByte()) {
      base::Vector<const uint8_t> y_chars = y_content.ToOneByteVector();
      r = CompareChars(x_chars.begin(), y_chars.begin(), prefix_length);
    } else {
      base::Vector<const base::uc16> y_chars = y_content.ToUC16Vector();
      r = CompareChars(x_chars.begin(), y_chars.begin(), prefix_length);
    }
  }
  if (r < 0) {
    result = ComparisonResult::kLessThan;
  } else if (r > 0) {
    result = ComparisonResult::kGreaterThan;
  }
  return result;
}

namespace {

uint32_t ToValidIndex(Tagged<String> str, Tagged<Object> number) {
  uint32_t index = PositiveNumberToUint32(number);
  uint32_t length_value = static_cast<uint32_t>(str->length());
  if (index > length_value) return length_value;
  return index;
}

}  // namespace

Tagged<Object> String::IndexOf(Isolate* isolate, Handle<Object> receiver,
                               Handle<Object> search, Handle<Object> position) {
  if (IsNullOrUndefined(*receiver, isolate)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNullOrUndefined,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "String.prototype.indexOf")));
  }
  Handle<String> receiver_string;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver_string,
                                     Object::ToString(isolate, receiver));

  Handle<String> search_string;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, search_string,
                                     Object::ToString(isolate, search));

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, position,
                                     Object::ToInteger(isolate, position));

  uint32_t index = ToValidIndex(*receiver_string, *position);
  return Smi::FromInt(
      String::IndexOf(isolate, receiver_string, search_string, index));
}

namespace {

template <typename T>
int SearchString(Isolate* isolate, String::FlatContent receiver_content,
                 base::Vector<T> pat_vector, int start_index) {
  if (receiver_content.IsOneByte()) {
    return SearchString(isolate, receiver_content.ToOneByteVector(), pat_vector,
                        start_index);
  }
  return SearchString(isolate, receiver_content.ToUC16Vector(), pat_vector,
                      start_index);
}

}  // namespace

int String::IndexOf(Isolate* isolate, Handle<String> receiver,
                    Handle<String> search, int start_index) {
  DCHECK_LE(0, start_index);
  DCHECK(start_index <= receiver->length());

  uint32_t search_length = search->length();
  if (search_length == 0) return start_index;

  uint32_t receiver_length = receiver->length();
  if (start_index + search_length > receiver_length) return -1;

  receiver = String::Flatten(isolate, receiver);
  search = String::Flatten(isolate, search);

  DisallowGarbageCollection no_gc;  // ensure vectors stay valid
  // Extract flattened substrings of cons strings before getting encoding.
  String::FlatContent receiver_content = receiver->GetFlatContent(no_gc);
  String::FlatContent search_content = search->GetFlatContent(no_gc);

  // dispatch on type of strings
  if (search_content.IsOneByte()) {
    base::Vector<const uint8_t> pat_vector = search_content.ToOneByteVector();
    return SearchString<const uint8_t>(isolate, receiver_content, pat_vector,
                                       start_index);
  }
  base::Vector<const base::uc16> pat_vector = search_content.ToUC16Vector();
  return SearchString<const base::uc16>(isolate, receiver_content, pat_vector,
                                        start_index);
}

MaybeHandle<String> String::GetSubstitution(Isolate* isolate, Match* match,
                                            Handle<String> replacement,
                                            int start_index) {
  DCHECK_GE(start_index, 0);

  Factory* factory = isolate->factory();

  const int replacement_length = replacement->length();
  const int captures_length = match->CaptureCount();

  replacement = String::Flatten(isolate, replacement);

  Handle<String> dollar_string =
      factory->LookupSingleCharacterStringFromCode('$');
  int next_dollar_ix =
      String::IndexOf(isolate, replacement, dollar_string, start_index);
  if (next_dollar_ix < 0) {
    return replacement;
  }

  IncrementalStringBuilder builder(isolate);

  if (next_dollar_ix > 0) {
    builder.AppendString(factory->NewSubString(replacement, 0, next_dollar_ix));
  }

  while (true) {
    const int peek_ix = next_dollar_ix + 1;
    if (peek_ix >= replacement_length) {
      builder.AppendCharacter('$');
      return indirect_handle(builder.Finish(), isolate);
    }

    int continue_from_ix = -1;
    const uint16_t peek = replacement->Get(peek_ix);
    switch (peek) {
      case '$':  // $$
        builder.AppendCharacter('$');
        continue_from_ix = peek_ix + 1;
        break;
      case '&':  // $& - match
        builder.AppendString(match->GetMatch());
        continue_from_ix = peek_ix + 1;
        break;
      case '`':  // $` - prefix
        builder.AppendString(match->GetPrefix());
        continue_from_ix = peek_ix + 1;
        break;
      case '\'':  // $' - suffix
        builder.AppendString(match->GetSuffix());
        continue_from_ix = peek_ix + 1;
        break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9': {
        // Valid indices are $1 .. $9, $01 .. $09 and $10 .. $99
        int scaled_index = (peek - '0');
        int advance = 1;

        if (peek_ix + 1 < replacement_length) {
          const uint16_t next_peek = replacement->Get(peek_ix + 1);
          if (next_peek >= '0' && next_peek <= '9') {
            const int new_scaled_index = scaled_index * 10 + (next_peek - '0');
            if (new_scaled_index < captures_length) {
              scaled_index = new_scaled_index;
              advance = 2;
            }
          }
        }

        if (scaled_index == 0 || scaled_index >= captures_length) {
          builder.AppendCharacter('$');
          continue_from_ix = peek_ix;
          break;
        }

        bool capture_exists;
        Handle<String> capture;
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, capture, match->GetCapture(scaled_index, &capture_exists));
        if (capture_exists) builder.AppendString(capture);
        continue_from_ix = peek_ix + advance;
        break;
      }
      case '<': {  // $<name> - named capture
        using CaptureState = String::Match::CaptureState;

        if (!match->HasNamedCaptures()) {
          builder.AppendCharacter('$');
          continue_from_ix = peek_ix;
          break;
        }

        Handle<String> bracket_string =
            factory->LookupSingleCharacterStringFromCode('>');
        const int closing_bracket_ix =
            String::IndexOf(isolate, replacement, bracket_string, peek_ix + 1);

        if (closing_bracket_ix == -1) {
          // No closing bracket was found, treat '$<' as a string literal.
          builder.AppendCharacter('$');
          continue_from_ix = peek_ix;
          break;
        }

        Handle<String> capture_name =
            factory->NewSubString(replacement, peek_ix + 1, closing_bracket_ix);
        Handle<String> capture;
        CaptureState capture_state;
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, capture,
            match->GetNamedCapture(capture_name, &capture_state));

        if (capture_state == CaptureState::MATCHED) {
          builder.AppendString(capture);
        }

        continue_from_ix = closing_bracket_ix + 1;
        break;
      }
      default:
        builder.AppendCharacter('$');
        continue_from_ix = peek_ix;
        break;
    }

    // Go the the next $ in the replacement.
    // TODO(jgruber): Single-char lookups could be much more efficient.
    DCHECK_NE(continue_from_ix, -1);
    next_dollar_ix =
        String::IndexOf(isolate, replacement, dollar_string, continue_from_ix);

    // Return if there are no more $ characters in the replacement. If we
    // haven't reached the end, we need to append the suffix.
    if (next_dollar_ix < 0) {
      if (continue_from_ix < replacement_length) {
        builder.AppendString(factory->NewSubString(
            replacement, continue_from_ix, replacement_length));
      }
      return indirect_handle(builder.Finish(), isolate);
    }

    // Append substring between the previous and the next $ character.
    if (next_dollar_ix > continue_from_ix) {
      builder.AppendString(
          factory->NewSubString(replacement, continue_from_ix, next_dollar_ix));
    }
  }

  UNREACHABLE();
}

namespace {  // for String.Prototype.lastIndexOf

template <typename schar, typename pchar>
int StringMatchBackwards(base::Vector<const schar> subject,
                         base::Vector<const pchar> pattern, int idx) {
  int pattern_length = pattern.length();
  DCHECK_GE(pattern_length, 1);
  DCHECK(idx + pattern_length <= subject.length());

  if (sizeof(schar) == 1 && sizeof(pchar) > 1) {
    for (int i = 0; i < pattern_length; i++) {
      base::uc16 c = pattern[i];
      if (c > String::kMaxOneByteCharCode) {
        return -1;
      }
    }
  }

  pchar pattern_first_char = pattern[0];
  for (int i = idx; i >= 0; i--) {
    if (subject[i] != pattern_first_char) continue;
    int j = 1;
    while (j < pattern_length) {
      if (pattern[j] != subject[i + j]) {
        break;
      }
      j++;
    }
    if (j == pattern_length) {
      return i;
    }
  }
  return -1;
}

}  // namespace

Tagged<Object> String::LastIndexOf(Isolate* isolate, Handle<Object> receiver,
                                   Handle<Object> search,
                                   Handle<Object> position) {
  if (IsNullOrUndefined(*receiver, isolate)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNullOrUndefined,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "String.prototype.lastIndexOf")));
  }
  Handle<String> receiver_string;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver_string,
                                     Object::ToString(isolate, receiver));

  Handle<String> search_string;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, search_string,
                                     Object::ToString(isolate, search));

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, position,
                                     Object::ToNumber(isolate, position));

  uint32_t start_index;

  if (IsNaN(*position)) {
    start_index = receiver_string->length();
  } else {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, position,
                                       Object::ToInteger(isolate, position));
    start_index = ToValidIndex(*receiver_string, *position);
  }

  uint32_t pattern_length = search_string->length();
  uint32_t receiver_length = receiver_string->length();

  if (start_index + pattern_length > receiver_length) {
    start_index = receiver_length - pattern_length;
  }

  if (pattern_length == 0) {
    return Smi::FromInt(start_index);
  }

  receiver_string = String::Flatten(isolate, receiver_string);
  search_string = String::Flatten(isolate, search_string);

  int last_index = -1;
  DisallowGarbageCollection no_gc;  // ensure vectors stay valid

  String::FlatContent receiver_content = receiver_string->GetFlatContent(no_gc);
  String::FlatContent search_content = search_string->GetFlatContent(no_gc);

  if (search_content.IsOneByte()) {
    base::Vector<const uint8_t> pat_vector = search_content.ToOneByteVector();
    if (receiver_content.IsOneByte()) {
      last_index = StringMatchBackwards(receiver_content.ToOneByteVector(),
                                        pat_vector, start_index);
    } else {
      last_index = StringMatchBackwards(receiver_content.ToUC16Vector(),
                                        pat_vector, start_index);
    }
  } else {
    base::Vector<const base::uc16> pat_vector = search_content.ToUC16Vector();
    if (receiver_content.IsOneByte()) {
      last_index = StringMatchBackwards(receiver_content.ToOneByteVector(),
                                        pat_vector, start_index);
    } else {
      last_index = StringMatchBackwards(receiver_content.ToUC16Vector(),
                                        pat_vector, start_index);
    }
  }
  return Smi::FromInt(last_index);
}

bool String::HasOneBytePrefix(base::Vector<const char> str) {
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(this));
  return IsEqualToImpl<EqualityType::kPrefix>(
      str, SharedStringAccessGuardIfNeeded::NotNeeded());
}

namespace {

template <typename Char>
bool IsIdentifierVector(base::Vector<Char> vec) {
  if (vec.empty()) {
    return false;
  }
  if (!IsIdentifierStart(vec[0])) {
    return false;
  }
  for (size_t i = 1; i < vec.size(); ++i) {
    if (!IsIdentifierPart(vec[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace

// static
bool String::IsIdentifier(Isolate* isolate, Handle<String> str) {
  str = String::Flatten(isolate, str);
  DisallowGarbageCollection no_gc;
  String::FlatContent flat = str->GetFlatContent(no_gc);
  return flat.IsOneByte() ? IsIdentifierVector(flat.ToOneByteVector())
                          : IsIdentifierVector(flat.ToUC16Vector());
}

namespace {

template <typename Char>
uint32_t HashString(Tagged<String> string, size_t start, int length,
                    uint64_t seed,
                    const SharedStringAccessGuardIfNeeded& access_guard) {
  DisallowGarbageCollection no_gc;

  if (length > String::kMaxHashCalcLength) {
    return StringHasher::GetTrivialHash(length);
  }

  std::unique_ptr<Char[]> buffer;
  const Char* chars;

  if (IsConsString(string)) {
    DCHECK_EQ(0, start);
    DCHECK(!string->IsFlat());
    buffer.reset(new Char[length]);
    String::WriteToFlat(string, buffer.get(), 0, length, access_guard);
    chars = buffer.get();
  } else {
    chars = string->GetDirectStringChars<Char>(no_gc, access_guard) + start;
  }

  return StringHasher::HashSequentialString<Char>(chars, length, seed);
}

}  // namespace

uint32_t String::ComputeAndSetRawHash() {
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(this));
  return ComputeAndSetRawHash(SharedStringAccessGuardIfNeeded::NotNeeded());
}

uint32_t String::ComputeAndSetRawHash(
    const SharedStringAccessGuardIfNeeded& access_guard) {
  DisallowGarbageCollection no_gc;
  // Should only be called if hash code has not yet been computed.
  //
  // If in-place internalizable strings are shared, there may be calls to
  // ComputeAndSetRawHash in parallel. Since only flat strings are in-place
  // internalizable and their contents do not change, the result hash is the
  // same. The raw hash field is stored with relaxed ordering.
  DCHECK_IMPLIES(!v8_flags.shared_string_table, !HasHashCode());

  // Store the hash code in the object.
  uint64_t seed = HashSeed(EarlyGetReadOnlyRoots());
  size_t start = 0;
  Tagged<String> string = this;
  StringShape shape(string);
  if (shape.IsSliced()) {
    Tagged<SlicedString> sliced = Cast<SlicedString>(string);
    start = sliced->offset();
    string = sliced->parent();
    shape = StringShape(string);
  }
  if (shape.IsCons() && string->IsFlat()) {
    string = Cast<ConsString>(string)->first();
    shape = StringShape(string);
  }
  if (shape.IsThin()) {
    string = Cast<ThinString>(string)->actual();
    shape = StringShape(string);
    if (length() == string->length()) {
      uint32_t raw_hash = string->RawHash();
      DCHECK(IsHashFieldComputed(raw_hash));
      set_raw_hash_field(raw_hash);
      return raw_hash;
    }
  }
  uint32_t raw_hash_field =
      shape.encoding_tag() == kOneByteStringTag
          ? HashString<uint8_t>(string, start, length(), seed, access_guard)
          : HashString<uint16_t>(string, start, length(), seed, access_guard);
  set_raw_hash_field_if_empty(raw_hash_field);
  // Check the hash code is there (or a forwarding index if the string was
  // internalized/externalized in parallel).
  DCHECK(HasHashCode() || HasForwardingIndex(kAcquireLoad));
  // Ensure that the hash value of 0 is never computed.
  DCHECK_NE(HashBits::decode(raw_hash_field), 0);
  return raw_hash_field;
}

bool String::SlowAsArrayIndex(uint32_t* index) {
  DisallowGarbageCollection no_gc;
  int length = this->length();
  if (length <= kMaxCachedArrayIndexLength) {
    uint32_t field = EnsureRawHash();  // Force computation of hash code.
    if (!IsIntegerIndex(field)) return false;
    *index = ArrayIndexValueBits::decode(field);
    return true;
  }
  if (length == 0 || length > kMaxArrayIndexSize) return false;
  StringCharacterStream stream(this);
  return StringToIndex(&stream, index);
}

bool String::SlowAsIntegerIndex(size_t* index) {
  DisallowGarbageCollection no_gc;
  int length = this->length();
  if (length <= kMaxCachedArrayIndexLength) {
    uint32_t field = EnsureRawHash();  // Force computation of hash code.
    if (!IsIntegerIndex(field)) return false;
    *index = ArrayIndexValueBits::decode(field);
    return true;
  }
  if (length == 0 || length > kMaxIntegerIndexSize) return false;
  StringCharacterStream stream(this);
  return StringToIndex<StringCharacterStream, size_t, kToIntegerIndex>(&stream,
                                                                       index);
}

void String::PrintOn(FILE* file) {
  int length = this->length();
  for (int i = 0; i < length; i++) {
    PrintF(file, "%c", Get(i));
  }
}

void String::PrintOn(std::ostream& ostream) {
  int length = this->length();
  for (int i = 0; i < length; i++) {
    ostream.put(Get(i));
  }
}

Handle<String> SeqString::Truncate(Isolate* isolate, Handle<SeqString> string,
                                   int new_length) {
  if (new_length == 0) return string->GetReadOnlyRoots().empty_string_handle();

  int new_size, old_size;
  int old_length = string->length();
  if (old_length <= new_length) return string;

  if (IsSeqOneByteString(*string)) {
    old_size = SeqOneByteString::SizeFor(old_length);
    new_size = SeqOneByteString::SizeFor(new_length);
  } else {
    DCHECK(IsSeqTwoByteString(*string));
    old_size = SeqTwoByteString::SizeFor(old_length);
    new_size = SeqTwoByteString::SizeFor(new_length);
  }

#if DEBUG
  Address start_of_string = (*string).address();
  DCHECK(IsAligned(start_of_string, kObjectAlignment));
  DCHECK(IsAligned(start_of_string + new_size, kObjectAlignment));
#endif

  Heap* heap = isolate->heap();
  if (!heap->IsLargeObject(*string)) {
    // Sizes are pointer size aligned, so that we can use filler objects
    // that are a multiple of pointer size.
    // No slot invalidation needed since this method is only used on freshly
    // allocated strings.
    heap->NotifyObjectSizeChange(*string, old_size, new_size,
                                 ClearRecordedSlots::kNo);
  }
  // We are storing the new length using release store after creating a filler
  // for the left-over space to avoid races with the sweeper thread.
  string->set_length(new_length, kReleaseStore);
  string->ClearPadding();

  return string;
}

SeqString::DataAndPaddingSizes SeqString::GetDataAndPaddingSizes() const {
  if (IsSeqOneByteString(this)) {
    return Cast<SeqOneByteString>(this)->GetDataAndPaddingSizes();
  }
  return Cast<SeqTwoByteString>(this)->GetDataAndPaddingSizes();
}

SeqString::DataAndPaddingSizes SeqOneByteString::GetDataAndPaddingSizes()
    const {
  int data_size = sizeof(SeqOneByteString) + length() * kOneByteSize;
  int padding_size = SizeFor(length()) - data_size;
  return DataAndPaddingSizes{data_size, padding_size};
}

SeqString::DataAndPaddingSizes SeqTwoByteString::GetDataAndPaddingSizes()
    const {
  int data_size = sizeof(SeqTwoByteString) + length() * base::kUC16Size;
  int padding_size = SizeFor(length()) - data_size;
  return DataAndPaddingSizes{data_size, padding_size};
}

#ifdef VERIFY_HEAP
V8_EXPORT_PRIVATE void SeqString::SeqStringVerify(Isolate* isolate) {
  StringVerify(isolate);
  CHECK(IsSeqString(this, isolate));
  DataAndPaddingSizes sz = GetDataAndPaddingSizes();
  auto padding = reinterpret_cast<char*>(address() + sz.data_size);
  CHECK(sz.padding_size <= kTaggedSize);
  for (int i = 0; i < sz.padding_size; ++i) {
    CHECK_EQ(padding[i], 0);
  }
}
#endif  // VERIFY_HEAP

void SeqString::ClearPadding() {
  DataAndPaddingSizes sz = GetDataAndPaddingSizes();
  DCHECK_EQ(sz.data_size + sz.padding_size, Size());
  if (sz.padding_size == 0) return;
  memset(reinterpret_cast<void*>(address() + sz.data_size), 0, sz.padding_size);
}

uint16_t ConsString::Get(
    int index, const SharedStringAccessGuardIfNeeded& access_guard) const {
  DCHECK(index >= 0 && index < this->length());

  // Check for a flattened cons string
  if (second()->length() == 0) {
    Tagged<String> left = first();
    return left->Get(index);
  }

  Tagged<String> string = Cast<String>(this);

  while (true) {
    if (StringShape(string).IsCons()) {
      Tagged<ConsString> cons_string = Cast<ConsString>(string);
      Tagged<String> left = cons_string->first();
      if (left->length() > index) {
        string = left;
      } else {
        index -= left->length();
        string = cons_string->second();
      }
    } else {
      return string->Get(index, access_guard);
    }
  }

  UNREACHABLE();
}

uint16_t ThinString::Get(
    int index, const SharedStringAccessGuardIfNeeded& access_guard) const {
  return actual()->Get(index, access_guard);
}

uint16_t SlicedString::Get(
    int index, const SharedStringAccessGuardIfNeeded& access_guard) const {
  return parent()->Get(offset() + index, access_guard);
}

int ExternalString::ExternalPayloadSize() const {
  int length_multiplier = IsTwoByteRepresentation() ? i::kShortSize : kCharSize;
  return length() * length_multiplier;
}

FlatStringReader::FlatStringReader(Isolate* isolate, Handle<String> str)
    : Relocatable(isolate), str_(str), length_(str->length()) {
#if DEBUG
  // Check that this constructor is called only from the main thread.
  DCHECK_EQ(ThreadId::Current(), isolate->thread_id());
#endif
  PostGarbageCollection();
}

void FlatStringReader::PostGarbageCollection() {
  DCHECK(str_->IsFlat());
  DisallowGarbageCollection no_gc;
  // This does not actually prevent the vector from being relocated later.
  String::FlatContent content = str_->GetFlatContent(no_gc);
  DCHECK(content.IsFlat());
  is_one_byte_ = content.IsOneByte();
  if (is_one_byte_) {
    start_ = content.ToOneByteVector().begin();
  } else {
    start_ = content.ToUC16Vector().begin();
  }
}

void ConsStringIterator::Initialize(Tagged<ConsString> cons_string,
                                    int offset) {
  DCHECK(!cons_string.is_null());
  root_ = cons_string;
  consumed_ = offset;
  // Force stack blown condition to trigger restart.
  depth_ = 1;
  maximum_depth_ = kStackSize + depth_;
  DCHECK(StackBlown());
}

Tagged<String> ConsStringIterator::Continue(int* offset_out) {
  DCHECK_NE(depth_, 0);
  DCHECK_EQ(0, *offset_out);
  bool blew_stack = StackBlown();
  Tagged<String> string;
  // Get the next leaf if there is one.
  if (!blew_stack) string = NextLeaf(&blew_stack);
  // Restart search from root.
  if (blew_stack) {
    DCHECK(string.is_null());
    string = Search(offset_out);
  }
  // Ensure future calls return null immediately.
  if (string.is_null()) Reset({});
  return string;
}

Tagged<String> ConsStringIterator::Search(int* offset_out) {
  Tagged<ConsString> cons_string = root_;
  // Reset the stack, pushing the root string.
  depth_ = 1;
  maximum_depth_ = 1;
  frames_[0] = cons_string;
  const int consumed = consumed_;
  int offset = 0;
  while (true) {
    // Loop until the string is found which contains the target offset.
    Tagged<String> string = cons_string->first();
    int length = string->length();
    int32_t type;
    if (consumed < offset + length) {
      // Target offset is in the left branch.
      // Keep going if we're still in a ConString.
      type = string->map()->instance_type();
      if ((type & kStringRepresentationMask) == kConsStringTag) {
        cons_string = Cast<ConsString>(string);
        PushLeft(cons_string);
        continue;
      }
      // Tell the stack we're done descending.
      AdjustMaximumDepth();
    } else {
      // Descend right.
      // Update progress through the string.
      offset += length;
      // Keep going if we're still in a ConString.
      string = cons_string->second();
      type = string->map()->instance_type();
      if ((type & kStringRepresentationMask) == kConsStringTag) {
        cons_string = Cast<ConsString>(string);
        PushRight(cons_string);
        continue;
      }
      // Need this to be updated for the current string.
      length = string->length();
      // Account for the possibility of an empty right leaf.
      // This happens only if we have asked for an offset outside the string.
      if (length == 0) {
        // Reset so future operations will return null immediately.
        Reset({});
        return {};
      }
      // Tell the stack we're done descending.
      AdjustMaximumDepth();
      // Pop stack so next iteration is in correct place.
      Pop();
    }
    DCHECK_NE(length, 0);
    // Adjust return values and exit.
    consumed_ = offset + length;
    *offset_out = consumed - offset;
    return string;
  }
  UNREACHABLE();
}

Tagged<String> ConsStringIterator::NextLeaf(bool* blew_stack) {
  while (true) {
    // Tree traversal complete.
    if (depth_ == 0) {
      *blew_stack = false;
      return {};
    }
    // We've lost track of higher nodes.
    if (StackBlown()) {
      *blew_stack = true;
      return {};
    }
    // Go right.
    Tagged<ConsString> cons_string = frames_[OffsetForDepth(depth_ - 1)];
    Tagged<String> string = cons_string->second();
    int32_t type = string->map()->instance_type();
    if ((type & kStringRepresentationMask) != kConsStringTag) {
      // Pop stack so next iteration is in correct place.
      Pop();
      int length = string->length();
      // Could be a flattened ConsString.
      if (length == 0) continue;
      consumed_ += length;
      return string;
    }
    cons_string = Cast<ConsString>(string);
    PushRight(cons_string);
    // Need to traverse all the way left.
    while (true) {
      // Continue left.
      string = cons_string->first();
      type = string->map()->instance_type();
      if ((type & kStringRepresentationMask) != kConsStringTag) {
        AdjustMaximumDepth();
        int length = string->length();
        if (length == 0) break;  // Skip empty left-hand sides of ConsStrings.
        consumed_ += length;
        return string;
      }
      cons_string = Cast<ConsString>(string);
      PushLeft(cons_string);
    }
  }
  UNREACHABLE();
}

const uint8_t* String::AddressOfCharacterAt(
    int start_index, const DisallowGarbageCollection& no_gc) {
  DCHECK(IsFlat());
  Tagged<String> subject = this;
  StringShape shape(subject);
  if (IsConsString(subject)) {
    subject = Cast<ConsString>(subject)->first();
    shape = StringShape(subject);
  } else if (IsSlicedString(subject)) {
    start_index += Cast<SlicedString>(subject)->offset();
    subject = Cast<SlicedString>(subject)->parent();
    shape = StringShape(subject);
  }
  if (IsThinString(subject)) {
    subject = Cast<ThinString>(subject)->actual();
    shape = StringShape(subject);
  }
  CHECK_LE(0, start_index);
  CHECK_LE(start_index, subject->length());
  switch (shape.representation_and_encoding_tag()) {
    case kOneByteStringTag | kSeqStringTag:
      return reinterpret_cast<const uint8_t*>(
          Cast<SeqOneByteString>(subject)->GetChars(no_gc) + start_index);
    case kTwoByteStringTag | kSeqStringTag:
      return reinterpret_cast<const uint8_t*>(
          Cast<SeqTwoByteString>(subject)->GetChars(no_gc) + start_index);
    case kOneByteStringTag | kExternalStringTag:
      return reinterpret_cast<const uint8_t*>(
          Cast<ExternalOneByteString>(subject)->GetChars() + start_index);
    case kTwoByteStringTag | kExternalStringTag:
      return reinterpret_cast<const uint8_t*>(
          Cast<ExternalTwoByteString>(subject)->GetChars() + start_index);
    default:
      UNREACHABLE();
  }
}

template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void String::WriteToFlat(
    Tagged<String> source, uint16_t* sink, int from, int to);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void String::WriteToFlat(
    Tagged<String> source, uint8_t* sink, int from, int to);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void String::WriteToFlat(
    Tagged<String> source, uint16_t* sink, int from, int to,
    const SharedStringAccessGuardIfNeeded&);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) void String::WriteToFlat(
    Tagged<String> source, uint8_t* sink, int from, int to,
    const SharedStringAccessGuardIfNeeded&);

namespace {
// Check that the constants defined in src/objects/instance-type.h coincides
// with the Torque-definition of string instance types in src/objects/string.tq.

DEFINE_TORQUE_GENERATED_STRING_INSTANCE_TYPE()

static_assert(kStringRepresentationMask == RepresentationBits::kMask);

static_assert(kStringEncodingMask == IsOneByteBit::kMask);
static_assert(kTwoByteStringTag == IsOneByteBit::encode(false));
static_assert(kOneByteStringTag == IsOneByteBit::encode(true));

static_assert(kUncachedExternalStringMask == IsUncachedBit::kMask);
static_assert(kUncachedExternalStringTag == IsUncachedBit::encode(true));

static_assert(kIsNotInternalizedMask == IsNotInternalizedBit::kMask);
static_assert(kNotInternalizedTag == IsNotInternalizedBit::encode(true));
static_assert(kInternalizedTag == IsNotInternalizedBit::encode(false));
}  // namespace

}  // namespace internal
}  // namespace v8

// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ROOTS_ROOTS_INL_H_
#define V8_ROOTS_ROOTS_INL_H_

#include "src/common/ptr-compr-inl.h"
#include "src/execution/isolate.h"
#include "src/execution/local-isolate.h"
#include "src/handles/handles.h"
#include "src/heap/page-metadata-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/cell.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/heap-number.h"
#include "src/objects/hole.h"
#include "src/objects/literal-objects.h"
#include "src/objects/map.h"
#include "src/objects/oddball.h"
#include "src/objects/property-array.h"
#include "src/objects/property-cell.h"
#include "src/objects/scope-info.h"
#include "src/objects/slots.h"
#include "src/objects/string.h"
#include "src/objects/swiss-name-dictionary.h"
#include "src/objects/tagged.h"
#include "src/roots/roots.h"
#include "src/roots/static-roots.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-objects.h"
#endif

namespace v8 {
namespace internal {

V8_INLINE constexpr bool operator<(RootIndex lhs, RootIndex rhs) {
  using type = typename std::underlying_type<RootIndex>::type;
  return static_cast<type>(lhs) < static_cast<type>(rhs);
}

V8_INLINE RootIndex operator++(RootIndex& index) {
  using type = typename std::underlying_type<RootIndex>::type;
  index = static_cast<RootIndex>(static_cast<type>(index) + 1);
  return index;
}

bool RootsTable::IsRootHandleLocation(Address* handle_location,
                                      RootIndex* index) const {
  FullObjectSlot location(handle_location);
  FullObjectSlot first_root(&roots_[0]);
  FullObjectSlot last_root(&roots_[kEntriesCount]);
  if (location >= last_root) return false;
  if (location < first_root) return false;
  *index = static_cast<RootIndex>(location - first_root);
  return true;
}

template <typename T>
bool RootsTable::IsRootHandle(Handle<T> handle, RootIndex* index) const {
  // This can't use handle.location() because it is called from places
  // where handle dereferencing is disallowed. Comparing the handle's
  // location against the root handle list is safe though.
  Address* handle_location = reinterpret_cast<Address*>(handle.address());
  return IsRootHandleLocation(handle_location, index);
}

ReadOnlyRoots::ReadOnlyRoots(Heap* heap)
    : ReadOnlyRoots(Isolate::FromHeap(heap)) {}

ReadOnlyRoots::ReadOnlyRoots(const Isolate* isolate)
    : read_only_roots_(reinterpret_cast<Address*>(
          isolate->roots_table().read_only_roots_begin().address())) {}

ReadOnlyRoots::ReadOnlyRoots(LocalIsolate* isolate)
    : ReadOnlyRoots(isolate->factory()->read_only_roots()) {}

// We use UncheckedCast below because we trust our read-only roots to
// have the right type, and to avoid the heavy #includes that would be
// required for checked casts.

#define ROOT_ACCESSOR(Type, name, CamelName)                        \
  Tagged<Type> ReadOnlyRoots::name() const {                        \
    DCHECK(CheckType_##name());                                     \
    return unchecked_##name();                                      \
  }                                                                 \
  Tagged<Type> ReadOnlyRoots::unchecked_##name() const {            \
    return UncheckedCast<Type>(object_at(RootIndex::k##CamelName)); \
  }                                                                 \
  Handle<Type> ReadOnlyRoots::name##_handle() const {               \
    DCHECK(CheckType_##name());                                     \
    Address* location = GetLocation(RootIndex::k##CamelName);       \
    return Handle<Type>(location);                                  \
  }

READ_ONLY_ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

Tagged<Boolean> ReadOnlyRoots::boolean_value(bool value) const {
  return value ? Tagged<Boolean>(true_value()) : Tagged<Boolean>(false_value());
}
Handle<Boolean> ReadOnlyRoots::boolean_value_handle(bool value) const {
  return value ? Handle<Boolean>(true_value_handle())
               : Handle<Boolean>(false_value_handle());
}

Address* ReadOnlyRoots::GetLocation(RootIndex root_index) const {
  size_t index = static_cast<size_t>(root_index);
  DCHECK_LT(index, kEntriesCount);
  Address* location = &read_only_roots_[index];
  // Filler objects must be created before the free space map is initialized.
  // Bootstrapping is able to handle kNullAddress being returned here.
  DCHECK_IMPLIES(*location == kNullAddress,
                 root_index == RootIndex::kFreeSpaceMap);
  return location;
}

Address ReadOnlyRoots::first_name_for_protector() const {
  return address_at(RootIndex::kFirstNameForProtector);
}

Address ReadOnlyRoots::last_name_for_protector() const {
  return address_at(RootIndex::kLastNameForProtector);
}

bool ReadOnlyRoots::IsNameForProtector(Tagged<HeapObject> object) const {
  return base::IsInRange(object.ptr(), first_name_for_protector(),
                         last_name_for_protector());
}

void ReadOnlyRoots::VerifyNameForProtectorsPages() const {
  // The symbols and strings that can cause protector invalidation should
  // reside on the same page so we can do a fast range check.
  CHECK_EQ(PageMetadata::FromAddress(first_name_for_protector()),
           PageMetadata::FromAddress(last_name_for_protector()));
}

Handle<Object> ReadOnlyRoots::handle_at(RootIndex root_index) const {
  return Handle<Object>(GetLocation(root_index));
}

Tagged<Object> ReadOnlyRoots::object_at(RootIndex root_index) const {
  return Tagged<Object>(address_at(root_index));
}

Address ReadOnlyRoots::address_at(RootIndex root_index) const {
#if V8_STATIC_ROOTS_BOOL
  return V8HeapCompressionScheme::DecompressTagged(
      V8HeapCompressionScheme::base(),
      StaticReadOnlyRootsPointerTable[static_cast<int>(root_index)]);
#else
  return *GetLocation(root_index);
#endif
}

bool ReadOnlyRoots::is_initialized(RootIndex root_index) const {
  size_t index = static_cast<size_t>(root_index);
  DCHECK_LT(index, kEntriesCount);
  return read_only_roots_[index] != kNullAddress;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ROOTS_ROOTS_INL_H_

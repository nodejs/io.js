// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/roots/roots.h"

#include <type_traits>

#include "src/common/globals.h"
#include "src/objects/elements-kind.h"
#include "src/objects/heap-object-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/visitors.h"
#include "src/roots/static-roots.h"

namespace v8 {
namespace internal {

static_assert(static_cast<int>(RootIndex::kUndefinedValue) ==
              Internals::kUndefinedValueRootIndex);
static_assert(static_cast<int>(RootIndex::kTheHoleValue) ==
              Internals::kTheHoleValueRootIndex);
static_assert(static_cast<int>(RootIndex::kNullValue) ==
              Internals::kNullValueRootIndex);
static_assert(static_cast<int>(RootIndex::kTrueValue) ==
              Internals::kTrueValueRootIndex);
static_assert(static_cast<int>(RootIndex::kFalseValue) ==
              Internals::kFalseValueRootIndex);
static_assert(static_cast<int>(RootIndex::kempty_string) ==
              Internals::kEmptyStringRootIndex);

const char* RootsTable::root_names_[RootsTable::kEntriesCount] = {
#define ROOT_NAME(type, name, CamelName) #name,
    ROOT_LIST(ROOT_NAME)
#undef ROOT_NAME
};

MapWord ReadOnlyRoots::one_pointer_filler_map_word() {
  return MapWord::FromMap(one_pointer_filler_map());
}

void ReadOnlyRoots::Iterate(RootVisitor* visitor) {
  visitor->VisitRootPointers(Root::kReadOnlyRootList, nullptr,
                             FullObjectSlot(read_only_roots_),
                             FullObjectSlot(&read_only_roots_[kEntriesCount]));
  visitor->Synchronize(VisitorSynchronization::kReadOnlyRootList);
}

#ifdef DEBUG
void ReadOnlyRoots::VerifyNameForProtectors() {
  DisallowGarbageCollection no_gc;
  Tagged<Name> prev;
  for (RootIndex root_index = RootIndex::kFirstNameForProtector;
       root_index <= RootIndex::kLastNameForProtector; ++root_index) {
    Tagged<Name> current = Cast<Name>(object_at(root_index));
    DCHECK(IsNameForProtector(current));
    if (root_index != RootIndex::kFirstNameForProtector) {
      // Make sure the objects are adjacent in memory.
      CHECK_LT(prev.address(), current.address());
      Address computed_address =
          prev.address() + ALIGN_TO_ALLOCATION_ALIGNMENT(prev->Size());
      CHECK_EQ(computed_address, current.address());
    }
    prev = current;
  }
}

#define ROOT_TYPE_CHECK(Type, name, CamelName)                                \
  bool ReadOnlyRoots::CheckType_##name() const {                              \
    Tagged<Type> value = unchecked_##name();                                  \
    /* For the oddball subtypes, the "IsFoo" checks only check for address in \
     * the RORoots, which is trivially true here. So, do a slow check of the  \
     * oddball kind instead. Do the casts via Tagged<Object> to satisfy cast  \
     * compatibility static_asserts in the Tagged class. */                   \
    if (std::is_same_v<Type, Undefined>) {                                    \
      return Cast<Oddball>(Tagged<Object>(value))->kind() ==                  \
             Oddball::kUndefined;                                             \
    } else if (std::is_same_v<Type, Null>) {                                  \
      return Cast<Oddball>(Tagged<Object>(value))->kind() == Oddball::kNull;  \
    } else if (std::is_same_v<Type, True>) {                                  \
      return Cast<Oddball>(Tagged<Object>(value))->kind() == Oddball::kTrue;  \
    } else if (std::is_same_v<Type, False>) {                                 \
      return Cast<Oddball>(Tagged<Object>(value))->kind() == Oddball::kFalse; \
    } else {                                                                  \
      return Is##Type(value);                                                 \
    }                                                                         \
  }

READ_ONLY_ROOT_LIST(ROOT_TYPE_CHECK)
#undef ROOT_TYPE_CHECK
#endif

Handle<HeapNumber> ReadOnlyRoots::FindHeapNumber(double value) {
  auto bits = base::bit_cast<uint64_t>(value);
  for (auto pos = RootIndex::kFirstHeapNumberRoot;
       pos <= RootIndex::kLastHeapNumberRoot; ++pos) {
    auto root = Cast<HeapNumber>(object_at(pos));
    if (base::bit_cast<uint64_t>(root->value()) == bits) {
      return Handle<HeapNumber>(GetLocation(pos));
    }
  }
  return Handle<HeapNumber>();
}

void ReadOnlyRoots::InitFromStaticRootsTable(Address cage_base) {
  CHECK(V8_STATIC_ROOTS_BOOL);
#if V8_STATIC_ROOTS_BOOL
  RootIndex pos = RootIndex::kFirstReadOnlyRoot;
  for (auto element : StaticReadOnlyRootsPointerTable) {
    auto ptr = V8HeapCompressionScheme::DecompressTagged(cage_base, element);
    DCHECK(!is_initialized(pos));
    read_only_roots_[static_cast<size_t>(pos)] = ptr;
    ++pos;
  }
  DCHECK_EQ(static_cast<int>(pos) - 1, RootIndex::kLastReadOnlyRoot);
#endif  // V8_STATIC_ROOTS_BOOL
}

}  // namespace internal
}  // namespace v8

// Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_CONTAINER_INTERNAL_INLINED_VECTOR_H_
#define ABSL_CONTAINER_INTERNAL_INLINED_VECTOR_H_

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/container/internal/compressed_tuple.h"
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"
#include "absl/types/span.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace inlined_vector_internal {

// GCC does not deal very well with the below code
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

template <typename A>
using AllocatorTraits = std::allocator_traits<A>;
template <typename A>
using ValueType = typename AllocatorTraits<A>::value_type;
template <typename A>
using SizeType = typename AllocatorTraits<A>::size_type;
template <typename A>
using Pointer = typename AllocatorTraits<A>::pointer;
template <typename A>
using ConstPointer = typename AllocatorTraits<A>::const_pointer;
template <typename A>
using SizeType = typename AllocatorTraits<A>::size_type;
template <typename A>
using DifferenceType = typename AllocatorTraits<A>::difference_type;
template <typename A>
using Reference = ValueType<A>&;
template <typename A>
using ConstReference = const ValueType<A>&;
template <typename A>
using Iterator = Pointer<A>;
template <typename A>
using ConstIterator = ConstPointer<A>;
template <typename A>
using ReverseIterator = typename std::reverse_iterator<Iterator<A>>;
template <typename A>
using ConstReverseIterator = typename std::reverse_iterator<ConstIterator<A>>;
template <typename A>
using MoveIterator = typename std::move_iterator<Iterator<A>>;

template <typename Iterator>
using IsAtLeastForwardIterator = std::is_convertible<
    typename std::iterator_traits<Iterator>::iterator_category,
    std::forward_iterator_tag>;

template <typename A>
using IsMoveAssignOk = std::is_move_assignable<ValueType<A>>;
template <typename A>
using IsSwapOk = absl::type_traits_internal::IsSwappable<ValueType<A>>;

template <typename T>
struct TypeIdentity {
  using type = T;
};

// Used for function arguments in template functions to prevent ADL by forcing
// callers to explicitly specify the template parameter.
template <typename T>
using NoTypeDeduction = typename TypeIdentity<T>::type;

template <typename A, bool IsTriviallyDestructible =
                          absl::is_trivially_destructible<ValueType<A>>::value>
struct DestroyAdapter;

template <typename A>
struct DestroyAdapter<A, /* IsTriviallyDestructible */ false> {
  static void DestroyElements(A& allocator, Pointer<A> destroy_first,
                              SizeType<A> destroy_size) {
    for (SizeType<A> i = destroy_size; i != 0;) {
      --i;
      AllocatorTraits<A>::destroy(allocator, destroy_first + i);
    }
  }
};

template <typename A>
struct DestroyAdapter<A, /* IsTriviallyDestructible */ true> {
  static void DestroyElements(A& allocator, Pointer<A> destroy_first,
                              SizeType<A> destroy_size) {
    static_cast<void>(allocator);
    static_cast<void>(destroy_first);
    static_cast<void>(destroy_size);
  }
};

template <typename A>
struct Allocation {
  Pointer<A> data = nullptr;
  SizeType<A> capacity = 0;
};

template <typename A,
          bool IsOverAligned =
              (alignof(ValueType<A>) > ABSL_INTERNAL_DEFAULT_NEW_ALIGNMENT)>
struct MallocAdapter {
  static Allocation<A> Allocate(A& allocator, SizeType<A> requested_capacity) {
    return {AllocatorTraits<A>::allocate(allocator, requested_capacity),
            requested_capacity};
  }

  static void Deallocate(A& allocator, Pointer<A> pointer,
                         SizeType<A> capacity) {
    AllocatorTraits<A>::deallocate(allocator, pointer, capacity);
  }
};

template <typename A, typename ValueAdapter>
void ConstructElements(NoTypeDeduction<A>& allocator,
                       Pointer<A> construct_first, ValueAdapter& values,
                       SizeType<A> construct_size) {
  for (SizeType<A> i = 0; i < construct_size; ++i) {
    ABSL_INTERNAL_TRY { values.ConstructNext(allocator, construct_first + i); }
    ABSL_INTERNAL_CATCH_ANY {
      DestroyAdapter<A>::DestroyElements(allocator, construct_first, i);
      ABSL_INTERNAL_RETHROW;
    }
  }
}

template <typename A, typename ValueAdapter>
void AssignElements(Pointer<A> assign_first, ValueAdapter& values,
                    SizeType<A> assign_size) {
  for (SizeType<A> i = 0; i < assign_size; ++i) {
    values.AssignNext(assign_first + i);
  }
}

template <typename A>
struct StorageView {
  Pointer<A> data;
  SizeType<A> size;
  SizeType<A> capacity;
};

template <typename A, typename Iterator>
class IteratorValueAdapter {
 public:
  explicit IteratorValueAdapter(const Iterator& it) : it_(it) {}

  void ConstructNext(A& allocator, Pointer<A> construct_at) {
    AllocatorTraits<A>::construct(allocator, construct_at, *it_);
    ++it_;
  }

  void AssignNext(Pointer<A> assign_at) {
    *assign_at = *it_;
    ++it_;
  }

 private:
  Iterator it_;
};

template <typename A>
class CopyValueAdapter {
 public:
  explicit CopyValueAdapter(ConstPointer<A> p) : ptr_(p) {}

  void ConstructNext(A& allocator, Pointer<A> construct_at) {
    AllocatorTraits<A>::construct(allocator, construct_at, *ptr_);
  }

  void AssignNext(Pointer<A> assign_at) { *assign_at = *ptr_; }

 private:
  ConstPointer<A> ptr_;
};

template <typename A>
class DefaultValueAdapter {
 public:
  explicit DefaultValueAdapter() {}

  void ConstructNext(A& allocator, Pointer<A> construct_at) {
    AllocatorTraits<A>::construct(allocator, construct_at);
  }

  void AssignNext(Pointer<A> assign_at) { *assign_at = ValueType<A>(); }
};

template <typename A>
class AllocationTransaction {
 public:
  explicit AllocationTransaction(A& allocator)
      : allocator_data_(allocator, nullptr), capacity_(0) {}

  ~AllocationTransaction() {
    if (DidAllocate()) {
      MallocAdapter<A>::Deallocate(GetAllocator(), GetData(), GetCapacity());
    }
  }

  AllocationTransaction(const AllocationTransaction&) = delete;
  void operator=(const AllocationTransaction&) = delete;

  A& GetAllocator() { return allocator_data_.template get<0>(); }
  Pointer<A>& GetData() { return allocator_data_.template get<1>(); }
  SizeType<A>& GetCapacity() { return capacity_; }

  bool DidAllocate() { return GetData() != nullptr; }

  Pointer<A> Allocate(SizeType<A> requested_capacity) {
    Allocation<A> result =
        MallocAdapter<A>::Allocate(GetAllocator(), requested_capacity);
    GetData() = result.data;
    GetCapacity() = result.capacity;
    return result.data;
  }

  ABSL_MUST_USE_RESULT Allocation<A> Release() && {
    Allocation<A> result = {GetData(), GetCapacity()};
    Reset();
    return result;
  }

 private:
  void Reset() {
    GetData() = nullptr;
    GetCapacity() = 0;
  }

  container_internal::CompressedTuple<A, Pointer<A>> allocator_data_;
  SizeType<A> capacity_;
};

template <typename A>
class ConstructionTransaction {
 public:
  explicit ConstructionTransaction(A& allocator)
      : allocator_data_(allocator, nullptr), size_(0) {}

  ~ConstructionTransaction() {
    if (DidConstruct()) {
      DestroyAdapter<A>::DestroyElements(GetAllocator(), GetData(), GetSize());
    }
  }

  ConstructionTransaction(const ConstructionTransaction&) = delete;
  void operator=(const ConstructionTransaction&) = delete;

  A& GetAllocator() { return allocator_data_.template get<0>(); }
  Pointer<A>& GetData() { return allocator_data_.template get<1>(); }
  SizeType<A>& GetSize() { return size_; }

  bool DidConstruct() { return GetData() != nullptr; }
  template <typename ValueAdapter>
  void Construct(Pointer<A> data, ValueAdapter& values, SizeType<A> size) {
    ConstructElements<A>(GetAllocator(), data, values, size);
    GetData() = data;
    GetSize() = size;
  }
  void Commit() && {
    GetData() = nullptr;
    GetSize() = 0;
  }

 private:
  container_internal::CompressedTuple<A, Pointer<A>> allocator_data_;
  SizeType<A> size_;
};

template <typename T, size_t N, typename A>
class Storage {
 public:
  struct MemcpyPolicy {};
  struct ElementwiseAssignPolicy {};
  struct ElementwiseSwapPolicy {};
  struct ElementwiseConstructPolicy {};

  using MoveAssignmentPolicy = absl::conditional_t<
      // Fast path: if the value type can be trivially move assigned and
      // destroyed, and we know the allocator doesn't do anything fancy, then
      // it's safe for us to simply adopt the contents of the storage for
      // `other` and remove its own reference to them. It's as if we had
      // individually move-assigned each value and then destroyed the original.
      absl::conjunction<absl::is_trivially_move_assignable<ValueType<A>>,
                        absl::is_trivially_destructible<ValueType<A>>,
                        std::is_same<A, std::allocator<ValueType<A>>>>::value,
      MemcpyPolicy,
      // Otherwise we use move assignment if possible. If not, we simulate
      // move assignment using move construction.
      //
      // Note that this is in contrast to e.g. std::vector and std::optional,
      // which are themselves not move-assignable when their contained type is
      // not.
      absl::conditional_t<IsMoveAssignOk<A>::value, ElementwiseAssignPolicy,
                          ElementwiseConstructPolicy>>;

  // The policy to be used specifically when swapping inlined elements.
  using SwapInlinedElementsPolicy = absl::conditional_t<
      // Fast path: if the value type can be trivially relocated, and we
      // know the allocator doesn't do anything fancy, then it's safe for us
      // to simply swap the bytes in the inline storage. It's as if we had
      // relocated the first vector's elements into temporary storage,
      // relocated the second's elements into the (now-empty) first's,
      // and then relocated from temporary storage into the second.
      absl::conjunction<absl::is_trivially_relocatable<ValueType<A>>,
                        std::is_same<A, std::allocator<ValueType<A>>>>::value,
      MemcpyPolicy,
      absl::conditional_t<IsSwapOk<A>::value, ElementwiseSwapPolicy,
                          ElementwiseConstructPolicy>>;

  static SizeType<A> NextCapacity(SizeType<A> current_capacity) {
    return current_capacity * 2;
  }

  static SizeType<A> ComputeCapacity(SizeType<A> current_capacity,
                                     SizeType<A> requested_capacity) {
    return (std::max)(NextCapacity(current_capacity), requested_capacity);
  }

  // ---------------------------------------------------------------------------
  // Storage Constructors and Destructor
  // ---------------------------------------------------------------------------

  Storage() : metadata_(A(), /* size and is_allocated */ 0u) {}

  explicit Storage(const A& allocator)
      : metadata_(allocator, /* size and is_allocated */ 0u) {}

  ~Storage() {
    // Fast path: if we are empty and not allocated, there's nothing to do.
    if (GetSizeAndIsAllocated() == 0) {
      return;
    }

    // Fast path: if no destructors need to be run and we know the allocator
    // doesn't do anything fancy, then all we need to do is deallocate (and
    // maybe not even that).
    if (absl::is_trivially_destructible<ValueType<A>>::value &&
        std::is_same<A, std::allocator<ValueType<A>>>::value) {
      DeallocateIfAllocated();
      return;
    }

    DestroyContents();
  }

  // ---------------------------------------------------------------------------
  // Storage Member Accessors
  // ---------------------------------------------------------------------------

  SizeType<A>& GetSizeAndIsAllocated() { return metadata_.template get<1>(); }

  const SizeType<A>& GetSizeAndIsAllocated() const {
    return metadata_.template get<1>();
  }

  SizeType<A> GetSize() const { return GetSizeAndIsAllocated() >> 1; }

  bool GetIsAllocated() const { return GetSizeAndIsAllocated() & 1; }

  Pointer<A> GetAllocatedData() {
    // GCC 12 has a false-positive -Wmaybe-uninitialized warning here.
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    return data_.allocated.allocated_data;
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic pop
#endif
  }

  ConstPointer<A> GetAllocatedData() const {
    return data_.allocated.allocated_data;
  }

  // ABSL_ATTRIBUTE_NO_SANITIZE_CFI is used because the memory pointed to may be
  // uninitialized, a common pattern in allocate()+construct() APIs.
  // https://clang.llvm.org/docs/ControlFlowIntegrity.html#bad-cast-checking
  // NOTE: When this was written, LLVM documentation did not explicitly
  // mention that casting `char*` and using `reinterpret_cast` qualifies
  // as a bad cast.
  ABSL_ATTRIBUTE_NO_SANITIZE_CFI Pointer<A> GetInlinedData() {
    return reinterpret_cast<Pointer<A>>(data_.inlined.inlined_data);
  }

  ABSL_ATTRIBUTE_NO_SANITIZE_CFI ConstPointer<A> GetInlinedData() const {
    return reinterpret_cast<ConstPointer<A>>(data_.inlined.inlined_data);
  }

  SizeType<A> GetAllocatedCapacity() const {
    return data_.allocated.allocated_capacity;
  }

  SizeType<A> GetInlinedCapacity() const {
    return static_cast<SizeType<A>>(kOptimalInlinedSize);
  }

  StorageView<A> MakeStorageView() {
    return GetIsAllocated() ? StorageView<A>{GetAllocatedData(), GetSize(),
                                             GetAllocatedCapacity()}
                            : StorageView<A>{GetInlinedData(), GetSize(),
                                             GetInlinedCapacity()};
  }

  A& GetAllocator() { return metadata_.template get<0>(); }

  const A& GetAllocator() const { return metadata_.template get<0>(); }

  // ---------------------------------------------------------------------------
  // Storage Member Mutators
  // ---------------------------------------------------------------------------

  ABSL_ATTRIBUTE_NOINLINE void InitFrom(const Storage& other);

  template <typename ValueAdapter>
  void Initialize(ValueAdapter values, SizeType<A> new_size);

  template <typename ValueAdapter>
  void Assign(ValueAdapter values, SizeType<A> new_size);

  template <typename ValueAdapter>
  void Resize(ValueAdapter values, SizeType<A> new_size);

  template <typename ValueAdapter>
  Iterator<A> Insert(ConstIterator<A> pos, ValueAdapter values,
                     SizeType<A> insert_count);

  template <typename... Args>
  Reference<A> EmplaceBack(Args&&... args);

  Iterator<A> Erase(ConstIterator<A> from, ConstIterator<A> to);

  void Reserve(SizeType<A> requested_capacity);

  void ShrinkToFit();

  void Swap(Storage* other_storage_ptr);

  void SetIsAllocated() {
    GetSizeAndIsAllocated() |= static_cast<SizeType<A>>(1);
  }

  void UnsetIsAllocated() {
    GetSizeAndIsAllocated() &= ((std::numeric_limits<SizeType<A>>::max)() - 1);
  }

  void SetSize(SizeType<A> size) {
    GetSizeAndIsAllocated() =
        (size << 1) | static_cast<SizeType<A>>(GetIsAllocated());
  }

  void SetAllocatedSize(SizeType<A> size) {
    GetSizeAndIsAllocated() = (size << 1) | static_cast<SizeType<A>>(1);
  }

  void SetInlinedSize(SizeType<A> size) {
    GetSizeAndIsAllocated() = size << static_cast<SizeType<A>>(1);
  }

  void AddSize(SizeType<A> count) {
    GetSizeAndIsAllocated() += count << static_cast<SizeType<A>>(1);
  }

  void SubtractSize(SizeType<A> count) {
    ABSL_HARDENING_ASSERT(count <= GetSize());

    GetSizeAndIsAllocated() -= count << static_cast<SizeType<A>>(1);
  }

  void SetAllocation(Allocation<A> allocation) {
    data_.allocated.allocated_data = allocation.data;
    data_.allocated.allocated_capacity = allocation.capacity;
  }

  void MemcpyFrom(const Storage& other_storage) {
    // Assumption check: it doesn't make sense to memcpy inlined elements unless
    // we know the allocator doesn't do anything fancy, and one of the following
    // holds:
    //
    //  *  The elements are trivially relocatable.
    //
    //  *  It's possible to trivially assign the elements and then destroy the
    //     source.
    //
    //  *  It's possible to trivially copy construct/assign the elements.
    //
    {
      using V = ValueType<A>;
      ABSL_HARDENING_ASSERT(
          other_storage.GetIsAllocated() ||
          (std::is_same<A, std::allocator<V>>::value &&
           (
               // First case above
               absl::is_trivially_relocatable<V>::value ||
               // Second case above
               (absl::is_trivially_move_assignable<V>::value &&
                absl::is_trivially_destructible<V>::value) ||
               // Third case above
               (absl::is_trivially_copy_constructible<V>::value ||
                absl::is_trivially_copy_assignable<V>::value))));
    }

    GetSizeAndIsAllocated() = other_storage.GetSizeAndIsAllocated();
    data_ = other_storage.data_;
  }

  void DeallocateIfAllocated() {
    if (GetIsAllocated()) {
      MallocAdapter<A>::Deallocate(GetAllocator(), GetAllocatedData(),
                                   GetAllocatedCapacity());
    }
  }

 private:
  ABSL_ATTRIBUTE_NOINLINE void DestroyContents();

  using Metadata = container_internal::CompressedTuple<A, SizeType<A>>;

  struct Allocated {
    Pointer<A> allocated_data;
    SizeType<A> allocated_capacity;
  };

  // `kOptimalInlinedSize` is an automatically adjusted inlined capacity of the
  // `InlinedVector`. Sometimes, it is possible to increase the capacity (from
  // the user requested `N`) without increasing the size of the `InlinedVector`.
  static constexpr size_t kOptimalInlinedSize =
      (std::max)(N, sizeof(Allocated) / sizeof(ValueType<A>));

  struct Inlined {
    alignas(ValueType<A>) char inlined_data[sizeof(
        ValueType<A>[kOptimalInlinedSize])];
  };

  union Data {
    Allocated allocated;
    Inlined inlined;
  };

  void SwapN(ElementwiseSwapPolicy, Storage* other, SizeType<A> n);
  void SwapN(ElementwiseConstructPolicy, Storage* other, SizeType<A> n);

  void SwapInlinedElements(MemcpyPolicy, Storage* other);
  template <typename NotMemcpyPolicy>
  void SwapInlinedElements(NotMemcpyPolicy, Storage* other);

  template <typename... Args>
  ABSL_ATTRIBUTE_NOINLINE Reference<A> EmplaceBackSlow(Args&&... args);

  Metadata metadata_;
  Data data_;
};

template <typename T, size_t N, typename A>
void Storage<T, N, A>::DestroyContents() {
  Pointer<A> data = GetIsAllocated() ? GetAllocatedData() : GetInlinedData();
  DestroyAdapter<A>::DestroyElements(GetAllocator(), data, GetSize());
  DeallocateIfAllocated();
}

template <typename T, size_t N, typename A>
void Storage<T, N, A>::InitFrom(const Storage& other) {
  const SizeType<A> n = other.GetSize();
  ABSL_HARDENING_ASSERT(n > 0);  // Empty sources handled handled in caller.
  ConstPointer<A> src;
  Pointer<A> dst;
  if (!other.GetIsAllocated()) {
    dst = GetInlinedData();
    src = other.GetInlinedData();
  } else {
    // Because this is only called from the `InlinedVector` constructors, it's
    // safe to take on the allocation with size `0`. If `ConstructElements(...)`
    // throws, deallocation will be automatically handled by `~Storage()`.
    SizeType<A> requested_capacity = ComputeCapacity(GetInlinedCapacity(), n);
    Allocation<A> allocation =
        MallocAdapter<A>::Allocate(GetAllocator(), requested_capacity);
    SetAllocation(allocation);
    dst = allocation.data;
    src = other.GetAllocatedData();
  }

  // Fast path: if the value type is trivially copy constructible and we know
  // the allocator doesn't do anything fancy, then we know it is legal for us to
  // simply memcpy the other vector's elements.
  if (absl::is_trivially_copy_constructible<ValueType<A>>::value &&
      std::is_same<A, std::allocator<ValueType<A>>>::value) {
    std::memcpy(reinterpret_cast<char*>(dst),
                reinterpret_cast<const char*>(src), n * sizeof(ValueType<A>));
  } else {
    auto values = IteratorValueAdapter<A, ConstPointer<A>>(src);
    ConstructElements<A>(GetAllocator(), dst, values, n);
  }

  GetSizeAndIsAllocated() = other.GetSizeAndIsAllocated();
}

template <typename T, size_t N, typename A>
template <typename ValueAdapter>
auto Storage<T, N, A>::Initialize(ValueAdapter values,
                                  SizeType<A> new_size) -> void {
  // Only callable from constructors!
  ABSL_HARDENING_ASSERT(!GetIsAllocated());
  ABSL_HARDENING_ASSERT(GetSize() == 0);

  Pointer<A> construct_data;
  if (new_size > GetInlinedCapacity()) {
    // Because this is only called from the `InlinedVector` constructors, it's
    // safe to take on the allocation with size `0`. If `ConstructElements(...)`
    // throws, deallocation will be automatically handled by `~Storage()`.
    SizeType<A> requested_capacity =
        ComputeCapacity(GetInlinedCapacity(), new_size);
    Allocation<A> allocation =
        MallocAdapter<A>::Allocate(GetAllocator(), requested_capacity);
    construct_data = allocation.data;
    SetAllocation(allocation);
    SetIsAllocated();
  } else {
    construct_data = GetInlinedData();
  }

  ConstructElements<A>(GetAllocator(), construct_data, values, new_size);

  // Since the initial size was guaranteed to be `0` and the allocated bit is
  // already correct for either case, *adding* `new_size` gives us the correct
  // result faster than setting it directly.
  AddSize(new_size);
}

template <typename T, size_t N, typename A>
template <typename ValueAdapter>
auto Storage<T, N, A>::Assign(ValueAdapter values,
                              SizeType<A> new_size) -> void {
  StorageView<A> storage_view = MakeStorageView();

  AllocationTransaction<A> allocation_tx(GetAllocator());

  absl::Span<ValueType<A>> assign_loop;
  absl::Span<ValueType<A>> construct_loop;
  absl::Span<ValueType<A>> destroy_loop;

  if (new_size > storage_view.capacity) {
    SizeType<A> requested_capacity =
        ComputeCapacity(storage_view.capacity, new_size);
    construct_loop = {allocation_tx.Allocate(requested_capacity), new_size};
    destroy_loop = {storage_view.data, storage_view.size};
  } else if (new_size > storage_view.size) {
    assign_loop = {storage_view.data, storage_view.size};
    construct_loop = {storage_view.data + storage_view.size,
                      new_size - storage_view.size};
  } else {
    assign_loop = {storage_view.data, new_size};
    destroy_loop = {storage_view.data + new_size, storage_view.size - new_size};
  }

  AssignElements<A>(assign_loop.data(), values, assign_loop.size());

  ConstructElements<A>(GetAllocator(), construct_loop.data(), values,
                       construct_loop.size());

  DestroyAdapter<A>::DestroyElements(GetAllocator(), destroy_loop.data(),
                                     destroy_loop.size());

  if (allocation_tx.DidAllocate()) {
    DeallocateIfAllocated();
    SetAllocation(std::move(allocation_tx).Release());
    SetIsAllocated();
  }

  SetSize(new_size);
}

template <typename T, size_t N, typename A>
template <typename ValueAdapter>
auto Storage<T, N, A>::Resize(ValueAdapter values,
                              SizeType<A> new_size) -> void {
  StorageView<A> storage_view = MakeStorageView();
  Pointer<A> const base = storage_view.data;
  const SizeType<A> size = storage_view.size;
  A& alloc = GetAllocator();
  if (new_size <= size) {
    // Destroy extra old elements.
    DestroyAdapter<A>::DestroyElements(alloc, base + new_size, size - new_size);
  } else if (new_size <= storage_view.capacity) {
    // Construct new elements in place.
    ConstructElements<A>(alloc, base + size, values, new_size - size);
  } else {
    // Steps:
    //  a. Allocate new backing store.
    //  b. Construct new elements in new backing store.
    //  c. Move existing elements from old backing store to new backing store.
    //  d. Destroy all elements in old backing store.
    // Use transactional wrappers for the first two steps so we can roll
    // back if necessary due to exceptions.
    AllocationTransaction<A> allocation_tx(alloc);
    SizeType<A> requested_capacity =
        ComputeCapacity(storage_view.capacity, new_size);
    Pointer<A> new_data = allocation_tx.Allocate(requested_capacity);

    ConstructionTransaction<A> construction_tx(alloc);
    construction_tx.Construct(new_data + size, values, new_size - size);

    IteratorValueAdapter<A, MoveIterator<A>> move_values(
        (MoveIterator<A>(base)));
    ConstructElements<A>(alloc, new_data, move_values, size);

    DestroyAdapter<A>::DestroyElements(alloc, base, size);
    std::move(construction_tx).Commit();
    DeallocateIfAllocated();
    SetAllocation(std::move(allocation_tx).Release());
    SetIsAllocated();
  }
  SetSize(new_size);
}

template <typename T, size_t N, typename A>
template <typename ValueAdapter>
auto Storage<T, N, A>::Insert(ConstIterator<A> pos, ValueAdapter values,
                              SizeType<A> insert_count) -> Iterator<A> {
  StorageView<A> storage_view = MakeStorageView();

  auto insert_index = static_cast<SizeType<A>>(
      std::distance(ConstIterator<A>(storage_view.data), pos));
  SizeType<A> insert_end_index = insert_index + insert_count;
  SizeType<A> new_size = storage_view.size + insert_count;

  if (new_size > storage_view.capacity) {
    AllocationTransaction<A> allocation_tx(GetAllocator());
    ConstructionTransaction<A> construction_tx(GetAllocator());
    ConstructionTransaction<A> move_construction_tx(GetAllocator());

    IteratorValueAdapter<A, MoveIterator<A>> move_values(
        MoveIterator<A>(storage_view.data));

    SizeType<A> requested_capacity =
        ComputeCapacity(storage_view.capacity, new_size);
    Pointer<A> new_data = allocation_tx.Allocate(requested_capacity);

    construction_tx.Construct(new_data + insert_index, values, insert_count);

    move_construction_tx.Construct(new_data, move_values, insert_index);

    ConstructElements<A>(GetAllocator(), new_data + insert_end_index,
                         move_values, storage_view.size - insert_index);

    DestroyAdapter<A>::DestroyElements(GetAllocator(), storage_view.data,
                                       storage_view.size);

    std::move(construction_tx).Commit();
    std::move(move_construction_tx).Commit();
    DeallocateIfAllocated();
    SetAllocation(std::move(allocation_tx).Release());

    SetAllocatedSize(new_size);
    return Iterator<A>(new_data + insert_index);
  } else {
    SizeType<A> move_construction_destination_index =
        (std::max)(insert_end_index, storage_view.size);

    ConstructionTransaction<A> move_construction_tx(GetAllocator());

    IteratorValueAdapter<A, MoveIterator<A>> move_construction_values(
        MoveIterator<A>(storage_view.data +
                        (move_construction_destination_index - insert_count)));
    absl::Span<ValueType<A>> move_construction = {
        storage_view.data + move_construction_destination_index,
        new_size - move_construction_destination_index};

    Pointer<A> move_assignment_values = storage_view.data + insert_index;
    absl::Span<ValueType<A>> move_assignment = {
        storage_view.data + insert_end_index,
        move_construction_destination_index - insert_end_index};

    absl::Span<ValueType<A>> insert_assignment = {move_assignment_values,
                                                  move_construction.size()};

    absl::Span<ValueType<A>> insert_construction = {
        insert_assignment.data() + insert_assignment.size(),
        insert_count - insert_assignment.size()};

    move_construction_tx.Construct(move_construction.data(),
                                   move_construction_values,
                                   move_construction.size());

    for (Pointer<A>
             destination = move_assignment.data() + move_assignment.size(),
             last_destination = move_assignment.data(),
             source = move_assignment_values + move_assignment.size();
         ;) {
      --destination;
      --source;
      if (destination < last_destination) break;
      *destination = std::move(*source);
    }

    AssignElements<A>(insert_assignment.data(), values,
                      insert_assignment.size());

    ConstructElements<A>(GetAllocator(), insert_construction.data(), values,
                         insert_construction.size());

    std::move(move_construction_tx).Commit();

    AddSize(insert_count);
    return Iterator<A>(storage_view.data + insert_index);
  }
}

template <typename T, size_t N, typename A>
template <typename... Args>
auto Storage<T, N, A>::EmplaceBack(Args&&... args) -> Reference<A> {
  StorageView<A> storage_view = MakeStorageView();
  const SizeType<A> n = storage_view.size;
  if (ABSL_PREDICT_TRUE(n != storage_view.capacity)) {
    // Fast path; new element fits.
    Pointer<A> last_ptr = storage_view.data + n;
    AllocatorTraits<A>::construct(GetAllocator(), last_ptr,
                                  std::forward<Args>(args)...);
    AddSize(1);
    return *last_ptr;
  }
  // TODO(b/173712035): Annotate with musttail attribute to prevent regression.
  return EmplaceBackSlow(std::forward<Args>(args)...);
}

template <typename T, size_t N, typename A>
template <typename... Args>
auto Storage<T, N, A>::EmplaceBackSlow(Args&&... args) -> Reference<A> {
  StorageView<A> storage_view = MakeStorageView();
  AllocationTransaction<A> allocation_tx(GetAllocator());
  IteratorValueAdapter<A, MoveIterator<A>> move_values(
      MoveIterator<A>(storage_view.data));
  SizeType<A> requested_capacity = NextCapacity(storage_view.capacity);
  Pointer<A> construct_data = allocation_tx.Allocate(requested_capacity);
  Pointer<A> last_ptr = construct_data + storage_view.size;

  // Construct new element.
  AllocatorTraits<A>::construct(GetAllocator(), last_ptr,
                                std::forward<Args>(args)...);
  // Move elements from old backing store to new backing store.
  ABSL_INTERNAL_TRY {
    ConstructElements<A>(GetAllocator(), allocation_tx.GetData(), move_values,
                         storage_view.size);
  }
  ABSL_INTERNAL_CATCH_ANY {
    AllocatorTraits<A>::destroy(GetAllocator(), last_ptr);
    ABSL_INTERNAL_RETHROW;
  }
  // Destroy elements in old backing store.
  DestroyAdapter<A>::DestroyElements(GetAllocator(), storage_view.data,
                                     storage_view.size);

  DeallocateIfAllocated();
  SetAllocation(std::move(allocation_tx).Release());
  SetIsAllocated();
  AddSize(1);
  return *last_ptr;
}

template <typename T, size_t N, typename A>
auto Storage<T, N, A>::Erase(ConstIterator<A> from,
                             ConstIterator<A> to) -> Iterator<A> {
  StorageView<A> storage_view = MakeStorageView();

  auto erase_size = static_cast<SizeType<A>>(std::distance(from, to));
  auto erase_index = static_cast<SizeType<A>>(
      std::distance(ConstIterator<A>(storage_view.data), from));
  SizeType<A> erase_end_index = erase_index + erase_size;

  // Fast path: if the value type is trivially relocatable and we know
  // the allocator doesn't do anything fancy, then we know it is legal for us to
  // simply destroy the elements in the "erasure window" (which cannot throw)
  // and then memcpy downward to close the window.
  if (absl::is_trivially_relocatable<ValueType<A>>::value &&
      std::is_nothrow_destructible<ValueType<A>>::value &&
      std::is_same<A, std::allocator<ValueType<A>>>::value) {
    DestroyAdapter<A>::DestroyElements(
        GetAllocator(), storage_view.data + erase_index, erase_size);
    std::memmove(
        reinterpret_cast<char*>(storage_view.data + erase_index),
        reinterpret_cast<const char*>(storage_view.data + erase_end_index),
        (storage_view.size - erase_end_index) * sizeof(ValueType<A>));
  } else {
    IteratorValueAdapter<A, MoveIterator<A>> move_values(
        MoveIterator<A>(storage_view.data + erase_end_index));

    AssignElements<A>(storage_view.data + erase_index, move_values,
                      storage_view.size - erase_end_index);

    DestroyAdapter<A>::DestroyElements(
        GetAllocator(), storage_view.data + (storage_view.size - erase_size),
        erase_size);
  }
  SubtractSize(erase_size);
  return Iterator<A>(storage_view.data + erase_index);
}

template <typename T, size_t N, typename A>
auto Storage<T, N, A>::Reserve(SizeType<A> requested_capacity) -> void {
  StorageView<A> storage_view = MakeStorageView();

  if (ABSL_PREDICT_FALSE(requested_capacity <= storage_view.capacity)) return;

  AllocationTransaction<A> allocation_tx(GetAllocator());

  IteratorValueAdapter<A, MoveIterator<A>> move_values(
      MoveIterator<A>(storage_view.data));

  SizeType<A> new_requested_capacity =
      ComputeCapacity(storage_view.capacity, requested_capacity);
  Pointer<A> new_data = allocation_tx.Allocate(new_requested_capacity);

  ConstructElements<A>(GetAllocator(), new_data, move_values,
                       storage_view.size);

  DestroyAdapter<A>::DestroyElements(GetAllocator(), storage_view.data,
                                     storage_view.size);

  DeallocateIfAllocated();
  SetAllocation(std::move(allocation_tx).Release());
  SetIsAllocated();
}

template <typename T, size_t N, typename A>
auto Storage<T, N, A>::ShrinkToFit() -> void {
  // May only be called on allocated instances!
  ABSL_HARDENING_ASSERT(GetIsAllocated());

  StorageView<A> storage_view{GetAllocatedData(), GetSize(),
                              GetAllocatedCapacity()};

  if (ABSL_PREDICT_FALSE(storage_view.size == storage_view.capacity)) return;

  AllocationTransaction<A> allocation_tx(GetAllocator());

  IteratorValueAdapter<A, MoveIterator<A>> move_values(
      MoveIterator<A>(storage_view.data));

  Pointer<A> construct_data;
  if (storage_view.size > GetInlinedCapacity()) {
    SizeType<A> requested_capacity = storage_view.size;
    construct_data = allocation_tx.Allocate(requested_capacity);
    if (allocation_tx.GetCapacity() >= storage_view.capacity) {
      // Already using the smallest available heap allocation.
      return;
    }
  } else {
    construct_data = GetInlinedData();
  }

  ABSL_INTERNAL_TRY {
    ConstructElements<A>(GetAllocator(), construct_data, move_values,
                         storage_view.size);
  }
  ABSL_INTERNAL_CATCH_ANY {
    SetAllocation({storage_view.data, storage_view.capacity});
    ABSL_INTERNAL_RETHROW;
  }

  DestroyAdapter<A>::DestroyElements(GetAllocator(), storage_view.data,
                                     storage_view.size);

  MallocAdapter<A>::Deallocate(GetAllocator(), storage_view.data,
                               storage_view.capacity);

  if (allocation_tx.DidAllocate()) {
    SetAllocation(std::move(allocation_tx).Release());
  } else {
    UnsetIsAllocated();
  }
}

template <typename T, size_t N, typename A>
auto Storage<T, N, A>::Swap(Storage* other_storage_ptr) -> void {
  using std::swap;
  ABSL_HARDENING_ASSERT(this != other_storage_ptr);

  if (GetIsAllocated() && other_storage_ptr->GetIsAllocated()) {
    swap(data_.allocated, other_storage_ptr->data_.allocated);
  } else if (!GetIsAllocated() && !other_storage_ptr->GetIsAllocated()) {
    SwapInlinedElements(SwapInlinedElementsPolicy{}, other_storage_ptr);
  } else {
    Storage* allocated_ptr = this;
    Storage* inlined_ptr = other_storage_ptr;
    if (!allocated_ptr->GetIsAllocated()) swap(allocated_ptr, inlined_ptr);

    StorageView<A> allocated_storage_view{
        allocated_ptr->GetAllocatedData(), allocated_ptr->GetSize(),
        allocated_ptr->GetAllocatedCapacity()};

    IteratorValueAdapter<A, MoveIterator<A>> move_values(
        MoveIterator<A>(inlined_ptr->GetInlinedData()));

    ABSL_INTERNAL_TRY {
      ConstructElements<A>(inlined_ptr->GetAllocator(),
                           allocated_ptr->GetInlinedData(), move_values,
                           inlined_ptr->GetSize());
    }
    ABSL_INTERNAL_CATCH_ANY {
      allocated_ptr->SetAllocation(Allocation<A>{
          allocated_storage_view.data, allocated_storage_view.capacity});
      ABSL_INTERNAL_RETHROW;
    }

    DestroyAdapter<A>::DestroyElements(inlined_ptr->GetAllocator(),
                                       inlined_ptr->GetInlinedData(),
                                       inlined_ptr->GetSize());

    inlined_ptr->SetAllocation(Allocation<A>{allocated_storage_view.data,
                                             allocated_storage_view.capacity});
  }

  swap(GetSizeAndIsAllocated(), other_storage_ptr->GetSizeAndIsAllocated());
  swap(GetAllocator(), other_storage_ptr->GetAllocator());
}

template <typename T, size_t N, typename A>
void Storage<T, N, A>::SwapN(ElementwiseSwapPolicy, Storage* other,
                             SizeType<A> n) {
  std::swap_ranges(GetInlinedData(), GetInlinedData() + n,
                   other->GetInlinedData());
}

template <typename T, size_t N, typename A>
void Storage<T, N, A>::SwapN(ElementwiseConstructPolicy, Storage* other,
                             SizeType<A> n) {
  Pointer<A> a = GetInlinedData();
  Pointer<A> b = other->GetInlinedData();
  // see note on allocators in `SwapInlinedElements`.
  A& allocator_a = GetAllocator();
  A& allocator_b = other->GetAllocator();
  for (SizeType<A> i = 0; i < n; ++i, ++a, ++b) {
    ValueType<A> tmp(std::move(*a));

    AllocatorTraits<A>::destroy(allocator_a, a);
    AllocatorTraits<A>::construct(allocator_b, a, std::move(*b));

    AllocatorTraits<A>::destroy(allocator_b, b);
    AllocatorTraits<A>::construct(allocator_a, b, std::move(tmp));
  }
}

template <typename T, size_t N, typename A>
void Storage<T, N, A>::SwapInlinedElements(MemcpyPolicy, Storage* other) {
  Data tmp = data_;
  data_ = other->data_;
  other->data_ = tmp;
}

template <typename T, size_t N, typename A>
template <typename NotMemcpyPolicy>
void Storage<T, N, A>::SwapInlinedElements(NotMemcpyPolicy policy,
                                           Storage* other) {
  // Note: `destroy` needs to use pre-swap allocator while `construct` -
  // post-swap allocator. Allocators will be swapped later on outside of
  // `SwapInlinedElements`.
  Storage* small_ptr = this;
  Storage* large_ptr = other;
  if (small_ptr->GetSize() > large_ptr->GetSize()) {
    std::swap(small_ptr, large_ptr);
  }

  auto small_size = small_ptr->GetSize();
  auto diff = large_ptr->GetSize() - small_size;
  SwapN(policy, other, small_size);

  IteratorValueAdapter<A, MoveIterator<A>> move_values(
      MoveIterator<A>(large_ptr->GetInlinedData() + small_size));

  ConstructElements<A>(large_ptr->GetAllocator(),
                       small_ptr->GetInlinedData() + small_size, move_values,
                       diff);

  DestroyAdapter<A>::DestroyElements(large_ptr->GetAllocator(),
                                     large_ptr->GetInlinedData() + small_size,
                                     diff);
}

// End ignore "array-bounds"
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

}  // namespace inlined_vector_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_INLINED_VECTOR_H_

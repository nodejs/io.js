// Copyright 2018 The Abseil Authors.
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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <ostream>
#include <set>
#include <type_traits>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/raw_hash_set.h"
#include "absl/container/internal/tracked.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {
using ::testing::AnyOf;

enum AllocSpec {
  kPropagateOnCopy = 1,
  kPropagateOnMove = 2,
  kPropagateOnSwap = 4,
};

struct AllocState {
  size_t num_allocs = 0;
  std::set<void*> owned;
};

template <class T,
          int Spec = kPropagateOnCopy | kPropagateOnMove | kPropagateOnSwap>
class CheckedAlloc {
 public:
  template <class, int>
  friend class CheckedAlloc;

  using value_type = T;

  CheckedAlloc() {}
  explicit CheckedAlloc(size_t id) : id_(id) {}
  CheckedAlloc(const CheckedAlloc&) = default;
  CheckedAlloc& operator=(const CheckedAlloc&) = default;

  template <class U>
  CheckedAlloc(const CheckedAlloc<U, Spec>& that)
      : id_(that.id_), state_(that.state_) {}

  template <class U>
  struct rebind {
    using other = CheckedAlloc<U, Spec>;
  };

  using propagate_on_container_copy_assignment =
      std::integral_constant<bool, (Spec & kPropagateOnCopy) != 0>;

  using propagate_on_container_move_assignment =
      std::integral_constant<bool, (Spec & kPropagateOnMove) != 0>;

  using propagate_on_container_swap =
      std::integral_constant<bool, (Spec & kPropagateOnSwap) != 0>;

  CheckedAlloc select_on_container_copy_construction() const {
    if (Spec & kPropagateOnCopy) return *this;
    return {};
  }

  T* allocate(size_t n) {
    T* ptr = std::allocator<T>().allocate(n);
    track_alloc(ptr);
    return ptr;
  }
  void deallocate(T* ptr, size_t n) {
    memset(ptr, 0, n * sizeof(T));  // The freed memory must be unpoisoned.
    track_dealloc(ptr);
    return std::allocator<T>().deallocate(ptr, n);
  }

  friend bool operator==(const CheckedAlloc& a, const CheckedAlloc& b) {
    return a.id_ == b.id_;
  }
  friend bool operator!=(const CheckedAlloc& a, const CheckedAlloc& b) {
    return !(a == b);
  }

  size_t num_allocs() const { return state_->num_allocs; }

  void swap(CheckedAlloc& that) {
    using std::swap;
    swap(id_, that.id_);
    swap(state_, that.state_);
  }

  friend void swap(CheckedAlloc& a, CheckedAlloc& b) { a.swap(b); }

  friend std::ostream& operator<<(std::ostream& o, const CheckedAlloc& a) {
    return o << "alloc(" << a.id_ << ")";
  }

 private:
  void track_alloc(void* ptr) {
    AllocState* state = state_.get();
    ++state->num_allocs;
    if (!state->owned.insert(ptr).second)
      ADD_FAILURE() << *this << " got previously allocated memory: " << ptr;
  }
  void track_dealloc(void* ptr) {
    if (state_->owned.erase(ptr) != 1)
      ADD_FAILURE() << *this
                    << " deleting memory owned by another allocator: " << ptr;
  }

  size_t id_ = std::numeric_limits<size_t>::max();

  std::shared_ptr<AllocState> state_ = std::make_shared<AllocState>();
};

struct Identity {
  size_t operator()(int32_t v) const { return static_cast<size_t>(v); }
};

struct Policy {
  using slot_type = Tracked<int32_t>;
  using init_type = Tracked<int32_t>;
  using key_type = int32_t;

  template <class allocator_type, class... Args>
  static void construct(allocator_type* alloc, slot_type* slot,
                        Args&&... args) {
    std::allocator_traits<allocator_type>::construct(
        *alloc, slot, std::forward<Args>(args)...);
  }

  template <class allocator_type>
  static void destroy(allocator_type* alloc, slot_type* slot) {
    std::allocator_traits<allocator_type>::destroy(*alloc, slot);
  }

  template <class allocator_type>
  static void transfer(allocator_type* alloc, slot_type* new_slot,
                       slot_type* old_slot) {
    construct(alloc, new_slot, std::move(*old_slot));
    destroy(alloc, old_slot);
  }

  template <class F>
  static auto apply(F&& f, int32_t v) -> decltype(std::forward<F>(f)(v, v)) {
    return std::forward<F>(f)(v, v);
  }

  template <class F>
  static auto apply(F&& f, const slot_type& v)
      -> decltype(std::forward<F>(f)(v.val(), v)) {
    return std::forward<F>(f)(v.val(), v);
  }

  template <class F>
  static auto apply(F&& f, slot_type&& v)
      -> decltype(std::forward<F>(f)(v.val(), std::move(v))) {
    return std::forward<F>(f)(v.val(), std::move(v));
  }

  static slot_type& element(slot_type* slot) { return *slot; }

  template <class Hash>
  static constexpr HashSlotFn get_hash_slot_fn() {
    return nullptr;
  }
};

template <int Spec>
struct PropagateTest : public ::testing::Test {
  using Alloc = CheckedAlloc<Tracked<int32_t>, Spec>;

  using Table = raw_hash_set<Policy, Identity, std::equal_to<int32_t>, Alloc>;

  PropagateTest() {
    EXPECT_EQ(a1, t1.get_allocator());
    EXPECT_NE(a2, t1.get_allocator());
  }

  Alloc a1 = Alloc(1);
  Table t1 = Table(0, a1);
  Alloc a2 = Alloc(2);
};

using PropagateOnAll =
    PropagateTest<kPropagateOnCopy | kPropagateOnMove | kPropagateOnSwap>;
using NoPropagateOnCopy = PropagateTest<kPropagateOnMove | kPropagateOnSwap>;
using NoPropagateOnMove = PropagateTest<kPropagateOnCopy | kPropagateOnSwap>;

TEST_F(PropagateOnAll, Empty) { EXPECT_EQ(0, a1.num_allocs()); }

TEST_F(PropagateOnAll, InsertAllocates) {
  auto it = t1.insert(0).first;
  EXPECT_EQ(1, a1.num_allocs());
  EXPECT_EQ(0, it->num_moves());
  EXPECT_EQ(0, it->num_copies());
}

TEST_F(PropagateOnAll, InsertDecomposes) {
  auto it = t1.insert(0).first;
  EXPECT_EQ(1, a1.num_allocs());
  EXPECT_EQ(0, it->num_moves());
  EXPECT_EQ(0, it->num_copies());

  EXPECT_FALSE(t1.insert(0).second);
  EXPECT_EQ(1, a1.num_allocs());
  EXPECT_EQ(0, it->num_moves());
  EXPECT_EQ(0, it->num_copies());
}

TEST_F(PropagateOnAll, RehashMoves) {
  auto it = t1.insert(0).first;
  EXPECT_EQ(0, it->num_moves());
  t1.rehash(2 * t1.capacity());
  EXPECT_EQ(2, a1.num_allocs());
  it = t1.find(0);
  EXPECT_EQ(1, it->num_moves());
  EXPECT_EQ(0, it->num_copies());
}

TEST_F(PropagateOnAll, CopyConstructor) {
  auto it = t1.insert(0).first;
  Table u(t1);
  EXPECT_EQ(2, a1.num_allocs());
  EXPECT_EQ(0, it->num_moves());
  EXPECT_EQ(1, it->num_copies());
}

TEST_F(NoPropagateOnCopy, CopyConstructor) {
  auto it = t1.insert(0).first;
  Table u(t1);
  EXPECT_EQ(1, a1.num_allocs());
  EXPECT_EQ(1, u.get_allocator().num_allocs());
  EXPECT_EQ(0, it->num_moves());
  EXPECT_EQ(1, it->num_copies());
}

TEST_F(PropagateOnAll, CopyConstructorWithSameAlloc) {
  auto it = t1.insert(0).first;
  Table u(t1, a1);
  EXPECT_EQ(2, a1.num_allocs());
  EXPECT_EQ(0, it->num_moves());
  EXPECT_EQ(1, it->num_copies());
}

TEST_F(NoPropagateOnCopy, CopyConstructorWithSameAlloc) {
  auto it = t1.insert(0).first;
  Table u(t1, a1);
  EXPECT_EQ(2, a1.num_allocs());
  EXPECT_EQ(0, it->num_moves());
  EXPECT_EQ(1, it->num_copies());
}

TEST_F(PropagateOnAll, CopyConstructorWithDifferentAlloc) {
  auto it = t1.insert(0).first;
  Table u(t1, a2);
  EXPECT_EQ(a2, u.get_allocator());
  EXPECT_EQ(1, a1.num_allocs());
  EXPECT_EQ(1, a2.num_allocs());
  EXPECT_EQ(0, it->num_moves());
  EXPECT_EQ(1, it->num_copies());
}

TEST_F(NoPropagateOnCopy, CopyConstructorWithDifferentAlloc) {
  auto it = t1.insert(0).first;
  Table u(t1, a2);
  EXPECT_EQ(a2, u.get_allocator());
  EXPECT_EQ(1, a1.num_allocs());
  EXPECT_EQ(1, a2.num_allocs());
  EXPECT_EQ(0, it->num_moves());
  EXPECT_EQ(1, it->num_copies());
}

TEST_F(PropagateOnAll, MoveConstructor) {
  t1.insert(0);
  Table u(std::move(t1));
  auto it = u.begin();
  EXPECT_THAT(a1.num_allocs(), AnyOf(1, 2));
  EXPECT_THAT(it->num_moves(), AnyOf(0, 1));
  EXPECT_EQ(0, it->num_copies());
}

TEST_F(NoPropagateOnMove, MoveConstructor) {
  t1.insert(0);
  Table u(std::move(t1));
  auto it = u.begin();
  EXPECT_THAT(a1.num_allocs(), AnyOf(1, 2));
  EXPECT_THAT(it->num_moves(), AnyOf(0, 1));
  EXPECT_EQ(0, it->num_copies());
}

TEST_F(PropagateOnAll, MoveConstructorWithSameAlloc) {
  t1.insert(0);
  Table u(std::move(t1), a1);
  auto it = u.begin();
  EXPECT_THAT(a1.num_allocs(), AnyOf(1, 2));
  EXPECT_THAT(it->num_moves(), AnyOf(0, 1));
  EXPECT_EQ(0, it->num_copies());
}

TEST_F(NoPropagateOnMove, MoveConstructorWithSameAlloc) {
  t1.insert(0);
  Table u(std::move(t1), a1);
  auto it = u.begin();
  EXPECT_THAT(a1.num_allocs(), AnyOf(1, 2));
  EXPECT_THAT(it->num_moves(), AnyOf(0, 1));
  EXPECT_EQ(0, it->num_copies());
}

TEST_F(PropagateOnAll, MoveConstructorWithDifferentAlloc) {
  auto it = t1.insert(0).first;
  Table u(std::move(t1), a2);
  it = u.find(0);
  EXPECT_EQ(a2, u.get_allocator());
  EXPECT_EQ(1, a1.num_allocs());
  EXPECT_THAT(a2.num_allocs(), AnyOf(1, 2));
  EXPECT_THAT(it->num_moves(), AnyOf(1, 2));
  EXPECT_EQ(0, it->num_copies());
}

TEST_F(NoPropagateOnMove, MoveConstructorWithDifferentAlloc) {
  auto it = t1.insert(0).first;
  Table u(std::move(t1), a2);
  it = u.find(0);
  EXPECT_EQ(a2, u.get_allocator());
  EXPECT_EQ(1, a1.num_allocs());
  EXPECT_THAT(a2.num_allocs(), AnyOf(1, 2));
  EXPECT_THAT(it->num_moves(), AnyOf(1, 2));
  EXPECT_EQ(0, it->num_copies());
}

TEST_F(PropagateOnAll, CopyAssignmentWithSameAlloc) {
  auto it = t1.insert(0).first;
  Table u(0, a1);
  u = t1;
  EXPECT_THAT(a1.num_allocs(), AnyOf(2, 3));
  EXPECT_THAT(it->num_moves(), AnyOf(0, 1));
  EXPECT_EQ(1, it->num_copies());
}

TEST_F(NoPropagateOnCopy, CopyAssignmentWithSameAlloc) {
  auto it = t1.insert(0).first;
  Table u(0, a1);
  u = t1;
  EXPECT_THAT(a1.num_allocs(), AnyOf(2, 3));
  EXPECT_THAT(it->num_moves(), AnyOf(0, 1));
  EXPECT_EQ(1, it->num_copies());
}

TEST_F(PropagateOnAll, CopyAssignmentWithDifferentAlloc) {
  auto it = t1.insert(0).first;
  Table u(0, a2);
  u = t1;
  EXPECT_EQ(a1, u.get_allocator());
  EXPECT_THAT(a1.num_allocs(), AnyOf(2, 3));
  EXPECT_EQ(0, a2.num_allocs());
  EXPECT_THAT(it->num_moves(), AnyOf(0, 1));
  EXPECT_EQ(1, it->num_copies());
}

TEST_F(NoPropagateOnCopy, CopyAssignmentWithDifferentAlloc) {
  auto it = t1.insert(0).first;
  Table u(0, a2);
  u = t1;
  EXPECT_EQ(a2, u.get_allocator());
  EXPECT_EQ(1, a1.num_allocs());
  EXPECT_THAT(a2.num_allocs(), AnyOf(1, 2));
  EXPECT_THAT(it->num_moves(), AnyOf(0, 1));
  EXPECT_EQ(1, it->num_copies());
}

TEST_F(PropagateOnAll, MoveAssignmentWithSameAlloc) {
  t1.insert(0);
  Table u(0, a1);
  u = std::move(t1);
  auto it = u.begin();
  EXPECT_EQ(a1, u.get_allocator());
  EXPECT_THAT(a1.num_allocs(), AnyOf(1, 2));
  EXPECT_THAT(it->num_moves(), AnyOf(0, 1));
  EXPECT_EQ(0, it->num_copies());
}

TEST_F(NoPropagateOnMove, MoveAssignmentWithSameAlloc) {
  t1.insert(0);
  Table u(0, a1);
  u = std::move(t1);
  auto it = u.begin();
  EXPECT_EQ(a1, u.get_allocator());
  EXPECT_THAT(a1.num_allocs(), AnyOf(1, 2));
  EXPECT_THAT(it->num_moves(), AnyOf(0, 1));
  EXPECT_EQ(0, it->num_copies());
}

TEST_F(PropagateOnAll, MoveAssignmentWithDifferentAlloc) {
  t1.insert(0);
  Table u(0, a2);
  u = std::move(t1);
  auto it = u.begin();
  EXPECT_EQ(a1, u.get_allocator());
  EXPECT_THAT(a1.num_allocs(), AnyOf(1, 2));
  EXPECT_EQ(0, a2.num_allocs());
  EXPECT_THAT(it->num_moves(), AnyOf(0, 1));
  EXPECT_EQ(0, it->num_copies());
}

TEST_F(NoPropagateOnMove, MoveAssignmentWithDifferentAlloc) {
  t1.insert(0);
  Table u(0, a2);
  u = std::move(t1);
  auto it = u.find(0);
  EXPECT_EQ(a2, u.get_allocator());
  EXPECT_EQ(1, a1.num_allocs());
  EXPECT_THAT(a2.num_allocs(), AnyOf(1, 2));
  EXPECT_THAT(it->num_moves(), AnyOf(1, 2));
  EXPECT_EQ(0, it->num_copies());
}

TEST_F(PropagateOnAll, Swap) {
  auto it = t1.insert(0).first;
  Table u(0, a2);
  u.swap(t1);
  EXPECT_EQ(a1, u.get_allocator());
  EXPECT_EQ(a2, t1.get_allocator());
  EXPECT_EQ(1, a1.num_allocs());
  EXPECT_EQ(0, a2.num_allocs());
  EXPECT_EQ(0, it->num_moves());
  EXPECT_EQ(0, it->num_copies());
}

// This allocator is similar to std::pmr::polymorphic_allocator.
// Note the disabled assignment.
template <class T>
class PAlloc {
  template <class>
  friend class PAlloc;

 public:
  // types
  using value_type = T;

  PAlloc() noexcept = default;
  explicit PAlloc(size_t id) noexcept : id_(id) {}
  PAlloc(const PAlloc&) noexcept = default;
  PAlloc& operator=(const PAlloc&) noexcept = delete;

  template <class U>
  PAlloc(const PAlloc<U>& that) noexcept : id_(that.id_) {}  // NOLINT

  template <class U>
  struct rebind {
    using other = PAlloc<U>;
  };

  constexpr PAlloc select_on_container_copy_construction() const { return {}; }

  // public member functions
  T* allocate(size_t) { return new T; }
  void deallocate(T* p, size_t) { delete p; }

  friend bool operator==(const PAlloc& a, const PAlloc& b) {
    return a.id_ == b.id_;
  }
  friend bool operator!=(const PAlloc& a, const PAlloc& b) { return !(a == b); }

 private:
  size_t id_ = std::numeric_limits<size_t>::max();
};

TEST(NoPropagateDeletedAssignment, CopyConstruct) {
  using PA = PAlloc<char>;
  using Table = raw_hash_set<Policy, Identity, std::equal_to<int32_t>, PA>;

  Table t1(PA{1}), t2(t1);
  EXPECT_EQ(t1.get_allocator(), PA(1));
  EXPECT_EQ(t2.get_allocator(), PA());
}

TEST(NoPropagateDeletedAssignment, CopyAssignment) {
  using PA = PAlloc<char>;
  using Table = raw_hash_set<Policy, Identity, std::equal_to<int32_t>, PA>;

  Table t1(PA{1}), t2(PA{2});
  t1 = t2;
  EXPECT_EQ(t1.get_allocator(), PA(1));
  EXPECT_EQ(t2.get_allocator(), PA(2));
}

TEST(NoPropagateDeletedAssignment, MoveAssignment) {
  using PA = PAlloc<char>;
  using Table = raw_hash_set<Policy, Identity, std::equal_to<int32_t>, PA>;

  Table t1(PA{1}), t2(PA{2});
  t1 = std::move(t2);
  EXPECT_EQ(t1.get_allocator(), PA(1));
}

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

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

#include "absl/container/btree_test.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/algorithm/container.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/macros.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/internal/test_allocator.h"
#include "absl/container/internal/test_instance_tracker.h"
#include "absl/flags/flag.h"
#include "absl/hash/hash_testing.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/compare.h"
#include "absl/types/optional.h"

ABSL_FLAG(int, test_values, 10000, "The number of values to use for tests");

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

using ::absl::test_internal::CopyableMovableInstance;
using ::absl::test_internal::InstanceTracker;
using ::absl::test_internal::MovableOnlyInstance;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Pair;
using ::testing::SizeIs;

template <typename T, typename U>
void CheckPairEquals(const T &x, const U &y) {
  ABSL_INTERNAL_CHECK(x == y, "Values are unequal.");
}

template <typename T, typename U, typename V, typename W>
void CheckPairEquals(const std::pair<T, U> &x, const std::pair<V, W> &y) {
  CheckPairEquals(x.first, y.first);
  CheckPairEquals(x.second, y.second);
}
}  // namespace

// The base class for a sorted associative container checker. TreeType is the
// container type to check and CheckerType is the container type to check
// against. TreeType is expected to be btree_{set,map,multiset,multimap} and
// CheckerType is expected to be {set,map,multiset,multimap}.
template <typename TreeType, typename CheckerType>
class base_checker {
 public:
  using key_type = typename TreeType::key_type;
  using value_type = typename TreeType::value_type;
  using key_compare = typename TreeType::key_compare;
  using pointer = typename TreeType::pointer;
  using const_pointer = typename TreeType::const_pointer;
  using reference = typename TreeType::reference;
  using const_reference = typename TreeType::const_reference;
  using size_type = typename TreeType::size_type;
  using difference_type = typename TreeType::difference_type;
  using iterator = typename TreeType::iterator;
  using const_iterator = typename TreeType::const_iterator;
  using reverse_iterator = typename TreeType::reverse_iterator;
  using const_reverse_iterator = typename TreeType::const_reverse_iterator;

 public:
  base_checker() : const_tree_(tree_) {}
  base_checker(const base_checker &other)
      : tree_(other.tree_), const_tree_(tree_), checker_(other.checker_) {}
  template <typename InputIterator>
  base_checker(InputIterator b, InputIterator e)
      : tree_(b, e), const_tree_(tree_), checker_(b, e) {}

  iterator begin() { return tree_.begin(); }
  const_iterator begin() const { return tree_.begin(); }
  iterator end() { return tree_.end(); }
  const_iterator end() const { return tree_.end(); }
  reverse_iterator rbegin() { return tree_.rbegin(); }
  const_reverse_iterator rbegin() const { return tree_.rbegin(); }
  reverse_iterator rend() { return tree_.rend(); }
  const_reverse_iterator rend() const { return tree_.rend(); }

  template <typename IterType, typename CheckerIterType>
  IterType iter_check(IterType tree_iter, CheckerIterType checker_iter) const {
    if (tree_iter == tree_.end()) {
      ABSL_INTERNAL_CHECK(checker_iter == checker_.end(),
                          "Checker iterator not at end.");
    } else {
      CheckPairEquals(*tree_iter, *checker_iter);
    }
    return tree_iter;
  }
  template <typename IterType, typename CheckerIterType>
  IterType riter_check(IterType tree_iter, CheckerIterType checker_iter) const {
    if (tree_iter == tree_.rend()) {
      ABSL_INTERNAL_CHECK(checker_iter == checker_.rend(),
                          "Checker iterator not at rend.");
    } else {
      CheckPairEquals(*tree_iter, *checker_iter);
    }
    return tree_iter;
  }
  void value_check(const value_type &v) {
    typename KeyOfValue<typename TreeType::key_type,
                        typename TreeType::value_type>::type key_of_value;
    const key_type &key = key_of_value(v);
    CheckPairEquals(*find(key), v);
    lower_bound(key);
    upper_bound(key);
    equal_range(key);
    contains(key);
    count(key);
  }
  void erase_check(const key_type &key) {
    EXPECT_FALSE(tree_.contains(key));
    EXPECT_EQ(tree_.find(key), const_tree_.end());
    EXPECT_FALSE(const_tree_.contains(key));
    EXPECT_EQ(const_tree_.find(key), tree_.end());
    EXPECT_EQ(tree_.equal_range(key).first,
              const_tree_.equal_range(key).second);
  }

  iterator lower_bound(const key_type &key) {
    return iter_check(tree_.lower_bound(key), checker_.lower_bound(key));
  }
  const_iterator lower_bound(const key_type &key) const {
    return iter_check(tree_.lower_bound(key), checker_.lower_bound(key));
  }
  iterator upper_bound(const key_type &key) {
    return iter_check(tree_.upper_bound(key), checker_.upper_bound(key));
  }
  const_iterator upper_bound(const key_type &key) const {
    return iter_check(tree_.upper_bound(key), checker_.upper_bound(key));
  }
  std::pair<iterator, iterator> equal_range(const key_type &key) {
    std::pair<typename CheckerType::iterator, typename CheckerType::iterator>
        checker_res = checker_.equal_range(key);
    std::pair<iterator, iterator> tree_res = tree_.equal_range(key);
    iter_check(tree_res.first, checker_res.first);
    iter_check(tree_res.second, checker_res.second);
    return tree_res;
  }
  std::pair<const_iterator, const_iterator> equal_range(
      const key_type &key) const {
    std::pair<typename CheckerType::const_iterator,
              typename CheckerType::const_iterator>
        checker_res = checker_.equal_range(key);
    std::pair<const_iterator, const_iterator> tree_res = tree_.equal_range(key);
    iter_check(tree_res.first, checker_res.first);
    iter_check(tree_res.second, checker_res.second);
    return tree_res;
  }
  iterator find(const key_type &key) {
    return iter_check(tree_.find(key), checker_.find(key));
  }
  const_iterator find(const key_type &key) const {
    return iter_check(tree_.find(key), checker_.find(key));
  }
  bool contains(const key_type &key) const { return find(key) != end(); }
  size_type count(const key_type &key) const {
    size_type res = checker_.count(key);
    EXPECT_EQ(res, tree_.count(key));
    return res;
  }

  base_checker &operator=(const base_checker &other) {
    tree_ = other.tree_;
    checker_ = other.checker_;
    return *this;
  }

  int erase(const key_type &key) {
    int size = tree_.size();
    int res = checker_.erase(key);
    EXPECT_EQ(res, tree_.count(key));
    EXPECT_EQ(res, tree_.erase(key));
    EXPECT_EQ(tree_.count(key), 0);
    EXPECT_EQ(tree_.size(), size - res);
    erase_check(key);
    return res;
  }
  iterator erase(iterator iter) {
    key_type key = iter.key();
    int size = tree_.size();
    int count = tree_.count(key);
    auto checker_iter = checker_.lower_bound(key);
    for (iterator tmp(tree_.lower_bound(key)); tmp != iter; ++tmp) {
      ++checker_iter;
    }
    auto checker_next = checker_iter;
    ++checker_next;
    checker_.erase(checker_iter);
    iter = tree_.erase(iter);
    EXPECT_EQ(tree_.size(), checker_.size());
    EXPECT_EQ(tree_.size(), size - 1);
    EXPECT_EQ(tree_.count(key), count - 1);
    if (count == 1) {
      erase_check(key);
    }
    return iter_check(iter, checker_next);
  }

  void erase(iterator begin, iterator end) {
    int size = tree_.size();
    int count = std::distance(begin, end);
    auto checker_begin = checker_.lower_bound(begin.key());
    for (iterator tmp(tree_.lower_bound(begin.key())); tmp != begin; ++tmp) {
      ++checker_begin;
    }
    auto checker_end =
        end == tree_.end() ? checker_.end() : checker_.lower_bound(end.key());
    if (end != tree_.end()) {
      for (iterator tmp(tree_.lower_bound(end.key())); tmp != end; ++tmp) {
        ++checker_end;
      }
    }
    const auto checker_ret = checker_.erase(checker_begin, checker_end);
    const auto tree_ret = tree_.erase(begin, end);
    EXPECT_EQ(std::distance(checker_.begin(), checker_ret),
              std::distance(tree_.begin(), tree_ret));
    EXPECT_EQ(tree_.size(), checker_.size());
    EXPECT_EQ(tree_.size(), size - count);
  }

  void clear() {
    tree_.clear();
    checker_.clear();
  }
  void swap(base_checker &other) {
    tree_.swap(other.tree_);
    checker_.swap(other.checker_);
  }

  void verify() const {
    tree_.verify();
    EXPECT_EQ(tree_.size(), checker_.size());

    // Move through the forward iterators using increment.
    auto checker_iter = checker_.begin();
    const_iterator tree_iter(tree_.begin());
    for (; tree_iter != tree_.end(); ++tree_iter, ++checker_iter) {
      CheckPairEquals(*tree_iter, *checker_iter);
    }

    // Move through the forward iterators using decrement.
    for (int n = tree_.size() - 1; n >= 0; --n) {
      iter_check(tree_iter, checker_iter);
      --tree_iter;
      --checker_iter;
    }
    EXPECT_EQ(tree_iter, tree_.begin());
    EXPECT_EQ(checker_iter, checker_.begin());

    // Move through the reverse iterators using increment.
    auto checker_riter = checker_.rbegin();
    const_reverse_iterator tree_riter(tree_.rbegin());
    for (; tree_riter != tree_.rend(); ++tree_riter, ++checker_riter) {
      CheckPairEquals(*tree_riter, *checker_riter);
    }

    // Move through the reverse iterators using decrement.
    for (int n = tree_.size() - 1; n >= 0; --n) {
      riter_check(tree_riter, checker_riter);
      --tree_riter;
      --checker_riter;
    }
    EXPECT_EQ(tree_riter, tree_.rbegin());
    EXPECT_EQ(checker_riter, checker_.rbegin());
  }

  const TreeType &tree() const { return tree_; }

  size_type size() const {
    EXPECT_EQ(tree_.size(), checker_.size());
    return tree_.size();
  }
  size_type max_size() const { return tree_.max_size(); }
  bool empty() const {
    EXPECT_EQ(tree_.empty(), checker_.empty());
    return tree_.empty();
  }

 protected:
  TreeType tree_;
  const TreeType &const_tree_;
  CheckerType checker_;
};

namespace {
// A checker for unique sorted associative containers. TreeType is expected to
// be btree_{set,map} and CheckerType is expected to be {set,map}.
template <typename TreeType, typename CheckerType>
class unique_checker : public base_checker<TreeType, CheckerType> {
  using super_type = base_checker<TreeType, CheckerType>;

 public:
  using iterator = typename super_type::iterator;
  using value_type = typename super_type::value_type;

 public:
  unique_checker() : super_type() {}
  unique_checker(const unique_checker &other) : super_type(other) {}
  template <class InputIterator>
  unique_checker(InputIterator b, InputIterator e) : super_type(b, e) {}
  unique_checker &operator=(const unique_checker &) = default;

  // Insertion routines.
  std::pair<iterator, bool> insert(const value_type &v) {
    int size = this->tree_.size();
    std::pair<typename CheckerType::iterator, bool> checker_res =
        this->checker_.insert(v);
    std::pair<iterator, bool> tree_res = this->tree_.insert(v);
    CheckPairEquals(*tree_res.first, *checker_res.first);
    EXPECT_EQ(tree_res.second, checker_res.second);
    EXPECT_EQ(this->tree_.size(), this->checker_.size());
    EXPECT_EQ(this->tree_.size(), size + tree_res.second);
    return tree_res;
  }
  iterator insert(iterator position, const value_type &v) {
    int size = this->tree_.size();
    std::pair<typename CheckerType::iterator, bool> checker_res =
        this->checker_.insert(v);
    iterator tree_res = this->tree_.insert(position, v);
    CheckPairEquals(*tree_res, *checker_res.first);
    EXPECT_EQ(this->tree_.size(), this->checker_.size());
    EXPECT_EQ(this->tree_.size(), size + checker_res.second);
    return tree_res;
  }
  template <typename InputIterator>
  void insert(InputIterator b, InputIterator e) {
    for (; b != e; ++b) {
      insert(*b);
    }
  }
};

// A checker for multiple sorted associative containers. TreeType is expected
// to be btree_{multiset,multimap} and CheckerType is expected to be
// {multiset,multimap}.
template <typename TreeType, typename CheckerType>
class multi_checker : public base_checker<TreeType, CheckerType> {
  using super_type = base_checker<TreeType, CheckerType>;

 public:
  using iterator = typename super_type::iterator;
  using value_type = typename super_type::value_type;

 public:
  multi_checker() : super_type() {}
  multi_checker(const multi_checker &other) : super_type(other) {}
  template <class InputIterator>
  multi_checker(InputIterator b, InputIterator e) : super_type(b, e) {}
  multi_checker &operator=(const multi_checker &) = default;

  // Insertion routines.
  iterator insert(const value_type &v) {
    int size = this->tree_.size();
    auto checker_res = this->checker_.insert(v);
    iterator tree_res = this->tree_.insert(v);
    CheckPairEquals(*tree_res, *checker_res);
    EXPECT_EQ(this->tree_.size(), this->checker_.size());
    EXPECT_EQ(this->tree_.size(), size + 1);
    return tree_res;
  }
  iterator insert(iterator position, const value_type &v) {
    int size = this->tree_.size();
    auto checker_res = this->checker_.insert(v);
    iterator tree_res = this->tree_.insert(position, v);
    CheckPairEquals(*tree_res, *checker_res);
    EXPECT_EQ(this->tree_.size(), this->checker_.size());
    EXPECT_EQ(this->tree_.size(), size + 1);
    return tree_res;
  }
  template <typename InputIterator>
  void insert(InputIterator b, InputIterator e) {
    for (; b != e; ++b) {
      insert(*b);
    }
  }
};

template <typename T, typename V>
void DoTest(const char *name, T *b, const std::vector<V> &values) {
  typename KeyOfValue<typename T::key_type, V>::type key_of_value;

  T &mutable_b = *b;
  const T &const_b = *b;

  // Test insert.
  for (int i = 0; i < values.size(); ++i) {
    mutable_b.insert(values[i]);
    mutable_b.value_check(values[i]);
  }
  ASSERT_EQ(mutable_b.size(), values.size());

  const_b.verify();

  // Test copy constructor.
  T b_copy(const_b);
  EXPECT_EQ(b_copy.size(), const_b.size());
  for (int i = 0; i < values.size(); ++i) {
    CheckPairEquals(*b_copy.find(key_of_value(values[i])), values[i]);
  }

  // Test range constructor.
  T b_range(const_b.begin(), const_b.end());
  EXPECT_EQ(b_range.size(), const_b.size());
  for (int i = 0; i < values.size(); ++i) {
    CheckPairEquals(*b_range.find(key_of_value(values[i])), values[i]);
  }

  // Test range insertion for values that already exist.
  b_range.insert(b_copy.begin(), b_copy.end());
  b_range.verify();

  // Test range insertion for new values.
  b_range.clear();
  b_range.insert(b_copy.begin(), b_copy.end());
  EXPECT_EQ(b_range.size(), b_copy.size());
  for (int i = 0; i < values.size(); ++i) {
    CheckPairEquals(*b_range.find(key_of_value(values[i])), values[i]);
  }

  // Test assignment to self. Nothing should change.
  b_range.operator=(b_range);
  EXPECT_EQ(b_range.size(), b_copy.size());

  // Test assignment of new values.
  b_range.clear();
  b_range = b_copy;
  EXPECT_EQ(b_range.size(), b_copy.size());

  // Test swap.
  b_range.clear();
  b_range.swap(b_copy);
  EXPECT_EQ(b_copy.size(), 0);
  EXPECT_EQ(b_range.size(), const_b.size());
  for (int i = 0; i < values.size(); ++i) {
    CheckPairEquals(*b_range.find(key_of_value(values[i])), values[i]);
  }
  b_range.swap(b_copy);

  // Test non-member function swap.
  swap(b_range, b_copy);
  EXPECT_EQ(b_copy.size(), 0);
  EXPECT_EQ(b_range.size(), const_b.size());
  for (int i = 0; i < values.size(); ++i) {
    CheckPairEquals(*b_range.find(key_of_value(values[i])), values[i]);
  }
  swap(b_range, b_copy);

  // Test erase via values.
  for (int i = 0; i < values.size(); ++i) {
    mutable_b.erase(key_of_value(values[i]));
    // Erasing a non-existent key should have no effect.
    ASSERT_EQ(mutable_b.erase(key_of_value(values[i])), 0);
  }

  const_b.verify();
  EXPECT_EQ(const_b.size(), 0);

  // Test erase via iterators.
  mutable_b = b_copy;
  for (int i = 0; i < values.size(); ++i) {
    mutable_b.erase(mutable_b.find(key_of_value(values[i])));
  }

  const_b.verify();
  EXPECT_EQ(const_b.size(), 0);

  // Test insert with hint.
  for (int i = 0; i < values.size(); i++) {
    mutable_b.insert(mutable_b.upper_bound(key_of_value(values[i])), values[i]);
  }

  const_b.verify();

  // Test range erase.
  mutable_b.erase(mutable_b.begin(), mutable_b.end());
  EXPECT_EQ(mutable_b.size(), 0);
  const_b.verify();

  // First half.
  mutable_b = b_copy;
  typename T::iterator mutable_iter_end = mutable_b.begin();
  for (int i = 0; i < values.size() / 2; ++i) ++mutable_iter_end;
  mutable_b.erase(mutable_b.begin(), mutable_iter_end);
  EXPECT_EQ(mutable_b.size(), values.size() - values.size() / 2);
  const_b.verify();

  // Second half.
  mutable_b = b_copy;
  typename T::iterator mutable_iter_begin = mutable_b.begin();
  for (int i = 0; i < values.size() / 2; ++i) ++mutable_iter_begin;
  mutable_b.erase(mutable_iter_begin, mutable_b.end());
  EXPECT_EQ(mutable_b.size(), values.size() / 2);
  const_b.verify();

  // Second quarter.
  mutable_b = b_copy;
  mutable_iter_begin = mutable_b.begin();
  for (int i = 0; i < values.size() / 4; ++i) ++mutable_iter_begin;
  mutable_iter_end = mutable_iter_begin;
  for (int i = 0; i < values.size() / 4; ++i) ++mutable_iter_end;
  mutable_b.erase(mutable_iter_begin, mutable_iter_end);
  EXPECT_EQ(mutable_b.size(), values.size() - values.size() / 4);
  const_b.verify();

  mutable_b.clear();
}

template <typename T>
void ConstTest() {
  using value_type = typename T::value_type;
  typename KeyOfValue<typename T::key_type, value_type>::type key_of_value;

  T mutable_b;
  const T &const_b = mutable_b;

  // Insert a single value into the container and test looking it up.
  value_type value = Generator<value_type>(2)(2);
  mutable_b.insert(value);
  EXPECT_TRUE(mutable_b.contains(key_of_value(value)));
  EXPECT_NE(mutable_b.find(key_of_value(value)), const_b.end());
  EXPECT_TRUE(const_b.contains(key_of_value(value)));
  EXPECT_NE(const_b.find(key_of_value(value)), mutable_b.end());
  EXPECT_EQ(*const_b.lower_bound(key_of_value(value)), value);
  EXPECT_EQ(const_b.upper_bound(key_of_value(value)), const_b.end());
  EXPECT_EQ(*const_b.equal_range(key_of_value(value)).first, value);

  // We can only create a non-const iterator from a non-const container.
  typename T::iterator mutable_iter(mutable_b.begin());
  EXPECT_EQ(mutable_iter, const_b.begin());
  EXPECT_NE(mutable_iter, const_b.end());
  EXPECT_EQ(const_b.begin(), mutable_iter);
  EXPECT_NE(const_b.end(), mutable_iter);
  typename T::reverse_iterator mutable_riter(mutable_b.rbegin());
  EXPECT_EQ(mutable_riter, const_b.rbegin());
  EXPECT_NE(mutable_riter, const_b.rend());
  EXPECT_EQ(const_b.rbegin(), mutable_riter);
  EXPECT_NE(const_b.rend(), mutable_riter);

  // We can create a const iterator from a non-const iterator.
  typename T::const_iterator const_iter(mutable_iter);
  EXPECT_EQ(const_iter, mutable_b.begin());
  EXPECT_NE(const_iter, mutable_b.end());
  EXPECT_EQ(mutable_b.begin(), const_iter);
  EXPECT_NE(mutable_b.end(), const_iter);
  typename T::const_reverse_iterator const_riter(mutable_riter);
  EXPECT_EQ(const_riter, mutable_b.rbegin());
  EXPECT_NE(const_riter, mutable_b.rend());
  EXPECT_EQ(mutable_b.rbegin(), const_riter);
  EXPECT_NE(mutable_b.rend(), const_riter);

  // Make sure various methods can be invoked on a const container.
  const_b.verify();
  ASSERT_TRUE(!const_b.empty());
  EXPECT_EQ(const_b.size(), 1);
  EXPECT_GT(const_b.max_size(), 0);
  EXPECT_TRUE(const_b.contains(key_of_value(value)));
  EXPECT_EQ(const_b.count(key_of_value(value)), 1);
}

template <typename T, typename C>
void BtreeTest() {
  ConstTest<T>();

  using V = typename remove_pair_const<typename T::value_type>::type;
  const std::vector<V> random_values = GenerateValuesWithSeed<V>(
      absl::GetFlag(FLAGS_test_values), 4 * absl::GetFlag(FLAGS_test_values),
      GTEST_FLAG_GET(random_seed));

  unique_checker<T, C> container;

  // Test key insertion/deletion in sorted order.
  std::vector<V> sorted_values(random_values);
  std::sort(sorted_values.begin(), sorted_values.end());
  DoTest("sorted:    ", &container, sorted_values);

  // Test key insertion/deletion in reverse sorted order.
  std::reverse(sorted_values.begin(), sorted_values.end());
  DoTest("rsorted:   ", &container, sorted_values);

  // Test key insertion/deletion in random order.
  DoTest("random:    ", &container, random_values);
}

template <typename T, typename C>
void BtreeMultiTest() {
  ConstTest<T>();

  using V = typename remove_pair_const<typename T::value_type>::type;
  const std::vector<V> random_values = GenerateValuesWithSeed<V>(
      absl::GetFlag(FLAGS_test_values), 4 * absl::GetFlag(FLAGS_test_values),
      GTEST_FLAG_GET(random_seed));

  multi_checker<T, C> container;

  // Test keys in sorted order.
  std::vector<V> sorted_values(random_values);
  std::sort(sorted_values.begin(), sorted_values.end());
  DoTest("sorted:    ", &container, sorted_values);

  // Test keys in reverse sorted order.
  std::reverse(sorted_values.begin(), sorted_values.end());
  DoTest("rsorted:   ", &container, sorted_values);

  // Test keys in random order.
  DoTest("random:    ", &container, random_values);

  // Test keys in random order w/ duplicates.
  std::vector<V> duplicate_values(random_values);
  duplicate_values.insert(duplicate_values.end(), random_values.begin(),
                          random_values.end());
  DoTest("duplicates:", &container, duplicate_values);

  // Test all identical keys.
  std::vector<V> identical_values(100);
  std::fill(identical_values.begin(), identical_values.end(),
            Generator<V>(2)(2));
  DoTest("identical: ", &container, identical_values);
}

template <typename T>
void BtreeMapTest() {
  using value_type = typename T::value_type;
  using mapped_type = typename T::mapped_type;

  mapped_type m = Generator<mapped_type>(0)(0);
  (void)m;

  T b;

  // Verify we can insert using operator[].
  for (int i = 0; i < 1000; i++) {
    value_type v = Generator<value_type>(1000)(i);
    b[v.first] = v.second;
  }
  EXPECT_EQ(b.size(), 1000);

  // Test whether we can use the "->" operator on iterators and
  // reverse_iterators. This stresses the btree_map_params::pair_pointer
  // mechanism.
  EXPECT_EQ(b.begin()->first, Generator<value_type>(1000)(0).first);
  EXPECT_EQ(b.begin()->second, Generator<value_type>(1000)(0).second);
  EXPECT_EQ(b.rbegin()->first, Generator<value_type>(1000)(999).first);
  EXPECT_EQ(b.rbegin()->second, Generator<value_type>(1000)(999).second);
}

template <typename T>
void BtreeMultiMapTest() {
  using mapped_type = typename T::mapped_type;
  mapped_type m = Generator<mapped_type>(0)(0);
  (void)m;
}

template <typename K, int N = 256>
void SetTest() {
  EXPECT_EQ(
      sizeof(absl::btree_set<K>),
      2 * sizeof(void *) + sizeof(typename absl::btree_set<K>::size_type));
  using BtreeSet = absl::btree_set<K>;
  BtreeTest<BtreeSet, std::set<K>>();
}

template <typename K, int N = 256>
void MapTest() {
  EXPECT_EQ(
      sizeof(absl::btree_map<K, K>),
      2 * sizeof(void *) + sizeof(typename absl::btree_map<K, K>::size_type));
  using BtreeMap = absl::btree_map<K, K>;
  BtreeTest<BtreeMap, std::map<K, K>>();
  BtreeMapTest<BtreeMap>();
}

TEST(Btree, set_int32) { SetTest<int32_t>(); }
TEST(Btree, set_string) { SetTest<std::string>(); }
TEST(Btree, set_cord) { SetTest<absl::Cord>(); }
TEST(Btree, map_int32) { MapTest<int32_t>(); }
TEST(Btree, map_string) { MapTest<std::string>(); }
TEST(Btree, map_cord) { MapTest<absl::Cord>(); }

template <typename K, int N = 256>
void MultiSetTest() {
  EXPECT_EQ(
      sizeof(absl::btree_multiset<K>),
      2 * sizeof(void *) + sizeof(typename absl::btree_multiset<K>::size_type));
  using BtreeMSet = absl::btree_multiset<K>;
  BtreeMultiTest<BtreeMSet, std::multiset<K>>();
}

template <typename K, int N = 256>
void MultiMapTest() {
  EXPECT_EQ(sizeof(absl::btree_multimap<K, K>),
            2 * sizeof(void *) +
                sizeof(typename absl::btree_multimap<K, K>::size_type));
  using BtreeMMap = absl::btree_multimap<K, K>;
  BtreeMultiTest<BtreeMMap, std::multimap<K, K>>();
  BtreeMultiMapTest<BtreeMMap>();
}

TEST(Btree, multiset_int32) { MultiSetTest<int32_t>(); }
TEST(Btree, multiset_string) { MultiSetTest<std::string>(); }
TEST(Btree, multiset_cord) { MultiSetTest<absl::Cord>(); }
TEST(Btree, multimap_int32) { MultiMapTest<int32_t>(); }
TEST(Btree, multimap_string) { MultiMapTest<std::string>(); }
TEST(Btree, multimap_cord) { MultiMapTest<absl::Cord>(); }

struct CompareIntToString {
  bool operator()(const std::string &a, const std::string &b) const {
    return a < b;
  }
  bool operator()(const std::string &a, int b) const {
    return a < absl::StrCat(b);
  }
  bool operator()(int a, const std::string &b) const {
    return absl::StrCat(a) < b;
  }
  using is_transparent = void;
};

struct NonTransparentCompare {
  template <typename T, typename U>
  bool operator()(const T &t, const U &u) const {
    // Treating all comparators as transparent can cause inefficiencies (see
    // N3657 C++ proposal). Test that for comparators without 'is_transparent'
    // alias (like this one), we do not attempt heterogeneous lookup.
    EXPECT_TRUE((std::is_same<T, U>()));
    return t < u;
  }
};

template <typename T>
bool CanEraseWithEmptyBrace(T t, decltype(t.erase({})) *) {
  return true;
}

template <typename T>
bool CanEraseWithEmptyBrace(T, ...) {
  return false;
}

template <typename T>
void TestHeterogeneous(T table) {
  auto lb = table.lower_bound("3");
  EXPECT_EQ(lb, table.lower_bound(3));
  EXPECT_NE(lb, table.lower_bound(4));
  EXPECT_EQ(lb, table.lower_bound({"3"}));
  EXPECT_NE(lb, table.lower_bound({}));

  auto ub = table.upper_bound("3");
  EXPECT_EQ(ub, table.upper_bound(3));
  EXPECT_NE(ub, table.upper_bound(5));
  EXPECT_EQ(ub, table.upper_bound({"3"}));
  EXPECT_NE(ub, table.upper_bound({}));

  auto er = table.equal_range("3");
  EXPECT_EQ(er, table.equal_range(3));
  EXPECT_NE(er, table.equal_range(4));
  EXPECT_EQ(er, table.equal_range({"3"}));
  EXPECT_NE(er, table.equal_range({}));

  auto it = table.find("3");
  EXPECT_EQ(it, table.find(3));
  EXPECT_NE(it, table.find(4));
  EXPECT_EQ(it, table.find({"3"}));
  EXPECT_NE(it, table.find({}));

  EXPECT_TRUE(table.contains(3));
  EXPECT_FALSE(table.contains(4));
  EXPECT_TRUE(table.count({"3"}));
  EXPECT_FALSE(table.contains({}));

  EXPECT_EQ(1, table.count(3));
  EXPECT_EQ(0, table.count(4));
  EXPECT_EQ(1, table.count({"3"}));
  EXPECT_EQ(0, table.count({}));

  auto copy = table;
  copy.erase(3);
  EXPECT_EQ(table.size() - 1, copy.size());
  copy.erase(4);
  EXPECT_EQ(table.size() - 1, copy.size());
  copy.erase({"5"});
  EXPECT_EQ(table.size() - 2, copy.size());
  EXPECT_FALSE(CanEraseWithEmptyBrace(table, nullptr));

  // Also run it with const T&.
  if (std::is_class<T>()) TestHeterogeneous<const T &>(table);
}

TEST(Btree, HeterogeneousLookup) {
  TestHeterogeneous(btree_set<std::string, CompareIntToString>{"1", "3", "5"});
  TestHeterogeneous(btree_map<std::string, int, CompareIntToString>{
      {"1", 1}, {"3", 3}, {"5", 5}});
  TestHeterogeneous(
      btree_multiset<std::string, CompareIntToString>{"1", "3", "5"});
  TestHeterogeneous(btree_multimap<std::string, int, CompareIntToString>{
      {"1", 1}, {"3", 3}, {"5", 5}});

  // Only maps have .at()
  btree_map<std::string, int, CompareIntToString> map{
      {"", -1}, {"1", 1}, {"3", 3}, {"5", 5}};
  EXPECT_EQ(1, map.at(1));
  EXPECT_EQ(3, map.at({"3"}));
  EXPECT_EQ(-1, map.at({}));
  const auto &cmap = map;
  EXPECT_EQ(1, cmap.at(1));
  EXPECT_EQ(3, cmap.at({"3"}));
  EXPECT_EQ(-1, cmap.at({}));
}

TEST(Btree, NoHeterogeneousLookupWithoutAlias) {
  using StringSet = absl::btree_set<std::string, NonTransparentCompare>;
  StringSet s;
  ASSERT_TRUE(s.insert("hello").second);
  ASSERT_TRUE(s.insert("world").second);
  EXPECT_TRUE(s.end() == s.find("blah"));
  EXPECT_TRUE(s.begin() == s.lower_bound("hello"));
  EXPECT_EQ(1, s.count("world"));
  EXPECT_TRUE(s.contains("hello"));
  EXPECT_TRUE(s.contains("world"));
  EXPECT_FALSE(s.contains("blah"));

  using StringMultiSet =
      absl::btree_multiset<std::string, NonTransparentCompare>;
  StringMultiSet ms;
  ms.insert("hello");
  ms.insert("world");
  ms.insert("world");
  EXPECT_TRUE(ms.end() == ms.find("blah"));
  EXPECT_TRUE(ms.begin() == ms.lower_bound("hello"));
  EXPECT_EQ(2, ms.count("world"));
  EXPECT_TRUE(ms.contains("hello"));
  EXPECT_TRUE(ms.contains("world"));
  EXPECT_FALSE(ms.contains("blah"));
}

TEST(Btree, DefaultTransparent) {
  {
    // `int` does not have a default transparent comparator.
    // The input value is converted to key_type.
    btree_set<int> s = {1};
    double d = 1.1;
    EXPECT_EQ(s.begin(), s.find(d));
    EXPECT_TRUE(s.contains(d));
  }

  {
    // `std::string` has heterogeneous support.
    btree_set<std::string> s = {"A"};
    EXPECT_EQ(s.begin(), s.find(absl::string_view("A")));
    EXPECT_TRUE(s.contains(absl::string_view("A")));
  }
}

class StringLike {
 public:
  StringLike() = default;

  StringLike(const char *s) : s_(s) {  // NOLINT
    ++constructor_calls_;
  }

  bool operator<(const StringLike &a) const { return s_ < a.s_; }

  static void clear_constructor_call_count() { constructor_calls_ = 0; }

  static int constructor_calls() { return constructor_calls_; }

 private:
  static int constructor_calls_;
  std::string s_;
};

int StringLike::constructor_calls_ = 0;

TEST(Btree, HeterogeneousLookupDoesntDegradePerformance) {
  using StringSet = absl::btree_set<StringLike>;
  StringSet s;
  for (int i = 0; i < 100; ++i) {
    ASSERT_TRUE(s.insert(absl::StrCat(i).c_str()).second);
  }
  StringLike::clear_constructor_call_count();
  s.find("50");
  ASSERT_EQ(1, StringLike::constructor_calls());

  StringLike::clear_constructor_call_count();
  s.contains("50");
  ASSERT_EQ(1, StringLike::constructor_calls());

  StringLike::clear_constructor_call_count();
  s.count("50");
  ASSERT_EQ(1, StringLike::constructor_calls());

  StringLike::clear_constructor_call_count();
  s.lower_bound("50");
  ASSERT_EQ(1, StringLike::constructor_calls());

  StringLike::clear_constructor_call_count();
  s.upper_bound("50");
  ASSERT_EQ(1, StringLike::constructor_calls());

  StringLike::clear_constructor_call_count();
  s.equal_range("50");
  ASSERT_EQ(1, StringLike::constructor_calls());

  StringLike::clear_constructor_call_count();
  s.erase("50");
  ASSERT_EQ(1, StringLike::constructor_calls());
}

// Verify that swapping btrees swaps the key comparison functors and that we can
// use non-default constructible comparators.
struct SubstringLess {
  SubstringLess() = delete;
  explicit SubstringLess(int length) : n(length) {}
  bool operator()(const std::string &a, const std::string &b) const {
    return absl::string_view(a).substr(0, n) <
           absl::string_view(b).substr(0, n);
  }
  int n;
};

TEST(Btree, SwapKeyCompare) {
  using SubstringSet = absl::btree_set<std::string, SubstringLess>;
  SubstringSet s1(SubstringLess(1), SubstringSet::allocator_type());
  SubstringSet s2(SubstringLess(2), SubstringSet::allocator_type());

  ASSERT_TRUE(s1.insert("a").second);
  ASSERT_FALSE(s1.insert("aa").second);

  ASSERT_TRUE(s2.insert("a").second);
  ASSERT_TRUE(s2.insert("aa").second);
  ASSERT_FALSE(s2.insert("aaa").second);

  swap(s1, s2);

  ASSERT_TRUE(s1.insert("b").second);
  ASSERT_TRUE(s1.insert("bb").second);
  ASSERT_FALSE(s1.insert("bbb").second);

  ASSERT_TRUE(s2.insert("b").second);
  ASSERT_FALSE(s2.insert("bb").second);
}

TEST(Btree, UpperBoundRegression) {
  // Regress a bug where upper_bound would default-construct a new key_compare
  // instead of copying the existing one.
  using SubstringSet = absl::btree_set<std::string, SubstringLess>;
  SubstringSet my_set(SubstringLess(3));
  my_set.insert("aab");
  my_set.insert("abb");
  // We call upper_bound("aaa").  If this correctly uses the length 3
  // comparator, aaa < aab < abb, so we should get aab as the result.
  // If it instead uses the default-constructed length 2 comparator,
  // aa == aa < ab, so we'll get abb as our result.
  SubstringSet::iterator it = my_set.upper_bound("aaa");
  ASSERT_TRUE(it != my_set.end());
  EXPECT_EQ("aab", *it);
}

TEST(Btree, Comparison) {
  const int kSetSize = 1201;
  absl::btree_set<int64_t> my_set;
  for (int i = 0; i < kSetSize; ++i) {
    my_set.insert(i);
  }
  absl::btree_set<int64_t> my_set_copy(my_set);
  EXPECT_TRUE(my_set_copy == my_set);
  EXPECT_TRUE(my_set == my_set_copy);
  EXPECT_FALSE(my_set_copy != my_set);
  EXPECT_FALSE(my_set != my_set_copy);

  my_set.insert(kSetSize);
  EXPECT_FALSE(my_set_copy == my_set);
  EXPECT_FALSE(my_set == my_set_copy);
  EXPECT_TRUE(my_set_copy != my_set);
  EXPECT_TRUE(my_set != my_set_copy);

  my_set.erase(kSetSize - 1);
  EXPECT_FALSE(my_set_copy == my_set);
  EXPECT_FALSE(my_set == my_set_copy);
  EXPECT_TRUE(my_set_copy != my_set);
  EXPECT_TRUE(my_set != my_set_copy);

  absl::btree_map<std::string, int64_t> my_map;
  for (int i = 0; i < kSetSize; ++i) {
    my_map[std::string(i, 'a')] = i;
  }
  absl::btree_map<std::string, int64_t> my_map_copy(my_map);
  EXPECT_TRUE(my_map_copy == my_map);
  EXPECT_TRUE(my_map == my_map_copy);
  EXPECT_FALSE(my_map_copy != my_map);
  EXPECT_FALSE(my_map != my_map_copy);

  ++my_map_copy[std::string(7, 'a')];
  EXPECT_FALSE(my_map_copy == my_map);
  EXPECT_FALSE(my_map == my_map_copy);
  EXPECT_TRUE(my_map_copy != my_map);
  EXPECT_TRUE(my_map != my_map_copy);

  my_map_copy = my_map;
  my_map["hello"] = kSetSize;
  EXPECT_FALSE(my_map_copy == my_map);
  EXPECT_FALSE(my_map == my_map_copy);
  EXPECT_TRUE(my_map_copy != my_map);
  EXPECT_TRUE(my_map != my_map_copy);

  my_map.erase(std::string(kSetSize - 1, 'a'));
  EXPECT_FALSE(my_map_copy == my_map);
  EXPECT_FALSE(my_map == my_map_copy);
  EXPECT_TRUE(my_map_copy != my_map);
  EXPECT_TRUE(my_map != my_map_copy);
}

TEST(Btree, RangeCtorSanity) {
  std::vector<int> ivec;
  ivec.push_back(1);
  std::map<int, int> imap;
  imap.insert(std::make_pair(1, 2));
  absl::btree_multiset<int> tmset(ivec.begin(), ivec.end());
  absl::btree_multimap<int, int> tmmap(imap.begin(), imap.end());
  absl::btree_set<int> tset(ivec.begin(), ivec.end());
  absl::btree_map<int, int> tmap(imap.begin(), imap.end());
  EXPECT_EQ(1, tmset.size());
  EXPECT_EQ(1, tmmap.size());
  EXPECT_EQ(1, tset.size());
  EXPECT_EQ(1, tmap.size());
}

}  // namespace

class BtreeNodePeer {
 public:
  // Yields the size of a leaf node with a specific number of values.
  template <typename ValueType>
  constexpr static size_t GetTargetNodeSize(size_t target_values_per_node) {
    return btree_node<
        set_params<ValueType, std::less<ValueType>, std::allocator<ValueType>,
                   /*TargetNodeSize=*/256,  // This parameter isn't used here.
                   /*Multi=*/false>>::SizeWithNSlots(target_values_per_node);
  }

  // Yields the number of slots in a (non-root) leaf node for this btree.
  template <typename Btree>
  constexpr static size_t GetNumSlotsPerNode() {
    return btree_node<typename Btree::params_type>::kNodeSlots;
  }

  template <typename Btree>
  constexpr static size_t GetMaxFieldType() {
    return std::numeric_limits<
        typename btree_node<typename Btree::params_type>::field_type>::max();
  }

  template <typename Btree>
  constexpr static bool UsesLinearNodeSearch() {
    return btree_node<typename Btree::params_type>::use_linear_search::value;
  }

  template <typename Btree>
  constexpr static bool FieldTypeEqualsSlotType() {
    return std::is_same<
        typename btree_node<typename Btree::params_type>::field_type,
        typename btree_node<typename Btree::params_type>::slot_type>::value;
  }
};

namespace {

class BtreeMapTest : public ::testing::Test {
 public:
  struct Key {};
  struct Cmp {
    template <typename T>
    bool operator()(T, T) const {
      return false;
    }
  };

  struct KeyLin {
    using absl_btree_prefer_linear_node_search = std::true_type;
  };
  struct CmpLin : Cmp {
    using absl_btree_prefer_linear_node_search = std::true_type;
  };

  struct KeyBin {
    using absl_btree_prefer_linear_node_search = std::false_type;
  };
  struct CmpBin : Cmp {
    using absl_btree_prefer_linear_node_search = std::false_type;
  };

  template <typename K, typename C>
  static bool IsLinear() {
    return BtreeNodePeer::UsesLinearNodeSearch<absl::btree_map<K, int, C>>();
  }
};

TEST_F(BtreeMapTest, TestLinearSearchPreferredForKeyLinearViaAlias) {
  // Test requesting linear search by directly exporting an alias.
  EXPECT_FALSE((IsLinear<Key, Cmp>()));
  EXPECT_TRUE((IsLinear<KeyLin, Cmp>()));
  EXPECT_TRUE((IsLinear<Key, CmpLin>()));
  EXPECT_TRUE((IsLinear<KeyLin, CmpLin>()));
}

TEST_F(BtreeMapTest, LinearChoiceTree) {
  // Cmp has precedence, and is forcing binary
  EXPECT_FALSE((IsLinear<Key, CmpBin>()));
  EXPECT_FALSE((IsLinear<KeyLin, CmpBin>()));
  EXPECT_FALSE((IsLinear<KeyBin, CmpBin>()));
  EXPECT_FALSE((IsLinear<int, CmpBin>()));
  EXPECT_FALSE((IsLinear<std::string, CmpBin>()));
  // Cmp has precedence, and is forcing linear
  EXPECT_TRUE((IsLinear<Key, CmpLin>()));
  EXPECT_TRUE((IsLinear<KeyLin, CmpLin>()));
  EXPECT_TRUE((IsLinear<KeyBin, CmpLin>()));
  EXPECT_TRUE((IsLinear<int, CmpLin>()));
  EXPECT_TRUE((IsLinear<std::string, CmpLin>()));
  // Cmp has no preference, Key determines linear vs binary.
  EXPECT_FALSE((IsLinear<Key, Cmp>()));
  EXPECT_TRUE((IsLinear<KeyLin, Cmp>()));
  EXPECT_FALSE((IsLinear<KeyBin, Cmp>()));
  // arithmetic key w/ std::less or std::greater: linear
  EXPECT_TRUE((IsLinear<int, std::less<int>>()));
  EXPECT_TRUE((IsLinear<double, std::greater<double>>()));
  // arithmetic key w/ custom compare: binary
  EXPECT_FALSE((IsLinear<int, Cmp>()));
  // non-arithmetic key: binary
  EXPECT_FALSE((IsLinear<std::string, std::less<std::string>>()));
}

TEST(Btree, BtreeMapCanHoldMoveOnlyTypes) {
  absl::btree_map<std::string, std::unique_ptr<std::string>> m;

  std::unique_ptr<std::string> &v = m["A"];
  EXPECT_TRUE(v == nullptr);
  v = absl::make_unique<std::string>("X");

  auto iter = m.find("A");
  EXPECT_EQ("X", *iter->second);
}

TEST(Btree, InitializerListConstructor) {
  absl::btree_set<std::string> set({"a", "b"});
  EXPECT_EQ(set.count("a"), 1);
  EXPECT_EQ(set.count("b"), 1);

  absl::btree_multiset<int> mset({1, 1, 4});
  EXPECT_EQ(mset.count(1), 2);
  EXPECT_EQ(mset.count(4), 1);

  absl::btree_map<int, int> map({{1, 5}, {2, 10}});
  EXPECT_EQ(map[1], 5);
  EXPECT_EQ(map[2], 10);

  absl::btree_multimap<int, int> mmap({{1, 5}, {1, 10}});
  auto range = mmap.equal_range(1);
  auto it = range.first;
  ASSERT_NE(it, range.second);
  EXPECT_EQ(it->second, 5);
  ASSERT_NE(++it, range.second);
  EXPECT_EQ(it->second, 10);
  EXPECT_EQ(++it, range.second);
}

TEST(Btree, InitializerListInsert) {
  absl::btree_set<std::string> set;
  set.insert({"a", "b"});
  EXPECT_EQ(set.count("a"), 1);
  EXPECT_EQ(set.count("b"), 1);

  absl::btree_multiset<int> mset;
  mset.insert({1, 1, 4});
  EXPECT_EQ(mset.count(1), 2);
  EXPECT_EQ(mset.count(4), 1);

  absl::btree_map<int, int> map;
  map.insert({{1, 5}, {2, 10}});
  // Test that inserting one element using an initializer list also works.
  map.insert({3, 15});
  EXPECT_EQ(map[1], 5);
  EXPECT_EQ(map[2], 10);
  EXPECT_EQ(map[3], 15);

  absl::btree_multimap<int, int> mmap;
  mmap.insert({{1, 5}, {1, 10}});
  auto range = mmap.equal_range(1);
  auto it = range.first;
  ASSERT_NE(it, range.second);
  EXPECT_EQ(it->second, 5);
  ASSERT_NE(++it, range.second);
  EXPECT_EQ(it->second, 10);
  EXPECT_EQ(++it, range.second);
}

template <typename Compare, typename Key>
void AssertKeyCompareStringAdapted() {
  using Adapted = typename key_compare_adapter<Compare, Key>::type;
  static_assert(
      std::is_same<Adapted, StringBtreeDefaultLess>::value ||
          std::is_same<Adapted, StringBtreeDefaultGreater>::value,
      "key_compare_adapter should have string-adapted this comparator.");
}
template <typename Compare, typename Key>
void AssertKeyCompareNotStringAdapted() {
  using Adapted = typename key_compare_adapter<Compare, Key>::type;
  static_assert(
      !std::is_same<Adapted, StringBtreeDefaultLess>::value &&
          !std::is_same<Adapted, StringBtreeDefaultGreater>::value,
      "key_compare_adapter shouldn't have string-adapted this comparator.");
}

TEST(Btree, KeyCompareAdapter) {
  AssertKeyCompareStringAdapted<std::less<std::string>, std::string>();
  AssertKeyCompareStringAdapted<std::greater<std::string>, std::string>();
  AssertKeyCompareStringAdapted<std::less<absl::string_view>,
                                absl::string_view>();
  AssertKeyCompareStringAdapted<std::greater<absl::string_view>,
                                absl::string_view>();
  AssertKeyCompareStringAdapted<std::less<absl::Cord>, absl::Cord>();
  AssertKeyCompareStringAdapted<std::greater<absl::Cord>, absl::Cord>();
  AssertKeyCompareNotStringAdapted<std::less<int>, int>();
  AssertKeyCompareNotStringAdapted<std::greater<int>, int>();
}

TEST(Btree, RValueInsert) {
  InstanceTracker tracker;

  absl::btree_set<MovableOnlyInstance> set;
  set.insert(MovableOnlyInstance(1));
  set.insert(MovableOnlyInstance(3));
  MovableOnlyInstance two(2);
  set.insert(set.find(MovableOnlyInstance(3)), std::move(two));
  auto it = set.find(MovableOnlyInstance(2));
  ASSERT_NE(it, set.end());
  ASSERT_NE(++it, set.end());
  EXPECT_EQ(it->value(), 3);

  absl::btree_multiset<MovableOnlyInstance> mset;
  MovableOnlyInstance zero(0);
  MovableOnlyInstance zero2(0);
  mset.insert(std::move(zero));
  mset.insert(mset.find(MovableOnlyInstance(0)), std::move(zero2));
  EXPECT_EQ(mset.count(MovableOnlyInstance(0)), 2);

  absl::btree_map<int, MovableOnlyInstance> map;
  std::pair<const int, MovableOnlyInstance> p1 = {1, MovableOnlyInstance(5)};
  std::pair<const int, MovableOnlyInstance> p2 = {2, MovableOnlyInstance(10)};
  std::pair<const int, MovableOnlyInstance> p3 = {3, MovableOnlyInstance(15)};
  map.insert(std::move(p1));
  map.insert(std::move(p3));
  map.insert(map.find(3), std::move(p2));
  ASSERT_NE(map.find(2), map.end());
  EXPECT_EQ(map.find(2)->second.value(), 10);

  absl::btree_multimap<int, MovableOnlyInstance> mmap;
  std::pair<const int, MovableOnlyInstance> p4 = {1, MovableOnlyInstance(5)};
  std::pair<const int, MovableOnlyInstance> p5 = {1, MovableOnlyInstance(10)};
  mmap.insert(std::move(p4));
  mmap.insert(mmap.find(1), std::move(p5));
  auto range = mmap.equal_range(1);
  auto it1 = range.first;
  ASSERT_NE(it1, range.second);
  EXPECT_EQ(it1->second.value(), 10);
  ASSERT_NE(++it1, range.second);
  EXPECT_EQ(it1->second.value(), 5);
  EXPECT_EQ(++it1, range.second);

  EXPECT_EQ(tracker.copies(), 0);
  EXPECT_EQ(tracker.swaps(), 0);
}

template <typename Cmp>
struct CheckedCompareOptedOutCmp : Cmp, BtreeTestOnlyCheckedCompareOptOutBase {
  using Cmp::Cmp;
  CheckedCompareOptedOutCmp() {}
  CheckedCompareOptedOutCmp(Cmp cmp) : Cmp(std::move(cmp)) {}  // NOLINT
};

// A btree set with a specific number of values per node. Opt out of
// checked_compare so that we can expect exact numbers of comparisons.
template <typename Key, int TargetValuesPerNode, typename Cmp = std::less<Key>>
class SizedBtreeSet
    : public btree_set_container<btree<
          set_params<Key, CheckedCompareOptedOutCmp<Cmp>, std::allocator<Key>,
                     BtreeNodePeer::GetTargetNodeSize<Key>(TargetValuesPerNode),
                     /*Multi=*/false>>> {
  using Base = typename SizedBtreeSet::btree_set_container;

 public:
  SizedBtreeSet() = default;
  using Base::Base;
};

template <typename Set>
void ExpectOperationCounts(const int expected_moves,
                           const int expected_comparisons,
                           const std::vector<int> &values,
                           InstanceTracker *tracker, Set *set) {
  for (const int v : values) set->insert(MovableOnlyInstance(v));
  set->clear();
  EXPECT_EQ(tracker->moves(), expected_moves);
  EXPECT_EQ(tracker->comparisons(), expected_comparisons);
  EXPECT_EQ(tracker->copies(), 0);
  EXPECT_EQ(tracker->swaps(), 0);
  tracker->ResetCopiesMovesSwaps();
}

#if defined(ABSL_HAVE_ADDRESS_SANITIZER) || \
    defined(ABSL_HAVE_HWADDRESS_SANITIZER)
constexpr bool kAsan = true;
#else
constexpr bool kAsan = false;
#endif

// Note: when the values in this test change, it is expected to have an impact
// on performance.
TEST(Btree, MovesComparisonsCopiesSwapsTracking) {
  if (kAsan) GTEST_SKIP() << "We do extra operations in ASan mode.";

  InstanceTracker tracker;
  // Note: this is minimum number of values per node.
  SizedBtreeSet<MovableOnlyInstance, /*TargetValuesPerNode=*/4> set4;
  // Note: this is the default number of values per node for a set of int32s
  // (with 64-bit pointers).
  SizedBtreeSet<MovableOnlyInstance, /*TargetValuesPerNode=*/61> set61;
  SizedBtreeSet<MovableOnlyInstance, /*TargetValuesPerNode=*/100> set100;

  // Don't depend on flags for random values because then the expectations will
  // fail if the flags change.
  std::vector<int> values =
      GenerateValuesWithSeed<int>(10000, 1 << 22, /*seed=*/23);

  EXPECT_EQ(BtreeNodePeer::GetNumSlotsPerNode<decltype(set4)>(), 4);
  EXPECT_EQ(BtreeNodePeer::GetNumSlotsPerNode<decltype(set61)>(), 61);
  EXPECT_EQ(BtreeNodePeer::GetNumSlotsPerNode<decltype(set100)>(), 100);
  if (sizeof(void *) == 8) {
    EXPECT_EQ(BtreeNodePeer::GetNumSlotsPerNode<absl::btree_set<int32_t>>(),
              // When we have generations, there is one fewer slot.
              BtreeGenerationsEnabled() ? 60 : 61);
  }

  // Test key insertion/deletion in random order.
  ExpectOperationCounts(56540, 134212, values, &tracker, &set4);
  ExpectOperationCounts(386718, 129807, values, &tracker, &set61);
  ExpectOperationCounts(586761, 130310, values, &tracker, &set100);

  // Test key insertion/deletion in sorted order.
  std::sort(values.begin(), values.end());
  ExpectOperationCounts(24972, 85563, values, &tracker, &set4);
  ExpectOperationCounts(20208, 87757, values, &tracker, &set61);
  ExpectOperationCounts(20124, 96583, values, &tracker, &set100);

  // Test key insertion/deletion in reverse sorted order.
  std::reverse(values.begin(), values.end());
  ExpectOperationCounts(54949, 127531, values, &tracker, &set4);
  ExpectOperationCounts(338813, 118266, values, &tracker, &set61);
  ExpectOperationCounts(534529, 125279, values, &tracker, &set100);
}

struct MovableOnlyInstanceThreeWayCompare {
  absl::weak_ordering operator()(const MovableOnlyInstance &a,
                                 const MovableOnlyInstance &b) const {
    return a.compare(b);
  }
};

// Note: when the values in this test change, it is expected to have an impact
// on performance.
TEST(Btree, MovesComparisonsCopiesSwapsTrackingThreeWayCompare) {
  if (kAsan) GTEST_SKIP() << "We do extra operations in ASan mode.";

  InstanceTracker tracker;
  // Note: this is minimum number of values per node.
  SizedBtreeSet<MovableOnlyInstance, /*TargetValuesPerNode=*/4,
                MovableOnlyInstanceThreeWayCompare>
      set4;
  // Note: this is the default number of values per node for a set of int32s
  // (with 64-bit pointers).
  SizedBtreeSet<MovableOnlyInstance, /*TargetValuesPerNode=*/61,
                MovableOnlyInstanceThreeWayCompare>
      set61;
  SizedBtreeSet<MovableOnlyInstance, /*TargetValuesPerNode=*/100,
                MovableOnlyInstanceThreeWayCompare>
      set100;

  // Don't depend on flags for random values because then the expectations will
  // fail if the flags change.
  std::vector<int> values =
      GenerateValuesWithSeed<int>(10000, 1 << 22, /*seed=*/23);

  EXPECT_EQ(BtreeNodePeer::GetNumSlotsPerNode<decltype(set4)>(), 4);
  EXPECT_EQ(BtreeNodePeer::GetNumSlotsPerNode<decltype(set61)>(), 61);
  EXPECT_EQ(BtreeNodePeer::GetNumSlotsPerNode<decltype(set100)>(), 100);
  if (sizeof(void *) == 8) {
    EXPECT_EQ(BtreeNodePeer::GetNumSlotsPerNode<absl::btree_set<int32_t>>(),
              // When we have generations, there is one fewer slot.
              BtreeGenerationsEnabled() ? 60 : 61);
  }

  // Test key insertion/deletion in random order.
  ExpectOperationCounts(56540, 124221, values, &tracker, &set4);
  ExpectOperationCounts(386718, 119816, values, &tracker, &set61);
  ExpectOperationCounts(586761, 120319, values, &tracker, &set100);

  // Test key insertion/deletion in sorted order.
  std::sort(values.begin(), values.end());
  ExpectOperationCounts(24972, 85563, values, &tracker, &set4);
  ExpectOperationCounts(20208, 87757, values, &tracker, &set61);
  ExpectOperationCounts(20124, 96583, values, &tracker, &set100);

  // Test key insertion/deletion in reverse sorted order.
  std::reverse(values.begin(), values.end());
  ExpectOperationCounts(54949, 117532, values, &tracker, &set4);
  ExpectOperationCounts(338813, 108267, values, &tracker, &set61);
  ExpectOperationCounts(534529, 115280, values, &tracker, &set100);
}

struct NoDefaultCtor {
  int num;
  explicit NoDefaultCtor(int i) : num(i) {}

  friend bool operator<(const NoDefaultCtor &a, const NoDefaultCtor &b) {
    return a.num < b.num;
  }
};

TEST(Btree, BtreeMapCanHoldNoDefaultCtorTypes) {
  absl::btree_map<NoDefaultCtor, NoDefaultCtor> m;

  for (int i = 1; i <= 99; ++i) {
    SCOPED_TRACE(i);
    EXPECT_TRUE(m.emplace(NoDefaultCtor(i), NoDefaultCtor(100 - i)).second);
  }
  EXPECT_FALSE(m.emplace(NoDefaultCtor(78), NoDefaultCtor(0)).second);

  auto iter99 = m.find(NoDefaultCtor(99));
  ASSERT_NE(iter99, m.end());
  EXPECT_EQ(iter99->second.num, 1);

  auto iter1 = m.find(NoDefaultCtor(1));
  ASSERT_NE(iter1, m.end());
  EXPECT_EQ(iter1->second.num, 99);

  auto iter50 = m.find(NoDefaultCtor(50));
  ASSERT_NE(iter50, m.end());
  EXPECT_EQ(iter50->second.num, 50);

  auto iter25 = m.find(NoDefaultCtor(25));
  ASSERT_NE(iter25, m.end());
  EXPECT_EQ(iter25->second.num, 75);
}

TEST(Btree, BtreeMultimapCanHoldNoDefaultCtorTypes) {
  absl::btree_multimap<NoDefaultCtor, NoDefaultCtor> m;

  for (int i = 1; i <= 99; ++i) {
    SCOPED_TRACE(i);
    m.emplace(NoDefaultCtor(i), NoDefaultCtor(100 - i));
  }

  auto iter99 = m.find(NoDefaultCtor(99));
  ASSERT_NE(iter99, m.end());
  EXPECT_EQ(iter99->second.num, 1);

  auto iter1 = m.find(NoDefaultCtor(1));
  ASSERT_NE(iter1, m.end());
  EXPECT_EQ(iter1->second.num, 99);

  auto iter50 = m.find(NoDefaultCtor(50));
  ASSERT_NE(iter50, m.end());
  EXPECT_EQ(iter50->second.num, 50);

  auto iter25 = m.find(NoDefaultCtor(25));
  ASSERT_NE(iter25, m.end());
  EXPECT_EQ(iter25->second.num, 75);
}

TEST(Btree, MapAt) {
  absl::btree_map<int, int> map = {{1, 2}, {2, 4}};
  EXPECT_EQ(map.at(1), 2);
  EXPECT_EQ(map.at(2), 4);
  map.at(2) = 8;
  const absl::btree_map<int, int> &const_map = map;
  EXPECT_EQ(const_map.at(1), 2);
  EXPECT_EQ(const_map.at(2), 8);
#ifdef ABSL_HAVE_EXCEPTIONS
  EXPECT_THROW(map.at(3), std::out_of_range);
#else
  EXPECT_DEATH_IF_SUPPORTED(map.at(3), "absl::btree_map::at");
#endif
}

TEST(Btree, BtreeMultisetEmplace) {
  const int value_to_insert = 123456;
  absl::btree_multiset<int> s;
  auto iter = s.emplace(value_to_insert);
  ASSERT_NE(iter, s.end());
  EXPECT_EQ(*iter, value_to_insert);
  iter = s.emplace(value_to_insert);
  ASSERT_NE(iter, s.end());
  EXPECT_EQ(*iter, value_to_insert);
  auto result = s.equal_range(value_to_insert);
  EXPECT_EQ(std::distance(result.first, result.second), 2);
}

TEST(Btree, BtreeMultisetEmplaceHint) {
  const int value_to_insert = 123456;
  absl::btree_multiset<int> s;
  auto iter = s.emplace(value_to_insert);
  ASSERT_NE(iter, s.end());
  EXPECT_EQ(*iter, value_to_insert);
  iter = s.emplace_hint(iter, value_to_insert);
  // The new element should be before the previously inserted one.
  EXPECT_EQ(iter, s.lower_bound(value_to_insert));
  ASSERT_NE(iter, s.end());
  EXPECT_EQ(*iter, value_to_insert);
}

TEST(Btree, BtreeMultimapEmplace) {
  const int key_to_insert = 123456;
  const char value0[] = "a";
  absl::btree_multimap<int, std::string> m;
  auto iter = m.emplace(key_to_insert, value0);
  ASSERT_NE(iter, m.end());
  EXPECT_EQ(iter->first, key_to_insert);
  EXPECT_EQ(iter->second, value0);
  const char value1[] = "b";
  iter = m.emplace(key_to_insert, value1);
  ASSERT_NE(iter, m.end());
  EXPECT_EQ(iter->first, key_to_insert);
  EXPECT_EQ(iter->second, value1);
  auto result = m.equal_range(key_to_insert);
  EXPECT_EQ(std::distance(result.first, result.second), 2);
}

TEST(Btree, BtreeMultimapEmplaceHint) {
  const int key_to_insert = 123456;
  const char value0[] = "a";
  absl::btree_multimap<int, std::string> m;
  auto iter = m.emplace(key_to_insert, value0);
  ASSERT_NE(iter, m.end());
  EXPECT_EQ(iter->first, key_to_insert);
  EXPECT_EQ(iter->second, value0);
  const char value1[] = "b";
  iter = m.emplace_hint(iter, key_to_insert, value1);
  // The new element should be before the previously inserted one.
  EXPECT_EQ(iter, m.lower_bound(key_to_insert));
  ASSERT_NE(iter, m.end());
  EXPECT_EQ(iter->first, key_to_insert);
  EXPECT_EQ(iter->second, value1);
}

TEST(Btree, ConstIteratorAccessors) {
  absl::btree_set<int> set;
  for (int i = 0; i < 100; ++i) {
    set.insert(i);
  }

  auto it = set.cbegin();
  auto r_it = set.crbegin();
  for (int i = 0; i < 100; ++i, ++it, ++r_it) {
    ASSERT_EQ(*it, i);
    ASSERT_EQ(*r_it, 99 - i);
  }
  EXPECT_EQ(it, set.cend());
  EXPECT_EQ(r_it, set.crend());
}

TEST(Btree, StrSplitCompatible) {
  const absl::btree_set<std::string> split_set = absl::StrSplit("a,b,c", ',');
  const absl::btree_set<std::string> expected_set = {"a", "b", "c"};

  EXPECT_EQ(split_set, expected_set);
}

TEST(Btree, KeyComp) {
  absl::btree_set<int> s;
  EXPECT_TRUE(s.key_comp()(1, 2));
  EXPECT_FALSE(s.key_comp()(2, 2));
  EXPECT_FALSE(s.key_comp()(2, 1));

  absl::btree_map<int, int> m1;
  EXPECT_TRUE(m1.key_comp()(1, 2));
  EXPECT_FALSE(m1.key_comp()(2, 2));
  EXPECT_FALSE(m1.key_comp()(2, 1));

  // Even though we internally adapt the comparator of `m2` to be three-way and
  // heterogeneous, the comparator we expose through key_comp() is the original
  // unadapted comparator.
  absl::btree_map<std::string, int> m2;
  EXPECT_TRUE(m2.key_comp()("a", "b"));
  EXPECT_FALSE(m2.key_comp()("b", "b"));
  EXPECT_FALSE(m2.key_comp()("b", "a"));
}

TEST(Btree, ValueComp) {
  absl::btree_set<int> s;
  EXPECT_TRUE(s.value_comp()(1, 2));
  EXPECT_FALSE(s.value_comp()(2, 2));
  EXPECT_FALSE(s.value_comp()(2, 1));

  absl::btree_map<int, int> m1;
  EXPECT_TRUE(m1.value_comp()(std::make_pair(1, 0), std::make_pair(2, 0)));
  EXPECT_FALSE(m1.value_comp()(std::make_pair(2, 0), std::make_pair(2, 0)));
  EXPECT_FALSE(m1.value_comp()(std::make_pair(2, 0), std::make_pair(1, 0)));

  // Even though we internally adapt the comparator of `m2` to be three-way and
  // heterogeneous, the comparator we expose through value_comp() is based on
  // the original unadapted comparator.
  absl::btree_map<std::string, int> m2;
  EXPECT_TRUE(m2.value_comp()(std::make_pair("a", 0), std::make_pair("b", 0)));
  EXPECT_FALSE(m2.value_comp()(std::make_pair("b", 0), std::make_pair("b", 0)));
  EXPECT_FALSE(m2.value_comp()(std::make_pair("b", 0), std::make_pair("a", 0)));
}

// Test that we have the protected members from the std::map::value_compare API.
// See https://en.cppreference.com/w/cpp/container/map/value_compare.
TEST(Btree, MapValueCompProtected) {
  struct key_compare {
    bool operator()(int l, int r) const { return l < r; }
    int id;
  };
  using value_compare = absl::btree_map<int, int, key_compare>::value_compare;
  struct value_comp_child : public value_compare {
    explicit value_comp_child(key_compare kc) : value_compare(kc) {}
    int GetId() const { return comp.id; }
  };
  value_comp_child c(key_compare{10});
  EXPECT_EQ(c.GetId(), 10);
}

TEST(Btree, DefaultConstruction) {
  absl::btree_set<int> s;
  absl::btree_map<int, int> m;
  absl::btree_multiset<int> ms;
  absl::btree_multimap<int, int> mm;

  EXPECT_TRUE(s.empty());
  EXPECT_TRUE(m.empty());
  EXPECT_TRUE(ms.empty());
  EXPECT_TRUE(mm.empty());
}

TEST(Btree, SwissTableHashable) {
  static constexpr int kValues = 10000;
  std::vector<int> values(kValues);
  std::iota(values.begin(), values.end(), 0);
  std::vector<std::pair<int, int>> map_values;
  for (int v : values) map_values.emplace_back(v, -v);

  using set = absl::btree_set<int>;
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      set{},
      set{1},
      set{2},
      set{1, 2},
      set{2, 1},
      set(values.begin(), values.end()),
      set(values.rbegin(), values.rend()),
  }));

  using mset = absl::btree_multiset<int>;
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      mset{},
      mset{1},
      mset{1, 1},
      mset{2},
      mset{2, 2},
      mset{1, 2},
      mset{1, 1, 2},
      mset{1, 2, 2},
      mset{1, 1, 2, 2},
      mset(values.begin(), values.end()),
      mset(values.rbegin(), values.rend()),
  }));

  using map = absl::btree_map<int, int>;
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      map{},
      map{{1, 0}},
      map{{1, 1}},
      map{{2, 0}},
      map{{2, 2}},
      map{{1, 0}, {2, 1}},
      map(map_values.begin(), map_values.end()),
      map(map_values.rbegin(), map_values.rend()),
  }));

  using mmap = absl::btree_multimap<int, int>;
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      mmap{},
      mmap{{1, 0}},
      mmap{{1, 1}},
      mmap{{1, 0}, {1, 1}},
      mmap{{1, 1}, {1, 0}},
      mmap{{2, 0}},
      mmap{{2, 2}},
      mmap{{1, 0}, {2, 1}},
      mmap(map_values.begin(), map_values.end()),
      mmap(map_values.rbegin(), map_values.rend()),
  }));
}

TEST(Btree, ComparableSet) {
  absl::btree_set<int> s1 = {1, 2};
  absl::btree_set<int> s2 = {2, 3};
  EXPECT_LT(s1, s2);
  EXPECT_LE(s1, s2);
  EXPECT_LE(s1, s1);
  EXPECT_GT(s2, s1);
  EXPECT_GE(s2, s1);
  EXPECT_GE(s1, s1);
}

TEST(Btree, ComparableSetsDifferentLength) {
  absl::btree_set<int> s1 = {1, 2};
  absl::btree_set<int> s2 = {1, 2, 3};
  EXPECT_LT(s1, s2);
  EXPECT_LE(s1, s2);
  EXPECT_GT(s2, s1);
  EXPECT_GE(s2, s1);
}

TEST(Btree, ComparableMultiset) {
  absl::btree_multiset<int> s1 = {1, 2};
  absl::btree_multiset<int> s2 = {2, 3};
  EXPECT_LT(s1, s2);
  EXPECT_LE(s1, s2);
  EXPECT_LE(s1, s1);
  EXPECT_GT(s2, s1);
  EXPECT_GE(s2, s1);
  EXPECT_GE(s1, s1);
}

TEST(Btree, ComparableMap) {
  absl::btree_map<int, int> s1 = {{1, 2}};
  absl::btree_map<int, int> s2 = {{2, 3}};
  EXPECT_LT(s1, s2);
  EXPECT_LE(s1, s2);
  EXPECT_LE(s1, s1);
  EXPECT_GT(s2, s1);
  EXPECT_GE(s2, s1);
  EXPECT_GE(s1, s1);
}

TEST(Btree, ComparableMultimap) {
  absl::btree_multimap<int, int> s1 = {{1, 2}};
  absl::btree_multimap<int, int> s2 = {{2, 3}};
  EXPECT_LT(s1, s2);
  EXPECT_LE(s1, s2);
  EXPECT_LE(s1, s1);
  EXPECT_GT(s2, s1);
  EXPECT_GE(s2, s1);
  EXPECT_GE(s1, s1);
}

TEST(Btree, ComparableSetWithCustomComparator) {
  // As specified by
  // http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2012/n3337.pdf section
  // [container.requirements.general].12, ordering associative containers always
  // uses default '<' operator
  // - even if otherwise the container uses custom functor.
  absl::btree_set<int, std::greater<int>> s1 = {1, 2};
  absl::btree_set<int, std::greater<int>> s2 = {2, 3};
  EXPECT_LT(s1, s2);
  EXPECT_LE(s1, s2);
  EXPECT_LE(s1, s1);
  EXPECT_GT(s2, s1);
  EXPECT_GE(s2, s1);
  EXPECT_GE(s1, s1);
}

TEST(Btree, EraseReturnsIterator) {
  absl::btree_set<int> set = {1, 2, 3, 4, 5};
  auto result_it = set.erase(set.begin(), set.find(3));
  EXPECT_EQ(result_it, set.find(3));
  result_it = set.erase(set.find(5));
  EXPECT_EQ(result_it, set.end());
}

TEST(Btree, ExtractAndInsertNodeHandleSet) {
  absl::btree_set<int> src1 = {1, 2, 3, 4, 5};
  auto nh = src1.extract(src1.find(3));
  EXPECT_THAT(src1, ElementsAre(1, 2, 4, 5));
  absl::btree_set<int> other;
  absl::btree_set<int>::insert_return_type res = other.insert(std::move(nh));
  EXPECT_THAT(other, ElementsAre(3));
  EXPECT_EQ(res.position, other.find(3));
  EXPECT_TRUE(res.inserted);
  EXPECT_TRUE(res.node.empty());

  absl::btree_set<int> src2 = {3, 4};
  nh = src2.extract(src2.find(3));
  EXPECT_THAT(src2, ElementsAre(4));
  res = other.insert(std::move(nh));
  EXPECT_THAT(other, ElementsAre(3));
  EXPECT_EQ(res.position, other.find(3));
  EXPECT_FALSE(res.inserted);
  ASSERT_FALSE(res.node.empty());
  EXPECT_EQ(res.node.value(), 3);
}

template <typename Set>
void TestExtractWithTrackingForSet() {
  InstanceTracker tracker;
  {
    Set s;
    // Add enough elements to make sure we test internal nodes too.
    const size_t kSize = 1000;
    while (s.size() < kSize) {
      s.insert(MovableOnlyInstance(s.size()));
    }
    for (int i = 0; i < kSize; ++i) {
      // Extract with key
      auto nh = s.extract(MovableOnlyInstance(i));
      EXPECT_EQ(s.size(), kSize - 1);
      EXPECT_EQ(nh.value().value(), i);
      // Insert with node
      s.insert(std::move(nh));
      EXPECT_EQ(s.size(), kSize);

      // Extract with iterator
      auto it = s.find(MovableOnlyInstance(i));
      nh = s.extract(it);
      EXPECT_EQ(s.size(), kSize - 1);
      EXPECT_EQ(nh.value().value(), i);
      // Insert with node and hint
      s.insert(s.begin(), std::move(nh));
      EXPECT_EQ(s.size(), kSize);
    }
  }
  EXPECT_EQ(0, tracker.instances());
}

template <typename Map>
void TestExtractWithTrackingForMap() {
  InstanceTracker tracker;
  {
    Map m;
    // Add enough elements to make sure we test internal nodes too.
    const size_t kSize = 1000;
    while (m.size() < kSize) {
      m.insert(
          {CopyableMovableInstance(m.size()), MovableOnlyInstance(m.size())});
    }
    for (int i = 0; i < kSize; ++i) {
      // Extract with key
      auto nh = m.extract(CopyableMovableInstance(i));
      EXPECT_EQ(m.size(), kSize - 1);
      EXPECT_EQ(nh.key().value(), i);
      EXPECT_EQ(nh.mapped().value(), i);
      // Insert with node
      m.insert(std::move(nh));
      EXPECT_EQ(m.size(), kSize);

      // Extract with iterator
      auto it = m.find(CopyableMovableInstance(i));
      nh = m.extract(it);
      EXPECT_EQ(m.size(), kSize - 1);
      EXPECT_EQ(nh.key().value(), i);
      EXPECT_EQ(nh.mapped().value(), i);
      // Insert with node and hint
      m.insert(m.begin(), std::move(nh));
      EXPECT_EQ(m.size(), kSize);
    }
  }
  EXPECT_EQ(0, tracker.instances());
}

TEST(Btree, ExtractTracking) {
  TestExtractWithTrackingForSet<absl::btree_set<MovableOnlyInstance>>();
  TestExtractWithTrackingForSet<absl::btree_multiset<MovableOnlyInstance>>();
  TestExtractWithTrackingForMap<
      absl::btree_map<CopyableMovableInstance, MovableOnlyInstance>>();
  TestExtractWithTrackingForMap<
      absl::btree_multimap<CopyableMovableInstance, MovableOnlyInstance>>();
}

TEST(Btree, ExtractAndInsertNodeHandleMultiSet) {
  absl::btree_multiset<int> src1 = {1, 2, 3, 3, 4, 5};
  auto nh = src1.extract(src1.find(3));
  EXPECT_THAT(src1, ElementsAre(1, 2, 3, 4, 5));
  absl::btree_multiset<int> other;
  auto res = other.insert(std::move(nh));
  EXPECT_THAT(other, ElementsAre(3));
  EXPECT_EQ(res, other.find(3));

  absl::btree_multiset<int> src2 = {3, 4};
  nh = src2.extract(src2.find(3));
  EXPECT_THAT(src2, ElementsAre(4));
  res = other.insert(std::move(nh));
  EXPECT_THAT(other, ElementsAre(3, 3));
  EXPECT_EQ(res, ++other.find(3));
}

TEST(Btree, ExtractAndInsertNodeHandleMap) {
  absl::btree_map<int, int> src1 = {{1, 2}, {3, 4}, {5, 6}};
  auto nh = src1.extract(src1.find(3));
  EXPECT_THAT(src1, ElementsAre(Pair(1, 2), Pair(5, 6)));
  absl::btree_map<int, int> other;
  absl::btree_map<int, int>::insert_return_type res =
      other.insert(std::move(nh));
  EXPECT_THAT(other, ElementsAre(Pair(3, 4)));
  EXPECT_EQ(res.position, other.find(3));
  EXPECT_TRUE(res.inserted);
  EXPECT_TRUE(res.node.empty());

  absl::btree_map<int, int> src2 = {{3, 6}};
  nh = src2.extract(src2.find(3));
  EXPECT_TRUE(src2.empty());
  res = other.insert(std::move(nh));
  EXPECT_THAT(other, ElementsAre(Pair(3, 4)));
  EXPECT_EQ(res.position, other.find(3));
  EXPECT_FALSE(res.inserted);
  ASSERT_FALSE(res.node.empty());
  EXPECT_EQ(res.node.key(), 3);
  EXPECT_EQ(res.node.mapped(), 6);
}

TEST(Btree, ExtractAndInsertNodeHandleMultiMap) {
  absl::btree_multimap<int, int> src1 = {{1, 2}, {3, 4}, {5, 6}};
  auto nh = src1.extract(src1.find(3));
  EXPECT_THAT(src1, ElementsAre(Pair(1, 2), Pair(5, 6)));
  absl::btree_multimap<int, int> other;
  auto res = other.insert(std::move(nh));
  EXPECT_THAT(other, ElementsAre(Pair(3, 4)));
  EXPECT_EQ(res, other.find(3));

  absl::btree_multimap<int, int> src2 = {{3, 6}};
  nh = src2.extract(src2.find(3));
  EXPECT_TRUE(src2.empty());
  res = other.insert(std::move(nh));
  EXPECT_THAT(other, ElementsAre(Pair(3, 4), Pair(3, 6)));
  EXPECT_EQ(res, ++other.begin());
}

TEST(Btree, ExtractMultiMapEquivalentKeys) {
  // Note: using string keys means a three-way comparator.
  absl::btree_multimap<std::string, int> map;
  for (int i = 0; i < 100; ++i) {
    for (int j = 0; j < 100; ++j) {
      map.insert({absl::StrCat(i), j});
    }
  }

  for (int i = 0; i < 100; ++i) {
    const std::string key = absl::StrCat(i);
    auto node_handle = map.extract(key);
    EXPECT_EQ(node_handle.key(), key);
    EXPECT_EQ(node_handle.mapped(), 0) << i;
  }

  for (int i = 0; i < 100; ++i) {
    const std::string key = absl::StrCat(i);
    auto node_handle = map.extract(key);
    EXPECT_EQ(node_handle.key(), key);
    EXPECT_EQ(node_handle.mapped(), 1) << i;
  }
}

TEST(Btree, ExtractAndGetNextSet) {
  absl::btree_set<int> src = {1, 2, 3, 4, 5};
  auto it = src.find(3);
  auto extracted_and_next = src.extract_and_get_next(it);
  EXPECT_THAT(src, ElementsAre(1, 2, 4, 5));
  EXPECT_EQ(extracted_and_next.node.value(), 3);
  EXPECT_EQ(*extracted_and_next.next, 4);
}

TEST(Btree, ExtractAndGetNextMultiSet) {
  absl::btree_multiset<int> src = {1, 2, 3, 4, 5};
  auto it = src.find(3);
  auto extracted_and_next = src.extract_and_get_next(it);
  EXPECT_THAT(src, ElementsAre(1, 2, 4, 5));
  EXPECT_EQ(extracted_and_next.node.value(), 3);
  EXPECT_EQ(*extracted_and_next.next, 4);
}

TEST(Btree, ExtractAndGetNextMap) {
  absl::btree_map<int, int> src = {{1, 2}, {3, 4}, {5, 6}};
  auto it = src.find(3);
  auto extracted_and_next = src.extract_and_get_next(it);
  EXPECT_THAT(src, ElementsAre(Pair(1, 2), Pair(5, 6)));
  EXPECT_EQ(extracted_and_next.node.key(), 3);
  EXPECT_EQ(extracted_and_next.node.mapped(), 4);
  EXPECT_THAT(*extracted_and_next.next, Pair(5, 6));
}

TEST(Btree, ExtractAndGetNextMultiMap) {
  absl::btree_multimap<int, int> src = {{1, 2}, {3, 4}, {5, 6}};
  auto it = src.find(3);
  auto extracted_and_next = src.extract_and_get_next(it);
  EXPECT_THAT(src, ElementsAre(Pair(1, 2), Pair(5, 6)));
  EXPECT_EQ(extracted_and_next.node.key(), 3);
  EXPECT_EQ(extracted_and_next.node.mapped(), 4);
  EXPECT_THAT(*extracted_and_next.next, Pair(5, 6));
}

TEST(Btree, ExtractAndGetNextEndIter) {
  absl::btree_set<int> src = {1, 2, 3, 4, 5};
  auto it = src.find(5);
  auto extracted_and_next = src.extract_and_get_next(it);
  EXPECT_THAT(src, ElementsAre(1, 2, 3, 4));
  EXPECT_EQ(extracted_and_next.node.value(), 5);
  EXPECT_EQ(extracted_and_next.next, src.end());
}

TEST(Btree, ExtractDoesntCauseExtraMoves) {
#ifdef _MSC_VER
  GTEST_SKIP() << "This test fails on MSVC.";
#endif

  using Set = absl::btree_set<MovableOnlyInstance>;
  std::array<std::function<void(Set &)>, 3> extracters = {
      [](Set &s) { auto node = s.extract(s.begin()); },
      [](Set &s) { auto ret = s.extract_and_get_next(s.begin()); },
      [](Set &s) { auto node = s.extract(MovableOnlyInstance(0)); }};

  InstanceTracker tracker;
  for (int i = 0; i < 3; ++i) {
    Set s;
    s.insert(MovableOnlyInstance(0));
    tracker.ResetCopiesMovesSwaps();

    extracters[i](s);
    // We expect to see exactly 1 move: from the original slot into the
    // extracted node.
    EXPECT_EQ(tracker.copies(), 0) << i;
    EXPECT_EQ(tracker.moves(), 1) << i;
    EXPECT_EQ(tracker.swaps(), 0) << i;
  }
}

// For multisets, insert with hint also affects correctness because we need to
// insert immediately before the hint if possible.
struct InsertMultiHintData {
  int key;
  int not_key;
  bool operator==(const InsertMultiHintData other) const {
    return key == other.key && not_key == other.not_key;
  }
};

struct InsertMultiHintDataKeyCompare {
  using is_transparent = void;
  bool operator()(const InsertMultiHintData a,
                  const InsertMultiHintData b) const {
    return a.key < b.key;
  }
  bool operator()(const int a, const InsertMultiHintData b) const {
    return a < b.key;
  }
  bool operator()(const InsertMultiHintData a, const int b) const {
    return a.key < b;
  }
};

TEST(Btree, InsertHintNodeHandle) {
  // For unique sets, insert with hint is just a performance optimization.
  // Test that insert works correctly when the hint is right or wrong.
  {
    absl::btree_set<int> src = {1, 2, 3, 4, 5};
    auto nh = src.extract(src.find(3));
    EXPECT_THAT(src, ElementsAre(1, 2, 4, 5));
    absl::btree_set<int> other = {0, 100};
    // Test a correct hint.
    auto it = other.insert(other.lower_bound(3), std::move(nh));
    EXPECT_THAT(other, ElementsAre(0, 3, 100));
    EXPECT_EQ(it, other.find(3));

    nh = src.extract(src.find(5));
    // Test an incorrect hint.
    it = other.insert(other.end(), std::move(nh));
    EXPECT_THAT(other, ElementsAre(0, 3, 5, 100));
    EXPECT_EQ(it, other.find(5));
  }

  absl::btree_multiset<InsertMultiHintData, InsertMultiHintDataKeyCompare> src =
      {{1, 2}, {3, 4}, {3, 5}};
  auto nh = src.extract(src.lower_bound(3));
  EXPECT_EQ(nh.value(), (InsertMultiHintData{3, 4}));
  absl::btree_multiset<InsertMultiHintData, InsertMultiHintDataKeyCompare>
      other = {{3, 1}, {3, 2}, {3, 3}};
  auto it = other.insert(--other.end(), std::move(nh));
  EXPECT_THAT(
      other, ElementsAre(InsertMultiHintData{3, 1}, InsertMultiHintData{3, 2},
                         InsertMultiHintData{3, 4}, InsertMultiHintData{3, 3}));
  EXPECT_EQ(it, --(--other.end()));

  nh = src.extract(src.find(3));
  EXPECT_EQ(nh.value(), (InsertMultiHintData{3, 5}));
  it = other.insert(other.begin(), std::move(nh));
  EXPECT_THAT(other,
              ElementsAre(InsertMultiHintData{3, 5}, InsertMultiHintData{3, 1},
                          InsertMultiHintData{3, 2}, InsertMultiHintData{3, 4},
                          InsertMultiHintData{3, 3}));
  EXPECT_EQ(it, other.begin());
}

struct IntCompareToCmp {
  absl::weak_ordering operator()(int a, int b) const {
    if (a < b) return absl::weak_ordering::less;
    if (a > b) return absl::weak_ordering::greater;
    return absl::weak_ordering::equivalent;
  }
};

TEST(Btree, MergeIntoUniqueContainers) {
  absl::btree_set<int, IntCompareToCmp> src1 = {1, 2, 3};
  absl::btree_multiset<int> src2 = {3, 4, 4, 5};
  absl::btree_set<int> dst;

  dst.merge(src1);
  EXPECT_TRUE(src1.empty());
  EXPECT_THAT(dst, ElementsAre(1, 2, 3));
  dst.merge(src2);
  EXPECT_THAT(src2, ElementsAre(3, 4));
  EXPECT_THAT(dst, ElementsAre(1, 2, 3, 4, 5));
}

TEST(Btree, MergeIntoUniqueContainersWithCompareTo) {
  absl::btree_set<int, IntCompareToCmp> src1 = {1, 2, 3};
  absl::btree_multiset<int> src2 = {3, 4, 4, 5};
  absl::btree_set<int, IntCompareToCmp> dst;

  dst.merge(src1);
  EXPECT_TRUE(src1.empty());
  EXPECT_THAT(dst, ElementsAre(1, 2, 3));
  dst.merge(src2);
  EXPECT_THAT(src2, ElementsAre(3, 4));
  EXPECT_THAT(dst, ElementsAre(1, 2, 3, 4, 5));
}

TEST(Btree, MergeIntoMultiContainers) {
  absl::btree_set<int, IntCompareToCmp> src1 = {1, 2, 3};
  absl::btree_multiset<int> src2 = {3, 4, 4, 5};
  absl::btree_multiset<int> dst;

  dst.merge(src1);
  EXPECT_TRUE(src1.empty());
  EXPECT_THAT(dst, ElementsAre(1, 2, 3));
  dst.merge(src2);
  EXPECT_TRUE(src2.empty());
  EXPECT_THAT(dst, ElementsAre(1, 2, 3, 3, 4, 4, 5));
}

TEST(Btree, MergeIntoMultiContainersWithCompareTo) {
  absl::btree_set<int, IntCompareToCmp> src1 = {1, 2, 3};
  absl::btree_multiset<int> src2 = {3, 4, 4, 5};
  absl::btree_multiset<int, IntCompareToCmp> dst;

  dst.merge(src1);
  EXPECT_TRUE(src1.empty());
  EXPECT_THAT(dst, ElementsAre(1, 2, 3));
  dst.merge(src2);
  EXPECT_TRUE(src2.empty());
  EXPECT_THAT(dst, ElementsAre(1, 2, 3, 3, 4, 4, 5));
}

TEST(Btree, MergeIntoMultiMapsWithDifferentComparators) {
  absl::btree_map<int, int, IntCompareToCmp> src1 = {{1, 1}, {2, 2}, {3, 3}};
  absl::btree_multimap<int, int, std::greater<int>> src2 = {
      {5, 5}, {4, 1}, {4, 4}, {3, 2}};
  absl::btree_multimap<int, int> dst;

  dst.merge(src1);
  EXPECT_TRUE(src1.empty());
  EXPECT_THAT(dst, ElementsAre(Pair(1, 1), Pair(2, 2), Pair(3, 3)));
  dst.merge(src2);
  EXPECT_TRUE(src2.empty());
  EXPECT_THAT(dst, ElementsAre(Pair(1, 1), Pair(2, 2), Pair(3, 3), Pair(3, 2),
                               Pair(4, 1), Pair(4, 4), Pair(5, 5)));
}

TEST(Btree, MergeIntoSetMovableOnly) {
  absl::btree_set<MovableOnlyInstance> src;
  src.insert(MovableOnlyInstance(1));
  absl::btree_multiset<MovableOnlyInstance> dst1;
  dst1.insert(MovableOnlyInstance(2));
  absl::btree_set<MovableOnlyInstance> dst2;

  // Test merge into multiset.
  dst1.merge(src);

  EXPECT_TRUE(src.empty());
  // ElementsAre/ElementsAreArray don't work with move-only types.
  ASSERT_THAT(dst1, SizeIs(2));
  EXPECT_EQ(*dst1.begin(), MovableOnlyInstance(1));
  EXPECT_EQ(*std::next(dst1.begin()), MovableOnlyInstance(2));

  // Test merge into set.
  dst2.merge(dst1);

  EXPECT_TRUE(dst1.empty());
  ASSERT_THAT(dst2, SizeIs(2));
  EXPECT_EQ(*dst2.begin(), MovableOnlyInstance(1));
  EXPECT_EQ(*std::next(dst2.begin()), MovableOnlyInstance(2));
}

struct KeyCompareToWeakOrdering {
  template <typename T>
  absl::weak_ordering operator()(const T &a, const T &b) const {
    return a < b ? absl::weak_ordering::less
                 : a == b ? absl::weak_ordering::equivalent
                          : absl::weak_ordering::greater;
  }
};

struct KeyCompareToStrongOrdering {
  template <typename T>
  absl::strong_ordering operator()(const T &a, const T &b) const {
    return a < b ? absl::strong_ordering::less
                 : a == b ? absl::strong_ordering::equal
                          : absl::strong_ordering::greater;
  }
};

TEST(Btree, UserProvidedKeyCompareToComparators) {
  absl::btree_set<int, KeyCompareToWeakOrdering> weak_set = {1, 2, 3};
  EXPECT_TRUE(weak_set.contains(2));
  EXPECT_FALSE(weak_set.contains(4));

  absl::btree_set<int, KeyCompareToStrongOrdering> strong_set = {1, 2, 3};
  EXPECT_TRUE(strong_set.contains(2));
  EXPECT_FALSE(strong_set.contains(4));
}

TEST(Btree, TryEmplaceBasicTest) {
  absl::btree_map<int, std::string> m;

  // Should construct a string from the literal.
  m.try_emplace(1, "one");
  EXPECT_EQ(1, m.size());

  // Try other string constructors and const lvalue key.
  const int key(42);
  m.try_emplace(key, 3, 'a');
  m.try_emplace(2, std::string("two"));

  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));
  EXPECT_THAT(m, ElementsAreArray(std::vector<std::pair<int, std::string>>{
                     {1, "one"}, {2, "two"}, {42, "aaa"}}));
}

TEST(Btree, TryEmplaceWithHintWorks) {
  // Use a counting comparator here to verify that hint is used.
  int calls = 0;
  auto cmp = [&calls](int x, int y) {
    ++calls;
    return x < y;
  };
  using Cmp = decltype(cmp);

  // Use a map that is opted out of key_compare being adapted so we can expect
  // strict comparison call limits.
  absl::btree_map<int, int, CheckedCompareOptedOutCmp<Cmp>> m(cmp);
  for (int i = 0; i < 128; ++i) {
    m.emplace(i, i);
  }

  // Sanity check for the comparator
  calls = 0;
  m.emplace(127, 127);
  EXPECT_GE(calls, 4);

  // Try with begin hint:
  calls = 0;
  auto it = m.try_emplace(m.begin(), -1, -1);
  EXPECT_EQ(129, m.size());
  EXPECT_EQ(it, m.begin());
  EXPECT_LE(calls, 2);

  // Try with end hint:
  calls = 0;
  std::pair<int, int> pair1024 = {1024, 1024};
  it = m.try_emplace(m.end(), pair1024.first, pair1024.second);
  EXPECT_EQ(130, m.size());
  EXPECT_EQ(it, --m.end());
  EXPECT_LE(calls, 2);

  // Try value already present, bad hint; ensure no duplicate added:
  calls = 0;
  it = m.try_emplace(m.end(), 16, 17);
  EXPECT_EQ(130, m.size());
  EXPECT_GE(calls, 4);
  EXPECT_EQ(it, m.find(16));

  // Try value already present, hint points directly to it:
  calls = 0;
  it = m.try_emplace(it, 16, 17);
  EXPECT_EQ(130, m.size());
  EXPECT_LE(calls, 2);
  EXPECT_EQ(it, m.find(16));

  m.erase(2);
  EXPECT_EQ(129, m.size());
  auto hint = m.find(3);
  // Try emplace in the middle of two other elements.
  calls = 0;
  m.try_emplace(hint, 2, 2);
  EXPECT_EQ(130, m.size());
  EXPECT_LE(calls, 2);

  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));
}

TEST(Btree, TryEmplaceWithBadHint) {
  absl::btree_map<int, int> m = {{1, 1}, {9, 9}};

  // Bad hint (too small), should still emplace:
  auto it = m.try_emplace(m.begin(), 2, 2);
  EXPECT_EQ(it, ++m.begin());
  EXPECT_THAT(m, ElementsAreArray(
                     std::vector<std::pair<int, int>>{{1, 1}, {2, 2}, {9, 9}}));

  // Bad hint, too large this time:
  it = m.try_emplace(++(++m.begin()), 0, 0);
  EXPECT_EQ(it, m.begin());
  EXPECT_THAT(m, ElementsAreArray(std::vector<std::pair<int, int>>{
                     {0, 0}, {1, 1}, {2, 2}, {9, 9}}));
}

TEST(Btree, TryEmplaceMaintainsSortedOrder) {
  absl::btree_map<int, std::string> m;
  std::pair<int, std::string> pair5 = {5, "five"};

  // Test both lvalue & rvalue emplace.
  m.try_emplace(10, "ten");
  m.try_emplace(pair5.first, pair5.second);
  EXPECT_EQ(2, m.size());
  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));

  int int100{100};
  m.try_emplace(int100, "hundred");
  m.try_emplace(1, "one");
  EXPECT_EQ(4, m.size());
  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));
}

TEST(Btree, TryEmplaceWithHintAndNoValueArgsWorks) {
  absl::btree_map<int, int> m;
  m.try_emplace(m.end(), 1);
  EXPECT_EQ(0, m[1]);
}

TEST(Btree, TryEmplaceWithHintAndMultipleValueArgsWorks) {
  absl::btree_map<int, std::string> m;
  m.try_emplace(m.end(), 1, 10, 'a');
  EXPECT_EQ(std::string(10, 'a'), m[1]);
}

template <typename Alloc>
using BtreeSetAlloc = absl::btree_set<int, std::less<int>, Alloc>;

TEST(Btree, AllocatorPropagation) {
  TestAllocPropagation<BtreeSetAlloc>();
}

TEST(Btree, MinimumAlignmentAllocator) {
  absl::btree_set<int8_t, std::less<int8_t>, MinimumAlignmentAlloc<int8_t>> set;

  // Do some basic operations. Test that everything is fine when allocator uses
  // minimal alignment.
  for (int8_t i = 0; i < 100; ++i) set.insert(i);
  set.erase(set.find(50), set.end());
  for (int8_t i = 51; i < 101; ++i) set.insert(i);

  EXPECT_EQ(set.size(), 100);
}

TEST(Btree, EmptyTree) {
  absl::btree_set<int> s;
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(s.size(), 0);
  EXPECT_GT(s.max_size(), 0);
}

bool IsEven(int k) { return k % 2 == 0; }

TEST(Btree, EraseIf) {
  // Test that erase_if works with all the container types and supports lambdas.
  {
    absl::btree_set<int> s = {1, 3, 5, 6, 100};
    EXPECT_EQ(erase_if(s, [](int k) { return k > 3; }), 3);
    EXPECT_THAT(s, ElementsAre(1, 3));
  }
  {
    absl::btree_multiset<int> s = {1, 3, 3, 5, 6, 6, 100};
    EXPECT_EQ(erase_if(s, [](int k) { return k <= 3; }), 3);
    EXPECT_THAT(s, ElementsAre(5, 6, 6, 100));
  }
  {
    absl::btree_map<int, int> m = {{1, 1}, {3, 3}, {6, 6}, {100, 100}};
    EXPECT_EQ(
        erase_if(m, [](std::pair<const int, int> kv) { return kv.first > 3; }),
        2);
    EXPECT_THAT(m, ElementsAre(Pair(1, 1), Pair(3, 3)));
  }
  {
    absl::btree_multimap<int, int> m = {{1, 1}, {3, 3}, {3, 6},
                                        {6, 6}, {6, 7}, {100, 6}};
    EXPECT_EQ(
        erase_if(m,
                 [](std::pair<const int, int> kv) { return kv.second == 6; }),
        3);
    EXPECT_THAT(m, ElementsAre(Pair(1, 1), Pair(3, 3), Pair(6, 7)));
  }
  // Test that erasing all elements from a large set works and test support for
  // function pointers.
  {
    absl::btree_set<int> s;
    for (int i = 0; i < 1000; ++i) s.insert(2 * i);
    EXPECT_EQ(erase_if(s, IsEven), 1000);
    EXPECT_THAT(s, IsEmpty());
  }
  // Test that erase_if supports other format of function pointers.
  {
    absl::btree_set<int> s = {1, 3, 5, 6, 100};
    EXPECT_EQ(erase_if(s, &IsEven), 2);
    EXPECT_THAT(s, ElementsAre(1, 3, 5));
  }
  // Test that erase_if invokes the predicate once per element.
  {
    absl::btree_set<int> s;
    for (int i = 0; i < 1000; ++i) s.insert(i);
    int pred_calls = 0;
    EXPECT_EQ(erase_if(s,
                       [&pred_calls](int k) {
                         ++pred_calls;
                         return k % 2;
                       }),
              500);
    EXPECT_THAT(s, SizeIs(500));
    EXPECT_EQ(pred_calls, 1000);
  }
}

TEST(Btree, InsertOrAssign) {
  absl::btree_map<int, int> m = {{1, 1}, {3, 3}};
  using value_type = typename decltype(m)::value_type;

  auto ret = m.insert_or_assign(4, 4);
  EXPECT_EQ(*ret.first, value_type(4, 4));
  EXPECT_TRUE(ret.second);
  ret = m.insert_or_assign(3, 100);
  EXPECT_EQ(*ret.first, value_type(3, 100));
  EXPECT_FALSE(ret.second);

  auto hint_ret = m.insert_or_assign(ret.first, 3, 200);
  EXPECT_EQ(*hint_ret, value_type(3, 200));
  hint_ret = m.insert_or_assign(m.find(1), 0, 1);
  EXPECT_EQ(*hint_ret, value_type(0, 1));
  // Test with bad hint.
  hint_ret = m.insert_or_assign(m.end(), -1, 1);
  EXPECT_EQ(*hint_ret, value_type(-1, 1));

  EXPECT_THAT(m, ElementsAre(Pair(-1, 1), Pair(0, 1), Pair(1, 1), Pair(3, 200),
                             Pair(4, 4)));
}

TEST(Btree, InsertOrAssignMovableOnly) {
  absl::btree_map<int, MovableOnlyInstance> m;
  using value_type = typename decltype(m)::value_type;

  auto ret = m.insert_or_assign(4, MovableOnlyInstance(4));
  EXPECT_EQ(*ret.first, value_type(4, MovableOnlyInstance(4)));
  EXPECT_TRUE(ret.second);
  ret = m.insert_or_assign(4, MovableOnlyInstance(100));
  EXPECT_EQ(*ret.first, value_type(4, MovableOnlyInstance(100)));
  EXPECT_FALSE(ret.second);

  auto hint_ret = m.insert_or_assign(ret.first, 3, MovableOnlyInstance(200));
  EXPECT_EQ(*hint_ret, value_type(3, MovableOnlyInstance(200)));

  EXPECT_EQ(m.size(), 2);
}

TEST(Btree, BitfieldArgument) {
  union {
    int n : 1;
  };
  n = 0;
  absl::btree_map<int, int> m;
  m.erase(n);
  m.count(n);
  m.find(n);
  m.contains(n);
  m.equal_range(n);
  m.insert_or_assign(n, n);
  m.insert_or_assign(m.end(), n, n);
  m.try_emplace(n);
  m.try_emplace(m.end(), n);
  m.at(n);
  m[n];
}

TEST(Btree, SetRangeConstructorAndInsertSupportExplicitConversionComparable) {
  const absl::string_view names[] = {"n1", "n2"};

  absl::btree_set<std::string> name_set1{std::begin(names), std::end(names)};
  EXPECT_THAT(name_set1, ElementsAreArray(names));

  absl::btree_set<std::string> name_set2;
  name_set2.insert(std::begin(names), std::end(names));
  EXPECT_THAT(name_set2, ElementsAreArray(names));
}

// A type that is explicitly convertible from int and counts constructor calls.
struct ConstructorCounted {
  explicit ConstructorCounted(int i) : i(i) { ++constructor_calls; }
  bool operator==(int other) const { return i == other; }

  int i;
  static int constructor_calls;
};
int ConstructorCounted::constructor_calls = 0;

struct ConstructorCountedCompare {
  bool operator()(int a, const ConstructorCounted &b) const { return a < b.i; }
  bool operator()(const ConstructorCounted &a, int b) const { return a.i < b; }
  bool operator()(const ConstructorCounted &a,
                  const ConstructorCounted &b) const {
    return a.i < b.i;
  }
  using is_transparent = void;
};

TEST(Btree,
     SetRangeConstructorAndInsertExplicitConvComparableLimitConstruction) {
  const int i[] = {0, 1, 1};
  ConstructorCounted::constructor_calls = 0;

  absl::btree_set<ConstructorCounted, ConstructorCountedCompare> set{
      std::begin(i), std::end(i)};
  EXPECT_THAT(set, ElementsAre(0, 1));
  EXPECT_EQ(ConstructorCounted::constructor_calls, 2);

  set.insert(std::begin(i), std::end(i));
  EXPECT_THAT(set, ElementsAre(0, 1));
  EXPECT_EQ(ConstructorCounted::constructor_calls, 2);
}

TEST(Btree,
     SetRangeConstructorAndInsertSupportExplicitConversionNonComparable) {
  const int i[] = {0, 1};

  absl::btree_set<std::vector<void *>> s1{std::begin(i), std::end(i)};
  EXPECT_THAT(s1, ElementsAre(IsEmpty(), ElementsAre(IsNull())));

  absl::btree_set<std::vector<void *>> s2;
  s2.insert(std::begin(i), std::end(i));
  EXPECT_THAT(s2, ElementsAre(IsEmpty(), ElementsAre(IsNull())));
}

// libstdc++ included with GCC 4.9 has a bug in the std::pair constructors that
// prevents explicit conversions between pair types.
// We only run this test for the libstdc++ from GCC 7 or newer because we can't
// reliably check the libstdc++ version prior to that release.
#if !defined(__GLIBCXX__) || \
    (defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 7)
TEST(Btree, MapRangeConstructorAndInsertSupportExplicitConversionComparable) {
  const std::pair<absl::string_view, int> names[] = {{"n1", 1}, {"n2", 2}};

  absl::btree_map<std::string, int> name_map1{std::begin(names),
                                              std::end(names)};
  EXPECT_THAT(name_map1, ElementsAre(Pair("n1", 1), Pair("n2", 2)));

  absl::btree_map<std::string, int> name_map2;
  name_map2.insert(std::begin(names), std::end(names));
  EXPECT_THAT(name_map2, ElementsAre(Pair("n1", 1), Pair("n2", 2)));
}

TEST(Btree,
     MapRangeConstructorAndInsertExplicitConvComparableLimitConstruction) {
  const std::pair<int, int> i[] = {{0, 1}, {1, 2}, {1, 3}};
  ConstructorCounted::constructor_calls = 0;

  absl::btree_map<ConstructorCounted, int, ConstructorCountedCompare> map{
      std::begin(i), std::end(i)};
  EXPECT_THAT(map, ElementsAre(Pair(0, 1), Pair(1, 2)));
  EXPECT_EQ(ConstructorCounted::constructor_calls, 2);

  map.insert(std::begin(i), std::end(i));
  EXPECT_THAT(map, ElementsAre(Pair(0, 1), Pair(1, 2)));
  EXPECT_EQ(ConstructorCounted::constructor_calls, 2);
}

TEST(Btree,
     MapRangeConstructorAndInsertSupportExplicitConversionNonComparable) {
  const std::pair<int, int> i[] = {{0, 1}, {1, 2}};

  absl::btree_map<std::vector<void *>, int> m1{std::begin(i), std::end(i)};
  EXPECT_THAT(m1,
              ElementsAre(Pair(IsEmpty(), 1), Pair(ElementsAre(IsNull()), 2)));

  absl::btree_map<std::vector<void *>, int> m2;
  m2.insert(std::begin(i), std::end(i));
  EXPECT_THAT(m2,
              ElementsAre(Pair(IsEmpty(), 1), Pair(ElementsAre(IsNull()), 2)));
}

TEST(Btree, HeterogeneousTryEmplace) {
  absl::btree_map<std::string, int> m;
  std::string s = "key";
  absl::string_view sv = s;
  m.try_emplace(sv, 1);
  EXPECT_EQ(m[s], 1);

  m.try_emplace(m.end(), sv, 2);
  EXPECT_EQ(m[s], 1);
}

TEST(Btree, HeterogeneousOperatorMapped) {
  absl::btree_map<std::string, int> m;
  std::string s = "key";
  absl::string_view sv = s;
  m[sv] = 1;
  EXPECT_EQ(m[s], 1);

  m[sv] = 2;
  EXPECT_EQ(m[s], 2);
}

TEST(Btree, HeterogeneousInsertOrAssign) {
  absl::btree_map<std::string, int> m;
  std::string s = "key";
  absl::string_view sv = s;
  m.insert_or_assign(sv, 1);
  EXPECT_EQ(m[s], 1);

  m.insert_or_assign(m.end(), sv, 2);
  EXPECT_EQ(m[s], 2);
}
#endif

// This test requires std::launder for mutable key access in node handles.
#if defined(__cpp_lib_launder) && __cpp_lib_launder >= 201606
TEST(Btree, NodeHandleMutableKeyAccess) {
  {
    absl::btree_map<std::string, std::string> map;

    map["key1"] = "mapped";

    auto nh = map.extract(map.begin());
    nh.key().resize(3);
    map.insert(std::move(nh));

    EXPECT_THAT(map, ElementsAre(Pair("key", "mapped")));
  }
  // Also for multimap.
  {
    absl::btree_multimap<std::string, std::string> map;

    map.emplace("key1", "mapped");

    auto nh = map.extract(map.begin());
    nh.key().resize(3);
    map.insert(std::move(nh));

    EXPECT_THAT(map, ElementsAre(Pair("key", "mapped")));
  }
}
#endif

struct MultiKey {
  int i1;
  int i2;
};

bool operator==(const MultiKey a, const MultiKey b) {
  return a.i1 == b.i1 && a.i2 == b.i2;
}

// A heterogeneous comparator that has different equivalence classes for
// different lookup types.
struct MultiKeyComp {
  using is_transparent = void;
  bool operator()(const MultiKey a, const MultiKey b) const {
    if (a.i1 != b.i1) return a.i1 < b.i1;
    return a.i2 < b.i2;
  }
  bool operator()(const int a, const MultiKey b) const { return a < b.i1; }
  bool operator()(const MultiKey a, const int b) const { return a.i1 < b; }
};

// A heterogeneous, three-way comparator that has different equivalence classes
// for different lookup types.
struct MultiKeyThreeWayComp {
  using is_transparent = void;
  absl::weak_ordering operator()(const MultiKey a, const MultiKey b) const {
    if (a.i1 < b.i1) return absl::weak_ordering::less;
    if (a.i1 > b.i1) return absl::weak_ordering::greater;
    if (a.i2 < b.i2) return absl::weak_ordering::less;
    if (a.i2 > b.i2) return absl::weak_ordering::greater;
    return absl::weak_ordering::equivalent;
  }
  absl::weak_ordering operator()(const int a, const MultiKey b) const {
    if (a < b.i1) return absl::weak_ordering::less;
    if (a > b.i1) return absl::weak_ordering::greater;
    return absl::weak_ordering::equivalent;
  }
  absl::weak_ordering operator()(const MultiKey a, const int b) const {
    if (a.i1 < b) return absl::weak_ordering::less;
    if (a.i1 > b) return absl::weak_ordering::greater;
    return absl::weak_ordering::equivalent;
  }
};

template <typename Compare>
class BtreeMultiKeyTest : public ::testing::Test {};
using MultiKeyComps = ::testing::Types<MultiKeyComp, MultiKeyThreeWayComp>;
TYPED_TEST_SUITE(BtreeMultiKeyTest, MultiKeyComps);

TYPED_TEST(BtreeMultiKeyTest, EqualRange) {
  absl::btree_set<MultiKey, TypeParam> set;
  for (int i = 0; i < 100; ++i) {
    for (int j = 0; j < 100; ++j) {
      set.insert({i, j});
    }
  }

  for (int i = 0; i < 100; ++i) {
    auto equal_range = set.equal_range(i);
    EXPECT_EQ(equal_range.first->i1, i);
    EXPECT_EQ(equal_range.first->i2, 0) << i;
    EXPECT_EQ(std::distance(equal_range.first, equal_range.second), 100) << i;
  }
}

TYPED_TEST(BtreeMultiKeyTest, Extract) {
  absl::btree_set<MultiKey, TypeParam> set;
  for (int i = 0; i < 100; ++i) {
    for (int j = 0; j < 100; ++j) {
      set.insert({i, j});
    }
  }

  for (int i = 0; i < 100; ++i) {
    auto node_handle = set.extract(i);
    EXPECT_EQ(node_handle.value().i1, i);
    EXPECT_EQ(node_handle.value().i2, 0) << i;
  }

  for (int i = 0; i < 100; ++i) {
    auto node_handle = set.extract(i);
    EXPECT_EQ(node_handle.value().i1, i);
    EXPECT_EQ(node_handle.value().i2, 1) << i;
  }
}

TYPED_TEST(BtreeMultiKeyTest, Erase) {
  absl::btree_set<MultiKey, TypeParam> set = {
      {1, 1}, {2, 1}, {2, 2}, {3, 1}};
  EXPECT_EQ(set.erase(2), 2);
  EXPECT_THAT(set, ElementsAre(MultiKey{1, 1}, MultiKey{3, 1}));
}

TYPED_TEST(BtreeMultiKeyTest, Count) {
  const absl::btree_set<MultiKey, TypeParam> set = {
      {1, 1}, {2, 1}, {2, 2}, {3, 1}};
  EXPECT_EQ(set.count(2), 2);
}

TEST(Btree, SetIteratorsAreConst) {
  using Set = absl::btree_set<int>;
  EXPECT_TRUE(
      (std::is_same<typename Set::iterator::reference, const int &>::value));
  EXPECT_TRUE(
      (std::is_same<typename Set::iterator::pointer, const int *>::value));

  using MSet = absl::btree_multiset<int>;
  EXPECT_TRUE(
      (std::is_same<typename MSet::iterator::reference, const int &>::value));
  EXPECT_TRUE(
      (std::is_same<typename MSet::iterator::pointer, const int *>::value));
}

TEST(Btree, AllocConstructor) {
  using Alloc = CountingAllocator<int>;
  using Set = absl::btree_set<int, std::less<int>, Alloc>;
  int64_t bytes_used = 0;
  Alloc alloc(&bytes_used);
  Set set(alloc);

  set.insert({1, 2, 3});

  EXPECT_THAT(set, ElementsAre(1, 2, 3));
  EXPECT_GT(bytes_used, set.size() * sizeof(int));
}

TEST(Btree, AllocInitializerListConstructor) {
  using Alloc = CountingAllocator<int>;
  using Set = absl::btree_set<int, std::less<int>, Alloc>;
  int64_t bytes_used = 0;
  Alloc alloc(&bytes_used);
  Set set({1, 2, 3}, alloc);

  EXPECT_THAT(set, ElementsAre(1, 2, 3));
  EXPECT_GT(bytes_used, set.size() * sizeof(int));
}

TEST(Btree, AllocRangeConstructor) {
  using Alloc = CountingAllocator<int>;
  using Set = absl::btree_set<int, std::less<int>, Alloc>;
  int64_t bytes_used = 0;
  Alloc alloc(&bytes_used);
  std::vector<int> v = {1, 2, 3};
  Set set(v.begin(), v.end(), alloc);

  EXPECT_THAT(set, ElementsAre(1, 2, 3));
  EXPECT_GT(bytes_used, set.size() * sizeof(int));
}

TEST(Btree, AllocCopyConstructor) {
  using Alloc = CountingAllocator<int>;
  using Set = absl::btree_set<int, std::less<int>, Alloc>;
  int64_t bytes_used1 = 0;
  Alloc alloc1(&bytes_used1);
  Set set1(alloc1);

  set1.insert({1, 2, 3});

  int64_t bytes_used2 = 0;
  Alloc alloc2(&bytes_used2);
  Set set2(set1, alloc2);

  EXPECT_THAT(set1, ElementsAre(1, 2, 3));
  EXPECT_THAT(set2, ElementsAre(1, 2, 3));
  EXPECT_GT(bytes_used1, set1.size() * sizeof(int));
  EXPECT_EQ(bytes_used1, bytes_used2);
}

TEST(Btree, AllocMoveConstructor_SameAlloc) {
  using Alloc = CountingAllocator<int>;
  using Set = absl::btree_set<int, std::less<int>, Alloc>;
  int64_t bytes_used = 0;
  Alloc alloc(&bytes_used);
  Set set1(alloc);

  set1.insert({1, 2, 3});

  const int64_t original_bytes_used = bytes_used;
  EXPECT_GT(original_bytes_used, set1.size() * sizeof(int));

  Set set2(std::move(set1), alloc);

  EXPECT_THAT(set2, ElementsAre(1, 2, 3));
  EXPECT_EQ(bytes_used, original_bytes_used);
}

TEST(Btree, AllocMoveConstructor_DifferentAlloc) {
  using Alloc = CountingAllocator<int>;
  using Set = absl::btree_set<int, std::less<int>, Alloc>;
  int64_t bytes_used1 = 0;
  Alloc alloc1(&bytes_used1);
  Set set1(alloc1);

  set1.insert({1, 2, 3});

  const int64_t original_bytes_used = bytes_used1;
  EXPECT_GT(original_bytes_used, set1.size() * sizeof(int));

  int64_t bytes_used2 = 0;
  Alloc alloc2(&bytes_used2);
  Set set2(std::move(set1), alloc2);

  EXPECT_THAT(set2, ElementsAre(1, 2, 3));
  // We didn't free these bytes allocated by `set1` yet.
  EXPECT_EQ(bytes_used1, original_bytes_used);
  EXPECT_EQ(bytes_used2, original_bytes_used);
}

bool IntCmp(const int a, const int b) { return a < b; }

TEST(Btree, SupportsFunctionPtrComparator) {
  absl::btree_set<int, decltype(IntCmp) *> set(IntCmp);
  set.insert({1, 2, 3});
  EXPECT_THAT(set, ElementsAre(1, 2, 3));
  EXPECT_TRUE(set.key_comp()(1, 2));
  EXPECT_TRUE(set.value_comp()(1, 2));

  absl::btree_map<int, int, decltype(IntCmp) *> map(&IntCmp);
  map[1] = 1;
  EXPECT_THAT(map, ElementsAre(Pair(1, 1)));
  EXPECT_TRUE(map.key_comp()(1, 2));
  EXPECT_TRUE(map.value_comp()(std::make_pair(1, 1), std::make_pair(2, 2)));
}

template <typename Compare>
struct TransparentPassThroughComp {
  using is_transparent = void;

  // This will fail compilation if we attempt a comparison that Compare does not
  // support, and the failure will happen inside the function implementation so
  // it can't be avoided by using SFINAE on this comparator.
  template <typename T, typename U>
  bool operator()(const T &lhs, const U &rhs) const {
    return Compare()(lhs, rhs);
  }
};

TEST(Btree,
     SupportsTransparentComparatorThatDoesNotImplementAllVisibleOperators) {
  absl::btree_set<MultiKey, TransparentPassThroughComp<MultiKeyComp>> set;
  set.insert(MultiKey{1, 2});
  EXPECT_TRUE(set.contains(1));
}

TEST(Btree, ConstructImplicitlyWithUnadaptedComparator) {
  absl::btree_set<MultiKey, MultiKeyComp> set = {{}, MultiKeyComp{}};
}

TEST(Btree, InvalidComparatorsCaught) {
  if (!IsAssertEnabled()) GTEST_SKIP() << "Assertions not enabled.";

  {
    struct ZeroAlwaysLessCmp {
      bool operator()(int lhs, int rhs) const {
        if (lhs == 0) return true;
        return lhs < rhs;
      }
    };
    absl::btree_set<int, ZeroAlwaysLessCmp> set;
    EXPECT_DEATH(set.insert({0, 1, 2}), "is_self_equivalent");
  }
  {
    struct ThreeWayAlwaysLessCmp {
      absl::weak_ordering operator()(int, int) const {
        return absl::weak_ordering::less;
      }
    };
    absl::btree_set<int, ThreeWayAlwaysLessCmp> set;
    EXPECT_DEATH(set.insert({0, 1, 2}), "is_self_equivalent");
  }
  {
    struct SumGreaterZeroCmp {
      bool operator()(int lhs, int rhs) const {
        // First, do equivalence correctly - so we can test later condition.
        if (lhs == rhs) return false;
        return lhs + rhs > 0;
      }
    };
    absl::btree_set<int, SumGreaterZeroCmp> set;
    // Note: '!' only needs to be escaped when it's the first character.
    EXPECT_DEATH(set.insert({0, 1, 2}),
                 R"regex(\!lhs_comp_rhs \|\| !comp\(\)\(rhs, lhs\))regex");
  }
  {
    struct ThreeWaySumGreaterZeroCmp {
      absl::weak_ordering operator()(int lhs, int rhs) const {
        // First, do equivalence correctly - so we can test later condition.
        if (lhs == rhs) return absl::weak_ordering::equivalent;

        if (lhs + rhs > 0) return absl::weak_ordering::less;
        if (lhs + rhs == 0) return absl::weak_ordering::equivalent;
        return absl::weak_ordering::greater;
      }
    };
    absl::btree_set<int, ThreeWaySumGreaterZeroCmp> set;
    EXPECT_DEATH(set.insert({0, 1, 2}), "lhs_comp_rhs < 0 -> rhs_comp_lhs > 0");
  }
  // Verify that we detect cases of comparators that violate transitivity.
  // When the comparators below check for the presence of an optional field,
  // they violate transitivity because instances that have the optional field
  // compare differently with each other from how they compare with instances
  // that don't have the optional field.
  struct ClockTime {
    absl::optional<int> hour;
    int minute;
  };
  // `comp(a,b) && comp(b,c) && !comp(a,c)` violates transitivity.
  ClockTime a = {absl::nullopt, 1};
  ClockTime b = {2, 5};
  ClockTime c = {6, 0};
  {
    struct NonTransitiveTimeCmp {
      bool operator()(ClockTime lhs, ClockTime rhs) const {
        if (lhs.hour.has_value() && rhs.hour.has_value() &&
            *lhs.hour != *rhs.hour) {
          return *lhs.hour < *rhs.hour;
        }
        return lhs.minute < rhs.minute;
      }
    };
    NonTransitiveTimeCmp cmp;
    ASSERT_TRUE(cmp(a, b) && cmp(b, c) && !cmp(a, c));
    absl::btree_set<ClockTime, NonTransitiveTimeCmp> set;
    EXPECT_DEATH(set.insert({a, b, c}), "is_ordered_correctly");
    absl::btree_multiset<ClockTime, NonTransitiveTimeCmp> mset;
    EXPECT_DEATH(mset.insert({a, a, b, b, c, c}), "is_ordered_correctly");
  }
  {
    struct ThreeWayNonTransitiveTimeCmp {
      absl::weak_ordering operator()(ClockTime lhs, ClockTime rhs) const {
        if (lhs.hour.has_value() && rhs.hour.has_value() &&
            *lhs.hour != *rhs.hour) {
          return *lhs.hour < *rhs.hour ? absl::weak_ordering::less
                                       : absl::weak_ordering::greater;
        }
        return lhs.minute < rhs.minute    ? absl::weak_ordering::less
               : lhs.minute == rhs.minute ? absl::weak_ordering::equivalent
                                          : absl::weak_ordering::greater;
      }
    };
    ThreeWayNonTransitiveTimeCmp cmp;
    ASSERT_TRUE(cmp(a, b) < 0 && cmp(b, c) < 0 && cmp(a, c) > 0);
    absl::btree_set<ClockTime, ThreeWayNonTransitiveTimeCmp> set;
    EXPECT_DEATH(set.insert({a, b, c}), "is_ordered_correctly");
    absl::btree_multiset<ClockTime, ThreeWayNonTransitiveTimeCmp> mset;
    EXPECT_DEATH(mset.insert({a, a, b, b, c, c}), "is_ordered_correctly");
  }
}

TEST(Btree, MutatedKeysCaught) {
  if (!IsAssertEnabled()) GTEST_SKIP() << "Assertions not enabled.";

  struct IntPtrCmp {
    bool operator()(int *lhs, int *rhs) const { return *lhs < *rhs; }
  };
  {
    absl::btree_set<int *, IntPtrCmp> set;
    int arr[] = {0, 1, 2};
    set.insert({&arr[0], &arr[1], &arr[2]});
    arr[0] = 100;
    EXPECT_DEATH(set.insert(&arr[0]), "is_ordered_correctly");
  }
  {
    absl::btree_multiset<int *, IntPtrCmp> set;
    int arr[] = {0, 1, 2};
    set.insert({&arr[0], &arr[0], &arr[1], &arr[1], &arr[2], &arr[2]});
    arr[0] = 100;
    EXPECT_DEATH(set.insert(&arr[0]), "is_ordered_correctly");
  }
}

#ifndef _MSC_VER
// This test crashes on MSVC.
TEST(Btree, InvalidIteratorUse) {
  if (!BtreeGenerationsEnabled())
    GTEST_SKIP() << "Generation validation for iterators is disabled.";

  // Invalid memory use can trigger use-after-free in ASan, HWASAN or
  // invalidated iterator assertions.
  constexpr const char *kInvalidMemoryDeathMessage =
      "use-after-free|invalidated iterator";

  {
    absl::btree_set<int> set;
    for (int i = 0; i < 10; ++i) set.insert(i);
    auto it = set.begin();
    set.erase(it++);
    EXPECT_DEATH(set.erase(it++), kInvalidMemoryDeathMessage);
  }
  {
    absl::btree_set<int> set;
    for (int i = 0; i < 10; ++i) set.insert(i);
    auto it = set.insert(20).first;
    set.insert(30);
    EXPECT_DEATH(*it, kInvalidMemoryDeathMessage);
  }
  {
    absl::btree_set<int> set;
    for (int i = 0; i < 10000; ++i) set.insert(i);
    auto it = set.find(5000);
    ASSERT_NE(it, set.end());
    set.erase(1);
    EXPECT_DEATH(*it, kInvalidMemoryDeathMessage);
  }
  {
    absl::btree_set<int> set;
    for (int i = 0; i < 10; ++i) set.insert(i);
    auto it = set.insert(20).first;
    set.insert(30);
    EXPECT_DEATH(void(it == set.begin()), kInvalidMemoryDeathMessage);
    EXPECT_DEATH(void(set.begin() == it), kInvalidMemoryDeathMessage);
  }
}
#endif

class OnlyConstructibleByAllocator {
  explicit OnlyConstructibleByAllocator(int i) : i_(i) {}

 public:
  OnlyConstructibleByAllocator(const OnlyConstructibleByAllocator &other)
      : i_(other.i_) {}
  OnlyConstructibleByAllocator &operator=(
      const OnlyConstructibleByAllocator &other) {
    i_ = other.i_;
    return *this;
  }
  int Get() const { return i_; }
  bool operator==(int i) const { return i_ == i; }

 private:
  template <typename T>
  friend class OnlyConstructibleAllocator;

  int i_;
};

template <typename T = OnlyConstructibleByAllocator>
class OnlyConstructibleAllocator : public std::allocator<T> {
 public:
  OnlyConstructibleAllocator() = default;
  template <class U>
  explicit OnlyConstructibleAllocator(const OnlyConstructibleAllocator<U> &) {}

  void construct(OnlyConstructibleByAllocator *p, int i) {
    new (p) OnlyConstructibleByAllocator(i);
  }
  template <typename Pair>
  void construct(Pair *p, const int i) {
    OnlyConstructibleByAllocator only(i);
    new (p) Pair(std::move(only), i);
  }

  template <class U>
  struct rebind {
    using other = OnlyConstructibleAllocator<U>;
  };
};

struct OnlyConstructibleByAllocatorComp {
  using is_transparent = void;
  bool operator()(OnlyConstructibleByAllocator a,
                  OnlyConstructibleByAllocator b) const {
    return a.Get() < b.Get();
  }
  bool operator()(int a, OnlyConstructibleByAllocator b) const {
    return a < b.Get();
  }
  bool operator()(OnlyConstructibleByAllocator a, int b) const {
    return a.Get() < b;
  }
};

TEST(Btree, OnlyConstructibleByAllocatorType) {
  const std::array<int, 2> arr = {3, 4};
  {
    absl::btree_set<OnlyConstructibleByAllocator,
                    OnlyConstructibleByAllocatorComp,
                    OnlyConstructibleAllocator<>>
        set;
    set.emplace(1);
    set.emplace_hint(set.end(), 2);
    set.insert(arr.begin(), arr.end());
    EXPECT_THAT(set, ElementsAre(1, 2, 3, 4));
  }
  {
    absl::btree_multiset<OnlyConstructibleByAllocator,
                         OnlyConstructibleByAllocatorComp,
                         OnlyConstructibleAllocator<>>
        set;
    set.emplace(1);
    set.emplace_hint(set.end(), 2);
    // TODO(ezb): fix insert_multi to allow this to compile.
    // set.insert(arr.begin(), arr.end());
    EXPECT_THAT(set, ElementsAre(1, 2));
  }
  {
    absl::btree_map<OnlyConstructibleByAllocator, int,
                    OnlyConstructibleByAllocatorComp,
                    OnlyConstructibleAllocator<>>
        map;
    map.emplace(1);
    map.emplace_hint(map.end(), 2);
    map.insert(arr.begin(), arr.end());
    EXPECT_THAT(map,
                ElementsAre(Pair(1, 1), Pair(2, 2), Pair(3, 3), Pair(4, 4)));
  }
  {
    absl::btree_multimap<OnlyConstructibleByAllocator, int,
                         OnlyConstructibleByAllocatorComp,
                         OnlyConstructibleAllocator<>>
        map;
    map.emplace(1);
    map.emplace_hint(map.end(), 2);
    // TODO(ezb): fix insert_multi to allow this to compile.
    // map.insert(arr.begin(), arr.end());
    EXPECT_THAT(map, ElementsAre(Pair(1, 1), Pair(2, 2)));
  }
}

class NotAssignable {
 public:
  explicit NotAssignable(int i) : i_(i) {}
  NotAssignable(const NotAssignable &other) : i_(other.i_) {}
  NotAssignable &operator=(NotAssignable &&other) = delete;
  int Get() const { return i_; }
  bool operator==(int i) const { return i_ == i; }
  friend bool operator<(NotAssignable a, NotAssignable b) {
    return a.i_ < b.i_;
  }

 private:
  int i_;
};

TEST(Btree, NotAssignableType) {
  {
    absl::btree_set<NotAssignable> set;
    set.emplace(1);
    set.emplace_hint(set.end(), 2);
    set.insert(NotAssignable(3));
    set.insert(set.end(), NotAssignable(4));
    EXPECT_THAT(set, ElementsAre(1, 2, 3, 4));
    set.erase(set.begin());
    EXPECT_THAT(set, ElementsAre(2, 3, 4));
  }
  {
    absl::btree_multiset<NotAssignable> set;
    set.emplace(1);
    set.emplace_hint(set.end(), 2);
    set.insert(NotAssignable(2));
    set.insert(set.end(), NotAssignable(3));
    EXPECT_THAT(set, ElementsAre(1, 2, 2, 3));
    set.erase(set.begin());
    EXPECT_THAT(set, ElementsAre(2, 2, 3));
  }
  {
    absl::btree_map<NotAssignable, int> map;
    map.emplace(NotAssignable(1), 1);
    map.emplace_hint(map.end(), NotAssignable(2), 2);
    map.insert({NotAssignable(3), 3});
    map.insert(map.end(), {NotAssignable(4), 4});
    EXPECT_THAT(map,
                ElementsAre(Pair(1, 1), Pair(2, 2), Pair(3, 3), Pair(4, 4)));
    map.erase(map.begin());
    EXPECT_THAT(map, ElementsAre(Pair(2, 2), Pair(3, 3), Pair(4, 4)));
  }
  {
    absl::btree_multimap<NotAssignable, int> map;
    map.emplace(NotAssignable(1), 1);
    map.emplace_hint(map.end(), NotAssignable(2), 2);
    map.insert({NotAssignable(2), 3});
    map.insert(map.end(), {NotAssignable(3), 3});
    EXPECT_THAT(map,
                ElementsAre(Pair(1, 1), Pair(2, 2), Pair(2, 3), Pair(3, 3)));
    map.erase(map.begin());
    EXPECT_THAT(map, ElementsAre(Pair(2, 2), Pair(2, 3), Pair(3, 3)));
  }
}

struct ArenaLike {
  void* recycled = nullptr;
  size_t recycled_size = 0;
};

// A very simple implementation of arena allocation.
template <typename T>
class ArenaLikeAllocator : public std::allocator<T> {
 public:
  // Standard library containers require the ability to allocate objects of
  // different types which they can do so via rebind.other.
  template <typename U>
  struct rebind {
    using other = ArenaLikeAllocator<U>;
  };

  explicit ArenaLikeAllocator(ArenaLike* arena) noexcept : arena_(arena) {}

  ~ArenaLikeAllocator() {
    if (arena_->recycled != nullptr) {
      delete [] static_cast<T*>(arena_->recycled);
      arena_->recycled = nullptr;
    }
  }

  template<typename U>
  explicit ArenaLikeAllocator(const ArenaLikeAllocator<U>& other) noexcept
      : arena_(other.arena_) {}

  T* allocate(size_t num_objects, const void* = nullptr) {
    size_t size = num_objects * sizeof(T);
    if (arena_->recycled != nullptr && arena_->recycled_size == size) {
      T* result = static_cast<T*>(arena_->recycled);
      arena_->recycled = nullptr;
      return result;
    }
    return new T[num_objects];
  }

  void deallocate(T* p, size_t num_objects) {
    size_t size = num_objects * sizeof(T);

    // Simulate writing to the freed memory as an actual arena allocator might
    // do. This triggers an error report if the memory is poisoned.
    memset(p, 0xde, size);

    if (arena_->recycled == nullptr) {
      arena_->recycled = p;
      arena_->recycled_size = size;
    } else {
      delete [] p;
    }
  }

  ArenaLike* arena_;
};

// This test verifies that an arena allocator that reuses memory will not be
// asked to free poisoned BTree memory.
TEST(Btree, ReusePoisonMemory) {
  using Alloc = ArenaLikeAllocator<int64_t>;
  using Set = absl::btree_set<int64_t, std::less<int64_t>, Alloc>;
  ArenaLike arena;
  Alloc alloc(&arena);
  Set set(alloc);

  set.insert(0);
  set.erase(0);
  set.insert(0);
}

TEST(Btree, IteratorSubtraction) {
  absl::BitGen bitgen;
  std::vector<int> vec;
  // Randomize the set's insertion order so the nodes aren't all full.
  for (int i = 0; i < 1000000; ++i) vec.push_back(i);
  absl::c_shuffle(vec, bitgen);

  absl::btree_set<int> set;
  for (int i : vec) set.insert(i);

  for (int i = 0; i < 1000; ++i) {
    size_t begin = absl::Uniform(bitgen, 0u, set.size());
    size_t end = absl::Uniform(bitgen, begin, set.size());
    ASSERT_EQ(end - begin, set.find(end) - set.find(begin))
        << begin << " " << end;
  }
}

TEST(Btree, DereferencingEndIterator) {
  if (!IsAssertEnabled()) GTEST_SKIP() << "Assertions not enabled.";

  absl::btree_set<int> set;
  for (int i = 0; i < 1000; ++i) set.insert(i);
  EXPECT_DEATH(*set.end(), R"regex(Dereferencing end\(\) iterator)regex");
}

TEST(Btree, InvalidIteratorComparison) {
  if (!IsAssertEnabled()) GTEST_SKIP() << "Assertions not enabled.";

  absl::btree_set<int> set1, set2;
  for (int i = 0; i < 1000; ++i) {
    set1.insert(i);
    set2.insert(i);
  }

  constexpr const char *kValueInitDeathMessage =
      "Comparing default-constructed iterator with .*non-default-constructed "
      "iterator";
  typename absl::btree_set<int>::iterator iter1, iter2;
  EXPECT_EQ(iter1, iter2);
  EXPECT_DEATH(void(set1.begin() == iter1), kValueInitDeathMessage);
  EXPECT_DEATH(void(iter1 == set1.begin()), kValueInitDeathMessage);

  constexpr const char *kDifferentContainerDeathMessage =
      "Comparing iterators from different containers";
  iter1 = set1.begin();
  iter2 = set2.begin();
  EXPECT_DEATH(void(iter1 == iter2), kDifferentContainerDeathMessage);
  EXPECT_DEATH(void(iter2 == iter1), kDifferentContainerDeathMessage);
}

TEST(Btree, InvalidPointerUse) {
  if (!kAsan)
    GTEST_SKIP() << "We only detect invalid pointer use in ASan mode.";

  absl::btree_set<int> set;
  set.insert(0);
  const int *ptr = &*set.begin();
  set.insert(1);
  EXPECT_DEATH(std::cout << *ptr, "use-after-free");
  size_t slots_per_node = BtreeNodePeer::GetNumSlotsPerNode<decltype(set)>();
  for (int i = 2; i < slots_per_node - 1; ++i) set.insert(i);
  ptr = &*set.begin();
  set.insert(static_cast<int>(slots_per_node));
  EXPECT_DEATH(std::cout << *ptr, "use-after-free");
}

template<typename Set>
void TestBasicFunctionality(Set set) {
  using value_type = typename Set::value_type;
  for (int i = 0; i < 100; ++i) { set.insert(value_type(i)); }
  for (int i = 50; i < 100; ++i) { set.erase(value_type(i)); }
  auto it = set.begin();
  for (int i = 0; i < 50; ++i, ++it) {
    ASSERT_EQ(set.find(value_type(i)), it) << i;
  }
}

template<size_t align>
struct alignas(align) OveralignedKey {
  explicit OveralignedKey(int i) : key(i) {}
  bool operator<(const OveralignedKey &other) const { return key < other.key; }
  int key = 0;
};

TEST(Btree, OveralignedKey) {
  // Test basic functionality with both even and odd numbers of slots per node.
  // The goal here is to detect cases where alignment may be incorrect.
  TestBasicFunctionality(
      SizedBtreeSet<OveralignedKey<16>, /*TargetValuesPerNode=*/8>());
  TestBasicFunctionality(
      SizedBtreeSet<OveralignedKey<16>, /*TargetValuesPerNode=*/9>());
}

TEST(Btree, FieldTypeEqualsSlotType) {
  // This breaks if we try to do layout_type::Pointer<slot_type> because
  // slot_type is the same as field_type.
  using set_type = absl::btree_set<uint8_t>;
  static_assert(BtreeNodePeer::FieldTypeEqualsSlotType<set_type>(), "");
  TestBasicFunctionality(set_type());
}

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

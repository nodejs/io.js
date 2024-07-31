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

#ifndef ABSL_CONTAINER_INTERNAL_BTREE_CONTAINER_H_
#define ABSL_CONTAINER_INTERNAL_BTREE_CONTAINER_H_

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/internal/throw_delegate.h"
#include "absl/container/internal/btree.h"  // IWYU pragma: export
#include "absl/container/internal/common.h"
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

// A common base class for btree_set, btree_map, btree_multiset, and
// btree_multimap.
template <typename Tree>
class btree_container {
  using params_type = typename Tree::params_type;

 protected:
  // Alias used for heterogeneous lookup functions.
  // `key_arg<K>` evaluates to `K` when the functors are transparent and to
  // `key_type` otherwise. It permits template argument deduction on `K` for the
  // transparent case.
  template <class K>
  using key_arg =
      typename KeyArg<params_type::kIsKeyCompareTransparent>::template type<
          K, typename Tree::key_type>;

 public:
  using key_type = typename Tree::key_type;
  using value_type = typename Tree::value_type;
  using size_type = typename Tree::size_type;
  using difference_type = typename Tree::difference_type;
  using key_compare = typename Tree::original_key_compare;
  using value_compare = typename Tree::value_compare;
  using allocator_type = typename Tree::allocator_type;
  using reference = typename Tree::reference;
  using const_reference = typename Tree::const_reference;
  using pointer = typename Tree::pointer;
  using const_pointer = typename Tree::const_pointer;
  using iterator = typename Tree::iterator;
  using const_iterator = typename Tree::const_iterator;
  using reverse_iterator = typename Tree::reverse_iterator;
  using const_reverse_iterator = typename Tree::const_reverse_iterator;
  using node_type = typename Tree::node_handle_type;

  struct extract_and_get_next_return_type {
    node_type node;
    iterator next;
  };

  // Constructors/assignments.
  btree_container() : tree_(key_compare(), allocator_type()) {}
  explicit btree_container(const key_compare &comp,
                           const allocator_type &alloc = allocator_type())
      : tree_(comp, alloc) {}
  explicit btree_container(const allocator_type &alloc)
      : tree_(key_compare(), alloc) {}

  btree_container(const btree_container &other)
      : btree_container(other, absl::allocator_traits<allocator_type>::
                                   select_on_container_copy_construction(
                                       other.get_allocator())) {}
  btree_container(const btree_container &other, const allocator_type &alloc)
      : tree_(other.tree_, alloc) {}

  btree_container(btree_container &&other) noexcept(
      std::is_nothrow_move_constructible<Tree>::value) = default;
  btree_container(btree_container &&other, const allocator_type &alloc)
      : tree_(std::move(other.tree_), alloc) {}

  btree_container &operator=(const btree_container &other) = default;
  btree_container &operator=(btree_container &&other) noexcept(
      std::is_nothrow_move_assignable<Tree>::value) = default;

  // Iterator routines.
  iterator begin() ABSL_ATTRIBUTE_LIFETIME_BOUND { return tree_.begin(); }
  const_iterator begin() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.begin();
  }
  const_iterator cbegin() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.begin();
  }
  iterator end() ABSL_ATTRIBUTE_LIFETIME_BOUND { return tree_.end(); }
  const_iterator end() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.end();
  }
  const_iterator cend() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.end();
  }
  reverse_iterator rbegin() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.rbegin();
  }
  const_reverse_iterator rbegin() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.rbegin();
  }
  const_reverse_iterator crbegin() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.rbegin();
  }
  reverse_iterator rend() ABSL_ATTRIBUTE_LIFETIME_BOUND { return tree_.rend(); }
  const_reverse_iterator rend() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.rend();
  }
  const_reverse_iterator crend() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.rend();
  }

  // Lookup routines.
  template <typename K = key_type>
  size_type count(const key_arg<K> &key) const {
    auto equal_range = this->equal_range(key);
    return equal_range.second - equal_range.first;
  }
  template <typename K = key_type>
  iterator find(const key_arg<K> &key) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.find(key);
  }
  template <typename K = key_type>
  const_iterator find(const key_arg<K> &key) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.find(key);
  }
  template <typename K = key_type>
  bool contains(const key_arg<K> &key) const {
    return find(key) != end();
  }
  template <typename K = key_type>
  iterator lower_bound(const key_arg<K> &key) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.lower_bound(key);
  }
  template <typename K = key_type>
  const_iterator lower_bound(const key_arg<K> &key) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.lower_bound(key);
  }
  template <typename K = key_type>
  iterator upper_bound(const key_arg<K> &key) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.upper_bound(key);
  }
  template <typename K = key_type>
  const_iterator upper_bound(const key_arg<K> &key) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.upper_bound(key);
  }
  template <typename K = key_type>
  std::pair<iterator, iterator> equal_range(const key_arg<K> &key)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.equal_range(key);
  }
  template <typename K = key_type>
  std::pair<const_iterator, const_iterator> equal_range(
      const key_arg<K> &key) const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.equal_range(key);
  }

  // Deletion routines. Note that there is also a deletion routine that is
  // specific to btree_set_container/btree_multiset_container.

  // Erase the specified iterator from the btree. The iterator must be valid
  // (i.e. not equal to end()).  Return an iterator pointing to the node after
  // the one that was erased (or end() if none exists).
  iterator erase(const_iterator iter) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.erase(iterator(iter));
  }
  iterator erase(iterator iter) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.erase(iter);
  }
  iterator erase(const_iterator first,
                 const_iterator last) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return tree_.erase_range(iterator(first), iterator(last)).second;
  }
  template <typename K = key_type>
  size_type erase(const key_arg<K> &key) {
    auto equal_range = this->equal_range(key);
    return tree_.erase_range(equal_range.first, equal_range.second).first;
  }

  // Extract routines.
  extract_and_get_next_return_type extract_and_get_next(const_iterator position)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    // Use Construct instead of Transfer because the rebalancing code will
    // destroy the slot later.
    // Note: we rely on erase() taking place after Construct().
    return {CommonAccess::Construct<node_type>(get_allocator(),
                                               iterator(position).slot()),
            erase(position)};
  }
  node_type extract(iterator position) {
    // Use Construct instead of Transfer because the rebalancing code will
    // destroy the slot later.
    auto node =
        CommonAccess::Construct<node_type>(get_allocator(), position.slot());
    erase(position);
    return node;
  }
  node_type extract(const_iterator position) {
    return extract(iterator(position));
  }

  // Utility routines.
  ABSL_ATTRIBUTE_REINITIALIZES void clear() { tree_.clear(); }
  void swap(btree_container &other) { tree_.swap(other.tree_); }
  void verify() const { tree_.verify(); }

  // Size routines.
  size_type size() const { return tree_.size(); }
  size_type max_size() const { return tree_.max_size(); }
  bool empty() const { return tree_.empty(); }

  friend bool operator==(const btree_container &x, const btree_container &y) {
    if (x.size() != y.size()) return false;
    return std::equal(x.begin(), x.end(), y.begin());
  }

  friend bool operator!=(const btree_container &x, const btree_container &y) {
    return !(x == y);
  }

  friend bool operator<(const btree_container &x, const btree_container &y) {
    return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end());
  }

  friend bool operator>(const btree_container &x, const btree_container &y) {
    return y < x;
  }

  friend bool operator<=(const btree_container &x, const btree_container &y) {
    return !(y < x);
  }

  friend bool operator>=(const btree_container &x, const btree_container &y) {
    return !(x < y);
  }

  // The allocator used by the btree.
  allocator_type get_allocator() const { return tree_.get_allocator(); }

  // The key comparator used by the btree.
  key_compare key_comp() const { return key_compare(tree_.key_comp()); }
  value_compare value_comp() const { return tree_.value_comp(); }

  // Support absl::Hash.
  template <typename State>
  friend State AbslHashValue(State h, const btree_container &b) {
    for (const auto &v : b) {
      h = State::combine(std::move(h), v);
    }
    return State::combine(std::move(h), b.size());
  }

 protected:
  friend struct btree_access;
  Tree tree_;
};

// A common base class for btree_set and btree_map.
template <typename Tree>
class btree_set_container : public btree_container<Tree> {
  using super_type = btree_container<Tree>;
  using params_type = typename Tree::params_type;
  using init_type = typename params_type::init_type;
  using is_key_compare_to = typename params_type::is_key_compare_to;
  friend class BtreeNodePeer;

 protected:
  template <class K>
  using key_arg = typename super_type::template key_arg<K>;

 public:
  using key_type = typename Tree::key_type;
  using value_type = typename Tree::value_type;
  using size_type = typename Tree::size_type;
  using key_compare = typename Tree::original_key_compare;
  using allocator_type = typename Tree::allocator_type;
  using iterator = typename Tree::iterator;
  using const_iterator = typename Tree::const_iterator;
  using node_type = typename super_type::node_type;
  using insert_return_type = InsertReturnType<iterator, node_type>;

  // Inherit constructors.
  using super_type::super_type;
  btree_set_container() {}

  // Range constructors.
  template <class InputIterator>
  btree_set_container(InputIterator b, InputIterator e,
                      const key_compare &comp = key_compare(),
                      const allocator_type &alloc = allocator_type())
      : super_type(comp, alloc) {
    insert(b, e);
  }
  template <class InputIterator>
  btree_set_container(InputIterator b, InputIterator e,
                      const allocator_type &alloc)
      : btree_set_container(b, e, key_compare(), alloc) {}

  // Initializer list constructors.
  btree_set_container(std::initializer_list<init_type> init,
                      const key_compare &comp = key_compare(),
                      const allocator_type &alloc = allocator_type())
      : btree_set_container(init.begin(), init.end(), comp, alloc) {}
  btree_set_container(std::initializer_list<init_type> init,
                      const allocator_type &alloc)
      : btree_set_container(init.begin(), init.end(), alloc) {}

  // Insertion routines.
  std::pair<iterator, bool> insert(const value_type &v)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return this->tree_.insert_unique(params_type::key(v), v);
  }
  std::pair<iterator, bool> insert(value_type &&v)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return this->tree_.insert_unique(params_type::key(v), std::move(v));
  }
  template <typename... Args>
  std::pair<iterator, bool> emplace(Args &&...args)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    // Use a node handle to manage a temp slot.
    auto node = CommonAccess::Construct<node_type>(this->get_allocator(),
                                                   std::forward<Args>(args)...);
    auto *slot = CommonAccess::GetSlot(node);
    return this->tree_.insert_unique(params_type::key(slot), slot);
  }
  iterator insert(const_iterator hint,
                  const value_type &v) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return this->tree_
        .insert_hint_unique(iterator(hint), params_type::key(v), v)
        .first;
  }
  iterator insert(const_iterator hint,
                  value_type &&v) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return this->tree_
        .insert_hint_unique(iterator(hint), params_type::key(v), std::move(v))
        .first;
  }
  template <typename... Args>
  iterator emplace_hint(const_iterator hint,
                        Args &&...args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    // Use a node handle to manage a temp slot.
    auto node = CommonAccess::Construct<node_type>(this->get_allocator(),
                                                   std::forward<Args>(args)...);
    auto *slot = CommonAccess::GetSlot(node);
    return this->tree_
        .insert_hint_unique(iterator(hint), params_type::key(slot), slot)
        .first;
  }
  template <typename InputIterator>
  void insert(InputIterator b, InputIterator e) {
    this->tree_.insert_iterator_unique(b, e, 0);
  }
  void insert(std::initializer_list<init_type> init) {
    this->tree_.insert_iterator_unique(init.begin(), init.end(), 0);
  }
  insert_return_type insert(node_type &&node) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (!node) return {this->end(), false, node_type()};
    std::pair<iterator, bool> res =
        this->tree_.insert_unique(params_type::key(CommonAccess::GetSlot(node)),
                                  CommonAccess::GetSlot(node));
    if (res.second) {
      CommonAccess::Destroy(&node);
      return {res.first, true, node_type()};
    } else {
      return {res.first, false, std::move(node)};
    }
  }
  iterator insert(const_iterator hint,
                  node_type &&node) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (!node) return this->end();
    std::pair<iterator, bool> res = this->tree_.insert_hint_unique(
        iterator(hint), params_type::key(CommonAccess::GetSlot(node)),
        CommonAccess::GetSlot(node));
    if (res.second) CommonAccess::Destroy(&node);
    return res.first;
  }

  // Node extraction routines.
  template <typename K = key_type>
  node_type extract(const key_arg<K> &key) {
    const std::pair<iterator, bool> lower_and_equal =
        this->tree_.lower_bound_equal(key);
    return lower_and_equal.second ? extract(lower_and_equal.first)
                                  : node_type();
  }
  using super_type::extract;

  // Merge routines.
  // Moves elements from `src` into `this`. If the element already exists in
  // `this`, it is left unmodified in `src`.
  template <
      typename T,
      typename absl::enable_if_t<
          absl::conjunction<
              std::is_same<value_type, typename T::value_type>,
              std::is_same<allocator_type, typename T::allocator_type>,
              std::is_same<typename params_type::is_map_container,
                           typename T::params_type::is_map_container>>::value,
          int> = 0>
  void merge(btree_container<T> &src) {  // NOLINT
    for (auto src_it = src.begin(); src_it != src.end();) {
      if (insert(std::move(params_type::element(src_it.slot()))).second) {
        src_it = src.erase(src_it);
      } else {
        ++src_it;
      }
    }
  }

  template <
      typename T,
      typename absl::enable_if_t<
          absl::conjunction<
              std::is_same<value_type, typename T::value_type>,
              std::is_same<allocator_type, typename T::allocator_type>,
              std::is_same<typename params_type::is_map_container,
                           typename T::params_type::is_map_container>>::value,
          int> = 0>
  void merge(btree_container<T> &&src) {
    merge(src);
  }
};

// Base class for btree_map.
template <typename Tree>
class btree_map_container : public btree_set_container<Tree> {
  using super_type = btree_set_container<Tree>;
  using params_type = typename Tree::params_type;
  friend class BtreeNodePeer;

 private:
  template <class K>
  using key_arg = typename super_type::template key_arg<K>;

 public:
  using key_type = typename Tree::key_type;
  using mapped_type = typename params_type::mapped_type;
  using value_type = typename Tree::value_type;
  using key_compare = typename Tree::original_key_compare;
  using allocator_type = typename Tree::allocator_type;
  using iterator = typename Tree::iterator;
  using const_iterator = typename Tree::const_iterator;

  // Inherit constructors.
  using super_type::super_type;
  btree_map_container() {}

  // Insertion routines.
  // Note: the nullptr template arguments and extra `const M&` overloads allow
  // for supporting bitfield arguments.
  template <typename K = key_type, class M>
  std::pair<iterator, bool> insert_or_assign(const key_arg<K> &k, const M &obj)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign_impl(k, obj);
  }
  template <typename K = key_type, class M, K * = nullptr>
  std::pair<iterator, bool> insert_or_assign(key_arg<K> &&k, const M &obj)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign_impl(std::forward<K>(k), obj);
  }
  template <typename K = key_type, class M, M * = nullptr>
  std::pair<iterator, bool> insert_or_assign(const key_arg<K> &k, M &&obj)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign_impl(k, std::forward<M>(obj));
  }
  template <typename K = key_type, class M, K * = nullptr, M * = nullptr>
  std::pair<iterator, bool> insert_or_assign(key_arg<K> &&k, M &&obj)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign_impl(std::forward<K>(k), std::forward<M>(obj));
  }
  template <typename K = key_type, class M>
  iterator insert_or_assign(const_iterator hint, const key_arg<K> &k,
                            const M &obj) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign_hint_impl(hint, k, obj);
  }
  template <typename K = key_type, class M, K * = nullptr>
  iterator insert_or_assign(const_iterator hint, key_arg<K> &&k,
                            const M &obj) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign_hint_impl(hint, std::forward<K>(k), obj);
  }
  template <typename K = key_type, class M, M * = nullptr>
  iterator insert_or_assign(const_iterator hint, const key_arg<K> &k,
                            M &&obj) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign_hint_impl(hint, k, std::forward<M>(obj));
  }
  template <typename K = key_type, class M, K * = nullptr, M * = nullptr>
  iterator insert_or_assign(const_iterator hint, key_arg<K> &&k,
                            M &&obj) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign_hint_impl(hint, std::forward<K>(k),
                                      std::forward<M>(obj));
  }

  template <typename K = key_type, typename... Args,
            typename absl::enable_if_t<
                !std::is_convertible<K, const_iterator>::value, int> = 0>
  std::pair<iterator, bool> try_emplace(const key_arg<K> &k, Args &&...args)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return try_emplace_impl(k, std::forward<Args>(args)...);
  }
  template <typename K = key_type, typename... Args,
            typename absl::enable_if_t<
                !std::is_convertible<K, const_iterator>::value, int> = 0>
  std::pair<iterator, bool> try_emplace(key_arg<K> &&k, Args &&...args)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return try_emplace_impl(std::forward<K>(k), std::forward<Args>(args)...);
  }
  template <typename K = key_type, typename... Args>
  iterator try_emplace(const_iterator hint, const key_arg<K> &k,
                       Args &&...args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return try_emplace_hint_impl(hint, k, std::forward<Args>(args)...);
  }
  template <typename K = key_type, typename... Args>
  iterator try_emplace(const_iterator hint, key_arg<K> &&k,
                       Args &&...args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return try_emplace_hint_impl(hint, std::forward<K>(k),
                                 std::forward<Args>(args)...);
  }

  template <typename K = key_type>
  mapped_type &operator[](const key_arg<K> &k) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return try_emplace(k).first->second;
  }
  template <typename K = key_type>
  mapped_type &operator[](key_arg<K> &&k) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return try_emplace(std::forward<K>(k)).first->second;
  }

  template <typename K = key_type>
  mapped_type &at(const key_arg<K> &key) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    auto it = this->find(key);
    if (it == this->end())
      base_internal::ThrowStdOutOfRange("absl::btree_map::at");
    return it->second;
  }
  template <typename K = key_type>
  const mapped_type &at(const key_arg<K> &key) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    auto it = this->find(key);
    if (it == this->end())
      base_internal::ThrowStdOutOfRange("absl::btree_map::at");
    return it->second;
  }

 private:
  // Note: when we call `std::forward<M>(obj)` twice, it's safe because
  // insert_unique/insert_hint_unique are guaranteed to not consume `obj` when
  // `ret.second` is false.
  template <class K, class M>
  std::pair<iterator, bool> insert_or_assign_impl(K &&k, M &&obj) {
    const std::pair<iterator, bool> ret =
        this->tree_.insert_unique(k, std::forward<K>(k), std::forward<M>(obj));
    if (!ret.second) ret.first->second = std::forward<M>(obj);
    return ret;
  }
  template <class K, class M>
  iterator insert_or_assign_hint_impl(const_iterator hint, K &&k, M &&obj) {
    const std::pair<iterator, bool> ret = this->tree_.insert_hint_unique(
        iterator(hint), k, std::forward<K>(k), std::forward<M>(obj));
    if (!ret.second) ret.first->second = std::forward<M>(obj);
    return ret.first;
  }

  template <class K, class... Args>
  std::pair<iterator, bool> try_emplace_impl(K &&k, Args &&... args) {
    return this->tree_.insert_unique(
        k, std::piecewise_construct, std::forward_as_tuple(std::forward<K>(k)),
        std::forward_as_tuple(std::forward<Args>(args)...));
  }
  template <class K, class... Args>
  iterator try_emplace_hint_impl(const_iterator hint, K &&k, Args &&... args) {
    return this->tree_
        .insert_hint_unique(iterator(hint), k, std::piecewise_construct,
                            std::forward_as_tuple(std::forward<K>(k)),
                            std::forward_as_tuple(std::forward<Args>(args)...))
        .first;
  }
};

// A common base class for btree_multiset and btree_multimap.
template <typename Tree>
class btree_multiset_container : public btree_container<Tree> {
  using super_type = btree_container<Tree>;
  using params_type = typename Tree::params_type;
  using init_type = typename params_type::init_type;
  using is_key_compare_to = typename params_type::is_key_compare_to;
  friend class BtreeNodePeer;

  template <class K>
  using key_arg = typename super_type::template key_arg<K>;

 public:
  using key_type = typename Tree::key_type;
  using value_type = typename Tree::value_type;
  using size_type = typename Tree::size_type;
  using key_compare = typename Tree::original_key_compare;
  using allocator_type = typename Tree::allocator_type;
  using iterator = typename Tree::iterator;
  using const_iterator = typename Tree::const_iterator;
  using node_type = typename super_type::node_type;

  // Inherit constructors.
  using super_type::super_type;
  btree_multiset_container() {}

  // Range constructors.
  template <class InputIterator>
  btree_multiset_container(InputIterator b, InputIterator e,
                           const key_compare &comp = key_compare(),
                           const allocator_type &alloc = allocator_type())
      : super_type(comp, alloc) {
    insert(b, e);
  }
  template <class InputIterator>
  btree_multiset_container(InputIterator b, InputIterator e,
                           const allocator_type &alloc)
      : btree_multiset_container(b, e, key_compare(), alloc) {}

  // Initializer list constructors.
  btree_multiset_container(std::initializer_list<init_type> init,
                           const key_compare &comp = key_compare(),
                           const allocator_type &alloc = allocator_type())
      : btree_multiset_container(init.begin(), init.end(), comp, alloc) {}
  btree_multiset_container(std::initializer_list<init_type> init,
                           const allocator_type &alloc)
      : btree_multiset_container(init.begin(), init.end(), alloc) {}

  // Insertion routines.
  iterator insert(const value_type &v) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return this->tree_.insert_multi(v);
  }
  iterator insert(value_type &&v) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return this->tree_.insert_multi(std::move(v));
  }
  iterator insert(const_iterator hint,
                  const value_type &v) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return this->tree_.insert_hint_multi(iterator(hint), v);
  }
  iterator insert(const_iterator hint,
                  value_type &&v) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return this->tree_.insert_hint_multi(iterator(hint), std::move(v));
  }
  template <typename InputIterator>
  void insert(InputIterator b, InputIterator e) {
    this->tree_.insert_iterator_multi(b, e);
  }
  void insert(std::initializer_list<init_type> init) {
    this->tree_.insert_iterator_multi(init.begin(), init.end());
  }
  template <typename... Args>
  iterator emplace(Args &&...args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    // Use a node handle to manage a temp slot.
    auto node = CommonAccess::Construct<node_type>(this->get_allocator(),
                                                   std::forward<Args>(args)...);
    return this->tree_.insert_multi(CommonAccess::GetSlot(node));
  }
  template <typename... Args>
  iterator emplace_hint(const_iterator hint,
                        Args &&...args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    // Use a node handle to manage a temp slot.
    auto node = CommonAccess::Construct<node_type>(this->get_allocator(),
                                                   std::forward<Args>(args)...);
    return this->tree_.insert_hint_multi(iterator(hint),
                                         CommonAccess::GetSlot(node));
  }
  iterator insert(node_type &&node) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (!node) return this->end();
    iterator res =
        this->tree_.insert_multi(params_type::key(CommonAccess::GetSlot(node)),
                                 CommonAccess::GetSlot(node));
    CommonAccess::Destroy(&node);
    return res;
  }
  iterator insert(const_iterator hint,
                  node_type &&node) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (!node) return this->end();
    iterator res = this->tree_.insert_hint_multi(
        iterator(hint),
        std::move(params_type::element(CommonAccess::GetSlot(node))));
    CommonAccess::Destroy(&node);
    return res;
  }

  // Node extraction routines.
  template <typename K = key_type>
  node_type extract(const key_arg<K> &key) {
    const std::pair<iterator, bool> lower_and_equal =
        this->tree_.lower_bound_equal(key);
    return lower_and_equal.second ? extract(lower_and_equal.first)
                                  : node_type();
  }
  using super_type::extract;

  // Merge routines.
  // Moves all elements from `src` into `this`.
  template <
      typename T,
      typename absl::enable_if_t<
          absl::conjunction<
              std::is_same<value_type, typename T::value_type>,
              std::is_same<allocator_type, typename T::allocator_type>,
              std::is_same<typename params_type::is_map_container,
                           typename T::params_type::is_map_container>>::value,
          int> = 0>
  void merge(btree_container<T> &src) {  // NOLINT
    for (auto src_it = src.begin(), end = src.end(); src_it != end; ++src_it) {
      insert(std::move(params_type::element(src_it.slot())));
    }
    src.clear();
  }

  template <
      typename T,
      typename absl::enable_if_t<
          absl::conjunction<
              std::is_same<value_type, typename T::value_type>,
              std::is_same<allocator_type, typename T::allocator_type>,
              std::is_same<typename params_type::is_map_container,
                           typename T::params_type::is_map_container>>::value,
          int> = 0>
  void merge(btree_container<T> &&src) {
    merge(src);
  }
};

// A base class for btree_multimap.
template <typename Tree>
class btree_multimap_container : public btree_multiset_container<Tree> {
  using super_type = btree_multiset_container<Tree>;
  using params_type = typename Tree::params_type;
  friend class BtreeNodePeer;

 public:
  using mapped_type = typename params_type::mapped_type;

  // Inherit constructors.
  using super_type::super_type;
  btree_multimap_container() {}
};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_BTREE_CONTAINER_H_

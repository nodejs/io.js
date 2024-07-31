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

#ifndef ABSL_CONTAINER_INTERNAL_RAW_HASH_MAP_H_
#define ABSL_CONTAINER_INTERNAL_RAW_HASH_MAP_H_

#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/throw_delegate.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/raw_hash_set.h"  // IWYU pragma: export

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <class Policy, class Hash, class Eq, class Alloc>
class raw_hash_map : public raw_hash_set<Policy, Hash, Eq, Alloc> {
  // P is Policy. It's passed as a template argument to support maps that have
  // incomplete types as values, as in unordered_map<K, IncompleteType>.
  // MappedReference<> may be a non-reference type.
  template <class P>
  using MappedReference = decltype(P::value(
      std::addressof(std::declval<typename raw_hash_map::reference>())));

  // MappedConstReference<> may be a non-reference type.
  template <class P>
  using MappedConstReference = decltype(P::value(
      std::addressof(std::declval<typename raw_hash_map::const_reference>())));

  using KeyArgImpl =
      KeyArg<IsTransparent<Eq>::value && IsTransparent<Hash>::value>;

 public:
  using key_type = typename Policy::key_type;
  using mapped_type = typename Policy::mapped_type;
  template <class K>
  using key_arg = typename KeyArgImpl::template type<K, key_type>;

  static_assert(!std::is_reference<key_type>::value, "");

  // TODO(b/187807849): Evaluate whether to support reference mapped_type and
  // remove this assertion if/when it is supported.
  static_assert(!std::is_reference<mapped_type>::value, "");

  using iterator = typename raw_hash_map::raw_hash_set::iterator;
  using const_iterator = typename raw_hash_map::raw_hash_set::const_iterator;

  raw_hash_map() {}
  using raw_hash_map::raw_hash_set::raw_hash_set;

  // The last two template parameters ensure that both arguments are rvalues
  // (lvalue arguments are handled by the overloads below). This is necessary
  // for supporting bitfield arguments.
  //
  //   union { int n : 1; };
  //   flat_hash_map<int, int> m;
  //   m.insert_or_assign(n, n);
  template <class K = key_type, class V = mapped_type, K* = nullptr,
            V* = nullptr>
  std::pair<iterator, bool> insert_or_assign(key_arg<K>&& k, V&& v)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign_impl(std::forward<K>(k), std::forward<V>(v));
  }

  template <class K = key_type, class V = mapped_type, K* = nullptr>
  std::pair<iterator, bool> insert_or_assign(key_arg<K>&& k, const V& v)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign_impl(std::forward<K>(k), v);
  }

  template <class K = key_type, class V = mapped_type, V* = nullptr>
  std::pair<iterator, bool> insert_or_assign(const key_arg<K>& k, V&& v)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign_impl(k, std::forward<V>(v));
  }

  template <class K = key_type, class V = mapped_type>
  std::pair<iterator, bool> insert_or_assign(const key_arg<K>& k, const V& v)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign_impl(k, v);
  }

  template <class K = key_type, class V = mapped_type, K* = nullptr,
            V* = nullptr>
  iterator insert_or_assign(const_iterator, key_arg<K>&& k,
                            V&& v) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign(std::forward<K>(k), std::forward<V>(v)).first;
  }

  template <class K = key_type, class V = mapped_type, K* = nullptr>
  iterator insert_or_assign(const_iterator, key_arg<K>&& k,
                            const V& v) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign(std::forward<K>(k), v).first;
  }

  template <class K = key_type, class V = mapped_type, V* = nullptr>
  iterator insert_or_assign(const_iterator, const key_arg<K>& k,
                            V&& v) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign(k, std::forward<V>(v)).first;
  }

  template <class K = key_type, class V = mapped_type>
  iterator insert_or_assign(const_iterator, const key_arg<K>& k,
                            const V& v) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return insert_or_assign(k, v).first;
  }

  // All `try_emplace()` overloads make the same guarantees regarding rvalue
  // arguments as `std::unordered_map::try_emplace()`, namely that these
  // functions will not move from rvalue arguments if insertions do not happen.
  template <class K = key_type, class... Args,
            typename std::enable_if<
                !std::is_convertible<K, const_iterator>::value, int>::type = 0,
            K* = nullptr>
  std::pair<iterator, bool> try_emplace(key_arg<K>&& k, Args&&... args)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return try_emplace_impl(std::forward<K>(k), std::forward<Args>(args)...);
  }

  template <class K = key_type, class... Args,
            typename std::enable_if<
                !std::is_convertible<K, const_iterator>::value, int>::type = 0>
  std::pair<iterator, bool> try_emplace(const key_arg<K>& k, Args&&... args)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return try_emplace_impl(k, std::forward<Args>(args)...);
  }

  template <class K = key_type, class... Args, K* = nullptr>
  iterator try_emplace(const_iterator, key_arg<K>&& k,
                       Args&&... args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return try_emplace(std::forward<K>(k), std::forward<Args>(args)...).first;
  }

  template <class K = key_type, class... Args>
  iterator try_emplace(const_iterator, const key_arg<K>& k,
                       Args&&... args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return try_emplace(k, std::forward<Args>(args)...).first;
  }

  template <class K = key_type, class P = Policy>
  MappedReference<P> at(const key_arg<K>& key) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    auto it = this->find(key);
    if (it == this->end()) {
      base_internal::ThrowStdOutOfRange(
          "absl::container_internal::raw_hash_map<>::at");
    }
    return Policy::value(&*it);
  }

  template <class K = key_type, class P = Policy>
  MappedConstReference<P> at(const key_arg<K>& key) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    auto it = this->find(key);
    if (it == this->end()) {
      base_internal::ThrowStdOutOfRange(
          "absl::container_internal::raw_hash_map<>::at");
    }
    return Policy::value(&*it);
  }

  template <class K = key_type, class P = Policy, K* = nullptr>
  MappedReference<P> operator[](key_arg<K>&& key)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    // It is safe to use unchecked_deref here because try_emplace
    // will always return an iterator pointing to a valid item in the table,
    // since it inserts if nothing is found for the given key.
    return Policy::value(
        &this->unchecked_deref(try_emplace(std::forward<K>(key)).first));
  }

  template <class K = key_type, class P = Policy>
  MappedReference<P> operator[](const key_arg<K>& key)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    // It is safe to use unchecked_deref here because try_emplace
    // will always return an iterator pointing to a valid item in the table,
    // since it inserts if nothing is found for the given key.
    return Policy::value(&this->unchecked_deref(try_emplace(key).first));
  }

 private:
  template <class K, class V>
  std::pair<iterator, bool> insert_or_assign_impl(K&& k, V&& v)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    auto res = this->find_or_prepare_insert(k);
    if (res.second) {
      this->emplace_at(res.first, std::forward<K>(k), std::forward<V>(v));
    } else {
      Policy::value(&*res.first) = std::forward<V>(v);
    }
    return res;
  }

  template <class K = key_type, class... Args>
  std::pair<iterator, bool> try_emplace_impl(K&& k, Args&&... args)
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    auto res = this->find_or_prepare_insert(k);
    if (res.second) {
      this->emplace_at(res.first, std::piecewise_construct,
                       std::forward_as_tuple(std::forward<K>(k)),
                       std::forward_as_tuple(std::forward<Args>(args)...));
    }
    return res;
  }
};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_RAW_HASH_MAP_H_

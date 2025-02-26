// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NODE_CACHE_H_
#define V8_COMPILER_NODE_CACHE_H_

#include "src/base/export-template.h"
#include "src/base/hashing.h"
#include "src/base/macros.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Zone;
template <typename>
class ZoneVector;


namespace compiler {

// Forward declarations.
class Node;


// A cache for nodes based on a key. Useful for implementing canonicalization of
// nodes such as constants, parameters, etc.
template <typename Key, typename Hash = base::hash<Key>,
          typename Pred = std::equal_to<Key> >
class EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE) NodeCache final {
 public:
  explicit NodeCache(Zone* zone) : map_(zone) {}
  ~NodeCache() = default;
  NodeCache(const NodeCache&) = delete;
  NodeCache& operator=(const NodeCache&) = delete;

  // Search for node associated with {key} and return a pointer to a memory
  // location in this cache that stores an entry for the key. If the location
  // returned by this method contains a non-nullptr node, the caller can use
  // that node. Otherwise it is the responsibility of the caller to fill the
  // entry with a new node.
  Node** Find(Key key) { return &(map_[key]); }

  // Appends all nodes from this cache to {nodes}.
  void GetCachedNodes(ZoneVector<Node*>* nodes) {
    for (const auto& entry : map_) {
      if (entry.second) nodes->push_back(entry.second);
    }
  }

 private:
  ZoneUnorderedMap<Key, Node*, Hash, Pred> map_;
};

// Various default cache types.
using Int32NodeCache = NodeCache<int32_t>;
using Int64NodeCache = NodeCache<int64_t>;

// All we want is the numeric value of the RelocInfo::Mode enum. We typedef
// below to avoid pulling in assembler.h
using RelocInfoMode = char;
using RelocInt32Key = std::pair<int32_t, RelocInfoMode>;
using RelocInt64Key = std::pair<int64_t, RelocInfoMode>;
using RelocInt32NodeCache = NodeCache<RelocInt32Key>;
using RelocInt64NodeCache = NodeCache<RelocInt64Key>;
#if V8_HOST_ARCH_32_BIT
using IntPtrNodeCache = Int32NodeCache;
#else
using IntPtrNodeCache = Int64NodeCache;
#endif

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_NODE_CACHE_H_

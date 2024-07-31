// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_IMPORT_WRAPPER_CACHE_H_
#define V8_WASM_WASM_IMPORT_WRAPPER_CACHE_H_

#include "src/base/platform/mutex.h"
#include "src/wasm/module-instantiate.h"

namespace v8 {
namespace internal {

class Counters;

namespace wasm {

class WasmCode;
class WasmEngine;

using FunctionSig = Signature<ValueType>;

// Implements a cache for import wrappers.
class WasmImportWrapperCache {
 public:
  struct CacheKey {
    CacheKey(ImportCallKind kind, uint32_t canonical_type_index,
             int expected_arity, Suspend suspend)
        : kind(kind),
          canonical_type_index(canonical_type_index),
          expected_arity(expected_arity),
          suspend(suspend) {}

    bool operator==(const CacheKey& rhs) const {
      return kind == rhs.kind &&
             canonical_type_index == rhs.canonical_type_index &&
             expected_arity == rhs.expected_arity && suspend == rhs.suspend;
    }

    ImportCallKind kind;
    uint32_t canonical_type_index;
    int expected_arity;
    Suspend suspend;
  };

  class CacheKeyHash {
   public:
    size_t operator()(const CacheKey& key) const {
      return base::hash_combine(static_cast<uint8_t>(key.kind),
                                key.canonical_type_index, key.expected_arity);
    }
  };

  // Helper class to modify the cache under a lock.
  class V8_NODISCARD ModificationScope {
   public:
    explicit ModificationScope(WasmImportWrapperCache* cache)
        : cache_(cache), guard_(&cache->mutex_) {}

    V8_EXPORT_PRIVATE WasmCode*& operator[](const CacheKey& key);

   private:
    WasmImportWrapperCache* const cache_;
    base::MutexGuard guard_;
  };

  ~WasmImportWrapperCache() { clear(); }

  // Clear this cache, dropping all reference counts.
  void clear();

  // Not thread-safe, use ModificationScope to get exclusive write access to the
  // cache.
  V8_EXPORT_PRIVATE WasmCode*& operator[](const CacheKey& key);

  // Thread-safe. Assumes the key exists in the map.
  V8_EXPORT_PRIVATE WasmCode* Get(ImportCallKind kind,
                                  uint32_t canonical_type_index,
                                  int expected_arity, Suspend suspend) const;
  // Thread-safe. Returns nullptr if the key doesn't exist in the map.
  WasmCode* MaybeGet(ImportCallKind kind, uint32_t canonical_type_index,
                     int expected_arity, Suspend suspend) const;

  size_t EstimateCurrentMemoryConsumption() const;

 private:
  mutable base::Mutex mutex_;
  std::unordered_map<CacheKey, WasmCode*, CacheKeyHash> entry_map_;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_IMPORT_WRAPPER_CACHE_H_

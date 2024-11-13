// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-import-wrapper-cache.h"

#include <vector>

#include "src/wasm/std-object-sizes.h"
#include "src/wasm/wasm-code-manager.h"

namespace v8::internal::wasm {

WasmCode*& WasmImportWrapperCache::ModificationScope::operator[](
    const CacheKey& key) {
  return cache_->entry_map_[key];
}

void WasmImportWrapperCache::clear() {
  std::vector<WasmCode*> ptrs;
  {
    base::MutexGuard lock(&mutex_);
    if (entry_map_.empty()) return;
    ptrs.reserve(entry_map_.size());
    for (auto& [key, code] : entry_map_) {
      if (code) ptrs.push_back(code);
    }
    entry_map_.clear();
  }
  if (ptrs.empty()) return;
  WasmCode::DecrementRefCount(base::VectorOf(ptrs));
}

WasmCode*& WasmImportWrapperCache::operator[](
    const WasmImportWrapperCache::CacheKey& key) {
  return entry_map_[key];
}

WasmCode* WasmImportWrapperCache::Get(ImportCallKind kind,
                                      uint32_t canonical_type_index,
                                      int expected_arity,
                                      Suspend suspend) const {
  base::MutexGuard lock(&mutex_);

  auto it =
      entry_map_.find({kind, canonical_type_index, expected_arity, suspend});
  DCHECK(it != entry_map_.end());
  return it->second;
}

WasmCode* WasmImportWrapperCache::MaybeGet(ImportCallKind kind,
                                           uint32_t canonical_type_index,
                                           int expected_arity,
                                           Suspend suspend) const {
  base::MutexGuard lock(&mutex_);

  auto it =
      entry_map_.find({kind, canonical_type_index, expected_arity, suspend});
  if (it == entry_map_.end()) return nullptr;
  return it->second;
}

size_t WasmImportWrapperCache::EstimateCurrentMemoryConsumption() const {
  UPDATE_WHEN_CLASS_CHANGES(WasmImportWrapperCache, 88);
  base::MutexGuard lock(&mutex_);
  return sizeof(WasmImportWrapperCache) + ContentSize(entry_map_);
}

}  // namespace v8::internal::wasm

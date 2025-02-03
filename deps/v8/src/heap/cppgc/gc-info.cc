// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/internal/gc-info.h"

#include "include/cppgc/internal/name-trait.h"
#include "include/v8config.h"
#include "src/heap/cppgc/gc-info-table.h"

namespace cppgc::internal {

namespace {

HeapObjectName GetHiddenName(
    const void*, HeapObjectNameForUnnamedObject name_retrieval_mode) {
  return {
      NameProvider::kHiddenName,
      name_retrieval_mode == HeapObjectNameForUnnamedObject::kUseHiddenName};
}

}  // namespace

// static
GCInfoIndex EnsureGCInfoIndexTrait::EnsureGCInfoIndex(
    std::atomic<GCInfoIndex>& registered_index, TraceCallback trace_callback,
    FinalizationCallback finalization_callback, NameCallback name_callback) {
  return GlobalGCInfoTable::GetMutable().RegisterNewGCInfo(
      registered_index,
      GCInfo(finalization_callback, trace_callback, name_callback));
}

// static
GCInfoIndex EnsureGCInfoIndexTrait::EnsureGCInfoIndex(
    std::atomic<GCInfoIndex>& registered_index, TraceCallback trace_callback,
    FinalizationCallback finalization_callback) {
  return GlobalGCInfoTable::GetMutable().RegisterNewGCInfo(
      registered_index,
      GCInfo(finalization_callback, trace_callback, GetHiddenName));
}

// static
GCInfoIndex EnsureGCInfoIndexTrait::EnsureGCInfoIndex(
    std::atomic<GCInfoIndex>& registered_index, TraceCallback trace_callback,
    NameCallback name_callback) {
  return GlobalGCInfoTable::GetMutable().RegisterNewGCInfo(
      registered_index, GCInfo(nullptr, trace_callback, name_callback));
}

// static
GCInfoIndex EnsureGCInfoIndexTrait::EnsureGCInfoIndex(
    std::atomic<GCInfoIndex>& registered_index, TraceCallback trace_callback) {
  return GlobalGCInfoTable::GetMutable().RegisterNewGCInfo(
      registered_index, GCInfo(nullptr, trace_callback, GetHiddenName));
}

}  // namespace cppgc::internal

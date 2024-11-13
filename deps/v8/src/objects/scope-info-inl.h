// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SCOPE_INFO_INL_H_
#define V8_OBJECTS_SCOPE_INFO_INL_H_

#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/scope-info.h"
#include "src/objects/string.h"
#include "src/roots/roots-inl.h"
#include "src/torque/runtime-macro-shims.h"
#include "src/torque/runtime-support.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/scope-info-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(ScopeInfo)

bool ScopeInfo::IsAsmModule() const { return IsAsmModuleBit::decode(Flags()); }

bool ScopeInfo::HasSimpleParameters() const {
  return HasSimpleParametersBit::decode(Flags());
}

uint32_t ScopeInfo::Flags() const { return flags(); }
int ScopeInfo::ParameterCount() const { return parameter_count(); }
int ScopeInfo::ContextLocalCount() const { return context_local_count(); }

ObjectSlot ScopeInfo::data_start() { return RawField(OffsetOfElementAt(0)); }

bool ScopeInfo::HasInlinedLocalNames() const {
  return ContextLocalCount() < kScopeInfoMaxInlinedLocalNamesSize;
}

template <typename ScopeInfoPtr>
class ScopeInfo::LocalNamesRange {
 public:
  class Iterator {
   public:
    Iterator(const LocalNamesRange* range, InternalIndex index)
        : range_(range), index_(index) {
      DCHECK_NOT_NULL(range);
      if (!range_->inlined()) advance_hashtable_index();
    }

    Iterator& operator++() {
      DCHECK_LT(index_, range_->max_index());
      ++index_;
      if (range_->inlined()) return *this;
      advance_hashtable_index();
      return *this;
    }

    friend bool operator==(const Iterator& a, const Iterator& b) {
      return a.range_ == b.range_ && a.index_ == b.index_;
    }

    friend bool operator!=(const Iterator& a, const Iterator& b) {
      return !(a == b);
    }

    Tagged<String> name(PtrComprCageBase cage_base) const {
      DCHECK_LT(index_, range_->max_index());
      if (range_->inlined()) {
        return scope_info()->ContextInlinedLocalName(cage_base,
                                                     index_.as_int());
      }
      return Cast<String>(table()->KeyAt(cage_base, index_));
    }

    Tagged<String> name() const {
      PtrComprCageBase cage_base = GetPtrComprCageBase(*scope_info());
      return name(cage_base);
    }

    const Iterator* operator*() const { return this; }

    int index() const {
      if (range_->inlined()) return index_.as_int();
      return table()->IndexAt(index_);
    }

   private:
    const LocalNamesRange* range_;
    InternalIndex index_;

    ScopeInfoPtr scope_info() const { return range_->scope_info_; }

    Tagged<NameToIndexHashTable> table() const {
      return scope_info()->context_local_names_hashtable();
    }

    void advance_hashtable_index() {
      DisallowGarbageCollection no_gc;
      ReadOnlyRoots roots = scope_info()->GetReadOnlyRoots();
      InternalIndex max = range_->max_index();
      // Increment until iterator points to a valid key or max.
      while (index_ < max) {
        Tagged<Object> key = table()->KeyAt(index_);
        if (table()->IsKey(roots, key)) break;
        ++index_;
      }
    }

    friend class LocalNamesRange;
  };

  bool inlined() const { return scope_info_->HasInlinedLocalNames(); }

  InternalIndex max_index() const {
    int max = inlined()
                  ? scope_info_->ContextLocalCount()
                  : scope_info_->context_local_names_hashtable()->Capacity();
    return InternalIndex(max);
  }

  explicit LocalNamesRange(ScopeInfoPtr scope_info) : scope_info_(scope_info) {}

  inline Iterator begin() const { return Iterator(this, InternalIndex(0)); }

  inline Iterator end() const { return Iterator(this, max_index()); }

 private:
  ScopeInfoPtr scope_info_;
};

// static
ScopeInfo::LocalNamesRange<Handle<ScopeInfo>> ScopeInfo::IterateLocalNames(
    Handle<ScopeInfo> scope_info) {
  return LocalNamesRange<Handle<ScopeInfo>>(scope_info);
}

// static
ScopeInfo::LocalNamesRange<Tagged<ScopeInfo>> ScopeInfo::IterateLocalNames(
    Tagged<ScopeInfo> scope_info, const DisallowGarbageCollection& no_gc) {
  USE(no_gc);
  return LocalNamesRange<Tagged<ScopeInfo>>(scope_info);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SCOPE_INFO_INL_H_

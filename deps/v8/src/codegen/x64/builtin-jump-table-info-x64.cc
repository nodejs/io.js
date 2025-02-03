// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/x64/builtin-jump-table-info-x64.h"

#include "src/base/memory.h"
#include "src/codegen/assembler-inl.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

void BuiltinJumpTableInfoWriter::Add(uint32_t pc_offset, int32_t target) {
  entries_.emplace_back(pc_offset, target);
}

size_t BuiltinJumpTableInfoWriter::entry_count() const {
  return entries_.size();
}

uint32_t BuiltinJumpTableInfoWriter::size_in_bytes() const {
  return static_cast<uint32_t>(entry_count() *
                               BuiltinJumpTableInfoEntry::kSize);
}

void BuiltinJumpTableInfoWriter::Emit(Assembler* assm) {
  for (auto i = entries_.begin(); i != entries_.end(); ++i) {
    static_assert(BuiltinJumpTableInfoEntry::kPCOffsetSize == kUInt32Size);
    assm->dd(i->pc_offset);
    static_assert(BuiltinJumpTableInfoEntry::kTargetSize == kInt32Size);
    assm->dd(i->target);
    static_assert(BuiltinJumpTableInfoEntry::kSize ==
                  BuiltinJumpTableInfoEntry::kPCOffsetSize +
                      BuiltinJumpTableInfoEntry::kTargetSize);
  }
}

BuiltinJumpTableInfoIterator::BuiltinJumpTableInfoIterator(Address start,
                                                           uint32_t size)
    : start_(start), size_(size), cursor_(start_) {
  DCHECK_NE(kNullAddress, start);
}

uint32_t BuiltinJumpTableInfoIterator::GetPCOffset() const {
  return base::ReadUnalignedValue<uint32_t>(
      cursor_ + offsetof(BuiltinJumpTableInfoEntry, pc_offset));
}

int32_t BuiltinJumpTableInfoIterator::GetTarget() const {
  return base::ReadUnalignedValue<int32_t>(
      cursor_ + offsetof(BuiltinJumpTableInfoEntry, target));
}

void BuiltinJumpTableInfoIterator::Next() {
  cursor_ += BuiltinJumpTableInfoEntry::kSize;
}

bool BuiltinJumpTableInfoIterator::HasCurrent() const {
  return cursor_ < start_ + size_;
}

}  // namespace internal
}  // namespace v8

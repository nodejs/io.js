// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_RELOC_INFO_INL_H_
#define V8_CODEGEN_RELOC_INFO_INL_H_

#include "src/codegen/assembler-inl.h"
#include "src/codegen/reloc-info.h"
#include "src/heap/heap-write-barrier-inl.h"

namespace v8 {
namespace internal {

void WritableRelocInfo::set_target_object(Tagged<InstructionStream> host,
                                          Tagged<HeapObject> target,
                                          WriteBarrierMode write_barrier_mode,
                                          ICacheFlushMode icache_flush_mode) {
  set_target_object(target, icache_flush_mode);
  if (!v8_flags.disable_write_barriers) {
    WriteBarrierForCode(host, this, target, write_barrier_mode);
  }
}

template <typename RelocInfoT>
RelocIteratorBase<RelocInfoT>::RelocIteratorBase(RelocInfoT reloc_info,
                                                 const uint8_t* pos,
                                                 const uint8_t* end,
                                                 int mode_mask)
    : pos_(pos), end_(end), rinfo_(reloc_info), mode_mask_(mode_mask) {
  DCHECK_EQ(reloc_info.rmode(), RelocInfo::NO_INFO);
  DCHECK_EQ(reloc_info.data(), 0);
  // Relocation info is read backwards.
  DCHECK_GE(pos_, end_);
  if (mode_mask_ == 0) pos_ = end_;
  next();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_RELOC_INFO_INL_H_

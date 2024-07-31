// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_PARALLEL_MOVE_INL_H_
#define V8_WASM_BASELINE_PARALLEL_MOVE_INL_H_

#include "src/wasm/baseline/liftoff-assembler-inl.h"
#include "src/wasm/baseline/parallel-move.h"

namespace v8::internal::wasm {

ParallelMove::ParallelMove(LiftoffAssembler* wasm_asm)
    : asm_(wasm_asm), last_spill_offset_(asm_->TopSpillOffset()) {}

}  // namespace v8::internal::wasm

#endif  // V8_WASM_BASELINE_PARALLEL_MOVE_INL_H_

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/store-store-elimination-reducer-inl.h"
#include "test/unittests/compiler/turboshaft/reducer-test.h"
namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

class StoreStoreEliminationReducerTest : public ReducerTest {};

TEST_F(StoreStoreEliminationReducerTest, MergeObjectInitialzationStore) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    OpIndex param0 = Asm.GetParameter(0);

    OpIndex heap_const0 = __ HeapConstant(Asm.factory().undefined_value());
    OpIndex heap_const1 = __ HeapConstant(Asm.factory().null_value());

    __ Store(param0, heap_const0, StoreOp::Kind::TaggedBase(),
             MemoryRepresentation::TaggedPointer(),
             WriteBarrierKind::kNoWriteBarrier, 0, true);

    OpIndex store0 = __ output_graph().LastOperation();
    DCHECK(__ output_graph().Get(store0).template Is<StoreOp>());
    Asm.Capture(store0, "store0");

    __ Store(param0, heap_const1, StoreOp::Kind::TaggedBase(),
             MemoryRepresentation::AnyTagged(),
             WriteBarrierKind::kNoWriteBarrier, 4, true);

    OpIndex store1 = __ output_graph().LastOperation();
    DCHECK(__ output_graph().Get(store1).template Is<StoreOp>());
    Asm.Capture(store1, "store1");

    __ Return(param0);
  });

  test.Run<StoreStoreEliminationReducer>();
#ifdef V8_COMPRESS_POINTERS
  const auto& store0_out = test.GetCapture("store0");
  const StoreOp* store64 = store0_out.GetFirst<StoreOp>();
  ASSERT_TRUE(store64 != nullptr);
  ASSERT_EQ(store64->kind, StoreOp::Kind::TaggedBase());
  ASSERT_EQ(store64->stored_rep, MemoryRepresentation::Uint64());
  ASSERT_EQ(store64->write_barrier, WriteBarrierKind::kNoWriteBarrier);

  const auto& store1_out = test.GetCapture("store1");
  ASSERT_TRUE(store1_out.IsEmpty());
#endif  // V8_COMPRESS_POINTERS
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/late-load-elimination-reducer.h"

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/required-optimization-reducer.h"
#include "src/compiler/turboshaft/variable-reducer.h"
#include "test/common/flag-utils.h"
#include "test/unittests/compiler/turboshaft/reducer-test.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// Use like this:
// V<...> C(my_var) = ...
#define C(value) value = Asm.CaptureHelperForMacro(#value)

class LateLoadEliminationReducerTest : public ReducerTest {
 public:
  LateLoadEliminationReducerTest()
      : ReducerTest(),
        flag_load_elimination_(&v8_flags.turboshaft_load_elimination, true) {}

 private:
  const FlagScope<bool> flag_load_elimination_;
};

/* TruncateInt64ToInt32(
 *     BitcastTaggedToWordPtrForTagAndSmiBits(
 *         Load[Tagged]))
 * => Load[Int32]
 */
TEST_F(LateLoadEliminationReducerTest, Int32TruncatedLoad_Foldable) {
  auto test = CreateFromGraph(2, [](auto& Asm) {
    V<Object> C(load) = __ Load(
        Asm.GetParameter(0), {}, LoadOp::Kind::TaggedBase(),
        MemoryRepresentation::AnyTagged(), RegisterRepresentation::Tagged(), 0);
    V<WordPtr> temp = __ BitcastTaggedToWordPtrForTagAndSmiBits(load);
    V<Word32> C(truncate) = __ TruncateWordPtrToWord32(temp);
    V<Object> C(result) =
        __ Conditional(truncate, Asm.GetParameter(0), Asm.GetParameter(1));
    __ Return(result);
  });

  test.Run<LateLoadEliminationReducer>();

#ifdef V8_COMPRESS_POINTERS

  // Load should have been replaced by an int32 load.
  const LoadOp* load = test.GetCapturedAs<LoadOp>("load");
  ASSERT_NE(load, nullptr);
  ASSERT_EQ(load->loaded_rep, MemoryRepresentation::Int32());
  ASSERT_EQ(load->result_rep, RegisterRepresentation::Word32());

  // The truncation chain should have been eliminated.
  ASSERT_TRUE(test.GetCapture("truncate").IsEmpty());

  // The select uses the load as condition directly.
  const SelectOp* result = test.GetCapturedAs<SelectOp>("result");
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(&test.graph().Get(result->cond()), load);

#endif
}

/* TruncateInt64ToInt32(
 *     BitcastTaggedToWordPtrForTagAndSmiBits(
 *         Load[Tagged]))
 * cannot be optimized because Load[Tagged] has another non-truncating use.
 */
TEST_F(LateLoadEliminationReducerTest,
       Int32TruncatedLoad_NonFoldable_AdditionalUse) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    V<Object> C(load) = __ Load(
        Asm.GetParameter(0), {}, LoadOp::Kind::TaggedBase(),
        MemoryRepresentation::AnyTagged(), RegisterRepresentation::Tagged(), 0);
    V<WordPtr> temp = __ BitcastTaggedToWordPtrForTagAndSmiBits(load);
    V<Word32> C(truncate) = __ TruncateWordPtrToWord32(temp);
    __ Return(__ Conditional(truncate, Asm.GetParameter(0), load));
  });

  test.Run<LateLoadEliminationReducer>();

#ifdef V8_COMPRESS_POINTERS

  // Load should still be tagged.
  const LoadOp* load = test.GetCapturedAs<LoadOp>("load");
  ASSERT_NE(load, nullptr);
  ASSERT_EQ(load->loaded_rep, MemoryRepresentation::AnyTagged());
  ASSERT_EQ(load->result_rep, RegisterRepresentation::Tagged());

  // The truncation chain should still be present.
  ASSERT_FALSE(test.GetCapture("truncate").IsEmpty());

#endif
}

/* TruncateInt64ToInt32(
 *     BitcastTaggedToWordPtrForTagAndSmiBits(
 *         Load[Tagged]))
 * cannot be optimized because there is another non-truncated Load that is
 * elminated by LateLoadElimination that adds additional uses.
 */
TEST_F(LateLoadEliminationReducerTest,
       Int32TruncatedLoad_NonFoldable_ReplacingOtherLoad) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    V<Object> C(load) = __ Load(
        Asm.GetParameter(0), {}, LoadOp::Kind::TaggedBase(),
        MemoryRepresentation::AnyTagged(), RegisterRepresentation::Tagged(), 0);
    V<WordPtr> temp = __ BitcastTaggedToWordPtrForTagAndSmiBits(load);
    V<Word32> C(truncate) = __ TruncateWordPtrToWord32(temp);
    V<Object> C(other_load) = __ Load(
        Asm.GetParameter(0), {}, LoadOp::Kind::TaggedBase(),
        MemoryRepresentation::AnyTagged(), RegisterRepresentation::Tagged(), 0);
    V<Object> C(result) =
        __ Conditional(truncate, Asm.GetParameter(0), other_load);
    __ Return(result);
  });

  test.Run<LateLoadEliminationReducer>();

#ifdef V8_COMPRESS_POINTERS

  // Load should still be tagged.
  const LoadOp* load = test.GetCapturedAs<LoadOp>("load");
  ASSERT_NE(load, nullptr);
  ASSERT_EQ(load->loaded_rep, MemoryRepresentation::AnyTagged());
  ASSERT_EQ(load->result_rep, RegisterRepresentation::Tagged());

  // The truncation chain should still be present.
  ASSERT_FALSE(test.GetCapture("truncate").IsEmpty());

  // The other load has been eliminated.
  ASSERT_TRUE(test.GetCapture("other_load").IsEmpty());

  // The select's input is the first load.
  const SelectOp* result = test.GetCapturedAs<SelectOp>("result");
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(&test.graph().Get(result->vfalse()), load);

#endif
}

/* TruncateInt64ToInt32(
 *     BitcastTaggedToWordPtrForTagAndSmiBits(
 *         Load[Tagged]))
 * => Load[Int32]
 * because the other load that is eliminated by LateLoadElimination is also a
 * truncating load.
 */
TEST_F(LateLoadEliminationReducerTest,
       Int32TruncatedLoad_Foldable_ReplacingOtherLoad) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    V<Object> C(load) = __ Load(
        Asm.GetParameter(0), {}, LoadOp::Kind::TaggedBase(),
        MemoryRepresentation::AnyTagged(), RegisterRepresentation::Tagged(), 0);
    V<WordPtr> temp = __ BitcastTaggedToWordPtrForTagAndSmiBits(load);
    V<Word32> C(truncate) = __ TruncateWordPtrToWord32(temp);
    V<Object> C(other_load) = __ Load(
        Asm.GetParameter(0), {}, LoadOp::Kind::TaggedBase(),
        MemoryRepresentation::AnyTagged(), RegisterRepresentation::Tagged(), 0);
    V<WordPtr> other_temp =
        __ BitcastTaggedToWordPtrForTagAndSmiBits(other_load);
    V<Word32> C(other_truncate) = __ TruncateWordPtrToWord32(other_temp);
    V<Word32> C(result) =
        __ Conditional(truncate, __ Word32Constant(42), other_truncate);
    __ Return(__ TagSmi(result));
  });

  test.Run<LateLoadEliminationReducer>();

#ifdef V8_COMPRESS_POINTERS
  // Load should have been replaced by an int32 load.
  const LoadOp* load = test.GetCapturedAs<LoadOp>("load");
  ASSERT_NE(load, nullptr);
  ASSERT_EQ(load->loaded_rep, MemoryRepresentation::Int32());
  ASSERT_EQ(load->result_rep, RegisterRepresentation::Word32());

  // Both truncation chains should have been eliminated.
  ASSERT_TRUE(test.GetCapture("truncate").IsEmpty());
  ASSERT_TRUE(test.GetCapture("other_truncate").IsEmpty());

  // The other load should have been eliminated.
  ASSERT_TRUE(test.GetCapture("other_load").IsEmpty());

  // The select uses the load as condition and the second input directly.
  const SelectOp* result = test.GetCapturedAs<SelectOp>("result");
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(&test.graph().Get(result->cond()), load);
  ASSERT_EQ(&test.graph().Get(result->vfalse()), load);

#endif
}

/* TruncateInt64ToInt32(
 *     BitcastTaggedToWordPtrForTagAndSmiBits(
 *         Load[Tagged]))
 * cannot be optimized because this load is replaced by another load that has
 * non-truncated uses.
 */
TEST_F(LateLoadEliminationReducerTest,
       Int32TruncatedLoad_NonFoldable_ReplacedByOtherLoad) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    V<Object> C(other_load) = __ Load(
        Asm.GetParameter(0), {}, LoadOp::Kind::TaggedBase(),
        MemoryRepresentation::AnyTagged(), RegisterRepresentation::Tagged(), 0);
    V<Object> C(load) = __ Load(
        Asm.GetParameter(0), {}, LoadOp::Kind::TaggedBase(),
        MemoryRepresentation::AnyTagged(), RegisterRepresentation::Tagged(), 0);
    V<WordPtr> temp = __ BitcastTaggedToWordPtrForTagAndSmiBits(load);
    V<Word32> C(truncate) = __ TruncateWordPtrToWord32(temp);
    V<Object> C(result) =
        __ Conditional(truncate, Asm.GetParameter(0), other_load);
    __ Return(result);
  });

  test.Run<LateLoadEliminationReducer>();

#ifdef V8_COMPRESS_POINTERS

  // The other load should still be tagged.
  const LoadOp* other_load = test.GetCapturedAs<LoadOp>("other_load");
  ASSERT_NE(other_load, nullptr);
  ASSERT_EQ(other_load->loaded_rep, MemoryRepresentation::AnyTagged());
  ASSERT_EQ(other_load->result_rep, RegisterRepresentation::Tagged());

  // The truncation chain should still be present.
  const ChangeOp* truncate = test.GetCapturedAs<ChangeOp>("truncate");
  ASSERT_NE(truncate, nullptr);
  // ... but the input is now the other load.
  const TaggedBitcastOp& bitcast =
      test.graph().Get(truncate->input()).Cast<TaggedBitcastOp>();
  ASSERT_EQ(other_load, &test.graph().Get(bitcast.input()));

  // The load has been eliminated.
  ASSERT_TRUE(test.GetCapture("load").IsEmpty());

  // The select's input is unchanged.
  const SelectOp* result = test.GetCapturedAs<SelectOp>("result");
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(&test.graph().Get(result->cond()), truncate);
  ASSERT_EQ(&test.graph().Get(result->vfalse()), other_load);

#endif
}

/* TruncateInt64ToInt32(
 *     BitcastTaggedToWordPtrForTagAndSmiBits(
 *         Load[Tagged]))
 * => Load[Int32]
 * because the other load that is replacing the load is also a truncating load.
 */
TEST_F(LateLoadEliminationReducerTest,
       Int32TruncatedLoad_Foldable_ReplacedByOtherLoad) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    V<Object> C(other_load) = __ Load(
        Asm.GetParameter(0), {}, LoadOp::Kind::TaggedBase(),
        MemoryRepresentation::AnyTagged(), RegisterRepresentation::Tagged(), 0);
    V<Object> C(load) = __ Load(
        Asm.GetParameter(0), {}, LoadOp::Kind::TaggedBase(),
        MemoryRepresentation::AnyTagged(), RegisterRepresentation::Tagged(), 0);
    V<WordPtr> temp = __ BitcastTaggedToWordPtrForTagAndSmiBits(load);
    V<Word32> C(truncate) = __ TruncateWordPtrToWord32(temp);
    V<WordPtr> other_temp =
        __ BitcastTaggedToWordPtrForTagAndSmiBits(other_load);
    V<Word32> C(other_truncate) = __ TruncateWordPtrToWord32(other_temp);
    V<Word32> C(result) =
        __ Conditional(truncate, __ Word32Constant(42), other_truncate);
    __ Return(__ TagSmi(result));
  });

  test.Run<LateLoadEliminationReducer>();

#if V8_COMPRESS_POINTERS

  // The other load should be replaced by an int32 load.
  const LoadOp* other_load = test.GetCapturedAs<LoadOp>("other_load");
  ASSERT_NE(other_load, nullptr);
  ASSERT_EQ(other_load->loaded_rep, MemoryRepresentation::Int32());
  ASSERT_EQ(other_load->result_rep, RegisterRepresentation::Word32());

  // The truncation chains should be eliminated.
  ASSERT_TRUE(test.GetCapture("truncate").IsEmpty());
  ASSERT_TRUE(test.GetCapture("other_truncate").IsEmpty());

  // The load has been eliminated.
  ASSERT_TRUE(test.GetCapture("load").IsEmpty());

  // The select uses the other load as condition and the second input directly.
  const SelectOp* result = test.GetCapturedAs<SelectOp>("result");
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(&test.graph().Get(result->cond()), other_load);
  ASSERT_EQ(&test.graph().Get(result->vfalse()), other_load);

#endif
}

/* TruncateInt64ToInt32(
 *     BitcastTaggedToWordPtrForTagAndSmiBits(
 *         Load[Tagged]))
 * cannot be optimized because the BitcastTaggedToWordPtrForTagAndSmiBits has an
 * additional (potentially non-truncating) use.
 */
TEST_F(LateLoadEliminationReducerTest,
       Int32TruncatedLoad_NonFoldable_AdditionalBitcastUse) {
  auto test = CreateFromGraph(1, [](auto& Asm) {
    V<Object> C(load) = __ Load(
        Asm.GetParameter(0), {}, LoadOp::Kind::TaggedBase(),
        MemoryRepresentation::AnyTagged(), RegisterRepresentation::Tagged(), 0);
    V<WordPtr> temp = __ BitcastTaggedToWordPtrForTagAndSmiBits(load);
    V<Word32> C(truncate) = __ TruncateWordPtrToWord32(temp);
    V<WordPtr> C(result) = __ Conditional(
        truncate, __ BitcastTaggedToWordPtr(Asm.GetParameter(0)), temp);
    __ Return(__ BitcastWordPtrToSmi(result));
  });

  test.Run<LateLoadEliminationReducer>();

#ifdef V8_COMPRESS_POINTERS

  // The load should still be tagged.
  const LoadOp* other_load = test.GetCapturedAs<LoadOp>("load");
  ASSERT_NE(other_load, nullptr);
  ASSERT_EQ(other_load->loaded_rep, MemoryRepresentation::AnyTagged());
  ASSERT_EQ(other_load->result_rep, RegisterRepresentation::Tagged());

  // The truncation chain should still be present.
  const ChangeOp* truncate = test.GetCapturedAs<ChangeOp>("truncate");
  ASSERT_NE(truncate, nullptr);

  // The select's input is unchanged.
  const SelectOp* result = test.GetCapturedAs<SelectOp>("result");
  ASSERT_NE(result, nullptr);
  ASSERT_EQ(&test.graph().Get(result->cond()), truncate);

#endif
}

#undef C

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

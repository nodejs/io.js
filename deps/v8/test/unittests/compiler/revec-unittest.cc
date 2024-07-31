// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/machine-type.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/machine-graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/revectorizer.h"
#include "src/compiler/wasm-compiler.h"
#include "src/wasm/wasm-module.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"
#include "testing/gmock-support.h"

using testing::AllOf;
using testing::Capture;
using testing::CaptureEq;

namespace v8 {
namespace internal {
namespace compiler {

class RevecTest : public TestWithIsolateAndZone {
 public:
  RevecTest()
      : TestWithIsolateAndZone(kCompressGraphZone),
        graph_(zone()),
        common_(zone()),
        machine_(zone(), MachineRepresentation::kWord64,
                 MachineOperatorBuilder::Flag::kAllOptionalOps),
        mcgraph_(&graph_, &common_, &machine_),
        source_positions_(
            mcgraph()->zone()->New<SourcePositionTable>(mcgraph()->graph())) {}

  void TestBinOp(const Operator* bin_op,
                 const IrOpcode::Value expected_simd256_op_code);
  void TestShiftOp(const Operator* shift_op,
                   const IrOpcode::Value expected_simd256_op_code);
  void TestSplatOp(const Operator* splat_op,
                   MachineType splat_input_machine_type,
                   const IrOpcode::Value expected_simd256_op_code);
  void TestLoadSplat(LoadTransformation transform, const Operator* bin_op,
                     LoadTransformation expected_transform);

  Graph* graph() { return &graph_; }
  CommonOperatorBuilder* common() { return &common_; }
  MachineOperatorBuilder* machine() { return &machine_; }
  MachineGraph* mcgraph() { return &mcgraph_; }
  SourcePositionTable* source_positions() { return source_positions_; }

 private:
  Graph graph_;
  CommonOperatorBuilder common_;
  MachineOperatorBuilder machine_;
  MachineGraph mcgraph_;
  SourcePositionTable* source_positions_;
};

// Create a graph which perform binary operation on two 256 bit vectors(a, b),
// store the result in c: simd128 *a,*b,*c; *c = *a bin_op *b;
// *(c+1) = *(a+1) bin_op *(b+1);
// In Revectorization, two simd 128 nodes can be combined into one 256 node:
// simd256 *d, *e, *f;
// *f = *d bin_op *e;
void RevecTest::TestBinOp(const Operator* bin_op,
                          const IrOpcode::Value expected_simd256_op_code) {
  if (!CpuFeatures::IsSupported(AVX2)) return;
  Node* start = graph()->NewNode(common()->Start(5));
  graph()->SetStart(start);

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* sixteen = graph()->NewNode(common()->Int64Constant(16));
  // offset of memory start field in WASM instance object.
  Node* offset = graph()->NewNode(common()->Int64Constant(23));

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* p1 = graph()->NewNode(common()->Parameter(1), start);
  Node* p2 = graph()->NewNode(common()->Parameter(2), start);
  Node* p3 = graph()->NewNode(common()->Parameter(3), start);

  StoreRepresentation store_rep(MachineRepresentation::kSimd128,
                                WriteBarrierKind::kNoWriteBarrier);
  LoadRepresentation load_rep(MachineType::Simd128());
  Node* load0 = graph()->NewNode(machine()->Load(MachineType::Int64()), p0,
                                 offset, start, start);
  Node* mem_buffer1 = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* mem_buffer2 = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* mem_store = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* load1 = graph()->NewNode(machine()->ProtectedLoad(load_rep), load0, p1,
                                 load0, start);
  Node* load2 = graph()->NewNode(machine()->ProtectedLoad(load_rep),
                                 mem_buffer1, p1, load1, start);
  Node* load3 = graph()->NewNode(machine()->ProtectedLoad(load_rep), load0, p2,
                                 load2, start);
  Node* load4 = graph()->NewNode(machine()->ProtectedLoad(load_rep),
                                 mem_buffer2, p2, load3, start);
  Node* bin_op1 = graph()->NewNode(bin_op, load1, load3);
  Node* bin_op2 = graph()->NewNode(bin_op, load2, load4);
  Node* store1 = graph()->NewNode(machine()->Store(store_rep), load0, p3,
                                  bin_op1, load4, start);
  Node* store2 = graph()->NewNode(machine()->Store(store_rep), mem_store, p3,
                                  bin_op2, store1, start);
  Node* ret = graph()->NewNode(common()->Return(0), zero, store2, start);
  Node* end = graph()->NewNode(common()->End(1), ret);
  graph()->SetEnd(end);

  graph()->RecordSimdStore(store1);
  graph()->RecordSimdStore(store2);
  graph()->SetSimd(true);

  // Test whether the graph can be revectorized
  Revectorizer revec(zone(), graph(), mcgraph(), source_positions());
  EXPECT_TRUE(revec.TryRevectorize(nullptr));

  // Test whether the graph has been revectorized
  Node* store_256 = ret->InputAt(1);
  EXPECT_EQ(StoreRepresentationOf(store_256->op()).representation(),
            MachineRepresentation::kSimd256);
  EXPECT_EQ(store_256->InputAt(2)->opcode(), expected_simd256_op_code);
}

#define BIN_OP_LIST(V)     \
  V(F64x2Add, F64x4Add)    \
  V(F32x4Add, F32x8Add)    \
  V(I64x2Add, I64x4Add)    \
  V(I32x4Add, I32x8Add)    \
  V(I16x8Add, I16x16Add)   \
  V(I8x16Add, I8x32Add)    \
  V(F64x2Sub, F64x4Sub)    \
  V(F32x4Sub, F32x8Sub)    \
  V(I64x2Sub, I64x4Sub)    \
  V(I32x4Sub, I32x8Sub)    \
  V(I16x8Sub, I16x16Sub)   \
  V(I8x16Sub, I8x32Sub)    \
  V(F64x2Mul, F64x4Mul)    \
  V(F32x4Mul, F32x8Mul)    \
  V(I64x2Mul, I64x4Mul)    \
  V(I32x4Mul, I32x8Mul)    \
  V(I16x8Mul, I16x16Mul)   \
  V(F64x2Div, F64x4Div)    \
  V(F32x4Div, F32x8Div)    \
  V(F64x2Eq, F64x4Eq)      \
  V(F32x4Eq, F32x8Eq)      \
  V(I64x2Eq, I64x4Eq)      \
  V(I32x4Eq, I32x8Eq)      \
  V(I16x8Eq, I16x16Eq)     \
  V(I8x16Eq, I8x32Eq)      \
  V(F64x2Ne, F64x4Ne)      \
  V(F32x4Ne, F32x8Ne)      \
  V(I64x2GtS, I64x4GtS)    \
  V(I32x4GtS, I32x8GtS)    \
  V(I16x8GtS, I16x16GtS)   \
  V(I8x16GtS, I8x32GtS)    \
  V(F64x2Lt, F64x4Lt)      \
  V(F32x4Lt, F32x8Lt)      \
  V(F64x2Le, F64x4Le)      \
  V(F32x4Le, F32x8Le)      \
  V(I32x4MinS, I32x8MinS)  \
  V(I16x8MinS, I16x16MinS) \
  V(I8x16MinS, I8x32MinS)  \
  V(I32x4MinU, I32x8MinU)  \
  V(I16x8MinU, I16x16MinU) \
  V(I8x16MinU, I8x32MinU)  \
  V(I32x4MaxS, I32x8MaxS)  \
  V(I16x8MaxS, I16x16MaxS) \
  V(I8x16MaxS, I8x32MaxS)  \
  V(I32x4MaxU, I32x8MaxU)  \
  V(I16x8MaxU, I16x16MaxU) \
  V(I8x16MaxU, I8x32MaxU)  \
  V(F64x2Min, F64x4Min)    \
  V(F64x2Max, F64x4Max)    \
  V(F32x4Min, F32x8Min)    \
  V(F32x4Max, F32x8Max)

#define TEST_BIN_OP(op128, op256)                      \
  TEST_F(RevecTest, op256) {                           \
    TestBinOp(machine()->op128(), IrOpcode::k##op256); \
  }

BIN_OP_LIST(TEST_BIN_OP)

#undef TEST_BIN_OP
#undef BIN_OP_LIST

// Create a graph with load chain that can not be packed due to effect
// dependency:
//   [Load4] -> [Load3] -> [Load2] -> [Irrelevant Load] -> [Load1]
//
// After reordering, no effect dependency will be broken so the graph can be
// revectorized:
//   [Load4] -> [Load3] -> [Load2] -> [Load1] -> [Irrelevant Load]
TEST_F(RevecTest, ReorderLoadChain1) {
  if (!CpuFeatures::IsSupported(AVX2)) return;

  Node* start = graph()->NewNode(common()->Start(5));
  graph()->SetStart(start);

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* sixteen = graph()->NewNode(common()->Int64Constant(16));
  // offset of memory start field in Wasm instance object.
  Node* offset = graph()->NewNode(common()->Int64Constant(23));

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* p1 = graph()->NewNode(common()->Parameter(1), start);
  Node* p2 = graph()->NewNode(common()->Parameter(2), start);
  Node* p3 = graph()->NewNode(common()->Parameter(3), start);

  StoreRepresentation store_rep(MachineRepresentation::kSimd128,
                                WriteBarrierKind::kNoWriteBarrier);
  LoadRepresentation load_rep(MachineType::Simd128());
  Node* load0 = graph()->NewNode(machine()->Load(MachineType::Int64()), p0,
                                 offset, start, start);
  Node* mem_buffer1 = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* mem_buffer2 = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* mem_store = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* load1 = graph()->NewNode(machine()->ProtectedLoad(load_rep), load0, p1,
                                 load0, start);
  Node* irrelevant_load = graph()->NewNode(machine()->ProtectedLoad(load_rep),
                                           mem_buffer1, p1, load1, start);
  Node* load2 = graph()->NewNode(machine()->ProtectedLoad(load_rep),
                                 mem_buffer1, p1, irrelevant_load, start);
  Node* load3 = graph()->NewNode(machine()->ProtectedLoad(load_rep), load0, p2,
                                 load2, start);
  Node* load4 = graph()->NewNode(machine()->ProtectedLoad(load_rep),
                                 mem_buffer2, p2, load3, start);
  Node* add1 = graph()->NewNode(machine()->F32x4Add(), load1, load3);
  Node* add2 = graph()->NewNode(machine()->F32x4Add(), load2, load4);
  Node* store1 = graph()->NewNode(machine()->Store(store_rep), load0, p3, add1,
                                  load4, start);
  Node* store2 = graph()->NewNode(machine()->Store(store_rep), mem_store, p3,
                                  add2, store1, start);
  Node* ret = graph()->NewNode(common()->Return(0), zero, store2, start);
  Node* end = graph()->NewNode(common()->End(1), ret);
  graph()->SetEnd(end);

  graph()->RecordSimdStore(store1);
  graph()->RecordSimdStore(store2);
  graph()->SetSimd(true);

  // Test whether the graph can be revectorized
  Revectorizer revec(zone(), graph(), mcgraph(), source_positions());
  EXPECT_TRUE(revec.TryRevectorize(nullptr));
}

// Create a graph with load chain that can not be packed due to effect
// dependency:
//   [Load4] -> [Load2] -> [Load1] -> [Irrelevant Load] -> [Load3]
//
// After reordering, no effect dependency will be broken so the graph can be
// revectorized:
//   [Load4] -> [Load3] -> [Load2] -> [Load1] -> [Irrelevant Load]
TEST_F(RevecTest, ReorderLoadChain2) {
  if (!CpuFeatures::IsSupported(AVX2)) return;

  Node* start = graph()->NewNode(common()->Start(5));
  graph()->SetStart(start);

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* sixteen = graph()->NewNode(common()->Int64Constant(16));
  // offset of memory start field in Wasm instance object.
  Node* offset = graph()->NewNode(common()->Int64Constant(23));

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* p1 = graph()->NewNode(common()->Parameter(1), start);
  Node* p2 = graph()->NewNode(common()->Parameter(2), start);
  Node* p3 = graph()->NewNode(common()->Parameter(3), start);

  StoreRepresentation store_rep(MachineRepresentation::kSimd128,
                                WriteBarrierKind::kNoWriteBarrier);
  LoadRepresentation load_rep(MachineType::Simd128());
  Node* load0 = graph()->NewNode(machine()->Load(MachineType::Int64()), p0,
                                 offset, start, start);
  Node* mem_buffer1 = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* mem_buffer2 = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* mem_store = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* load3 = graph()->NewNode(machine()->ProtectedLoad(load_rep), load0, p2,
                                 load0, start);
  Node* irrelevant_load = graph()->NewNode(machine()->ProtectedLoad(load_rep),
                                           mem_buffer1, p1, load3, start);
  Node* load1 = graph()->NewNode(machine()->ProtectedLoad(load_rep), load0, p1,
                                 irrelevant_load, start);
  Node* load2 = graph()->NewNode(machine()->ProtectedLoad(load_rep),
                                 mem_buffer1, p1, load1, start);
  Node* load4 = graph()->NewNode(machine()->ProtectedLoad(load_rep),
                                 mem_buffer2, p2, load2, start);
  Node* add1 = graph()->NewNode(machine()->F32x4Add(), load1, load3);
  Node* add2 = graph()->NewNode(machine()->F32x4Add(), load2, load4);
  Node* store1 = graph()->NewNode(machine()->Store(store_rep), load0, p3, add1,
                                  load4, start);
  Node* store2 = graph()->NewNode(machine()->Store(store_rep), mem_store, p3,
                                  add2, store1, start);
  Node* ret = graph()->NewNode(common()->Return(0), zero, store2, start);
  Node* end = graph()->NewNode(common()->End(1), ret);
  graph()->SetEnd(end);

  graph()->RecordSimdStore(store1);
  graph()->RecordSimdStore(store2);
  graph()->SetSimd(true);

  // Test whether the graph can be revectorized
  Revectorizer revec(zone(), graph(), mcgraph(), source_positions());
  EXPECT_TRUE(revec.TryRevectorize(nullptr));
}

// Test shift using an immediate and a value loaded from memory (b) on a 256-bit
// vector a and store the result in another 256-bit vector c:
//   simd128 *a, *c;
//   int32 *b;
//   *c = (*a shift_op 1) shift_op *b;
//   *(c+1) = (*(a+1) shift_op 1) shift_op *b;
// In Revectorization, two simd128 nodes can be coalesced into one simd256 node
// as below:
//   simd256 *a, *c; *c = (*a shift_op 1) shift_op *b;
void RevecTest::TestShiftOp(const Operator* shift_op,
                            const IrOpcode::Value expected_simd256_op_code) {
  Node* start = graph()->NewNode(common()->Start(4));
  graph()->SetStart(start);

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* one = graph()->NewNode(common()->Int32Constant(1));
  Node* sixteen = graph()->NewNode(common()->Int64Constant(16));
  Node* offset = graph()->NewNode(common()->Int64Constant(23));

  // Wasm array base address
  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* a = graph()->NewNode(common()->Parameter(1), start);
  Node* b = graph()->NewNode(common()->Parameter(2), start);
  Node* c = graph()->NewNode(common()->Parameter(3), start);

  LoadRepresentation load_rep(MachineType::Simd128());
  StoreRepresentation store_rep(MachineRepresentation::kSimd128,
                                WriteBarrierKind::kNoWriteBarrier);
  Node* base = graph()->NewNode(machine()->Load(MachineType::Int64()), p0,
                                offset, start, start);
  Node* base16 = graph()->NewNode(machine()->Int64Add(), base, sixteen);
  Node* load0 = graph()->NewNode(machine()->ProtectedLoad(load_rep), base, a,
                                 base, start);
  Node* load1 = graph()->NewNode(machine()->ProtectedLoad(load_rep), base16, a,
                                 load0, start);
  Node* shift0 = graph()->NewNode(shift_op, load0, one);
  Node* shift1 = graph()->NewNode(shift_op, load1, one);
  Node* load2 =
      graph()->NewNode(machine()->ProtectedLoad(LoadRepresentation::Int32()),
                       base, b, load1, start);
  Node* store0 =
      graph()->NewNode(machine()->Store(store_rep), base, c,
                       graph()->NewNode(shift_op, shift0, load2), load2, start);
  Node* store1 = graph()->NewNode(machine()->Store(store_rep), base16, c,
                                  graph()->NewNode(shift_op, shift1, load2),
                                  store0, start);
  Node* ret = graph()->NewNode(common()->Return(0), zero, store1, start);
  Node* end = graph()->NewNode(common()->End(1), ret);
  graph()->SetEnd(end);

  graph()->RecordSimdStore(store0);
  graph()->RecordSimdStore(store1);
  graph()->SetSimd(true);

  Revectorizer revec(zone(), graph(), mcgraph(), source_positions());
  bool result = revec.TryRevectorize(nullptr);

  if (CpuFeatures::IsSupported(AVX2)) {
    EXPECT_TRUE(result);
    Node* store_256 = ret->InputAt(1);
    EXPECT_EQ(StoreRepresentationOf(store_256->op()).representation(),
              MachineRepresentation::kSimd256);
    EXPECT_EQ(store_256->InputAt(2)->opcode(), expected_simd256_op_code);
    return;
  }

  EXPECT_FALSE(result);
}

TEST_F(RevecTest, I64x4Shl) {
  TestShiftOp(machine()->I64x2Shl(), IrOpcode::kI64x4Shl);
}
TEST_F(RevecTest, I32x8ShrS) {
  TestShiftOp(machine()->I32x4Shl(), IrOpcode::kI32x8Shl);
}
TEST_F(RevecTest, I16x16ShrU) {
  TestShiftOp(machine()->I16x8Shl(), IrOpcode::kI16x16Shl);
}

void RevecTest::TestSplatOp(const Operator* splat_op,
                            MachineType splat_input_machine_type,
                            const IrOpcode::Value expected_simd256_op_code) {
  if (!CpuFeatures::IsSupported(AVX2)) {
    return;
  }
  Node* start = graph()->NewNode(common()->Start(3));
  graph()->SetStart(start);

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* sixteen = graph()->NewNode(common()->Int64Constant(16));
  Node* offset = graph()->NewNode(common()->Int64Constant(23));

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* p1 = graph()->NewNode(common()->Parameter(1), start);
  Node* p2 = graph()->NewNode(common()->Parameter(2), start);

  Node* base = graph()->NewNode(machine()->Load(MachineType::Uint64()), p0,
                                offset, start, start);

  Node* load =
      graph()->NewNode(machine()->ProtectedLoad(splat_input_machine_type), base,
                       p1, base, start);
  Node* splat0 = graph()->NewNode(splat_op, load);
  Node* splat1 = graph()->NewNode(splat_op, load);

  StoreRepresentation store_rep(MachineRepresentation::kSimd128,
                                WriteBarrierKind::kNoWriteBarrier);

  Node* store0 = graph()->NewNode(machine()->Store(store_rep), base, p2, splat0,
                                  load, start);
  Node* store1 =
      graph()->NewNode(machine()->Store(store_rep),
                       graph()->NewNode(machine()->Int64Add(), base, sixteen),
                       p2, splat1, store0, start);

  Node* ret = graph()->NewNode(common()->Return(0), zero, store0, start);
  Node* end = graph()->NewNode(common()->End(1), ret);
  graph()->SetEnd(end);

  graph()->RecordSimdStore(store0);
  graph()->RecordSimdStore(store1);
  graph()->SetSimd(true);

  Revectorizer revec(zone(), graph(), mcgraph(), source_positions());
  bool result = revec.TryRevectorize(nullptr);

  EXPECT_TRUE(result);
  Node* store_256 = ret->InputAt(1);
  EXPECT_EQ(StoreRepresentationOf(store_256->op()).representation(),
            MachineRepresentation::kSimd256);
  EXPECT_EQ(store_256->InputAt(2)->opcode(), expected_simd256_op_code);
  return;
}

#define SPLAT_OP_LIST(V)            \
  V(I8x16Splat, I8x32Splat, Int8)   \
  V(I16x8Splat, I16x16Splat, Int16) \
  V(I32x4Splat, I32x8Splat, Int32)  \
  V(I64x2Splat, I64x4Splat, Int64)

#define TEST_SPLAT(op128, op256, machine_type)                   \
  TEST_F(RevecTest, op256) {                                     \
    TestSplatOp(machine()->op128(), MachineType::machine_type(), \
                IrOpcode::k##op256);                             \
  }

SPLAT_OP_LIST(TEST_SPLAT)

#undef TEST_SPLAT
#undef SPLAT_OP_LIST

// Create a graph which multiplies a F32x8 vector with a shuffle splat vector.
//   float *a, *b, *c;
//   c[0123] = a[0123] * b[1111];
//   c[4567] = a[4567] * b[1111];
//
// After the revectorization phase, two consecutive 128-bit loads and multiplies
// can be coalesced using 256-bit operators:
//   c[01234567] = a[01234567] * b[11111111];
TEST_F(RevecTest, ShuffleForSplat) {
  if (!CpuFeatures::IsSupported(AVX2)) return;

  Node* start = graph()->NewNode(common()->Start(4));
  graph()->SetStart(start);

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* sixteen = graph()->NewNode(common()->Int64Constant(16));
  Node* offset = graph()->NewNode(common()->Int64Constant(23));

  // Wasm array base address
  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  // Load base address a*
  Node* p1 = graph()->NewNode(common()->Parameter(1), start);
  // Load for shuffle base address b*
  Node* p2 = graph()->NewNode(common()->Parameter(2), start);
  // Store base address c*
  Node* p3 = graph()->NewNode(common()->Parameter(3), start);

  LoadRepresentation load_rep(MachineType::Simd128());
  StoreRepresentation store_rep(MachineRepresentation::kSimd128,
                                WriteBarrierKind::kNoWriteBarrier);
  Node* base = graph()->NewNode(machine()->Load(MachineType::Int64()), p0,
                                offset, start, start);
  Node* load0 = graph()->NewNode(machine()->ProtectedLoad(load_rep), base, p1,
                                 base, start);
  Node* base16 = graph()->NewNode(machine()->Int64Add(), base, sixteen);
  Node* load1 = graph()->NewNode(machine()->ProtectedLoad(load_rep), base16, p1,
                                 load0, start);

  // Load and shuffle for splat
  Node* load2 = graph()->NewNode(machine()->ProtectedLoad(load_rep), base, p2,
                                 load1, start);
  const uint8_t mask[16] = {4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7, 4, 5, 6, 7};
  Node* shuffle = graph()->NewNode(machine()->I8x16Shuffle(mask), load2, load2);

  Node* mul0 = graph()->NewNode(machine()->F32x4Mul(), load0, shuffle);
  Node* mul1 = graph()->NewNode(machine()->F32x4Mul(), load1, shuffle);
  Node* store0 = graph()->NewNode(machine()->Store(store_rep), base, p3, mul0,
                                  load2, start);
  Node* base16_store = graph()->NewNode(machine()->Int64Add(), base, sixteen);
  Node* store1 = graph()->NewNode(machine()->Store(store_rep), base16_store, p3,
                                  mul1, store0, start);
  Node* ret = graph()->NewNode(common()->Return(0), zero, store1, start);
  Node* end = graph()->NewNode(common()->End(1), ret);
  graph()->SetEnd(end);

  graph()->RecordSimdStore(store0);
  graph()->RecordSimdStore(store1);
  graph()->SetSimd(true);

  Revectorizer revec(zone(), graph(), mcgraph(), source_positions());
  EXPECT_TRUE(revec.TryRevectorize(nullptr));

  // Test whether the graph has been revectorized
  Node* store_256 = ret->InputAt(1);
  EXPECT_EQ(StoreRepresentationOf(store_256->op()).representation(),
            MachineRepresentation::kSimd256);
}

void RevecTest::TestLoadSplat(
    const LoadTransformation load_transform, const Operator* bin_op,
    const LoadTransformation expected_load_transform) {
  if (!CpuFeatures::IsSupported(AVX2)) {
    return;
  }
  Node* start = graph()->NewNode(common()->Start(3));
  graph()->SetStart(start);

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* sixteen = graph()->NewNode(common()->Int64Constant(16));
  Node* offset = graph()->NewNode(common()->Int64Constant(23));

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* a = graph()->NewNode(common()->Parameter(1), start);
  Node* b = graph()->NewNode(common()->Parameter(2), start);
  Node* c = graph()->NewNode(common()->Parameter(3), start);

  Node* base = graph()->NewNode(machine()->Load(MachineType::Uint64()), p0,
                                offset, start, start);

  Node* loadSplat = graph()->NewNode(
      machine()->LoadTransform(MemoryAccessKind::kProtected, load_transform),
      base, a, base, start);

  LoadRepresentation load_rep(MachineType::Simd128());
  Node* load0 = graph()->NewNode(machine()->ProtectedLoad(load_rep), base, b,
                                 loadSplat, start);
  Node* load1 = graph()->NewNode(
      machine()->ProtectedLoad(load_rep),
      graph()->NewNode(machine()->Int64Add(), base, sixteen), b, load0, start);

  StoreRepresentation store_rep(MachineRepresentation::kSimd128,
                                WriteBarrierKind::kNoWriteBarrier);
  Node* store0 = graph()->NewNode(machine()->Store(store_rep), base, c,
                                  graph()->NewNode(bin_op, load0, loadSplat),
                                  load1, start);
  Node* store1 = graph()->NewNode(
      machine()->Store(store_rep),
      graph()->NewNode(machine()->Int64Add(), base, sixteen), c,
      graph()->NewNode(bin_op, load1, loadSplat), store0, start);

  Node* ret = graph()->NewNode(common()->Return(0), zero, store0, start);
  Node* end = graph()->NewNode(common()->End(1), ret);
  graph()->SetEnd(end);

  graph()->RecordSimdStore(store0);
  graph()->RecordSimdStore(store1);
  graph()->SetSimd(true);

  Revectorizer revec(zone(), graph(), mcgraph(), source_positions());
  bool result = revec.TryRevectorize(nullptr);

  EXPECT_TRUE(result);
  Node* store_256 = ret->InputAt(1);
  EXPECT_EQ(StoreRepresentationOf(store_256->op()).representation(),
            MachineRepresentation::kSimd256);
  EXPECT_EQ(LoadTransformParametersOf(store_256->InputAt(2)->InputAt(1)->op())
                .transformation,
            expected_load_transform);
}

TEST_F(RevecTest, Load8Splat) {
  TestLoadSplat(LoadTransformation::kS128Load8Splat, machine()->I8x16Add(),
                LoadTransformation::kS256Load8Splat);
}
TEST_F(RevecTest, Load64Splat) {
  TestLoadSplat(LoadTransformation::kS128Load64Splat, machine()->I64x2Add(),
                LoadTransformation::kS256Load64Splat);
}

// Create a graph with Store nodes that can not be packed due to effect
// intermediate:
//   [Store0] -> [Load] -> [Store1]
TEST_F(RevecTest, StoreDependencyCheck) {
  if (!CpuFeatures::IsSupported(AVX2)) return;

  Node* start = graph()->NewNode(common()->Start(5));
  graph()->SetStart(start);

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* sixteen = graph()->NewNode(common()->Int64Constant(16));
  // offset of memory start field in WASM instance object.
  Node* offset = graph()->NewNode(common()->Int64Constant(23));

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* p1 = graph()->NewNode(common()->Parameter(1), start);
  Node* p2 = graph()->NewNode(common()->Parameter(2), start);
  Node* p3 = graph()->NewNode(common()->Parameter(3), start);

  StoreRepresentation store_rep(MachineRepresentation::kSimd128,
                                WriteBarrierKind::kNoWriteBarrier);
  LoadRepresentation load_rep(MachineType::Simd128());
  Node* load0 = graph()->NewNode(machine()->Load(MachineType::Int64()), p0,
                                 offset, start, start);
  Node* mem_buffer1 = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* mem_buffer2 = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* mem_store = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* load1 = graph()->NewNode(machine()->ProtectedLoad(load_rep), load0, p1,
                                 load0, start);
  Node* load2 = graph()->NewNode(machine()->ProtectedLoad(load_rep),
                                 mem_buffer1, p1, load1, start);
  Node* load3 = graph()->NewNode(machine()->ProtectedLoad(load_rep), load0, p2,
                                 load2, start);
  Node* load4 = graph()->NewNode(machine()->ProtectedLoad(load_rep),
                                 mem_buffer2, p2, load3, start);
  Node* add1 = graph()->NewNode(machine()->F32x4Add(), load1, load3);
  Node* add2 = graph()->NewNode(machine()->F32x4Add(), load2, load4);
  Node* store1 = graph()->NewNode(machine()->Store(store_rep), load0, p3, add1,
                                  load4, start);
  Node* effect_intermediate = graph()->NewNode(
      machine()->ProtectedLoad(load_rep), mem_buffer2, p2, store1, start);
  Node* store2 = graph()->NewNode(machine()->Store(store_rep), mem_store, p3,
                                  add2, effect_intermediate, start);
  Node* ret = graph()->NewNode(common()->Return(0), zero, store2, start);
  Node* end = graph()->NewNode(common()->End(1), ret);
  graph()->SetEnd(end);

  graph()->RecordSimdStore(store1);
  graph()->RecordSimdStore(store2);
  graph()->SetSimd(true);

  // Test whether the graph can be revectorized
  Revectorizer revec(zone(), graph(), mcgraph(), source_positions());
  EXPECT_FALSE(revec.TryRevectorize(nullptr));
}

TEST_F(RevecTest, S128Zero) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;

  Node* start = graph()->NewNode(common()->Start(5));
  graph()->SetStart(start);

  Node* control = graph()->start();
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* sixteen = graph()->NewNode(common()->Int64Constant(16));
  Node* zero128 = graph()->NewNode(machine()->S128Zero());
  // offset of memory start field in WASM instance object.
  Node* offset = graph()->NewNode(common()->Int64Constant(23));

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* p1 = graph()->NewNode(common()->Parameter(1), start);
  Node* base = graph()->NewNode(machine()->Load(MachineType::Uint64()), p0,
                                offset, start, control);
  StoreRepresentation store_rep(MachineRepresentation::kSimd128,
                                WriteBarrierKind::kNoWriteBarrier);
  Node* store1 = graph()->NewNode(machine()->Store(store_rep), base, p1,
                                  zero128, base, control);
  Node* object = graph()->NewNode(machine()->Int64Add(), base, sixteen);
  Node* store2 = graph()->NewNode(machine()->Store(store_rep), object, p1,
                                  zero128, store1, control);
  Node* ret = graph()->NewNode(common()->Return(0), zero, store2, control);
  Node* end = graph()->NewNode(common()->End(1), ret);
  graph()->SetEnd(end);

  graph()->RecordSimdStore(store1);
  graph()->RecordSimdStore(store2);
  graph()->SetSimd(true);

  // Test whether the graph can be revectorized
  Revectorizer revec(zone(), graph(), mcgraph(), source_positions());
  EXPECT_TRUE(revec.TryRevectorize(nullptr));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8

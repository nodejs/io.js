// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_TURBOSHAFT_WASM_REVEC_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_WASM_REVEC_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/use-map.h"
#include "src/compiler/wasm-graph-assembler.h"

namespace v8::internal::compiler::turboshaft {

#define SIMD256_LOADTRANSFORM_OP(V) \
  V(8x8S, 8x16S)                    \
  V(8x8U, 8x16U)                    \
  V(16x4S, 16x8S)                   \
  V(16x4U, 16x8U)                   \
  V(32x2S, 32x4S)                   \
  V(32x2U, 32x4U)                   \
  V(8Splat, 8Splat)                 \
  V(16Splat, 16Splat)               \
  V(32Splat, 32Splat)               \
  V(64Splat, 64Splat)

#define SIMD256_UNARY_OP(V)                                \
  V(S128Not, S256Not)                                      \
  V(I8x16Abs, I8x32Abs)                                    \
  V(I8x16Neg, I8x32Neg)                                    \
  V(I16x8ExtAddPairwiseI8x16S, I16x16ExtAddPairwiseI8x32S) \
  V(I16x8ExtAddPairwiseI8x16U, I16x16ExtAddPairwiseI8x32U) \
  V(I32x4ExtAddPairwiseI16x8S, I32x8ExtAddPairwiseI16x16S) \
  V(I32x4ExtAddPairwiseI16x8U, I32x8ExtAddPairwiseI16x16U) \
  V(I16x8Abs, I16x16Abs)                                   \
  V(I16x8Neg, I16x16Neg)                                   \
  V(I32x4Abs, I32x8Abs)                                    \
  V(I32x4Neg, I32x8Neg)                                    \
  V(F32x4Abs, F32x8Abs)                                    \
  V(F32x4Neg, F32x8Neg)                                    \
  V(F32x4Sqrt, F32x8Sqrt)                                  \
  V(F64x2Sqrt, F64x4Sqrt)                                  \
  V(I32x4UConvertF32x4, I32x8UConvertF32x8)                \
  V(F32x4UConvertI32x4, F32x8UConvertI32x8)

#define SIMD256_BINOP_SIMPLE_OP(V)                 \
  V(I8x16Eq, I8x32Eq)                              \
  V(I8x16Ne, I8x32Ne)                              \
  V(I8x16GtS, I8x32GtS)                            \
  V(I8x16GtU, I8x32GtU)                            \
  V(I8x16GeS, I8x32GeS)                            \
  V(I8x16GeU, I8x32GeU)                            \
  V(I16x8Eq, I16x16Eq)                             \
  V(I16x8Ne, I16x16Ne)                             \
  V(I16x8GtS, I16x16GtS)                           \
  V(I16x8GtU, I16x16GtU)                           \
  V(I16x8GeS, I16x16GeS)                           \
  V(I16x8GeU, I16x16GeU)                           \
  V(I32x4Eq, I32x8Eq)                              \
  V(I32x4Ne, I32x8Ne)                              \
  V(I32x4GtS, I32x8GtS)                            \
  V(I32x4GtU, I32x8GtU)                            \
  V(I32x4GeS, I32x8GeS)                            \
  V(I32x4GeU, I32x8GeU)                            \
  V(F32x4Eq, F32x8Eq)                              \
  V(F32x4Ne, F32x8Ne)                              \
  V(F32x4Lt, F32x8Lt)                              \
  V(F32x4Le, F32x8Le)                              \
  V(F64x2Eq, F64x4Eq)                              \
  V(F64x2Ne, F64x4Ne)                              \
  V(F64x2Lt, F64x4Lt)                              \
  V(F64x2Le, F64x4Le)                              \
  V(S128And, S256And)                              \
  V(S128AndNot, S256AndNot)                        \
  V(S128Or, S256Or)                                \
  V(S128Xor, S256Xor)                              \
  V(I8x16SConvertI16x8, I8x32SConvertI16x16)       \
  V(I8x16UConvertI16x8, I8x32UConvertI16x16)       \
  V(I8x16Add, I8x32Add)                            \
  V(I8x16AddSatS, I8x32AddSatS)                    \
  V(I8x16AddSatU, I8x32AddSatU)                    \
  V(I8x16Sub, I8x32Sub)                            \
  V(I8x16SubSatS, I8x32SubSatS)                    \
  V(I8x16SubSatU, I8x32SubSatU)                    \
  V(I8x16MinS, I8x32MinS)                          \
  V(I8x16MinU, I8x32MinU)                          \
  V(I8x16MaxS, I8x32MaxS)                          \
  V(I8x16MaxU, I8x32MaxU)                          \
  V(I8x16RoundingAverageU, I8x32RoundingAverageU)  \
  V(I16x8SConvertI32x4, I16x16SConvertI32x8)       \
  V(I16x8UConvertI32x4, I16x16UConvertI32x8)       \
  V(I16x8Add, I16x16Add)                           \
  V(I16x8AddSatS, I16x16AddSatS)                   \
  V(I16x8AddSatU, I16x16AddSatU)                   \
  V(I16x8Sub, I16x16Sub)                           \
  V(I16x8SubSatS, I16x16SubSatS)                   \
  V(I16x8SubSatU, I16x16SubSatU)                   \
  V(I16x8Mul, I16x16Mul)                           \
  V(I16x8MinS, I16x16MinS)                         \
  V(I16x8MinU, I16x16MinU)                         \
  V(I16x8MaxS, I16x16MaxS)                         \
  V(I16x8MaxU, I16x16MaxU)                         \
  V(I16x8RoundingAverageU, I16x16RoundingAverageU) \
  V(I32x4Add, I32x8Add)                            \
  V(I32x4Sub, I32x8Sub)                            \
  V(I32x4Mul, I32x8Mul)                            \
  V(I32x4MinS, I32x8MinS)                          \
  V(I32x4MinU, I32x8MinU)                          \
  V(I32x4MaxS, I32x8MaxS)                          \
  V(I32x4MaxU, I32x8MaxU)                          \
  V(I32x4DotI16x8S, I32x8DotI16x16S)               \
  V(I64x2Add, I64x4Add)                            \
  V(I64x2Sub, I64x4Sub)                            \
  V(I64x2Mul, I64x4Mul)                            \
  V(I64x2Eq, I64x4Eq)                              \
  V(I64x2Ne, I64x4Ne)                              \
  V(I64x2GtS, I64x4GtS)                            \
  V(I64x2GeS, I64x4GeS)                            \
  V(F32x4Add, F32x8Add)                            \
  V(F32x4Sub, F32x8Sub)                            \
  V(F32x4Mul, F32x8Mul)                            \
  V(F32x4Div, F32x8Div)                            \
  V(F32x4Min, F32x8Min)                            \
  V(F32x4Max, F32x8Max)                            \
  V(F32x4Pmin, F32x8Pmin)                          \
  V(F32x4Pmax, F32x8Pmax)                          \
  V(F64x2Add, F64x4Add)                            \
  V(F64x2Sub, F64x4Sub)                            \
  V(F64x2Mul, F64x4Mul)                            \
  V(F64x2Div, F64x4Div)                            \
  V(F64x2Min, F64x4Min)                            \
  V(F64x2Max, F64x4Max)                            \
  V(F64x2Pmin, F64x4Pmin)                          \
  V(F64x2Pmax, F64x4Pmax)

#define SIMD256_SHIFT_OP(V) \
  V(I16x8Shl, I16x16Shl)    \
  V(I16x8ShrS, I16x16ShrS)  \
  V(I16x8ShrU, I16x16ShrU)  \
  V(I32x4Shl, I32x8Shl)     \
  V(I32x4ShrS, I32x8ShrS)   \
  V(I32x4ShrU, I32x8ShrU)   \
  V(I64x2Shl, I64x4Shl)     \
  V(I64x2ShrU, I64x4ShrU)

#define SIMD256_TERNARY_OP(V) V(S128Select, S256Select)

#include "src/compiler/turboshaft/define-assembler-macros.inc"

class NodeGroup {
 public:
  // Current only support merge 2 Simd128 into Simd256
  static constexpr int kSize = kSimd256Size / kSimd128Size;
  NodeGroup(OpIndex a, OpIndex b) {
    indexes_[0] = a;
    indexes_[1] = b;
  }
  size_t size() const { return kSize; }
  OpIndex operator[](int i) const { return indexes_[i]; }

  bool operator==(const NodeGroup& other) const {
    return indexes_[0] == other.indexes_[0] && indexes_[1] == other.indexes_[1];
  }
  bool operator!=(const NodeGroup& other) const {
    return indexes_[0] != other.indexes_[0] || indexes_[1] != other.indexes_[1];
  }

  template <typename T>
  struct Iterator {
    T* p;
    T& operator*() { return *p; }
    bool operator!=(const Iterator& rhs) { return p != rhs.p; }
    void operator++() { ++p; }
  };

  auto begin() const { return Iterator<const OpIndex>{indexes_}; }
  auto end() const { return Iterator<const OpIndex>{indexes_ + kSize}; }

 private:
  OpIndex indexes_[kSize];
};

// A PackNode consists of a fixed number of isomorphic simd128 nodes which can
// execute in parallel and convert to a 256-bit simd node later. The nodes in a
// PackNode must satisfy that they can be scheduled in the same basic block and
// are mutually independent.
class PackNode : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  explicit PackNode(const NodeGroup& node_group)
      : nodes_(node_group), revectorized_node_() {}
  NodeGroup Nodes() const { return nodes_; }
  bool IsSame(const NodeGroup& node_group) const {
    return nodes_ == node_group;
  }
  bool IsSame(const PackNode& other) const { return nodes_ == other.nodes_; }
  OpIndex RevectorizedNode() const { return revectorized_node_; }
  void SetRevectorizedNode(OpIndex node) { revectorized_node_ = node; }

  void Print(Graph* graph) const;

 private:
  NodeGroup nodes_;
  OpIndex revectorized_node_;
};

class SLPTree : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  explicit SLPTree(Graph& graph, Zone* zone)
      : graph_(graph),
        phase_zone_(zone),
        root_(nullptr),
        node_to_packnode_(zone) {}

  PackNode* BuildTree(const NodeGroup& roots);
  void DeleteTree();

  PackNode* GetPackNode(OpIndex node);
  ZoneUnorderedMap<OpIndex, PackNode*>& GetNodeMapping() {
    return node_to_packnode_;
  }

  void Print(const char* info);

 private:
  // This is the recursive part of BuildTree.
  PackNode* BuildTreeRec(const NodeGroup& node_group, unsigned depth);

  // Baseline: create a new PackNode, and return.
  PackNode* NewPackNode(const NodeGroup& node_group);

  // Recursion: create a new PackNode and call BuildTreeRec recursively
  PackNode* NewPackNodeAndRecurs(const NodeGroup& node_group, int start_index,
                                 int count, unsigned depth);

  bool IsSideEffectFree(OpIndex first, OpIndex second);
  bool CanBePacked(const NodeGroup& node_group);
  bool IsEqual(const OpIndex node0, const OpIndex node1);

  Graph& graph() const { return graph_; }
  Zone* zone() const { return phase_zone_; }

  Graph& graph_;
  Zone* phase_zone_;
  PackNode* root_;
  // Maps a specific node to PackNode.
  ZoneUnorderedMap<OpIndex, PackNode*> node_to_packnode_;
  static constexpr size_t RecursionMaxDepth = 1000;
};

class WasmRevecAnalyzer {
 public:
  WasmRevecAnalyzer(Zone* zone, Graph& graph)
      : graph_(graph),
        phase_zone_(zone),
        store_seeds_(zone),
        slp_tree_(nullptr),
        revectorizable_node_(zone),
        should_reduce_(false),
        use_map_(nullptr) {
    Run();
  }

  void Run();

  bool CanMergeSLPTrees();
  bool ShouldReduce() const { return should_reduce_; }

  PackNode* GetPackNode(const OpIndex ig_index) {
    auto itr = revectorizable_node_.find(ig_index);
    if (itr != revectorizable_node_.end()) {
      return itr->second;
    }
    return nullptr;
  }

  const OpIndex GetReduced(const OpIndex node) {
    auto pnode = GetPackNode(node);
    if (!pnode) {
      return OpIndex::Invalid();
    }
    return pnode->RevectorizedNode();
  }

  const Operation& GetStartOperation(const PackNode* pnode, const OpIndex node,
                                     const Operation& op) {
    DCHECK(pnode);
    OpIndex start = pnode->Nodes()[0];
    if (start == node) return op;
    return graph_.Get(start);
  }

  base::Vector<const OpIndex> uses(OpIndex node) {
    return use_map_->uses(node);
  }

 private:
  void ProcessBlock(const Block& block);
  bool DecideVectorize();

  Graph& graph_;
  Zone* phase_zone_;
  ZoneVector<std::pair<const StoreOp*, const StoreOp*>> store_seeds_;
  const wasm::WasmModule* module_ = PipelineData::Get().wasm_module();
  const wasm::FunctionSig* signature_ = PipelineData::Get().wasm_sig();
  SLPTree* slp_tree_;
  ZoneUnorderedMap<OpIndex, PackNode*> revectorizable_node_;
  bool should_reduce_;
  SimdUseMap* use_map_;
};

template <class Next>
class WasmRevecReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(WasmRevec)

  OpIndex GetExtractOpIfNeeded(PackNode* pnode, OpIndex ig_index,
                               OpIndex og_index) {
    uint8_t lane = 0;
    for (; lane < static_cast<uint8_t>(pnode->Nodes().size()); lane++) {
      if (pnode->Nodes()[lane] == ig_index) break;
    }

    for (auto use : analyzer_.uses(ig_index)) {
      if (!analyzer_.GetPackNode(use)) {
        OpIndex extract_128 = __ Simd256Extract128Lane(og_index, lane);
        return extract_128;
      }
    }

    return OpIndex::Invalid();
  }

  V<Simd128> REDUCE_INPUT_GRAPH(Simd128LoadTransform)(
      V<Simd128> ig_index, const Simd128LoadTransformOp& load_transform) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      V<Simd256> og_index = pnode->RevectorizedNode();
      // Skip revectorized node.
      if (!og_index.valid()) {
        auto base = __ MapToNewGraph(load_transform.base());
        auto index = __ MapToNewGraph(load_transform.index());
        auto offset = load_transform.offset;
        DCHECK_EQ(load_transform.offset, 0);

        og_index = __ Simd256LoadTransform(
            base, index, load_transform.load_kind,
            Get256LoadTransformKindFrom128(load_transform.transform_kind),
            offset);
        pnode->SetRevectorizedNode(og_index);
      }
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }

    return Next::ReduceInputGraphSimd128LoadTransform(ig_index, load_transform);
  }

  OpIndex REDUCE_INPUT_GRAPH(Load)(OpIndex ig_index, const LoadOp& load) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      OpIndex og_index = pnode->RevectorizedNode();

      // Emit revectorized op.
      if (!og_index.valid()) {
        const LoadOp* start = analyzer_.GetStartOperation(pnode, ig_index, load)
                                  .TryCast<LoadOp>();
        DCHECK_EQ(start->base(), load.base());

        auto base = __ MapToNewGraph(start->base());
        auto index = __ MapToNewGraph(start->index());
        og_index = __ Load(base, index, load.kind,
                           MemoryRepresentation::Simd256(), start->offset);
        pnode->SetRevectorizedNode(og_index);
      }

      // Emit extract op if needed.
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }

    // no_change
    return Next::ReduceInputGraphLoad(ig_index, load);
  }

  OpIndex REDUCE_INPUT_GRAPH(Store)(OpIndex ig_index, const StoreOp& store) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      OpIndex og_index = pnode->RevectorizedNode();

      // Emit revectorized op.
      if (!og_index.valid()) {
        const StoreOp* start =
            (analyzer_.GetStartOperation(pnode, ig_index, store))
                .TryCast<StoreOp>();
        DCHECK_EQ(start->base(), store.base());

        auto base = __ MapToNewGraph(start->base());
        auto index = __ MapToNewGraph(start->index());
        OpIndex value = analyzer_.GetReduced(start->value());
        DCHECK(value.valid());

        __ Store(base, index, value, store.kind,
                 MemoryRepresentation::Simd256(), store.write_barrier,
                 start->offset);

        // Set an arbitrary valid opindex here to skip reduce later.
        pnode->SetRevectorizedNode(ig_index);
      }

      // No extract op needed for Store.
      return OpIndex::Invalid();
    }

    // no_change
    return Next::ReduceInputGraphStore(ig_index, store);
  }

  OpIndex REDUCE_INPUT_GRAPH(Simd128Unary)(OpIndex ig_index,
                                           const Simd128UnaryOp& unary) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      OpIndex og_index = pnode->RevectorizedNode();
      // Skip revectorized node.
      if (!og_index.valid()) {
        auto input = analyzer_.GetReduced(unary.input());
        og_index = __ Simd256Unary(V<Simd256>::Cast(input),
                                   GetSimd256UnaryKind(unary.kind));
        pnode->SetRevectorizedNode(og_index);
      }
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }
    return Next::ReduceInputGraphSimd128Unary(ig_index, unary);
  }

  OpIndex REDUCE_INPUT_GRAPH(Simd128Binop)(OpIndex ig_index,
                                           const Simd128BinopOp& op) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      OpIndex og_index = pnode->RevectorizedNode();
      // Skip revectorized node.
      if (!og_index.valid()) {
        auto left = analyzer_.GetReduced(op.left());
        auto right = analyzer_.GetReduced(op.right());
        og_index = __ Simd256Binop(left, right, GetSimd256BinOpKind(op.kind));
        pnode->SetRevectorizedNode(og_index);
      }
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }

    // no_change
    return Next::ReduceInputGraphSimd128Binop(ig_index, op);
  }

  OpIndex REDUCE_INPUT_GRAPH(Simd128Shift)(OpIndex ig_index,
                                           const Simd128ShiftOp& op) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      OpIndex og_index = pnode->RevectorizedNode();
      // Skip revectorized node.
      if (!og_index.valid()) {
        V<Simd256> input = analyzer_.GetReduced(op.input());
        DCHECK(input.valid());
        V<Word32> shift = __ MapToNewGraph(op.shift());
        og_index =
            __ Simd256Shift(input, shift, GetSimd256ShiftOpKind(op.kind));
        pnode->SetRevectorizedNode(og_index);
      }
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }

    // no_change
    return Next::ReduceInputGraphSimd128Shift(ig_index, op);
  }

  OpIndex REDUCE_INPUT_GRAPH(Simd128Ternary)(OpIndex ig_index,
                                             const Simd128TernaryOp& ternary) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      OpIndex og_index = pnode->RevectorizedNode();
      // Skip revectorized node.
      if (!og_index.valid()) {
        V<Simd256> first = analyzer_.GetReduced(ternary.first());
        V<Simd256> second = analyzer_.GetReduced(ternary.second());
        V<Simd256> third = analyzer_.GetReduced(ternary.third());

        og_index = __ Simd256Ternary(first, second, third,
                                     GetSimd256TernaryKind(ternary.kind));

        pnode->SetRevectorizedNode(og_index);
      }

      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }
    return Next::ReduceInputGraphSimd128Ternary(ig_index, ternary);
  }

 private:
  static Simd256UnaryOp::Kind GetSimd256UnaryKind(
      Simd128UnaryOp::Kind simd128_kind) {
    switch (simd128_kind) {
#define UNOP_KIND_MAPPING(from, to)   \
  case Simd128UnaryOp::Kind::k##from: \
    return Simd256UnaryOp::Kind::k##to;
      SIMD256_UNARY_OP(UNOP_KIND_MAPPING)
#undef UNOP_KIND_MAPPING
      default:
        UNIMPLEMENTED();
    }
  }

  static Simd256BinopOp::Kind GetSimd256BinOpKind(Simd128BinopOp::Kind kind) {
    switch (kind) {
#define BINOP_KIND_MAPPING(from, to)  \
  case Simd128BinopOp::Kind::k##from: \
    return Simd256BinopOp::Kind::k##to;
      SIMD256_BINOP_SIMPLE_OP(BINOP_KIND_MAPPING)
#undef BINOP_KIND_MAPPING
      default:
        UNIMPLEMENTED();
    }
  }

  static Simd256ShiftOp::Kind GetSimd256ShiftOpKind(Simd128ShiftOp::Kind kind) {
    switch (kind) {
#define SHIFT_KIND_MAPPING(from, to)  \
  case Simd128ShiftOp::Kind::k##from: \
    return Simd256ShiftOp::Kind::k##to;
      SIMD256_SHIFT_OP(SHIFT_KIND_MAPPING)
#undef SHIFT_KIND_MAPPING
      default:
        UNIMPLEMENTED();
    }
  }

  static Simd256TernaryOp::Kind GetSimd256TernaryKind(
      Simd128TernaryOp::Kind simd128_kind) {
    switch (simd128_kind) {
#define TERNARY_KIND_MAPPING(from, to)  \
  case Simd128TernaryOp::Kind::k##from: \
    return Simd256TernaryOp::Kind::k##to;
      SIMD256_TERNARY_OP(TERNARY_KIND_MAPPING)
#undef TERNARY_KIND_MAPPING
      default:
        UNIMPLEMENTED();
    }
  }

  static Simd256LoadTransformOp::TransformKind Get256LoadTransformKindFrom128(
      Simd128LoadTransformOp::TransformKind simd128_kind) {
    switch (simd128_kind) {
#define TRANSFORM_KIND_MAPPING(from, to)               \
  case Simd128LoadTransformOp::TransformKind::k##from: \
    return Simd256LoadTransformOp::TransformKind::k##to;
      SIMD256_LOADTRANSFORM_OP(TRANSFORM_KIND_MAPPING)
#undef TRANSFORM_KIND_MAPPING
      default:
        UNREACHABLE();
    }
  }

  const wasm::WasmModule* module_ = PipelineData::Get().wasm_module();
  WasmRevecAnalyzer analyzer_ = *PipelineData::Get().wasm_revec_analyzer();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_WASM_REVEC_REDUCER_H_

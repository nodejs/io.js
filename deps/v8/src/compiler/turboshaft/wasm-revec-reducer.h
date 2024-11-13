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

#define SIMD256_UNARY_SIMPLE_OP(V)                         \
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
  V(I32x4SConvertF32x4, I32x8SConvertF32x8)                \
  V(F32x4UConvertI32x4, F32x8UConvertI32x8)                \
  V(F32x4SConvertI32x4, F32x8SConvertI32x8)

#define SIMD256_UNARY_SIGN_EXTENSION_OP(V)                              \
  V(I64x2SConvertI32x4Low, I64x4SConvertI32x4, I64x2SConvertI32x4High)  \
  V(I64x2UConvertI32x4Low, I64x4UConvertI32x4, I64x2UConvertI32x4High)  \
  V(I32x4SConvertI16x8Low, I32x8SConvertI16x8, I32x4SConvertI16x8High)  \
  V(I32x4UConvertI16x8Low, I32x8UConvertI16x8, I32x4UConvertI16x8High)  \
  V(I16x8SConvertI8x16Low, I16x16SConvertI8x16, I16x8SConvertI8x16High) \
  V(I16x8UConvertI8x16Low, I16x16UConvertI8x16, I16x8UConvertI8x16High)

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
  V(F64x2Pmax, F64x4Pmax)                          \
  V(F32x4RelaxedMin, F32x8RelaxedMin)              \
  V(F32x4RelaxedMax, F32x8RelaxedMax)              \
  V(F64x2RelaxedMin, F64x4RelaxedMin)              \
  V(F64x2RelaxedMax, F64x4RelaxedMax)              \
  V(I16x8DotI8x16I7x16S, I16x16DotI8x32I7x32S)

#define SIMD256_BINOP_SIGN_EXTENSION_OP(V)                           \
  V(I16x8ExtMulLowI8x16S, I16x16ExtMulI8x16S, I16x8ExtMulHighI8x16S) \
  V(I16x8ExtMulLowI8x16U, I16x16ExtMulI8x16U, I16x8ExtMulHighI8x16U) \
  V(I32x4ExtMulLowI16x8S, I32x8ExtMulI16x8S, I32x4ExtMulHighI16x8S)  \
  V(I32x4ExtMulLowI16x8U, I32x8ExtMulI16x8U, I32x4ExtMulHighI16x8U)  \
  V(I64x2ExtMulLowI32x4S, I64x4ExtMulI32x4S, I64x2ExtMulHighI32x4S)  \
  V(I64x2ExtMulLowI32x4U, I64x4ExtMulI32x4U, I64x2ExtMulHighI32x4U)

#define SIMD256_SHIFT_OP(V) \
  V(I16x8Shl, I16x16Shl)    \
  V(I16x8ShrS, I16x16ShrS)  \
  V(I16x8ShrU, I16x16ShrU)  \
  V(I32x4Shl, I32x8Shl)     \
  V(I32x4ShrS, I32x8ShrS)   \
  V(I32x4ShrU, I32x8ShrU)   \
  V(I64x2Shl, I64x4Shl)     \
  V(I64x2ShrU, I64x4ShrU)

#define SIMD256_TERNARY_OP(V)                        \
  V(S128Select, S256Select)                          \
  V(F32x4Qfma, F32x8Qfma)                            \
  V(F32x4Qfms, F32x8Qfms)                            \
  V(F64x2Qfma, F64x4Qfma)                            \
  V(F64x2Qfms, F64x4Qfms)                            \
  V(I8x16RelaxedLaneSelect, I8x32RelaxedLaneSelect)  \
  V(I16x8RelaxedLaneSelect, I16x16RelaxedLaneSelect) \
  V(I32x4RelaxedLaneSelect, I32x8RelaxedLaneSelect)  \
  V(I64x2RelaxedLaneSelect, I64x4RelaxedLaneSelect)  \
  V(I32x4DotI8x16I7x16AddS, I32x8DotI8x32I7x32AddS)

#define SIMD256_SPLAT_OP(V) \
  V(I8x16, I8x32)           \
  V(I16x8, I16x16)          \
  V(I32x4, I32x8)           \
  V(I64x2, I64x4)           \
  V(F32x4, F32x8)           \
  V(F64x2, F64x4)

#define REDUCE_SEED_KIND(V) \
  V(I64x2Add)               \
  V(I32x4Add)               \
  V(I8x16Add)               \
  V(I16x8AddSatS)           \
  V(I16x8AddSatU)           \
  V(I8x16AddSatS)           \
  V(I8x16AddSatU)           \
  V(I16x8SConvertI32x4)     \
  V(I16x8UConvertI32x4)     \
  V(I8x16SConvertI16x8)     \
  V(I8x16UConvertI16x8)

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
  enum class ForcePackType {
    kNone,     // natural packing, not force.
    kSplat,    // force pack 2 identical nodes or 2 loads at the same address
    kGeneral,  // force pack 2 different nodes
  };
  explicit PackNode(Zone* zone, const NodeGroup& node_group)
      : nodes_(node_group),
        revectorized_node_(),
        force_pack_right_inputs_(zone) {}
  NodeGroup Nodes() const { return nodes_; }
  bool IsSame(const NodeGroup& node_group) const {
    return nodes_ == node_group;
  }
  bool IsSame(const PackNode& other) const { return nodes_ == other.nodes_; }
  V<Simd256> RevectorizedNode() const { return revectorized_node_; }
  void SetRevectorizedNode(V<Simd256> node) { revectorized_node_ = node; }

  bool is_force_pack() const {
    return force_pack_type_ != ForcePackType::kNone;
  }
  void set_force_pack_type(ForcePackType type) { force_pack_type_ = type; }
  ForcePackType force_pack_type() { return force_pack_type_; }

  void set_force_packed_pair(OpIndex left, OpIndex right) {
    force_packed_pair_ = {left, right};
  }

  OpIndex force_packed_left() const {
    DCHECK(force_packed_pair_.first.valid());
    return force_packed_pair_.first;
  }
  OpIndex force_packed_right() const {
    DCHECK(force_packed_pair_.second.valid());
    return force_packed_pair_.second;
  }

  ZoneSet<OpIndex>& force_pack_right_inputs() {
    return force_pack_right_inputs_;
  }

  void Print(Graph* graph) const;

 private:
  NodeGroup nodes_;
  V<Simd256> revectorized_node_;
  ForcePackType force_pack_type_ = ForcePackType::kNone;
  // og_index of the force packed nodes.
  std::pair<OpIndex, OpIndex> force_packed_pair_ = {OpIndex::Invalid(),
                                                    OpIndex::Invalid()};
  // When we emit the force packed node, before we emit the right node, we need
  // to make sure all it's input chains are emitted.
  ZoneSet<OpIndex> force_pack_right_inputs_;
};

class ShufflePackNode : public PackNode {
 public:
  class SpecificInfo {
   public:
    enum class Kind {
      kS256Load32Transform,
      kS256Load64Transform,
#ifdef V8_TARGET_ARCH_X64
      kShufd,
      kShufps,
      kS32x8UnpackLow,
      kS32x8UnpackHigh,
#endif  // V8_TARGET_ARCH_X64
    };
    union Param {
      int splat_index = 0;
#ifdef V8_TARGET_ARCH_X64
      uint8_t shufd_control;
      uint8_t shufps_control;
#endif  // V8_TARGET_ARCH_X64
    };

    Kind kind() { return kind_; }
    void set_kind(Kind kind) { kind_ = kind; }

    void set_splat_index(uint8_t value) {
      DCHECK(kind_ == Kind::kS256Load32Transform ||
             kind_ == Kind::kS256Load64Transform);
      param_.splat_index = value;
    }
    int splat_index() const {
      DCHECK(kind_ == Kind::kS256Load32Transform ||
             kind_ == Kind::kS256Load64Transform);
      return param_.splat_index;
    }

#ifdef V8_TARGET_ARCH_X64
    void set_shufd_control(uint8_t control) {
      DCHECK_EQ(kind_, Kind::kShufd);
      param_.shufd_control = control;
    }
    uint8_t shufd_control() const {
      DCHECK_EQ(kind_, Kind::kShufd);
      return param_.shufd_control;
    }

    void set_shufps_control(uint8_t control) {
      DCHECK_EQ(kind_, Kind::kShufps);
      param_.shufps_control = control;
    }
    uint8_t shufps_control() const {
      DCHECK_EQ(kind_, Kind::kShufps);
      return param_.shufps_control;
    }
#endif  // V8_TARGET_ARCH_X64

   private:
    Kind kind_;
    Param param_;
  };

  ShufflePackNode(Zone* zone, const NodeGroup& node_group,
                  SpecificInfo::Kind kind)
      : PackNode(zone, node_group) {
    info_.set_kind(kind);
  }

  SpecificInfo& info() { return info_; }

 private:
  SpecificInfo info_;
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

  PackNode* NewForcePackNode(const NodeGroup& node_group,
                             PackNode::ForcePackType type, const Graph& graph);

  // Recursion: create a new PackNode and call BuildTreeRec recursively
  PackNode* NewPackNodeAndRecurs(const NodeGroup& node_group, int start_index,
                                 int count, unsigned depth);

  PackNode* NewCommutativePackNodeAndRecurs(const NodeGroup& node_group,
                                            unsigned depth);

  ShufflePackNode* NewShufflePackNode(const NodeGroup& node_group,
                                      ShufflePackNode::SpecificInfo::Kind kind);

#ifdef V8_TARGET_ARCH_X64
  // The Simd Shuffle in wasm is a high level representation, and it can map to
  // different x64 intructions base on its shuffle array. And the performance of
  // different intructions are varies greatly.
  // For example, if the shuffle array are totally random, there is a high
  // probability to use a general shuffle. Under x64, the general shuffle may
  // consists of a series mov, a vpinsrq and a vpshufb. It's performance cost is
  // high. However, if the shuffle array is in an particular pattern, for
  // example: [0,  1,  2,  3,  32, 33, 34, 35, 4,  5,  6,  7,  36, 37, 38, 39,
  //      16, 17, 18, 19, 48, 49, 50, 51, 20, 21, 22, 23, 52, 53, 54, 55]
  // we can use a single vpunpckldq instruction. It's performance cost is much
  // more lower than a general one.
  //
  // This function is used to try to match the shuffle array to the
  // x64 instructions which has the best performance.
  ShufflePackNode* X64TryMatch256Shuffle(const NodeGroup& node_group,
                                         const uint8_t* shuffle0,
                                         const uint8_t* shuffle1);
#endif  // V8_TARGET_ARCH_X64

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
  WasmRevecAnalyzer(PipelineData* data, Zone* zone, Graph& graph)
      : data_(data),
        graph_(graph),
        phase_zone_(zone),
        store_seeds_(zone),
        reduce_seeds_(zone),
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
  bool IsSupportedReduceSeed(const Operation& op);
  void ProcessBlock(const Block& block);
  bool DecideVectorize();

  PipelineData* data_;
  Graph& graph_;
  Zone* phase_zone_;
  ZoneVector<std::pair<OpIndex, OpIndex>> store_seeds_;
  ZoneVector<std::pair<OpIndex, OpIndex>> reduce_seeds_;
  const wasm::WasmModule* module_ = data_->wasm_module();
  const wasm::FunctionSig* signature_ = data_->wasm_sig();
  SLPTree* slp_tree_;
  ZoneUnorderedMap<OpIndex, PackNode*> revectorizable_node_;
  bool should_reduce_;
  SimdUseMap* use_map_;
};

template <class Next>
class WasmRevecReducer : public UniformReducerAdapter<WasmRevecReducer, Next> {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(WasmRevec)
  using Adapter = UniformReducerAdapter<WasmRevecReducer, Next>;

  OpIndex GetExtractOpIfNeeded(PackNode* pnode, OpIndex ig_index,
                               OpIndex og_index) {
    uint8_t lane = 0;
    for (; lane < static_cast<uint8_t>(pnode->Nodes().size()); lane++) {
      if (pnode->Nodes()[lane] == ig_index) break;
    }

    for (auto use : analyzer_.uses(ig_index)) {
      if (!analyzer_.GetPackNode(use)) {
        if (pnode->is_force_pack()) {
          return lane == 0 ? pnode->force_packed_left()
                           : pnode->force_packed_right();
        } else {
          return __ Simd256Extract128Lane(og_index, lane);
        }
      }
    }

    return OpIndex::Invalid();
  }

  V<Simd128> REDUCE_INPUT_GRAPH(Simd128Constant)(
      V<Simd128> ig_index, const Simd128ConstantOp& constant_op) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      V<Simd256> og_index = pnode->RevectorizedNode();
      // Skip revectorized node.
      if (!og_index.valid()) {
        NodeGroup inputs = pnode->Nodes();
        const Simd128ConstantOp& op0 =
            __ input_graph().Get(inputs[0]).template Cast<Simd128ConstantOp>();
        const Simd128ConstantOp& op1 =
            __ input_graph().Get(inputs[1]).template Cast<Simd128ConstantOp>();
        uint8_t value[kSimd256Size] = {};
        memcpy(value, op0.value, kSimd128Size);
        memcpy(value + kSimd128Size, op1.value, kSimd128Size);

        og_index = __ Simd256Constant(value);

        pnode->SetRevectorizedNode(og_index);
      }
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }
    return Adapter::ReduceInputGraphSimd128Constant(ig_index, constant_op);
  }

  V<Simd128> REDUCE_INPUT_GRAPH(Simd128LoadTransform)(
      V<Simd128> ig_index, const Simd128LoadTransformOp& load_transform) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      if (pnode->is_force_pack()) {
        return Adapter::ReduceInputGraphSimd128LoadTransform(ig_index,
                                                             load_transform);
      }
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

    return Adapter::ReduceInputGraphSimd128LoadTransform(ig_index,
                                                         load_transform);
  }

  OpIndex REDUCE_INPUT_GRAPH(Load)(OpIndex ig_index, const LoadOp& load) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      if (pnode->is_force_pack()) {
        return Adapter::ReduceInputGraphLoad(ig_index, load);
      }
      OpIndex og_index = pnode->RevectorizedNode();

      // Emit revectorized op.
      if (!og_index.valid()) {
        const LoadOp* start = analyzer_.GetStartOperation(pnode, ig_index, load)
                                  .TryCast<LoadOp>();
        DCHECK_EQ(start->base(), load.base());

        auto base = __ MapToNewGraph(load.base());
        // We need to use load's index here due to there would be different
        // ChangeOps from the same index. If start is not load, it's possible
        // that the ChangeOp of start index is not visited yet.
        auto index = __ MapToNewGraph(load.index());
        og_index = __ Load(base, index, load.kind,
                           MemoryRepresentation::Simd256(), start->offset);
        pnode->SetRevectorizedNode(og_index);
      }

      // Emit extract op if needed.
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }

    // no_change
    return Adapter::ReduceInputGraphLoad(ig_index, load);
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

        auto base = __ MapToNewGraph(store.base());
        // We need to use store's index here due to there would be different
        // ChangeOps from the same index. If start is not store, it's possible
        // that the ChangeOp of start index is not visited yet.
        auto index = __ MapToNewGraph(store.index());
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
    return Adapter::ReduceInputGraphStore(ig_index, store);
  }

  OpIndex REDUCE_INPUT_GRAPH(Phi)(OpIndex ig_index, const PhiOp& phi) {
    if (phi.rep == RegisterRepresentation::Simd128()) {
      if (auto pnode = analyzer_.GetPackNode(ig_index)) {
        OpIndex og_index = pnode->RevectorizedNode();

        // Don't reduce revectorized node.
        if (!og_index.valid()) {
          base::SmallVector<OpIndex, 16> elements;
          og_index = __ ResolvePhi(
              phi,
              [&](OpIndex ind, int block_id) {
                return analyzer_.GetReduced(ind);
              },
              RegisterRepresentation::Simd256());
          pnode->SetRevectorizedNode(og_index);
        }

        OpIndex extract_op_index =
            GetExtractOpIfNeeded(pnode, ig_index, og_index);
        // If phis are not be mapped to anything in the new graph,
        // they will be skipped in FixLoopPhis in copying-phase.
        // return og_index to create the mapping.
        if (extract_op_index == OpIndex::Invalid()) {
          return og_index;
        } else {
          return extract_op_index;
        }
      }
    }

    // no_change
    return Adapter::ReduceInputGraphPhi(ig_index, phi);
  }

  void FixLoopPhi(const PhiOp& input_phi, OpIndex output_index,
                  Block* output_graph_loop) {
    if (input_phi.rep == RegisterRepresentation::Simd128()) {
      OpIndex phi_index = __ input_graph().Index(input_phi);
      DCHECK(phi_index.valid());
      if (auto* pnode = analyzer_.GetPackNode(phi_index)) {
        auto pending_index = pnode->RevectorizedNode();
        DCHECK(pending_index.valid());
        if (pending_index.valid() &&
            output_graph_loop->Contains(pending_index)) {
          // Need skip replaced op
          if (auto* pending_phi = __ output_graph()
                                      .Get(pending_index)
                                      .template TryCast<PendingLoopPhiOp>()) {
            __ output_graph().template Replace<PhiOp>(
                pending_index,
                base::VectorOf({pending_phi -> first(),
                                analyzer_.GetReduced(input_phi.input(1))}),
                RegisterRepresentation::Simd256());
            return;
          }
        }
      }
    }

    return Adapter::FixLoopPhi(input_phi, output_index, output_graph_loop);
  }

  V<Simd128> REDUCE_INPUT_GRAPH(Simd128Unary)(V<Simd128> ig_index,
                                              const Simd128UnaryOp& unary) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      if (pnode->is_force_pack()) {
        return Adapter::ReduceInputGraphSimd128Unary(ig_index, unary);
      }
      V<Simd256> og_index = pnode->RevectorizedNode();
      // Skip revectorized node.
      if (!og_index.valid()) {
        V<Simd256> input = analyzer_.GetReduced(unary.input());
        if (!input.valid()) {
          V<Simd128> input = __ MapToNewGraph(unary.input());
          og_index = __ Simd256Unary(input, GetSimd256UnaryKind(unary.kind));
        } else {
          og_index = __ Simd256Unary(input, GetSimd256UnaryKind(unary.kind));
        }
        pnode->SetRevectorizedNode(og_index);
      }
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }
    return Adapter::ReduceInputGraphSimd128Unary(ig_index, unary);
  }

  V<Simd128> REDUCE_INPUT_GRAPH(Simd128Binop)(V<Simd128> ig_index,
                                              const Simd128BinopOp& op) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      if (pnode->is_force_pack()) {
        return Adapter::ReduceInputGraphSimd128Binop(ig_index, op);
      }
      V<Simd256> og_index = pnode->RevectorizedNode();
      // Skip revectorized node.
      if (!og_index.valid()) {
        V<Simd256> left = analyzer_.GetReduced(op.left());
        V<Simd256> right = analyzer_.GetReduced(op.right());
        if (!left.valid() || !right.valid()) {
          V<Simd128> left = __ MapToNewGraph(op.left());
          V<Simd128> right = __ MapToNewGraph(op.right());
          og_index = __ Simd256Binop(left, right, GetSimd256BinOpKind(op.kind));
        } else {
          og_index = __ Simd256Binop(left, right, GetSimd256BinOpKind(op.kind));
        }
        pnode->SetRevectorizedNode(og_index);
      }
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }

    // no_change
    return Adapter::ReduceInputGraphSimd128Binop(ig_index, op);
  }

  V<Simd128> REDUCE_INPUT_GRAPH(Simd128Shift)(V<Simd128> ig_index,
                                              const Simd128ShiftOp& op) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      V<Simd256> og_index = pnode->RevectorizedNode();
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
    return Adapter::ReduceInputGraphSimd128Shift(ig_index, op);
  }

  V<Simd128> REDUCE_INPUT_GRAPH(Simd128Ternary)(
      V<Simd128> ig_index, const Simd128TernaryOp& ternary) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      V<Simd256> og_index = pnode->RevectorizedNode();
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
    return Adapter::ReduceInputGraphSimd128Ternary(ig_index, ternary);
  }

  V<Simd128> REDUCE_INPUT_GRAPH(Simd128Splat)(V<Simd128> ig_index,
                                              const Simd128SplatOp& op) {
    if (auto pnode = analyzer_.GetPackNode(ig_index)) {
      V<Simd256> og_index = pnode->RevectorizedNode();
      // Skip revectorized node.
      if (!og_index.valid()) {
        og_index = __ Simd256Splat(__ MapToNewGraph(op.input()),
                                   Get256SplatOpKindFrom128(op.kind));

        pnode->SetRevectorizedNode(og_index);
      }
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }

    return Adapter::ReduceInputGraphSimd128Splat(ig_index, op);
  }

  V<Simd128> REDUCE_INPUT_GRAPH(Simd128Shuffle)(V<Simd128> ig_index,
                                                const Simd128ShuffleOp& op) {
    if (ShufflePackNode* pnode =
            static_cast<ShufflePackNode*>(analyzer_.GetPackNode(ig_index))) {
      V<Simd256> og_index = pnode->RevectorizedNode();
      // Skip revectorized node.
      if (!og_index.valid()) {
        const ShufflePackNode::SpecificInfo::Kind kind = pnode->info().kind();
        switch (kind) {
          case ShufflePackNode::SpecificInfo::Kind::kS256Load32Transform:
          case ShufflePackNode::SpecificInfo::Kind::kS256Load64Transform: {
            const bool is_32 =
                kind ==
                ShufflePackNode::SpecificInfo::Kind::kS256Load32Transform;

            const OpIndex load_index =
                op.input(pnode->info().splat_index() >> (is_32 ? 2 : 1));
            const LoadOp& load =
                __ input_graph().Get(load_index).template Cast<LoadOp>();

            const int bytes_per_lane = is_32 ? 4 : 8;
            const int splat_index =
                pnode->info().splat_index() * bytes_per_lane;
            const int offset = splat_index + load.offset;

            V<WordPtr> base = __ WordPtrAdd(__ MapToNewGraph(load.base()),
                                            __ IntPtrConstant(offset));

            V<WordPtr> index = load.index().has_value()
                                   ? __ MapToNewGraph(load.index().value())
                                   : __ IntPtrConstant(0);

            const Simd256LoadTransformOp::TransformKind transform_kind =
                is_32 ? Simd256LoadTransformOp::TransformKind::k32Splat
                      : Simd256LoadTransformOp::TransformKind::k64Splat;
            og_index = __ Simd256LoadTransform(base, index, load.kind,
                                               transform_kind, 0);
            pnode->SetRevectorizedNode(og_index);
            break;
          }
#ifdef V8_TARGET_ARCH_X64
          case ShufflePackNode::SpecificInfo::Kind::kShufd: {
            V<Simd256> og_left = analyzer_.GetReduced(op.left());
            DCHECK_EQ(og_left, analyzer_.GetReduced(op.right()));
            og_index = __ Simd256Shufd(og_left, pnode->info().shufd_control());
            pnode->SetRevectorizedNode(og_index);
            break;
          }
          case ShufflePackNode::SpecificInfo::Kind::kShufps: {
            V<Simd256> og_left = analyzer_.GetReduced(op.left());
            V<Simd256> og_right = analyzer_.GetReduced(op.right());
            og_index = __ Simd256Shufps(og_left, og_right,
                                        pnode->info().shufps_control());
            pnode->SetRevectorizedNode(og_index);
            break;
          }
          case ShufflePackNode::SpecificInfo::Kind::kS32x8UnpackLow: {
            V<Simd256> og_left = analyzer_.GetReduced(op.left());
            V<Simd256> og_right = analyzer_.GetReduced(op.right());
            og_index = __ Simd256Unpack(og_left, og_right,
                                        Simd256UnpackOp::Kind::k32x8Low);
            pnode->SetRevectorizedNode(og_index);
            break;
          }
          case ShufflePackNode::SpecificInfo::Kind::kS32x8UnpackHigh: {
            V<Simd256> og_left = analyzer_.GetReduced(op.left());
            V<Simd256> og_right = analyzer_.GetReduced(op.right());
            og_index = __ Simd256Unpack(og_left, og_right,
                                        Simd256UnpackOp::Kind::k32x8High);
            pnode->SetRevectorizedNode(og_index);
            break;
          }
#endif  // V8_TARGET_ARCH_X64
          default:
            UNREACHABLE();
        }
      }
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }

    return Adapter::ReduceInputGraphSimd128Shuffle(ig_index, op);
  }

  template <typename Op, typename Continuation>
  OpIndex ReduceInputGraphOperation(OpIndex ig_index, const Op& op) {
    if (PackNode* pnode = analyzer_.GetPackNode(ig_index);
        pnode && pnode->is_force_pack()) {
      // Handle force packing nodes.
      OpIndex og_index = pnode->RevectorizedNode();
      if (!og_index.valid()) {
        switch (pnode->force_pack_type()) {
          case PackNode::ForcePackType::kSplat: {
            OpIndex og_left = Continuation{this}.ReduceInputGraph(ig_index, op);
            og_index = __ SimdPack128To256(og_left, og_left);
            pnode->set_force_packed_pair(og_left, og_left);
            pnode->SetRevectorizedNode(og_index);
            break;
          }
          case PackNode::ForcePackType::kGeneral: {
            OpIndex og_left = Continuation{this}.ReduceInputGraph(ig_index, op);
            // Emit right node's input tree.

            const Block* current_input_block = Asm().current_input_block();
            for (OpIndex idx : pnode->force_pack_right_inputs()) {
              DCHECK(!Asm().input_graph().Get(idx).template Is<PhiOp>());
              Asm().template VisitOpAndUpdateMapping<false>(
                  idx, current_input_block);
            }

            OpIndex right_ig_index = pnode->Nodes()[1];
            const Op& right_ig_op =
                Asm().input_graph().Get(right_ig_index).template Cast<Op>();
            OpIndex og_right = Continuation{this}.ReduceInputGraph(
                right_ig_index, right_ig_op);
            og_index = __ SimdPack128To256(og_left, og_right);
            pnode->set_force_packed_pair(og_left, og_right);
            pnode->SetRevectorizedNode(og_index);
            break;
          }
          default:
            UNIMPLEMENTED();
        }
      }
      return GetExtractOpIfNeeded(pnode, ig_index, og_index);
    }

    if (__ op_mapping_[ig_index].valid()) {
      // The op is already emitted during emitting force pack right node input
      // trees.
      return OpIndex::Invalid();
    }

    return Continuation{this}.ReduceInputGraph(ig_index, op);
  }

 private:
  static Simd256UnaryOp::Kind GetSimd256UnaryKind(
      Simd128UnaryOp::Kind simd128_kind) {
    switch (simd128_kind) {
#define UNOP_KIND_MAPPING(from, to)   \
  case Simd128UnaryOp::Kind::k##from: \
    return Simd256UnaryOp::Kind::k##to;
      SIMD256_UNARY_SIMPLE_OP(UNOP_KIND_MAPPING)
#undef UNOP_KIND_MAPPING

#define SIGN_EXTENSION_UNOP_KIND_MAPPING(from_1, to, from_2) \
  case Simd128UnaryOp::Kind::k##from_1:                      \
    return Simd256UnaryOp::Kind::k##to;                      \
  case Simd128UnaryOp::Kind::k##from_2:                      \
    return Simd256UnaryOp::Kind::k##to;
      SIMD256_UNARY_SIGN_EXTENSION_OP(SIGN_EXTENSION_UNOP_KIND_MAPPING)
#undef SIGN_EXTENSION_UNOP_KIND_MAPPING
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

#define SIGN_EXTENSION_BINOP_KIND_MAPPING(from_1, to, from_2) \
  case Simd128BinopOp::Kind::k##from_1:                       \
    return Simd256BinopOp::Kind::k##to;                       \
  case Simd128BinopOp::Kind::k##from_2:                       \
    return Simd256BinopOp::Kind::k##to;
      SIMD256_BINOP_SIGN_EXTENSION_OP(SIGN_EXTENSION_BINOP_KIND_MAPPING)
#undef SIGN_EXTENSION_UNOP_KIND_MAPPING
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

  static Simd256SplatOp::Kind Get256SplatOpKindFrom128(
      Simd128SplatOp::Kind kind) {
    switch (kind) {
#define SPLAT_KIND_MAPPING(from, to)  \
  case Simd128SplatOp::Kind::k##from: \
    return Simd256SplatOp::Kind::k##to;
      SIMD256_SPLAT_OP(SPLAT_KIND_MAPPING)
      default:
        UNREACHABLE();
    }
  }

  const wasm::WasmModule* module_ = __ data() -> wasm_module();
  WasmRevecAnalyzer analyzer_ = *__ data() -> wasm_revec_analyzer();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_WASM_REVEC_REDUCER_H_

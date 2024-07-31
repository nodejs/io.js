// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_WASM_INLINING_H_
#define V8_COMPILER_WASM_INLINING_H_

#include "src/compiler/graph-reducer.h"
#include "src/compiler/machine-graph.h"

namespace v8 {
namespace internal {

class SourcePosition;
struct WasmInliningPosition;

namespace wasm {
struct CompilationEnv;
struct DanglingExceptions;
class WasmFeatures;
struct WasmModule;
}  // namespace wasm

namespace compiler {

struct WasmCompilationData;

// The WasmInliner provides the core graph inlining machinery for Webassembly
// graphs.
class WasmInliner final : public AdvancedReducer {
 public:
  WasmInliner(Editor* editor, wasm::CompilationEnv* env,
              WasmCompilationData& data, MachineGraph* mcgraph,
              const char* debug_name,
              ZoneVector<WasmInliningPosition>* inlining_positions,
              wasm::WasmFeatures* detected)
      : AdvancedReducer(editor),
        env_(env),
        data_(data),
        mcgraph_(mcgraph),
        debug_name_(debug_name),
        initial_graph_size_(mcgraph->graph()->NodeCount()),
        current_graph_size_(initial_graph_size_),
        inlining_candidates_(),
        inlining_positions_(inlining_positions),
        detected_(detected) {}

  const char* reducer_name() const override { return "WasmInliner"; }

  // Registers (tail) calls to possibly be inlined, prioritized by inlining
  // heuristics provided by {LexicographicOrdering}.
  // Only locally defined functions are inlinable, and a limited number of
  // inlinings of a specific function is allowed.
  Reduction Reduce(Node* node) final;
  // Inlines calls registered by {Reduce}, until an inlining budget is exceeded.
  void Finalize() final;

  static bool graph_size_allows_inlining(const wasm::WasmModule* module,
                                         size_t graph_size,
                                         size_t initial_graph_size);

 private:
  struct CandidateInfo {
    Node* node;
    uint32_t inlinee_index;
    int call_count;
    int wire_byte_size;

    int64_t score() const {
      // Note that the zero-point is arbitrary. Functions with negative score
      // can still get inlined.

      // Note(mliedtke): Adding information about "this call has constant
      // arguments" didn't seem to provide measurable gains at the current
      // state, still this would be an interesting measure to retry at a later
      // point potentially together with other metrics.
      const int count_factor = 2;
      const int size_factor = 3;
      return int64_t{call_count} * count_factor -
             int64_t{wire_byte_size} * size_factor;
    }
  };

  struct LexicographicOrdering {
    // Returns if c1 should be prioritized less than c2.
    bool operator()(CandidateInfo& c1, CandidateInfo& c2) {
      return c1.score() < c2.score();
    }
  };

  Zone* zone() const { return mcgraph_->zone(); }
  CommonOperatorBuilder* common() const { return mcgraph_->common(); }
  Graph* graph() const { return mcgraph_->graph(); }
  MachineGraph* mcgraph() const { return mcgraph_; }
  const wasm::WasmModule* module() const;

  Reduction ReduceCall(Node* call);
  void InlineCall(Node* call, Node* callee_start, Node* callee_end,
                  const wasm::FunctionSig* inlinee_sig,
                  SourcePosition parent_pos,
                  wasm::DanglingExceptions* dangling_exceptions);
  void InlineTailCall(Node* call, Node* callee_start, Node* callee_end);
  void RewireFunctionEntry(Node* call, Node* callee_start);

  int GetCallCount(Node* call);

  void Trace(Node* call, int inlinee, const char* decision);
  void Trace(const CandidateInfo& candidate, const char* decision);

  wasm::CompilationEnv* const env_;
  WasmCompilationData& data_;
  MachineGraph* const mcgraph_;
  const char* debug_name_;
  const size_t initial_graph_size_;
  size_t current_graph_size_;
  std::priority_queue<CandidateInfo, std::vector<CandidateInfo>,
                      LexicographicOrdering>
      inlining_candidates_;
  std::unordered_set<Node*> seen_;
  std::unordered_map<uint32_t, int> function_inlining_count_;
  ZoneVector<WasmInliningPosition>* inlining_positions_;
  wasm::WasmFeatures* detected_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_WASM_INLINING_H_

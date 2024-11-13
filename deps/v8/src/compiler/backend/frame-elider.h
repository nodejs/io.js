// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_FRAME_ELIDER_H_
#define V8_COMPILER_BACKEND_FRAME_ELIDER_H_

#include "src/compiler/backend/instruction.h"

namespace v8 {
namespace internal {
namespace compiler {

// Determine which instruction blocks need a frame and where frames must be
// constructed/deconstructed.
class FrameElider {
 public:
  explicit FrameElider(InstructionSequence* code, bool has_dummy_end_block,
                       bool is_wasm_to_js);
  void Run();

 private:
  void MarkBlocks();
  void PropagateMarks();
  void MarkDeConstruction();
  bool PropagateInOrder();
  bool PropagateReversed();
  bool PropagateIntoBlock(InstructionBlock* block);
  const InstructionBlocks& instruction_blocks() const;
  InstructionBlock* InstructionBlockAt(RpoNumber rpo_number) const;
  Instruction* InstructionAt(int index) const;

  InstructionSequence* const code_;
  const bool has_dummy_end_block_;
  const bool is_wasm_to_js_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_FRAME_ELIDER_H_

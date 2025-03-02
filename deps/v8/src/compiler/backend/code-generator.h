// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_CODE_GENERATOR_H_
#define V8_COMPILER_BACKEND_CODE_GENERATOR_H_

#include <memory>
#include <optional>

#include "src/codegen/macro-assembler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/codegen/safepoint-table.h"
#include "src/codegen/source-position-table.h"
#include "src/compiler/backend/gap-resolver.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/backend/unwinding-info-writer.h"
#include "src/compiler/osr.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/objects/code-kind.h"
#include "src/objects/deoptimization-data.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/trap-handler/trap-handler.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8::internal::compiler {

// Forward declarations.
class DeoptimizationExit;
class FrameAccessState;
class Linkage;
class OutOfLineCode;

struct BranchInfo {
  FlagsCondition condition;
  Label* true_label;
  Label* false_label;
  bool fallthru;
};

class InstructionOperandIterator {
 public:
  InstructionOperandIterator(Instruction* instr, size_t pos)
      : instr_(instr), pos_(pos) {}

  Instruction* instruction() const { return instr_; }
  InstructionOperand* Advance() { return instr_->InputAt(pos_++); }

 private:
  Instruction* instr_;
  size_t pos_;
};

// These structs hold pc offsets for generated instructions and is only used
// when tracing for turbolizer is enabled.
struct TurbolizerCodeOffsetsInfo {
  int code_start_register_check = -1;
  int deopt_check = -1;
  int blocks_start = -1;
  int out_of_line_code = -1;
  int deoptimization_exits = -1;
  int pools = -1;
  int jump_tables = -1;
};

struct TurbolizerInstructionStartInfo {
  int gap_pc_offset = -1;
  int arch_instr_pc_offset = -1;
  int condition_pc_offset = -1;
};

// Generates native code for a sequence of instructions.
class V8_EXPORT_PRIVATE CodeGenerator final : public GapResolver::Assembler {
 public:
  explicit CodeGenerator(Zone* codegen_zone, Frame* frame, Linkage* linkage,
                         InstructionSequence* instructions,
                         OptimizedCompilationInfo* info, Isolate* isolate,
                         std::optional<OsrHelper> osr_helper,
                         int start_source_position,
                         JumpOptimizationInfo* jump_opt,
                         const AssemblerOptions& options, Builtin builtin,
                         size_t max_unoptimized_frame_height,
                         size_t max_pushed_argument_count,
                         const char* debug_name = nullptr);

  // Generate native code. After calling AssembleCode, call FinalizeCode to
  // produce the actual code object. If an error occurs during either phase,
  // FinalizeCode returns an empty MaybeHandle.
  void AssembleCode();  // Does not need to run on main thread.
  MaybeHandle<Code> FinalizeCode();

#if V8_ENABLE_WEBASSEMBLY
  base::OwnedVector<uint8_t> GenerateWasmDeoptimizationData();
#endif

  base::OwnedVector<uint8_t> GetSourcePositionTable();
  base::OwnedVector<uint8_t> GetProtectedInstructionsData();

  InstructionSequence* instructions() const { return instructions_; }
  FrameAccessState* frame_access_state() const { return frame_access_state_; }
  const Frame* frame() const { return frame_access_state_->frame(); }
  Isolate* isolate() const { return isolate_; }
  Linkage* linkage() const { return linkage_; }

  Label* GetLabel(RpoNumber rpo) { return &labels_[rpo.ToSize()]; }

  void RecordProtectedInstruction(uint32_t instr_offset);

  SourcePosition start_source_position() const {
    return start_source_position_;
  }

  void AssembleSourcePosition(Instruction* instr);
  void AssembleSourcePosition(SourcePosition source_position);

  // Record a safepoint with the given pointer map. When pc_offset is 0, then
  // the current pc is used to define the safepoint. Otherwise the provided
  // pc_offset is used.
  void RecordSafepoint(ReferenceMap* references, int pc_offset = 0);

  Zone* zone() const { return zone_; }
  MacroAssembler* masm() { return &masm_; }
  SafepointTableBuilder* safepoint_table_builder() { return &safepoints_; }
  size_t handler_table_offset() const { return handler_table_offset_; }

  const ZoneVector<int>& block_starts() const { return block_starts_; }
  const ZoneVector<TurbolizerInstructionStartInfo>& instr_starts() const {
    return instr_starts_;
  }

  const TurbolizerCodeOffsetsInfo& offsets_info() const {
    return offsets_info_;
  }

#if V8_ENABLE_WEBASSEMBLY
  bool IsWasm() const { return info()->IsWasm(); }
#endif

  static constexpr int kBinarySearchSwitchMinimalCases = 4;

  // Returns true if an offset should be applied to the given stack check. There
  // are two reasons that this could happen:
  // 1. The optimized frame is smaller than the corresponding deoptimized frames
  //    and an offset must be applied in order to be able to deopt safely.
  // 2. The current function pushes a large number of arguments to the stack.
  //    These are not accounted for by the initial frame setup.
  bool ShouldApplyOffsetToStackCheck(Instruction* instr, uint32_t* offset);
  uint32_t GetStackCheckOffset();

  CodeKind code_kind() const { return info_->code_kind(); }

 private:
  GapResolver* resolver() { return &resolver_; }
  SafepointTableBuilder* safepoints() { return &safepoints_; }
  OptimizedCompilationInfo* info() const { return info_; }
  OsrHelper* osr_helper() { return &(*osr_helper_); }

  // Create the FrameAccessState object. The Frame is immutable from here on.
  void CreateFrameAccessState(Frame* frame);

  // Architecture - specific frame finalization.
  void FinishFrame(Frame* frame);

  // Checks if {block} will appear directly after {current_block_} when
  // assembling code, in which case, a fall-through can be used.
  bool IsNextInAssemblyOrder(RpoNumber block) const;

  // Check if a heap object can be materialized by loading from a heap root,
  // which is cheaper on some platforms than materializing the actual heap
  // object constant.
  bool IsMaterializableFromRoot(Handle<HeapObject> object,
                                RootIndex* index_return);

  enum CodeGenResult { kSuccess, kTooManyDeoptimizationBailouts };

  // Assemble instructions for the specified block.
  CodeGenResult AssembleBlock(const InstructionBlock* block);

  // Assemble code for the specified instruction.
  CodeGenResult AssembleInstruction(int instruction_index,
                                    const InstructionBlock* block);
  void AssembleGaps(Instruction* instr);

  // Compute branch info from given instruction. Returns a valid rpo number
  // if the branch is redundant, the returned rpo number point to the target
  // basic block.
  RpoNumber ComputeBranchInfo(BranchInfo* branch, FlagsCondition condition,
                              Instruction* instr);

  // Returns true if a instruction is a tail call that needs to adjust the stack
  // pointer before execution. The stack slot index to the empty slot above the
  // adjusted stack pointer is returned in |slot|.
  bool GetSlotAboveSPBeforeTailCall(Instruction* instr, int* slot);

  // Determines how to call helper stubs depending on the code kind.
  StubCallMode DetermineStubCallMode() const;

  CodeGenResult AssembleDeoptimizerCall(DeoptimizationExit* exit);

  DeoptimizationExit* BuildTranslation(Instruction* instr, int pc_offset,
                                       size_t frame_state_offset,
                                       size_t immediate_args_count,
                                       OutputFrameStateCombine state_combine);

  // ===========================================================================
  // ============= Architecture-specific code generation methods. ==============
  // ===========================================================================

  CodeGenResult AssembleArchInstruction(Instruction* instr);
  void AssembleArchJump(RpoNumber target);
  void AssembleArchJumpRegardlessOfAssemblyOrder(RpoNumber target);
  void AssembleArchBranch(Instruction* instr, BranchInfo* branch);
  void AssembleArchConditionalBranch(Instruction* instr, BranchInfo* branch);

  // Generates special branch for deoptimization condition.
  void AssembleArchDeoptBranch(Instruction* instr, BranchInfo* branch);

  void AssembleArchBoolean(Instruction* instr, FlagsCondition condition);
  void AssembleArchConditionalBoolean(Instruction* instr);
  void AssembleArchSelect(Instruction* instr, FlagsCondition condition);
#if V8_ENABLE_WEBASSEMBLY
  void AssembleArchTrap(Instruction* instr, FlagsCondition condition);
#endif  // V8_ENABLE_WEBASSEMBLY
#if V8_TARGET_ARCH_X64
  void AssembleArchBinarySearchSwitchRange(
      Register input, RpoNumber def_block, std::pair<int32_t, Label*>* begin,
      std::pair<int32_t, Label*>* end, std::optional<int32_t>& last_cmp_value);
#else
  void AssembleArchBinarySearchSwitchRange(Register input, RpoNumber def_block,
                                           std::pair<int32_t, Label*>* begin,
                                           std::pair<int32_t, Label*>* end);
#endif  // V8_TARGET_ARCH_X64
  void AssembleArchBinarySearchSwitch(Instruction* instr);
  void AssembleArchTableSwitch(Instruction* instr);

  // Generates code to check whether the {kJavaScriptCallCodeStartRegister}
  // contains the expected pointer to the start of the instruction stream.
  void AssembleCodeStartRegisterCheck();

#ifdef V8_ENABLE_LEAPTIERING
  // Generates code to check whether the {kJavaScriptCallDispatchHandleRegister}
  // references a valid entry compatible with this code.
  void AssembleDispatchHandleRegisterCheck();
#endif  // V8_ENABLE_LEAPTIERING

  // When entering a code that is marked for deoptimization, rather continuing
  // with its execution, we jump to a lazy compiled code. We need to do this
  // because this code has already been deoptimized and needs to be unlinked
  // from the JS functions referring it.
  // TODO(olivf, 42204201) Rename this to AssertNotDeoptimized once
  // non-leaptiering is removed from the codebase.
  void BailoutIfDeoptimized();

  // Assemble NOP instruction for lazy deoptimization. This place will be
  // patched later as a jump instruction to deoptimization trampoline.
  void AssemblePlaceHolderForLazyDeopt(Instruction* instr);

  // Generates an architecture-specific, descriptor-specific prologue
  // to set up a stack frame.
  void AssembleConstructFrame();

  // Generates an architecture-specific, descriptor-specific return sequence
  // to tear down a stack frame.
  void AssembleReturn(InstructionOperand* pop);

  void AssembleDeconstructFrame();

  // Generates code to manipulate the stack in preparation for a tail call.
  void AssemblePrepareTailCall();

  enum PushTypeFlag {
    kImmediatePush = 0x1,
    kRegisterPush = 0x2,
    kStackSlotPush = 0x4,
    kScalarPush = kRegisterPush | kStackSlotPush
  };

  using PushTypeFlags = base::Flags<PushTypeFlag>;

  static bool IsValidPush(InstructionOperand source, PushTypeFlags push_type);

  // Generate a list of moves from an instruction that are candidates to be
  // turned into push instructions on platforms that support them. In general,
  // the list of push candidates are moves to a set of contiguous destination
  // InstructionOperand locations on the stack that don't clobber values that
  // are needed to resolve the gap or use values generated by the gap,
  // i.e. moves that can be hoisted together before the actual gap and assembled
  // together.
  static void GetPushCompatibleMoves(Instruction* instr,
                                     PushTypeFlags push_type,
                                     ZoneVector<MoveOperands*>* pushes);

  class MoveType {
   public:
    enum Type {
      kRegisterToRegister,
      kRegisterToStack,
      kStackToRegister,
      kStackToStack,
      kConstantToRegister,
      kConstantToStack
    };

    // Detect what type of move or swap needs to be performed. Note that these
    // functions do not take into account the representation (Tagged, FP,
    // ...etc).

    static Type InferMove(InstructionOperand* source,
                          InstructionOperand* destination);
    static Type InferSwap(InstructionOperand* source,
                          InstructionOperand* destination);
  };
  // Called before a tail call |instr|'s gap moves are assembled and allows
  // gap-specific pre-processing, e.g. adjustment of the sp for tail calls that
  // need it before gap moves or conversion of certain gap moves into pushes.
  void AssembleTailCallBeforeGap(Instruction* instr,
                                 int first_unused_stack_slot);
  // Called after a tail call |instr|'s gap moves are assembled and allows
  // gap-specific post-processing, e.g. adjustment of the sp for tail calls that
  // need it after gap moves.
  void AssembleTailCallAfterGap(Instruction* instr,
                                int first_unused_stack_slot);

  void FinishCode();
  void MaybeEmitOutOfLineConstantPool();

  void IncrementStackAccessCounter(InstructionOperand* source,
                                   InstructionOperand* destination);

  // ===========================================================================
  // ============== Architecture-specific gap resolver methods. ================
  // ===========================================================================

  // Interface used by the gap resolver to emit moves and swaps.
  void AssembleMove(InstructionOperand* source,
                    InstructionOperand* destination) final;
  void AssembleSwap(InstructionOperand* source,
                    InstructionOperand* destination) final;
  AllocatedOperand Push(InstructionOperand* src) final;
  void Pop(InstructionOperand* src, MachineRepresentation rep) final;
  void PopTempStackSlots() final;
  void MoveToTempLocation(InstructionOperand* src,
                          MachineRepresentation rep) final;
  void MoveTempLocationTo(InstructionOperand* dst,
                          MachineRepresentation rep) final;
  void SetPendingMove(MoveOperands* move) final;

  // ===========================================================================
  // =================== Jump table construction methods. ======================
  // ===========================================================================

  class JumpTable;
  // Adds a jump table that is emitted after the actual code.  Returns label
  // pointing to the beginning of the table.  {targets} is assumed to be static
  // or zone allocated.
  Label* AddJumpTable(base::Vector<Label*> targets);
  // Emits a jump table.
  void AssembleJumpTable(base::Vector<Label*> targets);

  // ===========================================================================
  // ================== Deoptimization table construction. =====================
  // ===========================================================================

  void RecordCallPosition(Instruction* instr);
  void RecordDeoptInfo(Instruction* instr, int pc_offset);
  Handle<DeoptimizationData> GenerateDeoptimizationData();
  int DefineProtectedDeoptimizationLiteral(
      IndirectHandle<TrustedObject> object);
  int DefineDeoptimizationLiteral(DeoptimizationLiteral literal);
  bool HasProtectedDeoptimizationLiteral(
      IndirectHandle<TrustedObject> object) const;
  DeoptimizationEntry const& GetDeoptimizationEntry(Instruction* instr,
                                                    size_t frame_state_offset);

  void BuildTranslationForFrameStateDescriptor(
      FrameStateDescriptor* descriptor, InstructionOperandIterator* iter,
      OutputFrameStateCombine state_combine);
  void TranslateStateValueDescriptor(StateValueDescriptor* desc,
                                     StateValueList* nested,
                                     InstructionOperandIterator* iter);
  void TranslateFrameStateDescriptorOperands(FrameStateDescriptor* desc,
                                             InstructionOperandIterator* iter);
  void AddTranslationForOperand(Instruction* instr, InstructionOperand* op,
                                MachineType type);

  void PrepareForDeoptimizationExits(ZoneDeque<DeoptimizationExit*>* exits);
  DeoptimizationExit* AddDeoptimizationExit(Instruction* instr,
                                            size_t frame_state_offset,
                                            size_t immediate_args_count);

  // ===========================================================================

  struct HandlerInfo {
    // {handler} is nullptr if the Call should lazy deopt on exceptions.
    Label* handler;
    int pc_offset;
  };

  friend class OutOfLineCode;
  friend class CodeGeneratorTester;

  Zone* zone_;
  Isolate* isolate_;
  FrameAccessState* frame_access_state_;
  Linkage* const linkage_;
  InstructionSequence* const instructions_;
  UnwindingInfoWriter unwinding_info_writer_;
  OptimizedCompilationInfo* const info_;
  Label* const labels_;
  Label return_label_;
  RpoNumber current_block_;
  SourcePosition start_source_position_;
  SourcePosition current_source_position_;
  MacroAssembler masm_;
  GapResolver resolver_;
  SafepointTableBuilder safepoints_;
  ZoneVector<HandlerInfo> handlers_;
  int next_deoptimization_id_ = 0;
  int deopt_exit_start_offset_ = 0;
  int eager_deopt_count_ = 0;
  int lazy_deopt_count_ = 0;
  ZoneDeque<DeoptimizationExit*> deoptimization_exits_;
  ZoneDeque<IndirectHandle<TrustedObject>> protected_deoptimization_literals_;
  ZoneDeque<DeoptimizationLiteral> deoptimization_literals_;
  size_t inlined_function_count_ = 0;
  FrameTranslationBuilder translations_;
  int handler_table_offset_ = 0;

  // Deoptimization exits must be as small as possible, since their count grows
  // with function size. {jump_deoptimization_entry_labels_} is an optimization
  // to that effect, which extracts the (potentially large) instruction
  // sequence for the final jump to the deoptimization entry into a single spot
  // per InstructionStream object. All deopt exits can then near-call to this
  // label. Note: not used on all architectures.
  Label jump_deoptimization_entry_labels_[kDeoptimizeKindCount];

  // The maximal combined height of all frames produced upon deoptimization, and
  // the maximal number of pushed arguments for function calls. Applied as an
  // offset to the first stack check of an optimized function.
  const size_t max_unoptimized_frame_height_;
  const size_t max_pushed_argument_count_;

  // The number of incoming parameters for code using JS linkage (i.e.
  // JavaScript functions). Only computed during AssembleCode.
  uint16_t parameter_count_ = 0;

  // kArchCallCFunction could be reached either:
  //   kArchCallCFunction;
  // or:
  //   kArchSaveCallerRegisters;
  //   kArchCallCFunction;
  //   kArchRestoreCallerRegisters;
  // The boolean is used to distinguish the two cases. In the latter case, we
  // also need to decide if FP registers need to be saved, which is controlled
  // by fp_mode_.
  bool caller_registers_saved_;
  SaveFPRegsMode fp_mode_;

  JumpTable* jump_tables_;
  OutOfLineCode* ools_;
  std::optional<OsrHelper> osr_helper_;
  int osr_pc_offset_;
  SourcePositionTableBuilder source_position_table_builder_;
#if V8_ENABLE_WEBASSEMBLY
  ZoneVector<trap_handler::ProtectedInstructionData> protected_instructions_;
#endif  // V8_ENABLE_WEBASSEMBLY
  CodeGenResult result_;
  ZoneVector<int> block_starts_;
  TurbolizerCodeOffsetsInfo offsets_info_;
  ZoneVector<TurbolizerInstructionStartInfo> instr_starts_;
  MoveCycleState move_cycle_;

  const char* debug_name_ = nullptr;
};

}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_BACKEND_CODE_GENERATOR_H_
